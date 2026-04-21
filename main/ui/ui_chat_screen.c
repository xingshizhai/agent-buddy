#include "ui_chat_screen.h"
#include "ui.h"
#include "app_audio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_chat";

static lv_obj_t *s_msg_list   = NULL;   // scrollable message container
static lv_obj_t *s_mic_btn    = NULL;
static lv_obj_t *s_mic_label  = NULL;
static bool      s_recording  = false;

static void on_back_btn(lv_event_t *e)
{
    (void)e;
    ui_navigate_to(UI_SCREEN_MAIN);
}

static void on_mic_btn(lv_event_t *e)
{
    (void)e;
    s_recording = !s_recording;
    if (s_recording) {
        app_audio_record_start(16000);
        ui_chat_set_recording(true);
        ESP_LOGI(TAG, "Recording started");
    } else {
        app_audio_record_stop();
        ui_chat_set_recording(false);
        ESP_LOGI(TAG, "Recording stopped");
        // TODO: send recorded audio to AI agent
    }
}

lv_obj_t *ui_chat_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ── Top bar ─────────────────────────────────────────────────────────────
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);

    // Back button
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

    // ── Message list (scrollable) ────────────────────────────────────────────
    // Leave 80px at bottom for mic button
    s_msg_list = lv_obj_create(scr);
    lv_obj_set_size(s_msg_list, LV_PCT(100), 480 - 48 - 80);
    lv_obj_align(s_msg_list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(s_msg_list, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_msg_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_msg_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_msg_list, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msg_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_list, LV_SCROLLBAR_MODE_AUTO);

    // Welcome bubble
    ui_chat_append_message("system", "Hello! Tap the mic button to start talking.");

    // ── Mic / record button ─────────────────────────────────────────────────
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_size(bottom, LV_PCT(100), 80);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bottom, 0, LV_PART_MAIN);

    s_mic_btn = lv_button_create(bottom);
    lv_obj_set_size(s_mic_btn, 64, 64);
    lv_obj_align(s_mic_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
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
        // system message — centered, muted
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x2D2D44), LV_PART_MAIN);
        lv_obj_set_width(bubble, LV_PCT(95));
    }

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), LV_PART_MAIN);

    // Scroll to bottom
    lv_obj_scroll_to_y(s_msg_list, LV_COORD_MAX, LV_ANIM_ON);
}

void ui_chat_set_recording(bool recording)
{
    if (!s_mic_btn || !s_mic_label) return;
    if (recording) {
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0xFF4444), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_STOP);
    } else {
        lv_obj_set_style_bg_color(s_mic_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
        lv_label_set_text(s_mic_label, LV_SYMBOL_AUDIO);
    }
}
