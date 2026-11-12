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

- **实验 02 已启动，资料核对与固件准备见下节**。需要先核实：
  1. 屏幕实物 `042BN-T81-D2` 与 GDEY042T81（SSD1683）的兼容性；
  2. 驱动板 RESE 挡位（0.47 Ω / 3 Ω）与参考电路 2.2 Ω 的差异及正确选择；
  3. 驱动板 3.3 V 供电与升压电路连接方式。
- 在电气兼容性确认前，不进行墨水屏接线与上电。
- 已推送至 GitHub：<https://github.com/Sprouting123/Smart-Calendar>（分支 `main`）。

---

## 实验 02 — 4.2 英寸墨水屏首次点亮（准备阶段）

- **日期**：2026-11-12
- **状态**：🟡 资料核对与固件编译完成；**墨水屏未接线、未上电、固件未烧录**
- **范围**：规格核对、RESE/驱动板兼容性分析、供电评估、GPIO 接线表、GxEPD2 驱动选型、Hello World 固件
- **明确不做**：不要求用户接线或通电，不烧录新固件

### 1. 资料核对

#### 1.1 面板型号

`042BN-T81-D2 V2` 与 **Good Display GDEY042T81** 对应：

- 4.2 英寸，400×300，黑白，24 Pin / 0.5 mm FPC，SPI
- 驱动 IC：**SSD1683**
- 刷新时间：全刷约 2 s，快刷约 1.5 s，局刷约 0.3 s
- 工作温度：0–50 °C
- 可视区 84.8 × 63.6 mm，像素间距 0.212 mm

依据：

- Good Display GDEY042T81 官方规格书（含 24 Pin 引脚表与参考电路）
- 社区项目 [lake-lanier-display](https://github.com/nkljucaric/lake-lanier-display/blob/main/claude.md)
  明确记录："Panel arrived labeled `042BN-T81-D2, V2` … identified as Good Display
  **GDEY042T81** (SSD1683 driver, no inking)."
- Arduino Forum 中 ZinggJM 对同类 Waveshare 4.2" Rev 2.2 面板的确认

> 说明：`042BN-T81-D2` 不是 Good Display 官网的独立型号，而是面板/模组厂内的背标。
> 目前证据一致指向 GDEY042T81，但最终仍建议用实物照片核对 FPC 丝印与排线方向。

#### 1.2 FPC 24 Pin 定义（来自 GDEY042T81 规格书 §5）

| Pin | 名称 | 类型 | 功能 |
| --- | --- | --- | --- |
| 1 | NC | — | 悬空 |
| 2 | GDR | O | N-MOSFET 栅极驱动 |
| 3 | RESE | I | 升压控制环电流检测输入 |
| 4 | NC | — | 悬空 |
| 5 | VSH2 | C | 正源极驱动电压（红） |
| 6 | TSCL | O | 温度传感器 I²C 时钟 |
| 7 | TSDA | I/O | 温度传感器 I²C 数据 |
| 8 | BS1 | I | 总线模式选择（L=4 线 SPI，H=3 线 SPI） |
| 9 | BUSY | O | 忙状态输出（高=忙） |
| 10 | RES# | I | 复位，低有效 |
| 11 | D/C# | I | 数据/命令选择 |
| 12 | CS# | I | 片选，低有效 |
| 13 | SCL | I | SPI 时钟 |
| 14 | SDA | I/O | SPI 数据 |
| 15 | VDDIO | P | 接口逻辑电源，应与 VCI 同接 3.3 V |
| 16 | VCI | P | 芯片电源，2.2–3.7 V（典型 3.3 V） |
| 17 | VSS | P | 地 |
| 18 | VDD | C | 内核电源，外接电容 |
| 19 | VPP | P | 测试用，不接 |
| 20 | VSH1 | C | 正源极驱动电压 |
| 21 | VGH | C | 正栅极驱动电压 |
| 22 | VSL | C | 负源极驱动电压 |
| 23 | VGL | C | 负栅极驱动电压 |
| 24 | VCOM | C | VCOM 驱动电压 |

逻辑侧实际接到驱动板排针的只有第 8–16 脚中的 BS1、BUSY、RES#、D/C#、CS#、SCL、SDA、
VDDIO、VCI，以及 VSS；其余升压/驱动电压脚由驱动板上的升压电路产生，不直接接 ESP32。

#### 1.3 RESE 参考电路（关键）

GDEY042T81 规格书 §12（第 31 页）的参考电路：

- **R2 = 2.2 Ω**（RESE 到 GND 的电流检测电阻）
- R1 = 1 MΩ（与 R2 并联，影响很小）
- L1 = 47 µH / 500 mA
- D1、D2、D3 = MBR0530
- Q1 = Si1308EDL（N-MOSFET）
- C3 = 4.7 µF / 25 V，C5 = 1 µF / 25 V，C4 = 4.7 µF / 25 V

驱动板 V1.5 提供 **0.47 Ω / 3 Ω** 两挡：

| 挡位 | 判断 |
| --- | --- |
| **3 Ω** | 最接近参考值 2.2 Ω，电流略低，**应选此挡** |
| 0.47 Ω | 约为参考值的 1/4.7，升压电流显著增大，**不适用于 SSD1683 黑白屏** |

社区与厂商资料交叉验证：

- Good Display DESPI 系列转接板同样提供 3 Ω / 0.47 Ω 两挡，用于适配不同驱动 IC；
- Arduino Forum 讨论中明确：GDEY042T81 按规格书应用 **2.2 Ω**，3 Ω 是同类板上最接近的可选值；
  0.47 Ω 是 UltraChip（UC 系列）面板的常用值，不应混用；
- JLC 测试板资料中也有 "3R / 0.47R 根据屏的要求匹配" 的说明。

结论：**RESE 开关必须置于 3 Ω 挡**。这是目前证据下唯一合理的选择，但 3 Ω 与 2.2 Ω
仍有约 36% 差异，实际刷新效果需首次点亮时验证；若对比度/刷新异常，再考虑换 2.2 Ω。

#### 1.4 驱动板

`24Pin 通用墨水屏驱动板 V1.5` 属于通用转接板，网上没有与丝印完全对应的公开原理图。
因此以下项目必须用实物照片/万用表确认，不能仅凭型号推断：

1. FPC 插座是否为 24 Pin / 0.5 mm，Pin 1 方向与面板排线一致；
2. RESE 切换开关的实际挡位与丝印（应选 3 Ω）；
3. 升压电路元件是否与参考电路同量级（47 µH 电感、MBR0530、Si1308/MOS、4.7 µF/25 V 等）；
4. 是否有 3 线/4 线 SPI 切换开关（BS1），应选 **4 线 SPI**；
5. CS2 是否为第二片选/备用脚，单屏使用时保持悬空；
6. 板上有无 5 V 输入口——**禁止使用 5 V**，只接 3.3 V。

#### 1.5 供电评估

- 面板规格：刷新功率约 **18.48 mW**（3.3 V 下约 5.6 mA），休眠约 0.0165 mW；
- ESP32-C3 SuperMini 通过 USB 5 V 经板载 LDO 产生 3.3 V，3.3 V 排针可对外供电；
- 实验 02 不启用 Wi-Fi，主控 + 屏幕功耗余量充足；
- 具体 LDO 型号/额定电流因 SuperMini 厂家批次而异，需照片确认；Espressif 建议 3.3 V
  电源具备 ≥500 mA 能力；
- 建议在驱动板 3.3 V/GND 间就近加 **100 µF 电解 + 100 nF 陶瓷** 去耦（首次点亮可先不加，
  但后续 Wi-Fi + 刷新并发时建议加）；
- **ESP32-C3 与驱动板必须共地**；不得把 5 V 接到驱动板 3V3。

### 2. GPIO 接线表

ESP32-C3 SuperMini 可用 GPIO：0–10、20、21。以下方案避开 strapping 脚（GPIO2/8/9）、
USB（18/19）与 UART0（20/21），并使用 ESP32-C3 默认 FSPI 的 SCK/MOSI：

| 驱动板引脚 | ESP32-C3 SuperMini | 说明 |
| --- | --- | --- |
| GND | GND | 必须共地 |
| 3V3 | 3V3 | **仅 3.3 V**，禁止 5 V |
| SCK | GPIO4 | FSPI SCK |
| SDA | GPIO6 | FSPI MOSI |
| RST | GPIO10 | 复位，低有效 |
| DC | GPIO3 | 数据/命令选择 |
| CS1 | GPIO7 | 片选，低有效 |
| BUSY | GPIO0 | 忙状态输入（高=忙） |
| CS2 | 不接 | 单屏不用 |

补充：

- GPIO5 留给 SPI MISO（本屏不使用，但 `SPI.begin()` 传 -1 会在 C3 上打印错误，故保留）；
- GPIO1、GPIO2、GPIO8、GPIO9 不使用；GPIO2/8/9 为 strapping，GPIO8 还接板载 LED；
- 接线前先断电，FPC 插好后锁紧，**禁止带电插拔 FPC**。

### 3. GxEPD2 驱动选型

- 库：`zinggjm/GxEPD2@1.6.9`（PlatformIO 已安装；依赖 Adafruit GFX 1.13.0、BusIO 1.17.4）
- 驱动类：**`GxEPD2_420_GDEY042T81`**
  - 400×300，SSD1683，黑白
  - `hasPartialUpdate = true`，`hasFastPartialUpdate = true`
  - 库源码注释："GDEY042T81 400x300, SSD1683 (no inking)"
- 注意：**不能使用** `GxEPD2_420`（那是 GDEW042T2 / UC8176 的旧类）

### 4. 固件与编译

新增/修改：

- `include/epd_config.h`：引脚、面板尺寸、驱动类别名（唯一接线依据）
- `src/main.cpp`：实验 02 Hello World，一次全刷后 `hibernate()`，不做周期刷新
- `platformio.ini`：新增 `lib_deps = zinggjm/GxEPD2@1.6.9`

编译结果（仅编译，未烧录）：

```text
RAM:   [=         ]   8.8% (used 28748 bytes from 327680 bytes)
Flash: [==        ]  19.6% (used 257104 bytes from 1310720 bytes)
[SUCCESS] Took 7.43 seconds
```

固件行为：

1. 串口打印引脚映射与安全提示；
2. 显式 `SPI.begin(4, 5, 6, 7)`，避免使用 lolin_c3_mini 变体的默认 SPI 引脚；
3. `display.init(115200, true, 10, false)`，10 ms 复位脉冲；
4. 全屏绘制 "Hello World" 等信息，执行一次全刷；
5. `display.hibernate()`，之后只打印心跳，不再刷新屏幕。

### 5. 首次上电前的阻塞检查项（需用户配合）

在以下项目全部确认前，**不接线、不上电**：

- [ ] 墨水屏背面标签与 FPC 排线的清晰照片（确认 24 Pin、Pin 1 方向）；
- [ ] 驱动板正反面照片（FPC 座、RESE 开关丝印、SPI 模式开关、升压元件、电源口）；
- [ ] SuperMini 电源部分照片（确认 LDO 型号/额定电流）；
- [ ] 断电状态下把 RESE 开关拨到 **3 Ω**，用万用表测 RESE 测试点/FPC Pin 3 对 GND：
      应约为 3 Ω（不是 0.47 Ω，也不是 0 Ω）；
- [ ] 万用表确认 3V3 与 GND 无短路；
- [ ] 确认驱动板只接 3.3 V，5 V 输入口不接；
- [ ] 若有 BS1/3 线-4 线开关，置于 4 线 SPI；
- [ ] 首次上电只接 USB，先测 3.3 V 轨电压与总电流；若电流异常或电压明显跌落，立即断电。

### 6. 未解决问题 / 需要补充的资料

1. **驱动板 V1.5 的确切原理图**：公开资料中没有与丝印完全对应的版本，需要实物照片或卖家资料；
2. **RESE 3 Ω 是否足够理想**：参考值为 2.2 Ω，3 Ω 预计可用但未实测；若刷新偏弱，需评估换 2.2 Ω；
3. **面板真伪/批次**：`042BN-T81-D2` 与 GDEY042T81 的对应关系来自社区证据，最终以实物显示效果为准；
4. **SuperMini LDO 具体型号**：不同批次不同，需照片确认；
5. **CS2 的处理**：单屏使用计划悬空，需驱动板资料确认无特殊要求；
6. **FPC 方向**：必须通过照片/丝印确认，插反可能损坏面板；
7. **BUSY 极性**：面板规格书为高=忙；若驱动板含反相/缓冲电路，需确认极性未被改变。

### 7. 参考资料

- [Good Display GDEY042T81 产品页](https://www.good-display.cn/product/379.html)
- [GDEY042T81 规格书 PDF（含 §5 引脚表、§12 参考电路）](https://files.seeedstudio.com/wiki/Other_Display/42-epaper/GDEY042T81.pdf)
- [SSD1683 控制器规格书](https://v4.cecdn.yun300.cn/100001_1909185148/SSD1683.PDF)
- [GxEPD2 库](https://github.com/ZinggJM/GxEPD2)
- [042BN-T81-D2 / GDEY042T81 识别记录](https://github.com/nkljucaric/lake-lanier-display/blob/main/claude.md)
- [Arduino Forum：GxEPD2 与 GDEY042T81 / RESE 讨论](https://forum.arduino.cc/t/gxepd2-new-display-type-donation-for-development/1222424/11)
- [Good Display DESPI-PICO 24 Pin 转接板（RESE 3 Ω / 0.47 Ω）](https://www.good-display.cn/product/514.html)
- [ESP32-C3 Datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html)
- [ESP32-C3 SuperMini 引脚说明](https://github.com/sidharthmohannair/Tutorial-ESP32-C3-Super-Mini)

### 8. 下一步

1. 用户提供上述照片与万用表测量结果；
2. 核对无误后，给出最终接线确认单；
3. 用户确认接线安全后，再烧录并首次点亮；
4. 首次点亮只做一次全刷，观察 Busy 时序、电流与显示效果，记录结果。
