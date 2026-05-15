# Multi-Service Protocol & UI Redesign

**Date:** 2026-05-15  
**Status:** Approved  
**Scope:** PC daemon rewrite (Python), BLE protocol redesign, ESP32 protocol layer, ESP32 UI improvements

---

## 1. Goals

1. Replace the bash daemon (`agent-buddy-daemon.sh`) with a cross-platform Python package.
2. Redesign the PC↔ESP32 BLE protocol to support multiple services (Claude today, Cursor and others in the future) and bidirectional interaction.
3. Improve the ESP32 usage screen: dynamic arc colors, service name in topbar, status label, last-updated timestamp, dynamic multi-service screen cycling.

---

## 2. BLE Role Recap

| Role | Party |
|------|-------|
| GATT Server (Peripheral) | ESP32 — hosts the service, advertises "Agent Buddy" |
| GATT Client (Central) | PC daemon — scans, connects, writes/subscribes |

Characteristics (UUIDs unchanged):

| UUID suffix | Direction | Purpose |
|-------------|-----------|---------|
| `...0002` RX | PC → ESP32 (Write) | PC pushes data to ESP32 |
| `...0003` TX | ESP32 → PC (Notify) | ESP32 sends cap / ack / err |
| `...0004` REQ | ESP32 → PC (Notify) | ESP32 sends req (refresh request) |

---

## 3. Protocol Specification

### 3.1 Message Envelope

Every message in both directions uses a common JSON envelope:

```json
{"type":"<msg_type>", "v":1, "svc":"<service_id>", "payload":{...}}
```

| Field | Required | Description |
|-------|----------|-------------|
| `type` | always | Message type string |
| `v` | always | Protocol version integer (currently `1`) |
| `svc` | data/req only | Service identifier: `"claude"`, `"cursor"`, … |
| `payload` | data only | Service-specific data object |

Total envelope overhead is ~30 bytes. All messages must fit within the BLE GATT RX buffer limit of 512 bytes.

### 3.2 Message Types

#### PC → ESP32 (written to RX characteristic)

| `type` | Description |
|--------|-------------|
| `data` | Push a service data update |

```json
{
  "type": "data", "v": 1, "svc": "claude",
  "payload": {
    "s": 42,   "sr": 180,
    "w": 17,   "wr": 6230,
    "st": "allowed"
  }
}
```

The `payload` schema is service-defined. Claude reuses its existing compact fields (`s`, `sr`, `w`, `wr`, `st`). Future services define their own payload fields.

#### ESP32 → PC (notified via TX characteristic, except `req` via REQ characteristic)

| `type` | Characteristic | Trigger | Example |
|--------|---------------|---------|---------|
| `cap` | TX | Sent once after every connection | Lists supported services and screens |
| `req` | REQ | Periodic timer or button press | Requests a service refresh |
| `ack` | TX | After receiving a `data` message | Confirms parse success or failure |
| `err` | TX | On protocol/parse error | Reports error code and description |

```json
// cap — broadcast on connect
{"type":"cap","v":1,"svcs":["claude"],"screens":["usage","ble"]}

// req — request refresh (svc optional; omit to refresh all)
{"type":"req","v":1,"svc":"claude"}

// ack — data received
{"type":"ack","v":1,"ok":true}

// err — error report
{"type":"err","v":1,"code":1,"msg":"unknown svc: cursor"}
```

Error codes:

| Code | Meaning |
|------|---------|
| 1 | JSON parse failure |
| 2 | Unknown / unsupported service |
| 3 | Protocol version mismatch |

### 3.3 Typical Interaction Flow

```
[Connection established]
ESP32 → PC : {"type":"cap","v":1,"svcs":["claude"],"screens":["usage","ble"]}

[PC periodic push]
PC    → ESP32 : {"type":"data","v":1,"svc":"claude","payload":{...}}
ESP32 → PC   : {"type":"ack","v":1,"ok":true}

[ESP32 requests refresh]
ESP32 → PC   : {"type":"req","v":1,"svc":"claude"}
PC    → ESP32 : {"type":"data","v":1,"svc":"claude","payload":{...}}

[Unknown service sent by PC]
PC    → ESP32 : {"type":"data","v":1,"svc":"cursor","payload":{...}}
ESP32 → PC   : {"type":"err","v":1,"code":2,"msg":"unsupported svc"}
```

---

## 4. Python Daemon Architecture

### 4.1 Directory Structure

```
daemon/
├── main.py              # Entry point: lifecycle, reconnect loop, asyncio orchestration
├── ble.py               # BLE layer: scan / connect / write / subscribe (bleak, cross-platform)
├── protocol.py          # Message serialization / deserialization
├── requirements.txt     # bleak, aiohttp (or httpx)
└── services/
    ├── __init__.py
    ├── base.py          # ServiceBase abstract class
    └── claude.py        # Claude OAuth token + Anthropic API rate-limit poller
```

### 4.2 ServiceBase Interface

```python
class ServiceBase:
    service_id: str    # "claude", "cursor", …
    poll_interval: int # seconds between polls

    async def poll(self) -> dict | None:
        """Return payload dict on success, None on failure."""
```

Adding a new service = one new file in `services/` + one line in `main.py`'s service registry.

### 4.3 protocol.py API

```python
def make_data(svc: str, payload: dict) -> bytes
def parse(raw: bytes) -> dict   # {"type":"cap","v":1,"svcs":[...]}
```

### 4.4 Main Loop

1. Scan for "Agent Buddy" by service UUID (fall back to name).
2. Connect; subscribe to TX and REQ characteristic notifications.
3. Receive `cap` → know which services the ESP32 supports.
4. Start one `asyncio.Task` per configured service (independent poll loops).
5. On `req` notification → set an `asyncio.Event` to wake that service's task immediately.
6. On `ack` / `err` → log.
7. On disconnect → exponential backoff reconnect.

### 4.5 Concurrency Model

```
main loop
  ├── Task: claude.poll()   every 60 s  (woken early by req event)
  ├── Task: cursor.poll()   every 300 s (future)
  └── Task: ble.listen()    continuous notify handler
```

Each service task owns its own poll timer and `asyncio.Event` for early wakeup. Tasks are independent; a slow API call in one service does not delay others.

---

## 5. ESP32 Changes

### 5.1 Protocol Layer

**`protocol.h`** — new envelope type and dispatch interface:

```c
typedef enum { MSG_DATA, MSG_UNKNOWN } proto_msg_type_t;

typedef struct {
    proto_msg_type_t type;
    char  svc[16];
    int   version;
} proto_envelope_t;

bool protocol_parse(const char *json, proto_envelope_t *env, usage_data_t *out);
```

**`protocol.c`** — parse the outer envelope, dispatch payload to a service parser table:

```c
static const struct { const char *svc; parser_fn fn; } s_parsers[] = {
    { "claude", proto_claude_parse },
    // { "cursor", proto_cursor_parse },  ← add one line per future service
    { NULL, NULL }
};
```

**`proto_claude.c`** — payload parsing logic unchanged; entry point signature adjusts to receive only the `payload` object.

### 5.2 Multi-Service Data Store

**`usage_data.h`** — add a store for up to `PROTO_MAX_SERVICES` service slots:

```c
#define PROTO_MAX_SERVICES 4

typedef struct {
    usage_data_t slots[PROTO_MAX_SERVICES];
    int          count;
} service_store_t;

usage_data_t *store_get(service_store_t *s, const char *svc);
usage_data_t *store_set(service_store_t *s, const usage_data_t *d);
```

`main.c` owns one global `service_store_t`. The UI reads data for the currently displayed service by name.

### 5.3 BLE Layer

**Connect-time message** — replace `{"req":true}` with a proper `cap` envelope:

```c
send_notify(s_tx_handle,
    "{\"type\":\"cap\",\"v\":1,"
    "\"svcs\":[\"claude\"],"
    "\"screens\":[\"usage\",\"ble\"]}\n");
```

**New send helpers:**

```c
void ble_gatt_send_ack(bool ok);
void ble_gatt_send_err(int code, const char *msg);
void ble_gatt_send_req(const char *svc);   // NULL = refresh all
```

`ack` and `err` go via TX characteristic. `req` continues to use REQ characteristic (preserving existing PC subscription mechanism; only the payload format changes to the envelope).

### 5.4 Files Modified / Added

| File | Change |
|------|--------|
| `components/protocol/include/protocol/protocol.h` | New envelope struct + updated parse signature |
| `components/protocol/include/protocol/usage_data.h` | `service_store_t`, `store_get`, `store_set` |
| `components/protocol/src/protocol.c` | Envelope parse + parser dispatch table |
| `components/protocol/src/proto_claude.c` | Adjusted entry point (payload only) |
| `components/ble/include/ble/ble_gatt.h` | New `send_ack`, `send_err`, `send_req` signatures |
| `components/ble/src/ble_gatt.c` | `cap` on connect; new send helpers |
| `firmware/main/main.c` | Handle `cap` response; service_store lookup; dynamic screen registration |

---

## 6. ESP32 UI Improvements

### 6.1 Screen Cycle (button press)

```
[Splash] → [Claude usage] → [Cursor usage (future)] → [BLE status] → [Claude usage] → …
```

Service screens are registered dynamically when `main.c` receives the `cap` message. Each service in `svcs[]` gets one usage screen. BLE status is always last.

### 6.2 Usage Screen Layout (320×240, dark theme)

```
┌──────────────────────────────────────┐  y=0
│  CLAUDE                          ●   │  topbar 30 px
├──────────────────────────────────────┤
│                                      │
│    ╭──────╮          ╭──────╮        │
│    │  42% │          │  17% │        │  arcs 100×100, dynamic color
│    ╰──────╯          ╰──────╯        │
│    Session           Weekly          │
│    3h20m             6d14h           │  reset countdown
│                                      │
│  ● allowed                            │  status label
└──────────────────────────────────────┘
```

### 6.3 Arc Color Thresholds

| Range | Color | Hex |
|-------|-------|-----|
| 0 – 60% | Green | `#00CC66` |
| 60 – 85% | Yellow | `#FFAA00` |
| 85 – 100% | Red | `#FF3333` |

Color is applied to the arc indicator segment via `lv_obj_set_style_arc_color(..., LV_PART_INDICATOR)`.

### 6.4 New UI Elements

| Element | Content | Color |
|---------|---------|-------|
| Topbar title | Service name (`CLAUDE`, `CURSOR`, …) | White |
| BLE dot | `LV_SYMBOL_BLUETOOTH` | Blue=connected, Grey=disconnected |
| Status label | `"allowed"` / `"limited"` | Green / Red |
| Reset label | `"3h20m"` / `"43m"` / `"—"` | `#666666` |

### 6.5 Code Changes

| File | Change |
|------|--------|
| `components/ui/src/ui_usage.c` | `ui_usage_init(svc_name)` takes service name; arc color helper; status + timestamp labels |
| `components/ui/src/ui_usage.h` | Updated `ui_usage_init` signature |
| `components/ui/src/ui.c` | Dynamic screen list; `screen_t` replaced with runtime array |
| `components/ui/include/ui/ui.h` | `ui_register_service_screen(svc_name)` |
| `firmware/main/main.c` | Call `ui_register_service_screen` for each svc in `cap.svcs[]` |

---

## 7. Unchanged

- BLE UUIDs and characteristic count/properties
- NimBLE peripheral-only role, single connection
- LVGL port mutex usage pattern (`lvgl_port_lock` / `lvgl_port_unlock`)
- Splash screen and animations
- `proto_claude.c` payload field names (`s`, `sr`, `w`, `wr`, `st`)
- Linux systemd service wrapper (`agent-buddy-daemon.service`)

---

## 8. Adding a Future Service (Cursor example)

**PC side:**
1. Create `daemon/services/cursor.py` implementing `ServiceBase.poll()`.
2. Register in `main.py` service list.

**ESP32 side:**
1. Create `firmware/components/protocol/src/proto_cursor.c`.
2. Add `{ "cursor", proto_cursor_parse }` to `s_parsers[]` in `protocol.c`.
3. Update `cap` message `svcs` array in `ble_gatt.c`.

No other files need to change.
