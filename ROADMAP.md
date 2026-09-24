# Roadmap

**English** · [中文](ROADMAP_CN.md)

Roughly ordered by value. Every item is sized so a newcomer with a Tab5 can pick it up. **Comment on the issue (or open one) before starting**, so work does not collide.

## 1. Display path — do the same thing with fewer bytes

The 30 fps cap is a **deliberate ceiling**, not a measured limit: we capped it because more drawing per second means more CPU write-back into the PSRAM that the DSI is simultaneously DMA-reading, and that is the regime that used to wedge the panel (see [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md)). So the interesting work here is making each frame *cheaper*, not more frequent:

- [ ] **Skip columns whose head has not moved a whole cell.** Today every column redraws its whole trail each frame. Publish the before/after drawn-fps and a 10-minute freeze check.
- [ ] **Re-measure the `drawChar` vs `fillRect` trade-off** for the faintest tail cells (the original measurement: `fillRect` ≈ 2–3× cheaper). If it wins, ship it behind a runtime switch, default off.
- [ ] **Write up the DSI stall mechanism properly** — ESP32-P4 TRM (MIPI-DSI bridge, PSRAM controller, AXI interconnect) plus `esp_lcd_panel_dpi.c`'s DMA link-list. The community has no clear answer for "how many concurrent PSRAM writers can this SoC take", and this project has a reproducible-ish case.
- [ ] **Only then** consider raising the fps cap, behind a switch, with numbers.

## 2. Use the hardware this build ignores

- [ ] **Audio spectrum (ES8388 + ES7210).** The StickS3 and Cardputer builds have a 40-band bars visualiser; on Tab5 nothing is wired. Needs I²S + codec init on the P4 path. Interesting design constraint: it must not become permanent on-screen clutter (this family of clocks treats meters as events, not furniture).
- [ ] **BMI270 shake → change character set.** Same part as the StickS3, where `updateIMU()`/`checkShake()` already work; the port is trivial. Also useful for a "pick up the clock" wake.
- [ ] **INA226 battery gauge.** Read it, and decide honestly where it goes: this project's rule is that functional info should be event-driven rather than a permanent HUD — a low-battery warning that fades in is more in keeping than a permanent percentage.
- [ ] **RX8130CE alarm interrupt** → wake from sleep on the minute/second (pairs well with §3).

## 3. Power

- [ ] **Light sleep** with GPIO (IMU interrupt) / RTC wake. Today "idle" only dims, drops to 8 fps and turns the backlight off — the CPU never sleeps, so idle current is nothing like the hardware's potential.
- [ ] **Measure and publish idle current** using the INA226, at each idle level. Numbers, not adjectives.

## 4. Provisioning and UX

- [ ] **Non-ASCII SSIDs.** Today a Chinese SSID renders as `□`. Two halves: (a) a small CJK bitmap font (same technique as the katakana set — `tools/fontgen.py` already generates 16×16 1 bpp glyphs), and (b) non-ASCII **password** entry on the on-screen keyboard.
- [ ] **Named timezones with DST rules** instead of a raw UTC-offset grid. Store an offset + a DST rule (or a TZ string and let SNTP apply it).
- [ ] **Manual time entry** as a last-resort fallback: today, no usable access point on first boot means no clock at all.
- [ ] **Swipe gestures** as a shortcut layer: vertical swipe = brightness, so changing it does not require the setup menu.
- [ ] Optional: **second hand / date / seconds style** variants; keep it a choice, not a permanent addition.

## 5. Fonts and licence hygiene

- [ ] **Regenerate `katakana_font.h` from an OFL font** (Noto Sans JP, M PLUS, …) so downstream redistribution is unambiguous — see [CREDITS.md](CREDITS.md). This changes the glyph shapes slightly, so it needs a side-by-side render; the author is picky about how the katakana look at 16×16.
- [ ] **Make `tools/fontgen.py` take a `--font` argument** with a documented OFL default; it currently hard-codes a macOS system font path.

## 6. Build, packaging and CI

- [ ] **GitHub Actions build check** using the pioarduino platform with a pinned toolchain — prove every PR compiles from a clean tree, and attach the merged image as an artifact.
- [ ] **Auto-detect the RISC-V toolchain** in `src/override_toolchain.py` instead of the hard-coded `esp-14.2.0_20260121` path (this is the single most common build failure for newcomers).
- [ ] **`tools/snap.py`** — a serial capture helper that handles the firmware's 921600 baud switch and writes a BMP; then wire it into the docs so contributors can attach screenshots to PRs.
- [ ] **Tagged releases** that attach the merged `.bin`, the M5Burner `.zip` and a `sha256sums.txt`.

## 7. Docs

- [ ] Windows and Linux build walkthroughs (the current one is macOS-flavoured).
- [x] Screenshots in the README — first real-device photo added. A short capture (screen in motion) is still wanted: nothing sells a clock like seeing it run.
- [ ] Architecture notes: the glow-buffer/PAL design, the drawing budget, why there is no sprite in the hot path.
- [ ] More translations.

## Explicitly out of scope

- **Any asset from *The Matrix*.** No film stills, logos, or the "digital rain" trademark artwork — this stays an unaffiliated fan project with no Warner Bros. material.
- **A second framebuffer writer "because it should be fine".** Deeper queues and triple buffering are the classic answer on other boards and the wrong answer on this one (see the notes); anything like it needs measurements and a runtime switch first.
- **Adding a debug-console workflow.** Visual verification is a project convention, and on this SoC a chatty serial session is an aggravating factor for the display path.
