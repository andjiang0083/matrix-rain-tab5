# Building and flashing

**English** · [中文](BUILDING_CN.md)

This is not a `pio run`-and-hope project. It pins a community platform, a specific Arduino core, a custom set of prebuilt ESP-IDF libraries, and it overrides the compiler toolchain from a script. Build it once, get it right, and everything after that is fast — fight it blindly and you will lose an evening.

Everything below was learned the hard way on macOS. Linux behaves the same; Windows differs mostly in paths.

---

## 1. Prerequisites

| Need | Why |
|---|---|
| **PlatformIO Core 6.1.x** — use the one in `~/.platformio/penv/bin/pio` | The pioarduino platform requires it. A `pio` from some other Python installation will resolve to a different `core` and the pinned platform may not install. |
| **ESP-IDF 5.5.x RISC-V toolchain** (`riscv32-esp-elf`) | The build replaces the PlatformIO toolchain with Espressif's patched GCC, which the prebuilt Arduino libraries need. |
| **~1.5 GB of disk and a decent network connection** | The first build downloads the platform, a 3.3.10 Arduino core archive, a ~200 MB library bundle and the toolchain. |
| Python 3.8+ with `Pillow` | Only if you regenerate the katakana font (`tools/fontgen.py`). |

Installing the toolchain (if you already have ESP-IDF v5.5.x somewhere):

```bash
# option A — you have an IDF checkout
~/esp/esp-idf-v5.5.3/install.sh
# then point src/override_toolchain.py at ~/.espressif/tools/riscv32-esp-elf/<version>/riscv32-esp-elf/bin

# option B — just install the tools, no IDF
python3 $IDF_PATH/tools/idf_tools.py install riscv32-esp-elf
```

**Read step 2 before you build — you will almost certainly have to edit it.**

---

## 2. The toolchain override (the pitfall that eats the most time)

`src/override_toolchain.py` is wired into the build via `extra_scripts` and prepends a hard-coded path to `PATH`:

```python
idf_tc_path = os.path.expanduser(
    "~/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin"
)
env.PrependENVPath("PATH", idf_tc_path)
print(f"Toolchain path overridden: {idf_tc_path}")
```

That version string (`esp-14.2.0_20260121`) is *the author's machine*. If your `ls ~/.espressif/tools/riscv32-esp-elf/` shows a different directory:

- the build prints your missing path and then fails with compiler errors that look nothing like a PATH problem (unknown `-m` options, `cc1plus: error`, or a linker that cannot find `libgcc`);
- fix it by editing that one line to match your installed version — or make it auto-detect:

```python
import glob, os
cands = sorted(glob.glob(os.path.expanduser("~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin")))
assert cands, "no riscv32-esp-elf toolchain found — see BUILDING.md step 1"
env.PrependENVPath("PATH", cands[-1])
```

A PR that replaces the hard-coded path with auto-detection would be very welcome.

---

## 3. Build

```bash
git clone https://github.com/andjiang0083/matrix-rain-tab5.git
cd matrix-rain-tab5

~/.platformio/penv/bin/pio run -e tab5          # ~2 minutes after the first download
```

A good build ends like this (values from the current tree):

```
RAM:   [=         ]  10.4% (used 53008 bytes from 512000 bytes)
Flash: [====      ]  35.8% (used 1127264 bytes from 3145728 bytes)
Building .pio/build/tab5/firmware.bin
Creating binary "firmware.factory.bin" with:
    Offset   | File
 -  0x2000   | bootloader.bin
 -  0x8000   | partitions.bin
 -  0xe000   | boot_app0.bin
 -  0x10000  | firmware.bin
======================== [SUCCESS] Took 112.21 seconds ========================
```

**Always verify from a clean tree** (`rm -rf .pio && pio run`) before you claim a change builds. Incremental builds hide missing files and stale generated headers — the katakana font and the `sdkconfig.h` override are both easy to break this way.

The build prints `Creating binary "firmware.factory.bin"` — that is your merged, flashable image (see §6). If your platform version does not produce it, see §6 for the explicit `merge_bin` command.

---

## 4. Flash

```bash
~/.platformio/penv/bin/pio run -e tab5 --target upload --upload-port /dev/cu.usbmodem1101
```

- On macOS the Tab5 enumerates as `/dev/cu.usbmodem*`; no driver needed. If nothing appears, try another USB-C cable or port — the panel is powered over the same connector and a weak supply will enumerate and then drop.
- `upload_speed` is `1500000`. If uploads fail intermittently, lower it to `921600` in `platformio.ini`.
- After flashing, the serial port stays available at **115200**:

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

A healthy boot looks like this — **this is the line to quote when filing a bug report**:

```
=== MATRIX RAIN v1.3.1 (Boot NTP + RTC + confirm screen + WiFi-setup) ===
Sprite OK
Bitmap font: 91 chars loaded
Ready — tap screen to cycle character sets, S=screenshot
Connecting to WiFi...
FPS: 30
```

### ⚠️ Keep the serial session short

USB-CDC traffic is a *third* consumer of PSRAM bandwidth (alongside DSI scan-out and the CPU's write-back). A long-lived serial session on this board can push the display path over the edge — this is exactly the mechanism behind the freeze described in [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md). Use the serial port to grab the boot banner and a `FPS:` sample, then disconnect it and judge the clock by looking at it.

As a rule for this project: **verify visually, not through a debug loop.** One visible change per flash.

---

## 5. Screenshots over serial

The panel cannot be read back (the ST7123 has no reliable pixel-read path), so the firmware re-renders a frame into a PSRAM sprite on request:

1. open the monitor, press `S`;
2. the firmware prints `BMP:<bytes>` then streams a 24-bit BMP at 921600 baud, then `\nEND\n`;
3. the tool switches back to 115200.

The host side has to survive a baud-rate change (the device switches before the host does — start the capture at 115200 and let it fail, or listen at 921600 from the beginning and accept that the banner was garbled). A small helper script that does this correctly is on the roadmap.

---

## 6. Packaging for M5Burner

M5Burner wants a **merged image** — bootloader + partition table + boot_app0 + application in one file, flashed from offset `0x0` — and it is happiest with **DIO** flash mode.

`firmware.factory.bin` from the build is already merged with exactly these offsets:

| Offset | Content |
|---|---|
| `0x2000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` (application) |

So:

```bash
cp .pio/build/tab5/firmware.factory.bin releases/matrix-rain-tab5-vX.Y.Z.bin
shasum -a 256 releases/matrix-rain-tab5-vX.Y.Z.bin
```

If your platform does not emit a factory image, merge it yourself:

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

Verify the image before you ship it — this reads the flash-mode byte straight out of the bootloader header and expects **`0x02` = DIO**:

```bash
python3 - <<'PY'
d = open('releases/matrix-rain-tab5-vX.Y.Z.bin','rb').read()
assert d[0x2000] == 0xE9, "no bootloader image at 0x2000 — wrong merge offsets"
assert d[0x8000:0x8002] == b'\xaa\x50', "no partition table at 0x8000"
print(f"{len(d)} bytes, bootloader flash_mode = 0x{d[0x2002]:02x}",
      "(DIO ✔)" if d[0x2002] == 0x02 else "(⚠ must be 0x02 for M5Burner)")
PY
```

Then build the M5Burner bundle: `matrix-rain.json` (see `m5burner/`), the `.bin`, and a README, zipped together. M5Burner's *Import Custom FW* accepts either the `.zip` or the `.json`.

---

## 7. Feeding the watchdog (read this before adding a screen)

The application registers the task watchdog with an **8 s** timeout and `trigger_panic = true`. Every blocking UI loop in `wifi_setup.cpp` feeds it (`esp_task_wdt_reset()`) for that reason: a wizard screen that waits for a touch without feeding the watchdog resets the board within seconds, and the panic looks like a random crash.

Two more guards exist for the display path, and they are not decoration:

- `drawRainOn()` calls `esp_task_wdt_reset()` every 40 columns;
- `loop()` compares the frame start time against a **3 s** budget and calls `esp_restart()` if a frame overran — this is the only recovery from a stalled DSI write on this board (a blocked write never reaches the watchdog either).

**Do not remove the frame-timeout guard** unless you have replaced it with something better.

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Build fails with bizarre compiler errors, missing `libgcc`, unknown `-m*` flags | `src/override_toolchain.py` points at a toolchain version you do not have | install ESP-IDF 5.5.x `riscv32-esp-elf`, or edit the path (§2) |
| `Directory specified in EXTRA_COMPONENT_DIRS doesn't exist` / platform will not install | PlatformIO Core too old, or you are not using the `penv` binary | use `~/.platformio/penv/bin/pio` |
| Build fails downloading `framework-arduinoespressif32-libs` | the pinned third-party lib bundle URL is unreachable | check the `platform_packages` URLs in `platformio.ini`; that project is community-maintained |
| Black screen, no rain, boots fine otherwise | PSRAM not running at 200 MHz (the prebuilt libs need it) | keep `-I src` in `build_flags` and `src/sdkconfig.h` untouched — it overrides `CONFIG_SPIRAM_SPEED` via `#include_next` |
| `Sprite ALLOC FAILED` on the serial log | PSRAM not initialised (same as above) — or `canvas.setPsram(true)` removed | see the previous row |
| WiFi scan returns 0 networks, or never connects | hosted SDIO pins not set — the defaults are wrong for this board | `hostedSetPins(12, 13, 11, 10, 9, 8, 15)` **before** `hostedInitWiFi()` (already done in `wifi_setup.cpp`) |
| Networks missing from the scan list | the ESP32-C6 hosted link is **2.4 GHz only**, so 5 GHz SSIDs simply do not appear (hidden SSIDs are scanned but may show as blank) | use a 2.4 GHz SSID |
| Image boots with the wrong partition layout / app won't fit | wrong partition table | `board_build.partitions = app3M_fat9M_16MB.csv` |
| Screen freezes after seconds–minutes, no panic, needs a power cycle | PSRAM bus contention (DSI + CPU write-back + USB DMA) | unplug USB; the 3 s frame guard will restart the device on its own in most cases — see [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md) |
| Random resets, `Task watchdog got triggered` in the log | a blocking screen that does not feed the watchdog | add `esp_task_wdt_reset()` inside the wait loop (§7) |
| `TIMEOUT 3000ms` then reboot | the frame guard fired — a frame took >3 s | usually the same bus-contention story; check for a new full-screen sprite or a second framebuffer writer |
| Blue screen and restart right after leaving the setup menu | stale sprite/glow state from pre-1.3.1 builds | v1.3.1 clears the glow buffers and re-syncs the clock on exit — update |
| Screen "residue" (old menu pixels) after leaving the menu | same as above | same as above |
| Katakana set renders as blank cells | `katakana_font.h` missing or truncated — it is generated | run `python3 tools/fontgen.py` (needs Pillow) |
| Katakana looks slightly different from the screenshots | you regenerated the font with a different TTF | expected — see [CREDITS.md](CREDITS.md) |
| Chinese SSID shows as `□` boxes | the GLCD font and the on-screen keyboard are ASCII-only | known; see the README status table |
| M5Burner imports the firmware but the board will not boot it | the image is not merged, or is QIO | re-merge with the offsets in §6 and check the flash-mode byte |
| Serial shows nothing at all | you are on the wrong port/baud, or the firmware is stuck before `M5.begin()` | 115200 on `/dev/cu.usbmodem*`; press the reset button |

---

## 9. Version strings, for bug reports

Two strings identify a build precisely. Please paste both:

1. the boot banner — `=== MATRIX RAIN vX.Y.Z (…) ===`
2. one `FPS:` line, plus whether the screen recovered by itself or needed a power cycle.

The version shown in **Setup → About** should match the banner; if it does not, you are running a stale image.
