// firmware/main/main.c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "icm42670.h"
#include "ble/ble_gatt.h"
#include "ble/ble_hid.h"
#include "protocol/protocol.h"
#include "protocol/usage_data.h"
#include "ui/ui.h"
#include "splash/splash.h"
#include "splash/usage_rate.h"

#define TAG "MAIN"

// ── Kconfig-derived constants ─────────────────────────────────────────────────
#define DATA_TIMEOUT_MS      ((uint32_t)CONFIG_AGENT_BUDDY_DATA_TIMEOUT_S    * 1000UL)
#define SCREEN_DIM_TIMEOUT_MS ((uint32_t)CONFIG_AGENT_BUDDY_SCREEN_DIM_TIMEOUT_S * 1000UL)

// ── State ─────────────────────────────────────────────────────────────────────
static uint32_t        s_last_data_ms = 0;
static service_store_t s_store;
static bool            s_screen_on   = true;

// ── Hardware handles ──────────────────────────────────────────────────────────
static icm42670_handle_t s_imu_handle = NULL;
static lv_display_t     *s_lv_disp   = NULL;

// ── Screen helpers ────────────────────────────────────────────────────────────

static void screen_on(void)
{
    bsp_display_brightness_set(CONFIG_AGENT_BUDDY_DISPLAY_BRIGHTNESS);
    s_screen_on = true;
    // Reset LVGL inactivity timer so the dim timeout restarts from zero
    if (lvgl_port_lock(50)) {
        lv_display_trigger_activity(s_lv_disp);
        lvgl_port_unlock();
    }
}

static void screen_off(void)
{
    bsp_display_brightness_set(0);
    s_screen_on = false;
    ESP_LOGI(TAG, "screen dimmed after %lus inactivity",
             (unsigned long)CONFIG_AGENT_BUDDY_SCREEN_DIM_TIMEOUT_S);
}

// ── IMU auto-rotation ─────────────────────────────────────────────────────────

static void imu_rotation_check(void)
{
    if (!s_imu_handle) return;
    static lv_disp_rotation_t s_last_rot = LV_DISP_ROTATION_180;
    static uint32_t s_last_ms = 0;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_ms < 500) return;
    s_last_ms = now;

    icm42670_value_t accel = {0};
    if (icm42670_get_acce_value(s_imu_handle, &accel) != ESP_OK) return;

    lv_disp_rotation_t rot = (accel.y > 0.5f) ? LV_DISP_ROTATION_180 : LV_DISP_ROTATION_0;
    if (rot == s_last_rot) return;

    ESP_LOGI(TAG, "rotation %d→%d (ay=%.2f)", s_last_rot, rot, accel.y);
    s_last_rot = rot;

    bsp_display_brightness_set(0);
    if (lvgl_port_lock(200)) {
        lv_display_set_rotation(s_lv_disp, rot);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    if (s_screen_on)
        bsp_display_brightness_set(CONFIG_AGENT_BUDDY_DISPLAY_BRIGHTNESS);
}

// ── Button callback ───────────────────────────────────────────────────────────

static void on_main_button(void *arg, void *data)
{
    if (!s_screen_on) {
        // First press only wakes the screen; does not navigate
        screen_on();
        ESP_LOGI(TAG, "screen woken by button");
        return;
    }

    // Reset inactivity timer on every button press
    if (lvgl_port_lock(50)) {
        lv_display_trigger_activity(s_lv_disp);
        lvgl_port_unlock();
    }

    if (ui_get_current_screen() == SCREEN_SPLASH) {
        if (lvgl_port_lock(100)) { splash_next(); lvgl_port_unlock(); }
    } else {
        ui_cycle_screen();
    }
}

// ── app_main ──────────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "Agent Buddy starting...");
    ESP_LOGI(TAG, "BLE name   : %s",   CONFIG_AGENT_BUDDY_BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "Brightness : %d%%", CONFIG_AGENT_BUDDY_DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "Data t/o   : %ds",  CONFIG_AGENT_BUDDY_DATA_TIMEOUT_S);
    ESP_LOGI(TAG, "Screen dim : %ds",  CONFIG_AGENT_BUDDY_SCREEN_DIM_TIMEOUT_S);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    store_init(&s_store);

    bsp_i2c_init();

    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size    = BSP_LCD_H_RES * 40,
        .double_buffer  = true,
        .flags          = { .buff_dma = true, .buff_spiram = false },
    };
    s_lv_disp = bsp_display_start_with_config(&disp_cfg);
    bsp_display_brightness_set(CONFIG_AGENT_BUDDY_DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "Display: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    if (icm42670_create(i2c_bus, ICM42670_I2C_ADDRESS, &s_imu_handle) == ESP_OK) {
        icm42670_cfg_t imu_cfg = {
            .acce_fs = ACCE_FS_4G,  .acce_odr = ACCE_ODR_100HZ,
            .gyro_fs = GYRO_FS_500DPS, .gyro_odr = GYRO_ODR_100HZ,
        };
        icm42670_config(s_imu_handle, &imu_cfg);
        ESP_LOGI(TAG, "IMU ready");
    } else {
        ESP_LOGW(TAG, "IMU init failed — auto-rotation disabled");
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MAIN])
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_SINGLE_CLICK,
                               NULL, on_main_button, NULL);

    ble_gatt_init(CONFIG_AGENT_BUDDY_BLE_DEVICE_NAME);

    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_init();
        ui_register_service_screen("claude");
        lv_timer_create(splash_tick, 80, NULL);
        lvgl_port_unlock();
    }

    ui_update_ble_status(ble_gatt_get_state(),
                         ble_gatt_get_name(),
                         ble_gatt_get_mac());

    ESP_LOGI(TAG, "Ready. Waiting for BLE data...");

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (1) {
        // ── BLE data ──────────────────────────────────────────────────────────
        if (ble_gatt_has_data()) {
            const char *json = ble_gatt_get_data();
            if (json) {
                proto_envelope_t env  = {0};
                usage_data_t     data = {0};

                if (protocol_parse(json, &env, &data)) {
                    store_set(&s_store, &data);
                    usage_rate_sample(data.session_pct);
                    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);

                    ui_update(&data);

                    if (ui_get_current_screen() == SCREEN_SPLASH)
                        ui_show_screen(SCREEN_SERVICE);

                    // Wake screen on incoming data
                    if (!s_screen_on) screen_on();

                    ble_gatt_send_ack(true);
                    ESP_LOGI(TAG, "data[%s]: s=%.0f%% w=%.0f%% st=%s",
                             data.platform, data.session_pct,
                             data.weekly_pct, data.status);
                } else {
                    ble_gatt_send_err(
                        env.type == MSG_UNKNOWN ? 2 : 1,
                        env.type == MSG_UNKNOWN ? "unsupported svc" : "parse error");
                    ESP_LOGW(TAG, "protocol_parse failed (svc=%s)", env.svc);
                }
            }
        }

        // ── BLE state change ──────────────────────────────────────────────────
        static ble_gatt_state_t s_last_ble = BLE_GATT_STATE_INIT;
        ble_gatt_state_t cur_ble = ble_gatt_get_state();
        if (cur_ble != s_last_ble) {
            s_last_ble = cur_ble;
            ui_update_ble_status(cur_ble, ble_gatt_get_name(), ble_gatt_get_mac());
            ESP_LOGI(TAG, "BLE state: %d", cur_ble);
            if (cur_ble == BLE_GATT_STATE_DISCONNECTED &&
                ui_get_current_screen() != SCREEN_SPLASH) {
                ui_show_screen(SCREEN_SPLASH);
                s_last_data_ms = 0;
            }
        }

        // ── Data timeout → splash ─────────────────────────────────────────────
        if (s_last_data_ms > 0 && ui_get_current_screen() != SCREEN_SPLASH) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if ((now_ms - s_last_data_ms) > DATA_TIMEOUT_MS) {
                ESP_LOGI(TAG, "No data for %lus, returning to splash",
                         (unsigned long)CONFIG_AGENT_BUDDY_DATA_TIMEOUT_S);
                ui_show_screen(SCREEN_SPLASH);
                s_last_data_ms = 0;
            }
        }

        // ── Screen dim / wake ─────────────────────────────────────────────────
        if (CONFIG_AGENT_BUDDY_SCREEN_DIM_TIMEOUT_S > 0) {
            uint32_t inactive_ms = 0;
            if (lvgl_port_lock(20)) {
                inactive_ms = lv_display_get_inactive_time(s_lv_disp);
                lvgl_port_unlock();
            }
            if (s_screen_on && inactive_ms >= SCREEN_DIM_TIMEOUT_MS) {
                screen_off();
            } else if (!s_screen_on && inactive_ms < SCREEN_DIM_TIMEOUT_MS) {
                // LVGL detected touch (inactivity timer was reset by indev)
                screen_on();
                ESP_LOGI(TAG, "screen woken by touch");
            }
        }

        imu_rotation_check();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
