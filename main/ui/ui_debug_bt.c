#include "ui_debug_bt.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <stdio.h>

static const char *TAG = "ui_debug_bt";

static void on_back(lv_event_t *e) { (void)e; ui_navigate_to(UI_SCREEN_DEBUG); }

static lv_obj_t *info_row(lv_obj_t *parent, const char *key, const char *val, int y_ofs)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 700, 48);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_ofs);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 16, LV_PART_MAIN);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, val);
    lv_obj_set_style_text_color(v, lv_color_hex(0xDDDDDD), LV_PART_MAIN);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

lv_obj_t *ui_debug_bt_screen_create(void)
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

    lv_obj_t *bb = lv_button_create(bar);
    lv_obj_set_size(bb, 80, 36);
    lv_obj_align(bb, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(bb, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(bb, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bbl = lv_label_create(bb);
    lv_label_set_text(bbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(bbl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " Bluetooth");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Content
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), 480 - 48);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 16, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    // Device name from Kconfig
    info_row(content, "Device Name", CONFIG_AGENT_BUDDY_BT_DEVICE_NAME, 0);

    // BT MAC (base MAC + 2)
    uint8_t mac[6];
    char mac_str[24] = "--";
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK)
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    info_row(content, "BT MAC", mac_str, 56);

    // WiFi MAC for reference
    char wmac_str[24] = "--";
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
        snprintf(wmac_str, sizeof(wmac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    info_row(content, "WiFi MAC", wmac_str, 112);

    info_row(content, "BT Classic", "Available (not init)", 168);
    info_row(content, "BLE",        "Available (not init)", 224);

    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Bluetooth stack not initialized in this build");
    lv_obj_set_style_text_color(note, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 700);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 290);

    ESP_LOGI(TAG, "BT debug screen created");
    return scr;
}
