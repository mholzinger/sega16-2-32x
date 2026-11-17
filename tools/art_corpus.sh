#!/bin/sh
# Record the art census corpus from the user's own MAME altbeast set: the arcade's sprite
# table, every frame, along five scripted rounds and the Neff tape. tools/bake_art.py ranks
# the sprite frames in it and bakes the ones the port draws most. It is the arcade's data,
# so it is never committed; this records whatever is missing and leaves the rest alone.
# Needs `mame` on PATH and mame/altbeast.zip. About 10 minutes for all six, 386 MB.
#   tools/art_corpus.sh [DIR]       default build/gate/neff/art
#   DIR/round<N>/spr.bin, log.txt   tools/arcade_round_census.lua, ROUND=N, 0..4
#   DIR/tape/spr.bin, timeline.csv  tests/gameplay/arcade_oracle.sh on
#                                   tools/inputs/arcade_neff_v1.csv, sprite dump from frame 900
# Re-recorded 2026-11-16 on MAME 0.288: all six spr.bin byte-identical to the 11-04 corpus.
set -e
cd "$(dirname "$0")/.."
[ "${GAME:-altbeast}" = altbeast ] || { echo "art_corpus: the census scripts and the tape are altbeast's, not GAME=$GAME"; exit 2; }
OUT=${1:-build/gate/neff/art}
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
todo=""
for r in 0 1 2 3 4; do [ -s "$OUT/round$r/spr.bin" ] || todo="$todo $r"; done
[ -s "$OUT/tape/spr.bin" ] || todo="$todo tape"
[ -n "$todo" ] || exit 0
command -v mame >/dev/null || { echo "art_corpus: needs mame on PATH -- the corpus is recorded from the arcade"; exit 2; }
[ -f mame/altbeast.zip ] || { echo "art_corpus: needs mame/altbeast.zip"; exit 2; }
echo "art_corpus: recording$todo into $OUT"
# ONE AT A TIME, each from its own empty nvram and cfg folders: a run that inherits another
# run's work RAM or DIP switches plays a different game (tools/sound_inputs.sh, arcade_oracle.sh).
for r in $todo; do
    if [ "$r" = tape ]; then
        mkdir -p "$OUT/tape"
        GD_SPRDUMP="$OUT/tape/spr.bin.part" GD_SPRFROM=900 \
            tests/gameplay/arcade_oracle.sh tools/inputs/arcade_neff_v1.csv "$OUT/tape" 31000 >/dev/null
        mv "$OUT/tape/spr.bin.part" "$OUT/tape/spr.bin"
    else
        mkdir -p "$OUT/round$r.part"
        NV=$(mktemp -d); CF=$(mktemp -d)
        OUT="$OUT/round$r.part" ROUND=$r CAP=90000 mame altbeast -rompath ./mame -skip_gameinfo \
            -video none -sound none -nothrottle -window -resolution 160x120 \
            -keyboardprovider none -nomouse -nojoystick -nvram_directory "$NV" -cfg_directory "$CF" \
            -bench 1600 -autoboot_script tools/arcade_round_census.lua >/dev/null 2>&1 || true
        rm -rf "$NV" "$CF"
        [ -s "$OUT/round$r.part/spr.bin" ] || { echo "art_corpus: round $r recorded nothing"; exit 2; }
        rm -rf "$OUT/round$r"; mv "$OUT/round$r.part" "$OUT/round$r"
    fi
done
for r in 0 1 2 3 4; do [ -s "$OUT/round$r/spr.bin" ] || { echo "art_corpus: round $r is missing"; exit 2; }; done
[ -s "$OUT/tape/spr.bin" ] || { echo "art_corpus: the tape dump is missing"; exit 2; }
