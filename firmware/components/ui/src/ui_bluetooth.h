#pragma once
#include "ble/ble_gatt.h"
#include "lvgl.h"
void ui_bluetooth_init(lv_obj_t *parent);
void ui_bluetooth_update(ble_gatt_state_t state, const char *name, const char *mac);
