#include "ui_debug_wifi.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>

static const char *TAG = "ui_debug_wifi";

static lv_obj_t *s_val[5];   // status, ssid, ip, rssi, channel
static lv_timer_t *s_timer = NULL;

static void refresh_wifi_info(void)
{
    wifi_ap_record_t ap = {0};
    bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);

    if (!connected) {
        lv_label_set_text(s_val[0], "Disconnected");
        lv_obj_set_style_text_color(s_val[0], lv_color_hex(0xFF5555), LV_PART_MAIN);
        lv_label_set_text(s_val[1], "--");
        lv_label_set_text(s_val[2], "--");
        lv_label_set_text(s_val[3], "--");
        lv_label_set_text(s_val[4], "--");
        return;
    }

    lv_label_set_text(s_val[0], "Connected");
    lv_obj_set_style_text_color(s_val[0], lv_color_hex(0x4CAF50), LV_PART_MAIN);
    lv_label_set_text(s_val[1], (char *)ap.ssid);

    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_str[24];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        lv_label_set_text(s_val[2], ip_str);
    } else {
        lv_label_set_text(s_val[2], "Unavailable");
    }

    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", ap.rssi);
    lv_label_set_text(s_val[3], rssi_str);

    char ch_str[8];
    snprintf(ch_str, sizeof(ch_str), "%d", ap.primary);
    lv_label_set_text(s_val[4], ch_str);
}

static void on_timer(lv_timer_t *t) { (void)t; refresh_wifi_info(); }

static void on_back(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    ui_navigate_to(UI_SCREEN_DEBUG);
}

static lv_obj_t *add_row(lv_obj_t *parent, const char *key, int y_ofs)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 700, 48);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_ofs);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 16, LV_PART_MAIN);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_color(key_lbl, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(key_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, "--");
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(0xDDDDDD), LV_PART_MAIN);
    lv_obj_align(val_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    return val_lbl;
}

lv_obj_t *ui_debug_wifi_screen_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi Info");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Content area
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), 480 - 48);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 16, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    static const char *keys[] = { "Status", "SSID", "IP Address", "Signal", "Channel" };
    for (int i = 0; i < 5; i++)
        s_val[i] = add_row(content, keys[i], i * 56);

    s_timer = lv_timer_create(on_timer, 2000, NULL);
    refresh_wifi_info();

    ESP_LOGI(TAG, "WiFi debug screen created");
    return scr;
}
