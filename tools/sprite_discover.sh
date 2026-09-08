#!/bin/sh
# LOOP 17 JOB 1 — sprite-frame discovery coverage run.
#   tools/sprite_discover.sh [outdir]
#
# Two arcade passes, both against MAME altbeast (the oracle, not the
# port): attract (65s, the cutscene/logo/demo frames) and a scripted
# play pass (coin+start+walk+mash, the combat frames attract never
# shows). Each writes its own CSV; sprite_discover.py merges them and
# prints the bake budget.
#
# Coverage is additive: re-run with more passes (or drop Mike's own
# recordings in as extra CSVs) and merge the lot. A frame discovered
# late is not a bug -- it just stays on the live decoder.
set -e
DIR=${1:-discover}
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)

rm -rf nvram/altbeast   # NVRAM-credits trap: credited attract skips scenes

echo "[1/2] attract pass (3900 frames)"
DISC_OUT="$DIR/attract.csv" DISC_FRAMES=3900 \
    mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
    -nothrottle -autoboot_script tools/sprite_discover.lua 2>&1 | \
    grep sprite_discover || true

echo "[2/2] play pass (7200 frames, scripted)"
DISC_OUT="$DIR/play.csv" DISC_FRAMES=7200 DISC_PLAY=1 \
    mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
    -nothrottle -autoboot_script tools/sprite_discover.lua 2>&1 | \
    grep sprite_discover || true

python3 tools/sprite_discover.py "$DIR"
