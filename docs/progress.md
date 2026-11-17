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

ESP32-C3 SuperMini 可用 GPIO：0–10、20、21。以下方案避开 USB（18/19）与 UART0（20/21），
并使用 ESP32-C3 默认 FSPI 的 SCK/MOSI。注意：V1.5 卖家接线图使用 GPIO2 作为 RST；
GPIO2 是 strapping 脚，上电时不得被驱动板拉低，否则会影响启动模式。

| 驱动板引脚 | ESP32-C3 SuperMini | 说明 |
| --- | --- | --- |
| GND | GND | 必须共地 |
| 3V3 | 3V3 | **仅 3.3 V**，禁止 5 V |
| SCK | GPIO4 | FSPI SCK |
| SDA | GPIO6 | FSPI MOSI |
| RST | GPIO2 | 复位，低有效（按 V1.5 卖家接线图） |
| DC | GPIO3 | 数据/命令选择 |
| CS1 | GPIO7 | 片选，低有效 |
| BUSY | GPIO10 | 忙状态输入（高=忙，按 V1.5 卖家接线图） |
| CS2 | 不接 | 单屏不用 |

补充：

- GPIO5 留给 SPI MISO（本屏不使用，但 `SPI.begin()` 传 -1 会在 C3 上打印错误，故保留）；
- GPIO1、GPIO8、GPIO9 不使用；GPIO2 用于 RST（strapping，上电时不得拉低）；GPIO8 还接板载 LED；
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

---

## 实验 02 实际运行记录（2026-11-17）

### 1. 硬件状态（用户确认）

- 驱动板供电：3.3 V（用户测得板端约 2.88 V，ESP32 端 3.3 V；压降原因未定性，
  可能是板载二极管，也可能是接触电阻）；
- RESE 开关：已置于 **3 Ω**，未使用 0.47 Ω；
- FPC：用户确认金手指朝下，已多次重新插拔并换过面包板；
- BUSY 杜邦线：用户用万用表测过，线本身导通；
- 面板：GDEY042T81 / 背标 `042BN-T81-D2`，SSD1683，400×300。

### 2. 固件

- 驱动类：`GxEPD2_420_GDEY042T81`（GxEPD2 1.6.9）；
- 诊断固件在 `setup()` 中：
  1. 用内部上拉/下拉探测 BUSY，判断是否浮空；
  2. 显式 `SPI.begin(4, 5, 6, 7)`；
  3. `display.init(115200, true, 50, false)`；
  4. 执行一次 `clearScreen(0xFF)`，等待 3 s；
  5. 写入全屏 canvas，调用 `refresh(false)`；
  6. 等待并采样 BUSY；
  7. 最后给 BUSY 保持内部上拉，验证开漏 BUSY 的可能性。

### 3. 实测日志（2026-11-17 18:33–18:36）

未加上拉时：

```text
BUSY probe [quiescent]: pull-up=1 pull-down=0 -> floating / high-Z
BUSY probe [after init]: pull-up=1 pull-down=0 -> floating / high-Z
_Update_Full : 3
BUSY probe [after clear wait]: pull-up=1 pull-down=0 -> floating / high-Z
_Update_Full : 1
BUSY probe [after image refresh]: pull-up=1 pull-down=0 -> floating / high-Z
```

加上内部上拉后：

```text
Busy Timeout!
_Update_Full : 10001034
Busy Timeout!
_Update_Full : 10001028
BUSY 始终保持 1
```

屏幕始终全白，没有任何图像。

### 4. 结论

- BUSY 在初始化、清屏、写图、刷新后都未被面板驱动；加上拉后只会 10 秒超时。
- 这排除了“驱动板缺少 BUSY 上拉”这一可能。
- 也排除了“驱动类选错导致 BUSY 浮空”：驱动类错误不会让面板侧完全不驱动 BUSY。
- 当前证据指向硬件侧：FPC 未真正接触、驱动板 FPC 座/走线问题、FPC 引脚定义不兼容，
  或面板控制器已损坏。
- 未验证：驱动板是否有 BS1/3W-4W 跳线、FPC 座是 top-contact 还是 bottom-contact、
  面板 VDD/VDDIO 是否真正到达、面板是否损坏。

### 5. 下一步

1. 取得驱动板正反面与 FPC 座特写照片，确认座子触点方向、插入深度、丝印与跳线；
2. 确认是否存在 `BS1` / `3W-4W` / `SPI` 第二开关，若有必须置于 4-wire；
3. 若板上有 VDD/VDDIO/GND 测试点，测量面板供电是否到达；
4. 用已知良好的驱动板或面板做交叉验证；
5. 在完成上述确认前，不再反复复位；保持 RESE 3 Ω，禁止 0.47 Ω。

### 6. 规格书核对（2026-11-17）

已获取 GDEY042T81 规格书并确认：

- FPC Pin 9 = BUSY，**高 = 忙**（Note 5-4）；
- Pin 15 VDDIO 必须与 Pin 16 VCI 一起接 3.3 V；
- Pin 17 = VSS，接地；
- Pin 8 BS1：**低 = 4-wire SPI**，高 = 3-wire SPI；
- Pin 12 = CS#，Pin 13/14 = SCL/SDA，Pin 10/11 = RES#/D/C#；
- Mechanical Drawing 中 FPC 的 “Connect” 面为金手指面，另一面为 Stiffener。

下一步需核对 V1.5 驱动板 FPC 座是否按此定义走线。若 BUSY、VDDIO/VCI、VSS 任一错位，
面板不会上电或 BUSY 不会响应，与当前“全白 + BUSY 浮空”的现象一致。

### 7. 商品详情核对（2026-11-17）

卖家商品详情中的 ESP32C3 接线图为：

```
3.3V --- 3V3
GND  --- GND
GPIO2  --- RST
GPIO3  --- DC
GPIO4  --- SCK
GPIO6  --- SDA
GPIO7  --- CS
GPIO10 --- BUSY
```

当前 `include/epd_config.h` 为：

```
PIN_RST  = 10
PIN_BUSY = 0
```

**RST 与 BUSY 刚好互换。** 如果实际按卖家图接线，则固件把 GPIO10 当 RST 驱动，
而真正的 BUSY 在 GPIO10；固件读取的 GPIO0 没有连接 BUSY，必然浮空。
这与实测“BUSY 浮空、全白、10 秒超时”完全一致。

商品详情同时说明：

- 本板 FPC 为双向，排线金属面朝上/朝下均可，但必须插到底；
- CS2 为双芯片第二条片选，单芯片不接；
- 卖家只承诺“24P/26P/34P 通用”，是否支持 GDEY042T81 仍需核对引脚定义。

下一步：用万用表确认驱动板 RST、BUSY 两根线实际接到 ESP32 的哪两个 GPIO。
若为 GPIO2/GPIO10，则修改固件引脚定义，无需重新接线。

**2026-11-17 20:14 实测结果**：用户确认按卖家图接线（RST=GPIO2、BUSY=GPIO10），
固件已改为 `PIN_RST=2`、`PIN_BUSY=10` 并烧录。复位后 BUSY 仍然浮空：

```text
BUSY probe [quiescent]: pull-up=1 pull-down=0 -> floating / high-Z
BUSY probe [after init]: pull-up=1 pull-down=0 -> floating / high-Z
Busy Timeout!
_Update_Full : 10001042
Busy Timeout!
_Update_Full : 10000036
```

结论：RST/BUSY 引脚映射错误确实存在，修正它是必要的，但仍不是全部原因。
剩余怀疑集中在：

1. FPC 金手指仍未真正接触（需新特写确认是否插到底）；
2. V1.5 板的 FPC 座引脚定义与 GDEY042T81 不一致（BUSY / VDDIO / VCI / VSS 错位）；
3. 面板控制器已损坏。

**2026-11-17 20:26 复测结果：面板已响应**

用户发现 ESP32 与驱动板之间的接线左右接反，重新接线后：

```text
BUSY probe [quiescent]: pull-up=0 pull-down=0 -> driven LOW
BUSY probe [after init]: pull-up=0 pull-down=0 -> driven LOW
_Update_Full : 1700999
BUSY probe [after clear wait]: pull-up=0 pull-down=0 -> driven LOW
BUSY probe [after image write]: pull-up=0 pull-down=0 -> driven LOW
_Update_Full : 1699000
BUSY probe [after image refresh]: pull-up=0 pull-down=0 -> driven LOW
BUSY during wait: sawHIGH=0 sawLOW=1 final=0
```

结论：

- BUSY 已被面板驱动为低，不再是高阻；
- 两次全刷各约 1.7 秒，无 `Busy Timeout!`；
- 面板已响应，屏幕点亮。

根因是 ESP32 与驱动板之间的接线左右接反（SDA/SCK 等信号错位），
以及此前固件 RST/BUSY 引脚与卖家接线图不一致。两处均已修正。

2026-11-17 用户确认屏幕显示内容正常：边框 + **Hello World** + **Smart Calendar**
+ **Experiment 02 - DIAG** + **400 x 300, black / white**。**实验 02 完成。**

**2026-11-17 20:32 固件整理并复测**：移除 BUSY 探测、clearScreen 等待和诊断采样，
改为 paged full-window Hello World，刷新后 `hibernate()`。串口：

```text
_Update_Full : 1701000
_Update_Full : 1701000
Full refresh complete; panel hibernated.
Experiment 02 done. No further refresh.
```

用户确认显示正常（文本改为 **Experiment 02 - DONE**）。实验 02 固件收尾完成。

---

## 实验 03：Wi-Fi / NTP / 日历（2026-11-17）

### 1. 目标

- ESP32-C3 连接 2.4 GHz Wi-Fi；
- 通过 NTP 获取本地时间（默认 `CST-8`，UTC+8）；
- 显示时间、日期、星期和当前月日历；
- 保持低频刷新，避免残影。

### 2. 设计决策

- **Wi-Fi 凭据**放在 `include/secrets.h`，该文件由 `.gitignore` 排除；
  仓库只提交模板 `include/secrets.example.h`，不提交真实 SSID/密码。
- **时区**：`CST-8`（中国标准时间）。NTP 服务器：`ntp.aliyun.com`、
  `cn.pool.ntp.org`、`pool.ntp.org`。
- **显示布局**：
  - 顶部大号 `HH:MM`；
  - 第二行 `YYYY-MM-DD  Weekday`；
  - 中部当前月日历，周一开始，今日黑底白字高亮；
  - 底部 Wi-Fi / NTP 状态。
- **刷新策略**：
  - 启动、跨日、Wi-Fi 状态变化、每小时：全屏刷新；
  - 每分钟：只对顶部时间区域做局刷（y=16，h=64，按 8 像素对齐）。
  - 这样时间可精确到分钟，又不会每分钟整屏闪烁。
- **字体**：当前使用 Adafruit GFX 的 ASCII 字体，月份/星期为英文；
  中文字体需要额外字库，留到后续实验，避免第一版固件过大。
- **失败处理**：Wi-Fi 连接失败每 30 秒重试；Wi-Fi 已连但 NTP 失败也每 30 秒重试；
  屏幕显示对应错误提示。

### 3. 代码与文件

- `src/main.cpp`：Experiment 03 固件；
- `include/secrets.example.h`：Wi-Fi 凭据模板（提交）；
- `include/secrets.h`：本地真实凭据（不提交）；
- `README.md`：补充实验 03 的 Wi-Fi 配置、布局与刷新说明。

### 4. 编译结果

```text
RAM:   [=         ]  10.9% (used 35576 bytes from 327680 bytes)
Flash: [===       ]  28.9% (used 379238 bytes from 1310720 bytes)
[SUCCESS] Took 8.04 seconds
```

### 5. 测试状态：已完成（2026-11-17 21:24）

最终结果：**11 dBm 下家庭 Wi-Fi 连接成功，NTP 同步，月历显示和分钟局刷正常。**
完整排查过程见下。

已填入真实 Wi-Fi 凭据并烧录。诊断结果：

- ESP32-C3 Wi-Fi 正常：扫描到 30–38 个 2.4 GHz 网络；
- 目标 SSID 可见：channel 3，RSSI -49 ~ -55 dBm，WPA/WPA2；
- SSID 精确匹配，密码长度 8，无多余空格；
- `WiFi.begin` 20 秒超时，断开原因码为 **2 (`AUTH_EXPIRE`)** 和 **202 (`AUTH_FAIL`)**；
- 这是认证失败，最可能是 Wi-Fi 密码错误，或路由器 MAC 过滤/安全设置拒绝。
- 用户随后确认：密码正确，且该网络为 **2.4 GHz / 5 GHz 双频合一**。
  因此下一步重点转向路由器的双频合一、WPA 混合模式、Wi-Fi 6 only 和 MAC 过滤。
- 用户补充：另一块 **ESP32-S3** 桌面机器人项目能正常连接同一 Wi-Fi。
  这说明路由器和密码大概率正常，C3 的射频接收也正常（扫描到 40 个网络，
  目标 AP RSSI -47 dBm）。C3 与 S3 的差异可能在于：
  1. 路由器 MAC 白名单/黑名单/设备数量上限（S3 已登记，C3 未登记）；
  2. 双频合一 + WPA/WPA2 混合模式对 C3（Wi-Fi 4 + 当前 Arduino 核心）的兼容性；
  3. C3 在 Wi-Fi 发射瞬间的供电跌落（S3 板供电/天线条件不同）；
  4. S3 项目使用的 Arduino-ESP32 / ESP-IDF 核心版本不同。

**2026-11-17 21:10 交叉验证与整片擦除**：

- 只读对比 S3 项目：同样使用 `platformio/espressif32@6.12.0`，Wi-Fi 初始化流程为
  `WiFi.persistent(false)`、`WiFi.mode(WIFI_STA)`、`WiFi.setAutoReconnect(false)`、
  `WiFi.setSleep(false)`、`WiFi.begin(ssid, password)`。
- C3 补上同样配置，设置最大 TX 19.5 dBm，并执行整片 `erase` + 重刷。
- 结果：热点和 Xiaomi 路由器仍然都在认证阶段失败（reason 2/202）；
  C3 扫描接收正常（34 个网络，热点 -46 dBm）。
- 结论：**代码/平台差异基本排除**。剩余怀疑集中在 C3 板的 Wi-Fi 发射、
  天线、供电或该板个体硬件差异。
- 下一步：换 USB 线/口并测 3.3 V；拍 C3 天线端特写；断开墨水屏负载复测；
  有条件时换一块 ESP32-C3 板做交叉验证。

**2026-11-17 21:15 11 dBm 发射功率测试（未完成）**：

- 将 TX 功率从 19.5 dBm 降到 11 dBm（`WIFI_POWER_11dBm`，读回 44）后重刷；
- 本次扫描列表中没有目标热点，`target NOT found`，失败为 `NO_SSID_AVAIL (201)`；
- 因此这次没有真正测试 11 dBm 下的认证，热点可能已自动关闭；
- 同一时刻 Xiaomi AP 可见（channel 3，RSSI -49 ~ -55 dBm，WPA/WPA2）。
- 待热点重新开启，或切回 Xiaomi 凭据后，再做 11 dBm 的有效测试。

**2026-11-17 21:19 11 dBm 测试成功**：

- 热点重新开启后，扫描到目标 AP（channel 6，RSSI -43 dBm，WPA2）；
- TX power 11 dBm（`WiFi.getTxPower()` 读回 44）；
- Wi-Fi 连接成功并获得 IP；
- NTP 同步成功：`2026-11-17 21:19:58`；
- 全刷 `_Update_Full : 1700000`，随后整分钟局刷 `_Update_Part : 400000`；
- 心跳：`Wi-Fi=1 NTP=1`。

结论：**19.5 dBm 下认证失败，11 dBm 下成功**。该 C3 板在最大发射功率下存在
供电跌落或 PA 失真的可能；当前 AP 信号足够强，后续默认使用 **11 dBm**。
下一步：切回家里 Xiaomi Wi-Fi 验证，成功后移除扫描诊断代码，恢复干净固件。

**2026-11-17 21:24 家庭 Wi-Fi 验证成功（清理版固件）**：

- Xiaomi AP：channel 3，RSSI -44 dBm，WPA/WPA2；
- TX power 11 dBm；
- Wi-Fi 连接成功并获得 IP；
- NTP：`2026-11-17 21:24:09`；
- 全刷 `_Update_Full : 1700000`，随后整分钟局刷 `_Update_Part : 400000`；
- 心跳：`Wi-Fi=1 NTP=1`；
- 已移除 Wi-Fi 扫描、事件日志和详细凭据打印；
- 固件保留 `WiFi.setTxPower(WIFI_POWER_11dBm)`。

**实验 03 完成。**

---

## 实验 04：今日待办（2026-11-17）

### 1. 目标

- 在墨水屏上显示今天最多 3 条待办；
- 提供本地编辑方式，不依赖外部云服务；
- 保持现有时间/日期/月历布局和低频刷新策略。

### 2. 设计决策

- **数据源**：ESP32 本地网页（`http://<设备IP>/`）编辑，存入 NVS
  （`Preferences`，命名空间 `todos`）。重启后保留，不依赖外部服务。
- **容量**：最多 3 条，每条最多 40 字符；保存时去掉首尾空白，
  并把换行替换为空格。
- **完成状态**：每条带 checkbox；完成后屏幕显示实心方框和删除线。
- **布局**（400×300）：
  - 时间改为 18pt（`FreeSansBold18pt7b`），日期 9pt；
  - 月历压缩为 6 行 × 18px；
  - 中部下方显示 `TODAY` + 最多 3 行待办；
  - 底部显示 `Web UI: <IP>`。
- **刷新**：
  - 保存待办后立即全刷；
  - 每分钟仍只局刷时间区域（y=16，h=40，8 像素对齐）；
  - 跨日、每小时、Wi-Fi 状态变化继续全刷。
- **字体限制**：当前使用 Adafruit GFX ASCII 字体，中文显示为 `?`；
  网页本身支持 UTF-8，但墨水屏中文字体留到后续实验。
- **网页安全**：仅局域网、无鉴权；只编辑待办，不涉及 Wi-Fi 密码或其他密钥。

### 3. 代码

- `src/main.cpp`：
  - `WebServer`：`/` 编辑页，`/save` POST 保存；
  - `Preferences`：读写 NVS `todos`；
  - `drawTodos()`：绘制 `TODAY`、复选框、文本和完成删除线；
  - 时间字体改为 `FreeSansBold18pt7b`，重新压缩日历布局；
  - 底部状态改为网页入口 IP。
- `README.md` / `AGENTS.md`：补充待办数据源、容量和 ASCII 限制。

### 4. 编译结果

```text
RAM:   [==        ]  16.6% (used 54260 bytes from 327680 bytes)
Flash: [======    ]  59.6% (used 781546 bytes from 1310720 bytes)
```

### 5. 测试结果（2026-11-17 21:29）

- 家庭 Wi-Fi 连接、NTP 同步正常；
- 网页 `http://<设备IP>/` 返回 `200 OK`，包含 3 组 `todo/done` 输入；
- 用户通过网页添加待办后，墨水屏立即全刷并正常显示；
- 完成勾选显示实心方框和删除线；
- 时间分钟局刷仍正常（`_Update_Part : 400001`）。

**实验 04 完成。**

---

## 实验 05：ChatGPT Plus 额度（2026-11-17）

### 1. 目标

- 在月历右侧显示 ChatGPT Plus 的 5 小时/周剩余额度；
- 自动更新，不手动填写；
- 数据真实，不抓 ChatGPT 网页 session、不编造。

### 2. 数据源与决策

- ChatGPT Plus 订阅额度没有官方公开 API；
- 采用 Codex CLI 使用的未公开接口：
  `GET https://chatgpt.com/backend-api/wham/usage`
  带 `Authorization: Bearer <access_token>` 和 `ChatGPT-Account-Id`；
- 本机 `~/.codex/auth.json` 已有 Codex CLI 登录凭据；
- 接口返回 `plan_type`、`rate_limit.primary_window`（18000 s = 5 h）和
  `secondary_window`（604800 s = 7 d），含 `used_percent`、`reset_at`；
- 只取当前账号自己的额度，token 只留在电脑，不发送给 ESP32、不提交仓库；
- 接口非官方，可能随 OpenAI 更新失效；失败时显示 stale/no data，不编造。

### 3. 实现

- ESP32：
  - WebServer 新增 `POST /quota`，接收 `plan/h5/h5r/wk/wkr/upd/upd_ts`；
  - NVS namespace `quota` 保存这些字段；
  - 月历左移并缩窄到 7×35 px，右侧增加 `CHATGPT PLUS` 面板；
  - 额度更新只局刷右侧面板（x=264, y=104, w=128, h=112），
    时间仍每分钟局刷；
  - 网页增加只读的 ChatGPT 额度区。
- 电脑 helper：`tools/codex_quota_push.py`
  - 读取 `~/.codex/auth.json`；
  - 查询 `wham/usage`，把 `used_percent` 换成剩余百分比；
  - 格式化为 5h `HH:MM`、周 `%a %H:%M`；
  - POST 到 ESP32 `/quota`，打印状态但不打印 token/账号；
  - `--once` / `--interval 300`。
- 用户级 systemd：
  - `tools/smart-calendar-quota.service` 安装到 `~/.config/systemd/user/`；
  - `tools/smart-calendar-quota.env.example` 复制到
    `~/.config/smart-calendar-quota.env`；
  - 已 `enable --now`，并执行 `loginctl enable-linger hy`。
- 代理问题：systemd 服务不继承 shell 的
  `HTTP_PROXY/HTTPS_PROXY/ALL_PROXY`，首次启动报
  `Network is unreachable`；在 env 文件加入代理变量，并设置
  `NO_PROXY=<ESP32-IP>,localhost,127.0.0.1`，重启后推送成功。

### 4. 编译结果

```text
RAM:   [==        ]  16.6% (used 54340 bytes from 327680 bytes)
Flash: [======    ]  60.0% (used 786830 bytes from 1310720 bytes)
```

### 5. 测试结果（2026-11-17 21:57）

- 接口测试：HTTP 200，`plan_type=plus`，
  primary 5h `used_percent=0`，secondary weekly `used_percent=0`；
- helper 推送：
  `[21:57] plan=plus 5h=100% (22:52) wk=100% (Tue 17:52) -> ESP32 200`；
- 串口：`_Update_Part : 399999`（额度面板局刷）；
- 网页：`5h: 100% (resets 22:52)`、`Week: 100% (resets Tue 17:52)`、
  `Updated: 21:57`；
- systemd 服务 active，`linger=yes`；
- 用户确认屏幕额度面板显示正常。

**实验 05 完成。**

---

## 实验 05.1：中文字体与布局调整（2026-11-17）

### 1. 用户要求

- 支持中文；
- 去掉占屏过大的表格型月历；
- 待办区域加大。

### 2. 实现

- 引入 `olikraus/U8g2_for_Adafruit_GFX@1.8.0` + `olikraus/U8g2@2.36.18`；
- 使用 `u8g2_font_wqy16_t_gb2312`（GB2312 常用字）绘制日期、待办、额度；
- 时间改为 24pt `FreeSansBold24pt7b`；
- 日期/星期改为中文：`YYYY年MM月DD日 星期X`；
- **移除 6×7 月历、月份/星期表头和表格线**；
- “今日待办”区域扩大：最多 3 条，每条最多 2 行，16px 中文；
- ChatGPT 额度改为底部单行，下一行网页 IP；
- 网页支持中文；`truncateUtf8` 按 UTF-8 字符数限制 40，避免截断半个汉字；
- 时间局刷窗口 y=16..56；额度/IP 局刷窗口 y=256..296；整屏刷新策略不变。

### 3. 编译结果

```text
RAM:   [==        ]  16.6% (used 54420 bytes from 327680 bytes)
Flash: [========  ]  83.8% (used 1098282 bytes from 1310720 bytes)
```

剩余约 212 KB。

### 4. 测试

- 固件烧录成功，Wi-Fi/NTP 正常；
- 串口出现两次 `_Update_Part : 400000`（时间/额度局刷）；
- 中文日期、`今日待办`、底部额度已使用 GB2312 字体；
- 用户要求先提交；字号/位置如需微调，后续再做。

### 5. 注意

- 表格月历是**按用户偏好移除**的，不是故障；日期/星期仍保留；
- 中文字体只覆盖 GB2312 常用字，emoji/生僻字可能无法显示；
- `include/secrets.h` 密码行曾因末尾多一个反引号导致编译失败；
  已删除该字符，未改密码内容。

---

## 实验 05.2：卡片式首页 UI（2026-11-17）

### 1. 用户要求

- 参考 400×300 卡片式参考图；
- 顶部页头、左侧时间卡 + 今日待办、右侧月历、底部 AI 用量；
- 细边框、留白、清晰层次；
- 当天日期必须能看见；
- AI 卡片改成 5 小时/周，并显示**剩余**用量。

### 2. 实现

- 新增 `include/ui_layout.h` / `src/ui_layout.cpp`：
  - `UiData`：时间、日期、待办、AI 剩余、网页 IP；
  - `UiLayout`：`drawHeader()`、`drawTimeCard()`、`drawTodoCard()`、
    `drawCalendarCard()`、`drawAIUsageCard()`、`drawMainUI()`；
- `main.cpp` 保留 Wi-Fi/NTP/WebServer/NVS/helper，绘制改为
  `ui.drawMainUI()` / `ui.drawTimeCardPartial()`；
- 布局：
  - 页头 30px，外层 1px 边框；
  - 左列 48%：时间卡（42px `u8g2_font_fub42_tn`）+ 今日待办卡；
  - 右列 52%：紧凑月历卡，无表格线；当天用 **黑框 + 白底 + 黑字** 高亮；
  - 底部 AI 剩余卡：5 小时/本周两行进度条，显示真实剩余百分比；
  - AI 卡片标题右侧显示 `ChatGPT PLUS`，不再使用示例值。
- 当天日期最初使用黑底白字，U8g2 白字在实心黑块上不可见；
  改为黑框/白底/黑字后修复。
- AI 卡片初版使用 42%/68% 示例值（带 `示例` 标签）；后按用户要求
  改为真实 5 小时/周剩余百分比，移除示例标签。
- 时间卡每分钟局刷；AI 剩余卡在 `/quota` 推送后局刷；
  启动/跨日/Wi-Fi 变化/每小时/待办保存全刷。

### 3. 编译

```text
RAM:   [==        ]  16.6% (used 54420 bytes from 327680 bytes)
Flash: [========  ]  84.4% (used 1106444 bytes from 1310720 bytes)
```

### 4. 测试

- 新 UI 烧录成功，Wi-Fi/NTP 正常；
- 时间卡 `_Update_Part : 400000`；
- helper 推送后 AI 剩余卡 `_Update_Part : 400000`；
- 当天日期黑框白底黑字显示正常；
- AI 剩余卡显示 `5 小时 100%` / `本周 100%`
  （当前 helper 读到剩余 100%）；
- 用户确认卡片布局正确；AI 卡片按用户要求改为剩余百分比。

**实验 05.2 完成。**

下一步：

1. 核对路由器 2.4 GHz Wi-Fi 密码，更新 `include/secrets.h`
   （不要把密码发到对话，也不要提交）；
2. 检查路由器是否开启 MAC 过滤，必要时将 ESP32 MAC
   `AC:A2:6A:8E:29:80` 加入白名单；
3. 连接成功后确认 NTP 时间、日期、星期与日历显示；
4. 观察每分钟局刷残影，必要时调整刷新策略；
5. 成功后移除扫描诊断代码，恢复干净固件。
