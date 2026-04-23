#include "ui_chat_screen.h"
#include "ui.h"
#include "app_ai.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_chat";

static lv_obj_t *s_msg_list   = NULL;
static lv_obj_t *s_mic_btn    = NULL;
static lv_obj_t *s_mic_label  = NULL;
static lv_obj_t *s_status_lbl = NULL;

// ── AI callbacks (called on LVGL task via lv_async_call) ─────────────────────

static void on_ai_state(ai_state_t state)
{
    if (!s_mic_btn) return;
    switch (state) {
    case AI_STATE_DISCONNECTED:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0x555555), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_AUDIO);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Disconnected");
        break;
    case AI_STATE_CONNECTING:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0x888800), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_AUDIO);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Connecting...");
        break;
    case AI_STATE_IDLE:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_AUDIO);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Tap to speak");
        break;
    case AI_STATE_RECORDING:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0xFF4444), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_STOP);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Listening...");
        break;
    case AI_STATE_PROCESSING:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0x444488), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_REFRESH);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Processing...");
        break;
    case AI_STATE_PLAYING:
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0x226622), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_VOLUME_MAX);
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "Speaking...");
        break;
    }
}

static void on_transcript(const char *text)
{
    ui_chat_append_message("user", text);
}

static void on_response(const char *text)
{
    ui_chat_append_message("assistant", text);
}

static void on_status(const char *msg)
{
    if (s_status_lbl) lv_label_set_text(s_status_lbl, msg);
}

// ── Button handlers ───────────────────────────────────────────────────────────

static void on_back_btn(lv_event_t *e)
{
    (void)e;
    // Stop any active recording before navigating away
    if (app_ai_get_state() == AI_STATE_RECORDING)
        app_ai_stop_recording();
    ui_navigate_to(UI_SCREEN_MAIN);
}

static void on_mic_btn(lv_event_t *e)
{
    (void)e;
    ai_state_t state = app_ai_get_state();
    if (state == AI_STATE_IDLE) {
        esp_err_t r = app_ai_start_recording();
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "start_recording failed: 0x%x", r);
            if (s_status_lbl) lv_label_set_text(s_status_lbl, "Mic error");
        }
    } else if (state == AI_STATE_RECORDING) {
        app_ai_stop_recording();
    }
}

// ── Screen creation ───────────────────────────────────────────────────────────

lv_obj_t *ui_chat_screen_create(void)
{
    // Register AI callbacks
    app_ai_set_state_cb(on_ai_state);
    app_ai_set_transcript_cb(on_transcript);
    app_ai_set_response_cb(on_response);
    app_ai_set_status_cb(on_status);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ── Top bar ──────────────────────────────────────────────────────────────
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);

    lv_obj_t *back_btn = lv_button_create(bar);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, on_back_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Chat");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // ── Message list ─────────────────────────────────────────────────────────
    s_msg_list = lv_obj_create(scr);
    lv_obj_set_size(s_msg_list, LV_PCT(100), 480 - 48 - 90);
    lv_obj_align(s_msg_list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(s_msg_list, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_msg_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_msg_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_msg_list, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msg_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_list, LV_SCROLLBAR_MODE_AUTO);

    ui_chat_append_message("system", "Hello! Tap the mic to start talking.");

    // ── Bottom bar (status + mic button) ─────────────────────────────────────
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_size(bottom, LV_PCT(100), 90);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bottom, 0, LV_PART_MAIN);

    // Status label (left of mic button)
    s_status_lbl = lv_label_create(bottom);
    lv_label_set_text(s_status_lbl, "Connecting...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 12, 0);

    // Mic button (centered)
    s_mic_btn = lv_button_create(bottom);
    lv_obj_set_size(s_mic_btn, 64, 64);
    lv_obj_align(s_mic_btn, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_radius(s_mic_btn, 32, LV_PART_MAIN);
    lv_obj_add_event_cb(s_mic_btn, on_mic_btn, LV_EVENT_CLICKED, NULL);

    s_mic_label = lv_label_create(s_mic_btn);
    lv_label_set_text(s_mic_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(s_mic_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mic_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(s_mic_label);

    ESP_LOGI(TAG, "Chat screen created");
    return scr;
}

// ── Public helpers ────────────────────────────────────────────────────────────

void ui_chat_append_message(const char *role, const char *text)
{
    if (!s_msg_list) return;

    bool is_user = (strcmp(role, "user") == 0);

    lv_obj_t *bubble = lv_obj_create(s_msg_list);
    lv_obj_set_width(bubble, LV_PCT(85));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bubble, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(bubble, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(bubble, 0, LV_PART_MAIN);

    if (is_user) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x16213E), LV_PART_MAIN);
        lv_obj_align(bubble, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x2D2D44), LV_PART_MAIN);
        lv_obj_set_width(bubble, LV_PCT(95));
    }

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), LV_PART_MAIN);

    lv_obj_scroll_to_y(s_msg_list, LV_COORD_MAX, LV_ANIM_ON);
}

void ui_chat_set_recording(bool recording)
{
    on_ai_state(recording ? AI_STATE_RECORDING : AI_STATE_IDLE);
}
