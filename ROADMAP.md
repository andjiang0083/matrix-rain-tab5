# Roadmap

**English** · [中文](ROADMAP_CN.md)

This file is the **context**: why each area is worth working on, what the constraint is, and what a change has to prove before it ships. It is deliberately *not* a second task list — **the issue tracker is the only list**. Every open item here is an issue; each section links to its filtered view. **Comment on the issue before starting**, so work does not collide.

## 1. Display path — do the same thing with fewer bytes

The 30 fps cap is a **deliberate ceiling**, not a measured limit: we capped it because more drawing per second means more CPU write-back into the PSRAM that the DSI is simultaneously DMA-reading, and that is the regime that used to wedge the panel (see [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md)). So the interesting work here is making each frame *cheaper*, not more frequent — and only afterwards, with numbers, is raising the cap worth discussing.

**Open items →** [`area/display`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fdisplay)

## 2. Use the hardware this build ignores

Four parts are on the board and unused by this build: the ES8388/ES7210 audio path, the BMI270 IMU, the INA226 battery gauge and the RX8130CE alarm interrupt line. The constraint that applies to all four is this family's convention: meters and status readouts are **events, not furniture** — a low-battery warning that fades in beats a permanent percentage.

**Open items →** [`area/hardware`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fhardware)

## 3. Power

"Idle" today only dims (2 min → 25 %), drops to 8 fps (3 min) and kills the backlight (10 min) — **unless a USB-C cable is attached, in which case the whole chain is suspended** (v1.3.5: the charger's status line through the IO expander *or* the pack not being drained, so a full pack — or no pack fitted at all — still counts as mains). The CPU never sleeps, so idle current is nothing like the hardware's potential. Numbers, not adjectives — and measuring them at all needs the INA226 (§2) to be readable first.

**Open items →** [`area/power`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fpower)

## 4. Provisioning and UX

The first-boot path is the rough edge: a non-ASCII SSID renders as `□`, the timezone list is a raw UTC-offset grid, and with no usable access point there is no clock at all. Anything added here stays a choice in the setup menu or a gesture — not new permanent furniture.

**Open items →** [`area/ux`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fux)

## 5. Fonts and licence hygiene

`src/katakana_font.h` is generated from a macOS system font path, which makes downstream redistribution murky. Regenerating it from an OFL font is the fix; the glyphs change slightly, so it needs a side-by-side render — the author is picky about how katakana look at 16×16.

**Open items →** [`area/fonts`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Ffonts)

## 6. Build, packaging and CI

**Shipped:** the GitHub Actions build check (`.github/workflows/build.yml` — clean-tree compile plus the release pre-flight on the merged image, artifact attached); RISC-V toolchain auto-detection in `src/override_toolchain.py` (the single most common newcomer failure); `tools/snap.py` serial capture, documented in BUILDING.md §5 — a real-device capture still stalls part-way, so treat it as unfinished until the tracker says otherwise.

**Open items →** [`area/build`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fbuild)

## 7. Docs

**Shipped:** a real-device photo and a **GIF of the screen in motion** in the README — both taken from a phone clip of the flashed build, cropped to the panel and stripped of the keyboard, hand and desk.

**Open items →** [`area/docs`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fdocs)

## Explicitly out of scope

- **Any asset from *The Matrix*.** No film stills, logos, or the "digital rain" trademark artwork — this stays an unaffiliated fan project with no Warner Bros. material.
- **A second framebuffer writer "because it should be fine".** Deeper queues and triple buffering are the classic answer on other boards and the wrong answer on this one (see the notes); anything like it needs measurements and a runtime switch first.
- **Adding a debug-console workflow.** Visual verification is a project convention, and on this SoC a chatty serial session is an aggravating factor for the display path.
