#pragma once
#include "lvgl.h"

// Create and return the WiFi connection screen
// Shows network list + password input + virtual keyboard
lv_obj_t *ui_wifi_connect_screen_create(void);
