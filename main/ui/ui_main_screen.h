#pragma once
#include "lvgl.h"

lv_obj_t *ui_main_screen_create(void);
void      ui_main_screen_set_wifi(bool connected);
void      ui_main_screen_set_status(const char *text);
