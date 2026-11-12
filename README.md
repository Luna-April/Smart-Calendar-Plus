# Smart Calendar 智能桌面台历

一款基于 **ESP32-C3** 与 **4.2 英寸黑白电子墨水屏** 的低成本桌面台历。
设备通过 Wi-Fi 校时与获取信息，USB 持续供电，界面追求极简、低频刷新。

> 状态：**实验 01 已完成**（ESP32-C3 主控与环境验证）。
> 墨水屏尚未连接，实验 02 之前禁止给墨水屏上电。

本项目与 `/home/hy/ai-desktop-robot/`（AI 桌宠机器人）完全独立，后者不属于本仓库范围。

## 硬件清单

| 部件 | 型号 / 标识 | 说明 |
| --- | --- | --- |
| 主控 | ESP32-C3 SuperMini | RISC-V 单核 160 MHz，4 MB Flash，Wi-Fi/BLE，Type-C 原生 USB |
| 墨水屏 | GDEY042T81 / 实物背标 `042BN-T81-D2` | 4.2"，400×300，黑白，24 Pin 0.5 mm FPC，SPI，驱动 IC 标称 SSD1683 |
| 驱动板 | 24Pin 通用墨水屏驱动板 V1.5 | 3.3 V 供电，SPI 接口，带升压与 RESE 切换（0.47 Ω / 3 Ω） |
| 工具 | 面包板、杜邦线、万用表、电烙铁、USB 数据线 | 已有，无需重复购买 |

### 电气安全约束（重要）

- 驱动板**只能使用 3.3 V**，禁止接 5 V。
- GDEY042T81 参考电路使用 **2.2 Ω** 电流检测电阻，而驱动板提供 **0.47 Ω / 3 Ω** 两档。
  在核实屏幕实物型号、驱动板电路与 RESE 配置之前，**墨水屏不得上电**。
- 当前墨水屏与驱动板均未连接；实验 01 不涉及任何墨水屏操作。

## 软件环境

- Ubuntu Linux，Python 3.10.12
- PlatformIO Core 6.2.0（安装在本项目独立虚拟环境 `.venv/`，未改动系统 Python）
- PlatformIO 平台：`espressif32@6.12.0`
- 框架：Arduino（Arduino-ESP32 core，ESP-IDF SDK v4.4.7）
- 板卡定义：`lolin_c3_mini`

### 为什么选 `lolin_c3_mini`

PlatformIO 没有 "ESP32-C3 SuperMini" 官方板卡。`lolin_c3_mini` 与 SuperMini 最接近：

- 4 MB Flash
- 原生 USB Serial/JTAG（`ARDUINO_USB_MODE=1`、`ARDUINO_USB_CDC_ON_BOOT=1`）
- USB VID/PID 为 `303A:1001`，与本机识别到的设备一致

后续墨水屏的 SPI 引脚会在代码中显式定义，不依赖板卡默认引脚。

### 串口识别

本机同时存在两个 CDC 串口，必须按 VID/PID 区分：

| 设备节点 | VID:PID | 设备 | 用途 |
| --- | --- | --- | --- |
| `/dev/ttyACM0` | `2bdf:02b1` | General 4K USB Camera | **禁止烧录** |
| `/dev/ttyACM1` | `303a:1001` | Espressif USB JTAG/serial debug unit | ESP32-C3，烧录/监视 |

稳定路径（不随 ttyACM 编号变化）：

```
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AC:A2:6A:8E:29:80-if00
```

本机已安装 udev 规则 `/etc/udev/rules.d/99-espressif-usb-jtag.rules`，仅对 `303a:1001`
开放读写权限（规则源文件见 `tools/99-espressif-usb-jtag.rules`）。换电脑时需要重新安装。

## 目录结构

```text
Smart-Calendar/
├── platformio.ini          # PlatformIO 工程配置
├── src/main.cpp            # 实验 01 心跳测试固件
├── include/                # 项目头文件（预留）
├── lib/                    # 项目私有库（预留）
├── docs/progress.md        # 实验记录与进度
├── tools/                  # 辅助脚本 / udev 规则
├── .venv/                  # 本地 PlatformIO 环境（不纳入版本管理）
├── README.md
├── AGENTS.md               # 开发规范与硬件安全约束
└── .gitignore
```

## 构建与运行

```bash
cd /home/hy/Smart-Calendar

# 编译
.venv/bin/pio run

# 烧录（先确认 /dev/ttyACM1 为 303a:1001）
.venv/bin/pio run -t upload --upload-port /dev/ttyACM1

# 串口监视（Ctrl+C 退出）
.venv/bin/pio device monitor --port /dev/ttyACM1 --baud 115200
```

固件行为：启动时打印芯片信息，之后每秒输出一行心跳，包含运行秒数与计数器。

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
[1 s] heartbeat #1 - Smart Calendar alive
[2 s] heartbeat #2 - Smart Calendar alive
...
```

## 实验 01 结果摘要

- 编译：成功，RAM 13,580 B（4.1%），Flash 232,362 B（17.7%）
- 烧录：成功，bootloader / 分区表 / 应用均写入并通过哈希校验
- 串口：心跳稳定 1 Hz，连续 30 秒内 USB 无断开
- 芯片：ESP32-C3 (QFN32) rev v0.4，4 MB 嵌入式 Flash，MAC `ac:a2:6a:8e:29:80`

详细过程见 [`docs/progress.md`](docs/progress.md)。

## 后续路线

| 实验 | 内容 |
| --- | --- |
| 02 | 核实屏幕/驱动板电气兼容，接线并点亮墨水屏，显示 Hello World |
| 03 | Wi-Fi 联网、NTP 校时、日期与日历显示 |
| 04 | 极简信息布局，显示今日待办（最多 3 项） |
| 05 | 研究并接入 AI 工具使用额度数据 |
| 06 | 刷新策略、异常恢复、长期运行稳定性 |
| 07 | 桌面支架或外壳 |
