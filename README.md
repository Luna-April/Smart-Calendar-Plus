# Smart Calendar 智能桌面台历

一款基于 **ESP32-C3** 与 **4.2 英寸黑白电子墨水屏** 的低成本桌面台历。
设备通过 Wi-Fi 校时与获取信息，USB 持续供电，界面追求极简、低频刷新。

> 状态：**实验 05 已完成（2026-11-17）**——Wi-Fi/NTP/日期/待办/ChatGPT Plus 额度均已验证；
> 当前为卡片式首页：左侧时间/今日待办，右侧紧凑月历，底部 AI 剩余额度。
> 额度由本机 Codex CLI helper 自动推送；ESP32-C3 的 Wi-Fi TX 固定为 **11 dBm**。
> 实验 04/03/02/01 已完成。

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
- 实验 02 已验证接线与点亮；后续操作仍必须保持 3.3 V、RESE 3 Ω，禁止带电插拔 FPC。

## 软件环境

- Ubuntu Linux，Python 3.10.12
- PlatformIO Core 6.2.0（安装在本项目独立虚拟环境 `.venv/`，未改动系统 Python）
- PlatformIO 平台：`espressif32@6.12.0`
- 框架：Arduino（Arduino-ESP32 core，ESP-IDF SDK v4.4.7）
- 板卡定义：`lolin_c3_mini`
- 墨水屏库：`zinggjm/GxEPD2@1.6.9`（驱动类 `GxEPD2_420_GDEY042T81`）
- 中文字体：`olikraus/U8g2_for_Adafruit_GFX@1.8.0` + `olikraus/U8g2@2.36.18`
  （字体 `u8g2_font_wqy16_t_gb2312`，GB2312 常用字）

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
├── src/main.cpp            # 主流程：Wi-Fi/NTP/网页/NVS/UI 调度
├── src/ui_layout.cpp       # 卡片式首页绘制（页头/时间/待办/月历/AI）
├── include/ui_layout.h     # UiData 与 UiLayout 接口
├── include/epd_config.h    # 墨水屏引脚、尺寸与驱动类
├── include/secrets.example.h # Wi-Fi 凭据模板（secrets.h 不提交）
├── include/                # 其他项目头文件（预留）
├── lib/                    # 项目私有库（预留）
├── docs/progress.md        # 实验记录与进度
├── tools/                  # 辅助脚本 / udev 规则
│   ├── codex_quota_push.py # Codex 额度推送 helper
│   ├── smart-calendar-quota.service # 用户级 systemd 服务模板
│   └── smart-calendar-quota.env.example # 服务环境模板
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

固件行为（实验 05，已烧录）：

1. 连接 2.4 GHz Wi-Fi（凭据在 `include/secrets.h`，TX 固定 11 dBm）；
2. NTP 校时，默认时区 `CST-8`；
3. 顶部页头：线框日历图标 + `Smart Calendar` + 网页 IP；
4. 左列：42px 大号时间卡 + 中文日期/星期；下方“今日待办”卡，最多 3 条；
5. 右列：紧凑月历卡（无表格线，当天黑框白底黑字高亮）；
6. 底部：`AI 剩余` 卡，显示 ChatGPT Plus 5 小时/周剩余百分比；
7. 待办通过 `http://<设备IP>/` 编辑，保存在 NVS；支持中文（GB2312，最多 40 个字符）；
8. 额度由电脑上的 Codex helper POST 到 `/quota`，保存在 NVS；
9. 启动、跨日、Wi-Fi 状态变化、每小时、待办保存后全刷；
   时间卡每分钟局刷，AI 剩余卡在 helper 推送后局刷。

实验记录见 [`docs/progress.md`](docs/progress.md) 的“实验 05：ChatGPT Plus 额度”、
“实验 05.1：中文字体与布局调整”和“实验 05.2：卡片式首页 UI”。

## 实验 02 接线方案（已验证）

> ✅ 该接线已于 2026-11-17 验证点亮。RESE 必须置于 **3 Ω**，驱动板只能接 **3.3 V**。
> 完整的安全核对清单与实测记录见 [`docs/progress.md`](docs/progress.md) 实验 02。

| 驱动板引脚 | ESP32-C3 SuperMini | 说明 |
| --- | --- | --- |
| GND | GND | 必须共地 |
| 3V3 | 3V3 | 仅 3.3 V，禁止 5 V |
| SCK | GPIO4 | FSPI SCK |
| SDA | GPIO6 | FSPI MOSI |
| RST | GPIO2 | 复位，低有效（按 V1.5 卖家接线图） |
| DC | GPIO3 | 数据/命令选择 |
| CS1 | GPIO7 | 片选 |
| BUSY | GPIO10 | 忙状态输入（按 V1.5 卖家接线图） |
| CS2 | 不接 | 单屏不用 |

## 实验 03：Wi-Fi / NTP / 日历

### Wi-Fi 配置（密码不提交）

```bash
cp include/secrets.example.h include/secrets.h
```

编辑 `include/secrets.h`，填入 **2.4 GHz** Wi-Fi 的 SSID 和密码。该文件已被 `.gitignore`
排除，不要提交，也不要把密码发到聊天里。

默认时区为 `CST-8`（中国标准时间，UTC+8）；如需修改，编辑 `src/main.cpp` 的 `TIMEZONE`。

### 显示与刷新

- 顶部：24pt `HH:MM`；
- 第二行：中文日期/星期（`YYYY年MM月DD日 星期X`）；
- 下方：“今日待办”大区域（表格月历已按用户偏好移除）；
- 底部：ChatGPT Plus 额度 + 网页入口 IP；
- 启动、跨日、Wi-Fi 状态变化、每小时做一次全刷；时间/额度更新只局刷对应区域；
- 中文字体：U8g2 `u8g2_font_wqy16_t_gb2312`（GB2312 常用字）。

### 测试

填写 `include/secrets.h` 后：

```bash
.venv/bin/pio run -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AC:A2:6A:8E:29:80-if00
.venv/bin/pio device monitor --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AC:A2:6A:8E:29:80-if00 --baud 115200
```

ESP32-C3 只支持 **2.4 GHz Wi-Fi**；5 GHz-only 网络无法连接。

### 实验结果（2026-11-17）

- 家庭 Wi-Fi 2.4 GHz 连接成功，NTP 同步；
- 实验 03 当时显示时间、日期/星期、当前月日历；该表格月历已在实验 05.1 中按用户偏好移除。
- 每分钟时间区域局刷约 0.4 s；启动、跨日、Wi-Fi 状态变化和每小时做一次全刷；
- **重要硬件约束**：本机 ESP32-C3 在 19.5 dBm 最大 TX 下 WPA2 认证失败
  （disconnect reason 2/202），固定使用 **11 dBm**（`WIFI_POWER_11dBm`）。

## 实验 04：今日待办

待办在设备本地编辑，不依赖外部服务：

1. 设备连上 Wi-Fi 后，底部显示 `Web UI: <IP>`；
2. 手机/电脑打开 `http://<IP>/`；
3. 填写最多 3 条待办（每条最多 40 字符），可勾选完成，保存；
4. 保存后屏幕立即全刷；完成项显示实心方框和删除线。

数据保存在 ESP32 NVS（`Preferences`，命名空间 `todos`），重启不丢。
现已加入 U8g2 GB2312 中文字体，每条最多 40 个字符、最多显示 2 行；
表格月历已按用户偏好移除，待办区扩大。

### 实验结果（2026-11-17）

- Wi-Fi/NTP、网页编辑、3 条待办显示、完成勾选均验证通过；
- 实验 04 当时编译：RAM 16.6%，Flash 59.6%；
- 加入中文字体与布局调整后，当前固件 Flash 83.8%（见实验 05.1）。

## 实验 05：ChatGPT Plus 额度

屏幕底部显示 ChatGPT Plus 的 5 小时/周剩余额度。数据由电脑上的本地 helper
从 Codex CLI 的 usage 接口读取并推送到 ESP32。

### 数据源与限制

- 数据源：Codex CLI 使用的 `chatgpt.com/backend-api/wham/usage` 接口（非官方公开 API）；
- 只读取当前账号自己的 `used_percent` 和 `reset_at`，换算成剩余百分比；
- token 只留在电脑的 `~/.codex/auth.json`，不写入仓库、不发送给 ESP32；
- 接口可能随 OpenAI 更新失效；失效时屏幕显示 `stale` 或 `no data`，不编造数字；
- token 过期后运行一次 `codex login`（或使用 Codex CLI）刷新。

### 本地 helper

```bash
# 手动推送一次
.venv/bin/python tools/codex_quota_push.py --esp32 http://<设备IP> --once

# 持续推送（默认每 300 秒）
.venv/bin/python tools/codex_quota_push.py --esp32 http://<设备IP> --interval 300
```

### 用户级 systemd 服务

```bash
cp tools/smart-calendar-quota.service ~/.config/systemd/user/
cp tools/smart-calendar-quota.env.example ~/.config/smart-calendar-quota.env
# 编辑 ~/.config/smart-calendar-quota.env：ESP32 IP、代理/NO_PROXY
systemctl --user daemon-reload
systemctl --user enable --now smart-calendar-quota.service
loginctl enable-linger "$USER"
```

如果 ESP32 的 DHCP 地址变化，更新 `SMART_CALENDAR_URL` 和 `NO_PROXY` 后重启服务。
如果电脑通过本地代理上网，`NO_PROXY` 必须包含 ESP32 的 IP，否则 `/quota` 也会走代理。

### 屏幕布局

```text
页头：线框日历图标 + Smart Calendar + 网页 IP
左列：42px 时间卡（中文日期/星期） / 今日待办卡
右列：紧凑月历卡（无表格线，当天黑框白底黑字）
底部：AI 剩余（5 小时 / 本周进度条，ChatGPT PLUS）
```

时间卡每分钟局刷；AI 剩余卡在 helper 推送后局刷；每小时/跨日/待办保存仍全刷。

### 中文字体与卡片式首页（2026-11-17）

- 引入 `U8g2_for_Adafruit_GFX@1.8.0` + `U8g2@2.36.18`，
  字体 `u8g2_font_wqy16_t_gb2312`；时间使用 `u8g2_font_fub42_tn`；
- 新增 `include/ui_layout.h` / `src/ui_layout.cpp`，把页头、时间卡、待办卡、
  月历卡、AI 剩余卡从 `main.cpp` 拆出；
- 页头：线框日历图标 + `Smart Calendar` + 网页 IP；
- 左列 48%：42px 时间卡 + 中文日期/星期；下方今日待办卡，最多 3 条；
- 右列 52%：紧凑月历卡，无表格线；当天用黑框 + 白底 + 黑字高亮；
- 底部：AI 剩余卡，5 小时/周两行进度条，数据为真实剩余百分比；
- 网页支持中文，按 UTF-8 字符数限制 40，避免截断半个汉字；
- 编译：RAM 16.6%（54,420 B），Flash 84.4%（1,106,444 / 1,310,720 B）。

### 实验结果（2026-11-17）

- 接口返回 `plan_type=plus`，5h/周 `used_percent = 0`（剩余 100%）；
- helper 推送成功：`5h=100% (22:52)`、`wk=100% (Tue 17:52)`，ESP32 返回 200；
- AI 剩余卡显示 5 小时/本周剩余百分比，不再使用示例值；
- 串口确认时间卡/AI 剩余卡局刷：`_Update_Part : 400000`；
- 用户级服务已启用并运行，`linger=yes`；
- 当前编译：RAM 16.6%，Flash 84.4%。

## 实验 02 结果摘要

- 日期：2026-11-17
- 结果：4.2" GDEY042T81（SSD1683）首次点亮成功，显示 Hello World 诊断图
- 驱动：GxEPD2 1.6.9，`GxEPD2_420_GDEY042T81`
- 接线：SCK=GPIO4、SDA=GPIO6、CS1=GPIO7、DC=GPIO3、RST=GPIO2、BUSY=GPIO10、3V3、GND
- RESE：3 Ω（禁止 0.47 Ω）
- 关键日志：BUSY 被面板驱动为低；两次全刷约 1.7 s，无 `Busy Timeout!`
- 根因：ESP32 与驱动板接线左右接反；早期固件 RST/BUSY 与卖家接线图不一致。两处均已修正。
- 详细记录见 [`docs/progress.md`](docs/progress.md) 实验 02。

## 实验 01 结果摘要

- 编译：成功，RAM 13,580 B（4.1%），Flash 232,362 B（17.7%）
- 烧录：成功，bootloader / 分区表 / 应用均写入并通过哈希校验
- 串口：心跳稳定 1 Hz，连续 30 秒内 USB 无断开
- 芯片：ESP32-C3 (QFN32) rev v0.4，4 MB 嵌入式 Flash，MAC `ac:a2:6a:8e:29:80`

详细过程见 [`docs/progress.md`](docs/progress.md)。

## 后续路线

| 实验 | 内容 |
| --- | --- |
| 02 | ✅ 已完成（2026-11-17）：核实兼容、接线并点亮墨水屏，显示 Hello World |
| 03 | ✅ 已完成（2026-11-17）：Wi-Fi 联网、NTP 校时、日期与日历显示 |
| 04 | ✅ 已完成（2026-11-17）：极简信息布局，显示今日待办（最多 3 项） |
| 05 | ✅ 已完成（2026-11-17）：接入 ChatGPT Plus 5h/周额度（Codex usage 接口） |
| 06 | 刷新策略、异常恢复、长期运行稳定性 |
| 07 | 桌面支架或外壳 |

> 2026-11-17 追加：加入中文字体（GB2312），改为卡片式首页；
> AI 剩余卡显示 5 小时/周剩余额度。
