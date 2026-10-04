#!/bin/sh
# ---------------------------------------------------------------------------
# make_release.sh — one command from a built tree to a publishable image.
#
#   sh tools/make_release.sh 1.3.4
#
# The checks are the point, not decoration. M5Burner always writes the file it
# is given at flash offset 0x0, so an app-only image (or a merge with the wrong
# bootloader offset / flash mode) flashes fine over USB and then bricks the
# bootloader: black screen, no boot, no error. Both failures have happened in
# this family of projects. So this script refuses to emit an artefact that does
# not pass the structural check, and it diffs the bootloader + partition table
# region against the previous release — those bytes must not move between
# versions.
#
# Produces, under .release/:
#   matrix-rain-tab5-v<ver>.bin    merged image (bootloader @0x2000, DIO)
#   matrix-rain-tab5-v<ver>.zip    M5Burner Custom-FW bundle (json + bin + readme)
#   sha256sums.txt                 checksums for both
# ---------------------------------------------------------------------------
set -eu

VER="${1:?usage: sh tools/make_release.sh <version, e.g. 1.3.4>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.release"
BIN="$OUT/matrix-rain-tab5-v$VER.bin"
ZIP="$OUT/matrix-rain-tab5-v$VER.zip"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

[ -f "$ROOT/VERSION" ] || { echo "no VERSION file in $ROOT" >&2; exit 1; }
[ "$(cat "$ROOT/VERSION")" = "$VER" ] || {
  echo "VERSION says $(cat "$ROOT/VERSION"), asked for $VER — bump VERSION first" >&2; exit 1; }
[ -x "$PIO" ] || { echo "platformio not found at $PIO (set PIO=...)" >&2; exit 1; }

cd "$ROOT"
"$PIO" run -e tab5

mkdir -p "$OUT"
cp .pio/build/tab5/firmware.factory.bin "$BIN"

python3 - "$BIN" <<'PY'
import sys
p = sys.argv[1]
d = open(p, 'rb').read()
assert d[0x2000] == 0xE9,          "no bootloader image at 0x2000 — wrong merge offsets"
assert d[0x8000:0x8002] == b'\xaa\x50', "no partition table at 0x8000"
assert d[0x10000] == 0xE9,         "no application image at 0x10000"
assert len(d) < 2 * 1024 * 1024,   "image is %d bytes — padded to the whole flash?" % len(d)
mode = d[0x2002]
print("%s\n  %d bytes, bootloader flash_mode = 0x%02x %s" % (
      p.split('/')[-1], len(d), mode, "(DIO ok)" if mode == 0x02 else "(MUST be 0x02 for M5Burner)"))
assert mode == 0x02, "flash mode is not DIO"
PY

PREV=$(ls "$OUT"/matrix-rain-tab5-v*.bin 2>/dev/null | grep -v "v$VER.bin" | sort -V | tail -1 || true)
if [ -n "$PREV" ]; then
  python3 - "$PREV" "$BIN" <<'PY'
import sys
old = open(sys.argv[1], 'rb').read()
new = open(sys.argv[2], 'rb').read()
same = old[:0x10000] == new[:0x10000]
print("  bootloader + partition table + boot_app0 vs %s: %s" % (
      sys.argv[1].split('/')[-1], "identical" if same else "DIFFERENT — investigate before publishing"))
PY
fi

rm -f "$ZIP"
(cd "$ROOT" && zip -q -j "$ZIP" m5burner/matrix-rain.json "$BIN" m5burner/README.md)

(cd "$OUT" && shasum -a 256 "matrix-rain-tab5-v$VER.bin" "matrix-rain-tab5-v$VER.zip" > sha256sums.txt)
echo "--- $OUT/sha256sums.txt ---"
cat "$OUT/sha256sums.txt"
