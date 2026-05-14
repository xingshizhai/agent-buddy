#include "app_lcd.h"
#include "board_pins.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt1151.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "esp_log.h"

static const char *TAG = "app_lcd";

static lv_display_t *s_disp        = NULL;
static lv_indev_t   *s_touch_indev = NULL;

// ST7262E43 35 Hz timing — from Espressif official lvgl_port RGB example
#define LCD_PANEL_35HZ_RGB_TIMING()  {              \
    .pclk_hz            = 18 * 1000 * 1000,         \
    .h_res              = BSP_LCD_H_RES,             \
    .v_res              = BSP_LCD_V_RES,             \
    .hsync_pulse_width  = 40,                        \
    .hsync_back_porch   = 40,                        \
    .hsync_front_porch  = 48,                        \
    .vsync_pulse_width  = 23,                        \
    .vsync_back_porch   = 32,                        \
    .vsync_front_porch  = 13,                        \
    .flags.pclk_active_neg = true,                   \
}

static esp_err_t lcd_panel_init(esp_lcd_panel_handle_t *out_panel)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src                = LCD_CLK_SRC_PLL160M,
        .dma_burst_size         = 64,
        .data_width             = 16,
        .in_color_format        = LCD_COLOR_FMT_RGB565,  // required in ESP-IDF 6.0
        .num_fbs                = 2,
        .bounce_buffer_size_px  = BSP_LCD_H_RES * 10,
        .disp_gpio_num          = GPIO_NUM_NC,
        .pclk_gpio_num          = BSP_LCD_PCLK,
        .vsync_gpio_num         = BSP_LCD_VSYNC,
        .hsync_gpio_num         = BSP_LCD_HSYNC,
        .de_gpio_num            = BSP_LCD_DE,
        .data_gpio_nums = {
            BSP_LCD_DATA0,  BSP_LCD_DATA1,  BSP_LCD_DATA2,  BSP_LCD_DATA3,
            BSP_LCD_DATA4,  BSP_LCD_DATA5,  BSP_LCD_DATA6,  BSP_LCD_DATA7,
            BSP_LCD_DATA8,  BSP_LCD_DATA9,  BSP_LCD_DATA10, BSP_LCD_DATA11,
            BSP_LCD_DATA12, BSP_LCD_DATA13, BSP_LCD_DATA14, BSP_LCD_DATA15,
        },
        .timings       = LCD_PANEL_35HZ_RGB_TIMING(),
        .flags.fb_in_psram = true,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, out_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*out_panel));
    return ESP_OK;
}

static esp_err_t touch_init(i2c_master_bus_handle_t i2c_bus,
                             esp_lcd_touch_handle_t *out_tp)
{
    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_I2C_FREQ_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max          = BSP_LCD_H_RES,
        .y_max          = BSP_LCD_V_RES,
        .rst_gpio_num   = GPIO_NUM_NC,
        .int_gpio_num   = GPIO_NUM_NC,
        .levels         = {.reset = 0, .interrupt = 0},
        .flags          = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };
    return esp_lcd_touch_new_i2c_gt1151(tp_io, &tp_cfg, out_tp);
}

esp_err_t app_lcd_init(i2c_master_bus_handle_t i2c_bus)
{
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(lcd_panel_init(&panel));
    ESP_LOGI(TAG, "RGB panel ready");

    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(touch_init(i2c_bus, &tp));
    ESP_LOGI(TAG, "GT1151 touch ready");

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority      = 4,
        .task_stack         = 6144,
        .task_affinity      = 1,    // core 1
        .task_max_sleep_ms  = 500,
        .timer_period_ms    = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle   = panel,
        .buffer_size    = BSP_LCD_H_RES * BSP_LCD_V_RES,  // full screen for direct_mode
        .double_buffer  = false,
        .hres           = BSP_LCD_H_RES,
        .vres           = BSP_LCD_V_RES,
        .monochrome     = false,
        .color_format   = LV_COLOR_FORMAT_RGB565,
        .rotation       = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
        .flags          = {
            .buff_dma    = false,
            .buff_spiram = false,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,   // must match bounce_buffer_size_px in panel config
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_disp,
        .handle = tp,
    };
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!s_touch_indev) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL port ready (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

lv_display_t *app_lcd_get_display(void)     { return s_disp; }
lv_indev_t   *app_lcd_get_touch_indev(void) { return s_touch_indev; }
