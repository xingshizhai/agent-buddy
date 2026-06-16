#pragma once
#include <stdint.h>

void ble_hid_key_press(uint8_t keycode, uint8_t modifier);
void ble_hid_key_release(void);
void ble_hid_set_conn(uint16_t conn_handle);
