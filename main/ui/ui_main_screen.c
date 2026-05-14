#include "ui_main_screen.h"
#include "ui.h"
#include "esp_log.h"

static const char *TAG = "ui_main";

// Widget references kept for runtime updates
static lv_obj_t *s_wifi_label  = NULL;
static lv_obj_t *s_status_label = NULL;

static void on_chat_btn(lv_event_t *e)  { (void)e; ui_navigate_to(UI_SCREEN_CHAT);  }
static void on_debug_btn(lv_event_t *e) { (void)e; ui_navigate_to(UI_SCREEN_DEBUG); }

lv_obj_t *ui_main_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ── Status bar (top strip) ──────────────────────────────────────────────
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 4, LV_PART_MAIN);

    // WiFi indicator
    s_wifi_label = lv_label_create(bar);
    lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " --");
    lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_wifi_label, LV_ALIGN_RIGHT_MID, -8, 0);

    // App name in status bar
    lv_obj_t *app_label = lv_label_create(bar);
    lv_label_set_text(app_label, "Agent Buddy");
    lv_obj_set_style_text_color(app_label, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(app_label, LV_ALIGN_LEFT_MID, 8, 0);

    // ── Main content area ───────────────────────────────────────────────────
    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Agent Buddy");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    // Subtitle
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "AI-powered voice & chat assistant");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -44);

    // Status label
    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Initializing...");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x64B5F6), LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 0);

    // Chat button
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 56);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 28, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_chat_btn, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, LV_SYMBOL_AUDIO " Start Chat");
    lv_label_set_long_mode(btn_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(btn_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(btn_label);

    // Debug button
    lv_obj_t *dbg_btn = lv_button_create(scr);
    lv_obj_set_size(dbg_btn, 120, 40);
    lv_obj_align(dbg_btn, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_bg_color(dbg_btn, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_radius(dbg_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_border_color(dbg_btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(dbg_btn, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(dbg_btn, on_debug_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dbg_lbl = lv_label_create(dbg_btn);
    lv_label_set_text(dbg_lbl, LV_SYMBOL_SETTINGS " Debug");
    lv_obj_set_style_text_color(dbg_lbl, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_center(dbg_lbl);

    ESP_LOGI(TAG, "Main screen created");
    return scr;
}

void ui_main_screen_set_wifi(bool connected)
{
    if (!s_wifi_label) return;
    if (connected) {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " OK");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x4CAF50), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " --");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x888888), LV_PART_MAIN);
    }
}

void ui_main_screen_set_status(const char *text)
{
    if (s_status_label) lv_label_set_text(s_status_label, text);
}
