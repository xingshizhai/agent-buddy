#include "ui_debug_screen.h"
#include "ui.h"
#include "esp_log.h"

static const char *TAG = "ui_debug";

typedef struct { const char *icon; const char *label; ui_screen_id_t target; } menu_item_t;

static const menu_item_t s_items[] = {
    { LV_SYMBOL_AUDIO,    "Audio Test",      UI_SCREEN_DEBUG_AUDIO },
    { LV_SYMBOL_WIFI,     "WiFi Info",       UI_SCREEN_DEBUG_WIFI  },
    { LV_SYMBOL_BLUETOOTH,"Bluetooth Status",UI_SCREEN_DEBUG_BT    },
};

static void on_back(lv_event_t *e)   { (void)e; ui_navigate_to(UI_SCREEN_MAIN); }

static void on_item(lv_event_t *e)
{
    ui_screen_id_t *id = lv_event_get_user_data(e);
    ui_navigate_to(*id);
}

lv_obj_t *ui_debug_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Top bar
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
    lv_obj_add_event_cb(back_btn, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back_btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Debug");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Menu list
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), 480 - 48);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    static ui_screen_id_t s_ids[3];
    for (int i = 0; i < 3; i++) {
        s_ids[i] = s_items[i].target;

        lv_obj_t *row = lv_button_create(list);
        lv_obj_set_size(row, LV_PCT(100), 72);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x16213E), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(row, on_item, LV_EVENT_CLICKED, &s_ids[i]);

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, s_items[i].icon);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xE94560), LV_PART_MAIN);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, s_items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 48, 0);

        lv_obj_t *arr = lv_label_create(row);
        lv_label_set_text(arr, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arr, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -16, 0);
    }

    ESP_LOGI(TAG, "Debug screen created");
    return scr;
}
