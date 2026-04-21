#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_STATUS_IDLE = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_DISCONNECTED,
} app_wifi_status_t;

typedef void (*app_wifi_status_cb_t)(app_wifi_status_t status, void *ctx);

esp_err_t app_wifi_init(void);
esp_err_t app_wifi_connect(const char *ssid, const char *password);
app_wifi_status_t app_wifi_get_status(void);
void app_wifi_set_status_cb(app_wifi_status_cb_t cb, void *ctx);
bool app_wifi_is_connected(void);
