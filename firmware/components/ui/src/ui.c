#include "ui/ui.h"
#include "ui_usage.h"
#include "ui_bluetooth.h"
#include "ui_splash.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "splash/splash.h"
#include "esp_log.h"

#define TAG "UI"

static screen_t  s_current    = SCREEN_SPLASH;
static lv_obj_t *s_scr_usage  = NULL;
static lv_obj_t *s_scr_bt     = NULL;
static lv_obj_t *s_scr_splash = NULL;

static void screen_setup(lv_obj_t *s)
{
    lv_obj_set_size(s, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(void)
{
    lv_obj_t *root = lv_screen_active();

    s_scr_usage  = lv_obj_create(root);
    s_scr_bt     = lv_obj_create(root);
    s_scr_splash = lv_obj_create(root);

    screen_setup(s_scr_usage);
    screen_setup(s_scr_bt);
    screen_setup(s_scr_splash);

    ui_usage_init(s_scr_usage);
    ui_bluetooth_init(s_scr_bt);
    ui_splash_init(s_scr_splash);
    splash_init(s_scr_splash);

    ui_show_screen(SCREEN_SPLASH);
    ESP_LOGI(TAG, "UI ready");
}

void ui_show_screen(screen_t screen)
{
    if (lvgl_port_lock(100)) {
        lv_obj_add_flag(s_scr_usage,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_bt,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN);

        switch (screen) {
        case SCREEN_USAGE:     lv_obj_clear_flag(s_scr_usage,  LV_OBJ_FLAG_HIDDEN); splash_hide(); break;
        case SCREEN_BLUETOOTH: lv_obj_clear_flag(s_scr_bt,     LV_OBJ_FLAG_HIDDEN); splash_hide(); break;
        case SCREEN_SPLASH:    lv_obj_clear_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN); splash_show(); break;
        default: break;
        }
        s_current = screen;
        lvgl_port_unlock();
    }
}

void ui_cycle_screen(void)
{
    ui_show_screen((screen_t)((s_current + 1) % SCREEN_COUNT));
}

screen_t ui_get_current_screen(void) { return s_current; }

void ui_update(const usage_data_t *data)
{
    if (lvgl_port_lock(100)) {
        ui_usage_update(data);
        lvgl_port_unlock();
    }
}

void ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac)
{
    if (lvgl_port_lock(100)) {
        ui_bluetooth_update(state, name, mac);
        ui_usage_update_ble(state);
        lvgl_port_unlock();
    }
}
