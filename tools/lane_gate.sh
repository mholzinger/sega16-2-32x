#!/bin/sh
# The in-game music gate (2026-10-31): replay the level-1 tape on a rom under the mapper-aware
# ares, recording both audio streams, the synth lane's counters, every SH-2 write into the
# lane's state and every COMM write, then print one screen:
#   - the YM2612 and PWM levels per 2 s (the music must start when the level does)
#   - the lane's counters at the end (starts, landing, stops, samples)
#   - master writes into the lane's state (a stack landing there = a master exception, which
#     is how lane1 died: md_sound cleared the 68K's boot beacon)
#   - the sound commands the port posted (COMM10 0x5Axx) and the 68K's countdowns (COMM14)
#
#   tools/lane_gate.sh build/port/rom/lane2.32x [FRAMES]      -> build/gate/<name>/
set -e
cd "$(dirname "$0")/.."
ROM=$1; N=$(basename $ROM .32x); F=${2:-1500}
D=build/gate/$N; mkdir -p $D
NM=~/src/marsdev/mars/sh-elf/bin/sh-elf-nm
ELF=${ROM%.32x}.elf
A=$($NM $ELF | grep ' _opm_lane_census$' | cut -c1-8 || true)
SC=$($NM $ELF | grep ' _speech_census$' | cut -c1-8 || true)
W=""
if [ -n "$A" ]; then
    E=$(printf %08x $((0x$A + 0x34)))
    W="--gameframe 0xFFF02A --gameframe-log $D/w.csv --gameframe-watch sdram:0x$A:32 --gameframe-watch-every 100 --watch-sdram 0x$A:0x$E:100:$F --watch-sdram-out $D/wr.csv"
fi
~/src/bin/ares-headless-ssf --frames $F --input tools/inputs/level1_full.csv --pad1 fighting \
    --audio-wav $D/cap.wav --audio-from 0 $W --trace-comm $D/comm.csv $ROM 2>&1 | grep -v unusual > $D/ares.log || true
if [ -n "$SC" ]; then    # the mixer's census at the end: [2] = sides that played without their fill (low 16) / real (high 16)
    ~/src/bin/ares-headless-ssf --frames $F --input tools/inputs/level1_full.csv --pad1 fighting \
        --gameframe 0xFFF02A --gameframe-log $D/sc.csv --gameframe-watch sdram:0x$SC:32 --gameframe-watch-every 300 $ROM > /dev/null 2>&1 || true
    python3 - $D <<'PY'
import sys, csv
rows=[r for r in csv.reader(open(sys.argv[1] + '/sc.csv')) if len(r)>2 and r[2] and r[0].isdigit()]
if rows:
    w=rows[-1][2]; v=[int(w[i*8:i*8+8],16) for i in range(4)]
    print('mixer @%s: sides %d fills %d UNDERRUNS %d (real %d) plays %d' % (rows[-1][0], v[0], v[1], v[2] & 0xffff, v[2] >> 16, v[3]))
PY
fi
python3 - $D "$A" <<'PY'
import sys, os, csv, wave, collections
import numpy as np
D, A = sys.argv[1], sys.argv[2]
for s in ('YM2612', 'PWM'):
    p = f'{D}/cap.wav.{s}.wav'
    if not os.path.exists(p):
        print(s, 'NO CAPTURE'); continue
    w = wave.open(p); n, r, ch = w.getnframes(), w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, ch)[:, 0].astype(float)
    print('%-7s dBFS/2s:' % s, ' '.join('%d:%.0f' % (t, 20 * np.log10(np.sqrt((x[t * r:(t + 2) * r] ** 2).mean()) / 32768 + 1e-9))
                                        for t in range(0, int(n / r), 2)))
if A:
    rows = [r for r in csv.reader(open(f'{D}/w.csv')) if len(r) > 2 and r[2] and r[0].isdigit()]
    if rows:
        w = rows[-1][2]; v = [int(w[i * 8:i * 8 + 8], 16) for i in range(8)]
        p0 = v[1] if v[1] < 2 ** 31 else v[1] - 2 ** 32
        print('lane census @%s: starts %d landing %d stops %d samples %d cmd %02x left %d ahead %d' % (rows[-1][0], v[0], p0, v[2], v[3], v[4], v[5], v[6]))
    wr = list(csv.reader(open(f'{D}/wr.csv')))[1:]
    m = collections.Counter((int(x[0]) // 100 * 100, x[3]) for x in wr if x[1] == 'shm')
    print('MASTER writes into the lane state:', 'none' if not m else m.most_common(5))
cm = [x for x in csv.reader(open(f'{D}/comm.csv')) if len(x) > 3 and x[0].isdigit()]
snd = [(x[0], x[3]) for x in cm if x[2] == '5' and x[1] == 'shm' and x[3].startswith('0x5a')]
c14 = [(x[0], x[3]) for x in cm if x[2] == '7' and x[1] == 'm68k' and int(x[0]) > 20]
print('sound commands posted (frame, COMM10):', snd[:12])
print('68K COMM14 after boot (frame, value):', c14[:12])
PY
