## What this changes · 这个 PR 改了什么

<!-- One paragraph. Explain *why*, not just what. / 一段话说清楚"为什么"，不只是"改了什么"。 -->

Closes #

## How it was verified · 怎么验证的

<!-- Describe what you saw on the real device. "It compiles" is not verification. -->
<!-- 描述你在真机上看到了什么。"能编过"不算验证。 -->

- Build: `rm -rf .pio && pio run -e tab5` → clean tree, no warnings added
- Device: flashed and ran for ___ minutes
- USB: tested with the cable attached / detached (both?)
- Observed: ___ (FPS, freeze or no freeze, residue or no residue)

## Checklist · 检查清单

- [ ] Built from a **clean tree**, not incrementally · 在**干净树**里构建过，不是增量
- [ ] Verified on real hardware · 在真机上验证过
- [ ] One visible change per flash (this PR is a single change) · 一次刷机只改一个可见的东西（本 PR 只含一个改动）
- [ ] If the display path or PSRAM traffic changed: measurements included and it is **behind a runtime switch**, default off · 若动了绘制通路/PSRAM 流量：附上实测数据，且**放在运行时开关后面**、默认关闭
- [ ] The frame-timeout guard and the watchdog feeds are intact · 帧超时保护和看门狗喂狗没有被删
- [ ] Docs updated if behaviour changed (README status table, CHANGELOG, version string) · 行为有变时同步了文档（README 现状表、CHANGELOG、版本号）
- [ ] No film assets, ROMs, or proprietary fonts added · 没有引入电影素材、ROM 或专有字体

## Notes for the reviewer · 给评审者的说明

<!-- Anything you are unsure about, any dead end you hit, any number that surprised you. -->
<!-- 不确定的地方、走过的死路、让你意外的数字——都写在这里。 -->
