# Agent Buddy

A multi-service AI usage monitor for the **ESP32-S3-BOX-3**, displaying real-time token consumption for Claude and Kimi Code on the device screen via Bluetooth Low Energy.

---

## Overview

Agent Buddy consists of three components:

| Component | Path | Description |
|-----------|------|-------------|
| **Display Firmware** | `firmware/` | ESP32-S3-BOX-3 firmware — receives usage data via BLE and renders arc gauges on screen |
| **PC Daemon** | `daemon/` | Cross-platform Python package — polls Claude and Kimi APIs and pushes data to the device over BLE GATT |
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

**Customize parameters (`idf.py menuconfig` → "Agent Buddy Configuration"):**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `BLE Device Name` | `Agent Buddy` | BLE advertising name |
| `Display Brightness` | `80 %` | Backlight level |
| `Data Timeout` | `180 s` | Return to splash after N seconds without BLE data |
| `Screen Dim Timeout` | `900 s` | Dim display after N seconds of inactivity |

**Windows (PowerShell):**
```powershell
. .\build_esp32.ps1 -ActivateOnly
idf.py -C firmware build
idf.py -C firmware -p COM3 flash
```

### 2. Install Daemon Dependencies

```bash
pip install -r daemon/requirements.txt
```

Runtime dependencies: `bleak` (BLE), `curl` (API calls, must be in PATH).

### 3. Run the Daemon

**From the `daemon/` directory:**
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
# Edit WorkingDirectory in daemon/agent-buddy-daemon.service first
cp daemon/agent-buddy-daemon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now agent-buddy-daemon
journalctl --user -u agent-buddy-daemon -f
```

Press **Ctrl+C** to stop the daemon gracefully.

### 4. Configuration

```bash
mkdir -p ~/.config/agent-buddy
cp daemon/config.example.toml ~/.config/agent-buddy/config.toml
```

Key settings in `~/.config/agent-buddy/config.toml`:

```toml
[proxy]
# HTTP/HTTPS/SOCKS5 proxy for all API calls
# url = "http://127.0.0.1:7890"

[ble]
scan_timeout   = 10   # seconds per BLE scan
reconnect_wait = 5    # seconds between reconnect attempts

[services.claude]
poll_interval    = 60    # seconds between Anthropic API polls
credentials_file = ""    # override ~/.claude/.credentials.json path

[services.kimi]
enabled       = false              # set true to activate
poll_interval = 300                # seconds between Kimi API polls
# auth_token = "eyJhbGci..."      # kimi-auth JWT from browser DevTools
                                   # or save to ~/.config/agent-buddy/kimi-auth.txt
```

### 5. Kimi Code Setup (optional)

1. Log in to [https://www.kimi.com/code/console](https://www.kimi.com/code/console) in your browser.
2. Open DevTools → Application → Cookies → copy the `kimi-auth` value.
3. Save it:

```bash
# Option A: config file
echo 'enabled = true' >> ~/.config/agent-buddy/config.toml
echo 'auth_token = "eyJhbGci..."' >> ~/.config/agent-buddy/config.toml

# Option B: token file (simpler)
echo "eyJhbGci..." > ~/.config/agent-buddy/kimi-auth.txt
```

The `kimi-auth` token expires after ~30 days; renew it from the browser.

### 6. (Optional) Voice Server

```bash
cd server
pip install -r requirements.txt
cp .env.example .env   # fill in DASHSCOPE_API_KEY and DEEPSEEK_API_KEY
python main.py         # listens on 0.0.0.0:8765
```

---

## Display & Interaction

### Usage Screen

Each service (Claude, Kimi) gets its own screen with two arc gauges:

| Element | Description |
|---------|-------------|
| **Topbar** | Service name (`CLAUDE` / `KIMI`) and BLE indicator |
| **Session arc** | 5-hour rate-limit utilization (0–100%) |
| **Weekly arc** | 7-day quota utilization (0–100%) |
| **Reset countdown** | Minutes until each window resets |
| **Status label** | `allowed` (green) or `limited` (red) |

Arc colors: **green** (< 60%) → **yellow** (60–85%) → **red** (> 85%).

### Navigation

| Input | Action | Result |
|-------|--------|--------|
| 🔴 Red circle (short press) | — | Next service screen (Claude → Kimi → …) |
| 🔴 Red circle (long press) | Hold ~1.5 s | Return to Splash animation |
| ⚙️ Side key (short press) | — | Previous service screen (← reverse) |
| ⚙️ Side key (long press) | Hold ~1.5 s | Open / save & exit Settings |
| 🔇 Mute key | — | Reserved for future voice mute |
| 👆 Swipe left on screen | — | Next service |
| 👆 Swipe right on screen | — | Previous service |
| Any button / touch | (screen dimmed) | Wake screen only — no navigation |

### Settings Page

Access by **long-pressing the side key**. Tap the left or right half of each row to adjust:

| Setting | Options |
|---------|---------|
| Brightness | 20 / 40 / 60 / 80 / 100 % |
| Screen Off | Off / 5 / 10 / 15 / 30 / 60 min |
| BLE Name | Display only (change via `idf.py menuconfig`) |

Press **Save & Exit** to persist settings to NVS (survives reboots).

### Auto-Dim

The display turns off automatically after the configured inactivity timeout. Any button press or screen touch wakes it; the first input only wakes — it does not trigger navigation.

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

**PC → Device:**
```json
{"type": "data", "v": 1, "svc": "claude", "payload": {"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed"}}
{"type": "data", "v": 1, "svc": "kimi",   "payload": {"s": 0,  "sr": 266, "w": 30, "wr": 1466, "st": "allowed"}}
```

**Device → PC (TX):**
```json
{"type": "cap", "v": 1, "svcs": ["claude", "kimi"], "screens": ["usage", "ble"]}
{"type": "ack", "v": 1, "ok": true}
{"type": "err", "v": 1, "code": 1, "msg": "parse error"}
```

**Device → PC (REQ):**
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
│   │   ├── ble/                # NimBLE GATT peripheral — cap/req/ack/err, 500 ms cap delay
│   │   ├── protocol/           # Envelope parser + service dispatch table (cJSON)
│   │   ├── settings/           # NVS persistence (brightness, dim timeout, BLE name)
│   │   ├── splash/             # Boot animation engine + usage-rate classifier
│   │   └── ui/                 # LVGL screens: splash, usage (per service), BT status, settings
│   ├── fonts/                  # Pre-compiled LVGL font files (Styrene, Tiempos, Mono)
│   ├── main/
│   │   ├── Kconfig.projbuild   # menuconfig: BLE name, brightness, timeouts
│   │   └── main.c              # app_main: BSP init, IMU rotation, button handlers, main loop
│   └── sdkconfig.defaults      # Default build configuration
├── daemon/                     # Python package (python main.py or python -m daemon)
│   ├── main.py                 # Entry point: lifecycle, reconnect loop
│   ├── ble.py                  # BLE scan / connect / notify (bleak, cross-platform)
│   ├── protocol.py             # Message serialisation / deserialisation
│   ├── config.py               # Config loader (TOML)
│   ├── config.example.toml     # Annotated example configuration
│   ├── services/
│   │   ├── base.py             # ServiceBase abstract class
│   │   ├── claude.py           # Claude OAuth + Anthropic rate-limit headers
│   │   └── kimi.py             # Kimi Code BillingService/GetUsages via kimi-auth JWT
│   ├── tests/                  # pytest unit tests (35 tests)
│   ├── requirements.txt        # Runtime deps: bleak
│   ├── requirements-dev.txt    # Dev deps: pytest, pytest-asyncio
│   └── agent-buddy-daemon.service  # systemd user service unit
├── server/                     # Voice AI WebSocket server (ASR → LLM → TTS)
├── docs/superpowers/
│   ├── specs/                  # Design specifications
│   └── plans/                  # Implementation plans
├── build_esp32.ps1             # Windows IDF build helper
└── CLAUDE.md                   # Claude Code project guidance
```

---

## Adding a New Service

**Daemon side** — create `daemon/services/cursor.py`:
```python
class CursorService(ServiceBase):
    service_id = "cursor"
    poll_interval = 300
    async def poll(self) -> dict | None:
        # return {"s": ..., "sr": ..., "w": ..., "wr": ..., "st": "allowed"}
```
Register in `daemon/main.py` `SERVICES` list.

**Firmware side:**
1. Add `{ "cursor", proto_claude_parse }` to `s_parsers[]` in `protocol.c` (reuses same payload format).
2. Add `"cursor"` to the `svcs` array in `protocol_cap()`.
3. Call `ui_register_service_screen("cursor")` in `main.c`.

---

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

---
---

# Agent Buddy（中文说明）

基于 **ESP32-S3-BOX-3** 的多服务 AI 用量监控器，通过蓝牙低功耗（BLE）将 Claude 和 Kimi Code 的实时用量显示在设备屏幕上。

---

## 项目概览

| 组件 | 路径 | 说明 |
|------|------|------|
| **显示固件** | `firmware/` | ESP32-S3-BOX-3 固件 — 通过 BLE 接收用量数据，在屏幕上渲染弧形仪表盘 |
| **PC Daemon** | `daemon/` | 跨平台 Python 包 — 轮询 Claude / Kimi API，通过 BLE GATT 推送数据 |
| **语音服务器** | `server/` | 可选的 Python WebSocket 服务器，提供 ASR → LLM → TTS 语音对话管道 |

---

## 硬件要求

- **ESP32-S3-BOX-3**（乐鑫）— 320×240 LCD、ICM-42607-P IMU、GT911 触摸、ES8311 音频编解码器
- 一台带蓝牙且安装了 Python 3.11+ 的电脑（Linux 使用 BlueZ；macOS 使用 `bleak`）

---

## 快速开始

### 1. 编译并烧录固件

需要 **ESP-IDF ≥ 5.2**。

```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash
```

**可配置参数（`idf.py menuconfig` → "Agent Buddy Configuration"）：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `BLE Device Name` | `Agent Buddy` | 蓝牙广播名称 |
| `Display Brightness` | `80 %` | 背光亮度 |
| `Data Timeout` | `180 s` | 无 BLE 数据后返回 Splash 的秒数 |
| `Screen Dim Timeout` | `900 s` | 无操作后熄屏的秒数 |

### 2. 安装 Daemon 依赖

```bash
pip install -r daemon/requirements.txt
```

运行时依赖：`bleak`（BLE）、`curl`（API 调用，需在 PATH 中）。

### 3. 运行 Daemon

```bash
cd daemon && python main.py      # 从 daemon/ 目录运行
# 或
python -m daemon                 # 从仓库根目录运行
```

按 **Ctrl+C** 可正常退出。

### 4. 配置文件

```bash
mkdir -p ~/.config/agent-buddy
cp daemon/config.example.toml ~/.config/agent-buddy/config.toml
```

主要配置项：

```toml
[proxy]
# API 访问代理（HTTP/HTTPS/SOCKS5）
# url = "http://127.0.0.1:7890"

[services.claude]
poll_interval = 60   # 轮询间隔（秒）

[services.kimi]
enabled   = false              # 设为 true 启用 Kimi 服务
# auth_token = "eyJhbGci..."  # 从浏览器 F12 → Cookies 复制 kimi-auth
```

### 5. Kimi Code 设置（可选）

1. 在浏览器中登录 [https://www.kimi.com/code/console](https://www.kimi.com/code/console)
2. F12 → Application → Cookies → 复制 `kimi-auth` 的值
3. 保存 token（约 30 天有效期，过期后需重新获取）：

```bash
echo "eyJhbGci..." > ~/.config/agent-buddy/kimi-auth.txt
```

---

## 屏幕显示与操作

### 用量仪表盘

每个服务（Claude、Kimi）独立一个屏幕，显示两个弧形仪表：

| 元素 | 说明 |
|------|------|
| **顶栏** | 服务名称（`CLAUDE` / `KIMI`）及 BLE 连接状态 |
| **Session 弧形** | 5 小时速率限制用量（0–100%） |
| **Weekly 弧形** | 7 天配额用量（0–100%） |
| **重置倒计时** | 各时间窗口的剩余重置分钟数 |
| **状态标签** | `allowed`（绿色）或 `limited`（红色） |

弧形颜色：**绿色**（< 60%）→ **黄色**（60–85%）→ **红色**（> 85%）

### 导航操作

| 输入方式 | 操作 | 功能 |
|----------|------|------|
| 🔴 红色圆圈 | 短按 | 正向切换服务（Claude→Kimi→…） |
| 🔴 红色圆圈 | 长按约 1.5s | 返回 Splash 动画首页 |
| ⚙️ 侧边键 | 短按 | 反向切换服务（←） |
| ⚙️ 侧边键 | 长按约 1.5s | 进入 / 保存退出配置页 |
| 🔇 静音键 | — | 保留给语音功能 |
| 👆 左滑屏幕 | 滑动 | 下一个服务 |
| 👆 右滑屏幕 | 滑动 | 上一个服务 |
| 任意按键/触摸 | 熄屏时 | 仅唤醒屏幕，不触发导航 |

### 配置页

**侧边键长按**进入配置页，点击各行左/右半区调整数值：

| 设置项 | 可选值 |
|--------|--------|
| Brightness（亮度） | 20 / 40 / 60 / 80 / 100 % |
| Screen Off（熄屏时间） | Off / 5 / 10 / 15 / 30 / 60 分钟 |
| BLE Name（蓝牙名） | 仅显示，修改需 `idf.py menuconfig` |

按 **Save & Exit** 保存到 NVS，重启后设置仍然保留。

### 自动熄屏

无操作超过设定时间后自动熄屏。任意按键或触摸唤醒屏幕，第一次输入仅唤醒，不触发导航。

---

## BLE 协议

### 特征值

| 角色 | UUID |
|------|------|
| Service | `41474e54-4255-4459-0000-000000000001` |
| RX — PC 写入设备 | `41474e54-4255-4459-0000-000000000002` |
| TX — 设备发送通知 | `41474e54-4255-4459-0000-000000000003` |
| REQ — 设备请求刷新 | `41474e54-4255-4459-0000-000000000004` |

### 消息格式

```json
// PC → 设备（数据推送）
{"type": "data", "v": 1, "svc": "claude", "payload": {"s": 72, "sr": 156, "w": 41, "wr": 4596, "st": "allowed"}}
{"type": "data", "v": 1, "svc": "kimi",   "payload": {"s": 0,  "sr": 266, "w": 30, "wr": 1466, "st": "allowed"}}

// 设备 → PC（TX 特征值）
{"type": "cap", "v": 1, "svcs": ["claude", "kimi"], "screens": ["usage", "ble"]}
{"type": "ack", "v": 1, "ok": true}
```

---

## 项目结构

```
agent-buddy/
├── firmware/                   # ESP-IDF 工程（ESP32-S3-BOX-3）
│   ├── components/
│   │   ├── ble/                # NimBLE GATT 外围设备
│   │   ├── protocol/           # 信封解析器 + 服务分发表（cJSON）
│   │   ├── settings/           # NVS 持久化（亮度、熄屏超时、BLE 名称）
│   │   ├── splash/             # 启动动画引擎
│   │   └── ui/                 # LVGL 屏幕：动画、用量仪表盘、蓝牙状态、配置页
│   ├── fonts/                  # 预编译 LVGL 字体文件
│   ├── main/
│   │   ├── Kconfig.projbuild   # menuconfig 参数定义
│   │   └── main.c              # 主循环：BSP 初始化、按键回调、BLE → UI
│   └── sdkconfig.defaults
├── daemon/                     # Python 包
│   ├── services/
│   │   ├── claude.py           # Claude OAuth + Anthropic API 速率限制头
│   │   └── kimi.py             # Kimi Code BillingService/GetUsages
│   ├── config.example.toml     # 示例配置文件（含 Kimi 配置说明）
│   └── tests/                  # pytest 单元测试（35 个）
├── docs/superpowers/           # 设计规格和实现计划文档
├── server/                     # 语音 AI WebSocket 服务器
└── CLAUDE.md                   # Claude Code 项目说明
```

---

## 扩展新服务

**Daemon 端**：创建 `daemon/services/cursor.py` 实现 `ServiceBase.poll()`，在 `main.py` 的 `SERVICES` 列表中注册。

**固件端**：
1. 在 `protocol.c` 的 `s_parsers[]` 中添加 `{ "cursor", proto_claude_parse }`（复用相同 payload 格式）
2. 在 `protocol_cap()` 的 `svcs` 数组中加入 `"cursor"`
3. 在 `main.c` 中调用 `ui_register_service_screen("cursor")`

---

## 许可证

GNU General Public License v3.0 — 详见 [LICENSE](LICENSE)。
