# Clawdmeter → ESP32-S3-BOX-3 移植设计

**日期：** 2026-05-14  
**目标：** 将 [Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)（Arduino + Waveshare AMOLED）移植到 ESP32-S3-BOX-3，改用纯 ESP-IDF 开发。  
**参考项目：** [smart-buddy](https://github.com/xingshizhai/smart-buddy)（ESP-IDF + esp-box-3 BSP，同一硬件已验证）

---

## 硬件对比

| 组件 | 原设备（Waveshare AMOLED 2.16"） | ESP32-S3-BOX-3 |
|---|---|---|
| 显示屏 | QSPI AMOLED CO5300 480×480 | SPI ST7789 320×240 |
| 触摸 | CST9220（I2C） | GT911（I2C，via BSP） |
| IMU | QMI8658（3轴陀螺仪+加速度计） | ICM-42607-P（同规格，via iot_sensor_hub） |
| 电源管理 | AXP2101（电池+PMU） | 无（USB-C 直供，移除） |
| 物理按键 | GPIO0 + GPIO18 + PMU-PKEY | GPIO0 + GPIO1 + BSP_BUTTON_MAIN（触摸圆圈） |
| PSRAM | 8MB OPI | 16MB OPI |
| Flash | — | 16MB |
| 构建系统 | PlatformIO + Arduino | 纯 ESP-IDF（idf.py + CMake） |

---

## 第 1 节：项目结构与构建系统

```
clawdmeter_box3/
├── CMakeLists.txt
├── idf_component.yml          # 依赖 espressif/esp-box-3
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32s3
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   └── main.c                 # app_main，FreeRTOS 任务创建
└── components/
    ├── ble/                   # BLE 传输层（GATT 数据 + HID 键盘）
    │   ├── include/ble/ble_gatt.h
    │   ├── include/ble/ble_hid.h
    │   ├── src/ble_gatt.c     # Clawdmeter 自定义 UUID，NimBLE
    │   └── src/ble_hid.c      # BLE HID 键盘服务（NimBLE 手动注册）
    ├── protocol/              # 协议抽象（多平台可扩展）
    │   ├── include/protocol/
    │   │   ├── usage_data.h   # 通用 UsageData 结构（平台无关）
    │   │   └── protocol.h     # 协议接口（parse / ack / nack）
    │   ├── src/protocol.c     # 按 platform 字段分发
    │   └── src/proto_claude.c # Claude JSON 实现（当前 Clawdmeter 格式）
    ├── ui/                    # LVGL 三屏管理
    │   ├── include/ui/ui.h
    │   ├── src/ui.c           # 屏幕管理器
    │   ├── src/ui_usage.c     # 用量屏
    │   ├── src/ui_bluetooth.c # 蓝牙状态屏
    │   └── src/ui_splash.c    # Splash 动画屏
    └── splash/                # 动画轮播 + 用量速率计算
        ├── include/splash/splash.h
        ├── include/splash/usage_rate.h
        ├── src/splash.c
        ├── src/usage_rate.c
        └── src/splash_animations.h   # 复用原 RGB565 C 数组
```

### 组件依赖链

- `idf_component.yml` → `espressif/esp-box-3`，自动拉入：
  - `esp_lcd`（ST7789 驱动）
  - `esp_lcd_touch_gt911`（GT911 触摸驱动）
  - `esp_lvgl_port`（LVGL 线程安全集成）
  - `iot_button`（物理按键和触摸圆圈）
  - `iot_sensor_hub`（ICM-42607-P IMU）
- `ble/` 组件：通过 `sdkconfig.defaults` 启用 `CONFIG_BT_NIMBLE_ENABLED=y`
- `daemon_proto/` 不需要独立组件：使用 ESP-IDF 内置 `cJSON`（替代 ArduinoJson）
- `splash/` 中的 RGB565 C 数组文件直接从原项目复用

### 扩展性设计

- **更多 Claude 功能**：在 `protocol/` 组件中扩充 `proto_claude.c` 或 `usage_data_t` 字段
- **多平台支持**：在 `protocol/src/` 中新增 `proto_openai.c`、`proto_gemini.c` 等，协议分发器通过 JSON 中的 `"platform"` 字段路由

---

## 第 2 节：硬件层

### 显示屏与触摸（via esp-box-3 BSP）

直接调用 BSP API，无需单独编写驱动：

```c
bsp_display_cfg_t cfg = {
    .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size    = BSP_LCD_H_RES * 40,   // 40 行分条渲染
    .double_buffer  = true,
    .flags          = { .buff_dma = true },
};
lv_display_t *disp = bsp_display_start_with_config(&cfg);
// GT911 触摸由 BSP 自动初始化并注册为 LVGL 输入设备
```

### IMU 自动旋转（ICM-42607-P）

通过 `iot_sensor_hub` 事件接口读取加速度计数据，判断朝向变化：

- 旋转范围：**0° / 180°**（横屏翻转，适合桌面仪表盘场景）
- 旋转实现：`bsp_display_rotate(disp, LV_DISP_ROTATION_180)`
- 旋转期间：消隐 → 等待 LVGL 完整刷新 → 渐亮恢复（~100ms）

### 按键交互

| 交互 | 实现方式 | 功能 |
|---|---|---|
| 触摸屏 Space 按钮（LVGL lv_btn） | LV_EVENT_PRESSED / RELEASED | BLE HID Space（语音 PTT） |
| 触摸屏 Shift+Tab 按钮（LVGL lv_btn） | LV_EVENT_PRESSED / RELEASED | BLE HID Shift+Tab（模式切换） |
| 触摸圆圈（BSP_BUTTON_MAIN） | iot_button 短按回调 | Splash 屏：切换动画；其他屏：循环屏幕 |
| GPIO0（Config 键） | iot_button（备用） | 可选：重置 BLE 绑定 |
| GPIO1（Mute 键） | iot_button（备用） | 不使用（可留作未来扩展） |

HID 触摸按钮通过 LVGL 事件直接调用 `ble_hid_key_press()` / `ble_hid_key_release()`，无需中断处理。

### 移除项

- `power.c / power.h`（AXP2101 PMU）→ 全部删除
- UI 电量图标 → 移除
- `imu.cpp` QMI8658 专用代码 → 替换为 `iot_sensor_hub` 接口

### 分辨率适配（480×480 → 320×240）

| 资产类型 | 处理方式 |
|---|---|
| UI 坐标 | 用 `BSP_LCD_H_RES` / `BSP_LCD_V_RES` 宏替代硬编码 |
| 字体文件 | 大部分复用（Styrene B 28/24/20px）；Tiempos Text 56→34px 需重新生成 |
| Splash 动画帧 | 重新运行 `tools/scrape_claudepix.js` + `convert_to_c.js` 拉取后居中显示 |

---

## 第 3 节：BLE 架构

### 两个 GATT 服务共存于同一 NimBLE host

#### 服务 1：Clawdmeter 自定义数据服务（保持原 UUID）

Daemon shell 脚本无需任何修改：

| 角色 | UUID | 方向 |
|---|---|---|
| Service | `4c41555a-4465-7669-6365-000000000001` | — |
| RX（接收 JSON） | `4c41555a-4465-7669-6365-000000000002` | daemon write → ESP32 |
| TX（发送 ACK） | `4c41555a-4465-7669-6365-000000000003` | ESP32 notify → daemon |
| REQ（请求刷新） | `4c41555a-4465-7669-6365-000000000004` | ESP32 notify → daemon |

实现参考 `smart-buddy/transport_ble.c` 的 NimBLE 样板，替换 UUID 和回调。

#### 服务 2：BLE HID 键盘服务（UUID 0x1812）

- 手动注册标准 HID GATT service（Report Map、HID Information、Protocol Mode、Boot Report 等特征）
- HID Report Descriptor：标准 6-key keyboard（与 NimBLE-Arduino 等价）
- `ble_hid.c` 公开接口：
  ```c
  void ble_hid_key_press(uint8_t keycode, uint8_t modifier);
  void ble_hid_key_release(void);
  ```

#### 初始化顺序

```c
// 两个服务在同一数组中注册
static const struct ble_gatt_svc_def all_svcs[] = {
    /* 服务 1: Clawdmeter 数据服务 */
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &clawdmeter_svc_uuid.u, ... },
    /* 服务 2: HID 键盘服务 */
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812), ... },
    { .type = 0 }
};
ble_gatts_count_cfg(all_svcs);
ble_gatts_add_svcs(all_svcs);
nimble_port_freertos_init(ble_host_task);
// NimBLE host 启动后在 sync_cb 中开始广播
```

广播包同时包含 Clawdmeter UUID 和 HID UUID，daemon 和 macOS 系统都能识别。

---

## 第 4 节：UI 布局（320×240 横屏）

### 用量屏（主屏）

```
┌────────────────────────────────────┐  ▲
│  CLAWDMETER              ◉ BLE状态 │  │ 28px 顶栏
├─────────────┬──────────────────────┤  │
│  Session    │  Weekly              │  │
│  (弧形条)   │  (弧形条)            │  │ 172px 内容区
│   45%       │   28%                │  │
│  Reset 2h   │  Reset 120h          │  │
├─────────────┴──────────────────────┤  │
│  [ ◉  Space  ]  [ ⇥  Shift+Tab ]  │  │ 40px 触摸按钮栏
└────────────────────────────────────┘  ▼
     160px          160px           320px
```

- 弧形进度条直径约 100px，左右各半屏居中
- 底部两个 `lv_btn`：按住触发 HID，松开释放
- 顶栏 BLE 状态用图标指示（已连接 / 断开）

### Splash 屏

- claudepix 像素动画居中显示
- 根据 `usage_rate_group()`（0–3）选择动画，组值越高动画越繁忙
- 每 20s 在当前速率组内自动轮换
- 触摸任意位置 → 跳转至用量屏
- 触摸圆圈 → 切换到下一个动画

### 蓝牙屏

```
┌────────────────────────────────────┐
│  Bluetooth                         │
│                                    │
│  状态: ◉ Connected                 │
│  名称: Claude Controller           │
│  MAC:  AA:BB:CC:DD:EE:FF           │
│                                    │
│             [ 重置绑定 ]            │
└────────────────────────────────────┘
```

### 屏幕导航

- **触摸圆圈**：Usage → Bluetooth → Splash，循环
- **触摸屏幕任意位置（非按钮区）**：Splash ↔ 上一个屏幕（toggle）

### 字体适配

| 原字体 | 用途 | 新尺寸 | 处理方式 |
|---|---|---|---|
| Tiempos Text 56px | 标题 | 34px | 重新运行 lv_font_conv |
| Styrene B 48px | 百分比数字 | 28-30px | 复用现有 28px 文件 |
| Styrene B 28px | 面板标签 | 20px | 复用现有 20px 文件 |
| Styrene B 24/20px | 小文本 | 16px | 复用现有文件 |
| Mono 32px | 状态文本 | 18px | 复用现有 18px 文件 |

---

## 第 5 节：数据流与 FreeRTOS 任务结构

### 任务分配

| 任务 | 创建方 | 优先级 | 职责 |
|---|---|---|---|
| LVGL 渲染任务 | `esp_lvgl_port` 自动创建 | 中 | 驱动 lv_timer_handler |
| NimBLE Host 任务 | `nimble_port_freertos_init` | 中高 | BLE 协议栈 |
| 传感器事件任务 | `iot_sensor_hub` | 低 | IMU 数据采样 |
| App 主逻辑 | `app_main` 主线程 | 低 | 数据解析、UI 更新调度 |

### 线程安全说明

`gatts_access_cb` 在 NimBLE host 任务上下文中执行，`app_main` 在独立任务中轮询。两者之间通过以下机制保证线程安全：

- `ble_gatt.c` 内部用 **FreeRTOS 单元素队列**（`xQueueSend`）传递 JSON 字符串指针
- `ble_gatt_has_data()` = `xQueuePeek()`，非阻塞
- `ble_gatt_get_data()` = `xQueueReceive()`，取出后队列清空
- 队列满时（上次数据未处理）丢弃新数据并记录日志（正常情况不会发生，60s 轮询间隔远大于处理时间）

### BLE 数据接收 → UI 更新

```
[daemon shell script]
    │ BLE GATT write（Clawdmeter JSON）
    ▼
[ble_gatt.c: gatts_access_cb（NimBLE 任务）]
    │ xQueueSend(data_queue, &json_buf, 0)
    ▼
[app_main 主循环（20ms tick，主任务）]
    │ ble_gatt_has_data() == true
    │ protocol_parse(json, &usage_data)   ← 路由到 proto_claude.c
    │ usage_rate_sample(usage_data.session_pct)
    │ bsp_display_lock(100)               ← 获取 LVGL 互斥锁
    │ ui_update(&usage_data)
    │ if group 变化 && splash_active: splash_pick_for_group()
    │ bsp_display_unlock()
    └ ble_gatt_send_ack()
```

### 按键事件流

```
[LVGL LV_EVENT_PRESSED  on Space 按钮]  → ble_hid_key_press(0x2C, 0x00)
[LVGL LV_EVENT_RELEASED on Space 按钮]  → ble_hid_key_release()

[LVGL LV_EVENT_PRESSED  on Shift+Tab]   → ble_hid_key_press(0x2B, 0x02)
[LVGL LV_EVENT_RELEASED on Shift+Tab]   → ble_hid_key_release()

[BSP_BUTTON_MAIN 短按回调]
    → SCREEN_SPLASH: splash_next()
    → 其他屏: ui_cycle_screen()
```

### IMU 自动旋转流

```
[sensor_hub ACCEL 事件（~20Hz）]
    → 计算 0°/180° 朝向
    → 若变化: bsp_display_brightness_set(0)
              bsp_display_rotate(disp, new_rot)
              渐亮恢复（25ms × 4步）
```

### app_main 骨架

```c
void app_main(void) {
    nvs_flash_init();
    bsp_i2c_init();
    // BSP: display（含 GT911 触摸）、IMU、按键
    bsp_display_start_with_config(&disp_cfg);
    bsp_iot_button_create(btn_arr, NULL, BSP_BUTTON_NUM);

    // BLE
    ble_gatt_init("Claude Controller");  // 启动 NimBLE + 两个 GATT 服务
    protocol_init();

    // UI
    ui_init();
    ui_show_screen(SCREEN_SPLASH);

    // 主循环
    while (1) {
        if (ble_gatt_has_data()) {
            usage_data_t data = {0};
            if (protocol_parse(ble_gatt_get_data(), &data)) {
                // 更新 usage_rate，锁 LVGL，更新 UI
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

---

## 通用数据结构

```c
// protocol/include/protocol/usage_data.h
typedef struct {
    char  platform[16];       // "claude"、"openai" 等（无字段时默认 "claude"）
    float session_pct;        // 5h 窗口用量百分比（0–100）
    int   session_reset_mins; // 会话重置剩余分钟
    float weekly_pct;         // 7d 窗口用量百分比（0–100）
    int   weekly_reset_mins;  // 周重置剩余分钟
    char  status[16];         // "allowed" / "limited"
    bool  ok;
    bool  valid;
} usage_data_t;
```

---

## 原项目可复用的内容

| 文件 | 状态 |
|---|---|
| `splash_animations.h`（RGB565 C 数组） | 直接复用 |
| `usage_rate.c/h`（速率计算逻辑） | 直接复用（移除 Arduino 头文件依赖） |
| `font_styrene_*.c`、`font_mono_*.c` | 大部分复用 |
| `icons.h`（Lucide 图标） | 直接复用 |
| `daemon/claude-usage-daemon.sh` | 无需改动 |
| BLE GATT UUID 和 JSON 协议格式 | 保持不变 |

---

## 不移植的内容

| 内容 | 原因 |
|---|---|
| `power.c/h`（AXP2101） | BOX-3 无电池 |
| `display_cfg.h`（CO5300 QSPI 引脚） | 替换为 BSP 宏 |
| PlatformIO 配置 | 改用 ESP-IDF CMake |
| Arduino 库依赖（GFX、SensorLib、XPowersLib、NimBLE-Arduino） | 替换为 ESP-IDF 等价 API |
| `screenshot.sh`（串口截图工具） | 可选保留，ESP-IDF 版 UART 接口类似 |
