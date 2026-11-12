# 开发进度

## 实验 01 — ESP32-C3 主控验证

- **日期**：2026-11-12
- **状态**：✅ 完成
- **范围**：开发环境、设备识别、工程初始化、编译、首次烧录、串口验证
- **明确不做**：不连接墨水屏与驱动板，不涉及任何屏幕上电操作

### 目标

1. 检查 Python / PlatformIO / 串口权限；
2. 按 USB VID/PID 识别 ESP32-C3 及其实际串口；
3. 创建 PlatformIO 工程；
4. 编写每秒输出心跳的最小固件；
5. 编译并在用户确认后烧录；
6. 读取串口日志验证运行；
7. 补齐 README、AGENTS、进度文档与 .gitignore。

### 环境检查结果

| 项目 | 结果 |
| --- | --- |
| Python | 3.10.12（`/usr/bin/python3`） |
| PlatformIO | 系统未安装；`~/.platformio` 有 6.2.0 包缓存 |
| 处理方式 | 在项目内创建 `.venv`，安装 PlatformIO Core 6.2.0 |
| 串口权限 | 用户不在 `dialout` 组，初始无法读写 `/dev/ttyACM1` |
| 磁盘 | `/home` 剩余约 46 GB |

未使用 `/home/hy/ai-desktop-robot/.venv` 中的 PlatformIO，以免影响另一个项目。

### 设备识别

`lsusb` 与 `udevadm` 结果：

| 节点 | VID:PID | 设备 | 结论 |
| --- | --- | --- | --- |
| `/dev/ttyACM0` | `2bdf:02b1` | General 4K USB Camera | 摄像头，禁止烧录 |
| `/dev/ttyACM1` | `303a:1001` | Espressif USB JTAG/serial debug unit | ESP32-C3 |

- ESP32-C3 序列号：`AC:A2:6A:8E:29:80`
- 稳定路径：`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AC:A2:6A:8E:29:80-if00`
- esptool 读出的芯片信息：
  - ESP32-C3 (QFN32)，revision v0.4
  - 内置 Flash 4 MB（XMC）
  - 晶振 40 MHz
  - USB mode: USB-Serial/JTAG
  - MAC: `ac:a2:6a:8e:29:80`

### 工程初始化

创建的文件：

```text
platformio.ini
src/main.cpp
include/            （预留，含 .gitkeep）
lib/                （预留，含 .gitkeep）
docs/progress.md
tools/99-espressif-usb-jtag.rules
README.md
AGENTS.md
.gitignore
```

关键配置：

```ini
platform = espressif32@6.12.0
board = lolin_c3_mini
framework = arduino
monitor_speed = 115200
upload_speed = 460800
```

选择 `lolin_c3_mini` 的原因：PlatformIO 无 SuperMini 官方板卡，而 LOLIN C3 Mini 具备
4 MB Flash、原生 USB CDC（`ARDUINO_USB_MODE=1`、`ARDUINO_USB_CDC_ON_BOOT=1`）以及
`303A:1001` 的 USB ID，与实物一致。

### 编译结果

```text
RAM:   [          ]   4.1% (used 13580 bytes from 327680 bytes)
Flash: [==        ]  17.7% (used 232362 bytes from 1310720 bytes)
[SUCCESS] Took 562.83 seconds
```

首次编译耗时较长，主要是在下载并编译 Arduino-ESP32 框架。

### 烧录结果

在用户确认后执行：

```bash
.venv/bin/pio run -t upload --upload-port /dev/ttyACM1
```

结果：

- bootloader `0x00000000`：写入 13,248 B，哈希校验通过
- 分区表 `0x00008000`：写入 3,072 B，哈希校验通过
- 应用 `0x00010000`：写入 243,936 B，哈希校验通过
- `[SUCCESS] Took 2.18 seconds`

### 串口验证结果

单次连续读取 10 秒（节选）：

```text
[14 s] heartbeat #14 - Smart Calendar alive
[15 s] heartbeat #15 - Smart Calendar alive
...
[26 s] heartbeat #26 - Smart Calendar alive
```

启动横幅（复位后）：

```text
==========================================
 Smart Calendar - Experiment 01
 ESP32-C3 serial heartbeat test
==========================================
Chip       : ESP32-C3 (revision 4)
Cores      : 1
CPU        : 160 MHz
Flash      : 4 MB
SDK        : v4.4.7-dirty
Reset cause: 0
==========================================
```

- 心跳频率 1 Hz，时间戳与计数器连续一致
- 连续 30 秒内内核日志无 USB disconnect
- 复位原因 `0`（正常上电/硬复位）

### 遇到的问题与解决

#### 1. 初始无串口权限

- 现象：`/dev/ttyACM1` 属 `root:dialout`，权限 `660`，当前用户不在 `dialout` 组。
- 解决：新增仅针对 `303a:1001` 的 udev 规则
  `/etc/udev/rules.d/99-espressif-usb-jtag.rules`，将设备权限设为 `0666`。
  规则源文件保存在 `tools/99-espressif-usb-jtag.rules`。
- 说明：该改动仅影响 ESP32-C3 的 USB Serial/JTAG，不影响摄像头；换电脑需重新安装。

#### 2. 板子每 2.5 秒 USB 断开重连

- 现象：`journalctl` 显示 `303a:1001` 持续 disconnect/re-enumerate，设备号快速增长；
  内核约每分钟 24 次断开。
- 排查：读取串口得到 ROM bootloader 反复输出：

  ```text
  invalid header: 0xffffffff
  ```

  说明 Flash 中没有有效固件（读出 `0xFFFFFFFF`），ROM 启动失败后反复复位，
  导致 USB 反复重枚举。芯片与 USB 硬件本身正常。
- 解决：成功烧录实验 01 固件后，循环重启消失，USB 保持稳定，心跳正常。
- 结论：这是**空白 Flash 的正常表现**，不是硬件故障。

#### 3. 本地 Python 环境缺少 esptool 依赖

- 现象：直接用项目 `.venv` 运行 `tool-esptoolpy/esptool.py` 缺少 `intelhex` 等模块。
- 解决：使用 PlatformIO 的包环境执行：
  `.venv/bin/pio pkg exec --package tool-esptoolpy -- esptool.py ...`
- 日常烧录直接使用 `pio run -t upload`，无需手动调用 esptool。

### 复现命令

```bash
cd /home/hy/Smart-Calendar

.venv/bin/pio run
.venv/bin/pio run -t upload --upload-port /dev/ttyACM1
.venv/bin/pio device monitor --port /dev/ttyACM1 --baud 115200
```

### 下一步

- **实验 02 尚未开始**。需要先核实：
  1. 屏幕实物 `042BN-T81-D2` 与 GDEY042T81（SSD1683）的兼容性；
  2. 驱动板 RESE 挡位（0.47 Ω / 3 Ω）与参考电路 2.2 Ω 的差异及正确选择；
  3. 驱动板 3.3 V 供电与升压电路连接方式。
- 在电气兼容性确认前，不进行墨水屏接线与上电。
- 已推送至 GitHub：<https://github.com/Sprouting123/Smart-Calendar>（分支 `main`）。
