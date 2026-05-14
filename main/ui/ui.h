#pragma once

#include "lvgl.h"

typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_CHAT,
    UI_SCREEN_DEBUG,
    UI_SCREEN_DEBUG_AUDIO,
    UI_SCREEN_DEBUG_WIFI,
    UI_SCREEN_DEBUG_BT,
    UI_SCREEN_COUNT,
} ui_screen_id_t;

typedef struct {
    bool wifi_connected;
    char status_text[64];
} ui_state_t;

extern ui_state_t g_ui_state;

// Screen create functions
lv_obj_t *ui_main_screen_create(void);
lv_obj_t *ui_chat_screen_create(void);
lv_obj_t *ui_debug_screen_create(void);
lv_obj_t *ui_debug_audio_screen_create(void);
lv_obj_t *ui_debug_wifi_screen_create(void);
lv_obj_t *ui_debug_bt_screen_create(void);

void ui_navigate_to(ui_screen_id_t screen);
void ui_update_wifi_status(bool connected);
