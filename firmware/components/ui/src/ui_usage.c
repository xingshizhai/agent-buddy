// firmware/components/ui/src/ui_usage.c
#include "ui_usage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "UI_USAGE"

extern const lv_font_t font_styrene_28;
extern const lv_font_t font_styrene_20;
extern const lv_font_t font_mono_18;

#define TOP_OFFSET  2
#define TOP_H       28
#define CONTENT_TOP (TOP_OFFSET + TOP_H)
#define CONTENT_H   (BSP_LCD_V_RES - CONTENT_TOP)
#define HALF_W      (BSP_LCD_H_RES / 2)
#define ARC_DIAM    100
#define ARC_Y       (CONTENT_TOP + (CONTENT_H - ARC_DIAM) / 2 - 10)

static lv_color_t arc_color_for_pct(int pct)
{
    if (pct >= 85) return lv_color_hex(0xFF3333);
    if (pct >= 60) return lv_color_hex(0xFFAA00);
    return lv_color_hex(0x00CC66);
}

void ui_usage_init(lv_obj_t *parent, const char *svc_name, ui_usage_ctx_t *ctx)
{
    // Top bar
    lv_obj_t *topbar = lv_obj_create(parent);
    lv_obj_set_size(topbar, BSP_LCD_H_RES, TOP_H);
    lv_obj_set_pos(topbar, 0, TOP_OFFSET);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 4, 0);

    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, svc_name ? svc_name : "UNKNOWN");
    lv_obj_set_style_text_font(title, &font_styrene_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    ctx->ble_dot = lv_label_create(topbar);
    lv_label_set_text(ctx->ble_dot, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(ctx->ble_dot, lv_color_hex(0x888888), 0);
    lv_obj_align(ctx->ble_dot, LV_ALIGN_RIGHT_MID, -4, 0);

    // Session arc (left half)
    ctx->arc_session = lv_arc_create(parent);
    lv_obj_set_size(ctx->arc_session, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(ctx->arc_session, (HALF_W - ARC_DIAM) / 2, ARC_Y);
    lv_arc_set_range(ctx->arc_session, 0, 100);
    lv_arc_set_value(ctx->arc_session, 0);
    lv_obj_remove_style(ctx->arc_session, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ctx->arc_session, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ctx->arc_session, arc_color_for_pct(0), LV_PART_INDICATOR);

    ctx->lbl_session_pct = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_session_pct, "0%");
    lv_obj_set_style_text_font(ctx->lbl_session_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(ctx->lbl_session_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(ctx->lbl_session_pct, ctx->arc_session, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_sess = lv_label_create(parent);
    lv_label_set_text(lbl_sess, "Session");
    lv_obj_set_style_text_font(lbl_sess, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_sess, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_sess, ctx->arc_session, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    ctx->lbl_session_reset = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_session_reset, "");
    lv_obj_set_style_text_font(ctx->lbl_session_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_session_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(ctx->lbl_session_reset, lbl_sess, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Weekly arc (right half)
    ctx->arc_weekly = lv_arc_create(parent);
    lv_obj_set_size(ctx->arc_weekly, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(ctx->arc_weekly, HALF_W + (HALF_W - ARC_DIAM) / 2, ARC_Y);
    lv_arc_set_range(ctx->arc_weekly, 0, 100);
    lv_arc_set_value(ctx->arc_weekly, 0);
    lv_obj_remove_style(ctx->arc_weekly, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ctx->arc_weekly, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ctx->arc_weekly, arc_color_for_pct(0), LV_PART_INDICATOR);

    ctx->lbl_weekly_pct = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_weekly_pct, "0%");
    lv_obj_set_style_text_font(ctx->lbl_weekly_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(ctx->lbl_weekly_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(ctx->lbl_weekly_pct, ctx->arc_weekly, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_week = lv_label_create(parent);
    lv_label_set_text(lbl_week, "Weekly");
    lv_obj_set_style_text_font(lbl_week, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_week, ctx->arc_weekly, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    ctx->lbl_weekly_reset = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_weekly_reset, "");
    lv_obj_set_style_text_font(ctx->lbl_weekly_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_weekly_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(ctx->lbl_weekly_reset, lbl_week, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Status label (bottom-left)
    ctx->lbl_status = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_status, "");
    lv_obj_set_style_text_font(ctx->lbl_status, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_status, lv_color_hex(0x666666), 0);
    lv_obj_align(ctx->lbl_status, LV_ALIGN_BOTTOM_LEFT, 8, -6);

    ESP_LOGI(TAG, "usage screen ready: %s", svc_name ? svc_name : "?");
}

static void fmt_reset(char *buf, size_t len, int mins)
{
    if (mins < 0)       snprintf(buf, len, "-");
    else if (mins < 60) snprintf(buf, len, "%dm", mins);
    else                snprintf(buf, len, "%dh%dm", mins / 60, mins % 60);
}

void ui_usage_update(ui_usage_ctx_t *ctx, const usage_data_t *data)
{
    int sp = (int)data->session_pct;
    int wp = (int)data->weekly_pct;

    lv_arc_set_value(ctx->arc_session, sp);
    lv_arc_set_value(ctx->arc_weekly,  wp);
    lv_obj_set_style_arc_color(ctx->arc_session, arc_color_for_pct(sp), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ctx->arc_weekly,  arc_color_for_pct(wp), LV_PART_INDICATOR);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", sp);
    lv_label_set_text(ctx->lbl_session_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", wp);
    lv_label_set_text(ctx->lbl_weekly_pct, buf);

    char rbuf[16];
    fmt_reset(rbuf, sizeof(rbuf), data->session_reset_mins);
    lv_label_set_text(ctx->lbl_session_reset, rbuf);
    fmt_reset(rbuf, sizeof(rbuf), data->weekly_reset_mins);
    lv_label_set_text(ctx->lbl_weekly_reset, rbuf);

    // Status label with color
    bool limited = (data->status[0] == 'l');  // "limited"
    lv_obj_set_style_text_color(ctx->lbl_status,
        limited ? lv_color_hex(0xFF3333) : lv_color_hex(0x00CC66), 0);
    lv_label_set_text(ctx->lbl_status, data->status);
}

void ui_usage_update_ble(ui_usage_ctx_t *ctx, ble_gatt_state_t state)
{
    lv_color_t c = (state == BLE_GATT_STATE_CONNECTED)
                   ? lv_color_hex(0x0099FF) : lv_color_hex(0x444444);
    lv_obj_set_style_text_color(ctx->ble_dot, c, 0);
}
