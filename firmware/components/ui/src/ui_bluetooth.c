#include "ui_bluetooth.h"
#include "bsp/esp-bsp.h"

extern const lv_font_t font_styrene_28;
extern const lv_font_t font_styrene_20;
extern const lv_font_t font_mono_18;

static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_name;
static lv_obj_t *s_lbl_mac;

void ui_bluetooth_init(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_font(title, &font_styrene_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 12);

    s_lbl_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_status, "Status: -");
    lv_obj_set_style_text_font(s_lbl_status, &font_styrene_20, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_LEFT, 12, 60);

    s_lbl_name = lv_label_create(parent);
    lv_label_set_text(s_lbl_name, "Name: -");
    lv_obj_set_style_text_font(s_lbl_name, &font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(0x888888), 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_LEFT, 12, 90);

    s_lbl_mac = lv_label_create(parent);
    lv_label_set_text(s_lbl_mac, "MAC: -");
    lv_obj_set_style_text_font(s_lbl_mac, &font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_mac, lv_color_hex(0x888888), 0);
    lv_obj_align(s_lbl_mac, LV_ALIGN_TOP_LEFT, 12, 112);

    lv_obj_t *btn_reset = lv_btn_create(parent);
    lv_obj_set_size(btn_reset, 140, 36);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_t *lbl_r = lv_label_create(btn_reset);
    lv_label_set_text(lbl_r, "Reset Bond");
    lv_obj_center(lbl_r);
}

void ui_bluetooth_update(ble_gatt_state_t state, const char *name, const char *mac)
{
    const char *status_str = (state == BLE_GATT_STATE_CONNECTED)   ? "Connected"   :
                             (state == BLE_GATT_STATE_ADVERTISING)  ? "Advertising" : "Disconnected";
    lv_label_set_text_fmt(s_lbl_status, "Status: %s", status_str);
    lv_label_set_text_fmt(s_lbl_name,   "Name: %s",   name ? name : "-");
    lv_label_set_text_fmt(s_lbl_mac,    "MAC: %s",    mac  ? mac  : "-");
}
