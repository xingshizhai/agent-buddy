# Agent Buddy

A Claude API usage monitor for the **ESP32-S3-BOX-3**, displaying real-time session and weekly token consumption on the device screen via Bluetooth Low Energy.

---

## Overview

Agent Buddy consists of three components:

| Component | Path | Description |
|-----------|------|-------------|
| **Display Firmware** | `firmware/` | ESP32-S3-BOX-3 firmware — receives usage data via BLE and renders arc gauges on screen |
| **PC Daemon** | `daemon/` | Cross-platform Python package — polls Anthropic API and pushes data to the device over BLE GATT |
| **Voice Server** | `server/` | Optional Python WebSocket server providing ASR → LLM → TTS pipeline (DashScope + DeepSeek) |

---

## Hardware

- **ESP32-S3-BOX-3** (Espressif) — 320×240 LCD, ICM-42607-P IMU, GT911 touch, ES8311 codec
- A PC with Bluetooth and Python 3.11+ (Linux with BlueZ; macOS with CoreBluetooth via `bleak`)

---

## Quick Start

### 1. Build & Flash Firmware

Requires **ESP-IDF ≥ 5.2**.

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

### 2. Install Daemon Dependencies

```bash
pip install -r daemon/requirements.txt
```

Runtime dependencies: `bleak` (BLE), `curl` (Anthropic API, must be in PATH).

### 3. Run the Daemon

**Manual run (from the `daemon/` directory):**
```bash
cd daemon
python main.py
```

**Or from the repo root:**
```bash
python -m daemon
```

**As a systemd user service (Linux):**
```bash
# Edit WorkingDirectory in daemon/agent-buddy-daemon.service to match your repo path
cp daemon/agent-buddy-daemon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now agent-buddy-daemon
journalctl --user -u agent-buddy-daemon -f
```

Press **Ctrl+C** to stop the daemon gracefully.

The daemon auto-discovers "Agent Buddy" by BLE service UUID, connects, polls the Anthropic API every 60 seconds, and pushes usage data over BLE GATT. The display switches from the splash screen to the usage dashboard on first data receipt, and returns to the splash screen when BLE disconnects or after 3 minutes without data.

### 4. (Optional) Configuration

Copy the example config and edit as needed:

```bash
mkdir -p ~/.config/agent-buddy
cp daemon/config.example.toml ~/.config/agent-buddy/config.toml
```

Key settings in `~/.config/agent-buddy/config.toml`:

```toml
[proxy]
# HTTP/HTTPS/SOCKS5 proxy for API calls — leave commented to use no proxy
# url = "http://127.0.0.1:7890"

[ble]
scan_timeout  = 10   # seconds per BLE scan
reconnect_wait = 5   # seconds between reconnect attempts

[services.claude]
poll_interval    = 60   # seconds between Anthropic API polls
credentials_file = ""   # override path to ~/.claude/.credentials.json
```

### 5. (Optional) Voice Server

```bash
cd server
pip install -r requirements.txt
cp .env.example .env          # fill in DASHSCOPE_API_KEY and DEEPSEEK_API_KEY
python main.py                # listens on 0.0.0.0:8765
```

---

## Display

The ESP32 screen shows a usage dashboard with two arc gauges:

| Element | Description |
|---------|-------------|
| **Topbar** | Service name (`CLAUDE`) and BLE connection indicator |
| **Session arc** | 5-hour token utilization (0–100%) |
| **Weekly arc** | 7-day token utilization (0–100%) |
| **Reset countdown** | Minutes until each window resets |
| **Status label** | `allowed` (green) or `limited` (red) |

Arc colors change dynamically: **green** (< 60%) → **yellow** (60–85%) → **red** (> 85%).

Press the main button to cycle between service screens and the BLE status screen.

---

## BLE Protocol

### Characteristics

| Role | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX — PC writes to device | `41474e54-4255-4459-0000-000000000002` |
| TX — device notifies ACK / cap / err | `41474e54-4255-4459-0000-000000000003` |
| REQ — device requests refresh | `41474e54-4255-4459-0000-000000000004` |

### Message Format

All messages use a JSON envelope with a `type` field:

**PC → Device (RX characteristic):**
```json
{"type": "data", "v": 1, "svc": "claude", "payload": {"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed"}}
```

**Device → PC (TX characteristic):**
```json
{"type": "cap",  "v": 1, "svcs": ["claude"], "screens": ["usage", "ble"]}
{"type": "ack",  "v": 1, "ok": true}
{"type": "err",  "v": 1, "code": 1, "msg": "parse error"}
```

**Device → PC (REQ characteristic):**
```json
{"type": "req", "v": 1, "svc": "claude"}
```

Payload fields: `s` = session %, `sr` = session reset (min), `w` = weekly %, `wr` = weekly reset (min), `st` = status.

---

## Project Structure

```
agent-buddy/
├── firmware/                   # ESP-IDF project (ESP32-S3-BOX-3)
│   ├── components/
│   │   ├── ble/                # NimBLE GATT peripheral — custom service, cap/req/ack/err
│   │   ├── protocol/           # Envelope parser → service_store_t (cJSON)
│   │   ├── splash/             # Boot animation engine + usage rate classifier
│   │   └── ui/                 # LVGL screens: splash, per-service usage gauges, BT status
│   ├── fonts/                  # Pre-compiled LVGL font files (Styrene, Tiempos, Mono)
│   └── main/                   # app_main: BSP init, IMU rotation, BLE → UI loop
├── daemon/                     # Python package (python main.py or python -m daemon)
│   ├── main.py                 # Entry point: lifecycle, reconnect loop
│   ├── ble.py                  # BLE scan / connect / notify (bleak, cross-platform)
│   ├── protocol.py             # Message serialisation / deserialisation
│   ├── config.py               # Config loader (TOML)
│   ├── config.example.toml     # Annotated example configuration
│   ├── services/
│   │   ├── base.py             # ServiceBase abstract class
│   │   └── claude.py           # Claude OAuth + Anthropic API poller
│   ├── tests/                  # pytest unit tests
│   ├── requirements.txt        # Runtime deps: bleak
│   ├── requirements-dev.txt    # Dev deps: pytest, pytest-asyncio
│   └── agent-buddy-daemon.service  # systemd user service unit
├── server/                     # Voice AI WebSocket server (ASR → LLM → TTS)
├── build_esp32.ps1             # Windows IDF build helper
└── CLAUDE.md                   # Claude Code project guidance
```

---

## Adding a New Service

To display data from a new service (e.g. Cursor):

**Daemon side:**
1. Create `daemon/services/cursor.py` implementing `ServiceBase.poll()`.
2. Add `CursorService(...)` to the `SERVICES` list in `daemon/main.py`.

**Firmware side:**
1. Create `firmware/components/protocol/src/proto_cursor.c`.
2. Add `{ "cursor", proto_cursor_parse }` to the `s_parsers[]` table in `protocol.c`.
3. Add `"cursor"` to the `svcs` array in `protocol_cap()` in `protocol.c`.
4. Call `ui_register_service_screen("cursor")` in `main.c`.

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
| **PC Daemon** | `daemon/` | 跨平台 Python 包 — 轮询 Anthropic API，通过 BLE GATT 推送数据到设备 |
| **语音服务器** | `server/` | 可选的 Python WebSocket 服务器，提供 ASR → LLM → TTS 语音对话管道（DashScope + DeepSeek） |

---

## 硬件要求

- **ESP32-S3-BOX-3**（乐鑫）— 320×240 LCD、ICM-42607-P IMU、GT911 触摸、ES8311 音频编解码器
- 一台带蓝牙且安装了 Python 3.11+ 的电脑（Linux 使用 BlueZ；macOS 通过 `bleak` 使用 CoreBluetooth）

---

## 快速开始

### 1. 编译并烧录固件

需要 **ESP-IDF ≥ 5.2**。

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

### 2. 安装 Daemon 依赖

```bash
pip install -r daemon/requirements.txt
```

运行时依赖：`bleak`（BLE）、`curl`（Anthropic API，需在 PATH 中）。

### 3. 运行 Daemon

**从 `daemon/` 目录直接运行：**
```bash
cd daemon
python main.py
```

**从仓库根目录运行：**
```bash
python -m daemon
```

**作为 systemd 用户服务运行（Linux）：**
```bash
# 先编辑 daemon/agent-buddy-daemon.service 中的 WorkingDirectory 为实际路径
cp daemon/agent-buddy-daemon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now agent-buddy-daemon
journalctl --user -u agent-buddy-daemon -f
```

按 **Ctrl+C** 可正常退出。

Daemon 通过 BLE Service UUID 自动扫描并连接 "Agent Buddy" 设备，每 60 秒查询一次 Anthropic API，通过 BLE GATT 推送用量数据。设备收到首条数据后自动从动画画面切换到用量仪表盘；BLE 断开或超过 3 分钟无数据时自动切回动画画面。

### 4. （可选）配置文件

复制示例配置并按需修改：

```bash
mkdir -p ~/.config/agent-buddy
cp daemon/config.example.toml ~/.config/agent-buddy/config.toml
```

`~/.config/agent-buddy/config.toml` 中的主要配置项：

```toml
[proxy]
# API 访问代理（支持 HTTP/HTTPS/SOCKS5），不需要代理时注释掉此行
# url = "http://127.0.0.1:7890"

[ble]
scan_timeout  = 10   # 每次 BLE 扫描的超时秒数
reconnect_wait = 5   # 重连等待秒数

[services.claude]
poll_interval    = 60   # Anthropic API 轮询间隔（秒）
credentials_file = ""   # 覆盖凭证文件路径，空则使用默认 ~/.claude/.credentials.json
```

### 5. （可选）语音服务器

```bash
cd server
pip install -r requirements.txt
cp .env.example .env          # 填写 DASHSCOPE_API_KEY 和 DEEPSEEK_API_KEY
python main.py                # 监听 0.0.0.0:8765
```

---

## 屏幕显示

ESP32 屏幕显示带两个弧形仪表的用量面板：

| 元素 | 说明 |
|------|------|
| **顶栏** | 服务名称（`CLAUDE`）及 BLE 连接状态指示点 |
| **Session 弧形** | 5 小时 token 用量（0–100%） |
| **Weekly 弧形** | 7 天 token 用量（0–100%） |
| **重置倒计时** | 各时间窗口的剩余重置分钟数 |
| **状态标签** | `allowed`（绿色）或 `limited`（红色） |

弧形颜色根据用量动态变化：**绿色**（< 60%）→ **黄色**（60–85%）→ **红色**（> 85%）。

按主键可在各服务屏幕和 BLE 状态屏幕之间循环切换。

---

## BLE 协议

### 特征值

| 角色 | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX — PC 写入设备 | `41474e54-4255-4459-0000-000000000002` |
| TX — 设备发送 ACK / cap / err 通知 | `41474e54-4255-4459-0000-000000000003` |
| REQ — 设备请求刷新 | `41474e54-4255-4459-0000-000000000004` |

### 消息格式

所有消息使用带 `type` 字段的 JSON 信封：

**PC → 设备（RX 特征值）：**
```json
{"type": "data", "v": 1, "svc": "claude", "payload": {"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed"}}
```

**设备 → PC（TX 特征值）：**
```json
{"type": "cap",  "v": 1, "svcs": ["claude"], "screens": ["usage", "ble"]}
{"type": "ack",  "v": 1, "ok": true}
{"type": "err",  "v": 1, "code": 1, "msg": "parse error"}
```

**设备 → PC（REQ 特征值）：**
```json
{"type": "req", "v": 1, "svc": "claude"}
```

Payload 字段说明：`s` = Session 用量%，`sr` = Session 重置（分钟），`w` = Weekly 用量%，`wr` = Weekly 重置（分钟），`st` = 状态。

---

## 项目结构

```
agent-buddy/
├── firmware/                   # ESP-IDF 工程（ESP32-S3-BOX-3）
│   ├── components/
│   │   ├── ble/                # NimBLE GATT 外围设备 — 自定义服务、cap/req/ack/err
│   │   ├── protocol/           # 信封解析器 → service_store_t（cJSON）
│   │   ├── splash/             # 启动动画引擎 + 用量等级分类器
│   │   └── ui/                 # LVGL 屏幕：动画、各服务用量仪表盘、蓝牙状态
│   ├── fonts/                  # 预编译 LVGL 字体文件（Styrene、Tiempos、Mono）
│   └── main/                   # app_main：BSP 初始化、IMU 旋转、BLE → UI 主循环
├── daemon/                     # Python 包（python main.py 或 python -m daemon）
│   ├── main.py                 # 入口：生命周期、重连循环
│   ├── ble.py                  # BLE 扫描/连接/通知（bleak，跨平台）
│   ├── protocol.py             # 消息序列化/反序列化
│   ├── config.py               # 配置加载器（TOML）
│   ├── config.example.toml     # 带注释的示例配置文件
│   ├── services/
│   │   ├── base.py             # ServiceBase 抽象基类
│   │   └── claude.py           # Claude OAuth + Anthropic API 轮询
│   ├── tests/                  # pytest 单元测试
│   ├── requirements.txt        # 运行时依赖：bleak
│   ├── requirements-dev.txt    # 开发依赖：pytest、pytest-asyncio
│   └── agent-buddy-daemon.service  # systemd 用户服务单元文件
├── server/                     # 语音 AI WebSocket 服务器（ASR → LLM → TTS）
├── build_esp32.ps1             # Windows IDF 构建辅助脚本
└── CLAUDE.md                   # Claude Code 项目说明
```

---

## 扩展新服务

以接入 Cursor 为例：

**Daemon 端：**
1. 新建 `daemon/services/cursor.py`，实现 `ServiceBase.poll()`。
2. 在 `daemon/main.py` 的 `SERVICES` 列表中加入 `CursorService(...)`。

**固件端：**
1. 新建 `firmware/components/protocol/src/proto_cursor.c`。
2. 在 `protocol.c` 的 `s_parsers[]` 表中添加 `{ "cursor", proto_cursor_parse }`。
3. 在 `protocol.c` 的 `protocol_cap()` 函数中将 `"cursor"` 加入 `svcs` 数组。
4. 在 `main.c` 中调用 `ui_register_service_screen("cursor")`。

---

## 许可证

GNU General Public License v3.0 — 详见 [LICENSE](LICENSE)。
