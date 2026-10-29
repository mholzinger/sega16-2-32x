#!/bin/sh
# Add arcade work-RAM dumps to tests/demo-b/arcade for the listed frames (one MAME run):
#   tests/demo-b/arcade_dump.sh <scene> <frame> [frame ...]
#   tests/demo-b/arcade_dump.sh A 580 581 582
# THE SCENE IS PART OF THE NAME (aw_<scene>_<frame>.bin) because 0xFFF02A restarts at each
# attract step, so the same tick value occurs in several scenes -- 0xB4 in demo B and in
# the post-demo attract, 0x2BA in demo A's tail and the high-score step. The scene letters
# and their frame ranges are SCENES in tests/demo-b/check_demo.py, which refuses a dump
# whose frame falls outside the scene it is named for.
# MAME altbeast with a FRESH NVRAM directory (the attract sequence depends on it), the
# headless flags CLAUDE.md requires, tools/wram_dump.lua at 0xFFC000 x 0x4000.
set -e
cd "$(dirname "$0")/../.."
SC="$1"; shift
case "$SC" in
  [A-Z]) ;;
  *) echo "usage: $0 <scene letter> <frame> [frame ...]" >&2; exit 2 ;;
esac
FR=$(echo "$@" | tr ' ' ',')
LAST=$(echo "$@" | tr ' ' '\n' | sort -n | tail -1)
NV=$(mktemp -d)
OUT=$(mktemp -d)
WD_FRAMES="$FR" WD_ADDR=0xFFC000 WD_LEN=0x4000 WD_OUTDIR="$OUT" \
mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none -nothrottle \
    -window -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
    -bench $(( LAST / 60 + 5 )) -nvram_directory "$NV" -autoboot_script tools/wram_dump.lua \
    2>&1 | grep -c 'WRAM-DUMP DONE' || true
for f in "$@"; do
    [ -f "$OUT/aw_$f.bin" ] && mv "$OUT/aw_$f.bin" "tests/demo-b/arcade/aw_${SC}_$f.bin"
done
rm -rf "$NV" "$OUT"
