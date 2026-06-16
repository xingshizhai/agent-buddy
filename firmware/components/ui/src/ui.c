// firmware/components/ui/src/ui.c
#include "ui/ui.h"
#include "ui_usage.h"
#include "ui_bluetooth.h"
#include "ui_splash.h"
#include "ui_settings.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "splash/splash.h"
#include "protocol/usage_data.h"
#include "esp_log.h"
#include <string.h>

#define TAG "UI"
#define MAX_SVC_SCREENS PROTO_MAX_SERVICES

// ── Service screen registry ───────────────────────────────────────────────────

typedef struct {
    lv_obj_t      *screen;
    char           svc_name[16];
    ui_usage_ctx_t ctx;
} svc_screen_t;

static lv_obj_t    *s_scr_bt       = NULL;
static lv_obj_t    *s_scr_splash   = NULL;
static lv_obj_t    *s_scr_settings = NULL;
static svc_screen_t s_svc[MAX_SVC_SCREENS];
static int          s_svc_count    = 0;

static int      s_cycle_idx = 0;   // current service screen index
static screen_t s_current   = SCREEN_SPLASH;
static screen_t s_pre_settings = SCREEN_SERVICE; // screen before entering settings

// ── Swipe detection state ─────────────────────────────────────────────────────
static lv_point_t s_touch_start = {0, 0};
static bool       s_touching    = false;
#define SWIPE_THRESHOLD 50  // pixels

// ── Helpers ───────────────────────────────────────────────────────────────────

static void screen_setup(lv_obj_t *s)
{
    lv_obj_set_size(s, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

static void hide_all(void)
{
    for (int i = 0; i < s_svc_count; i++)
        lv_obj_add_flag(s_svc[i].screen, LV_OBJ_FLAG_HIDDEN);
    if (s_scr_bt)       lv_obj_add_flag(s_scr_bt,       LV_OBJ_FLAG_HIDDEN);
    if (s_scr_splash)   lv_obj_add_flag(s_scr_splash,   LV_OBJ_FLAG_HIDDEN);
    if (s_scr_settings) lv_obj_add_flag(s_scr_settings, LV_OBJ_FLAG_HIDDEN);
}

// ── Swipe callbacks on service screens ───────────────────────────────────────

static void on_svc_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_indev_get_point(indev, &s_touch_start);
    s_touching = true;
}

static void on_svc_released(lv_event_t *e)
{
    if (!s_touching) return;
    s_touching = false;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t end;
    lv_indev_get_point(indev, &end);

    int32_t dx = end.x - s_touch_start.x;
    int32_t dy = end.y - s_touch_start.y;

    // Horizontal swipe only (must be wider than tall)
    if (abs(dx) > SWIPE_THRESHOLD && abs(dx) > abs(dy)) {
        // Swipe left (dx < 0) → next service
        // Swipe right (dx > 0) → previous service
        ui_cycle_service(dx < 0 ? +1 : -1);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ui_init(void)
{
    lv_obj_t *root = lv_screen_active();

    s_scr_bt     = lv_obj_create(root);
    s_scr_splash = lv_obj_create(root);

    screen_setup(s_scr_bt);
    screen_setup(s_scr_splash);

    ui_bluetooth_init(s_scr_bt);
    ui_splash_init(s_scr_splash);
    splash_init(s_scr_splash);

    ui_show_screen(SCREEN_SPLASH);
    ESP_LOGI(TAG, "UI ready");
}

void ui_init_settings(device_settings_t *cfg, void (*on_exit)(void))
{
    if (s_scr_settings) return;   // already initialised

    lv_obj_t *root = lv_screen_active();
    s_scr_settings = lv_obj_create(root);
    screen_setup(s_scr_settings);
    ui_settings_init(s_scr_settings, cfg, on_exit);
    ESP_LOGI(TAG, "Settings screen ready");
}

void ui_register_service_screen(const char *svc_name)
{
    if (s_svc_count >= MAX_SVC_SCREENS) {
        ESP_LOGW(TAG, "Max service screens reached, ignoring %s", svc_name);
        return;
    }
    if (lvgl_port_lock(200)) {
        lv_obj_t *scr = lv_obj_create(lv_screen_active());
        screen_setup(scr);
        ui_usage_init(scr, svc_name, &s_svc[s_svc_count].ctx);
        strlcpy(s_svc[s_svc_count].svc_name, svc_name,
                sizeof(s_svc[s_svc_count].svc_name));
        s_svc[s_svc_count].screen = scr;

        // Register swipe handlers on the screen object
        lv_obj_add_event_cb(scr, on_svc_pressed,  LV_EVENT_PRESSED,  NULL);
        lv_obj_add_event_cb(scr, on_svc_released, LV_EVENT_RELEASED, NULL);

        s_svc_count++;
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Registered service screen: %s (%d total)", svc_name, s_svc_count);
}

void ui_show_screen(screen_t screen)
{
    if (lvgl_port_lock(100)) {
        hide_all();
        switch (screen) {
        case SCREEN_SERVICE:
            if (s_svc_count > 0) {
                lv_obj_clear_flag(s_svc[s_cycle_idx].screen, LV_OBJ_FLAG_HIDDEN);
                splash_hide();
            }
            break;
        case SCREEN_BLUETOOTH:
            lv_obj_clear_flag(s_scr_bt, LV_OBJ_FLAG_HIDDEN);
            splash_hide();
            break;
        case SCREEN_SPLASH:
            lv_obj_clear_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN);
            splash_show();
            break;
        case SCREEN_SETTINGS:
            if (s_scr_settings) {
                lv_obj_clear_flag(s_scr_settings, LV_OBJ_FLAG_HIDDEN);
                splash_hide();
            }
            break;
        }
        s_current = screen;
        lvgl_port_unlock();
    }
}

void ui_show_splash(void)
{
    ui_show_screen(SCREEN_SPLASH);
}

void ui_show_settings(void)
{
    if (!s_scr_settings) return;
    s_pre_settings = s_current;
    ui_show_screen(SCREEN_SETTINGS);
}

void ui_exit_settings(void)
{
    ui_show_screen(s_pre_settings != SCREEN_SETTINGS ? s_pre_settings : SCREEN_SERVICE);
}

// Cycle through service screens only (dir = +1 forward, -1 backward).
void ui_cycle_service(int dir)
{
    if (s_svc_count == 0) return;

    // If not on a service screen, jump to first service screen
    if (s_current != SCREEN_SERVICE) {
        s_cycle_idx = (dir >= 0) ? 0 : s_svc_count - 1;
        ui_show_screen(SCREEN_SERVICE);
        return;
    }
    s_cycle_idx = (s_cycle_idx + dir + s_svc_count) % s_svc_count;
    ui_show_screen(SCREEN_SERVICE);
}

// Legacy: cycles services + BLE screen
void ui_cycle_screen(void)
{
    if (s_svc_count == 0) {
        ui_show_screen(SCREEN_BLUETOOTH);
        return;
    }
    int total = s_svc_count + 1;
    s_cycle_idx = (s_cycle_idx + 1) % total;
    if (s_cycle_idx < s_svc_count)
        ui_show_screen(SCREEN_SERVICE);
    else {
        s_cycle_idx = s_svc_count;
        ui_show_screen(SCREEN_BLUETOOTH);
    }
}

screen_t ui_get_current_screen(void) { return s_current; }

void ui_update(const usage_data_t *data)
{
    for (int i = 0; i < s_svc_count; i++) {
        if (strcmp(s_svc[i].svc_name, data->platform) == 0) {
            if (lvgl_port_lock(100)) {
                ui_usage_update(&s_svc[i].ctx, data);
                lvgl_port_unlock();
            }
            return;
        }
    }
}

void ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac)
{
    if (lvgl_port_lock(100)) {
        ui_bluetooth_update(state, name, mac);
        for (int i = 0; i < s_svc_count; i++)
            ui_usage_update_ble(&s_svc[i].ctx, state);
        lvgl_port_unlock();
    }
}
