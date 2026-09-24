# Contributing

**English** · [中文](CONTRIBUTING_CN.md)

Thanks for considering it. This is a single-board project maintained by someone who owns the hardware, so the most useful contributions are the ones that can be **verified on a real Tab5**.

## Ways to help

- **Pick something from [ROADMAP.md](ROADMAP.md)** and comment on the issue (or open one) saying you are taking it, so nobody duplicates work.
- **Fix a bug** — with the evidence described below.
- **Improve the docs** — a build walkthrough for Windows, screenshots, translations.
- **Use the hardware this build ignores** — the ES8388 audio codec, the BMI270 IMU, the INA226 battery monitor, the RX8130CE interrupt line. All present, all unwired here.

## House rules (they come from real failures, not taste)

1. **One visible change per flash.** Flash it, look at the screen, then continue. Two changes per flash make a regression unattributable.
2. **Verify visually, not through a serial debug loop.** USB-CDC traffic competes with the display path for PSRAM bandwidth on this SoC and can be the thing that wedges it (see [BUILDING.md §4](BUILDING.md)). Log the boot banner, then unplug and look at the panel.
3. **The hot path stays single-writer and cheap.** Anything that adds a second framebuffer writer, a full-screen sprite in `loop()`, or a big per-frame allocation must ship **behind a runtime switch** (an NVS flag or a file), defaulting to the current behaviour — a bad experiment has to be rollback-able without a reflash. On this board, an overly eager drawing path does not just drop frames; it can lock the DSI up until the frame guard reboots you.
4. **Never remove the frame-timeout guard or the watchdog feeds** unless you replace them with something better. See [BUILDING.md §7](BUILDING.md).
5. **Start from a known-good baseline; prefer small targeted changes over rewrites.** A verifiable 10 % improvement beats a rewrite that might be 40 %.

## Building and testing

See [BUILDING.md](BUILDING.md). Before you open a PR:

- [ ] It builds **from a clean tree** (`rm -rf .pio && pio run`), not just incrementally.
- [ ] The device boots, the clock renders, and `FPS:` sits at ~30.
- [ ] The rain does not leave residue: watch at least one full column-reset cycle (about a minute) and check that swept bands were cleared.
- [ ] Leaving the setup menu (all four sub-screens) returns to a clean clock.
- [ ] If you touched the display path: 10+ minutes with **and without** USB attached, no freeze.
- [ ] If you touched anything risky: it is behind a runtime switch, and you documented the switch.

## Reporting a bug

Use the issue form and include:

- **What you saw** — screen behaviour, and the boot banner line.
- **A `FPS:` sample** — does it still print, or did the serial go quiet before the freeze?
- **Whether it recovered** — did it reboot itself, or did you have to cut power? That one detail separates a software panic from a hardware-level stall, and they have completely different causes.
- **Whether USB was attached** — it is a known aggravating factor, not a neutral observation.

## Commit and PR style

- Conventional-commit prefixes (`feat:`, `fix:`, `docs:`, `perf:`, `refactor:`, `chore:`) are appreciated.
- English or 中文 are both fine — use whichever expresses the change more precisely.
- Explain *why* in the body, especially for anything touching the drawing path, timing, or PSRAM traffic.
- Keep the version in `src/main.cpp` (the boot banner), the About screen and [CHANGELOG.md](CHANGELOG.md) in sync.

## Code style

Match the surrounding code: 2-space indent, `g_`-prefixed globals, `SCREAMING_CASE` constants in `config.h`, `static` for anything file-local. Green-on-black UI uses the `MG::` palette in `src/matrix_gui.h` — do not introduce new colours; that palette is deliberately monochrome-green.

Add a one-line comment explaining the board constraint behind any workaround, e.g. "Tab5: hosted link pins differ from the library defaults". The next person needs to know which lines are portable and which are board-specific.
