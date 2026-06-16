// firmware/components/ui/src/ui_settings.c
// Settings screen: brightness, dim timeout, BLE name (read-only).
// Layout: header + three tappable rows + Save & Exit button.
// Screen size: 320×240.
#include "ui_settings.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "UI_SET"

extern const lv_font_t font_styrene_20;
extern const lv_font_t font_mono_18;

// ── Brightness options ────────────────────────────────────────────────────────
static const uint8_t  BRIGHT_VALS[]  = {20, 40, 60, 80, 100};
static const char    *BRIGHT_LBLS[]  = {"20%", "40%", "60%", "80%", "100%"};
#define BRIGHT_N  (sizeof(BRIGHT_VALS)/sizeof(BRIGHT_VALS[0]))

// ── Dim timeout options ───────────────────────────────────────────────────────
static const uint16_t DIM_VALS[]  = {0, 300, 600, 900, 1800, 3600};
static const char    *DIM_LBLS[]  = {"Off", "5 min", "10 min", "15 min", "30 min", "60 min"};
#define DIM_N  (sizeof(DIM_VALS)/sizeof(DIM_VALS[0]))

// ── State ─────────────────────────────────────────────────────────────────────
static device_settings_t *s_cfg   = NULL;
static void (*s_on_exit)(void)     = NULL;

static lv_obj_t *s_lbl_bright_val = NULL;
static lv_obj_t *s_lbl_dim_val    = NULL;
static lv_obj_t *s_lbl_ble_val    = NULL;

static int s_bright_idx = 0;
static int s_dim_idx    = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

static int find_bright_idx(uint8_t v)
{
    for (int i = 0; i < (int)BRIGHT_N; i++)
        if (BRIGHT_VALS[i] == v) return i;
    return (int)BRIGHT_N - 1;  // default to highest
}

static int find_dim_idx(uint16_t v)
{
    for (int i = 0; i < (int)DIM_N; i++)
        if (DIM_VALS[i] == v) return i;
    return 3;  // default 15 min
}

// ── Row builder ───────────────────────────────────────────────────────────────

typedef struct { lv_event_cb_t cb_left; lv_event_cb_t cb_right; } row_cbs_t;

// Creates a settings row: [< label       value >]
// Returns pointer to the value label.
static lv_obj_t *make_row(lv_obj_t *parent, int y, const char *label,
                           const char *value, lv_event_cb_t cb_dec,
                           lv_event_cb_t cb_inc, bool readonly)
{
    // Row background
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, BSP_LCD_H_RES - 16, 44);
    lv_obj_set_pos(row, 8, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Label (left)
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

    // Value (right)
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, &font_mono_18, 0);
    lv_obj_set_style_text_color(val,
        readonly ? lv_color_hex(0x555555) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -10, 0);

    if (!readonly) {
        // Left half decrements, right half increments
        lv_obj_t *btn_dec = lv_obj_create(row);
        lv_obj_set_size(btn_dec, (BSP_LCD_H_RES - 16) / 2, 44);
        lv_obj_set_pos(btn_dec, 0, 0);
        lv_obj_set_style_bg_opa(btn_dec, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_dec, 0, 0);
        lv_obj_add_event_cb(btn_dec, cb_dec, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_inc = lv_obj_create(row);
        lv_obj_set_size(btn_inc, (BSP_LCD_H_RES - 16) / 2, 44);
        lv_obj_set_pos(btn_inc, (BSP_LCD_H_RES - 16) / 2, 0);
        lv_obj_set_style_bg_opa(btn_inc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_inc, 0, 0);
        lv_obj_add_event_cb(btn_inc, cb_inc, LV_EVENT_CLICKED, NULL);
    }

    return val;
}

// ── Event callbacks ───────────────────────────────────────────────────────────

static void on_bright_dec(lv_event_t *e)
{
    s_bright_idx = (s_bright_idx - 1 + (int)BRIGHT_N) % (int)BRIGHT_N;
    s_cfg->brightness = BRIGHT_VALS[s_bright_idx];
    lv_label_set_text(s_lbl_bright_val, BRIGHT_LBLS[s_bright_idx]);
    bsp_display_brightness_set(s_cfg->brightness);
}

static void on_bright_inc(lv_event_t *e)
{
    s_bright_idx = (s_bright_idx + 1) % (int)BRIGHT_N;
    s_cfg->brightness = BRIGHT_VALS[s_bright_idx];
    lv_label_set_text(s_lbl_bright_val, BRIGHT_LBLS[s_bright_idx]);
    bsp_display_brightness_set(s_cfg->brightness);
}

static void on_dim_dec(lv_event_t *e)
{
    s_dim_idx = (s_dim_idx - 1 + (int)DIM_N) % (int)DIM_N;
    s_cfg->screen_dim_secs = DIM_VALS[s_dim_idx];
    lv_label_set_text(s_lbl_dim_val, DIM_LBLS[s_dim_idx]);
}

static void on_dim_inc(lv_event_t *e)
{
    s_dim_idx = (s_dim_idx + 1) % (int)DIM_N;
    s_cfg->screen_dim_secs = DIM_VALS[s_dim_idx];
    lv_label_set_text(s_lbl_dim_val, DIM_LBLS[s_dim_idx]);
}

static void on_save_exit(lv_event_t *e)
{
    if (s_on_exit) s_on_exit();
}

// ── Public API ────────────────────────────────────────────────────────────────

void ui_settings_init(lv_obj_t *parent, device_settings_t *cfg,
                      void (*on_exit)(void))
{
    s_cfg     = cfg;
    s_on_exit = on_exit;

    s_bright_idx = find_bright_idx(cfg->brightness);
    s_dim_idx    = find_dim_idx(cfg->screen_dim_secs);

    // Header
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, BSP_LCD_H_RES, 30);
    lv_obj_set_pos(hdr, 0, 2);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 4, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_font(title, &font_styrene_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    // Hint label
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Tap left/right side of row to adjust");
    lv_obj_set_style_text_font(hint, &font_mono_18, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x444444), 0);
    lv_obj_set_width(hint, BSP_LCD_H_RES - 16);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 34);

    // Row y positions
    int y = 56;

    // Brightness row
    s_lbl_bright_val = make_row(parent, y, "Brightness",
                                BRIGHT_LBLS[s_bright_idx],
                                on_bright_dec, on_bright_inc, false);
    y += 50;

    // Dim timeout row
    s_lbl_dim_val = make_row(parent, y, "Screen Off",
                             DIM_LBLS[s_dim_idx],
                             on_dim_dec, on_dim_inc, false);
    y += 50;

    // BLE name row (read-only)
    s_lbl_ble_val = make_row(parent, y, "BLE Name",
                             cfg->ble_name, NULL, NULL, true);
    y += 50;

    // Save & Exit button
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 160, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0077CC), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, on_save_exit, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Save & Exit");
    lv_obj_set_style_text_font(btn_lbl, &font_styrene_20, 0);
    lv_obj_center(btn_lbl);

    ESP_LOGI(TAG, "Settings screen ready");
}

void ui_settings_refresh(void)
{
    if (!s_cfg || !s_lbl_bright_val) return;
    s_bright_idx = find_bright_idx(s_cfg->brightness);
    s_dim_idx    = find_dim_idx(s_cfg->screen_dim_secs);
    lv_label_set_text(s_lbl_bright_val, BRIGHT_LBLS[s_bright_idx]);
    lv_label_set_text(s_lbl_dim_val,    DIM_LBLS[s_dim_idx]);
    lv_label_set_text(s_lbl_ble_val,    s_cfg->ble_name);
}
