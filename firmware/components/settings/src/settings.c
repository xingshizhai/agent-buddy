#include "settings/settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include "sdkconfig.h"

#define TAG       "SETTINGS"
#define NVS_NS    "agent_buddy"
#define KEY_BRIGHT "brightness"
#define KEY_DIM    "dim_secs"
#define KEY_NAME   "ble_name"

void settings_load(device_settings_t *out)
{
    // Populate Kconfig defaults first
    out->brightness      = CONFIG_AGENT_BUDDY_DISPLAY_BRIGHTNESS;
    out->screen_dim_secs = CONFIG_AGENT_BUDDY_SCREEN_DIM_TIMEOUT_S;
    strlcpy(out->ble_name, CONFIG_AGENT_BUDDY_BLE_DEVICE_NAME, sizeof(out->ble_name));

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGD(TAG, "No NVS namespace yet — using defaults");
        return;
    }

    uint8_t  bright = 0;
    uint16_t dim    = 0;
    char     name[32] = {0};
    size_t   name_len = sizeof(name);

    if (nvs_get_u8(h,  KEY_BRIGHT, &bright)    == ESP_OK) out->brightness      = bright;
    if (nvs_get_u16(h, KEY_DIM,    &dim)        == ESP_OK) out->screen_dim_secs = dim;
    if (nvs_get_str(h, KEY_NAME,   name, &name_len) == ESP_OK)
        strlcpy(out->ble_name, name, sizeof(out->ble_name));

    nvs_close(h);
    ESP_LOGI(TAG, "Loaded: brightness=%d dim=%ds ble=%s",
             out->brightness, out->screen_dim_secs, out->ble_name);
}

void settings_save(const device_settings_t *s)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS for write");
        return;
    }
    nvs_set_u8(h,  KEY_BRIGHT, s->brightness);
    nvs_set_u16(h, KEY_DIM,    s->screen_dim_secs);
    nvs_set_str(h, KEY_NAME,   s->ble_name);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved: brightness=%d dim=%ds ble=%s",
             s->brightness, s->screen_dim_secs, s->ble_name);
}
