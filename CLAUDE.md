# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Agent Buddy** is an AI voice assistant running on ESP32-S3 hardware (custom v1.5 mainboard with SUB3 subboard). It features a 4.3" 800x480 RGB LCD with touch, stereo audio (ES8311 speaker + ES7210 mic), and WiFi connectivity.

The project has two components:
- **Firmware** (`main/`) — ESP-IDF C application with LVGL UI
- **Server** (`server/`) — Python FastAPI WebSocket server providing ASR → LLM → TTS pipeline

## Architecture

### Firmware (`main/`)

Entry point is `main.c` which initializes subsystems in order: I2C bus → LCD (LVGL) → Audio → WiFi → AI.

| Module | File | Purpose |
|--------|------|---------|
| LCD/UI | `app_lcd.c`, `ui/` | LVGL-based screens: main, chat, debug (audio/wifi/bt) |
| Audio | `app_audio.c` | I2S playback/record via ES8311 (speaker) + ES7210 (mic), shared I2C bus |
| WiFi | `app_wifi.c` | Station mode with status callbacks |
| AI | `app_ai.c` | WebSocket client to Python server, streams mic audio, receives PCM playback |
| Pins | `board_pins.h` | All GPIO definitions for v1.5 hardware |

Hardware details:
- Shared I2C bus (GPIO 47/48, 400kHz): GT1151 touch, ES8311, ES7210, TCA9554 IO expander
- Shared I2S bus (I2S_NUM_0): ES8311 DOUT, ES7210 DSIN
- RGB LCD: 16-bit data, ST7262E43 panel, 16 MHz PCLK
- Target: ESP32-S3 with 16MB flash + OPI PSRAM

### Server (`server/`)

WebSocket server at `ws://host:8765/ws/chat` handling one ESP32 client per connection.

Protocol:
- **Text frames**: JSON control messages (`start_speech`, `end_speech`, `ping`)
- **Binary frames**: raw PCM 16-bit stereo 16kHz audio from ESP32 mic
- **Text responses**: JSON (`transcript`, `response_end`, `status`, `error`)
- **Binary responses**: PCM audio chunks for speaker playback

Pipeline (`pipeline.py`): ASR (DashScope paraformer-realtime-v2) → LLM (DeepSeek via OpenAI-compatible API) → TTS (DashScope cosyvoice-v2). Supports multi-turn conversation with history (last 20 messages).

## Development Commands

### Firmware (ESP-IDF)

Build script handles ESP-IDF environment activation. Use PowerShell:

```powershell
# Activate environment only (then run idf.py manually)
. .\build_esp32.ps1 -ActivateOnly

# Build (activates env + runs idf.py build)
. .\build_esp32.ps1

# Common idf.py commands (after activation)
idf.py build          # Build firmware
idf.py flash          # Flash to device
idf.py monitor        # Serial monitor
idf.py build flash monitor  # Build, flash, and monitor in one command
```

Alternative batch file: `build_esp32.bat` (same purpose, simpler).

Flash: `do_flash.bat`

Serial monitor (Python-based with better filtering):
```
python serial_monitor.py
```

Configuration via menuconfig:
```
idf.py menuconfig
```
Key settings under "Agent Buddy Configuration": WiFi SSID/password, WebSocket server URL, LCD pixel clock.

### Server (Python)

```bash
cd server
pip install -r requirements.txt

# Set required env vars (see .env.example)
export DASHSCOPE_API_KEY=...
export DEEPSEEK_API_KEY=...

# Run server
python main.py
```

## Key Files

- `main/main.c` — Entry point, initialization order
- `main/board_pins.h` — All GPIO mappings (edit when hardware changes)
- `main/app_ai.c` — WebSocket protocol implementation
- `main/Kconfig.projbuild` — menuconfig options
- `sdkconfig.defaults` — ESP-IDF build defaults (CPU, PSRAM, flash, LCD)
- `server/pipeline.py` — AI pipeline orchestration (ASR/LLM/TTS)
- `server/config.py` — Server configuration, API keys, model settings
