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
# the build auto-detects any installed 14.x riscv32-esp-elf under ~/.espressif/tools/

# option B — just install the tools, no IDF
python3 $IDF_PATH/tools/idf_tools.py install riscv32-esp-elf
```

**Step 2 explains what the build picks and why — worth a read, no editing needed.**

---

## 2. The toolchain override (auto-detected — you do not edit it)

`src/override_toolchain.py` is wired into the build via `extra_scripts`. It auto-detects your installed RISC-V toolchain: it scans `~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin` and **prefers the newest GCC 14.x** — that is what the prebuilt Arduino libs (`idf-release_v5.5` bundle) were compiled with, and what supports the `xesppie` extension this SoC needs.

- If no 14.x is installed, it prints a `WARNING` and falls back to the newest version you have — that build may fail with compiler errors that look nothing like a PATH problem (unknown `-m` options, `cc1plus: error`, missing `libgcc`). Install an ESP-IDF 5.5.x toolchain instead (§1).
- If nothing is installed at all, the build stops with an error that tells you exactly what to install.
- The build always prints `Toolchain path overridden: <path>` — check that line if the compiler misbehaves.

You no longer need to edit this file. (It used to hard-code `esp-14.2.0_20260121` — the author's machine — which was the single most common newcomer build failure.)

---

## 3. Build

```bash
git clone https://github.com/andjiang0083/matrix-rain-tab5.git
cd matrix-rain-tab5

~/.platformio/penv/bin/pio run -e tab5
# very first run: downloads the platform, the Arduino core, the library bundle and
# the RISC-V toolchain (~1.5 GB) — a couple of minutes
# every clean rebuild after that (even with `rm -rf .pio`): a few seconds
```

A good build ends like this (values from the current tree):

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
=== MATRIX RAIN v1.3.3 (Boot NTP + RTC + confirm screen + WiFi-setup) ===
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

1. open the monitor (115200), press `S`;
2. the firmware prints `SNAP...`, renders, switches to 921600 and waits ~30 ms;
3. it prints `BMP:<bytes>`, streams a 24-bit BMP, then `\nEND\n`;
4. it switches back to 115200.

The host side has to survive a baud-rate change — and note that this capture path is **not finished work**: on a real device the pixel stream currently stalls part-way (tracked under `area/build` in the issue tracker; treat `tools/snap.py` as a starting point, not a working screenshot path). What the host has to do, and what `snap.py` implements: consume the device's `SNAP...` line at 115200, switch to 921600 during the silent render window, skip any device log line that lands before the header (the firmware can emit one there), then capture by byte count until `END`:

```bash
python3 tools/snap.py                     # auto-detects the port, writes matrix-rain-<timestamp>.bmp
python3 tools/snap.py /dev/cu.usbmodem1101 -o snap.bmp
```

Keep the session short (§4): grab one frame, unplug.

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

### Upgrading a device that already runs this firmware — keep the WiFi credentials

**Do not flash a merged image at `0x0` to upgrade.** Every merged image spans the
NVS partition at `0x9000` (the gap between `0x8000` and `0xe000` is filled with
`0xFF`), so it erases the saved WiFi credentials and the device boots into the
setup wizard. The bootloader and partition table do not change between releases,
so write the **app partition only**:

```bash
python3 -m esptool --chip esp32p4 -p /dev/cu.usbmodem101 write-flash \
  0x10000 .pio/build/tab5/firmware.bin --flash-mode dio --flash-freq 80m --flash-size 16MB
```

Flash a merged image at `0x0` only for a first install, or when the device is
running something else entirely (e.g. retro-go) — that path starts from a clean
NVS on purpose.

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
| Build fails with bizarre compiler errors, missing `libgcc`, unknown `-m*` flags | the auto-detected toolchain is not a GCC 14.x (check the `Toolchain path overridden:` line) | install ESP-IDF 5.5.x `riscv32-esp-elf` (§1) |
| `*** missing SConscript .../framework-arduinoespressif32-libs/esp32p4_es/pioarduino-build.py` | the platform's board definition sets `chip_variant = esp32p4_es` (ES silicon), but the pinned libs package only ships `esp32p4/` | keep `board_build.chip_variant = esp32p4` in `platformio.ini` (it is set — do not remove it) |
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
| Panel still dims/blanks after 2–10 min **with the cable in** | the idle override did not engage: `Setup → About` says `Power battery …` | check the boot line `[POWER] USB-C det: pin=… attached=…` — "attached" needs the charger's status line (IO expander #2, P6) high **or** the INA226 pack current to say the pack is not draining; a clock running off its battery reads a large negative mA, a cable reads ≈0 however full the pack is. See `readUsbDetect()` in `src/main.cpp` |
| Panel never dims **with no cable attached** | the detector reads "attached" — the status pin floating high, or the INA226 current not clearly negative (running off the pack, the system load must show up as a large discharge) | same boot line: on battery expect a large negative mA. The pull-down is set in `initUsbDetect()`, and `USB_DET_*` in `src/config.h` hold the pin, threshold and poll constants |
| Plugged in, but the pack never charges (charge level only falls) | charging was never enabled: M5Unified's expander bring-up reads the chip ID only and writes no direction or output register, so `CHG_EN` (expander #2 P7) was never driven | v1.3.5 calls `initCharging()` right after `M5.begin()` — P5 (QC) first, 50 ms, then P7 high. Check the boot line `[POWER] charging: P7(CHG_EN)=1 P5(QC_EN)=0 out=0x89`: if `out` bit 7 is 0 the write did not land. `digitalWrite()` touches only `OUT_SET`, so a direction must be set first |
| Blue screen and restart right after leaving the setup menu | stale sprite/glow state from pre-1.3.1 builds | v1.3.1 clears the glow buffers and re-syncs the clock on exit — update |
| Screen "residue" (old menu pixels) after leaving the menu | same as above | same as above |
| Katakana set renders as blank cells | `katakana_font.h` missing or truncated — it is generated | run `python3 tools/fontgen.py > src/katakana_font.h` (needs Pillow); on a machine that has the source TTF this reproduces the committed header byte-for-byte |
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
