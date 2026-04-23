#include "app_audio.h"
#include "board_pins.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_audio";

static i2s_chan_handle_t        s_tx_chan          = NULL;
static i2s_chan_handle_t        s_rx_chan          = NULL;
static esp_codec_dev_handle_t   s_spk_dev         = NULL;
static esp_codec_dev_handle_t   s_mic_dev         = NULL;
static int                      s_volume          = 75;
static uint32_t                 s_spk_sample_rate = 0;

// ── Custom I²C ctrl for esp_codec_dev using new i2c_master API ───────────────
// The bundled audio_codec_new_i2c_ctrl() uses legacy I2C driver.
// We implement the audio_codec_ctrl_if_t directly over i2c_master_dev_handle_t.

typedef struct {
    audio_codec_ctrl_if_t   base;
    i2c_master_dev_handle_t dev;
    bool                    opened;
} i2c_ctrl_impl_t;

static int i2c_ctrl_open(const audio_codec_ctrl_if_t *self, void *cfg, int cfg_size)
{
    (void)cfg; (void)cfg_size;
    ((i2c_ctrl_impl_t *)self)->opened = true;
    return 0;
}

static bool i2c_ctrl_is_open(const audio_codec_ctrl_if_t *self)
{
    return ((i2c_ctrl_impl_t *)self)->opened;
}

static int i2c_ctrl_read_reg(const audio_codec_ctrl_if_t *self,
                              int reg, int reg_len, void *data, int data_len)
{
    i2c_ctrl_impl_t *impl = (i2c_ctrl_impl_t *)self;
    uint8_t reg_buf[2];
    for (int i = 0; i < reg_len; i++)
        reg_buf[reg_len - 1 - i] = (reg >> (8 * i)) & 0xFF;
    esp_err_t ret = i2c_master_transmit_receive(impl->dev, reg_buf, reg_len,
                                                data, data_len, pdMS_TO_TICKS(50));
    return ret == ESP_OK ? 0 : -1;
}

static int i2c_ctrl_write_reg(const audio_codec_ctrl_if_t *self,
                               int reg, int reg_len, void *data, int data_len)
{
    i2c_ctrl_impl_t *impl = (i2c_ctrl_impl_t *)self;
    uint8_t buf[reg_len + data_len];
    for (int i = 0; i < reg_len; i++)
        buf[reg_len - 1 - i] = (reg >> (8 * i)) & 0xFF;
    memcpy(buf + reg_len, data, data_len);
    esp_err_t ret = i2c_master_transmit(impl->dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
    return ret == ESP_OK ? 0 : -1;
}

static int i2c_ctrl_close(const audio_codec_ctrl_if_t *self)
{
    ((i2c_ctrl_impl_t *)self)->opened = false;
    return 0;
}

static const audio_codec_ctrl_if_t *make_i2c_ctrl(i2c_master_bus_handle_t bus,
                                                   uint16_t addr)
{
    i2c_ctrl_impl_t *impl = calloc(1, sizeof(i2c_ctrl_impl_t));
    if (!impl) return NULL;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &impl->dev) != ESP_OK) {
        free(impl);
        return NULL;
    }

    impl->opened         = true;
    impl->base.open      = i2c_ctrl_open;
    impl->base.is_open   = i2c_ctrl_is_open;
    impl->base.read_reg  = i2c_ctrl_read_reg;
    impl->base.write_reg = i2c_ctrl_write_reg;
    impl->base.close     = i2c_ctrl_close;
    return &impl->base;
}

// ── PA enable via TCA9554 at 0x20 ────────────────────────────────────────────
// TCA9554 regs: 0x01=output, 0x03=direction (0=output). Pin0 = PA enable.
static esp_err_t pa_enable(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_TCA9554_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) return ret;

    uint8_t dir_cmd[] = {0x03, 0xFE};  // pin0 → output
    uint8_t out_cmd[] = {0x01, 0x01};  // pin0 → high (PA on)
    ret = i2c_master_transmit(dev, dir_cmd, sizeof(dir_cmd), pdMS_TO_TICKS(50));
    if (ret != ESP_OK) { i2c_master_bus_rm_device(dev); return ret; }
    ret = i2c_master_transmit(dev, out_cmd, sizeof(out_cmd), pdMS_TO_TICKS(50));
    i2c_master_bus_rm_device(dev);
    if (ret == ESP_OK) ESP_LOGI(TAG, "PA enabled");
    return ret;
}

// ── I²S init ─────────────────────────────────────────────────────────────────
static esp_err_t i2s_init(uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan));

    // TX channel: master, drives all clocks + data out
    i2s_std_config_t tx_cfg = {
        .clk_cfg  = {
            .sample_rate_hz = sample_rate,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_BCLK,
            .ws   = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &tx_cfg));

    // RX channel: reuses TX clocks — set clock GPIOs to UNUSED to avoid conflicts
    i2s_std_config_t rx_cfg = {
        .clk_cfg  = {
            .sample_rate_hz = sample_rate,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws   = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .din  = BSP_I2S_DSIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &rx_cfg));
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t app_audio_init(i2c_master_bus_handle_t i2c_bus)
{
    ESP_ERROR_CHECK(i2s_init(16000));
    ESP_LOGI(TAG, "I2S ready");

    // Separate TX-only interface for speaker — avoids ESP32-S3 paired-channel
    // disable-pending logic in the codec library when mic is also active.
    audio_codec_i2s_cfg_t tx_i2s_cfg = {
        .port       = BSP_I2S_NUM,
        .tx_handle  = s_tx_chan,
        .rx_handle  = NULL,
    };
    const audio_codec_data_if_t *tx_i2s_if = audio_codec_new_i2s_data(&tx_i2s_cfg);

    // Separate RX-only interface for mic
    audio_codec_i2s_cfg_t rx_i2s_cfg = {
        .port       = BSP_I2S_NUM,
        .tx_handle  = NULL,
        .rx_handle  = s_rx_chan,
    };
    const audio_codec_data_if_t *rx_i2s_if = audio_codec_new_i2s_data(&rx_i2s_cfg);

    // ES8311 speaker
    const audio_codec_ctrl_if_t *es8311_ctrl = make_i2c_ctrl(i2c_bus, BSP_ES8311_ADDR);
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if     = es8311_ctrl,
        .gpio_if     = NULL,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk    = true,
        .hw_gain     = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    esp_codec_dev_cfg_t spk_cfg = {
        .dev_type  = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if  = es8311_if,
        .data_if   = tx_i2s_if,
    };
    s_spk_dev = esp_codec_dev_new(&spk_cfg);
    // Open once at 16 kHz and keep open — avoid repeated close/open cycles that
    // interact poorly with the codec library's paired-channel enable-state machine.
    {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16, .channel = 2, .sample_rate = 16000,
        };
        esp_err_t r = esp_codec_dev_open(s_spk_dev, &fs);
        if (r == ESP_OK) {
            s_spk_sample_rate = 16000;
            ESP_LOGI(TAG, "ES8311 speaker ready (opened at 16 kHz)");
        } else {
            ESP_LOGW(TAG, "ES8311 open at init failed (0x%x); will retry on play", r);
        }
    }
    esp_codec_dev_set_out_vol(s_spk_dev, 75);

    // ES7210 mic ADC
    const audio_codec_ctrl_if_t *es7210_ctrl = make_i2c_ctrl(i2c_bus, BSP_ES7210_ADDR);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = es7210_ctrl,
    };
    const audio_codec_if_t *es7210_if = es7210_codec_new(&es7210_cfg);
    esp_codec_dev_cfg_t mic_cfg = {
        .dev_type  = ESP_CODEC_DEV_TYPE_IN,
        .codec_if  = es7210_if,
        .data_if   = rx_i2s_if,
    };
    s_mic_dev = esp_codec_dev_new(&mic_cfg);
    esp_err_t mic_ret = esp_codec_dev_set_in_gain(s_mic_dev, 30.0f);
    if (mic_ret != ESP_OK) {
        ESP_LOGW(TAG, "ES7210 init failed (0x%x) — mic disabled, check I2C wiring", mic_ret);
        s_mic_dev = NULL;
    } else {
        ESP_LOGI(TAG, "ES7210 mic ready");
    }

    esp_err_t pa_ret = pa_enable(i2c_bus);
    if (pa_ret != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 PA enable failed (0x%x) — speaker amp may be off", pa_ret);
    }
    return ESP_OK;
}

esp_err_t app_audio_play_start(uint32_t sample_rate)
{
    if (s_spk_sample_rate != sample_rate) {
        // Sample rate changed — must close and reopen to reconfigure ES8311 + I2S.
        esp_codec_dev_close(s_spk_dev);
        s_spk_sample_rate = 0;
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 2,
            .sample_rate     = sample_rate,
        };
        esp_err_t r = esp_codec_dev_open(s_spk_dev, &fs);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "spk reopen(%lu Hz) failed: 0x%x", sample_rate, r);
            return r;
        }
        s_spk_sample_rate = sample_rate;
    }
    esp_codec_dev_set_out_vol(s_spk_dev, s_volume);
    return ESP_OK;
}

esp_err_t app_audio_play_write(const int16_t *data, size_t samples)
{
    esp_err_t r = esp_codec_dev_write(s_spk_dev, (void *)data, samples * sizeof(int16_t));
    if (r != ESP_OK)
        ESP_LOGW(TAG, "play_write(%zu samples) err 0x%x", samples, r);
    return r;
}

esp_err_t app_audio_play_stop(void)
{
    // Keep the device open — ES8311 and I2S TX stay configured.
    // With auto_clear=true on the I2S channel, DMA drains to silence automatically.
    return ESP_OK;
}

esp_err_t app_audio_play(const int16_t *data, size_t samples, uint32_t sample_rate)
{
    esp_err_t ret = app_audio_play_start(sample_rate);
    if (ret != ESP_OK) return ret;
    ret = app_audio_play_write(data, samples);
    app_audio_play_stop();
    return ret;
}

esp_err_t app_audio_record_start(uint32_t sample_rate)
{
    if (!s_mic_dev) return ESP_ERR_INVALID_STATE;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = sample_rate,
    };
    return esp_codec_dev_open(s_mic_dev, &fs);
}

esp_err_t app_audio_record_read(int16_t *buf, size_t buf_samples, size_t *samples_read)
{
    if (!s_mic_dev) { if (samples_read) *samples_read = 0; return ESP_ERR_INVALID_STATE; }
    size_t bytes = buf_samples * sizeof(int16_t);
    esp_err_t ret = esp_codec_dev_read(s_mic_dev, buf, bytes);
    if (samples_read) *samples_read = (ret == ESP_OK) ? buf_samples : 0;
    return ret;
}

esp_err_t app_audio_record_stop(void)
{
    if (!s_mic_dev) return ESP_OK;
    return esp_codec_dev_close(s_mic_dev);
}

esp_err_t app_audio_set_volume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    return esp_codec_dev_set_out_vol(s_spk_dev, s_volume);
}

int app_audio_get_volume(void)
{
    return s_volume;
}

bool app_audio_mic_available(void)
{
    return s_mic_dev != NULL;
}
