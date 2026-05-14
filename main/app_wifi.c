#include "app_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "app_wifi";

#define WIFI_MAX_RETRIES    5

static app_wifi_status_t    s_status    = WIFI_STATUS_IDLE;
static app_wifi_status_cb_t s_cb        = NULL;
static void                *s_cb_ctx   = NULL;
static int                  s_retries  = 0;

static void notify(app_wifi_status_t status)
{
    s_status = status;
    if (s_cb) s_cb(status, s_cb_ctx);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retries < WIFI_MAX_RETRIES) {
                s_retries++;
                ESP_LOGW(TAG, "Retry %d/%d", s_retries, WIFI_MAX_RETRIES);
                esp_wifi_connect();
                notify(WIFI_STATUS_CONNECTING);
            } else {
                ESP_LOGE(TAG, "Connection failed");
                notify(WIFI_STATUS_DISCONNECTED);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        notify(WIFI_STATUS_CONNECTED);
    }
}

esp_err_t app_wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    return ESP_OK;
}

esp_err_t app_wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {};
    strlcpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    s_retries = 0;
    notify(WIFI_STATUS_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    return ESP_OK;
}

app_wifi_status_t app_wifi_get_status(void)  { return s_status; }
bool              app_wifi_is_connected(void) { return s_status == WIFI_STATUS_CONNECTED; }

void app_wifi_set_status_cb(app_wifi_status_cb_t cb, void *ctx)
{
    s_cb     = cb;
    s_cb_ctx = ctx;
}
