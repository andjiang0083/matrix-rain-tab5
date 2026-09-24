# 骇客帝国时钟 — Matrix Rain Clock（M5Stack Tab5）

[English](README.md) · **中文**

**5 英寸 MIPI-DSI 屏上的数字雨：80 列下落的字符、被雨折射的 7 段式时钟、以及一套全触控 WiFi 配网向导。**

> *"红药丸还是蓝药丸？"*
>
> 对 80/90 后来说，《骇客帝国》不只是一部电影——那是一场觉醒。绿色的代码雨，是我们第一次看见数据的美。
> 这台时钟送给每一个曾梦见锡安的少年，送给每一个依旧相信"没有勺子"的极客。

基于 [M5Stack Tab5](https://docs.m5stack.com/zh_CN/core/Tab5)（ESP32-P4，RISC-V，1280×720 屏）。整个程序是单板应用：Arduino + M5Unified，**不用 LVGL、不套框架**，每一个界面都是直接画到面板上的。

---

## 现状

以下都是**在真机上量过的状态**，不是愿景：

| 模块 | 状态 | 说明 |
|---|---|---|
| 数字雨引擎 | ✅ 可用 | 80 列 × 16 px 网格，拖尾最长 18 个字符，每列有独立纵深（速度与拖尾长度 0.6–1.5×），帧率上限 30 |
| 6 种字符集 | ✅ 可用 | `FULL` `NUM` `HEX` `BIN` `ALPHA` `KATAKANA`——点屏幕切换 |
| 片假名字库 | ✅ 可用 | 91 个字形，16×16 1bpp，用水平游程填充绘制（不需要 CJK 字体引擎） |
| 7 段式"幽灵发光"时钟 | ✅ 可用 | 雨**穿过**点亮的段时会被折射；穿过的那一格字符留在原地，作为残影慢慢淡去（约 8.5 秒） |
| 触控交互 | ✅ 可用 | 不用任何物理按键：点屏幕 = 换字符集，右下角 `SET` = 设置菜单 |
| WiFi 配网向导 | ✅ 可用 | 扫描 → 网络列表 → 屏幕键盘输密码 → 时区网格 → 存 NVS |
| 开机 NTP 对时 + 硬件 RTC | ✅ 可用 | 开机阻塞对时，UTC 写入 RX8130CE；之后每 60 秒读一次 RTC |
| 亮度调节 | ✅ 可用 | GPIO22 LEDC PWM（12 位），设置菜单里 20–100%，存 NVS |
| 串口截图 | ✅ 可用 | 串口里按 `S`：把一帧渲染进 PSRAM 的 sprite，以 BMP 流式发出 |
| 闲置省电 | ⚠️ 部分 | 2 分钟→降到 25% 亮度，3 分钟→8fps，10 分钟→关背光。**没有 Light Sleep**（CPU 一直在跑） |
| 时区 | ⚠️ 固定偏移 | 时区网格里选的是 UTC 偏移量，**不含夏令时规则** |
| 中文/非 ASCII SSID | ⚠️ 已知 | 显示成 `□` 方框——GLCD 字体和屏幕键盘都只支持 ASCII |
| 音频频谱（ES8388） | ❌ 本版本没有 | 移植来源的 StickS3 / Cardputer 版本有，Tab5 版没做 |
| IMU（BMI270） | ❌ 未使用 | 芯片在（和 StickS3 同一颗），"摇一摇换字符集"没实现 |
| 电池电量（INA226） | ❌ 未实现 | 屏幕上没有电量显示 |
| 显示通路的健壮性 | ⚠️ 受硬件限制 | 见下节——这是本项目最诚实的软肋 |

### 唯一真正的坑：PSRAM 带宽

ESP32-P4 没有专用显存：MIPI-DSI 控制器直接从 PSRAM 里 DMA 读帧缓冲，带宽约 110 MB/s（1280×720×2 字节 × 60Hz）。而这个程序画的每一个像素，都是 CPU 往同一片 PSRAM 里写。如果再插着 USB-CDC 串口，PSRAM 总线上就有三个竞争者——DSI 扫屏、CPU 回写、USB DMA——DSI 桥就会 underrun 并锁死。

移植时我们反复撞上它（画面约 30 秒后冻住、没有 panic、必须断电恢复），试错了五种假设之后才落到"总线争用"这个结论。它带来两个决定：

- **帧超时保护**：一帧耗时超过 3 秒就 `esp_restart()` 重启回来，而不是僵在那里；
- **绘制通路保持便宜且单写者**：80 列、30fps、不做双缓冲、热路径里不用全屏 sprite。

完整排查过程在 [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md)。如果你想让它更快，请从那份文档读起——"渲染进一个大 sprite 再一次性推上去"恰好就是我们不得不退回的方案。

---

## 硬件

| | |
|---|---|
| 板子 | M5Stack Tab5（Kit） |
| 主控 | ESP32-P4NRW32——RISC-V 双核 360MHz + 40MHz LP 核，自身不带无线 |
| 无线 | ESP32-C6-MINI-1U 协处理器，走 SDIO（Wi-Fi 6 / Thread / Zigbee） |
| 内存 | 32MB PSRAM（Octal 200MHz）+ 736KB 片内 SRAM |
| Flash | 16MB |
| 屏幕 | 5" IPS，1280×720 MIPI-DSI，ST7123 / ST7121 显示触控一体 |
| 触控 | 与面板一体（I²C）——本项目唯一的输入 |
| 音频 | ES8388 + 双麦阵列（本版本未使用） |
| 运动 | BMI270 六轴（本版本未使用） |
| RTC | RX8130CE + 备份超级电容 |
| 电源监测 | INA226 |
| 电池 | NP-F550（M5Stack 标称 50% 亮度约 6 小时） |
| USB | USB-C（OTG）+ USB-A Host |

板子上还有摄像头接口、RS-485、microSD、RTC 中断线和 M5-Bus——本项目都没碰。**欢迎把这些没用上的硬件用起来**（见 [ROADMAP.md](ROADMAP.md)）。

---

## 快速开始

### 方式一：用 M5Burner 刷发布镜像（不需要工具链）

1. 从 [Releases](../../releases) 下载 `matrix-rain-tab5-vX.Y.Z.zip`（或直接下 `.bin`）。
2. 打开 **M5Burner** → **Custom** 标签 → **Import Custom FW** → 选 `.zip`（或同目录的 `.json`）。
3. 选中 **Matrix Rain Clock**，点 **Burn**。

Release 里的 `.bin` 是**合并镜像**（bootloader @ `0x2000` + 分区表 + 应用，DIO 模式），从 `0x0` 开始写入。

### 方式二：从源码构建并刷机

```bash
git clone https://github.com/andjiang0083/matrix-rain-tab5.git
cd matrix-rain-tab5
~/.platformio/penv/bin/pio run                                   # 构建
~/.platformio/penv/bin/pio run --target upload \
  --upload-port /dev/cu.usbmodemXXXX                             # 刷机
```

Windows / Linux 的路径差异，以及我们踩过的每一个坑，都写在 [BUILDING.md](BUILDING.md)（中文版 [BUILDING_CN.md](BUILDING_CN.md)）。**跟工具链搏斗之前先读它**：构建用的是固定版本的 pioarduino 平台 + 固定版 Arduino 核心，还有一个你**大概率要改**的工具链路径覆盖脚本。

### 首次开机

1. **没有保存过 WiFi** → 自动进入配网向导：扫描、列出网络、屏幕键盘输密码、选 UTC 偏移、全部存进 NVS。
2. **有保存过 WiFi** → 连接（12 秒窗口）→ NTP 对时（`pool.ntp.org`、`time.google.com`）→ UTC 写入硬件 RTC → 显示**带实时秒数的确认界面**，你点 **Confirm** 它才开始。
3. 之后 WiFi 会断开，雨开始下。时钟不再需要网络：时间来自 RTC。

> **没有网络时间就没有钟。** 首次开机确实需要一个 2.4GHz 热点。之后 RX8130CE 自己走时；如果 RTC 无效（年份 < 2025），时钟显示 `00:00`。

---

## 操作

这个版本没有任何按键，两个触控区域就是全部交互。

| 手势 | 作用 |
|---|---|
| 点任意位置（`SET` 角除外） | 切换字符集（`FULL → NUM → HEX → BIN → ALPHA → KATAKANA`） |
| 点右下 `SET`（90×34 px） | 打开设置菜单 |
| 设置 → **WiFi Settings** | 不重启，直接重跑配网向导 |
| 设置 → **Character Set** | `<` / `>` 选同那六种字符集 |
| 设置 → **Brightness** | `−` / `+`，10% 一档，限幅 20–100%，存 NVS |
| 设置 → **About** | 版本、板子、台词、仓库地址 |
| 闲置后任意触摸 | 恢复亮度/帧率，退出省电状态 |

串口：每秒打印一次 `FPS: <n>`；按 `S` 触发截图。

---

## 工作原理

```
setup()
 ├─ M5.begin()                     → 自动识别板卡、1280×720 DSI、rotation 3
 ├─ autoConnectAndSync()           → NVS 凭据 → 连接 → NTP → UTC 写入 RX8130CE
 │   └─ runWifiSetup()             → 只有在没有可用凭据时才走
 ├─ rebuildTrailColors()           → 预计算 18 档拖尾颜色（每帧不做数学）
 ├─ initRain()                     → 每列纵深、速度、拖尾长度、字符
 └─ initBrightness()               → GPIO22 LEDC PWM，应用 NVS 里的亮度

loop()  [30fps；闲置 8fps]
 ├─ esp_task_wdt_reset()           → 8 秒任务看门狗，开 panic
 ├─ 闲置状态机                     → 2 分钟调暗 / 3 分钟 8fps / 10 分钟关背光
 ├─ updateClockFromNTP()           → 每 60 秒读 RTC，叠加 NVS 里的时区偏移
 ├─ handleTouch()                  → SET 角 或 换字符集
 ├─ updateRain()                   → 推进各列，随机换字（概率 = t²+2）
 ├─ drawRainOn(M5.Display)         → 清扫过的带 → 幽灵字符 → 拖尾字符
 └─ 帧超时保护                     → >3 秒 ⇒ esp_restart()
```

**雨**：每一列独占一条 22 px 宽的竖条，并记住 `prevY`。每帧只清掉它"扫过"的那一段——这正是止住"旧拖尾永远没人清 → 渲染量越来越大 → DSI 挂掉"那个故障的关键。纵深（`0.6–1.5`）同时决定速度（`140 px/s × 纵深`）和拖尾长度（`2 + 纵深 × 11`）。

**时钟**：数字是七个矩形段，不是字体。当某个字符与**点亮的**段相交时，会被从该段边缘向内推 4 px，并提亮/偏色——雨看起来是绕着数字弯过去的。穿过的那一格字符接着被写进 `80 × 45` 的发光缓冲，每帧衰减 1 级，于是时钟里积攒起一层慢慢淡去的幽灵字符（约 8.5 秒）。数字几何（`230 × 272 px` 单元）是当初"整屏 sprite"方案被放弃后定下来的，原因见移植笔记。

**片假名**：片假名集是位图字体——`tools/fontgen.py` 把 91 个字形从系统 TTF 渲染成 16×16 1bpp，固件用水平游程填充把它们画出来，所以不用链接任何 CJK 字体引擎。用的什么字体、以及为什么要注意，见 [CREDITS.md](CREDITS.md)。

---

## 性能

真机实测（固件每秒打印 `FPS:`）：

| | |
|---|---|
| 雨的帧率 | 上限 30fps（强制），闲置 3 分钟后 8fps |
| 每帧 PSRAM 流量 | 约 46KB 的 CPU 回写（80 列 × 最多 18 字符 × 2 字节）+ 清扫带的开销 |
| DSI 扫屏 | 1280×720×2 字节 @ 60Hz ≈ 110 MB/s，持续不断 |
| 字符绘制 | 直接写 DSI；在这块面板上 `drawChar` 大约是 `fillRect` 的 2–3 倍开销 |

屏幕上**故意没有**帧率显示——这一系列时钟的作者规矩就是"不做常驻 UI 杂物"。要看帧率请接串口，或从开机横幅确认固件版本。

---

## 目录结构

```
matrix-rain-tab5/
├── platformio.ini          构建配置（pioarduino 平台、固定版核心与库）
├── src/
│   ├── main.cpp            应用：雨引擎、时钟、设置菜单、闲置状态机
│   ├── config.h            几何、亮度档位表、时序常量
│   ├── wifi_setup.cpp/.h   配网向导 + NTP + RTC 处理
│   ├── matrix_gui.h        绿底黑字配色 + SSID 清洗
│   ├── katakana_font.h     生成的位图字库（91 字形）
│   ├── sdkconfig.h         给预编译 Arduino 库用的 PSRAM 200MHz 覆盖
│   ├── override_toolchain.py   把 IDF 的 RISC-V 工具链放进 PATH（**你需要改这里**）
│   └── setup_menu.h        设置菜单入口
├── tools/fontgen.py        重新生成 katakana_font.h
├── m5burner/               M5Burner 打包元数据
└── docs/PORTING-NOTES.md   移植笔记：硬件发现、DSI/PSRAM 事后分析、走过的死路
```

---

## 参与共建

欢迎开 issue 和 PR——项目约定见 [CONTRIBUTING.md](CONTRIBUTING.md)（[中文](CONTRIBUTING_CN.md)）（这些约定来自真实故障，不是口味），可上手的具体事项见 [ROADMAP.md](ROADMAP.md)（[中文](ROADMAP_CN.md)）。报 bug 时，最能省时间的两条信息是：**开机横幅那一行**，以及**屏幕是自己恢复的还是必须断电**。

## 致谢与许可

建立在 M5Stack 的 M5Unified/M5GFX、乐鑫的 Arduino 核心与 IDF，以及一个从 [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) 起步的配网流程之上。完整署名（含字库的注意事项）见 [CREDITS.md](CREDITS.md)。

**MIT** 许可，见 [LICENSE](LICENSE)。*The Matrix* 是华纳兄弟的商标；本项目是无关联的同人作品，不含任何电影素材。
