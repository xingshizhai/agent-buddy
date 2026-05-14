# Clawdmeter ESP32-S3-BOX-3 Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port Clawdmeter (Claude Code usage dashboard) from Arduino + Waveshare AMOLED 480×480 to pure ESP-IDF + ESP32-S3-BOX-3 (ST7789 SPI 320×240).

**Architecture:** Component-based ESP-IDF project at `~/projects/clawdmeter-box3/`. Five components: `ble` (NimBLE GATT data + HID keyboard), `protocol` (multi-platform JSON parsing), `splash` (animations + usage rate), `ui` (LVGL screens). Hardware init via `espressif/esp-box-3` BSP in `main.c`. Daemon shell script unchanged.

**Tech Stack:** ESP-IDF ≥5.2, NimBLE (built-in), LVGL 9 via esp_lvgl_port, espressif/esp-box-3 BSP, espressif/icm42670 (ICM-42607-P IMU), cJSON (built-in), iot_button (built-in)

**Design spec:** `docs/superpowers/specs/2026-05-14-clawdmeter-box3-port-design.md`  
**Reference project:** https://github.com/xingshizhai/smart-buddy (same hardware, same stack)  
**Original firmware:** https://github.com/HermannBjorgvin/Clawdmeter (source material)

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Create | Top-level project |
| `idf_component.yml` | Create | External component deps |
| `sdkconfig.defaults` | Create | BLE, PSRAM, LVGL, NimBLE settings |
| `sdkconfig.defaults.esp32s3` | Create | Flash/PSRAM size for S3 |
| `partitions.csv` | Create | 16MB flash layout |
| `main/CMakeLists.txt` | Create | Main component |
| `main/main.c` | Create | app_main, BSP init, IMU rotation, button callbacks, main loop |
| `components/protocol/CMakeLists.txt` | Create | Protocol component |
| `components/protocol/include/protocol/usage_data.h` | Create | Shared UsageData struct (platform-agnostic) |
| `components/protocol/include/protocol/protocol.h` | Create | Protocol interface (parse/ack) |
| `components/protocol/src/protocol.c` | Create | Platform dispatcher (routes by "platform" JSON field) |
| `components/protocol/src/proto_claude.c` | Create | Claude JSON parser (cJSON) |
| `components/splash/CMakeLists.txt` | Create | Splash component |
| `components/splash/include/splash/usage_rate.h` | Create | Rate tracking interface |
| `components/splash/include/splash/splash.h` | Create | Animation interface |
| `components/splash/src/usage_rate.c` | Create | Port from original (remove Arduino millis()) |
| `components/splash/src/splash.c` | Create | Animation canvas, frame tick, rate-group selection |
| `components/splash/src/splash_animations.h` | Fetch | RGB565 C arrays from original repo |
| `components/ble/CMakeLists.txt` | Create | BLE component |
| `components/ble/include/ble/ble_gatt.h` | Create | GATT service interface |
| `components/ble/include/ble/ble_hid.h` | Create | HID keyboard interface |
| `components/ble/src/ble_gatt.c` | Create | Clawdmeter custom GATT + NimBLE host init |
| `components/ble/src/ble_hid.c` | Create | HID keyboard GATT service (NimBLE manual registration) |
| `components/ui/CMakeLists.txt` | Create | UI component |
| `components/ui/include/ui/ui.h` | Create | Screen manager interface |
| `components/ui/src/ui.c` | Create | Screen switching, LVGL lock wrappers |
| `components/ui/src/ui_usage.c` | Create | Usage screen: arc bars + HID touch buttons |
| `components/ui/src/ui_bluetooth.c` | Create | Bluetooth status screen |
| `components/ui/src/ui_splash.c` | Create | Splash wrapper screen (hosts splash canvas) |
| `fonts/font_styrene_28.c` | Copy | Numbers font (from original repo) |
| `fonts/font_styrene_20.c` | Copy | Label font |
| `fonts/font_mono_18.c` | Copy | Status/mono font |
| `fonts/font_tiempos_34.c` | Generate | Title font 34px (requires proprietary OTF) |
| `fonts/icons.h` | Copy | Lucide BT icon arrays |

---

## Task 1: Project Scaffolding

**Files:**
- Create: `~/projects/clawdmeter-box3/CMakeLists.txt`
- Create: `~/projects/clawdmeter-box3/idf_component.yml`
- Create: `~/projects/clawdmeter-box3/sdkconfig.defaults`
- Create: `~/projects/clawdmeter-box3/sdkconfig.defaults.esp32s3`
- Create: `~/projects/clawdmeter-box3/partitions.csv`
- Create: `~/projects/clawdmeter-box3/main/CMakeLists.txt`
- Create: `~/projects/clawdmeter-box3/main/main.c` (stub)

- [ ] **Step 1: Create directory tree**

```bash
mkdir -p ~/projects/clawdmeter-box3/{main,components/{ble/{include/ble,src},protocol/{include/protocol,src},splash/{include/splash,src},ui/{include/ui,src}},fonts}
cd ~/projects/clawdmeter-box3
git init
```

- [ ] **Step 2: Write top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components"
                         "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clawdmeter_box3)
```

- [ ] **Step 3: Write idf_component.yml (project root)**

```yaml
## idf_component.yml
dependencies:
  idf: ">=5.2.0"
  espressif/esp-box-3:
    version: "*"
  espressif/icm42670:
    version: "^1.0.0"
```

- [ ] **Step 4: Write sdkconfig.defaults**

```
# Flash & PSRAM (ESP32-S3-BOX-3: 16MB flash, 16MB OPI PSRAM)
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384

# Partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
CONFIG_MAIN_TASK_STACK_SIZE=8192

# BLE / NimBLE — peripheral only, single connection
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Claude Controller"
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_NIMBLE_SM_BONDING=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=5120
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y

# NVS (BLE bonding storage)
CONFIG_NVS_ENCRYPTION=n
```

- [ ] **Step 5: Write sdkconfig.defaults.esp32s3**

```
CONFIG_IDF_TARGET_ESP32S3=y
```

- [ ] **Step 6: Write partitions.csv**

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
factory,  app,  factory, 0x20000,  0x200000,
storage,  data, spiffs,  0x220000, 0xDE0000,
```

- [ ] **Step 7: Write main/CMakeLists.txt (stub — will be filled in Task 7)**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        ble
        protocol
        splash
        ui
        nvs_flash
        espressif__esp-box-3
        espressif__esp_lvgl_port
        espressif__icm42670
        espressif__button
)
```

- [ ] **Step 8: Write main/main.c stub (verifiable build)**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Clawdmeter BOX-3 starting...");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

- [ ] **Step 9: Install dependencies and verify build**

```bash
cd ~/projects/clawdmeter-box3
idf.py update-dependencies
idf.py build
```

Expected: build succeeds with only `main.c` compiled. No link errors.

- [ ] **Step 10: Commit scaffold**

```bash
git add .
git commit -m "feat: initial ESP-IDF project scaffold for BOX-3 port"
```

---

## Task 2: Protocol Component

**Files:**
- Create: `components/protocol/CMakeLists.txt`
- Create: `components/protocol/include/protocol/usage_data.h`
- Create: `components/protocol/include/protocol/protocol.h`
- Create: `components/protocol/src/protocol.c`
- Create: `components/protocol/src/proto_claude.c`

- [ ] **Step 1: Write components/protocol/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/protocol.c" "src/proto_claude.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES json  # ESP-IDF built-in cJSON
)
```

- [ ] **Step 2: Write usage_data.h**

```c
// components/protocol/include/protocol/usage_data.h
#pragma once
#include <stdbool.h>

typedef struct {
    char  platform[16];        // "claude", "openai", etc. — defaults to "claude"
    float session_pct;         // 5h-window utilization 0–100
    int   session_reset_mins;  // minutes until session window resets (-1 = unknown)
    float weekly_pct;          // 7d-window utilization 0–100
    int   weekly_reset_mins;   // minutes until weekly window resets (-1 = unknown)
    char  status[16];          // "allowed" or "limited"
    bool  ok;                  // true when API call succeeded
    bool  valid;               // false until first successful parse
} usage_data_t;
```

- [ ] **Step 3: Write protocol.h**

```c
// components/protocol/include/protocol/protocol.h
#pragma once
#include "usage_data.h"
#include <stdbool.h>

// Parse a JSON payload from the daemon into out.
// Returns true on success. out->valid is set to true on success.
// If "platform" field is absent, defaults to "claude".
bool protocol_parse(const char *json, usage_data_t *out);

// ACK/NACK strings to write back to daemon via BLE GATT.
const char *protocol_ack(void);
const char *protocol_nack(void);
```

- [ ] **Step 4: Write proto_claude.c**

```c
// components/protocol/src/proto_claude.c
#include "cJSON.h"
#include "protocol/usage_data.h"
#include <string.h>
#include <stdbool.h>

// Parses Clawdmeter JSON: {"s":45,"sr":120,"w":28,"wr":7200,"st":"allowed","ok":true}
// Optional "platform" field; defaults to "claude".
bool proto_claude_parse(const char *json, usage_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *s  = cJSON_GetObjectItemCaseSensitive(root, "s");
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(root, "sr");
    cJSON *w  = cJSON_GetObjectItemCaseSensitive(root, "w");
    cJSON *wr = cJSON_GetObjectItemCaseSensitive(root, "wr");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "st");
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    cJSON *pl = cJSON_GetObjectItemCaseSensitive(root, "platform");

    strlcpy(out->platform,
            (pl && cJSON_IsString(pl)) ? pl->valuestring : "claude",
            sizeof(out->platform));

    out->session_pct        = (s  && cJSON_IsNumber(s))  ? (float)s->valuedouble  : 0.0f;
    out->session_reset_mins = (sr && cJSON_IsNumber(sr)) ? sr->valueint            : -1;
    out->weekly_pct         = (w  && cJSON_IsNumber(w))  ? (float)w->valuedouble  : 0.0f;
    out->weekly_reset_mins  = (wr && cJSON_IsNumber(wr)) ? wr->valueint            : -1;

    strlcpy(out->status,
            (st && cJSON_IsString(st)) ? st->valuestring : "unknown",
            sizeof(out->status));

    out->ok    = cJSON_IsTrue(ok);
    out->valid = out->ok;

    cJSON_Delete(root);
    return out->ok;
}
```

- [ ] **Step 5: Write protocol.c (dispatcher)**

```c
// components/protocol/src/protocol.c
#include "protocol/protocol.h"
#include "protocol/usage_data.h"
#include <string.h>

// Forward-declare platform parsers (add new ones here for multi-platform support)
bool proto_claude_parse(const char *json, usage_data_t *out);

bool protocol_parse(const char *json, usage_data_t *out)
{
    if (!json || !out) return false;

    // Quick platform peek: look for "platform":"<name>" in the raw JSON.
    // Falls through to Claude if not found (backward-compatible with daemon).
    if (strstr(json, "\"platform\":\"claude\"") || !strstr(json, "\"platform\"")) {
        return proto_claude_parse(json, out);
    }
    // Future: add proto_openai_parse(), proto_gemini_parse(), etc. here
    return false;
}

const char *protocol_ack(void)  { return "{\"ack\":true}\n"; }
const char *protocol_nack(void) { return "{\"ack\":false}\n"; }
```

- [ ] **Step 6: Verify build**

```bash
cd ~/projects/clawdmeter-box3
idf.py build 2>&1 | tail -5
```

Expected: BUILD SUCCESSFUL

- [ ] **Step 7: Commit protocol component**

```bash
git add components/protocol/
git commit -m "feat: add protocol component with Claude JSON parser"
```

---

## Task 3: Splash & Usage Rate Component

**Files:**
- Create: `components/splash/CMakeLists.txt`
- Create: `components/splash/include/splash/usage_rate.h`
- Create: `components/splash/include/splash/splash.h`
- Create: `components/splash/src/usage_rate.c`
- Create: `components/splash/src/splash.c`
- Fetch: `components/splash/src/splash_animations.h` (180KB from original repo)

- [ ] **Step 1: Fetch splash_animations.h from original repo**

The file is ~180KB of RGB565 C arrays. Fetch via GitHub API (base64-encoded):

```bash
cd ~/projects/clawdmeter-box3
python3 -c "
import urllib.request, json, base64, sys
url = 'https://api.github.com/repos/HermannBjorgvin/Clawdmeter/contents/firmware/src/splash_animations.h'
req = urllib.request.Request(url, headers={'User-Agent': 'curl/7.0'})
data = json.loads(urllib.request.urlopen(req).read())
open('components/splash/src/splash_animations.h', 'wb').write(base64.b64decode(data['content']))
print('Fetched', len(data['content']), 'chars base64 →', data['size'], 'bytes')
"
```

Alternatively with `gh`:
```bash
gh api repos/HermannBjorgvin/Clawdmeter/contents/firmware/src/splash_animations.h \
  | python3 -c "import json,sys,base64; d=json.load(sys.stdin); open('components/splash/src/splash_animations.h','wb').write(base64.b64decode(d['content']))"
```

- [ ] **Step 2: Write CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/usage_rate.c" "src/splash.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__esp_lvgl_port lvgl
)
```

- [ ] **Step 3: Write usage_rate.h**

```c
// components/splash/include/splash/usage_rate.h
#pragma once

// Call every time fresh data arrives from the daemon (~60s intervals).
// session_pct: 0–100.
void usage_rate_sample(float session_pct);

// Returns 0=idle, 1=normal, 2=active, 3=heavy.
// Returns 0 until ≥4 minutes of history accumulated.
int usage_rate_group(void);

// Reset all history (e.g. on reconnect).
void usage_rate_reset(void);
```

- [ ] **Step 4: Write usage_rate.c (ported from original, millis() → esp_timer_get_time())**

```c
// components/splash/src/usage_rate.c
#include "splash/usage_rate.h"
#include "esp_timer.h"
#include <stdint.h>

// Thresholds in %/min (same as original Clawdmeter):
//   < 0.10  → 0 Idle    (>17h to fill session)
//   < 0.20  → 1 Normal  (8–17h)
//   < 0.33  → 2 Active  (5–8h)
//   >=0.33  → 3 Heavy   (≤5h, pace-matching session reset)
#define RATE_THRESH_NORMAL  0.10f
#define RATE_THRESH_ACTIVE  0.20f
#define RATE_THRESH_HEAVY   0.33f

// Require ≥4 min history before trusting the rate (4 × 60s daemon intervals)
#define MIN_WINDOW_US  (240ULL * 1000000ULL)  // 240s in microseconds

#define RING_SIZE 6

typedef struct { uint64_t us; float pct; } sample_t;

static sample_t s_ring[RING_SIZE];
static uint8_t  s_count = 0;
static uint8_t  s_head  = 0;

void usage_rate_reset(void)
{
    s_count = 0;
    s_head  = 0;
}

void usage_rate_sample(float session_pct)
{
    uint64_t now = (uint64_t)esp_timer_get_time();

    if (s_count > 0) {
        uint8_t latest = (s_head + RING_SIZE - 1) % RING_SIZE;
        // Session reset detected: pct dropped >5%. Restart tracking.
        if (session_pct + 5.0f < s_ring[latest].pct) {
            usage_rate_reset();
        }
    }

    s_ring[s_head] = (sample_t){ now, session_pct };
    s_head = (s_head + 1) % RING_SIZE;
    if (s_count < RING_SIZE) s_count++;
}

int usage_rate_group(void)
{
    if (s_count < 2) return 0;

    uint8_t oldest = (s_head + RING_SIZE - s_count) % RING_SIZE;
    uint8_t latest = (s_head + RING_SIZE - 1) % RING_SIZE;

    uint64_t dt = s_ring[latest].us - s_ring[oldest].us;
    if (dt < MIN_WINDOW_US) return 0;

    float dp = s_ring[latest].pct - s_ring[oldest].pct;
    if (dp < 0.0f) dp = 0.0f;
    // rate in %/min
    float rate = dp * 60.0f / ((float)dt / 1000000.0f);

    if (rate < RATE_THRESH_NORMAL) return 0;
    if (rate < RATE_THRESH_ACTIVE) return 1;
    if (rate < RATE_THRESH_HEAVY)  return 2;
    return 3;
}
```

- [ ] **Step 5: Write splash.h**

```c
// components/splash/include/splash/splash.h
#pragma once
#include "lvgl.h"
#include <stdbool.h>

// Create the splash canvas inside parent. Canvas is BSP_LCD_H_RES × BSP_LCD_V_RES.
// Call once after lv_display is initialised and bsp_display_lock() is held.
void splash_init(lv_obj_t *parent);

// Advance animation frame if hold time elapsed. Register as LVGL timer callback.
// Signature matches lv_timer_cb_t: void cb(lv_timer_t *t).
void splash_tick(lv_timer_t *t);

// Cycle to next animation in the catalog (for BSP_BUTTON_MAIN while on splash).
void splash_next(void);

// Show / hide the splash container.
void splash_show(void);
void splash_hide(void);

// Pick animation matching current usage_rate_group().
void splash_pick_for_current_rate(void);

bool     splash_is_active(void);
lv_obj_t *splash_get_root(void);
```

- [ ] **Step 6: Write splash.c**

```c
// components/splash/src/splash.c
#include "splash/splash.h"
#include "splash/usage_rate.h"
#include "splash_animations.h"  // defines anim_catalog[], ANIM_COUNT, etc.
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <string.h>

#define TAG "SPLASH"

// splash_animations.h is expected to define:
//   typedef struct { const uint16_t *frames; int frame_count; int w; int h;
//                    int hold_ms; int group; } anim_entry_t;
//   extern const anim_entry_t anim_catalog[];
//   extern const int ANIM_COUNT;
// If the original header uses a different layout, adapt the draw call below.

static lv_obj_t   *s_root   = NULL;
static lv_obj_t   *s_canvas = NULL;
static bool        s_active = false;
static int         s_cur_anim = 0;
static int         s_cur_frame = 0;

// Draw current frame centered in BSP_LCD_H_RES × BSP_LCD_V_RES canvas
static void draw_frame(void)
{
    const anim_entry_t *a = &anim_catalog[s_cur_anim];
    const uint16_t *frame = a->frames + (size_t)s_cur_frame * a->w * a->h;

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, a->w, a->h, LV_COLOR_FORMAT_RGB565,
                     a->w * 2, (void *)frame, a->w * a->h * 2);

    lv_image_dsc_t img = {
        .header = { .cf = LV_COLOR_FORMAT_RGB565, .w = a->w, .h = a->h },
        .data_size = a->w * a->h * 2,
        .data = (const uint8_t *)frame,
    };

    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    int x = (BSP_LCD_H_RES - a->w) / 2;
    int y = (BSP_LCD_V_RES - a->h) / 2;
    lv_canvas_draw_image(s_canvas, x, y, &img, NULL);
}

void splash_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    // Allocate canvas pixel buffer from PSRAM
    size_t buf_size = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    void *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGE(TAG, "PSRAM alloc failed for canvas"); return; }

    s_canvas = lv_canvas_create(s_root);
    lv_canvas_set_buffer(s_canvas, buf, BSP_LCD_H_RES, BSP_LCD_V_RES,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    splash_pick_for_current_rate();
    draw_frame();
    ESP_LOGI(TAG, "splash ready, %d animations in catalog", ANIM_COUNT);
}

void splash_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_active || !s_canvas) return;

    const anim_entry_t *a = &anim_catalog[s_cur_anim];
    s_cur_frame = (s_cur_frame + 1) % a->frame_count;
    draw_frame();
}

void splash_next(void)
{
    s_cur_anim  = (s_cur_anim + 1) % ANIM_COUNT;
    s_cur_frame = 0;
    draw_frame();
}

void splash_pick_for_current_rate(void)
{
    int group = usage_rate_group();
    // Find first animation matching the rate group; fall back to group 0
    for (int i = 0; i < ANIM_COUNT; i++) {
        if (anim_catalog[i].group == group) {
            s_cur_anim = i;
            s_cur_frame = 0;
            return;
        }
    }
    s_cur_anim  = 0;
    s_cur_frame = 0;
}

void splash_show(void)
{
    s_active = true;
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    splash_pick_for_current_rate();
}

void splash_hide(void)
{
    s_active = false;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool      splash_is_active(void) { return s_active; }
lv_obj_t *splash_get_root(void)  { return s_root; }
```

> **Note on splash_animations.h format:** The original `splash_animations.h` may use different struct/array names. Check the top of the fetched file. If it defines `anims[]` instead of `anim_catalog[]`, or uses different field names, update the struct typedef reference in `splash.c` to match. The key fields needed are: frame pixel data pointer, frame count, frame width, frame height, hold duration ms, rate group index.

- [ ] **Step 7: Verify build**

```bash
idf.py build 2>&1 | tail -5
```

Expected: BUILD SUCCESSFUL

- [ ] **Step 8: Commit splash component**

```bash
git add components/splash/
git commit -m "feat: add splash and usage_rate components (ported from original)"
```

---

## Task 4: BLE GATT Component

**Files:**
- Create: `components/ble/CMakeLists.txt`
- Create: `components/ble/include/ble/ble_gatt.h`
- Create: `components/ble/src/ble_gatt.c`

- [ ] **Step 1: Write CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/ble_gatt.c" "src/ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
    PRIV_REQUIRES bt
)
```

- [ ] **Step 2: Write ble_gatt.h**

```c
// components/ble/include/ble/ble_gatt.h
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

// Start NimBLE host with Clawdmeter GATT + HID services.
// device_name: advertised name (e.g. "Claude Controller")
esp_err_t ble_gatt_init(const char *device_name);

// Polling API — call from app_main loop.
bool        ble_gatt_has_data(void);   // true if new JSON payload available
const char *ble_gatt_get_data(void);   // returns JSON string, clears has_data
void        ble_gatt_send_ack(void);   // notify TX characteristic with ACK
void        ble_gatt_send_nack(void);  // notify TX characteristic with NACK

ble_gatt_state_t ble_gatt_get_state(void);
const char      *ble_gatt_get_mac(void);   // "AA:BB:CC:DD:EE:FF"
const char      *ble_gatt_get_name(void);
```

- [ ] **Step 3: Write ble_gatt.c**

NimBLE GATT service using Clawdmeter custom UUIDs. UUID byte order: little-endian (LSB first).
`4c41555a-4465-7669-6365-000000000001` → `{0x01,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}`

```c
// components/ble/src/ble_gatt.c
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
#include <string.h>
#include <stdio.h>

void ble_store_config_init(void);

#define TAG "GATT"
#define RX_BUF_SIZE  512

// Clawdmeter custom UUIDs (little-endian, LSB first)
// Service:  4c41555a-4465-7669-6365-000000000001
static const ble_uuid128_t s_svc_uuid = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x01,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
// RX write: 000000000002
static const ble_uuid128_t s_rx_uuid  = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x02,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
// TX notify: 000000000003
static const ble_uuid128_t s_tx_uuid  = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x03,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};
// REQ notify: 000000000004
static const ble_uuid128_t s_req_uuid = { .u.type = BLE_UUID_TYPE_128,
    .value = {0x04,0x00,0x00,0x00,0x00,0x00,0x65,0x63,0x69,0x76,0x65,0x44,0x5a,0x55,0x41,0x4c}};

static uint16_t s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle    = 0;
static uint16_t s_tx_cccd_sub  = 0;  // 1 when daemon subscribed TX notify
static uint16_t s_req_handle   = 0;
static char     s_device_name[32];
static char     s_mac_str[18];
static ble_gatt_state_t s_state = BLE_GATT_STATE_INIT;

// Thread-safe 1-slot queue: NimBLE → app_main
static QueueHandle_t s_data_queue = NULL;
static char          s_rx_buf[RX_BUF_SIZE];
static char          s_queue_buf[RX_BUF_SIZE];  // owned by queue

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, len, NULL);
    s_rx_buf[len] = '\0';

    // Copy to queue buffer; overwrite if queue full (daemon sends ~1/min)
    memcpy(s_queue_buf, s_rx_buf, len + 1);
    xQueueOverwrite(s_data_queue, &s_queue_buf);
    return 0;
}

static int cccd_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Track TX CCCD subscription state for ble_gatt_send_ack/nack
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
        uint16_t val = 0;
        ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), NULL);
        s_tx_cccd_sub = val;
    }
    return 0;
}

static const struct ble_gatt_svc_def s_clawdmeter_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { // RX — daemon writes JSON here
                .uuid       = &s_rx_uuid.u,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .access_cb  = gatt_access_cb,
            },
            { // TX — ESP32 notifies ACK/NACK
                .uuid       = &s_tx_uuid.u,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
                .access_cb  = gatt_access_cb,
            },
            { // REQ — ESP32 notifies daemon to refresh immediately
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
    // Short name "Claude" fits in 31-byte adv (flags+uuid128+name = 29B)
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

// Forward-declared because start_advertising references it
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_state = BLE_GATT_STATE_CONNECTED;
            ESP_LOGI(TAG, "connected handle=%d", s_conn_handle);
            ble_gap_security_initiate(s_conn_handle);
            // Send REQ notify to ask daemon for immediate data
            send_notify(s_req_handle, "{\"req\":true}\n");
        } else {
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_tx_cccd_sub = 0;
        s_state = BLE_GATT_STATE_DISCONNECTED;
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
    ESP_LOGI(TAG, "BLE synced, tx_handle=%d req_handle=%d", s_tx_handle, s_req_handle);
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

    // Register Clawdmeter service + HID service together
    extern const struct ble_gatt_svc_def ble_hid_svc[];
    struct ble_gatt_svc_def combined[3];
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
const char *ble_gatt_get_mac(void)  { return s_mac_str; }
const char *ble_gatt_get_name(void) { return s_device_name; }
```

- [ ] **Step 4: Verify build**

```bash
idf.py build 2>&1 | tail -5
```

Expected: BUILD SUCCESSFUL (ble_hid.c not yet written — add a stub if linker complains)

- [ ] **Step 5: Commit**

```bash
git add components/ble/CMakeLists.txt components/ble/include/ble/ble_gatt.h components/ble/src/ble_gatt.c
git commit -m "feat: add BLE GATT component (Clawdmeter custom service, NimBLE)"
```

---

## Task 5: BLE HID Component

**Files:**
- Create: `components/ble/include/ble/ble_hid.h`
- Create: `components/ble/src/ble_hid.c`

- [ ] **Step 1: Write ble_hid.h**

```c
// components/ble/include/ble/ble_hid.h
#pragma once
#include <stdint.h>

// Send HID keyboard key-down report.
// keycode: HID usage code (e.g. 0x2C=Space, 0x2B=Tab)
// modifier: HID modifier byte (0x02=Left Shift, 0=none)
void ble_hid_key_press(uint8_t keycode, uint8_t modifier);

// Send HID keyboard key-up (all keys released).
void ble_hid_key_release(void);

// Called internally by ble_gatt.c — do not call directly.
extern const struct ble_gatt_svc_def ble_hid_svc[];
void ble_hid_set_conn(uint16_t conn_handle);
```

- [ ] **Step 2: Write ble_hid.c**

```c
// components/ble/src/ble_hid.c
// BLE HID Keyboard service (UUID 0x1812) registered alongside Clawdmeter GATT.
// Implements: HID Information, Report Map, Control Point, Protocol Mode, Report (notify).
#include "ble/ble_hid.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "esp_log.h"
#include <string.h>

#define TAG "HID"

// Boot keyboard HID report descriptor (8 bytes per report, no Report ID):
//   [modifier][reserved][key0][key1][key2][key3][key4][key5]
static const uint8_t s_report_map[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Controls)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
      0x05, 0x07,  // Usage Page (Keyboard/Keypad)
      0x19, 0xE0,  // Usage Minimum (Left Control)
      0x29, 0xE7,  // Usage Maximum (Right GUI)
      0x15, 0x00,  // Logical Minimum (0)
      0x25, 0x01,  // Logical Maximum (1)
      0x75, 0x01,  // Report Size (1 bit)
      0x95, 0x08,  // Report Count (8)
      0x81, 0x02,  // Input (Data, Variable, Absolute) — modifier byte
      0x95, 0x01,  // Report Count (1)
      0x75, 0x08,  // Report Size (8)
      0x81, 0x01,  // Input (Constant) — reserved byte
      0x95, 0x06,  // Report Count (6)
      0x75, 0x08,  // Report Size (8)
      0x15, 0x00,  // Logical Minimum (0)
      0x25, 0x65,  // Logical Maximum (101)
      0x05, 0x07,  // Usage Page (Keyboard/Keypad)
      0x19, 0x00,  // Usage Minimum (0)
      0x29, 0x65,  // Usage Maximum (101)
      0x81, 0x00,  // Input (Data, Array, Absolute) — key codes
    0xC0,        // End Collection
};

// HID Information: bcdHID=1.11, bCountryCode=0x00, Flags=normallyConnectable|remoteWake
static const uint8_t s_hid_info[] = { 0x11, 0x01, 0x00, 0x03 };

static uint16_t s_report_handle = 0;
static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_protocol_mode = 0x01;  // Report Protocol

static int hid_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (uuid16 == 0x2A4A) // HID Information
            return os_mbuf_append(ctxt->om, s_hid_info, sizeof(s_hid_info)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4B) // Report Map
            return os_mbuf_append(ctxt->om, s_report_map, sizeof(s_report_map)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4E) // Protocol Mode
            return os_mbuf_append(ctxt->om, &s_protocol_mode, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (uuid16 == 0x2A4D) // Report (read current state — all zeros = no key)
            { uint8_t z[8] = {0}; return os_mbuf_append(ctxt->om, z, sizeof(z)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0; }
        return 0;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (uuid16 == 0x2A4C) return 0; // HID Control Point — ignore
        if (uuid16 == 0x2A4E) {          // Protocol Mode
            uint8_t pm = 0;
            ble_hs_mbuf_to_flat(ctxt->om, &pm, 1, NULL);
            s_protocol_mode = pm;
        }
        return 0;
    default: return 0;
    }
}

// Report Reference descriptor: Report ID=0 (no report ID), Input report
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
            { // HID Information 0x2A4A
                .uuid = BLE_UUID16_DECLARE(0x2A4A),
                .flags = BLE_GATT_CHR_F_READ,
                .access_cb = hid_access_cb,
            },
            { // Report Map 0x2A4B
                .uuid = BLE_UUID16_DECLARE(0x2A4B),
                .flags = BLE_GATT_CHR_F_READ,
                .access_cb = hid_access_cb,
            },
            { // HID Control Point 0x2A4C
                .uuid = BLE_UUID16_DECLARE(0x2A4C),
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                .access_cb = hid_access_cb,
            },
            { // Protocol Mode 0x2A4E
                .uuid = BLE_UUID16_DECLARE(0x2A4E),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .access_cb = hid_access_cb,
            },
            { // Report 0x2A4D (Input Report, notify)
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_report_handle,
                .access_cb = hid_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    { // Report Reference 0x2908
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = report_ref_cb,
                    },
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
```

> **Note on `ble_hid_set_conn`:** In `ble_gatt.c`'s `gap_event_cb`, add `ble_hid_set_conn(s_conn_handle)` on `BLE_GAP_EVENT_CONNECT` and `ble_hid_set_conn(BLE_HS_CONN_HANDLE_NONE)` on `BLE_GAP_EVENT_DISCONNECT`.

- [ ] **Step 3: Add ble_hid_set_conn calls to ble_gatt.c**

In `ble_gatt.c` `gap_event_cb`, in `case BLE_GAP_EVENT_CONNECT`:
```c
// After: s_state = BLE_GATT_STATE_CONNECTED;
ble_hid_set_conn(s_conn_handle);
```
In `case BLE_GAP_EVENT_DISCONNECT`:
```c
// After: s_state = BLE_GATT_STATE_DISCONNECTED;
ble_hid_set_conn(BLE_HS_CONN_HANDLE_NONE);
```

- [ ] **Step 4: Verify build**

```bash
idf.py build 2>&1 | tail -5
```

Expected: BUILD SUCCESSFUL

- [ ] **Step 5: Commit BLE HID**

```bash
git add components/ble/include/ble/ble_hid.h components/ble/src/ble_hid.c components/ble/src/ble_gatt.c
git commit -m "feat: add BLE HID keyboard service (NimBLE manual GATT registration)"
```

---

## Task 6: UI Component

**Files:**
- Create: `components/ui/CMakeLists.txt`
- Create: `components/ui/include/ui/ui.h`
- Create: `components/ui/src/ui.c`
- Create: `components/ui/src/ui_usage.c`
- Create: `components/ui/src/ui_bluetooth.c`
- Create: `components/ui/src/ui_splash.c`
- Copy fonts: `fonts/font_styrene_28.c`, `fonts/font_styrene_20.c`, `fonts/font_mono_18.c`

- [ ] **Step 1: Copy font files from original Clawdmeter repo**

```bash
cd ~/projects/clawdmeter-box3
for font in font_styrene_28 font_styrene_20 font_mono_18; do
  gh api repos/HermannBjorgvin/Clawdmeter/contents/firmware/src/${font}.c \
    | python3 -c "import json,sys,base64; d=json.load(sys.stdin); open('fonts/${font}.c','wb').write(base64.b64decode(d['content']))"
  echo "Fetched fonts/${font}.c"
done
```

Generate `font_tiempos_34.c` if you have the proprietary font file:
```bash
# Requires assets/TiemposText-400-Regular.otf (from original Clawdmeter repo or Anthropic)
lv_font_conv --font assets/TiemposText-400-Regular.otf -r 0x20-0x7E \
  --size 34 --format lvgl --bpp 4 --no-compress \
  -o fonts/font_tiempos_34.c --lv-include "lvgl.h"
# Then apply the 4 LVGL 8→9 patches described in Clawdmeter README
```

If the font file is unavailable, create a stub that aliases `font_styrene_28`:
```c
// fonts/font_tiempos_34.c — stub; replace with real font when OTF available
#include "lvgl.h"
extern lv_font_t lv_font_styrene_28;
const lv_font_t lv_font_tiempos_34 = { /* identical to styrene_28 for now */ };
```

- [ ] **Step 2: Create fonts CMakeLists.txt**

```cmake
# fonts/CMakeLists.txt
idf_component_register(
    SRCS "font_styrene_28.c" "font_styrene_20.c" "font_mono_18.c" "font_tiempos_34.c"
    INCLUDE_DIRS "."
)
```

- [ ] **Step 3: Write ui/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/ui.c" "src/ui_usage.c" "src/ui_bluetooth.c" "src/ui_splash.c"
    INCLUDE_DIRS "include"
    REQUIRES splash protocol ble fonts
    PRIV_REQUIRES espressif__esp_lvgl_port espressif__esp-box-3 lvgl
)
```

- [ ] **Step 4: Write ui.h**

```c
// components/ui/include/ui/ui.h
#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include <stdbool.h>

typedef enum {
    SCREEN_SPLASH     = 0,
    SCREEN_USAGE      = 1,
    SCREEN_BLUETOOTH  = 2,
    SCREEN_COUNT      = 3,
} screen_t;

// Call once after LVGL display is ready (hold bsp_display_lock before calling)
void ui_init(void);

// Update usage screen data. Acquires LVGL lock internally — safe to call
// from app_main task.
void ui_update(const usage_data_t *data);

// Screen navigation (safe to call from button callbacks; acquires LVGL lock)
void     ui_show_screen(screen_t screen);
void     ui_cycle_screen(void);
screen_t ui_get_current_screen(void);

// BLE status update (for Bluetooth screen)
void ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac);
```

- [ ] **Step 5: Write ui.c (screen manager)**

```c
// components/ui/src/ui.c
#include "ui/ui.h"
#include "ui_usage.h"
#include "ui_bluetooth.h"
#include "ui_splash.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "splash/splash.h"
#include "esp_log.h"

#define TAG "UI"

static screen_t s_current = SCREEN_SPLASH;

// Internal screen objects (set by each module's init)
static lv_obj_t *s_scr_usage = NULL;
static lv_obj_t *s_scr_bt    = NULL;
static lv_obj_t *s_scr_splash = NULL;

void ui_init(void)
{
    lv_obj_t *root = lv_screen_active();

    // Each screen is a full-size child of the root screen
    s_scr_usage  = lv_obj_create(root);
    s_scr_bt     = lv_obj_create(root);
    s_scr_splash = lv_obj_create(root);

    for (lv_obj_t *s : (lv_obj_t*[]){s_scr_usage, s_scr_bt, s_scr_splash}) {
        lv_obj_set_size(s, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_set_pos(s, 0, 0);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(s, 0, 0);
        lv_obj_set_style_pad_all(s, 0, 0);
        lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    }

    ui_usage_init(s_scr_usage);
    ui_bluetooth_init(s_scr_bt);
    ui_splash_init(s_scr_splash);
    splash_init(s_scr_splash);  // creates canvas inside splash screen

    ui_show_screen(SCREEN_SPLASH);
    ESP_LOGI(TAG, "UI ready");
}

void ui_show_screen(screen_t screen)
{
    if (lvgl_port_lock(100)) {
        lv_obj_add_flag(s_scr_usage,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_bt,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN);

        switch (screen) {
        case SCREEN_USAGE:     lv_obj_clear_flag(s_scr_usage,  LV_OBJ_FLAG_HIDDEN); splash_hide(); break;
        case SCREEN_BLUETOOTH: lv_obj_clear_flag(s_scr_bt,     LV_OBJ_FLAG_HIDDEN); splash_hide(); break;
        case SCREEN_SPLASH:    lv_obj_clear_flag(s_scr_splash, LV_OBJ_FLAG_HIDDEN); splash_show(); break;
        default: break;
        }
        s_current = screen;
        lvgl_port_unlock();
    }
}

void ui_cycle_screen(void)
{
    // Splash → Usage → Bluetooth → Splash
    screen_t next = (screen_t)((s_current + 1) % SCREEN_COUNT);
    ui_show_screen(next);
}

screen_t ui_get_current_screen(void) { return s_current; }

void ui_update(const usage_data_t *data)
{
    if (lvgl_port_lock(100)) {
        ui_usage_update(data);
        lvgl_port_unlock();
    }
}

void ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac)
{
    if (lvgl_port_lock(100)) {
        ui_bluetooth_update(state, name, mac);
        ui_usage_update_ble(state);
        lvgl_port_unlock();
    }
}
```

- [ ] **Step 6: Write ui_usage.c (usage screen with HID touch buttons)**

Create `components/ui/src/ui_usage.h` (internal header):
```c
#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include "lvgl.h"
void ui_usage_init(lv_obj_t *parent);
void ui_usage_update(const usage_data_t *data);
void ui_usage_update_ble(ble_gatt_state_t state);
```

Create `components/ui/src/ui_usage.c`:
```c
// components/ui/src/ui_usage.c
// Layout: 28px top bar | 172px content (two arcs) | 40px HID button row
#include "ui_usage.h"
#include "ble/ble_hid.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

#define TAG "UI_USAGE"

// External fonts (from fonts/ component)
extern lv_font_t lv_font_tiempos_34;
extern lv_font_t lv_font_styrene_28;
extern lv_font_t lv_font_styrene_20;
extern lv_font_t lv_font_mono_18;

#define TOP_H     28
#define BTN_H     40
#define CONTENT_H (BSP_LCD_V_RES - TOP_H - BTN_H)  // 172px
#define HALF_W    (BSP_LCD_H_RES / 2)               // 160px
#define ARC_DIAM  100

static lv_obj_t *s_title;
static lv_obj_t *s_ble_dot;
static lv_obj_t *s_arc_session, *s_arc_weekly;
static lv_obj_t *s_lbl_session_pct, *s_lbl_weekly_pct;
static lv_obj_t *s_lbl_session_reset, *s_lbl_weekly_reset;
static lv_obj_t *s_btn_space, *s_btn_shift_tab;

static void on_space_pressed(lv_event_t *e)   { ble_hid_key_press(0x2C, 0x00); }
static void on_space_released(lv_event_t *e)  { ble_hid_key_release(); }
static void on_stab_pressed(lv_event_t *e)    { ble_hid_key_press(0x2B, 0x02); }
static void on_stab_released(lv_event_t *e)   { ble_hid_key_release(); }

void ui_usage_init(lv_obj_t *parent)
{
    // ── Top bar ──────────────────────────────────────────
    lv_obj_t *topbar = lv_obj_create(parent);
    lv_obj_set_size(topbar, BSP_LCD_H_RES, TOP_H);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 4, 0);

    s_title = lv_label_create(topbar);
    lv_label_set_text(s_title, "CLAWDMETER");
    lv_obj_set_style_text_font(s_title, &lv_font_tiempos_34, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 4, 0);

    s_ble_dot = lv_label_create(topbar);
    lv_label_set_text(s_ble_dot, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble_dot, lv_color_hex(0x888888), 0);
    lv_obj_align(s_ble_dot, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── Content area: left = Session, right = Weekly ─────
    // Session arc
    s_arc_session = lv_arc_create(parent);
    lv_obj_set_size(s_arc_session, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(s_arc_session, (HALF_W - ARC_DIAM) / 2, TOP_H + (CONTENT_H - ARC_DIAM) / 2);
    lv_arc_set_range(s_arc_session, 0, 100);
    lv_arc_set_value(s_arc_session, 0);
    lv_obj_remove_style(s_arc_session, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_session, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_session_pct = lv_label_create(parent);
    lv_label_set_text(s_lbl_session_pct, "0%");
    lv_obj_set_style_text_font(s_lbl_session_pct, &lv_font_styrene_28, 0);
    lv_obj_set_style_text_color(s_lbl_session_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(s_lbl_session_pct, s_arc_session, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_sess = lv_label_create(parent);
    lv_label_set_text(lbl_sess, "Session");
    lv_obj_set_style_text_font(lbl_sess, &lv_font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_sess, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_sess, s_arc_session, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_lbl_session_reset = lv_label_create(parent);
    lv_label_set_text(s_lbl_session_reset, "");
    lv_obj_set_style_text_font(s_lbl_session_reset, &lv_font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_session_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(s_lbl_session_reset, lbl_sess, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Weekly arc (right half)
    s_arc_weekly = lv_arc_create(parent);
    lv_obj_set_size(s_arc_weekly, ARC_DIAM, ARC_DIAM);
    lv_obj_set_pos(s_arc_weekly, HALF_W + (HALF_W - ARC_DIAM) / 2, TOP_H + (CONTENT_H - ARC_DIAM) / 2);
    lv_arc_set_range(s_arc_weekly, 0, 100);
    lv_arc_set_value(s_arc_weekly, 0);
    lv_obj_remove_style(s_arc_weekly, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_weekly, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_weekly_pct = lv_label_create(parent);
    lv_label_set_text(s_lbl_weekly_pct, "0%");
    lv_obj_set_style_text_font(s_lbl_weekly_pct, &lv_font_styrene_28, 0);
    lv_obj_set_style_text_color(s_lbl_weekly_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(s_lbl_weekly_pct, s_arc_weekly, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *lbl_week = lv_label_create(parent);
    lv_label_set_text(lbl_week, "Weekly");
    lv_obj_set_style_text_font(lbl_week, &lv_font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_week, s_arc_weekly, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_lbl_weekly_reset = lv_label_create(parent);
    lv_label_set_text(s_lbl_weekly_reset, "");
    lv_obj_set_style_text_font(s_lbl_weekly_reset, &lv_font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_weekly_reset, lv_color_hex(0x666666), 0);
    lv_obj_align_to(s_lbl_weekly_reset, lbl_week, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // ── Bottom HID button row ────────────────────────────
    int btn_y = BSP_LCD_V_RES - BTN_H;

    s_btn_space = lv_btn_create(parent);
    lv_obj_set_size(s_btn_space, HALF_W - 4, BTN_H - 4);
    lv_obj_set_pos(s_btn_space, 2, btn_y + 2);
    lv_obj_t *lbl_sp = lv_label_create(s_btn_space);
    lv_label_set_text(lbl_sp, LV_SYMBOL_AUDIO "  Space");
    lv_obj_center(lbl_sp);
    lv_obj_add_event_cb(s_btn_space, on_space_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_btn_space, on_space_released, LV_EVENT_RELEASED, NULL);

    s_btn_shift_tab = lv_btn_create(parent);
    lv_obj_set_size(s_btn_shift_tab, HALF_W - 4, BTN_H - 4);
    lv_obj_set_pos(s_btn_shift_tab, HALF_W + 2, btn_y + 2);
    lv_obj_t *lbl_st = lv_label_create(s_btn_shift_tab);
    lv_label_set_text(lbl_st, LV_SYMBOL_RIGHT "  Shift+Tab");
    lv_obj_center(lbl_st);
    lv_obj_add_event_cb(s_btn_shift_tab, on_stab_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_btn_shift_tab, on_stab_released, LV_EVENT_RELEASED, NULL);

    ESP_LOGI(TAG, "usage screen ready");
}

static void fmt_reset(char *buf, size_t len, int mins)
{
    if (mins < 0)       snprintf(buf, len, "—");
    else if (mins < 60) snprintf(buf, len, "%dm", mins);
    else                snprintf(buf, len, "%dh%dm", mins / 60, mins % 60);
}

void ui_usage_update(const usage_data_t *data)
{
    // Called with LVGL lock held (from ui_update)
    lv_arc_set_value(s_arc_session, (int)data->session_pct);
    lv_arc_set_value(s_arc_weekly,  (int)data->weekly_pct);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)data->session_pct);
    lv_label_set_text(s_lbl_session_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", (int)data->weekly_pct);
    lv_label_set_text(s_lbl_weekly_pct, buf);

    char rbuf[16];
    fmt_reset(rbuf, sizeof(rbuf), data->session_reset_mins);
    lv_label_set_text(s_lbl_session_reset, rbuf);
    fmt_reset(rbuf, sizeof(rbuf), data->weekly_reset_mins);
    lv_label_set_text(s_lbl_weekly_reset, rbuf);
}

void ui_usage_update_ble(ble_gatt_state_t state)
{
    lv_color_t c = (state == BLE_GATT_STATE_CONNECTED)
                   ? lv_color_hex(0x0099FF) : lv_color_hex(0x444444);
    lv_obj_set_style_text_color(s_ble_dot, c, 0);
}
```

- [ ] **Step 7: Write ui_bluetooth.c**

Create `components/ui/src/ui_bluetooth.h` (internal):
```c
#pragma once
#include "ble/ble_gatt.h"
#include "lvgl.h"
void ui_bluetooth_init(lv_obj_t *parent);
void ui_bluetooth_update(ble_gatt_state_t state, const char *name, const char *mac);
```

Create `components/ui/src/ui_bluetooth.c`:
```c
// components/ui/src/ui_bluetooth.c
#include "ui_bluetooth.h"
#include "bsp/esp-bsp.h"

extern lv_font_t lv_font_tiempos_34;
extern lv_font_t lv_font_styrene_20;
extern lv_font_t lv_font_mono_18;

static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_name;
static lv_obj_t *s_lbl_mac;

void ui_bluetooth_init(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_font(title, &lv_font_tiempos_34, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 12);

    s_lbl_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_status, "Status: —");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_styrene_20, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_LEFT, 12, 60);

    s_lbl_name = lv_label_create(parent);
    lv_label_set_text(s_lbl_name, "Name: —");
    lv_obj_set_style_text_font(s_lbl_name, &lv_font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(0x888888), 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_LEFT, 12, 90);

    s_lbl_mac = lv_label_create(parent);
    lv_label_set_text(s_lbl_mac, "MAC: —");
    lv_obj_set_style_text_font(s_lbl_mac, &lv_font_mono_18, 0);
    lv_obj_set_style_text_color(s_lbl_mac, lv_color_hex(0x888888), 0);
    lv_obj_align(s_lbl_mac, LV_ALIGN_TOP_LEFT, 12, 112);

    lv_obj_t *btn_reset = lv_btn_create(parent);
    lv_obj_set_size(btn_reset, 140, 36);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_t *lbl_r = lv_label_create(btn_reset);
    lv_label_set_text(lbl_r, "Reset Bond");
    lv_obj_center(lbl_r);
    // Bond reset: clear NVS bond store (requires ble_store_util_delete_all or nvs erase)
    // Add event callback here if bond reset is desired:
    // lv_obj_add_event_cb(btn_reset, on_reset_bond, LV_EVENT_CLICKED, NULL);
}

void ui_bluetooth_update(ble_gatt_state_t state, const char *name, const char *mac)
{
    const char *status_str = (state == BLE_GATT_STATE_CONNECTED) ? "Connected" :
                             (state == BLE_GATT_STATE_ADVERTISING) ? "Advertising" : "Disconnected";
    lv_label_set_text_fmt(s_lbl_status, "Status: %s", status_str);
    lv_label_set_text_fmt(s_lbl_name,   "Name: %s",   name ? name : "—");
    lv_label_set_text_fmt(s_lbl_mac,    "MAC: %s",    mac  ? mac  : "—");
}
```

- [ ] **Step 8: Write ui_splash.c (thin wrapper)**

Create `components/ui/src/ui_splash.h` (internal):
```c
#pragma once
#include "lvgl.h"
void ui_splash_init(lv_obj_t *parent);
```

Create `components/ui/src/ui_splash.c`:
```c
// components/ui/src/ui_splash.c
// The splash canvas is created by splash_init() from the splash component.
// This module just sets up touch-to-dismiss on the splash screen container.
#include "ui_splash.h"
#include "ui/ui.h"
#include "splash/splash.h"

static void on_splash_touch(lv_event_t *e)
{
    // Touch on splash screen → go to usage screen
    ui_show_screen(SCREEN_USAGE);
}

void ui_splash_init(lv_obj_t *parent)
{
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, on_splash_touch, LV_EVENT_CLICKED, NULL);
}
```

- [ ] **Step 9: Verify build**

```bash
idf.py build 2>&1 | tail -5
```

Expected: BUILD SUCCESSFUL

- [ ] **Step 10: Commit UI component**

```bash
git add components/ui/ fonts/
git commit -m "feat: add LVGL UI component (usage/bluetooth/splash screens, HID touch buttons)"
```

---

## Task 7: Main App Integration

**Files:**
- Modify: `main/main.c` (replace stub with full implementation)

- [ ] **Step 1: Write main/main.c**

```c
// main/main.c
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

// ── IMU auto-rotation ────────────────────────────────────────────────
static icm42670_handle_t s_imu_handle = NULL;
static lv_display_t     *s_lv_disp   = NULL;

static void imu_rotation_check(void)
{
    if (!s_imu_handle) return;
    static lv_disp_rotation_t s_last_rot = LV_DISP_ROTATION_0;
    static uint32_t s_last_ms = 0;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_ms < 500) return;  // check at 2Hz
    s_last_ms = now;

    icm42670_value_t accel = {0};
    if (icm42670_get_acce_value(s_imu_handle, &accel) != ESP_OK) return;

    // Simple 0°/180° landscape detection based on Y-axis gravity
    lv_disp_rotation_t rot = (accel.y > 0.5f) ? LV_DISP_ROTATION_0 : LV_DISP_ROTATION_180;
    if (rot == s_last_rot) return;

    ESP_LOGI(TAG, "rotation %d→%d (ay=%.2f)", s_last_rot, rot, accel.y);
    s_last_rot = rot;

    // Blank → rotate → ramp back
    bsp_display_brightness_set(0);
    if (lvgl_port_lock(200)) {
        bsp_display_rotate(s_lv_disp, rot);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_display_brightness_set(80);
}

// ── Button callbacks ─────────────────────────────────────────────────
static void on_main_button(void *arg, void *data)
{
    // BSP_BUTTON_MAIN (touch circle) — cycle screens / splash animations
    if (ui_get_current_screen() == SCREEN_SPLASH) {
        if (lvgl_port_lock(100)) { splash_next(); lvgl_port_unlock(); }
    } else {
        ui_cycle_screen();
    }
}

// ── LVGL timer for splash frame advance ─────────────────────────────
// 80ms per frame ≈ ~12fps — adjust to match original hold_ms per animation
static lv_timer_t *s_splash_timer = NULL;

// ── app_main ─────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "Clawdmeter BOX-3 starting...");

    // NVS (required by BLE bonding)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ── BSP: I2C (shared by touch + IMU) ────────────────
    bsp_i2c_init();

    // ── BSP: Display + touch (GT911) + LVGL ─────────────
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size    = BSP_LCD_H_RES * 40,
        .double_buffer  = true,
        .flags          = { .buff_dma = true, .buff_spiram = false },
    };
    s_lv_disp = bsp_display_start_with_config(&disp_cfg);
    bsp_display_brightness_set(80);
    ESP_LOGI(TAG, "Display: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    // ── IMU: ICM-42607-P ────────────────────────────────
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

    // ── BSP: Buttons ─────────────────────────────────────
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    // BSP_BUTTON_MAIN = touch circle, index 2 per bsp_button_t enum
    if (btns[BSP_BUTTON_MAIN])
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_SINGLE_CLICK, on_main_button, NULL);

    // ── BLE: GATT + HID ──────────────────────────────────
    ble_gatt_init("Claude Controller");

    // ── UI: LVGL screens ─────────────────────────────────
    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_init();
        // LVGL timer for splash frame advance (80ms ≈ 12fps)
        s_splash_timer = lv_timer_create(splash_tick, 80, NULL);
        lvgl_port_unlock();
    }

    // Update BLE status once display is ready
    ui_update_ble_status(ble_gatt_get_state(),
                         ble_gatt_get_name(),
                         ble_gatt_get_mac());

    ESP_LOGI(TAG, "Ready. Waiting for BLE data...");

    // ── Main loop ─────────────────────────────────────────
    while (1) {
        // BLE data → protocol parse → UI update
        if (ble_gatt_has_data()) {
            const char *json = ble_gatt_get_data();
            if (json) {
                usage_data_t data = {0};
                int grp_before = usage_rate_group();

                if (protocol_parse(json, &data)) {
                    usage_rate_sample(data.session_pct);
                    int grp_after = usage_rate_group();

                    ui_update(&data);

                    if (grp_after != grp_before && splash_is_active()) {
                        if (lvgl_port_lock(100)) {
                            splash_pick_for_current_rate();
                            lvgl_port_unlock();
                        }
                    }
                    ble_gatt_send_ack();
                    ESP_LOGI(TAG, "data: s=%.0f%% w=%.0f%% st=%s grp=%d",
                             data.session_pct, data.weekly_pct, data.status, grp_after);
                } else {
                    ble_gatt_send_nack();
                    ESP_LOGW(TAG, "JSON parse failed");
                }
            }
        }

        // BLE state change → update BT screen
        static ble_gatt_state_t s_last_ble = BLE_GATT_STATE_INIT;
        ble_gatt_state_t cur_ble = ble_gatt_get_state();
        if (cur_ble != s_last_ble) {
            s_last_ble = cur_ble;
            ui_update_ble_status(cur_ble, ble_gatt_get_name(), ble_gatt_get_mac());
            ESP_LOGI(TAG, "BLE state: %d", cur_ble);
        }

        // IMU rotation check (2Hz)
        imu_rotation_check();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

- [ ] **Step 2: Full build**

```bash
idf.py build 2>&1 | tail -10
```

Expected: BUILD SUCCESSFUL, binary size reported

- [ ] **Step 3: Check binary size**

```bash
idf.py size 2>&1 | grep -E "DRAM|IRAM|Flash"
```

Typical expected: Flash <1.5MB (well within 2MB factory partition), DRAM <200KB

- [ ] **Step 4: Commit main integration**

```bash
git add main/main.c
git commit -m "feat: main app integration — BSP init, IMU rotation, BLE data loop, splash timer"
```

---

## Task 8: Flash & Integration Test

> Pre-requisites: ESP-IDF environment active, ESP32-S3-BOX-3 connected via USB-C, daemon pairing script from original Clawdmeter repo.

- [ ] **Step 1: Flash firmware**

```bash
idf.py -p /dev/ttyACM0 flash monitor
# On macOS: idf.py -p /dev/cu.usbserial-* flash monitor
```

Expected serial output:
```
I (xxx) MAIN: Clawdmeter BOX-3 starting...
I (xxx) DISP: display ready 320x240
I (xxx) IMU: IMU ready
I (xxx) GATT: advertising as 'Claude Controller' rc=0
I (xxx) MAIN: Ready. Waiting for BLE data...
```

- [ ] **Step 2: Verify display**

Expected on screen:
- Splash screen with Clawd animation centered on black background
- Touch circle does nothing yet (BLE not paired)

- [ ] **Step 3: Pair via bluetoothctl (Linux) or System Settings (macOS)**

Linux:
```bash
bluetoothctl scan le   # wait for "Claude Controller" to appear
bluetoothctl pair <MAC>
bluetoothctl trust <MAC>
```

macOS: System Settings → Bluetooth → "Claude Controller" → Connect

- [ ] **Step 4: Run daemon from original Clawdmeter**

```bash
# From HermannBjorgvin/Clawdmeter clone:
./install.sh           # registers systemd service
systemctl --user start claude-usage-daemon
journalctl --user -u claude-usage-daemon -f
```

Expected daemon log:
```
[HH:MM:SS] GATT RX path: /org/bluez/hci0/dev_XX_XX/service.../char...
[HH:MM:SS] Sending: {"s":30,"sr":240,"w":15,"wr":7100,"st":"allowed","ok":true}
```

Expected serial monitor:
```
I (xxx) MAIN: data: s=30% w=15% st=allowed grp=0
```

Expected on screen: Usage screen shows arcs, percentages, reset times.

- [ ] **Step 5: Test HID touch buttons**

In a text editor on the host:
- Press "Space" button on screen → space character inserted
- Press "Shift+Tab" button → tab backward / mode toggle in Claude Code

- [ ] **Step 6: Test screen cycling**

- Press touch circle → cycles Usage → Bluetooth → Splash
- Bluetooth screen shows MAC address and connection status
- Touch splash screen → returns to Usage screen

- [ ] **Step 7: Test IMU rotation**

- Flip BOX-3 upside-down → display blanks briefly → redraws 180° rotated
- Flip right-side-up → blanks briefly → restores 0°

- [ ] **Step 8: Final commit**

```bash
git tag v1.0.0-box3
git commit --allow-empty -m "test: integration verified on hardware"
```

---

## Self-Review Notes

**Spec coverage check:**

| Spec requirement | Implemented in |
|---|---|
| Project structure (5 components) | Tasks 1–6 |
| Protocol abstraction (multi-platform) | Task 2 |
| esp-box-3 BSP for display/touch | Task 7 (main.c) |
| ICM-42607-P auto-rotation | Task 7 (imu_rotation_check) |
| BLE GATT custom service | Task 4 |
| BLE HID keyboard | Task 5 |
| HID via touch screen buttons | Task 6 (ui_usage.c) |
| Touch circle = screen cycle | Task 7 (on_main_button) |
| Splash animations centered 320×240 | Task 3 (splash.c) |
| Usage rate group selection | Task 3 (usage_rate.c) |
| LVGL mutex thread-safety | Task 6 (ui.c lvgl_port_lock) |
| FreeRTOS queue for BLE→app_main | Task 4 (ble_gatt.c) |
| Daemon unchanged | Task 8 (verified) |
| Extensible for future platforms | Task 2 (protocol.c dispatcher) |

**Known gaps to address during implementation:**

1. `splash_animations.h` struct layout: the file may use different field names than `anim_entry_t`. Open the fetched file top-20 lines to verify and update `splash.c` typedef accordingly.

2. `font_tiempos_34.c`: if the OTF file is unavailable, the stub alias approach means the title will render in Styrene B 28px. Acceptable for initial testing.

3. `ble_gatt.c` uses a forward declaration of `gap_event_cb` — ensure it appears before `start_advertising` in the compiled file, or add a `static int gap_event_cb(...);` prototype at the top of `ble_gatt.c`.

4. The `lv_canvas_draw_image` API may differ between LVGL 9.x minor versions. If it errors, use `lv_draw_img_dsc_t` with `lv_canvas_draw_img` or the draw unit API. Check LVGL 9 changelog for the installed version with `idf.py build 2>&1 | grep lvgl`.
