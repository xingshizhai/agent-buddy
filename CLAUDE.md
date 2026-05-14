# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This repo contains two independent ESP32-S3 firmware projects plus supporting PC-side software:

| Directory | Project | Purpose |
|-----------|---------|---------|
| `firmware/` | **Agent Buddy Display** | Claude API usage meter on ESP32-S3-BOX-3; receives data via BLE |
| `main/` (root CMakeLists.txt) | **Agent Buddy Voice** | Voice AI agent (WiFi + audio + LCD) on ESP32-S3 WROOM-1 |
| `daemon/` | PC daemon | Polls Anthropic API and pushes usage data to the display over BLE |
| `server/` | Python WebSocket server | ASR→LLM→TTS pipeline for the voice project |

## Build & Flash

### Agent Buddy Display (`firmware/`)

Requires ESP-IDF ≥ 5.2 and the `espressif/esp-box-3` BSP component.

**Linux/macOS:**
```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash
```

**Windows (PowerShell):**
```powershell
# Full build:
.\build_esp32.ps1

# Activate IDF env only (for manual idf.py commands):
. .\build_esp32.ps1 -ActivateOnly
idf.py menuconfig

# Activate + build in one subprocess (for CI/automation):
powershell -NoProfile -Command ". '.\build_esp32.ps1' -ActivateOnly; idf.py build"
```
The build script searches for IDF config in: `ESP_IDF_BUILD_CONFIG` env var → `idf-env.local.ps1` → `~\.esp-idf-build.ps1`. Copy `idf-env.example.ps1` to create one.

### Agent Buddy Voice (root `CMakeLists.txt`, target dir = repo root)

```bash
idf.py build          # from repo root
idf.py -p /dev/ttyACM0 flash monitor
```

WiFi credentials and server URL are set via `idf.py menuconfig` → *Agent Buddy Configuration*. Target is ESP32-S3 WROOM-1 (16 MB flash, OPI PSRAM); key `sdkconfig.defaults` options are already committed.

### Agent Buddy Server (`server/`)

```bash
cd server
pip install -r requirements.txt
cp .env.example .env   # fill in DASHSCOPE_API_KEY and DEEPSEEK_API_KEY
python main.py
```

Server listens on `0.0.0.0:8765` by default. The ESP32 connects to `ws://<host>:8765/ws/chat`.

## PC Daemon Setup (Display firmware)

**Linux:**
```bash
./install.sh    # installs systemd user service
systemctl --user start agent-buddy-daemon
journalctl --user -u agent-buddy-daemon -f
```
Dependencies: `curl`, `awk`, `bluetoothctl`, `busctl`.

**macOS:**
```bash
pip3 install bleak
python3 daemon/claude-usage-daemon-mac.py
```

The daemon reads the Claude OAuth token from `~/.claude/.credentials.json`, polls `api.anthropic.com` headers for rate-limit utilization, and writes a compact JSON payload to the BLE GATT RX characteristic every 60 s.

## Architecture

### Agent Buddy Display (`firmware/`)

**Data flow:** PC daemon → BLE GATT write → `ble_gatt.c` queue → `main.c` loop → `protocol_parse()` → `ui_update()` + `usage_rate_sample()`

Components under `firmware/components/`:

- **`ble/`** — NimBLE peripheral: custom 128-bit service UUID `41474e54-4255-4459-0000-000000000001` with RX (write), TX (notify ACK), REQ (notify refresh-request) characteristics. `ble_gatt_init()` starts advertising as "Agent Buddy".
- **`protocol/`** — Parses compact JSON `{"s":…,"sr":…,"w":…,"wr":…,"st":…,"ok":…}` into `usage_data_t` via `cJSON`.
- **`splash/`** — Animated boot splash and `usage_rate` group classifier (maps utilization % → 0–4 group for selecting animations).
- **`ui/`** — Three LVGL screens: splash, usage (bar charts + reset timers), bluetooth status. All LVGL calls must be wrapped in `lvgl_port_lock()`/`lvgl_port_unlock()`.
- **`fonts/`** — Pre-compiled LVGL font files (Styrene, Tiempos, Mono).

IMU (ICM-42607-P) auto-rotates the display at 500 ms intervals based on accelerometer Y-axis.

### Agent Buddy Voice (`main/`)

**Data flow:** Mic → I2S → `app_audio.c` (PCM stereo 16 kHz) → WebSocket binary → server `AIPipeline` → ASR → LLM → TTS → WebSocket binary → speaker

Source modules in `main/`:

- **`app_audio.c`** — I2S audio capture and playback.
- **`app_wifi.c`** — WiFi STA with status callback.
- **`app_ai.c`** — WebSocket client; serializes voice activity detection events and streams PCM.
- **`app_lcd.c`** — RGB LCD + LVGL init.
- **`main.c`** — Owns UI screens (`ui_main_screen`, `ui_chat_screen`, `ui_debug_*`) and coordinates WiFi/AI init.

### Agent Buddy Server (`server/`)

`pipeline.py` `AIPipeline` class handles one WebSocket connection:
- Binary frames → `StreamingASR.feed()` (Alibaba DashScope)
- `start_speech` / `end_speech` JSON control messages gate ASR
- ASR transcript → `providers/llm.py` streaming LLM (DeepSeek or configurable)
- LLM tokens flushed at sentence boundaries → `synthesize_to_stereo_pcm()` TTS → binary frames back to ESP32

History is capped at 20 messages (10 turns).

## BLE UUIDs (Display firmware)

| Role | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX (daemon → ESP32) | `41474e54-4255-4459-0000-000000000002` |
| TX (ESP32 ACK notify) | `41474e54-4255-4459-0000-000000000003` |
| REQ (ESP32 refresh notify) | `41474e54-4255-4459-0000-000000000004` |

## Key Constraints

- All LVGL calls in both firmware projects require holding the LVGL port mutex (`lvgl_port_lock` / `lvgl_port_unlock`).
- Display firmware uses NimBLE (not classic Bluetooth); the BLE role is peripheral-only, single connection (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`).
- The BLE GATT RX buffer is 512 bytes — JSON payloads must stay under this limit.
- Voice firmware WiFi credentials are compiled in via Kconfig (`CONFIG_AGENT_BUDDY_WIFI_SSID` / `CONFIG_AGENT_BUDDY_WIFI_PASSWORD`); do not hardcode them in source.
- Both projects target ESP32-S3 (240 MHz, OPI PSRAM). The Voice firmware `sdkconfig.defaults` enables the RGB panel VSYNC restart and 64-byte cache lines required for tear-free LCD.
