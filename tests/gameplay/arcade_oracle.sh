#!/bin/sh
# Record the ARCADE's timeline along an input tape: the gameplay oracle's arcade half.
#   tests/gameplay/arcade_oracle.sh <tape.csv> <outdir> [frames]
#   tests/gameplay/arcade_oracle.sh tools/inputs/level1_full.csv tests/gameplay/arcade_level1_full 36000
#
# One headless MAME run of altbeast from a cold boot with a FRESH NVRAM directory and a FRESH
# cfg directory (MAME rewrites cfg/<game>.cfg with the live DIPs at every exit, so a run in
# the repo cwd inherits the previous run's switches; measured 2026-10-22, see
# gameplay_dump.lua), replaying the tape through tools/gameplay_dump.lua with
# every recorder on:
#   <outdir>/timeline.csv   one row per frame, the TIMELINE CSV FORMAT (gameplay_dump.lua)
#   <outdir>/census.csv     active object slots every 60 frames
#   <outdir>/snap_<f>.png   a screenshot every 300 frames
#   <outdir>/ev_<f>_*.bin   work RAM + video RAM at every level/round/release/P1-state/boss
#                           change and at the final frame
#   <outdir>/aw_<f>.bin     the work-RAM window at the final frame
#   <outdir>/mame.log       MAME's own output
# GD_GOD=1 holds P1's lives at 3 while in play so the tape cannot game over (written after
# the row, so a death still shows as one row below 3). GD_DSW1 / GD_DSW2 (hex, default
# 66/54, what the current port boots with: PORT_CORO seeds the arcade's own values in
# sh_src/port_l2.c) pass through to the lua and set the arcade's DIPs; parity_compare
# section 0 refuses a corpus whose DIP latch 0xFFF01A/1B differs from the port's:
#   tests/gameplay/arcade_oracle.sh tools/inputs/level1_full.csv tests/gameplay/arcade_level1_full
#
# frames defaults to the last frame on the tape plus 60; -bench is frames/60 + 20 seconds,
# the hang backstop CLAUDE.md requires. The outdir is Sega-derived data and is gitignored
# (tests/gameplay/arcade*/).
set -e
cd "$(dirname "$0")/../.."
TAPE="$1"; OUT="$2"; FRAMES="$3"
[ -f "$TAPE" ] && [ -n "$OUT" ] || { echo "usage: $0 <tape.csv> <outdir> [frames]" >&2; exit 2; }
if [ -z "$FRAMES" ]; then
    FRAMES=$(( $(grep -v '^#' "$TAPE" | cut -d, -f1 | sort -n | tail -1) + 60 ))
fi
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
mkdir -p "$OUT"
NV=$(mktemp -d); CF=$(mktemp -d)
echo "arcade_oracle: tape $TAPE frames $FRAMES out $OUT dips ${GD_DSW1:-66}/${GD_DSW2:-54}"
GD_IN="$TAPE" GD_FRAMES="$FRAMES" GD_END="$FRAMES" GD_ADDR=0xFFC000 GD_LEN=0x4000 \
GD_OUTDIR="$OUT" GD_GOD=1 GD_TIMELINE="$OUT/timeline.csv" GD_CENSUS="$OUT/census.csv" \
GD_CENSUS_EVERY="${GD_CENSUS_EVERY:-60}" GD_SNAP_EVERY="${GD_SNAP_EVERY:-300}" GD_ONCHANGE=1 \
mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none -nothrottle \
    -window -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
    -bench $(( FRAMES / 60 + 20 )) -nvram_directory "$NV" -cfg_directory "$CF" -snapshot_directory "$OUT" \
    -autoboot_script tools/gameplay_dump.lua > "$OUT/mame.log" 2>&1 || true
rm -rf "$NV" "$CF"
grep 'GAMEPLAY-DUMP DIPS' "$OUT/mame.log" || echo "arcade_oracle: no DIPS line" >&2
grep 'GAMEPLAY-DUMP TAPE\|GAMEPLAY-DUMP GOD' "$OUT/mame.log"
grep -c 'GAMEPLAY-DUMP EVENT' "$OUT/mame.log" | sed 's/^/events: /'
grep 'GAMEPLAY-DUMP END' "$OUT/mame.log" || echo "arcade_oracle: no END line, see $OUT/mame.log" >&2
wc -l "$OUT/timeline.csv" "$OUT/census.csv"
