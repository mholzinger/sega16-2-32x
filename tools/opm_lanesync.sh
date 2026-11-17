#!/bin/sh
# The offset between the sound cart's two music lanes, on ares: the YM2612 lane (Z80 player)
# and the SH-2 synth lane (PWM). tools/opm_syncseq.py puts the same click on a channel of
# each lane at the same instants for a minute; this builds that as the lane cart's command
# 0x94, plays it headless recording both streams, finds every click in each, and prints the
# lane-minus-YM2612 offset: first, last, mean, spread, and the drift per minute.
#
#   tools/opm_lanesync.sh                 -> build/lanesync/{cap.wav.*.wav, offsets.txt}
#   OPM_LANE_DEFS='-DOPM_LANE_LEAD=400' tools/opm_lanesync.sh    # a candidate lead
set -e
cd "$(dirname "$0")/.."
W=build/lanesync
mkdir -p $W
python3 tools/opm_syncseq.py --secs ${SYNC_SECS:-58} --out $W/opm_syncseq
python3 tools/build_vgm_music.py --map 0x94=opm_syncseq.vgz --audio $W --drop-psg \
    --out sndtest/md/render_music_lane.h | tail -1
python3 tools/opm_music_gen.py --map 0x94=opm_syncseq.vgz --audio $W --channels 6 \
    --rate ${OPM_LANE_RATE:-16002.3} --row-scale ${OPM_LANE_ROWSCALE:-1.00028} --out sndtest/sh/opm_music.h
OBJS="sndtest/md/md_main.o sndtest/sh/sound.o sndtest/sh/s_main.o sndtest/sh/opm_lane.o sndtest/opm/opm.o"

make -s sndtest OPM_LANE=1 OPM_LANE_DEFS="$OPM_LANE_DEFS" ROMDIR=$W 2>&1 | grep 'sndtest rom'

printf '300,a,1\n306,a,0\n' > $W/press_a.csv
~/src/bin/ares-headless-audio --frames $(( (${SYNC_SECS:-58} + 7) * 60 )) --input $W/press_a.csv --audio-wav $W/cap.wav \
    --audio-from 290 $W/sndtest.32x > $W/ares.log 2>&1
python3 - $W <<'PY' | tee $W/offsets.txt
import sys, json, wave
import numpy as np
W = sys.argv[1]
man = json.load(open(W + '/opm_syncseq.json'))
clicks = man['clicks']

def onsets(path):
    w = wave.open(path)
    n, r, ch = w.getnframes(), w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, ch)[:, 0].astype(float)
    env = np.convolve(np.abs(x), np.ones(24) / 24, mode='same')
    thr = env.max() * 0.25
    out, i = [], 0
    while i < len(env):
        if env[i] > thr:
            out.append(i / r)
            i += int(0.3 * r)
        else:
            i += 1
    return out

ym = onsets(W + '/cap.wav.YM2612.wav')
pw = onsets(W + '/cap.wav.PWM.wav')
n = min(len(ym), len(pw), len(clicks))
print('clicks: sequence %d, YM2612 lane %d, PWM lane %d' % (len(clicks), len(ym), len(pw)))
if n < 2:
    sys.exit('too few clicks found')
d = np.array(pw[:n]) - np.array(ym[:n])
t = np.array(clicks[:n])
k, b = np.polyfit(t, d, 1)
print('YM2612 lane starts %.4f s after the sequence time; PWM lane %.4f s' % (ym[0] - clicks[0], pw[0] - clicks[0]))
print('PWM minus YM2612: first %+.1f ms, last %+.1f ms, mean %+.1f ms, spread %.1f ms (p-p), drift %+.1f ms/min'
      % (d[0] * 1e3, d[-1] * 1e3, d.mean() * 1e3, (d.max() - d.min()) * 1e3, k * 60e3))
print('per click (ms): ' + ' '.join('%+.0f' % (v * 1e3) for v in d))
PY
