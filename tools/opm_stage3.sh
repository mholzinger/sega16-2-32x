#!/bin/sh
# Stage 3 of the OPM measurement chain: the sound cart's SH-2 SYNTH LANE, on ares, against jt51.
#
#   tools/opm_stage3.sh [CHANNEL]   -> build/opm_compare/stage3/{table.txt,compare.json,compare.svg}
#
# Builds the test sequence (tools/opm_testseq.py) on CHANNEL (default 6: a lane channel), lays
# it into a lane cart as command 0x94 (the cart's boot default: one A press plays it) with
# tools/opm_lane_build.sh into build/testseq_lane, plays it headless on ares recording each
# stream, and runs tools/opm_compare.py on the PWM stream against jt51's render of the same
# sequence. Stage 2 (tools/opm_stage2.sh) is the same for the YM2612 lane; between them they
# say what each of the eight channels sounds like through the cart.
#
# The sequence's sync clicks also give the lane's start offset against the trigger and its
# clock against the Z80's: the numbers OPM_LANE_LEAD and OPM_LANE_RATE are set from.
set -e
cd "$(dirname "$0")/.."
CH=${1:-6}
W=build/testseq_lane
mkdir -p $W build/opm_compare/stage3
python3 tools/opm_testseq.py --channel $CH --out $W/opm_testseq
python3 tools/opm_ab.py --vgz $W/opm_testseq.vgz --channels $CH --rates 0 --secs 62 --out build/opm_ab/testseq_lane | grep NATIVE
# the lane cart, with the sequence as the only song (0x94); the shipped tables are untouched
# because the lane build writes its own generated files
python3 tools/build_vgm_music.py --map 0x94=opm_testseq.vgz --audio $W --drop-psg \
    --out sndtest/md/render_music_lane.h | tail -1
python3 tools/opm_music_gen.py --map 0x94=opm_testseq.vgz --audio $W --channels $CH \
    --rate ${OPM_LANE_RATE:-16002.3} --row-scale ${OPM_LANE_ROWSCALE:-1.00028} --out sndtest/sh/opm_music.h
OBJS="sndtest/md/md_main.o sndtest/sh/sound.o sndtest/sh/s_main.o sndtest/sh/opm_lane.o sndtest/opm/opm.o"

make -s sndtest OPM_LANE=1 OPM_LANE_DEFS="$OPM_LANE_DEFS" ROMDIR=$W 2>&1 | grep 'sndtest rom'

printf '300,a,1\n306,a,0\n' > $W/press_a.csv
# 290 frames of boot, A at 300, 57.7 s of sequence: 3900 frames with margin
~/src/bin/ares-headless-audio --frames 3900 --input $W/press_a.csv --audio-wav $W/cap.wav \
    --audio-from 290 $W/sndtest.32x 2>&1 | grep '"PWM"'
python3 tools/opm_compare.py build/opm_ab/testseq_lane/jt51.s16 $W/cap.wav.PWM.wav \
    --manifest $W/opm_testseq.json --labels jt51,cart-pwm --band-max 7000 --out build/opm_compare/stage3 \
    > build/opm_compare/stage3/table.txt || true
head -2 build/opm_compare/stage3/table.txt
grep flagged build/opm_compare/stage3/table.txt || true
echo "table: build/opm_compare/stage3/table.txt"
