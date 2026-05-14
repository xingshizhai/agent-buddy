#pragma once
#include "lvgl.h"

lv_obj_t *ui_chat_screen_create(void);
void      ui_chat_append_message(const char *role, const char *text);
void      ui_chat_set_recording(bool recording);
