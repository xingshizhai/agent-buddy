#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    BLE_GATT_STATE_INIT         = 0,
    BLE_GATT_STATE_ADVERTISING  = 1,
    BLE_GATT_STATE_CONNECTED    = 2,
    BLE_GATT_STATE_DISCONNECTED = 3,
} ble_gatt_state_t;

esp_err_t        ble_gatt_init(const char *device_name);
bool             ble_gatt_has_data(void);
const char      *ble_gatt_get_data(void);
ble_gatt_state_t ble_gatt_get_state(void);
const char      *ble_gatt_get_mac(void);
const char      *ble_gatt_get_name(void);

// Outgoing messages (via TX characteristic)
void ble_gatt_send_ack(bool ok);
void ble_gatt_send_err(int code, const char *msg);

// Refresh request (via REQ characteristic)
void ble_gatt_send_req(const char *svc);   // svc=NULL → refresh all
