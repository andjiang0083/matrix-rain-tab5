# Changelog

All notable changes to this project. Dates are the original build dates; version numbers follow the author's convention of small patches rather than feature milestones.

## v1.3.1 — first public release (2026-07-14, public repo published 2026-09-24)

The first public snapshot. Everything below was developed on a real Tab5 and is what the attached firmware contains.

### Rain and clock

- **80-column rain engine** on the 1280×720 panel: 16 px cells, trails up to 18 glyphs, per-column depth (0.6–1.5×) driving both fall speed (140 px/s × depth) and trail length (`2 + depth × 11`).
- **Six character sets** — `FULL`, `NUM`, `HEX`, `BIN`, `ALPHA`, `KATAKANA` — cycled by tapping the screen.
- **Katakana set** as a 91-glyph 16×16 1 bpp bitmap font, drawn with horizontal run-length fills; no CJK font engine is linked in.
- **7-segment ghost-glow clock**: glyphs that overlap a lit segment are pushed 4 px inwards from that segment's edge and brightened/hue-shifted (the rain visibly refracts around the digits), and are then written into an 80×45 glow buffer that decays one level per frame (~8.5 s of fading ghosts).
- **FIFO glow**: only the head of a column refreshes glow, so the digits fill from the bottom up rather than uniformly.
- **Band-swept clearing**: each column clears exactly the strip it swept between frames, which is what fixed the original "tails are never erased, the render load grows until the DSI locks up" failure.

### Provisioning, time and power

- **Touch WiFi wizard**: scan (32 APs) → SSID list → on-screen keyboard (full + numeric/symbol pages, shift) → UTC-offset picker → connect → save to NVS.
- **Boot NTP sync** (`pool.ntp.org`, `time.google.com`) with a **blocking confirmation screen** showing live seconds; the clock does not start until the user taps *Confirm*.
- **Hardware RTC**: UTC is written to the RX8130CE, and `updateClockFromNTP()` re-reads it every 60 s, applying the stored timezone offset — no network needed after first boot.
- **WiFi is disconnected after the initial sync**, and the hosted stack is left initialised so a re-run of the wizard does not re-init SDIO.
- **Backlight brightness** via 12-bit LEDC PWM on GPIO22, 20–100 % in 10 % steps, persisted in NVS.
- **Setup menu** (tap `SET`): WiFi, character set, brightness, About.
- **Idle state machine**: 2 min → 25 % brightness, 3 min → 8 fps, 10 min → backlight off; any touch restores.
- **Resilience**: 8 s task watchdog with panic enabled, watchdog feeds inside the rain loop and every blocking UI loop, and a **3 s frame-timeout guard** that reboots the board if a frame overruns (the only recovery from a stalled DSI write on this hardware).

### Fixed in 1.3.1

- Blue screen + reboot when returning from the setup menu (stale sprite/glow state combined with the frame timeout).
- Screen residue (old menu pixels) after leaving the setup menu.
- Non-ASCII SSIDs rendered as blank space instead of visible `□` placeholders.

### Development history of the display path — including what was reverted

Kept here because it is the most useful part of the record for anyone touching the drawing code:

| Version | Change | Result |
|---|---|---|
| 1.0.0 | 160 columns, 20-glyph trails, GLCD 8 px | ✅ stable, 37–48 fps |
| 1.0.1 | 80 columns, 8-glyph trails, switched to the Font2 BMP font | ❌ froze after ~30 s |
| 1.0.2 – 1.0.4 | Column grouping, shorter trails, removed background fills, removed the `startWrite` transaction | ❌ still froze |
| 1.0.5 | Back to the GLCD engine with a true 2× `setTextSize` | ❌ still froze |
| 1.0.6 | **Double buffering** (two framebuffers, buffer flip from an `on_refresh_done` callback) | ❌ **Guru Meditation on the first frames — reverted** |
| 1.0.7 | Back to single-buffered direct drawing, 30 fps cap, whole-trail redraw | ✅ stable (with USB detached), 30 fps |
| 1.2.1 | NTP confirm screen, timezone picker, RTC persistence | ✅ |
| 1.3.0 | Unified green-on-black palette, step indicators, `□` SSID placeholders | ✅ |
| 1.3.1 | Brightness, setup menu, the fixes above | ✅ current |

The two conclusions that still shape the code:

1. **`drawChar(code, x, y, N)`'s fourth argument is a *font index*, not a scale factor.** A stray `2` silently switches to the 16 px BMP font, a completely different rasteriser with a row-major write pattern — the change that started the whole freeze investigation.
2. **The definitive cause of the freezes is PSRAM bus contention**, not a software bug: DSI scan-out DMA-reads ~110 MB/s from PSRAM while the CPU writes the same memory, and a USB-CDC session adds a third consumer. Buffering tricks that add *another* writer make it worse, which is why double buffering crashed and why the shipped design is deliberately single-writer and cheap. Full write-up: [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md).

### Release assets

The image attached to this release was rebuilt from this repository's tree (Flash 35.8 %, RAM 10.4 %). It differs from the author's earlier private 1.3.1 build only in the About screen showing the public repository URL.
