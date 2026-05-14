#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include "lvgl.h"
void ui_usage_init(lv_obj_t *parent);
void ui_usage_update(const usage_data_t *data);
void ui_usage_update_ble(ble_gatt_state_t state);
