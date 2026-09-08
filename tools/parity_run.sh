#!/bin/sh
# One parity-loop measurement cycle: capture both machines scene-anchored,
# then print the scoreboard. Run from the repo root.
#   tools/parity_run.sh [capdir] [game] [rom]   (game: altbeast | altbeastj)
set -e
DIR=${1:-parity}
GAME=${2:-altbeast}
if [ "$GAME" = altbeast ]; then ROM=${3:-rom/s16.32x}; else ROM=${3:-rom/s16_$GAME.32x}; fi   # 3rd arg: rom override
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)   # MAME resolves relative snapshot paths against
                          # its own snap dir — absolute paths only
rm -f "$DIR"/*_arc.png "$DIR"/*_ours.png "$DIR"/*_diff.png
rm -rf nvram/$GAME   # NVRAM-credits trap: credited attract skips scenes

PC_DIR="$DIR" PC_TAG=arc mame $GAME -rompath ./mame -skip_gameinfo \
    -video none -sound none -nothrottle -window -resolution 160x120 -keyboardprovider none -bench 200 \
    -autoboot_script tools/parity_cap.lua >/dev/null 2>&1 || true

PC_DIR="$DIR" PC_TAG=ours mame 32x -cart $ROM -rompath ./mame \
    -skip_gameinfo -video none -sound none -nothrottle -window \
    -resolution 160x120 -keyboardprovider none -bench 200 \
    -autoboot_script tools/parity_cap.lua >/dev/null 2>&1 || true

python3 tools/parity_diff.py "$DIR"
