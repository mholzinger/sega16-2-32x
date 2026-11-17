#!/bin/sh
# Stage 2 of the OPM measurement chain: the sound cart's YM2612 lane, on ares, against jt51.
#
#   tools/opm_stage2.sh [CHANNEL]      -> build/opm_compare/stage2/{table.txt,compare.json,compare.svg}
#                                         build/opm_compare/stage2_jt12/... (the same stream through
#                                         jt12, the MiSTer's YM2612 RTL, instead of ares)
#
# Builds the test sequence (tools/opm_testseq.py) on CHANNEL (default 0), transcodes it into a
# sound cart as command 0x94 (the cart's boot default: one A press plays it), builds that cart
# into build/testseq (never rom/), plays it headless on ares recording each chip's stream, and
# runs tools/opm_compare.py on the YM2612 stream against jt51's render of the same sequence.
# The shipped sndtest/md/render_music.h is backed up and restored, and the objects it was
# compiled into are removed, so the next `make sndtest` is clean.
#
# The jt12 pass (tools/jt12_run.py) measures the transcode on the FPGA's YM2612 with no
# emulator, player or output filter in the way: where ares and jt12 agree, the finding is the
# transcode's or the chip's; where only ares differs, it is ares (measured 2026-10-31: they
# agree on every tonal single-operator test; ares' 2840 Hz low-pass is the only difference
# above 2 kHz, and jt12 reads 1.1 dB lower overall).
#
# WHY (2026-10-31, mdfourier 3). Findings on the first run: the cart's YM2612 is a flat 3.0 dB
# under the arcade on every single-operator test with tone within 0.0 dB; the Mega Drive
# low-pass shows as a trend over MUL; multi-carrier patches clip in the YM2612's 9-bit channel
# output (algorithm 7 at -7 dB); DT2 cannot be transcoded; the transcoder drops the first click.
set -e
cd "$(dirname "$0")/.."
CH=${1:-0}
W=build/testseq
BAK=$W/render_music_shipped.h
mkdir -p $W build/opm_compare/stage2
python3 tools/opm_testseq.py --channel $CH --out $W/opm_testseq
python3 tools/opm_ab.py --vgz $W/opm_testseq.vgz --channels $CH --rates 0 --secs 62 --out build/opm_ab/testseq | grep NATIVE
cp sndtest/md/render_music.h $BAK
restore() { cp $BAK sndtest/md/render_music.h; rm -f sndtest/md/md_main.o; }
trap restore EXIT
python3 tools/build_vgm_music.py --map 0x94=opm_testseq.vgz --audio $W --out sndtest/md/render_music.h | tail -1
grep -q 'RENDER_MUSIC_COUNT 1$' sndtest/md/render_music.h
rm -f sndtest/md/md_main.o
make -s sndtest ROMDIR=$W | grep 'sndtest rom'
printf '300,a,1\n306,a,0\n' > $W/press_a.csv
# 290 frames of boot, A at 300, 57.7 s of sequence: 3900 frames with margin
~/src/bin/ares-headless-audio --frames 3900 --input $W/press_a.csv --audio-wav $W/cap.wav \
    --audio-from 290 $W/sndtest.32x 2>&1 | grep 'YM2612'
python3 tools/opm_compare.py build/opm_ab/testseq/jt51.s16 $W/cap.wav.YM2612.wav \
    --manifest $W/opm_testseq.json --labels jt51,cart-ym2612 --out build/opm_compare/stage2 \
    > build/opm_compare/stage2/table.txt || true
head -2 build/opm_compare/stage2/table.txt
grep flagged build/opm_compare/stage2/table.txt
echo "table: build/opm_compare/stage2/table.txt"
mkdir -p build/opm_compare/stage2_jt12
python3 - <<'PY'
import sys, wave
sys.path.insert(0, 'tools')
import build_vgm_music as B
from vgm2ym import load
from jt12_run import jt12_render, RATE
seg, _, _ = load('build/testseq/opm_testseq.vgz')
stream, _ = B.transcode_split(seg, None)
x = jt12_render(stream, 60.0)
w = wave.open('build/testseq/jt12_seq.wav', 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(round(RATE)))
w.writeframes(x.astype('<i2').tobytes()); w.close()
PY
python3 tools/opm_compare.py build/opm_ab/testseq/jt51.s16 $W/jt12_seq.wav \
    --manifest $W/opm_testseq.json --labels jt51,jt12-ym2612 --out build/opm_compare/stage2_jt12 \
    > build/opm_compare/stage2_jt12/table.txt || true
head -1 build/opm_compare/stage2_jt12/table.txt
echo "table: build/opm_compare/stage2_jt12/table.txt"
