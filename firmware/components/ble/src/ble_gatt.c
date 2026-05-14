#include "ble/ble_gatt.h"
#include "ble/ble_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"
#include <string.h>
#include <stdio.h>

void ble_store_config_init(void);
extern const struct ble_gatt_svc_def ble_hid_svc[];

#define TAG "GATT"
#define RX_BUF_SIZE  512

// Clawdmeter UUIDs (little-endian, LSB first)
// Service:  4c41555a-4465-7669-6365-000000000001
static const ble_uuid128_t s_svc_uuid = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x01,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
static const ble_uuid128_t s_rx_uuid  = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x02,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
static const ble_uuid128_t s_tx_uuid  = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x03,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
static const ble_uuid128_t s_req_uuid = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x04,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};

static uint16_t s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle    = 0;
static uint16_t s_req_handle   = 0;
static char     s_device_name[32];
static char     s_mac_str[18];
static ble_gatt_state_t s_state = BLE_GATT_STATE_INIT;

static QueueHandle_t s_data_queue = NULL;
static char          s_rx_buf[RX_BUF_SIZE];
static char          s_queue_buf[RX_BUF_SIZE];

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, len, NULL);
    s_rx_buf[len] = '\0';
    memcpy(s_queue_buf, s_rx_buf, len + 1);
    xQueueOverwrite(s_data_queue, &s_queue_buf);
    return 0;
}

// Forward declaration — defined below start_advertising
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static const struct ble_gatt_svc_def s_clawdmeter_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = &s_rx_uuid.u,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .access_cb = gatt_access_cb,
            },
            {
                .uuid       = &s_tx_uuid.u,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
                .access_cb  = gatt_access_cb,
            },
            {
                .uuid       = &s_req_uuid.u,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_req_handle,
                .access_cb  = gatt_access_cb,
            },
            { .uuid = NULL }
        },
    },
    { .type = 0 }
};

static void send_notify(uint16_t handle, const char *msg)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (om) ble_gattc_notify_custom(s_conn_handle, handle, om);
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields adv = {0};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    adv.name = (const uint8_t *)"Claude";
    adv.name_len = 6;
    adv.name_is_complete = 0;
    ble_gap_adv_set_fields(&adv);

    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (const uint8_t *)s_device_name;
    rsp.name_len = strlen(s_device_name);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(100);
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc == 0) s_state = BLE_GATT_STATE_ADVERTISING;
    ESP_LOGI(TAG, "advertising as '%s' rc=%d", s_device_name, rc);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_state = BLE_GATT_STATE_CONNECTED;
            ble_hid_set_conn(s_conn_handle);
            ESP_LOGI(TAG, "connected handle=%d", s_conn_handle);
            // No forced pairing — open connection allows daemon to write freely.
            // HID keyboard pairing is handled by the OS if needed.
            send_notify(s_req_handle, "{\"req\":true}\n");
        } else {
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_state = BLE_GATT_STATE_DISCONNECTED;
        ble_hid_set_conn(BLE_HS_CONN_HANDLE_NONE);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption %s", event->enc_change.status == 0 ? "OK" : "FAIL");
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU=%d", event->mtu.value);
        return 0;

    default: return 0;
    }
}

static void ble_hs_sync_cb(void)
{
    ble_hs_util_ensure_addr(0);
    ESP_LOGI(TAG, "BLE synced, tx=%d req=%d", s_tx_handle, s_req_handle);
    start_advertising();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_gatt_init(const char *device_name)
{
    s_data_queue = xQueueCreate(1, RX_BUF_SIZE);
    if (!s_data_queue) return ESP_ERR_NO_MEM;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    if (device_name && device_name[0]) {
        strlcpy(s_device_name, device_name, sizeof(s_device_name));
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(s_device_name, sizeof(s_device_name), "Claude%02X%02X", mac[4], mac[5]);
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_mac_str, sizeof(s_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    nimble_port_init();
    ble_hs_cfg.sync_cb         = ble_hs_sync_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap       = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc           = 1;
    ble_hs_cfg.sm_bonding      = 1;
    ble_hs_cfg.sm_mitm         = 0;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Must be static: NimBLE keeps a pointer to this table after ble_gatt_init returns.
    static struct ble_gatt_svc_def combined[3];
    combined[0] = s_clawdmeter_svc[0];
    combined[1] = ble_hid_svc[0];
    combined[2] = (struct ble_gatt_svc_def){ .type = 0 };

    ble_gatts_count_cfg(combined);
    ble_gatts_add_svcs(combined);

    ble_svc_gap_device_name_set(s_device_name);
    ble_store_config_init();
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool ble_gatt_has_data(void)
{
    return uxQueueMessagesWaiting(s_data_queue) > 0;
}

const char *ble_gatt_get_data(void)
{
    static char out[RX_BUF_SIZE];
    if (xQueueReceive(s_data_queue, out, 0) == pdTRUE) return out;
    return NULL;
}

void ble_gatt_send_ack(void)  { send_notify(s_tx_handle, "{\"ack\":true}\n");  }
void ble_gatt_send_nack(void) { send_notify(s_tx_handle, "{\"ack\":false}\n"); }

ble_gatt_state_t ble_gatt_get_state(void) { return s_state; }
const char *ble_gatt_get_mac(void)         { return s_mac_str; }
const char *ble_gatt_get_name(void)        { return s_device_name; }
