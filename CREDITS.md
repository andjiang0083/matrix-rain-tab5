# Credits and attribution

中文摘要见文末（[跳到中文](#中文摘要)）。

This project is built on other people's work, and one of its files is generated from a font with terms you should know about. Here is exactly whose work is involved and under what terms.

## Build dependencies (fetched at build time, not vendored)

| Project | Author / origin | Licence | How it is used here |
|---|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) (and M5GFX) | M5Stack | MIT | Board abstraction, display/touch/RTC drivers for the Tab5. Pinned to the stable `^0.2.17` line — the `develop` branch had touch-detection timing issues on this board. |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) 3.3.10 | Espressif | LGPL-2.1 | Arduino API, `Preferences`/NVS, `esp32-hal-hosted.h` (the ESP32-C6 hosted link). |
| [esp32-arduino-lib-builder](https://github.com/bmorcelli/esp32-arduino-lib-builder) prebuilt libs | bmorcelli | see that repository | Prebuilt ESP-IDF libraries for the Tab5, built with PSRAM at 200 MHz. Without them the panel does not come up. |
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 | Espressif | Apache-2.0 | The SDK underneath: MIPI-DSI, LEDC, esp_hosted, esp_wifi, esp_task_wdt. |
| [PlatformIO](https://platformio.org/) + [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) | PlatformIO Labs / pioarduino community | Apache-2.0 | Build system and the community platform that supports ESP32-P4. |

## Influences and reference material

| Project | How it influenced this one |
|---|---|
| [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) (MIT, © 2023 shikarunochi) | The WiFi provisioning *approach* this project's wizard follows: scan → list → on-screen keyboard → connect → store credentials. The Tab5 implementation in `src/wifi_setup.cpp` is written directly against the IDF API (`esp_wifi_*`) for this board, including the hosted SDIO pin setup that the Launcher documents (`launcherWifiInitHostedSdio()`); no code was copied verbatim. |
| [tobozo/M5Tab5-Game-and-Watch](https://github.com/tobozo/M5Tab5-Game-and-Watch) | A production-grade reference for `platformio.ini` settings on this board (DSI clock, PSRAM speed, partition table). |
| [piaoxuebingfeng/M5TAB5-Demos](https://github.com/piaoxuebingfeng/M5TAB5-Demos) | Peripheral examples (IMU, WiFi STA/scan, RTC) used to confirm what works on the Tab5 during the port. |
| [M5Stack Tab5 documentation](https://docs.m5stack.com/en/core/Tab5) | Pin maps, power topology and the hardware specification table in the README. |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | M5GFX's upstream; the GLCD font used for the rain's ASCII sets comes from that font set — see its repository for the font's own terms. |

## Generated file with terms attached — `src/katakana_font.h`

⚠️ **Read this if you plan to redistribute or use this project commercially.**

- `src/katakana_font.h` is generated data, not hand-written code: 91 glyphs rasterised to 16×16 1 bpp by `tools/fontgen.py`.
- The committed bitmap was rendered from **Hiragino Kaku Gothic (ヒラギノ角ゴシック W3)**, a macOS **system font**. System-font licences generally permit rendering and printing, but they are not open licences, and in some jurisdictions a rendered bitmap is treated more strictly than one might expect.
- The author's position: this is a hobby project, the file is a 15 KB table of 16×16 bitmaps, and it is documented here rather than hidden. **If you are redistributing this project inside a product, or in a jurisdiction where font-derived bitmaps are treated as derivative works, regenerate the font from an open-licensed typeface first:**

  ```bash
  # edit the font path in tools/fontgen.py (currently a macOS system font), then:
  python3 tools/fontgen.py > src/katakana_font.h
  ```

  Good OFL candidates: **Noto Sans JP**, **M PLUS 1 Code**, **IPAex Gothic**, **Source Han Sans**. Note that glyph shapes at 16×16 will differ slightly from the shipped set — that is expected. Making the path a `--font` argument is on the [roadmap](ROADMAP.md); a PR is welcome.

## Trademarks and the fan-work boundary

- *The Matrix* — including the green "digital rain" as an identifiable visual and the quoted lines in the UI and docs — is the property of **Warner Bros.** This is an **unaffiliated fan project**: it contains no film assets, no logos and no artwork, and it is not endorsed by or connected to the rights holders. Quotes are used as short cultural references in a piece of free fan software.
- **M5Stack**, **Tab5**, **ESP32** are trademarks of their respective owners. This project is not affiliated with M5Stack; it is a community application written by an owner of the hardware.
- The project ships **no game ROMs, no BIOS images, no fonts under proprietary licences, and no media** of any kind. If you believe something here infringes your rights, please open an issue — it will be removed or replaced promptly.

## Port-specific work in this repository

Everything else is this repository's own work: the rain engine and its band-swept clearing / depth model, the 7-segment ghost-glow clock and the refraction of glyphs around lit segments, the katakana bitmap rasteriser path, the direct-DSI drawing budget and the frame-timeout guard, the green-on-black setup menu, the brightness/PWM handling and the idle state machine — plus the hardware findings written up in [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md).

---

## 中文摘要

本项目建立在他人工作之上，并且有**一个文件是生成物、带着条款**，所以有必要写清楚：

- **构建依赖**（构建时下载，不入库）：M5Unified / M5GFX（MIT，M5Stack）、Arduino-ESP32 3.3.10 核心（LGPL-2.1，乐鑫）、bmorcelli 的预编译 IDF 库（Tab5 用，PSRAM 200MHz）、ESP-IDF 5.5（Apache-2.0）、PlatformIO + pioarduino 平台。
- **参考与影响**：配网流程的思路来自 [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)（MIT，© 2023 shikarunochi），本仓库的 Tab5 实现是按 IDF API 重写的，没有照搬代码；`platformio.ini` 的板级参数参考了 tobozo/M5Tab5-Game-and-Watch；外设可用性参考了 piaoxuebingfeng/M5TAB5-Demos 与 M5Stack 官方文档。
- ⚠️ **`src/katakana_font.h` 是从 macOS 系统字体「ヒラギノ角ゴシック W3」渲染出的位图数据**（91 个 16×16 字形）。系统字体许可通常允许渲染使用，但不是开源许可。如果你要把本项目放进产品里分发，或所在司法辖区对"字体渲染位图"判定更严，**请用 OFL 字体重新生成**（改 `tools/fontgen.py` 里的字体路径后重跑）：Noto Sans JP、M PLUS 1 Code、IPAex Gothic、Source Han Sans 都可用。16×16 下的字形会和现在这版有细微差别，这是正常的。
- **《骇客帝国》** 及其绿色"数字雨"视觉、界面里引用的台词属于**华纳兄弟**。本项目是**无关联的同人作品**：不含任何电影素材、logo 或美术资源，也未获权利方认可。**M5Stack / Tab5 / ESP32** 是各自所有者的商标，本项目与 M5Stack 无隶属关系。
- 本仓库**不含任何游戏 ROM、BIOS 镜像、专有字体或媒体文件**。若你认为此处内容侵犯了你的权利，请开 issue，我们会尽快移除或替换。
