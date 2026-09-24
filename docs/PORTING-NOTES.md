# Porting notes — how this Tab5 build came to be

**English** · [中文](#中文版)

These are the working notes behind the code: what was measured, what was believed and later disproved, and which roads are dead ends. They are here so the next person does not pay for the same lessons.

If you only read one section, read [§2 — the display path](#2-the-display-path-where-every-hour-went).

---

## 1. What the port started from

The application began as a cyber-clock for the **M5StickS3** (240×135 SPI panel, two buttons) and was lifted onto the Tab5. Almost nothing transfers mechanically:

| | M5StickS3 | M5Stack Tab5 |
|---|---|---|
| SoC | ESP32-S3 (Xtensa, 240 MHz) | ESP32-P4 (RISC-V, dual 360 MHz + LP core) |
| Panel | 240×135 SPI, 8-bit-ish colour, cheap to overdraw | 1280×720 MIPI-DSI with a DMA scan-out at ~110 MB/s |
| Framebuffer | small, partly on-chip | 1.84 MB in PSRAM for a full-screen sprite |
| Input | two physical buttons | the panel's integrated touch controller |
| Radio | on-die WiFi | **none** — an ESP32-C6 over SDIO ("hosted") |
| Time | NTP, no RTC | NTP + RX8130CE hardware RTC |
| Scaling | 55 drops | 80 columns × up to 18 glyphs per column |

The interesting consequence: on the StickS3 you can afford to overdraw carelessly. On the Tab5, **every drawn pixel is a CPU write into the memory the display controller is concurrently reading**, and that single fact reorganises the whole design.

## 2. The display path (where every hour went)

### 2.1 What the failure looked like

Roughly 30 seconds after boot, the rain stops mid-frame. No `Guru Meditation`, no panic, no watchdog reset — the CPU is alive (touch still responds, serial still prints), the panel is simply frozen and stays frozen until you cut power. It appeared the moment the port changed the rain from 160 columns of 8 px characters to 80 columns of 16 px characters.

### 2.2 The version timeline

| Version | Change | Result |
|---|---|---|
| 1.0.0 | 160 columns × 20-glyph trails, GLCD font at 1× (8 px) | ✅ stable, 37–48 fps |
| 1.0.1 | 80 columns × 8-glyph trails, switched to "Font2" | ❌ freeze ≈ 30 s |
| 1.0.2 – 1.0.4 | Column grouping, 4-glyph trails, removed background fills, removed the `startWrite` batch | ❌ still froze |
| 1.0.5 | Back to the GLCD rasteriser with a real 2× `setTextSize` | ❌ still froze |
| 1.0.6 | **Double buffering**: two framebuffers, flip driven from an `on_refresh_done` callback | ❌ **Guru Meditation (load access fault) — reverted** |
| 1.0.7 | Single-buffered direct drawing, 30 fps cap, whole-trail redraw, 18-glyph trails | ✅ stable, 30 fps |

The early attempts at 1.0.2–1.0.4 are a case study in the wrong method: four variants, all built on the assumption that some *specific drawing call* was at fault, with no theory of the failure that predicted anything. They were eliminated one by one and taught nothing.

### 2.3 The trap that started it: `drawChar`'s fourth argument

```cpp
out.drawChar(code, x, y, 2);   // ⚠ this is NOT a scale factor
```

`LGFXBase`'s four-argument overload is `drawChar(uint16_t, int32_t, int32_t, uint8_t font)` — the last parameter is a **font index into `fontdata[]`**:

| Call | Font | Rasteriser | Cell height |
|:--:|---|---|:--:|
| `drawChar(c, x, y, 1)` | Font0 (GLCD) | column-wise vertical strips | 8 px |
| `drawChar(c, x, y, 2)` | Font2 (BMP) | **row-wise horizontal strips** | 16 px |

So the "make the characters twice as big" edit silently swapped in a *different rendering engine* whose PSRAM write pattern is row-major. To actually scale the GLCD font you use the three-argument form with `setTextSize(2)`.

This mattered because the two rasterisers interleave differently with the DSI scan-out: the BMP font writes long horizontal runs across the same rows the DMA is streaming, while the GLCD font writes narrow vertical runs. Two hypothesis generations later, the honest conclusion is that this was a **catalyst**, not the root cause: fixing the font call (1.0.5) did not fix the freeze.

### 2.4 The dead end: double buffering

The textbook answer — give the DMA one buffer while the CPU writes another — was implemented (two framebuffers, flip on refresh-done via a shadowed struct) and crashed immediately with a load access fault. Three reasons, all of which make it a bad idea *on this stack*:

1. the refresh-done callback runs in an interrupt context and needs `IRAM_ATTR` (M5GFX's version does not have it);
2. reaching into M5GFX's internal struct layout is fragile — offsets move with the IDF version;
3. the ESP-IDF DPI driver's DMA link-list handling is not designed for an external writer flipping buffers from an ISR.

Also relevant: at 1280×720@60 Hz the scan-out alone needs ≈110 MB/s of PSRAM bandwidth, so the "spare" buffer is not free memory, it is more traffic. **If you are tempted to retry this, do it behind a runtime switch, with a bandwidth budget, and expect to explain why the panel does not wedge.**

### 2.5 The model that survived: PSRAM bus contention

Best-supported explanation, and the one the shipped design is built around:

```
PSRAM bus, three possible writers:
  ┌─────────────────┐
  │ DSI scan-out DMA│  ≈110 MB/s, continuous, cannot be paused
  ├─────────────────┤
  │ CPU write-back  │  per-frame glyph and band writes
  ├─────────────────┤
  │ USB-CDC DMA     │  only when a serial session is attached
  └─────────────────┘
  → arbitration latency spikes → DSI bridge FIFO underruns → controller locks up
```

Evidence that fits:

- **The USB dependency.** With the cable detached the clock runs; with a serial session attached it was much more likely to freeze. USB DMA is the one writer you can add or remove at will, and removing it changes the outcome.
- **Why 1.0.0 survived.** 160 columns of 8 px glyphs is *more draw calls* but a quarter of the bytes per glyph, written as narrow vertical runs. Total per-frame write volume — and its burstiness — was lower than the 80-column 16 px configuration that died.
- **Why removing background fills and transactions changed nothing.** Those reduce draw *calls*, not bytes into PSRAM.

What is **not** established: the exact threshold, whether it is a bandwidth or a latency problem, and how much of it is IDF-version-specific. Treat the model as the best-supported hypothesis that predicts the observed behaviour, not as a datasheet number. A measured reproduction would be a genuinely valuable contribution (see [ROADMAP.md](../ROADMAP.md)).

### 2.6 What the design does about it

1. **No sprite in the hot path.** The full-screen `LGFX_Sprite` exists only for the on-demand screenshot; rendering goes straight to the panel. (The panel cannot be read back — the ST7123 exposes no reliable pixel-read path — which is why screenshots re-render a frame instead of grabbing one.)
2. **Band-swept clearing.** Each column tracks `prevY` and clears only the strip it swept since the previous frame. The "tails are never erased, load grows without bound, panel dies" failure is a real one — it was the *first* bug in this port, and it looks exactly like a thermal or leak problem if you do not think about who owns which pixels.
3. **A 30 fps cap.** Not a measured ceiling — a deliberate traffic budget.
4. **Pre-computed colours.** The 18 trail shades are calculated once at boot; the per-frame loop does no floating-point colour maths.
5. **The frame-timeout guard.** `loop()` measures its own frame; over **3 s** it calls `esp_restart()`. This is the only recovery available when a write to a wedged DSI never returns — the task watchdog never sees it either, because the task is blocked inside a driver call.
6. **Never remove the watchdog feeds** in the blocking UI screens (`wifi_setup.cpp`). An 8 s task watchdog with `trigger_panic` is armed; a wizard screen that waits for a touch without feeding it resets the board and produces a panic log that looks like a random crash.

## 3. Rendering details that are easy to get wrong

- **Trail colour ramp.** `fade(t) = 1 − t/(MAX_TRAIL−1)`, head weight `0.15 + 0.85·fade²`, blended from a bright head towards a very dark green floor rather than towards black — a hard fade to black reads as "stripes", a floor reads as depth.
- **Per-cell glyph churn.** Each trail cell re-rolls its character with probability `1/(t²+2)` (capped at 1/120): the head flickers constantly, the tail is nearly static. Uniform churn across the trail destroys the sense of motion.
- **Depth per column.** `depth ∈ [0.6, 1.5]` scales both speed and trail length. Uniform columns look like a loading animation; varying them is what makes it read as rain.
- **The clock is seven rectangles, not a font.** Digit geometry is `230 × 272 px` per cell, placed so the four digits + colon span 1080 px, centred. Segments only participate when the digit's segment is *on* for the current time.
- **Refraction, not masking.** A glyph overlapping a lit segment is offset 4 px inwards from that segment's edge (top segments push down, left segments push right, …) and gets a small brightness/hue shift — warm for top segments, cool for bottom ones. This is what makes the rain look like it bends around the digits instead of being hidden behind them.
- **The glow buffer is a PAL, not a blur.** `g_clockGlow[80][45]` is fed only by the *head* of a column, decays one level per frame, and is re-drawn as ghost characters. Because the head is refreshed from the bottom up as the column falls, the digits fill in a FIFO order rather than uniformly — worth preserving if you refactor.
- **Katakana is blitted, not rendered.** 16×16 1 bpp glyphs written as horizontal run-length `fillRect`s: 91 glyphs in ~15 KB, no CJK font engine, no PSRAM allocation.

## 4. Radio, time and power on this board

- **The P4 has no radio.** WiFi comes from an ESP32-C6 over SDIO, and the hosted link needs its pins set explicitly before initialisation: `hostedSetPins(12, 13, 11, 10, 9, 8, 15)` (CLK, CMD, D0–D3, RST) then `hostedInitWiFi()`. The library defaults are wrong for this board, and the failure mode is silent: a scan that returns zero networks.
- **2.4 GHz only.** 5 GHz SSIDs are simply absent from the scan list.
- **Init once.** The hosted stack is initialised once and left alive; re-running the wizard uses `esp_wifi_disconnect()` + a fresh scan rather than re-initialising SDIO.
- **UTC in the RTC.** NTP gives UTC, which is written to the RX8130CE; the display applies the stored UTC offset at read time (`updateClockFromNTP()`), so changing timezone does not need the network. The RTC is re-read every 60 s, which is cheap and immune to the `configTime()`/SNTP re-entry pitfalls of the older builds.
- **Idle is not sleep.** Three levels — 2 min → 25 % brightness, 3 min → 8 fps, 10 min → backlight off. No light sleep is implemented on this build; the CPU keeps running. Idle *current* has never been measured here, which is why the roadmap asks for INA226 numbers rather than claims.

## 5. Build system notes

Summarised here, fully documented in [BUILDING.md](../BUILDING.md):

- the platform is a pinned community fork (`pioarduino/platform-espressif32`), because upstream PlatformIO does not support the P4;
- the Arduino core and the IDF libraries are **pinned URLs** (3.3.10 plus a community-built lib bundle with PSRAM at 200 MHz) — the stock libraries do not bring the panel up;
- `src/override_toolchain.py` prepends a **hard-coded** RISC-V toolchain path to `PATH`; this is the single most common newcomer failure, and the fix is documented (and would make a good PR);
- `src/sdkconfig.h` overrides `CONFIG_SPIRAM_SPEED` to 200 for the prebuilt libs via `#include_next`, which is why `-I src` sits in `build_flags`;
- the merged M5Burner image is `firmware.factory.bin` from the build: bootloader @ `0x2000`, partitions @ `0x8000`, boot_app0 @ `0xe000`, application @ `0x10000`, DIO flash mode.

## 6. Open questions (good places to contribute)

1. **Measure the PSRAM arbitration limit.** How many concurrent writers, at what write patterns, before the DSI stalls? This would turn §2.5 from a hypothesis into a number.
2. **Is the Copy Engine / PPA usable for band updates?** Hardware-assisted moves would take the CPU off the PSRAM bus — but every naive full-screen DMA2D burst is also known to wedge the panel, so it needs throttling and a switch.
3. **Can the per-frame write volume be cut without adding a writer?** Only drawing columns whose head actually moved a cell is the obvious candidate.
4. **Does the `esp_hosted` link have a cost during rendering?** The C6 link is idle after the boot sync, but it is still a peripheral on the same interconnect; nobody has measured whether keeping it initialised costs anything.

---

## 中文版

[English](#porting-notes--how-this-tab5-build-came-to-be) · **中文**

这是代码背后的工作笔记：量过什么、信过什么后来被推翻、哪些路是死路。写下来的目的是让下一个人不必重复交学费。

### 1. 移植起点

这个应用最早是 **M5StickS3**（240×135 SPI 屏、两个按键）上的赛博时钟，后来被抬到 Tab5 上。几乎没有任何东西能机械照搬：

| | M5StickS3 | M5Stack Tab5 |
|---|---|---|
| 主控 | ESP32-S3（Xtensa 240MHz） | ESP32-P4（RISC-V 双 360MHz + LP 核） |
| 屏幕 | 240×135 SPI，随便重绘都不心疼 | 1280×720 MIPI-DSI，DMA 扫屏约 110MB/s |
| 帧缓冲 | 很小，部分在片内 | 全屏 sprite 在 PSRAM 里要 1.84MB |
| 输入 | 两个物理按键 | 面板一体的触控 IC |
| 无线 | 片内 WiFi | **没有**——靠 SDIO 挂一颗 ESP32-C6（hosted） |
| 时间 | NTP，无 RTC | NTP + RX8130CE 硬件 RTC |
| 规模 | 55 个雨滴 | 80 列 × 每列最多 18 字符 |

关键后果：在 StickS3 上你可以放心地乱重绘；在 Tab5 上，**你画的每个像素都是往显示控制器正在同时读的那片内存里写**，这一个事实重排了整个设计。

### 2. 显示通路（时间都花在这里）

**故障长什么样**：开机约 30 秒后，雨在某帧中间停住。没有 `Guru Meditation`、没有 panic、没有看门狗复位——CPU 还活着（触控有反应、串口还在打印），只有面板冻住了，而且不断电就一直冻着。它出现的时机，恰好是把雨从"160 列 × 8px 字符"改成"80 列 × 16px 字符"的那一刻。

**版本时间线**

| 版本 | 改动 | 结果 |
|---|---|---|
| 1.0.0 | 160 列 × 20 格拖尾，GLCD 字体 1×（8px） | ✅ 稳定，37–48fps |
| 1.0.1 | 80 列 × 8 格拖尾，改用 "Font2" | ❌ 约 30 秒冻住 |
| 1.0.2 – 1.0.4 | 列分组、缩短拖尾、去掉背景填充、去掉 `startWrite` 事务 | ❌ 仍然冻 |
| 1.0.5 | 回到 GLCD 光栅器 + 真正的 2× `setTextSize` | ❌ 仍然冻 |
| 1.0.6 | **双缓冲**：两个帧缓冲，由 `on_refresh_done` 回调驱动翻转 | ❌ **Guru Meditation（load access fault）——已回退** |
| 1.0.7 | 单缓冲直绘、30fps 上限、整条拖尾重绘、拖尾 18 格 | ✅ 稳定，30fps |

1.0.2–1.0.4 这四次尝试是"方法错了"的典型案例：四个变体都建立在"某个具体绘制调用有问题"的假设上，而那个假设无法预测任何现象，所以它们只是被一条条排除，什么也没教给我们。

**引子级的陷阱：`drawChar` 的第四个参数**

```cpp
out.drawChar(code, x, y, 2);   // ⚠ 这不是缩放倍数
```

`LGFXBase` 的四参重载是 `drawChar(uint16_t, int32_t, int32_t, uint8_t font)`——最后一个参数是**字体索引**：`0/1` = Font0（GLCD，逐列竖条纹，8px），`2` = Font2（BMP 字体，**逐行横条纹**，16px）。也就是说，"把字放大两倍"这个改动其实悄悄换掉了整个光栅器。真正的放大应该用三参版本加 `setTextSize(2)`。

它之所以要紧，是因为两种光栅器与 DSI 扫屏的交织方式不同：BMP 字体沿 DMA 正在流的同一行写长横条，GLCD 字体写窄竖条。但两代假设之后，诚实的结论是：**它是催化剂，不是根因**——把字体调用改对（1.0.5）并没有止住冻屏。

**死路：双缓冲**

教科书答案——DMA 读一个缓冲、CPU 写另一个——实现出来（两个帧缓冲 + 用影子结构体在刷新完成回调里翻转）立刻以 load access fault 崩溃。三个原因，每一个都说明它**在这套技术栈上**是坏主意：(1) 刷新回调在中断上下文里，需要 `IRAM_ATTR`，而 M5GFX 那个函数没有；(2) 伸手去改 M5GFX 内部结构体的布局很脆，偏移量随 IDF 版本变；(3) ESP-IDF 的 DPI 驱动 DMA link-list 不是为"外部者在 ISR 里翻缓冲"设计的。还有一点：1280×720@60Hz 的扫屏本身就要约 110MB/s，所以"空闲的那个缓冲"不是免费内存，而是更多流量。**如果你想重试，请放在运行时开关后面、带带宽预算，并且准备好解释为什么面板这次没被楔死。**

**活下来的模型：PSRAM 总线争用**

```
PSRAM 总线上可能的三个写入者：
  DSI 扫屏 DMA     ≈110MB/s，持续，无法暂停
  CPU 回写          每帧的字形与清扫写
  USB-CDC DMA       只有挂着串口会话时才有
  → 仲裁延迟尖峰 → DSI 桥 FIFO underrun → 控制器锁死
```

支持它的证据：**USB 依赖**——拔掉线时钟能跑，插着串口会话时冻的概率明显升高，而 USB DMA 正是你唯一能随时增减的那个写入者；**为什么 1.0.0 能活**——160 列 8px 是*更多次*绘制调用，但每个字形只有四分之一字节量、且是窄竖条，每帧总写入量与突发性都低于后来死掉的 80 列 16px 配置；**为什么去掉背景填充和事务没用**——那些减少的是绘制*调用次数*，不是写进 PSRAM 的字节数。

**没有确立的**：确切的阈值、到底是带宽问题还是延迟问题、以及其中多少是特定 IDF 版本的行为。请把这个模型当作"能解释全部观察现象的最佳假设"，而不是数据手册上的数字。一次可复现的量化实验会是非常有价值的贡献（见 [ROADMAP_CN.md](../ROADMAP_CN.md)）。

**设计因此做的六件事**：热路径里不用 sprite（全屏 sprite 只服务于按需截图；面板无法回读——ST7123 没有可靠的像素读取通路，所以截图是重新渲染一帧）；按扫过的带清扫（每列记 `prevY`，只清它这一帧扫过的那一条）；30fps 上限（不是量出来的天花板，是刻意的流量预算）；颜色预计算（18 档拖尾色开机算一次）；**帧超时保护**（一帧超过 3 秒就 `esp_restart()`——当写向已经卡死的 DSI 的调用永不返回时，这是唯一的恢复手段，任务看门狗也看不到它，因为任务阻塞在驱动调用里）；**不要删阻塞界面里的喂狗**（8 秒任务看门狗带 `trigger_panic`，一个等触摸却不喂狗的向导界面会复位板子，并留下一份看起来像"随机崩溃"的 panic 日志）。

### 3. 容易做错的渲染细节

- **拖尾色阶**：`fade(t) = 1 − t/(MAX_TRAIL−1)`，头部权重 `0.15 + 0.85·fade²`，从亮头混向一个**很暗但不黑**的绿底而不是混向黑——硬落到黑会读出"条纹"，留个底才会读出"纵深"。
- **每格独立换字**：概率 `1/(t²+2)`（上限 1/120）。头部剧烈闪烁、尾部几乎静止；整条拖尾均匀换字会把"运动感"毁掉。
- **每列纵深**：`depth ∈ [0.6, 1.5]` 同时缩放速度和拖尾长度。所有列一样就像个加载动画；有差异才像雨。
- **时钟是七个矩形，不是字体**：每个数字单元 `230 × 272 px`，四个数字 + 冒号共 1080px 居中。只有当前时间该段**点亮**时，该段才参与折射与发光。
- **是折射，不是遮罩**：与点亮段相交的字符会被从该段边缘向内推 4px（上段往下推、左段往右推……）并做小幅提亮/偏色——上段偏暖、下段偏冷。这才让雨看起来是"绕着数字弯过去"，而不是被数字挡住。
- **发光缓冲是残影，不是模糊**：`g_clockGlow[80][45]` 只由列的**头部**刷新，每帧衰减一级，再作为幽灵字符重绘。因为头部随列下落自下而上刷新，数字里是**FIFO 顺序**积攒起来的，不是均匀铺满——重构时请保住这个性质。
- **片假名是位块拷贝，不是字体渲染**：16×16 1bpp 字形用水平游程 `fillRect` 写出，91 个字形约 15KB，不链 CJK 引擎、不分配 PSRAM。

### 4. 无线、时间与省电

- **P4 自身没有无线**：WiFi 来自 SDIO 上的 ESP32-C6，hosted 链路必须在初始化前显式设管脚 `hostedSetPins(12, 13, 11, 10, 9, 8, 15)`（CLK, CMD, D0–D3, RST）再 `hostedInitWiFi()`。库的默认值对这块板子是错的，而失败方式是静默的：扫描返回零个网络。
- **只支持 2.4GHz**：5GHz 的 SSID 根本不出现在列表里。
- **只初始化一次**：hosted 栈初始化一次后一直留着；重跑向导用 `esp_wifi_disconnect()` + 重新扫描，不重新初始化 SDIO。
- **RTC 里存 UTC**：NTP 得到 UTC 写入 RX8130CE；显示时再叠加存储的 UTC 偏移（`updateClockFromNTP()`），所以改时区不需要网络。每 60 秒读一次 RTC，够便宜，也避开了早期版本反复调 `configTime()`/SNTP 的坑。
- **闲置不等于睡眠**：三档——2 分钟→25% 亮度、3 分钟→8fps、10 分钟→关背光。本版本没实现 light sleep，CPU 一直跑着。这里**从未测过闲置电流**，所以路线图里要的是 INA226 的数字，而不是形容词。

### 5. 构建系统要点

详见 [BUILDING_CN.md](../BUILDING_CN.md)，摘要：平台是固定版本的社区分支（`pioarduino/platform-espressif32`，上游 PlatformIO 不支持 P4）；Arduino 核心与 IDF 库都是**固定 URL**（3.3.10 + 社区构建的 PSRAM 200MHz 库包，官方库点不亮这块屏）；`src/override_toolchain.py` 把一条**硬编码**的 RISC-V 工具链路径塞进 `PATH`（新人最常撞的失败点，修法已写在文档里，也适合做成 PR）；`src/sdkconfig.h` 用 `#include_next` 覆盖 `CONFIG_SPIRAM_SPEED`，这就是 `build_flags` 里那个 `-I src` 存在的原因；M5Burner 合并镜像就是构建产出的 `firmware.factory.bin`（bootloader @ `0x2000`、分区表 @ `0x8000`、boot_app0 @ `0xe000`、应用 @ `0x10000`，DIO 模式）。

### 6. 开放问题（适合上手的地方）

1. **量化 PSRAM 仲裁极限**：几个并发写入者、什么写入模式，会把 DSI 顶到挂？这能把第 2 节的假设变成数字。
2. **Copy Engine / PPA 能不能用来做带更新？** 硬件搬运能把 CPU 挪出 PSRAM 总线——但朴素的整屏 DMA2D 突发也已知会把面板楔死，所以需要限流 + 开关。
3. **不增加写入者的前提下，能不能减少每帧写入量？** 显而易见的方向是：只画头部真的走满了一格的列。
4. **`esp_hosted` 链路在渲染时有没有代价？** 开机对时后 C6 链路是闲置的，但它仍是同一互连上的外设，没人量过保持初始化是否需要成本。
