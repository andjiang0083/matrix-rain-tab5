# 构建与刷机

[English](BUILDING.md) · **中文**

这不是一个 `pio run` 就能碰运气的项目：它固定了一个社区平台、一个指定版本的 Arduino 核心、一套定制的预编译 IDF 库，还要用一个脚本去覆盖编译器工具链。第一次构建把它搞对，后面就都是快的；闭着眼睛硬刚，大概率要赔进去一晚上。

以下内容都是在 macOS 上真踩出来的。Linux 基本一致，Windows 主要差在路径。

---

## 1. 前置条件

| 需要 | 为什么 |
|---|---|
| **PlatformIO Core 6.1.x** —— 用 `~/.platformio/penv/bin/pio` 那个 | pioarduino 平台要求这个版本。换成别的 Python 环境里的 `pio` 会指向另一套 `core`，固定版本的平台可能装不上。 |
| **ESP-IDF 5.5.x 的 RISC-V 工具链**（`riscv32-esp-elf`） | 构建会把 PlatformIO 自带的工具链换成乐鑫打过补丁的 GCC —— 预编译的 Arduino 库需要它。 |
| **约 1.5GB 磁盘 + 靠谱的网络** | 首次构建要下载平台、3.3.10 的 Arduino 核心压缩包、一个约 200MB 的库包和工具链。 |
| Python 3.8+ 且装了 `Pillow` | 仅在你要重新生成片假名字库（`tools/fontgen.py`）时需要。 |

安装工具链（如果你本地已经有 ESP-IDF v5.5.x）：

```bash
# 方案 A —— 你有 IDF 源码树
~/esp/esp-idf-v5.5.3/install.sh
# 然后把 src/override_toolchain.py 指向 ~/.espressif/tools/riscv32-esp-elf/<版本>/riscv32-esp-elf/bin

# 方案 B —— 只装工具，不要 IDF
python3 $IDF_PATH/tools/idf_tools.py install riscv32-esp-elf
```

**构建之前请先读第 2 节——你几乎一定要改它。**

---

## 2. 工具链覆盖（最耗时间的那个坑）

`src/override_toolchain.py` 通过 `extra_scripts` 挂在构建里，把一条**硬编码**路径塞进 `PATH`：

```python
idf_tc_path = os.path.expanduser(
    "~/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin"
)
env.PrependENVPath("PATH", idf_tc_path)
print(f"Toolchain path overridden: {idf_tc_path}")
```

那个版本号（`esp-14.2.0_20260121`）是**作者机器上的**版本。如果你 `ls ~/.espressif/tools/riscv32-esp-elf/` 出来的是别的目录：

- 构建会先打印出那条不存在的路径，然后以一堆**看起来跟 PATH 毫无关系**的编译错误失败（未知的 `-m` 选项、`cc1plus: error`、链接器找不到 `libgcc`）；
- 改这一行让它对上你安装的版本即可——或者干脆改成自动探测：

```python
import glob, os
cands = sorted(glob.glob(os.path.expanduser("~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin")))
assert cands, "no riscv32-esp-elf toolchain found — see BUILDING.md step 1"
env.PrependENVPath("PATH", cands[-1])
```

非常欢迎有人提 PR 把硬编码路径换成自动探测。

---

## 3. 构建

```bash
git clone https://github.com/andjiang0083/matrix-rain-tab5.git
cd matrix-rain-tab5

~/.platformio/penv/bin/pio run -e tab5
# 首次运行：下载平台、Arduino 核心、库包和 RISC-V 工具链（约 1.5GB）——几分钟
# 之后的每次干净重建（即使 rm -rf .pio）：几秒钟
```

一次健康的构建结尾长这样（数值来自当前代码树）：

```
RAM:   [=         ]  10.4% (used 53008 bytes from 512000 bytes)
Flash: [====      ]  35.8% (used 1127280 bytes from 3145728 bytes)
Building .pio/build/tab5/firmware.bin
Creating binary "firmware.factory.bin" with:
    Offset   | File
 -  0x2000   | bootloader.bin
 -  0x8000   | partitions.bin
 -  0xe000   | boot_app0.bin
 -  0x10000  | firmware.bin
========================= [SUCCESS] Took 7.90 seconds =========================
```

**声明"这个改动能编过"之前，务必在干净树里验证**（`rm -rf .pio && pio run`）。增量构建会掩盖缺失文件和过期的生成头文件——片假名字库和 `sdkconfig.h` 覆盖这两处都特别容易这么坏掉。

构建会打印 `Creating binary "firmware.factory.bin"`，那就是合并好的、可直接刷写的镜像（见第 6 节）。如果你的平台版本不产出它，用第 6 节里给出的 `merge_bin` 命令自己合。

---

## 4. 刷机

```bash
~/.platformio/penv/bin/pio run -e tab5 --target upload --upload-port /dev/cu.usbmodem1101
```

- macOS 上 Tab5 会枚举成 `/dev/cu.usbmodem*`，不需要驱动。如果什么都没出现，换一根 USB-C 线或换一个口——屏幕也是从同一个接口供电的，供电不足会先枚举成功再掉线。
- `upload_speed` 是 `1500000`。如果刷写不稳定，把它降到 `921600`。
- 刷完之后串口一直在 **115200**：

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

一次正常的开机长这样——**报 bug 时请粘这一行**：

```
=== MATRIX RAIN v1.3.1 (Boot NTP + RTC + confirm screen + WiFi-setup) ===
Sprite OK
Bitmap font: 91 chars loaded
Ready — tap screen to cycle character sets, S=screenshot
Connecting to WiFi...
FPS: 30
```

### ⚠️ 串口别长时间开着

USB-CDC 的流量是 PSRAM 带宽的**第三个**消费者（另外两个是 DSI 扫屏和 CPU 回写）。在这块板子上，长时间挂着串口会话就可能把显示通路推过临界点——这正是 [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md) 里那个"冻屏"的成因。串口只用来看开机横幅、抓一条 `FPS:`，然后拔掉，用眼睛判断时钟好不好。

本项目的规矩是：**用眼睛验证，不要靠调试循环。** 一次刷机只改一个看得见的东西。

---

## 5. 串口截图

这块屏读不回来（ST7123 没有可靠的像素回读通路），所以固件改成按需把一帧重新渲染进 PSRAM 的 sprite：

1. 打开串口监视器，按 `S`；
2. 固件先打印 `BMP:<字节数>`，然后以 921600 波特率流式发出 24 位 BMP，最后是 `\nEND\n`；
3. 之后波特率切回 115200。

主机侧必须能扛住波特率切换（设备先切、主机后切，所以要么按 115200 开始抓、接受第一次失败，要么一开始就按 921600 监听、接受横幅是乱码）。一个把这件事做对的小工具脚本已经在路线图里了。

---

## 6. M5Burner 打包

M5Burner 需要**合并镜像**——bootloader + 分区表 + boot_app0 + 应用合成一个文件，从 `0x0` 开始刷——并且对 **DIO** 模式最友好。

构建产物 `firmware.factory.bin` 已经按下面这些偏移合并好了：

| 偏移 | 内容 |
|---|---|
| `0x2000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin`（应用） |

所以：

```bash
cp .pio/build/tab5/firmware.factory.bin releases/matrix-rain-tab5-vX.Y.Z.bin
shasum -a 256 releases/matrix-rain-tab5-vX.Y.Z.bin
```

如果你的平台不产出 factory 镜像，手工合并：

```bash
ESPT=~/.platformio/packages/tool-esptoolpy/esptool.py
CORE=~/.platformio/packages/framework-arduinoespressif32
python3 $ESPT --chip esp32p4 merge_bin \
  --output releases/matrix-rain-tab5-vX.Y.Z.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  .pio/build/tab5/bootloader.bin \
  0x8000  .pio/build/tab5/partitions.bin \
  0xe000  $CORE/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tab5/firmware.bin
```

发布之前先验一遍——下面这段直接从 bootloader 头里读 flash 模式字节，期望 **`0x02` = DIO**：

```bash
python3 - <<'PY'
d = open('releases/matrix-rain-tab5-vX.Y.Z.bin','rb').read()
assert d[0x2000] == 0xE9, "no bootloader image at 0x2000 — wrong merge offsets"
assert d[0x8000:0x8002] == b'\xaa\x50', "no partition table at 0x8000"
print(f"{len(d)} bytes, bootloader flash_mode = 0x{d[0x2002]:02x}",
      "(DIO ✔)" if d[0x2002] == 0x02 else "(⚠ must be 0x02 for M5Burner)")
PY
```

再打 M5Burner 包：`matrix-rain.json`（见 `m5burner/`）+ `.bin` + 一份 README，一起 zip。M5Burner 的 *Import Custom FW* 接受 `.zip` 或 `.json`。

---

## 7. 喂狗（加新界面前请先读）

应用注册的任务看门狗超时是 **8 秒**，并且 `trigger_panic = true`。`wifi_setup.cpp` 里每个阻塞式界面循环都调 `esp_task_wdt_reset()` 就是为了这个：一个等触摸却不喂狗的向导界面会在几秒内把板子复位，panic 日志看起来像"随机崩溃"。

显示通路还有两道保护，它们不是装饰：

- `drawRainOn()` 每 40 列调一次 `esp_task_wdt_reset()`；
- `loop()` 用 **3 秒**预算比对帧起始时间，超了就 `esp_restart()`——在这块板子上，这是 DSI 写入卡住后**唯一**的恢复手段（写入阻塞时也不会走到看门狗）。

**除非你有更好的替代方案，否则不要删掉帧超时保护。**

---

## 8. 故障对照表

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 编译报诡异的错：找不到 `libgcc`、未知 `-m*` 选项 | `src/override_toolchain.py` 指向了你没装的工具链版本 | 装 ESP-IDF 5.5.x 的 `riscv32-esp-elf`，或改那条路径（第 2 节） |
| `Directory specified in EXTRA_COMPONENT_DIRS doesn't exist`／平台装不上 | PlatformIO Core 太旧，或没用 `penv` 里的那个二进制 | 用 `~/.platformio/penv/bin/pio` |
| 下载 `framework-arduinoespressif32-libs` 失败 | 固定的第三方库包地址不可达 | 检查 `platformio.ini` 里的 `platform_packages` 地址；这个包是社区在维护 |
| 黑屏、不下雨，但能正常启动 | PSRAM 没跑在 200MHz（预编译库需要） | 别动 `build_flags` 里的 `-I src` 和 `src/sdkconfig.h`——它用 `#include_next` 覆盖了 `CONFIG_SPIRAM_SPEED` |
| 串口打印 `Sprite ALLOC FAILED` | PSRAM 没初始化（同上），或者 `canvas.setPsram(true)` 被删了 | 见上一行 |
| WiFi 扫不到网络／永远连不上 | hosted SDIO 管脚没设，默认值对这块板子是错的 | 在 `hostedInitWiFi()` **之前**调 `hostedSetPins(12, 13, 11, 10, 9, 8, 15)`（`wifi_setup.cpp` 里已经做了） |
| 网络列表里找不到你的热点 | ESP32-C6 hosted 链路**只支持 2.4GHz**，5GHz 的 SSID 根本不会出现（隐藏 SSID 会扫到但可能显示为空白） | 用 2.4GHz 的 SSID |
| 启动后分区不对／应用装不下 | 分区表不对 | `board_build.partitions = app3M_fat9M_16MB.csv` |
| 画面跑几秒到几分钟后冻住，无 panic，必须断电 | PSRAM 总线争用（DSI + CPU 回写 + USB DMA） | 拔掉 USB；多数情况下 3 秒帧保护会自己重启回来——详见 [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md) |
| 随机复位，日志里 `Task watchdog got triggered` | 有阻塞界面没喂狗 | 在等待循环里加 `esp_task_wdt_reset()`（第 7 节） |
| 打印 `TIMEOUT 3000ms` 然后重启 | 帧保护触发——某一帧超过 3 秒 | 通常还是总线争用那件事：检查是不是新加了全屏 sprite 或第二个帧缓冲写入者 |
| 退出设置菜单时蓝屏并重启 | 1.3.1 之前的 sprite/发光缓冲残留状态 | v1.3.1 会在退出时清发光缓冲并重新同步时钟——升级 |
| 退出菜单后屏幕有"残影"（旧菜单像素） | 同上 | 同上 |
| 片假名集显示成空白格 | `katakana_font.h` 缺失或被截断了——它是生成物 | 跑 `python3 tools/fontgen.py > src/katakana_font.h`（需要 Pillow）；在装了该源字体的机器上，生成结果与仓库里的头文件**逐字节一致** |
| 片假名跟截图里长得不完全一样 | 你用别的 TTF 重新生成过字库 | 属正常——见 [CREDITS.md](CREDITS.md) |
| 中文 SSID 显示成 `□` 方框 | GLCD 字体和屏幕键盘只支持 ASCII | 已知限制，见 README 现状表 |
| M5Burner 能导入但板子启动不了 | 镜像没合并，或者是 QIO | 按第 6 节的偏移重新合并，并检查 flash 模式字节 |
| 串口完全没输出 | 端口/波特率不对，或固件卡在 `M5.begin()` 之前 | 115200，`/dev/cu.usbmodem*`；按一下复位键 |

---

## 9. 报 bug 用的版本串

两个字符串能精确定位一次构建。请都贴上：

1. 开机横幅 —— `=== MATRIX RAIN vX.Y.Z (…) ===`
2. 一行 `FPS:`，以及屏幕是**自己恢复的**还是**必须断电**。

**Setup → About** 里显示的版本应该和横幅一致；如果不一致，你刷的是旧镜像。
