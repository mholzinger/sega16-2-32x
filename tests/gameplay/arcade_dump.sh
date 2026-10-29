#!/bin/sh
# Add arcade work-RAM dumps to tests/gameplay/arcade for the listed TAPE frames, one MAME
# run, replaying the input tape that defines this timeline:
#   tests/gameplay/arcade_dump.sh <tape.csv> <frame> [frame ...]
#   tests/gameplay/arcade_dump.sh tools/inputs/level1_boss.csv 2400 2416 2432
#
# THE TAPE IS THE TIMELINE, not the attract's scene letters. demo-b names its dumps by
# scene because 0xFFF02A restarts at every attract step and one tick value occurs in
# several scenes; here the frame number is unique on its own, because the tape is replayed
# from a cold boot with a FRESH NVRAM directory every time -- that is what makes the run
# repeatable, and it is the same reason demo-b's driver makes one.
#
# Dumps land as aw_<frame>.bin, the same 0xFFC000 x 0x4000 window check_demo compares, so
# tests/demo-b/records.py reads them unchanged.
set -e
cd "$(dirname "$0")/../.."
TAPE="$1"; shift
[ -f "$TAPE" ] || { echo "usage: $0 <tape.csv> <frame> [frame ...]" >&2; exit 2; }
[ $# -gt 0 ] || { echo "usage: $0 <tape.csv> <frame> [frame ...]" >&2; exit 2; }
FR=$(echo "$@" | tr ' ' ',')
LAST=$(echo "$@" | tr ' ' '\n' | sort -n | tail -1)
NV=$(mktemp -d)
OUT=$(mktemp -d)
GD_IN="$TAPE" GD_FRAMES="$FR" GD_ADDR=0xFFC000 GD_LEN=0x4000 GD_OUTDIR="$OUT" \
mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none -nothrottle \
    -window -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
    -bench $(( LAST / 60 + 10 )) -nvram_directory "$NV" \
    -autoboot_script tools/gameplay_dump.lua \
    2>&1 | grep -c 'GAMEPLAY-DUMP DONE' || true
for f in "$@"; do
    [ -f "$OUT/aw_$f.bin" ] && mv "$OUT/aw_$f.bin" "tests/gameplay/arcade/aw_$f.bin"
done
ls tests/gameplay/arcade | wc -l
