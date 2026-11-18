# AGENTS.md — Smart Calendar 开发规范

本文件面向在本仓库工作的开发者与 AI 助手。

## 1. 项目边界

- 项目根目录：`/home/hy/Smart-Calendar/`
- **严禁修改、清理或覆盖 `/home/hy/ai-desktop-robot/`**。那是独立的 AI 桌宠机器人项目。
- 所有构建产物、依赖、配置都放在本仓库内，不污染其他项目或系统环境。

## 2. 硬件与电气安全（最高优先级）

- 墨水屏驱动板**只能使用 3.3 V**，禁止接 5 V。
- 墨水屏 RESE 挡位：GDEY042T81 规格书参考值为 **2.2 Ω**；驱动板只有 0.47 Ω / 3 Ω，
  **必须选 3 Ω**，**禁止使用 0.47 Ω**（0.47 Ω 适用于 UltraChip 类面板，会显著增大升压电流）。
- 在完成型号与电路核实前，**禁止要求用户给墨水屏接线或上电**：
  - 商品型号 GDEY042T81 与实物背标 `042BN-T81-D2` 不一致，需确认兼容性；
  - 驱动板 V1.5 无公开原理图，需实物照片与万用表测量确认 FPC 方向、升压电路与 RESE 实际挡位。
- 不得凭猜测声称硬件测试成功。没有串口日志、测量数据或照片等证据时，必须明确说明"未验证"。
- 涉及烧录、接线、上电、改系统配置等操作前，先说明风险并获得用户确认。

## 3. 串口与烧录

- 必须按 USB VID/PID 与序列号识别设备，**不得按 `/dev/ttyACMx` 编号猜测**。
- ESP32-C3：`303a:1001`，MAC `ac:a2:6a:8e:29:80`。
- 本机 `/dev/ttyACM0` 是 USB 摄像头（`2bdf:02b1`），**绝对禁止烧录**。
- 优先使用稳定路径：
  `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AC:A2:6A:8E:29:80-if00`
- 若存在多个疑似 ESP32 设备，停止操作并向用户确认。

## 4. 软件规范

- 保持代码简单、模块化，但避免过早引入复杂架构。
- 第一版功能范围：时间日期/星期、简洁月历、今日待办（最多 3 项）、AI 使用情况。
  当前为卡片式首页：左侧时间/今日待办，右侧紧凑月历卡（无表格线），
  底部 AI 剩余额度。
- 墨水屏驱动类固定为 `GxEPD2_420_GDEY042T81`（GxEPD2 1.6.9），不得误用 `GxEPD2_420`
  （后者对应 GDEW042T2 / UC8176）。
- 中文字体依赖：`olikraus/U8g2_for_Adafruit_GFX@1.8.0` + `olikraus/U8g2@2.36.18`，
  字体 `u8g2_font_wqy16_t_gb2312`（GB2312 常用字）；大号时间用
  `u8g2_font_fub42_tn`。
- 界面绘制集中在 `include/ui_layout.h` / `src/ui_layout.cpp`；`main.cpp`
  只负责数据、网络和刷新调度。
- 墨水屏 GPIO 映射以 `include/epd_config.h` 为准：SCK=4、MOSI=6、CS=7、DC=3、RST=2、BUSY=10
  （按 V1.5 驱动板卖家的 ESP32C3 接线图）。
  修改引脚必须同步更新 `docs/progress.md` 的接线表。
- 本机 ESP32-C3 SuperMini 的 Wi-Fi 发射功率固定为 `WIFI_POWER_11dBm`
  （STA 与配网 AP 都必须 11 dBm）。19.5 dBm 最大功率会导致 WPA2 认证失败
  （disconnect reason 2/202），不要改回最大功率。
- Wi-Fi 首次配网：无凭据/连接失败时启动 `SmartCalendar-XXXX` 热点
  （WPA2 `smartcalendar`、channel 6、11 dBm），配置页 `http://192.168.4.1`；
  保存到 NVS 后重启。主网页“重新配置 Wi-Fi”或长按 BOOT 5 秒可重置 Wi-Fi。
- 今日待办数据源：ESP32 本地网页（`http://<设备IP>/`）编辑，存入 NVS
  （`Preferences`，命名空间 `todos`），不依赖外部服务；最多 3 条、每条 40 个字符。
  已使用 U8g2 `u8g2_font_wqy16_t_gb2312` 支持常用中文（GB2312）；
  每条最多显示 2 行，emoji 可能无法显示。
- AI 使用额度数据源：本机 Codex CLI 的未公开
  `chatgpt.com/backend-api/wham/usage` 接口，由 `tools/codex_quota_push.py`
  读取 `~/.codex/auth.json` 并推送到 ESP32 `/quota`。token 只留在电脑上，
  禁止提交 `~/.codex/auth.json`，禁止把 token 写入固件或仓库。接口非官方、
  可能变化；失败时显示 stale/no data，禁止编造额度。当前 AI 剩余卡显示
  ChatGPT Plus 5 小时/周**剩余**百分比（不是已用百分比），不使用示例值。
- 墨水屏低频刷新，避免残影；**不要为了显示秒数而整屏刷新**。
- AI 使用数据必须来自真实、可靠的数据源，禁止编造；区分订阅额度与 API Token 用量。
- 优先复用已有工具链与库，不擅自安装不必要的系统级依赖或修改全局环境。
  本项目使用仓库内的 `.venv/`。
- 每完成一个实验，更新 `docs/progress.md`。
- 关键决策（板卡选择、引脚分配、刷新策略、数据来源）需在文档中解释原因。

## 5. 版本与密钥

- 可以初始化本地 Git 仓库，但**不得擅自创建远程仓库、提交或推送**。
- Wi-Fi 凭据由首次开机配网页写入 ESP32 NVS（`Preferences`，命名空间 `wifi`），
  编译镜像不含 Wi-Fi 密码；不再依赖 `include/secrets.h`。
- API Key、Token 等敏感信息不得写入受版本管理的文件；AI helper 的 token
  只留在电脑的 `~/.codex/auth.json`。
- 配网热点固定使用 WPA2 密码 `smartcalendar`，仅用于本地初始化；
  它不是用户的家庭 Wi-Fi 密码。
- `.pio/`、`.venv/`、编译产物与日志不纳入版本管理。

## 6. 文档要求

- `README.md`：项目介绍、硬件清单、开发环境、构建与运行方式。
- `AGENTS.md`：本文件，开发规范与安全约束。
- `docs/progress.md`：每次实验的日期、操作、结果、问题与解决方案、下一步。
