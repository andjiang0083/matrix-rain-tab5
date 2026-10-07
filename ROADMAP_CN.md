# 路线图

[English](ROADMAP.md) · **中文**

本文件是**背景**：每个方向为什么值得做、约束是什么、一个改动要证明到什么程度才算完成。它刻意**不是第二份任务清单**——**唯一的清单是 issue tracker**。这里每个开放项都是一个 issue，每节都链到它对应的筛选视图。**动手前先在 issue 下留言**，避免撞车。

## 1. 显示通路 —— 用更少的字节做同样的事

30fps 是**刻意设的天花板**，不是量出来的极限：我们压住它，是因为每秒画得越多，CPU 往 PSRAM 的回写就越多，而 DSI 同时在 DMA 读同一片 PSRAM——这正是过去会把面板弄挂的那个区间（见 [docs/PORTING-NOTES.md](docs/PORTING-NOTES.md)）。所以这里真正有意思的方向是让**每帧更便宜**，而不是更频繁；只有在拿到数据之后，提高上限才值得讨论。

**开放项 →** [`area/display`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fdisplay)

## 2. 把这个版本没用的硬件用起来

板上有四样东西本项目还没用：ES8388/ES7210 音频通路、BMI270 IMU、INA226 电量计、RX8130CE 闹钟中断线。对四者都成立的约束来自这一系列项目的约定：仪表和状态读数应当是**事件，不是家具**——淡入一次的低电量提醒，比常驻一个百分比更合适。

**开放项 →** [`area/hardware`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fhardware)

## 3. 省电

现在的"闲置"只是调暗（2 分钟后 25%）、降到 8fps（3 分钟）、关背光（10 分钟）；CPU 从未睡眠，所以闲置电流离硬件潜力差得远。要数字，不要形容词——而要想量出数字，得先让 INA226（§2）能读。

**开放项 →** [`area/power`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fpower)

## 4. 配网与交互

首次开机的路径是最粗糙的地方：非 ASCII 的 SSID 渲染成 `□`、时区列表是裸的 UTC 偏移网格、而没有可用热点时干脆没有钟。这里新增的东西一律做成设置菜单里的选项或一个手势——不要再加常驻元素。

**开放项 →** [`area/ux`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fux)

## 5. 字库与许可卫生

`src/katakana_font.h` 现在是从一个 macOS 系统字体路径生成的，下游再分发会有歧义。改用 OFL 字体重新生成即是解法；字形会有细微变化，需要并排渲染对比——作者对 16×16 下片假名的观感很挑。

**开放项 →** [`area/fonts`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Ffonts)

## 6. 构建、打包与 CI

**已交付：** GitHub Actions 构建检查（`.github/workflows/build.yml`——干净树编译 + 对合并镜像跑发布前检查，并附 artifact）；`src/override_toolchain.py` 的 RISC-V 工具链自动探测（新人最常撞的失败）；`tools/snap.py` 串口截图，见 BUILDING_CN.md §5——**真机抓帧仍会中途断流，在 tracker 结清之前请按"未完成"看待**。

**开放项 →** [`area/build`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fbuild)

## 7. 文档

**已交付：** README 里的第一张真机实拍。还缺一段**屏幕在动**的短视频——没有什么比"看见它在跑"更能说明问题。

**开放项 →** [`area/docs`](https://github.com/andjiang0083/matrix-rain-tab5/issues?q=is%3Aissue+is%3Aopen+label%3Aarea%2Fdocs)

## 明确不做

- **任何《骇客帝国》的素材。** 不用电影剧照、logo 或"数字雨"商标图形——这保持为一个无关联的同人项目，不含华纳兄弟的任何素材。
- **"应该没事"就再加一个帧缓冲写入者。** 更深的队列 + 三缓冲在别的板子上是标准答案，在这块板子上是错的（见移植笔记）；类似做法必须先有实测 + 运行时开关。
- **引入调试控制台工作流。** 用眼睛验证是本项目的约定；而在这颗 SoC 上，话多的串口会话是显示通路的加重因素。
