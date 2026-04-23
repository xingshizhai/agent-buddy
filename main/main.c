#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "board_pins.h"
#include "app_lcd.h"
#include "app_audio.h"
#include "app_wifi.h"
#include "app_ai.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_main_screen.h"
#include "ui_chat_screen.h"
#include "ui_debug_screen.h"
#include "ui_debug_audio.h"
#include "ui_debug_wifi.h"
#include "ui_debug_bt.h"

static const char *TAG = "main";

ui_state_t g_ui_state = {0};

static lv_obj_t *s_screens[UI_SCREEN_COUNT];

void ui_navigate_to(ui_screen_id_t screen)
{
    if (screen >= UI_SCREEN_COUNT || !s_screens[screen]) return;
    lv_screen_load_anim(s_screens[screen], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void ui_update_wifi_status(bool connected)
{
    g_ui_state.wifi_connected = connected;
    lvgl_port_lock(0);
    ui_main_screen_set_wifi(connected);
    ui_main_screen_set_status(connected ? "Ready" : "No network");
    lvgl_port_unlock();
}

static void ai_init_task(void *arg)
{
    (void)arg;
    app_ai_init();
    vTaskDelete(NULL);
}

static void on_wifi_status(app_wifi_status_t status, void *ctx)
{
    (void)ctx;
    switch (status) {
    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected");
        ui_update_wifi_status(true);
        // Defer app_ai_init out of the WiFi event task to avoid starving IDLE
        xTaskCreate(ai_init_task, "ai_init", 4096, NULL, 3, NULL);
        break;
    case WIFI_STATUS_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected");
        ui_update_wifi_status(false);
        break;
    case WIFI_STATUS_CONNECTING:
        lvgl_port_lock(0);
        ui_main_screen_set_status("Connecting to WiFi...");
        lvgl_port_unlock();
        break;
    default:
        break;
    }
}

static i2c_master_bus_handle_t s_i2c_bus;

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = BSP_I2C_PORT,
        .scl_io_num                   = BSP_I2C_SCL,
        .sda_io_num                   = BSP_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Agent Buddy starting");

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "I2C bus ready (SCL=%d SDA=%d)", BSP_I2C_SCL, BSP_I2C_SDA);

    ESP_ERROR_CHECK(app_lcd_init(s_i2c_bus));

    lvgl_port_lock(0);
    lv_obj_t *default_scr = lv_screen_active();
    lv_obj_set_style_bg_color(default_scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(default_scr, LV_OPA_COVER, LV_PART_MAIN);

    s_screens[UI_SCREEN_MAIN]        = ui_main_screen_create();
    s_screens[UI_SCREEN_CHAT]        = ui_chat_screen_create();
    s_screens[UI_SCREEN_DEBUG]       = ui_debug_screen_create();
    s_screens[UI_SCREEN_DEBUG_AUDIO] = ui_debug_audio_screen_create();
    s_screens[UI_SCREEN_DEBUG_WIFI]  = ui_debug_wifi_screen_create();
    s_screens[UI_SCREEN_DEBUG_BT]    = ui_debug_bt_screen_create();

    lv_screen_load(s_screens[UI_SCREEN_MAIN]);
    lvgl_port_unlock();

    ESP_ERROR_CHECK(app_audio_init(s_i2c_bus));

    app_wifi_set_status_cb(on_wifi_status, NULL);
    ESP_ERROR_CHECK(app_wifi_init());
    ESP_ERROR_CHECK(app_wifi_connect(CONFIG_AGENT_BUDDY_WIFI_SSID,
                                     CONFIG_AGENT_BUDDY_WIFI_PASSWORD));

    ESP_LOGI(TAG, "Init complete — running");
}
