# M5Burner packaging · M5Burner 打包

**English** · [中文](#中文)

## English

`matrix-rain.json` is the M5Burner catalogue entry for this firmware. A bundle the app can import consists of three files, zipped together or handed over as the `.json` alone:

```
matrix-rain.json                 ← this catalogue entry
matrix-rain-tab5-vX.Y.Z.bin      ← merged flash image (bootloader @0x2000 + partitions + app, DIO)
README.md                        ← short first-boot instructions
```

Import: M5Burner → **Custom** → **Import Custom FW** → pick the `.zip` (or the `.json`) → select **Matrix Rain Clock** → **Burn**.

The `.bin` must be a **merged** image written from offset `0x0`. `pio run` produces it as `.pio/build/tab5/firmware.factory.bin`; see [BUILDING.md](../BUILDING.md) §6 for the merge offsets and the verification snippet (the bootloader's flash-mode byte must be `0x02`, i.e. DIO).

The image attached to the GitHub release was built from this repository, so the `.bin` here and the release asset are byte-identical — verify with the `sha256` published in the release notes.

## 中文

`matrix-rain.json` 是这份固件在 M5Burner 里的目录条目。可导入的打包 = 三个文件放一起 zip（也可以只给 `.json`）：

```
matrix-rain.json                 ← 本目录条目
matrix-rain-tab5-vX.Y.Z.bin      ← 合并烧录镜像（bootloader @0x2000 + 分区表 + 应用，DIO）
README.md                        ← 首启简短说明
```

导入：M5Burner → **Custom** → **Import Custom FW** → 选 `.zip`（或 `.json`）→ 选中 **Matrix Rain Clock** → **Burn**。

`.bin` 必须是**合并镜像**，从 `0x0` 开始写入。`pio run` 会把它产出为 `.pio/build/tab5/firmware.factory.bin`；合并偏移和校验脚本见 [BUILDING_CN.md](../BUILDING_CN.md) 第 6 节（bootloader 的 flash 模式字节必须是 `0x02`，即 DIO）。

GitHub Release 上附的镜像就是从本仓库构建的，所以这里的 `.bin` 和 release asset 逐字节一致——可用发布说明里的 `sha256` 校验。
