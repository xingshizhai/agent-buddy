# Agent Buddy

A Claude API usage monitor for the **ESP32-S3-BOX-3**, displaying real-time session and weekly token consumption on the device screen via Bluetooth Low Energy.

---

## Overview

Agent Buddy consists of three components:

| Component | Path | Description |
|-----------|------|-------------|
| **Display Firmware** | `firmware/` | ESP32-S3-BOX-3 firmware — receives usage data via BLE and renders arc gauges on screen |
| **PC Daemon** | `daemon/` | Reads Claude OAuth token, polls Anthropic API, pushes JSON payload to the device over BLE GATT |
| **Voice Server** | `server/` | Optional Python WebSocket server providing ASR → LLM → TTS pipeline (DashScope + DeepSeek) |

---

## Hardware

- **ESP32-S3-BOX-3** (Espressif) — 320×240 LCD, ICM-42607-P IMU, GT911 touch, ES8311 codec
- A PC/Mac with Bluetooth (Linux: BlueZ; macOS: CoreBluetooth via `bleak`)

---

## Quick Start

### 1. Build & Flash Firmware

Requires **ESP-IDF ≥ 5.3**.

```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash
```

**Windows (PowerShell):**
```powershell
# Copy idf-env.example.ps1 → idf-env.local.ps1 and fill in IDF_PATH
. .\build_esp32.ps1 -ActivateOnly
idf.py -C firmware build
idf.py -C firmware -p COM3 flash
```

### 2. Install & Run the Daemon

**Linux (systemd):**
```bash
./install.sh                                     # install as user service
systemctl --user start agent-buddy-daemon        # start now
journalctl --user -u agent-buddy-daemon -f       # view logs
```

**macOS:**
```bash
pip3 install bleak
python3 daemon/agent-buddy-daemon-mac.py
```

**Manual run (any platform with BlueZ):**
```bash
bash daemon/agent-buddy-daemon.sh
```

The daemon auto-discovers the device by name "Agent Buddy", connects, polls the Anthropic API every 60 seconds, and writes the usage payload over BLE GATT. The display switches from the animation screen to the usage dashboard automatically on first data receipt, and returns to the animation screen after 3 minutes without data.

### 3. (Optional) Voice Server

```bash
cd server
pip install -r requirements.txt
cp .env.example .env          # fill in DASHSCOPE_API_KEY and DEEPSEEK_API_KEY
python main.py                # listens on 0.0.0.0:8765
```

---

## BLE Protocol

| Role | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX — daemon writes usage JSON | `41474e54-4255-4459-0000-000000000002` |
| TX — device notifies ACK | `41474e54-4255-4459-0000-000000000003` |
| REQ — device requests refresh | `41474e54-4255-4459-0000-000000000004` |

JSON payload format:
```json
{"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed", "ok": true}
```
`s` = session utilization %, `sr` = session reset (minutes), `w` = weekly %, `wr` = weekly reset, `st` = status.

---

## Project Structure

```
agent-buddy/
├── firmware/              # ESP-IDF project for ESP32-S3-BOX-3
│   ├── components/
│   │   ├── ble/           # NimBLE GATT peripheral (custom service + notify)
│   │   ├── protocol/      # JSON parser → usage_data_t (cJSON)
│   │   ├── splash/        # Boot animation engine + usage rate classifier
│   │   └── ui/            # LVGL screens: splash, usage gauges, BT status
│   ├── fonts/             # Pre-compiled LVGL font files
│   └── main/              # app_main: BSP init, IMU rotation, BLE → UI loop
├── daemon/
│   ├── agent-buddy-daemon.sh       # Linux BLE daemon (BlueZ / busctl)
│   ├── agent-buddy-daemon-mac.py   # macOS BLE daemon (bleak)
│   └── agent-buddy-daemon.service  # systemd user service unit
├── server/                # Voice AI WebSocket server (ASR→LLM→TTS)
├── install.sh             # Daemon installer (Linux systemd)
├── build_esp32.ps1        # Windows IDF build helper
└── CLAUDE.md              # Guidance for Claude Code
```

---

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

---
---

# Agent Buddy（中文说明）

基于 **ESP32-S3-BOX-3** 的 Claude API 用量监控器，通过蓝牙低功耗（BLE）将实时 Session 和 Weekly token 用量显示在设备屏幕上。

---

## 项目概览

Agent Buddy 由三个组件构成：

| 组件 | 路径 | 说明 |
|------|------|------|
| **显示固件** | `firmware/` | ESP32-S3-BOX-3 固件 — 通过 BLE 接收用量数据，在屏幕上渲染弧形仪表盘 |
| **PC Daemon** | `daemon/` | 读取 Claude OAuth Token，轮询 Anthropic API，通过 BLE GATT 推送 JSON 数据到设备 |
| **语音服务器** | `server/` | 可选的 Python WebSocket 服务器，提供 ASR → LLM → TTS 语音对话管道（DashScope + DeepSeek） |

---

## 硬件要求

- **ESP32-S3-BOX-3**（乐鑫）— 320×240 LCD、ICM-42607-P IMU、GT911 触摸、ES8311 音频编解码器
- 一台带蓝牙的电脑（Linux 使用 BlueZ；macOS 通过 `bleak` 使用 CoreBluetooth）

---

## 快速开始

### 1. 编译并烧录固件

需要 **ESP-IDF ≥ 5.3**。

```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash
```

**Windows（PowerShell）：**
```powershell
# 复制 idf-env.example.ps1 → idf-env.local.ps1 并填写 IDF_PATH
. .\build_esp32.ps1 -ActivateOnly
idf.py -C firmware build
idf.py -C firmware -p COM3 flash
```

### 2. 安装并运行 Daemon

**Linux（systemd）：**
```bash
./install.sh                                     # 安装为用户服务
systemctl --user start agent-buddy-daemon        # 立即启动
journalctl --user -u agent-buddy-daemon -f       # 查看日志
```

**macOS：**
```bash
pip3 install bleak
python3 daemon/agent-buddy-daemon-mac.py
```

**手动运行（Linux，任意终端）：**
```bash
bash daemon/agent-buddy-daemon.sh
```

Daemon 自动通过设备名 "Agent Buddy" 扫描并连接，每 60 秒查询一次 Anthropic API，通过 BLE GATT 写入用量数据。设备收到首条数据后自动从动画画面切换到用量仪表盘；超过 3 分钟无数据则自动切回动画画面。

### 3. （可选）语音服务器

```bash
cd server
pip install -r requirements.txt
cp .env.example .env          # 填写 DASHSCOPE_API_KEY 和 DEEPSEEK_API_KEY
python main.py                # 监听 0.0.0.0:8765
```

---

## BLE 协议

| 角色 | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX — Daemon 写入用量 JSON | `41474e54-4255-4459-0000-000000000002` |
| TX — 设备发送 ACK 通知 | `41474e54-4255-4459-0000-000000000003` |
| REQ — 设备请求刷新 | `41474e54-4255-4459-0000-000000000004` |

JSON 数据格式：
```json
{"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed", "ok": true}
```
`s` = Session 用量%，`sr` = Session 重置（分钟），`w` = Weekly 用量%，`wr` = Weekly 重置，`st` = 状态。

---

## 项目结构

```
agent-buddy/
├── firmware/              # ESP-IDF 工程（ESP32-S3-BOX-3）
│   ├── components/
│   │   ├── ble/           # NimBLE GATT 外围设备（自定义服务 + Notify）
│   │   ├── protocol/      # JSON 解析器 → usage_data_t（cJSON）
│   │   ├── splash/        # 启动动画引擎 + 用量等级分类器
│   │   └── ui/            # LVGL 屏幕：动画、用量仪表盘、蓝牙状态
│   ├── fonts/             # 预编译 LVGL 字体文件
│   └── main/              # app_main：BSP 初始化、IMU 旋转、BLE → UI 主循环
├── daemon/
│   ├── agent-buddy-daemon.sh       # Linux BLE Daemon（BlueZ / busctl）
│   ├── agent-buddy-daemon-mac.py   # macOS BLE Daemon（bleak）
│   └── agent-buddy-daemon.service  # systemd 用户服务单元文件
├── server/                # 语音 AI WebSocket 服务器（ASR→LLM→TTS）
├── install.sh             # Daemon 安装脚本（Linux systemd）
├── build_esp32.ps1        # Windows IDF 构建辅助脚本
└── CLAUDE.md              # Claude Code 项目说明
```

---

## 许可证

GNU General Public License v3.0 — 详见 [LICENSE](LICENSE)。
