#include "ui_usage.h"
#include "ble/ble_hid.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

#define TAG "UI_USAGE"

// Fonts defined in fonts/ component (no lv_font_ prefix in actual files)
extern const lv_font_t font_styrene_28;
extern const lv_font_t font_styrene_28;
extern const lv_font_t font_styrene_20;
extern const lv_font_t font_mono_18;

#define TOP_H     28
#define BTN_H     40
#define CONTENT_H (BSP_LCD_V_RES - TOP_H - BTN_H)
#define HALF_W    (BSP_LCD_H_RES / 2)
#define ARC_DIAM  100

static lv_obj_t *s_ble_dot;
static lv_obj_t *s_arc_session, *s_arc_weekly;
static lv_obj_t *s_lbl_session_pct, *s_lbl_weekly_pct;
static lv_obj_t *s_lbl_session_reset, *s_lbl_weekly_reset;

static void on_space_pressed(lv_event_t *e)   { ble_hid_key_press(0x2C, 0x00); }
static void on_space_released(lv_event_t *e)  { ble_hid_key_release(); }
static void on_stab_pressed(lv_event_t *e)    { ble_hid_key_press(0x2B, 0x02); }
static void on_stab_released(lv_event_t *e)   { ble_hid_key_release(); }

void ui_usage_init(lv_obj_t *parent)
{
    // Top bar
    lv_obj_t *topbar = lv_obj_create(parent);
    lv_obj_set_size(topbar, BSP_LCD_H_RES, TOP_H);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 4, 0);

    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, "AGENT BUDDY");
    lv_obj_set_style_text_font(title, &font_styrene_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_ble_dot = lv_label_create(topbar);
    lv_label_set_text(s_ble_dot, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble_dot, lv_color_hex(0x888888), 0);
    lv_obj_align(s_ble_dot, LV_ALIGN_RIGHT_MID, -4, 0);

    // Session arc (left half)
    s_arc_session = lv_arc_create(parent);
    lv_obj_set_size(s_arc_session, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(s_arc_session, (HALF_W - ARC_DIAM) / 2, TOP_H + (CONTENT_H - ARC_DIAM) / 2);
    lv_arc_set_range(s_arc_session, 0, 100);
    lv_arc_set_value(s_arc_session, 0);
    lv_obj_remove_style(s_arc_session, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_session, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_session_pct = lv_label_create(parent);
    lv_label_set_text(s_lbl_session_pct, "0%");
    lv_obj_set_style_text_font(s_lbl_session_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(s_lbl_session_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(s_lbl_session_pct, s_arc_session, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_sess = lv_label_create(parent);
    lv_label_set_text(lbl_sess, "Session");
    lv_obj_set_style_text_font(lbl_sess, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_sess, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_sess, s_arc_session, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_lbl_session_reset = lv_label_create(parent);
    lv_label_set_text(s_lbl_session_reset, "");
    lv_obj_set_style_text_font(s_lbl_session_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_session_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(s_lbl_session_reset, lbl_sess, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Weekly arc (right half)
    s_arc_weekly = lv_arc_create(parent);
    lv_obj_set_size(s_arc_weekly, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(s_arc_weekly, HALF_W + (HALF_W - ARC_DIAM) / 2, TOP_H + (CONTENT_H - ARC_DIAM) / 2);
    lv_arc_set_range(s_arc_weekly, 0, 100);
    lv_arc_set_value(s_arc_weekly, 0);
    lv_obj_remove_style(s_arc_weekly, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_weekly, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_weekly_pct = lv_label_create(parent);
    lv_label_set_text(s_lbl_weekly_pct, "0%");
    lv_obj_set_style_text_font(s_lbl_weekly_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(s_lbl_weekly_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(s_lbl_weekly_pct, s_arc_weekly, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_week = lv_label_create(parent);
    lv_label_set_text(lbl_week, "Weekly");
    lv_obj_set_style_text_font(lbl_week, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_week, s_arc_weekly, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_lbl_weekly_reset = lv_label_create(parent);
    lv_label_set_text(s_lbl_weekly_reset, "");
    lv_obj_set_style_text_font(s_lbl_weekly_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_weekly_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(s_lbl_weekly_reset, lbl_week, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // HID button row
    int btn_y = BSP_LCD_V_RES - BTN_H;

    lv_obj_t *btn_space = lv_btn_create(parent);
    lv_obj_set_size(btn_space, HALF_W - 4, BTN_H - 4);
    lv_obj_set_pos(btn_space, 2, btn_y + 2);
    lv_obj_t *lbl_sp = lv_label_create(btn_space);
    lv_label_set_text(lbl_sp, LV_SYMBOL_AUDIO "  Space");
    lv_obj_center(lbl_sp);
    lv_obj_add_event_cb(btn_space, on_space_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(btn_space, on_space_released, LV_EVENT_RELEASED, NULL);

    lv_obj_t *btn_stab = lv_btn_create(parent);
    lv_obj_set_size(btn_stab, HALF_W - 4, BTN_H - 4);
    lv_obj_set_pos(btn_stab, HALF_W + 2, btn_y + 2);
    lv_obj_t *lbl_st = lv_label_create(btn_stab);
    lv_label_set_text(lbl_st, LV_SYMBOL_RIGHT "  Shift+Tab");
    lv_obj_center(lbl_st);
    lv_obj_add_event_cb(btn_stab, on_stab_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(btn_stab, on_stab_released, LV_EVENT_RELEASED, NULL);

    ESP_LOGI(TAG, "usage screen ready");
}

static void fmt_reset(char *buf, size_t len, int mins)
{
    if (mins < 0)       snprintf(buf, len, "-");
    else if (mins < 60) snprintf(buf, len, "%dm", mins);
    else                snprintf(buf, len, "%dh%dm", mins / 60, mins % 60);
}

void ui_usage_update(const usage_data_t *data)
{
    lv_arc_set_value(s_arc_session, (int)data->session_pct);
    lv_arc_set_value(s_arc_weekly,  (int)data->weekly_pct);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)data->session_pct);
    lv_label_set_text(s_lbl_session_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", (int)data->weekly_pct);
    lv_label_set_text(s_lbl_weekly_pct, buf);

    char rbuf[16];
    fmt_reset(rbuf, sizeof(rbuf), data->session_reset_mins);
    lv_label_set_text(s_lbl_session_reset, rbuf);
    fmt_reset(rbuf, sizeof(rbuf), data->weekly_reset_mins);
    lv_label_set_text(s_lbl_weekly_reset, rbuf);
}

void ui_usage_update_ble(ble_gatt_state_t state)
{
    lv_color_t c = (state == BLE_GATT_STATE_CONNECTED)
                   ? lv_color_hex(0x0099FF) : lv_color_hex(0x444444);
    lv_obj_set_style_text_color(s_ble_dot, c, 0);
}
