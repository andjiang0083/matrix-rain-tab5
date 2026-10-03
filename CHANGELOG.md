# Changelog

All notable changes to this project. Dates are the original build dates; version numbers follow the author's convention of small patches rather than feature milestones.

## v1.3.2 — time source fix + a real crash fix (2026-10-04)

Found on a real Tab5: the clock showed a stale time *after* a "successful" WiFi sync, the RTC held a date of 2084-08-11 from an earlier bad write, and the device rebooted behind a blue screen shortly after a sync succeeded.

### Fixed — the blue screen that followed a successful sync

- **Task-watchdog panic caused by blocking USB-CDC writes.** The flash coredump named it: `loopTask` was stuck in `cdc0_write_char()` → `prvSendAcquireGeneric()` (i.e. waiting on the CDC ring buffer) when the 8 s task watchdog fired. HWCDC's default TX timeout is 100 ms and `HWCDC::write()` allows up to 20 consecutive timeouts (~2 s), so a handful of long lines starve the watchdog whenever a host is *connected but not draining* the ring — closing a serial monitor is enough. The panic paints the screen blue and then reboots. It lands right after a sync because that is where the boot path prints its longest line. `Serial.setTxTimeoutMs(0)` makes every CDC write non-blocking: bytes are dropped instead of waited for.
- **WiFi setup no longer hijacks the boot.** `runWifiSetup()` used to run whenever `autoConnectAndSync()` returned false, which conflated "no credentials saved" with "the network or the sync failed" — so one WiFi hiccup left the device sitting in the wizard with the clock never starting. With credentials present, a failed sync now boots the clock anyway (seeded from the validated RTC), and the wizard stays reachable from `SET → WiFi Setup`.
- **The first SNTP query is no longer sent before DHCP is up.** `esp_wifi_sta_get_ap_info() == ESP_OK` means *associated*, not *reachable*; a query sent with no DNS resolution is silently lost, which reads on the panel as "NTP is broken". The sync now waits for a lease and re-arms once (`sntp_restart()`) halfway through its window.
- **The clock path no longer touches NVS.** The timezone offset is read once into RAM instead of opening `Preferences` on every refresh; on the P4 flash and PSRAM share the MSPI path and the DSI scan-out is already marginal, so the flash traffic is worth removing.

### Fixed — the time source

- **`getLocalTime()` was being used as the "NTP has synced" signal, and it is not one.** It only reports that the system clock *looks* plausible — and `M5.begin()` seeds that clock from the hardware RTC (`RTC_Class::setSystemTimeFromRtc()`), so immediately after `configTime()` it returns true carrying the *pre-sync* value. The boot path therefore raced the NTP response: it confirmed the old time on screen and wrote it back into the RTC, which is why the clock still showed it afterwards. The SNTP notification callback plus `sntp_get_sync_status()` is now the signal, with a 20 s bound.
- **The clock face no longer reads the RTC.** `updateClockFromNTP()` derives HH:MM from the system clock (UTC epoch) plus the saved offset. The RX8130CE is only a fallback *seed*, and only after it passes range validation and a "is it actually ticking" check (two reads 1.1 s apart), so a stopped or garbage RTC cannot get through.
- **RTC writes are verified.** `M5.Rtc.setDateTime()` returns `void`, so a write that silently failed looked exactly like a good one; the value is now read back and compared (±2 s, allowing the seconds register to tick across the write).
- **The display is gated on a trusted source.** Nothing is shown until either a real NTP sync happened this boot or the RTC passed validation; otherwise the clock shows `00:00` rather than a plausible-looking wrong time.
- **The confirmation screen mixed UTC and local.** It printed the UTC *date* with the local *time*, which is a day out between 00:00 and 08:00 at UTC+8 (i.e. right now).
- Clock value refresh: 60 s → 5 s (it is now a division rather than an I2C read, so the minute flips punctually).
- New one-line boot diagnostic: `[TIME] ntp=... utc=... rtc=on write=ok` — reports the NTP result and whether the RTC write landed.
- Removed `src/timesync.h`, which declared `g_epoch` / `g_epochBase` / `g_timeValid` / `syncNTP()` — symbols that never existed anywhere in the build.

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
