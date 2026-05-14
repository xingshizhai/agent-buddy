#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    BLE_GATT_STATE_INIT,
    BLE_GATT_STATE_ADVERTISING,
    BLE_GATT_STATE_CONNECTED,
    BLE_GATT_STATE_DISCONNECTED,
} ble_gatt_state_t;

esp_err_t   ble_gatt_init(const char *device_name);

bool        ble_gatt_has_data(void);
const char *ble_gatt_get_data(void);
void        ble_gatt_send_ack(void);
void        ble_gatt_send_nack(void);

ble_gatt_state_t ble_gatt_get_state(void);
const char      *ble_gatt_get_mac(void);
const char      *ble_gatt_get_name(void);
