#include "ble/ble_hid.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "esp_log.h"
#include <string.h>

#define TAG "HID"

static const uint8_t s_report_map[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Controls)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
      0x05, 0x07,  // Usage Page (Keyboard/Keypad)
      0x19, 0xE0,  // Usage Minimum (Left Control)
      0x29, 0xE7,  // Usage Maximum (Right GUI)
      0x15, 0x00,  // Logical Minimum (0)
      0x25, 0x01,  // Logical Maximum (1)
      0x75, 0x01,  // Report Size (1)
      0x95, 0x08,  // Report Count (8)
      0x81, 0x02,  // Input (Data, Variable, Absolute) — modifier
      0x95, 0x01,  // Report Count (1)
      0x75, 0x08,  // Report Size (8)
      0x81, 0x01,  // Input (Constant) — reserved
      0x95, 0x06,  // Report Count (6)
      0x75, 0x08,  // Report Size (8)
      0x15, 0x00,  // Logical Minimum (0)
      0x25, 0x65,  // Logical Maximum (101)
      0x05, 0x07,  // Usage Page (Keyboard/Keypad)
      0x19, 0x00,  // Usage Minimum (0)
      0x29, 0x65,  // Usage Maximum (101)
      0x81, 0x00,  // Input (Data, Array, Absolute) — keys
    0xC0,          // End Collection
};

static const uint8_t s_hid_info[] = { 0x11, 0x01, 0x00, 0x03 };

static uint16_t s_report_handle = 0;
static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_protocol_mode = 0x01;

static int hid_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (uuid16 == 0x2A4A)
            return os_mbuf_append(ctxt->om, s_hid_info, sizeof(s_hid_info)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4B)
            return os_mbuf_append(ctxt->om, s_report_map, sizeof(s_report_map)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4E)
            return os_mbuf_append(ctxt->om, &s_protocol_mode, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4D) {
            uint8_t z[8] = {0};
            return os_mbuf_append(ctxt->om, z, sizeof(z)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        }
        return 0;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (uuid16 == 0x2A4C) return 0;
        if (uuid16 == 0x2A4E) {
            uint8_t pm = 0;
            ble_hs_mbuf_to_flat(ctxt->om, &pm, 1, NULL);
            s_protocol_mode = pm;
        }
        return 0;
    default: return 0;
    }
}

static const uint8_t s_report_ref[] = { 0x00, 0x01 };

static int report_ref_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, s_report_ref, sizeof(s_report_ref)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

const struct ble_gatt_svc_def ble_hid_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .flags = BLE_GATT_CHR_F_READ, .access_cb = hid_access_cb },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .flags = BLE_GATT_CHR_F_READ, .access_cb = hid_access_cb },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .flags = BLE_GATT_CHR_F_WRITE_NO_RSP, .access_cb = hid_access_cb },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP, .access_cb = hid_access_cb },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_report_handle,
                .access_cb = hid_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = report_ref_cb },
                    { .uuid = NULL }
                },
            },
            { .uuid = NULL }
        },
    },
    { .type = 0 }
};

void ble_hid_set_conn(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
}

static void send_report(uint8_t modifier, uint8_t keycode)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_report_handle == 0) return;
    uint8_t report[8] = { modifier, 0x00, keycode, 0, 0, 0, 0, 0 };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om) ble_gattc_notify_custom(s_conn_handle, s_report_handle, om);
}

void ble_hid_key_press(uint8_t keycode, uint8_t modifier) { send_report(modifier, keycode); }
void ble_hid_key_release(void)                            { send_report(0, 0); }
