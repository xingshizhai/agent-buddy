# ESP32 Protocol & UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update the ESP32 firmware to speak the new JSON-envelope BLE protocol, store data per-service, and render a dynamic usage screen with arc color feedback and a status label.

**Architecture:** A thin envelope parser in `protocol.c` dispatches each incoming `data` message to a service-specific parser (e.g. `proto_claude.c`) and stores the result in a `service_store_t` array. The UI layer creates one LVGL screen per registered service; `ui.c` cycles through service screens then the BLE screen on button press. Arc colors change dynamically based on utilisation %.

**Tech Stack:** ESP-IDF ≥ 5.2, NimBLE, LVGL via `espressif/esp-box-3` BSP, cJSON (already vendored in `firmware/components/protocol/src/`)

**Spec:** `docs/superpowers/specs/2026-05-15-multi-service-protocol-ui-design.md`

**Build command:** `cd firmware && idf.py build`
**Flash command:** `cd firmware && idf.py -p /dev/ttyACM0 flash monitor`

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Modify | `firmware/components/protocol/include/protocol/usage_data.h` | Add `service_store_t`, `store_*` declarations |
| Create | `firmware/components/protocol/src/service_store.c` | `store_init`, `store_get`, `store_set` |
| Modify | `firmware/components/protocol/include/protocol/protocol.h` | New envelope types, updated `protocol_parse` signature, message builder functions |
| Modify | `firmware/components/protocol/src/protocol.c` | Envelope parse + service dispatcher table |
| Modify | `firmware/components/protocol/src/proto_claude.c` | Accept `cJSON *payload` instead of raw JSON string |
| Modify | `firmware/components/protocol/CMakeLists.txt` | Add `service_store.c` to sources |
| Modify | `firmware/components/ble/include/ble/ble_gatt.h` | New `send_ack(bool)`, `send_err`, `send_req` signatures |
| Modify | `firmware/components/ble/src/ble_gatt.c` | Send `cap` on connect; implement new send helpers |
| Modify | `firmware/components/ui/include/ui/ui.h` | Add `ui_register_service_screen`; update `screen_t` enum |
| Modify | `firmware/components/ui/src/ui_usage.h` | Add `ui_usage_ctx_t`; update function signatures |
| Modify | `firmware/components/ui/src/ui_usage.c` | Arc color, status label, per-screen context struct |
| Modify | `firmware/components/ui/src/ui.c` | Dynamic service screen list; updated cycle logic |
| Modify | `firmware/main/main.c` | New `protocol_parse` call; `service_store_t`; dynamic screen registration; updated timeout check |

---

## Task 1: Service Store — `usage_data.h` and `service_store.c`

**Files:**
- Modify: `firmware/components/protocol/include/protocol/usage_data.h`
- Create: `firmware/components/protocol/src/service_store.c`
- Modify: `firmware/components/protocol/CMakeLists.txt`

- [ ] **Step 1: Update `usage_data.h`**

Add the store types at the bottom of the file, keeping the existing `usage_data_t` struct unchanged:

```c
#pragma once
#include <stdbool.h>

typedef struct {
    char  platform[16];        // "claude", "cursor", etc.
    float session_pct;         // 5h-window utilization 0–100
    int   session_reset_mins;  // minutes until session window resets (-1 = unknown)
    float weekly_pct;          // 7d-window utilization 0–100
    int   weekly_reset_mins;   // minutes until weekly window resets (-1 = unknown)
    char  status[16];          // "allowed" or "limited"
    bool  ok;
    bool  valid;
} usage_data_t;

// ── Multi-service store ───────────────────────────────────────────────────────

#define PROTO_MAX_SERVICES 4

typedef struct {
    usage_data_t slots[PROTO_MAX_SERVICES];
    int          count;
} service_store_t;

void          store_init(service_store_t *s);
usage_data_t *store_get(service_store_t *s, const char *svc);
usage_data_t *store_set(service_store_t *s, const usage_data_t *d);
```

- [ ] **Step 2: Create `service_store.c`**

```c
// firmware/components/protocol/src/service_store.c
#include "protocol/usage_data.h"
#include <string.h>

void store_init(service_store_t *s)
{
    memset(s, 0, sizeof(*s));
}

usage_data_t *store_get(service_store_t *s, const char *svc)
{
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->slots[i].platform, svc) == 0)
            return &s->slots[i];
    }
    return NULL;
}

usage_data_t *store_set(service_store_t *s, const usage_data_t *d)
{
    // Update existing slot
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->slots[i].platform, d->platform) == 0) {
            s->slots[i] = *d;
            return &s->slots[i];
        }
    }
    // Insert new slot
    if (s->count >= PROTO_MAX_SERVICES)
        return NULL;
    s->slots[s->count] = *d;
    return &s->slots[s->count++];
}
```

- [ ] **Step 3: Register the new source file in `CMakeLists.txt`**

Open `firmware/components/protocol/CMakeLists.txt`. Find the `SRCS` list and add `"src/service_store.c"`:

```cmake
idf_component_register(
    SRCS
        "src/protocol.c"
        "src/proto_claude.c"
        "src/service_store.c"
        "src/cJSON.c"
    INCLUDE_DIRS "include"
)
```

- [ ] **Step 4: Build to verify no compile errors**

```bash
cd firmware && idf.py build 2>&1 | tail -20
```

Expected: `Build successful` (or only pre-existing warnings — zero new errors).

- [ ] **Step 5: Commit**

```bash
git add firmware/components/protocol/include/protocol/usage_data.h \
        firmware/components/protocol/src/service_store.c \
        firmware/components/protocol/CMakeLists.txt
git commit -m "feat(esp32): add service_store multi-slot data store"
```

---

## Task 2: Protocol Header — Envelope Types and New Signatures

**Files:**
- Modify: `firmware/components/protocol/include/protocol/protocol.h`

- [ ] **Step 1: Replace the contents of `protocol.h`**

```c
#pragma once
#include "usage_data.h"
#include <stdbool.h>

// ── Incoming message envelope ─────────────────────────────────────────────────

typedef enum {
    MSG_DATA    = 0,   // "type":"data" — service data from PC
    MSG_UNKNOWN = 1,
} proto_msg_type_t;

typedef struct {
    proto_msg_type_t type;
    char             svc[16];   // service id, e.g. "claude"
    int              version;   // protocol version
} proto_envelope_t;

// Parse a JSON string received on the RX characteristic.
// Returns true and populates *env and *out on success.
// Returns false if the message is malformed, unknown type, or unsupported svc.
bool protocol_parse(const char *json, proto_envelope_t *env, usage_data_t *out);

// ── Outgoing message builders ─────────────────────────────────────────────────
// All functions return a pointer to an internal static buffer — use immediately,
// do not hold across calls.

// {"type":"cap","v":1,"svcs":["claude"],"screens":["usage","ble"]}
const char *protocol_cap(void);

// {"type":"ack","v":1,"ok":true/false}
const char *protocol_ack(bool ok);

// {"type":"err","v":1,"code":<code>,"msg":"<msg>"}
const char *protocol_err(int code, const char *msg);

// {"type":"req","v":1,"svc":"<svc>"}  (svc=NULL → omit svc field, refresh all)
const char *protocol_req(const char *svc);
```

- [ ] **Step 2: Build to surface any callers that use the old signature**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected errors (we will fix these in Tasks 3–6):
- `firmware/main/main.c`: `too few/many arguments to function 'protocol_parse'`
- `firmware/components/ble/src/ble_gatt.c`: `implicit declaration of 'protocol_ack'` / `'protocol_nack'`

- [ ] **Step 3: Commit the header**

```bash
git add firmware/components/protocol/include/protocol/protocol.h
git commit -m "feat(esp32): redesign protocol.h with envelope types and new signatures"
```

---

## Task 3: Protocol Implementation — Envelope Parse and Service Dispatch

**Files:**
- Modify: `firmware/components/protocol/src/protocol.c`

- [ ] **Step 1: Replace `protocol.c`**

```c
// firmware/components/protocol/src/protocol.c
#include "protocol/protocol.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

// Forward declaration — defined in proto_claude.c
bool proto_claude_parse(const cJSON *payload, usage_data_t *out);

typedef bool (*parser_fn)(const cJSON *payload, usage_data_t *out);

static const struct { const char *svc; parser_fn fn; } s_parsers[] = {
    { "claude", proto_claude_parse },
    // To add a new service: { "cursor", proto_cursor_parse },
    { NULL, NULL }
};

bool protocol_parse(const char *json, proto_envelope_t *env, usage_data_t *out)
{
    if (!json || !env || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *type_j = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *v_j    = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *svc_j  = cJSON_GetObjectItemCaseSensitive(root, "svc");

    env->version = (v_j && cJSON_IsNumber(v_j)) ? v_j->valueint : 0;

    if (!type_j || !cJSON_IsString(type_j) ||
        strcmp(type_j->valuestring, "data") != 0) {
        env->type = MSG_UNKNOWN;
        cJSON_Delete(root);
        return false;
    }
    env->type = MSG_DATA;

    strlcpy(env->svc,
            (svc_j && cJSON_IsString(svc_j)) ? svc_j->valuestring : "claude",
            sizeof(env->svc));
    strlcpy(out->platform, env->svc, sizeof(out->platform));

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!payload) {
        cJSON_Delete(root);
        return false;
    }

    bool ok = false;
    for (int i = 0; s_parsers[i].svc != NULL; i++) {
        if (strcmp(env->svc, s_parsers[i].svc) == 0) {
            ok = s_parsers[i].fn(payload, out);
            break;
        }
    }

    cJSON_Delete(root);
    return ok;
}

// ── Static message buffers ────────────────────────────────────────────────────

static char s_ack_buf[64];
static char s_err_buf[128];
static char s_req_buf[80];

const char *protocol_cap(void)
{
    return "{\"type\":\"cap\",\"v\":1,"
           "\"svcs\":[\"claude\"],"
           "\"screens\":[\"usage\",\"ble\"]}\n";
}

const char *protocol_ack(bool ok)
{
    snprintf(s_ack_buf, sizeof(s_ack_buf),
             "{\"type\":\"ack\",\"v\":1,\"ok\":%s}\n",
             ok ? "true" : "false");
    return s_ack_buf;
}

const char *protocol_err(int code, const char *msg)
{
    snprintf(s_err_buf, sizeof(s_err_buf),
             "{\"type\":\"err\",\"v\":1,\"code\":%d,\"msg\":\"%s\"}\n",
             code, msg ? msg : "");
    return s_err_buf;
}

const char *protocol_req(const char *svc)
{
    if (svc && svc[0]) {
        snprintf(s_req_buf, sizeof(s_req_buf),
                 "{\"type\":\"req\",\"v\":1,\"svc\":\"%s\"}\n", svc);
    } else {
        snprintf(s_req_buf, sizeof(s_req_buf),
                 "{\"type\":\"req\",\"v\":1}\n");
    }
    return s_req_buf;
}
```

- [ ] **Step 2: Build**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected remaining errors: only in `main.c` and `ble_gatt.c` (not yet updated). Zero errors in `protocol.c` itself.

- [ ] **Step 3: Commit**

```bash
git add firmware/components/protocol/src/protocol.c
git commit -m "feat(esp32): rewrite protocol.c with envelope dispatch table"
```

---

## Task 4: Update `proto_claude.c` — Accept cJSON Payload Object

**Files:**
- Modify: `firmware/components/protocol/src/proto_claude.c`

- [ ] **Step 1: Replace `proto_claude.c`**

The function now receives the already-parsed `payload` cJSON object instead of a raw JSON string:

```c
// firmware/components/protocol/src/proto_claude.c
#include "cJSON.h"
#include "protocol/usage_data.h"
#include <string.h>
#include <stdbool.h>

bool proto_claude_parse(const cJSON *payload, usage_data_t *out)
{
    if (!payload || !out) return false;

    cJSON *s  = cJSON_GetObjectItemCaseSensitive(payload, "s");
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(payload, "sr");
    cJSON *w  = cJSON_GetObjectItemCaseSensitive(payload, "w");
    cJSON *wr = cJSON_GetObjectItemCaseSensitive(payload, "wr");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(payload, "st");

    out->session_pct        = (s  && cJSON_IsNumber(s))  ? (float)s->valuedouble  : 0.0f;
    out->session_reset_mins = (sr && cJSON_IsNumber(sr)) ? sr->valueint            : -1;
    out->weekly_pct         = (w  && cJSON_IsNumber(w))  ? (float)w->valuedouble  : 0.0f;
    out->weekly_reset_mins  = (wr && cJSON_IsNumber(wr)) ? wr->valueint            : -1;

    strlcpy(out->status,
            (st && cJSON_IsString(st)) ? st->valuestring : "unknown",
            sizeof(out->status));

    out->ok    = true;
    out->valid = true;
    return true;
}
```

- [ ] **Step 2: Build**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected remaining errors: only in `main.c` and `ble_gatt.c`.

- [ ] **Step 3: Commit**

```bash
git add firmware/components/protocol/src/proto_claude.c
git commit -m "feat(esp32): update proto_claude to accept cJSON payload object"
```

---

## Task 5: Update BLE Layer — `cap` on Connect and New Send Helpers

**Files:**
- Modify: `firmware/components/ble/include/ble/ble_gatt.h`
- Modify: `firmware/components/ble/src/ble_gatt.c`

- [ ] **Step 1: Update `ble_gatt.h`**

Replace the `ble_gatt_send_ack` / `ble_gatt_send_nack` declarations with the new set:

```c
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
```

- [ ] **Step 2: Add `protocol` to the BLE component's dependency list**

Open `firmware/components/ble/CMakeLists.txt` and add `protocol` to `REQUIRES`:

```cmake
idf_component_register(
    SRCS "src/ble_gatt.c" "src/ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash protocol
    PRIV_REQUIRES bt
)
```

- [ ] **Step 3: Update the connect handler and send helpers in `ble_gatt.c`**

Find and replace the `BLE_GAP_EVENT_CONNECT` case and the `ble_gatt_send_ack` / `ble_gatt_send_nack` functions:

```c
// In gap_event_cb, BLE_GAP_EVENT_CONNECT case — replace the send_notify line:
case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
        s_conn_handle = event->connect.conn_handle;
        s_state = BLE_GATT_STATE_CONNECTED;
        ESP_LOGI(TAG, "connected handle=%d", s_conn_handle);
        send_notify(s_tx_handle, protocol_cap());
    } else {
        start_advertising();
    }
    return 0;
```

Replace the old `ble_gatt_send_ack` / `ble_gatt_send_nack` at the bottom of the file with:

```c
void ble_gatt_send_ack(bool ok)
{
    send_notify(s_tx_handle, protocol_ack(ok));
}

void ble_gatt_send_err(int code, const char *msg)
{
    send_notify(s_tx_handle, protocol_err(code, msg));
}

void ble_gatt_send_req(const char *svc)
{
    send_notify(s_req_handle, protocol_req(svc));
}
```

Also add the `protocol.h` include at the top of `ble_gatt.c`:

```c
#include "protocol/protocol.h"
```

- [ ] **Step 3: Build**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected remaining errors: only in `main.c`.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/ble/CMakeLists.txt \
        firmware/components/ble/include/ble/ble_gatt.h \
        firmware/components/ble/src/ble_gatt.c
git commit -m "feat(esp32): send cap on connect; add send_ack/err/req helpers"
```

---

## Task 6: UI Usage Screen — Context Struct, Arc Colors, Status Label

**Files:**
- Modify: `firmware/components/ui/src/ui_usage.h`
- Modify: `firmware/components/ui/src/ui_usage.c`

- [ ] **Step 1: Replace `ui_usage.h`**

```c
// firmware/components/ui/src/ui_usage.h
#pragma once
#include "lvgl.h"
#include "ble/ble_gatt.h"
#include "protocol/usage_data.h"

// Per-screen widget handles — one instance per registered service screen.
typedef struct {
    lv_obj_t *arc_session;
    lv_obj_t *arc_weekly;
    lv_obj_t *lbl_session_pct;
    lv_obj_t *lbl_weekly_pct;
    lv_obj_t *lbl_session_reset;
    lv_obj_t *lbl_weekly_reset;
    lv_obj_t *lbl_status;
    lv_obj_t *ble_dot;
} ui_usage_ctx_t;

// Create widgets on *parent for the given service name.
// Caller provides *ctx to receive widget handles.
void ui_usage_init(lv_obj_t *parent, const char *svc_name, ui_usage_ctx_t *ctx);

// Update all widgets from *data using *ctx.
void ui_usage_update(ui_usage_ctx_t *ctx, const usage_data_t *data);

// Update only the BLE indicator dot.
void ui_usage_update_ble(ui_usage_ctx_t *ctx, ble_gatt_state_t state);
```

- [ ] **Step 2: Replace `ui_usage.c`**

```c
// firmware/components/ui/src/ui_usage.c
#include "ui_usage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "UI_USAGE"

extern const lv_font_t font_styrene_28;
extern const lv_font_t font_styrene_20;
extern const lv_font_t font_mono_18;

#define TOP_OFFSET  2
#define TOP_H       28
#define CONTENT_TOP (TOP_OFFSET + TOP_H)
#define CONTENT_H   (BSP_LCD_V_RES - CONTENT_TOP)
#define HALF_W      (BSP_LCD_H_RES / 2)
#define ARC_DIAM    100
#define ARC_Y       (CONTENT_TOP + (CONTENT_H - ARC_DIAM) / 2 - 10)

static lv_color_t arc_color_for_pct(int pct)
{
    if (pct >= 85) return lv_color_hex(0xFF3333);
    if (pct >= 60) return lv_color_hex(0xFFAA00);
    return lv_color_hex(0x00CC66);
}

void ui_usage_init(lv_obj_t *parent, const char *svc_name, ui_usage_ctx_t *ctx)
{
    // Top bar
    lv_obj_t *topbar = lv_obj_create(parent);
    lv_obj_set_size(topbar, BSP_LCD_H_RES, TOP_H);
    lv_obj_set_pos(topbar, 0, TOP_OFFSET);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 4, 0);

    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, svc_name ? svc_name : "UNKNOWN");
    lv_obj_set_style_text_font(title, &font_styrene_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    ctx->ble_dot = lv_label_create(topbar);
    lv_label_set_text(ctx->ble_dot, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(ctx->ble_dot, lv_color_hex(0x888888), 0);
    lv_obj_align(ctx->ble_dot, LV_ALIGN_RIGHT_MID, -4, 0);

    // Session arc (left half)
    ctx->arc_session = lv_arc_create(parent);
    lv_obj_set_size(ctx->arc_session, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(ctx->arc_session, (HALF_W - ARC_DIAM) / 2, ARC_Y);
    lv_arc_set_range(ctx->arc_session, 0, 100);
    lv_arc_set_value(ctx->arc_session, 0);
    lv_obj_remove_style(ctx->arc_session, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ctx->arc_session, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ctx->arc_session, arc_color_for_pct(0), LV_PART_INDICATOR);

    ctx->lbl_session_pct = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_session_pct, "0%");
    lv_obj_set_style_text_font(ctx->lbl_session_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(ctx->lbl_session_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(ctx->lbl_session_pct, ctx->arc_session, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_sess = lv_label_create(parent);
    lv_label_set_text(lbl_sess, "Session");
    lv_obj_set_style_text_font(lbl_sess, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_sess, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_sess, ctx->arc_session, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    ctx->lbl_session_reset = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_session_reset, "");
    lv_obj_set_style_text_font(ctx->lbl_session_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_session_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(ctx->lbl_session_reset, lbl_sess, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Weekly arc (right half)
    ctx->arc_weekly = lv_arc_create(parent);
    lv_obj_set_size(ctx->arc_weekly, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(ctx->arc_weekly, HALF_W + (HALF_W - ARC_DIAM) / 2, ARC_Y);
    lv_arc_set_range(ctx->arc_weekly, 0, 100);
    lv_arc_set_value(ctx->arc_weekly, 0);
    lv_obj_remove_style(ctx->arc_weekly, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ctx->arc_weekly, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ctx->arc_weekly, arc_color_for_pct(0), LV_PART_INDICATOR);

    ctx->lbl_weekly_pct = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_weekly_pct, "0%");
    lv_obj_set_style_text_font(ctx->lbl_weekly_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(ctx->lbl_weekly_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(ctx->lbl_weekly_pct, ctx->arc_weekly, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_week = lv_label_create(parent);
    lv_label_set_text(lbl_week, "Weekly");
    lv_obj_set_style_text_font(lbl_week, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_week, ctx->arc_weekly, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    ctx->lbl_weekly_reset = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_weekly_reset, "");
    lv_obj_set_style_text_font(ctx->lbl_weekly_reset, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_weekly_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(ctx->lbl_weekly_reset, lbl_week, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Status label (bottom-left)
    ctx->lbl_status = lv_label_create(parent);
    lv_label_set_text(ctx->lbl_status, "");
    lv_obj_set_style_text_font(ctx->lbl_status, &font_mono_18, 0);
    lv_obj_set_style_text_color(ctx->lbl_status, lv_color_hex(0x666666), 0);
    lv_obj_align(ctx->lbl_status, LV_ALIGN_BOTTOM_LEFT, 8, -6);

    ESP_LOGI(TAG, "usage screen ready: %s", svc_name ? svc_name : "?");
}

static void fmt_reset(char *buf, size_t len, int mins)
{
    if (mins < 0)       snprintf(buf, len, "-");
    else if (mins < 60) snprintf(buf, len, "%dm", mins);
    else                snprintf(buf, len, "%dh%dm", mins / 60, mins % 60);
}

void ui_usage_update(ui_usage_ctx_t *ctx, const usage_data_t *data)
{
    int sp = (int)data->session_pct;
    int wp = (int)data->weekly_pct;

    lv_arc_set_value(ctx->arc_session, sp);
    lv_arc_set_value(ctx->arc_weekly,  wp);
    lv_obj_set_style_arc_color(ctx->arc_session, arc_color_for_pct(sp), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ctx->arc_weekly,  arc_color_for_pct(wp), LV_PART_INDICATOR);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", sp);
    lv_label_set_text(ctx->lbl_session_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", wp);
    lv_label_set_text(ctx->lbl_weekly_pct, buf);

    char rbuf[16];
    fmt_reset(rbuf, sizeof(rbuf), data->session_reset_mins);
    lv_label_set_text(ctx->lbl_session_reset, rbuf);
    fmt_reset(rbuf, sizeof(rbuf), data->weekly_reset_mins);
    lv_label_set_text(ctx->lbl_weekly_reset, rbuf);

    // Status label with color
    bool limited = (data->status[0] == 'l');  // "limited"
    lv_obj_set_style_text_color(ctx->lbl_status,
        limited ? lv_color_hex(0xFF3333) : lv_color_hex(0x00CC66), 0);
    lv_label_set_text(ctx->lbl_status, data->status);
}

void ui_usage_update_ble(ui_usage_ctx_t *ctx, ble_gatt_state_t state)
{
    lv_color_t c = (state == BLE_GATT_STATE_CONNECTED)
                   ? lv_color_hex(0x0099FF) : lv_color_hex(0x444444);
    lv_obj_set_style_text_color(ctx->ble_dot, c, 0);
}
```

- [ ] **Step 3: Build**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected: errors only in `ui.c` (calls old `ui_usage_init` signature) and `main.c`.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/ui/src/ui_usage.h \
        firmware/components/ui/src/ui_usage.c
git commit -m "feat(esp32): ui_usage ctx struct, arc colors, status label"
```

---

## Task 7: `ui.c` — Dynamic Service Screen Registration

**Files:**
- Modify: `firmware/components/ui/include/ui/ui.h`
- Modify: `firmware/components/ui/src/ui.c`

- [ ] **Step 1: Replace `ui.h`**

```c
#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include <stdbool.h>

typedef enum {
    SCREEN_SPLASH    = 0,
    SCREEN_SERVICE   = 1,   // any registered service screen
    SCREEN_BLUETOOTH = 2,
} screen_t;

void     ui_init(void);

// Register a usage screen for the given service name.
// Must be called after ui_init() and before the main loop runs.
// Screens appear in registration order in the button-press cycle.
void     ui_register_service_screen(const char *svc_name);

// Update the usage screen for data->platform (no-op if not registered).
void     ui_update(const usage_data_t *data);

void     ui_show_screen(screen_t screen);
void     ui_cycle_screen(void);
screen_t ui_get_current_screen(void);
void     ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac);
```

- [ ] **Step 2: Replace `ui.c`**

```c
// firmware/components/ui/src/ui.c
#include "ui/ui.h"
#include "ui_usage.h"
#include "ui_bluetooth.h"
#include "ui_splash.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "splash/splash.h"
#include "protocol/usage_data.h"
#include "esp_log.h"
#include <string.h>

#define TAG "UI"
#define MAX_SVC_SCREENS PROTO_MAX_SERVICES

typedef struct {
    lv_obj_t      *screen;
    char           svc_name[16];
    ui_usage_ctx_t ctx;
} svc_screen_t;

static lv_obj_t    *s_scr_bt     = NULL;
static lv_obj_t    *s_scr_splash = NULL;
static svc_screen_t s_svc[MAX_SVC_SCREENS];
static int          s_svc_count  = 0;

// Cycle index: 0 … s_svc_count-1 = service screens, s_svc_count = BLE screen
static int      s_cycle_idx = 0;
static screen_t s_current   = SCREEN_SPLASH;

static void screen_setup(lv_obj_t *s)
{
    lv_obj_set_size(s, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

static void hide_all(void)
{
    for (int i = 0; i < s_svc_count; i++)
        lv_obj_add_flag(s_svc[i].screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_scr_bt,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(void)
{
    lv_obj_t *root = lv_screen_active();

    s_scr_bt     = lv_obj_create(root);
    s_scr_splash = lv_obj_create(root);

    screen_setup(s_scr_bt);
    screen_setup(s_scr_splash);

    ui_bluetooth_init(s_scr_bt);
    ui_splash_init(s_scr_splash);
    splash_init(s_scr_splash);

    ui_show_screen(SCREEN_SPLASH);
    ESP_LOGI(TAG, "UI ready");
}

void ui_register_service_screen(const char *svc_name)
{
    if (s_svc_count >= MAX_SVC_SCREENS) {
        ESP_LOGW(TAG, "Max service screens reached, ignoring %s", svc_name);
        return;
    }
    if (lvgl_port_lock(200)) {
        lv_obj_t *scr = lv_obj_create(lv_screen_active());
        screen_setup(scr);
        ui_usage_init(scr, svc_name, &s_svc[s_svc_count].ctx);
        strlcpy(s_svc[s_svc_count].svc_name, svc_name, sizeof(s_svc[s_svc_count].svc_name));
        s_svc[s_svc_count].screen = scr;
        s_svc_count++;
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Registered service screen: %s (%d total)", svc_name, s_svc_count);
}

void ui_show_screen(screen_t screen)
{
    if (lvgl_port_lock(100)) {
        hide_all();
        switch (screen) {
        case SCREEN_SERVICE:
            if (s_svc_count > 0) {
                lv_obj_clear_flag(s_svc[s_cycle_idx].screen, LV_OBJ_FLAG_HIDDEN);
                splash_hide();
            }
            break;
        case SCREEN_BLUETOOTH:
            lv_obj_clear_flag(s_scr_bt, LV_OBJ_FLAG_HIDDEN);
            splash_hide();
            break;
        case SCREEN_SPLASH:
            lv_obj_clear_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN);
            splash_show();
            break;
        }
        s_current = screen;
        lvgl_port_unlock();
    }
}

void ui_cycle_screen(void)
{
    if (s_svc_count == 0) {
        ui_show_screen(SCREEN_BLUETOOTH);
        return;
    }
    // Cycle: svc[0] → svc[1] → … → BLE → svc[0]
    int total = s_svc_count + 1;  // +1 for BLE
    s_cycle_idx = (s_cycle_idx + 1) % total;
    if (s_cycle_idx < s_svc_count) {
        ui_show_screen(SCREEN_SERVICE);
    } else {
        s_cycle_idx = s_svc_count;  // keep at BLE position
        ui_show_screen(SCREEN_BLUETOOTH);
    }
}

screen_t ui_get_current_screen(void) { return s_current; }

void ui_update(const usage_data_t *data)
{
    for (int i = 0; i < s_svc_count; i++) {
        if (strcmp(s_svc[i].svc_name, data->platform) == 0) {
            if (lvgl_port_lock(100)) {
                ui_usage_update(&s_svc[i].ctx, data);
                lvgl_port_unlock();
            }
            return;
        }
    }
}

void ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac)
{
    if (lvgl_port_lock(100)) {
        ui_bluetooth_update(state, name, mac);
        for (int i = 0; i < s_svc_count; i++)
            ui_usage_update_ble(&s_svc[i].ctx, state);
        lvgl_port_unlock();
    }
}
```

- [ ] **Step 3: Build**

```bash
cd firmware && idf.py build 2>&1 | grep "error:"
```

Expected: errors only in `main.c`.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/ui/include/ui/ui.h \
        firmware/components/ui/src/ui.c
git commit -m "feat(esp32): dynamic service screen registration in ui.c"
```

---

## Task 8: Update `main.c` — Wire Everything Together

**Files:**
- Modify: `firmware/main/main.c`

- [ ] **Step 1: Replace the full `main.c`**

```c
// firmware/main/main.c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "icm42670.h"
#include "ble/ble_gatt.h"
#include "ble/ble_hid.h"
#include "protocol/protocol.h"
#include "protocol/usage_data.h"
#include "ui/ui.h"
#include "splash/splash.h"
#include "splash/usage_rate.h"

#define TAG "MAIN"

// Switch back to splash after this many ms with no BLE data
#define DATA_TIMEOUT_MS (3 * 60 * 1000)

static uint32_t       s_last_data_ms = 0;
static service_store_t s_store;

// ── IMU auto-rotation ────────────────────────────────────────────────────────
static icm42670_handle_t s_imu_handle = NULL;
static lv_display_t     *s_lv_disp   = NULL;

static void imu_rotation_check(void)
{
    if (!s_imu_handle) return;
    static lv_disp_rotation_t s_last_rot = LV_DISP_ROTATION_180;
    static uint32_t s_last_ms = 0;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_ms < 500) return;
    s_last_ms = now;

    icm42670_value_t accel = {0};
    if (icm42670_get_acce_value(s_imu_handle, &accel) != ESP_OK) return;

    lv_disp_rotation_t rot = (accel.y > 0.5f) ? LV_DISP_ROTATION_180 : LV_DISP_ROTATION_0;
    if (rot == s_last_rot) return;

    ESP_LOGI(TAG, "rotation %d→%d (ay=%.2f)", s_last_rot, rot, accel.y);
    s_last_rot = rot;

    bsp_display_brightness_set(0);
    if (lvgl_port_lock(200)) {
        lv_display_set_rotation(s_lv_disp, rot);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_display_brightness_set(80);
}

// ── Button callback ──────────────────────────────────────────────────────────
static void on_main_button(void *arg, void *data)
{
    if (ui_get_current_screen() == SCREEN_SPLASH) {
        if (lvgl_port_lock(100)) { splash_next(); lvgl_port_unlock(); }
    } else {
        ui_cycle_screen();
    }
}

// ── app_main ─────────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "Agent Buddy starting...");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    store_init(&s_store);

    bsp_i2c_init();

    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size    = BSP_LCD_H_RES * 40,
        .double_buffer  = true,
        .flags          = { .buff_dma = true, .buff_spiram = false },
    };
    s_lv_disp = bsp_display_start_with_config(&disp_cfg);
    bsp_display_brightness_set(80);

    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    if (icm42670_create(i2c_bus, ICM42670_I2C_ADDRESS, &s_imu_handle) == ESP_OK) {
        icm42670_cfg_t imu_cfg = {
            .acce_fs = ACCE_FS_4G, .acce_odr = ACCE_ODR_100HZ,
            .gyro_fs = GYRO_FS_500DPS, .gyro_odr = GYRO_ODR_100HZ,
        };
        icm42670_config(s_imu_handle, &imu_cfg);
        ESP_LOGI(TAG, "IMU ready");
    } else {
        ESP_LOGW(TAG, "IMU init failed — auto-rotation disabled");
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MAIN])
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_SINGLE_CLICK,
                               NULL, on_main_button, NULL);

    ble_gatt_init("Agent Buddy");

    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_init();
        // Register the Claude service screen unconditionally at boot.
        // Future: register dynamically from the cap message svcs[] list.
        ui_register_service_screen("CLAUDE");
        lv_timer_create(splash_tick, 80, NULL);
        lvgl_port_unlock();
    }

    ui_update_ble_status(ble_gatt_get_state(),
                         ble_gatt_get_name(),
                         ble_gatt_get_mac());

    ESP_LOGI(TAG, "Ready. Waiting for BLE data...");

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (1) {
        if (ble_gatt_has_data()) {
            const char *json = ble_gatt_get_data();
            if (json) {
                proto_envelope_t env = {0};
                usage_data_t     data = {0};

                if (protocol_parse(json, &env, &data)) {
                    store_set(&s_store, &data);
                    usage_rate_sample(data.session_pct);
                    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);

                    ui_update(&data);

                    if (ui_get_current_screen() == SCREEN_SPLASH) {
                        ui_show_screen(SCREEN_SERVICE);
                    }

                    ble_gatt_send_ack(true);
                    ESP_LOGI(TAG, "data[%s]: s=%.0f%% w=%.0f%% st=%s",
                             data.platform, data.session_pct,
                             data.weekly_pct, data.status);
                } else {
                    ble_gatt_send_err(
                        env.type == MSG_UNKNOWN ? 2 : 1,
                        env.type == MSG_UNKNOWN ? "unsupported svc" : "parse error");
                    ESP_LOGW(TAG, "protocol_parse failed (svc=%s)", env.svc);
                }
            }
        }

        static ble_gatt_state_t s_last_ble = BLE_GATT_STATE_INIT;
        ble_gatt_state_t cur_ble = ble_gatt_get_state();
        if (cur_ble != s_last_ble) {
            s_last_ble = cur_ble;
            ui_update_ble_status(cur_ble, ble_gatt_get_name(), ble_gatt_get_mac());
            ESP_LOGI(TAG, "BLE state: %d", cur_ble);
        }

        // Return to splash if no data received for DATA_TIMEOUT_MS
        if (s_last_data_ms > 0 && ui_get_current_screen() != SCREEN_SPLASH) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if ((now_ms - s_last_data_ms) > DATA_TIMEOUT_MS) {
                ESP_LOGI(TAG, "No data for %lu s, returning to splash",
                         (unsigned long)DATA_TIMEOUT_MS / 1000);
                ui_show_screen(SCREEN_SPLASH);
                s_last_data_ms = 0;
            }
        }

        imu_rotation_check();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

- [ ] **Step 2: Full build — must produce zero errors**

```bash
cd firmware && idf.py build 2>&1 | tail -5
```

Expected:
```
[100%] Linking CXX executable agent-buddy.elf
...
Project build complete.
```

If there are errors, fix them before proceeding.

- [ ] **Step 3: Commit**

```bash
git add firmware/main/main.c
git commit -m "feat(esp32): wire service_store and new protocol into main loop"
```

---

## Task 9: Flash and Integration Test

- [ ] **Step 1: Flash the firmware**

```bash
cd firmware && idf.py -p /dev/ttyACM0 flash monitor
```

- [ ] **Step 2: Verify boot sequence in serial output**

Expected lines (in order):
```
I (xxx) MAIN: Agent Buddy starting...
I (xxx) UI: UI ready
I (xxx) UI_USAGE: usage screen ready: CLAUDE
I (xxx) GATT: BLE synced, tx=N req=N
I (xxx) GATT: advertising as 'Agent Buddy' rc=0
I (xxx) MAIN: Ready. Waiting for BLE data...
```

- [ ] **Step 3: Connect the Python daemon and verify protocol handshake**

In a second terminal:
```bash
python -m daemon
```

Expected daemon log:
```
[HH:MM:SS] INFO daemon.ble: Found by UUID: ...
[HH:MM:SS] INFO daemon.ble: Connected to ...
[HH:MM:SS] INFO daemon.main: Device cap: svcs=['claude'] screens=['usage','ble']
```

Expected ESP32 serial log after connection:
```
I (xxx) GATT: connected handle=0
I (xxx) MAIN: BLE state: 2
```

- [ ] **Step 4: Verify data is received and displayed**

After the daemon sends the first `data` message (~60 s, or triggered by pressing the device button):

Expected daemon log:
```
[HH:MM:SS] INFO daemon.main: Sent claude data (XX bytes)
```

Expected ESP32 serial log:
```
I (xxx) MAIN: data[claude]: s=XX% w=XX% st=allowed
```

Expected on screen:
- Splash disappears, usage screen appears
- Topbar shows `CLAUDE`
- Session arc and weekly arc show correct percentages
- Arc color: green if < 60%, yellow if 60–85%, red if > 85%
- Status label at bottom-left shows `allowed` in green (or `limited` in red)

- [ ] **Step 5: Verify button cycling**

Press the main button and confirm the screen cycles:
```
CLAUDE usage → BLE status screen → CLAUDE usage → …
```

- [ ] **Step 6: Final commit if any last-minute fixes were needed**

```bash
git add -p   # stage only intentional changes
git commit -m "fix(esp32): integration test fixes"
```
