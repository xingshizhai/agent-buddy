#include "ui_wifi_connect.h"
#include "ui.h"
#include "app_wifi.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_wifi_connect";

enum {
    STATE_SCANNING,
    STATE_SELECT_NETWORK,
    STATE_PASSWORD_INPUT,
};

// UI state
static int s_state = STATE_SCANNING;
static int s_selected_network = -1;
static char s_password[65] = "";

// UI elements
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_pwd_textarea = NULL;
static lv_obj_t *s_keyboard = NULL;
static lv_obj_t *s_status_label = NULL;

// Get signal strength indicator string
static const char* get_rssi_icon(int8_t rssi)
{
    if (rssi >= -50) return LV_SYMBOL_WIFI;      // 4 bars
    if (rssi >= -60) return LV_SYMBOL_WIFI;      // 3 bars (use same)
    if (rssi >= -70) return "WiFi";              // 2 bars
    return "wifi_low";                           // 1 bar
}

// Format security type string
static const char* get_auth_string(uint8_t authmode)
{
    switch(authmode) {
        case WIFI_AUTH_OPEN:        return "Open";
        case WIFI_AUTH_WEP:         return "WEP";
        case WIFI_AUTH_WPA_PSK:     return "WPA";
        case WIFI_AUTH_WPA2_PSK:    return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2/3";
        case WIFI_AUTH_WPA3_PSK:    return "WPA3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2 Ent.";
        default:                    return "Unknown";
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to debug screen");
    ui_navigate_to(UI_SCREEN_DEBUG);
}

// Callback when scan completes
static void on_scan_done(uint16_t count, void *ctx)
{
    if (count == 0) {
        lv_label_set_text(s_status_label, "No networks found");
        return;
    }

    ESP_LOGI(TAG, "Scan done: %u networks", count);
    lv_obj_clean(s_list);  // Clear list

    // Add networks to list
    for (int i = 0; i < count && i < 20; i++) {
        app_wifi_network_t net;
        if (app_wifi_get_network(i, &net) != ESP_OK) continue;

        char item_text[80];
        snprintf(item_text, sizeof(item_text), "%s [%d dBm] %s",
                 (char *)net.ssid, net.rssi, get_auth_string(net.authmode));

        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 48);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = (lv_obj_t *)e->target;
            s_selected_network = (int)lv_obj_get_user_data(btn);
            
            app_wifi_network_t net;
            if (app_wifi_get_network(s_selected_network, &net) == ESP_OK) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Enter password for: %s", (char *)net.ssid);
                lv_label_set_text(s_status_label, msg);
            }
            
            s_state = STATE_PASSWORD_INPUT;
            lv_obj_add_state(s_pwd_textarea, LV_STATE_FOCUSED);
            lv_textarea_set_cursor_pos(s_pwd_textarea, 0);
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, item_text);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    }

    lv_label_set_text(s_status_label, "Select a network");
    s_state = STATE_SELECT_NETWORK;
}

static void on_keyboard_input(lv_event_t *e)
{
    lv_keyboard_t *kb = (lv_keyboard_t *)e->target;
    char ch = lv_keyboard_get_pressed_digit(kb);
    
    if (ch == LV_KEY_BACKSPACE || ch == 8) {
        // Handle backspace in textarea
        uint32_t pos = lv_textarea_get_cursor_pos(s_pwd_textarea);
        if (pos > 0) {
            lv_textarea_del_char(s_pwd_textarea);
        }
    } else if (ch == '\n' || ch == 10) {
        // Enter/Accept
        strncpy(s_password, lv_textarea_get_text(s_pwd_textarea), sizeof(s_password) - 1);
        ESP_LOGI(TAG, "Attempting connection with password of length %zu", strlen(s_password));
        
        if (s_selected_network >= 0) {
            app_wifi_network_t net;
            if (app_wifi_get_network(s_selected_network, &net) == ESP_OK) {
                lv_label_set_text(s_status_label, "Connecting...");
                app_wifi_connect((const char *)net.ssid, s_password);
            }
        }
    } else if (ch && ch != 0 && ch != LV_KEY_LEFT && ch != LV_KEY_RIGHT) {
        // Regular character - add to textarea
        lv_textarea_add_char(s_pwd_textarea, ch);
    }
}

lv_obj_t *ui_wifi_connect_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ──── TOP BAR ────
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    lv_obj_t *back_btn = lv_button_create(bar);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi Connect");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // ──── STATUS LABEL ────
    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Scanning networks...");
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);

    // ──── NETWORK LIST (upper half) ────
    s_list = lv_list_create(scr);
    lv_obj_set_size(s_list, 760, 140);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x0F1419), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 1, LV_PART_MAIN);

    // ──── PASSWORD INPUT (middle) ────
    lv_obj_t *pwd_label = lv_label_create(scr);
    lv_label_set_text(pwd_label, "Password:");
    lv_obj_align(pwd_label, LV_ALIGN_TOP_MID, -350, 230);
    lv_obj_set_style_text_color(pwd_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN);

    s_pwd_textarea = lv_textarea_create(scr);
    lv_obj_set_size(s_pwd_textarea, 700, 50);
    lv_obj_align(s_pwd_textarea, LV_ALIGN_TOP_MID, 0, 250);
    lv_textarea_set_password_mode(s_pwd_textarea, true);
    lv_textarea_set_placeholder_text(s_pwd_textarea, "Enter WiFi password");
    lv_textarea_set_max_length(s_pwd_textarea, 64);
    lv_obj_set_style_bg_color(s_pwd_textarea, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_pwd_textarea, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pwd_textarea, 2, LV_PART_MAIN);

    // ──── VIRTUAL KEYBOARD (lower half) ────
    s_keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(s_keyboard, 760, 120);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_keyboard_set_textarea(s_keyboard, s_pwd_textarea);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_keyboard, on_keyboard_input, LV_EVENT_VALUE_CHANGED, NULL);

    // ──── BUTTONS ────
    lv_obj_t *btn_container = lv_obj_create(scr);
    lv_obj_set_size(btn_container, 360, 50);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 130);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel_btn = lv_button_create(btn_container);
    lv_obj_set_size(cancel_btn, 160, 44);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    lv_obj_t *connect_btn = lv_button_create(btn_container);
    lv_obj_set_size(connect_btn, 160, 44);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
    lv_obj_add_event_cb(connect_btn, [](lv_event_t *e) {
        (void)e;
        if (s_selected_network < 0) {
            lv_label_set_text(s_status_label, "Please select a network");
            return;
        }
        strncpy(s_password, lv_textarea_get_text(s_pwd_textarea), sizeof(s_password) - 1);
        if (strlen(s_password) == 0) {
            lv_label_set_text(s_status_label, "Please enter a password");
            return;
        }
        app_wifi_network_t net;
        if (app_wifi_get_network(s_selected_network, &net) == ESP_OK) {
            lv_label_set_text(s_status_label, "Connecting...");
            app_wifi_connect((const char *)net.ssid, s_password);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);

    // ──── START SCAN ────
    s_state = STATE_SCANNING;
    s_selected_network = -1;
    memset(s_password, 0, sizeof(s_password));

    app_wifi_set_scan_done_cb(on_scan_done, NULL);
    app_wifi_start_scan();

    ESP_LOGI(TAG, "WiFi connect screen created");
    return scr;
}
