#!/bin/sh
# Record the sound lanes' inputs from the user's own MAME altbeast set (release, 2026-10-29).
# They are the arcade's audio, so they are never committed; this rebuilds whatever is missing
# and leaves anything present alone. Needs `mame` on PATH and mame/altbeast.zip. MAME 0.288
# reproduces the shipped banks byte for byte.
#   build/sfx/arc_<CMD>.wav     tools/arcade_wav.lua, one run per effect command
#   sndtest/speech/             tools/snd_tap.lua (200 s attract) -> tools/upd7759_decode.py,
#                               then tools/cmd_sweep.lua (every command) -> tools/soundmap_build.py
#   sndtest/sh/speech_bank8.c   tools/speech_bake.py
set -e
cd "$(dirname "$0")/.."
command -v mame >/dev/null || { echo "sound_inputs: needs mame on PATH -- the effects and speech are recorded from the arcade"; exit 2; }
[ -f mame/altbeast.zip ] || { echo "sound_inputs: needs mame/altbeast.zip"; exit 2; }
# ONE LINE: it is pasted into xargs' `sh -c` below, where a newline ends the command and
# MAME runs on with no script and no time limit (2026-10-29: six windows hung 45 min).
MAME="mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none -nothrottle -window -resolution 160x120 -keyboardprovider none -nomouse -nojoystick"
# The effect commands sfx_bake.py banks, and how long each runs after its injection (s).
SFX5="81 86 8A B4 B6 C0"
SFX6="82 83 84 85 87 88 89 8B 8C 8D 93 A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 AA AB AC B0 B3 B5 B7 B8 B9
      BA BB BC BD BE BF C1 C2 C3 C4 C5 C6 C7 C8 C9 CA CB CC CD CE CF D0 D1 D2 D4 D5 D6"
# THE GAME'S WORK RAM IS MAME NVRAM (16 KB), saved at exit and loaded at boot, so a run's
# audio depends on what ran before it: the first run of a fresh clone recorded effect 81
# differently from the shipped capture (2026-10-29). Every run here starts from its own copy
# of ONE warm state, made by a single warm-up run, and never touches the repo's nvram/.
NV=build/soundcap/nvram_warm
if [ ! -s $NV/altbeast/nvram ]; then
    mkdir -p $NV
    AW_CMD=01 AW_SECS=1 $MAME -nvram_directory $NV -seconds_to_run 20 -autoboot_script tools/arcade_wav.lua >/dev/null 2>&1
    [ -s $NV/altbeast/nvram ] || { echo "sound_inputs: the warm-up run wrote no nvram"; exit 2; }
fi
warm() { d=$(mktemp -d); cp -R $NV/. $d/; echo $d; }
export NV
mkdir -p build/sfx
todo=$( { for c in $SFX5; do echo "$c 5"; done; for c in $SFX6; do echo "$c 6"; done; } |
        while read c s; do [ -s build/sfx/arc_$c.wav ] || echo "$c $s"; done )
if [ -n "$todo" ]; then
    echo "sound_inputs: recording $(echo "$todo" | wc -l | tr -d ' ') effects"
    # ONE AT A TIME: six parallel runs came out different from the shipped captures on 3 of 7
    # effects (cause not isolated); sequential runs are byte-identical on all 63, ~2 min.
    echo "$todo" | xargs -P 1 -n 2 sh -c 'd=$(mktemp -d); cp -R $NV/. $d/; \
        AW_CMD=$0 AW_SECS=$1 '"$MAME"' -nvram_directory $d -seconds_to_run 20 \
        -wavwrite build/sfx/arc_$0.wav -autoboot_script tools/arcade_wav.lua >/dev/null 2>&1; rm -rf $d'
    for c in $SFX5 $SFX6; do
        [ -s build/sfx/arc_$c.wav ] || { echo "sound_inputs: effect $c was not recorded"; exit 2; }
    done
fi
if [ ! -s sndtest/sh/speech_bank8.c ]; then
    T=build/soundcap/speech; rm -rf $T; mkdir -p $T sndtest/speech sndtest/sh sndtest/md
    echo "sound_inputs: speech -- attract tap (200 s)"
    d=$(warm); ST_OUT=$T/tap.log $MAME -nvram_directory $d -bench 200 -autoboot_script tools/snd_tap.lua >/dev/null 2>&1
    python3 tools/upd7759_decode.py $T/tap.log sndtest/speech
    echo "sound_inputs: speech -- command sweep (about 40 min)"
    d=$(warm); ST_OUT=$T/sweep.log SB_DIR=$T $MAME -nvram_directory $d -bench 7000 -autoboot_script tools/cmd_sweep.lua >/dev/null 2>&1
    python3 tools/soundmap_build.py $T/sweep.log --speech-dir sndtest/speech --out sndtest/md/sndmap_data.h
    python3 tools/speech_order.py sndtest/speech/manifest.json
    python3 tools/speech_bake.py sndtest/speech sndtest/sh/speech_bank8.c --rate 8000
fi
