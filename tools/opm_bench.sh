#!/bin/sh
# The YM2151 channel engine's SH-2 cost, on ares (instruction cycles only: a floor; the rig is
# the ceiling). Builds the sound cart with OPM_BENCH=1 (sndtest/sh/opm_bench.c runs at boot:
# two feedback-7 voices at 62.5 kHz, eight, two at 31.25 kHz, two at 15.6 kHz, 8192 samples
# each on the FRT at phi/128) into build/bench, plays it headless, and prints cycles a sample
# per configuration. With OPM_BENCH_SONG=1 (the default here) it also replays the first
# SONG_SECS of a rip's overflow channels at SONG_RATE: the in-game shape, where the channels
# rest and decay and the engine's silence skip and envelope schedule count.
#
#   tools/opm_bench.sh            -> "2ch@62.5k 569  8ch@62.5k 2022  2ch@31.25k 613  2ch@15.6k 663 cyc/sample"
#                                    "song Rise ch6,7 20s@15625: NNN cyc/sample"
#   SONG=<rip.vgz> SONG_SECS=20 SONG_RATE=15625 tools/opm_bench.sh
#
# Gate the engine first (tools/opm_identity.sh, opm_ab.py): a faster engine that drifts is
# a regression. The engine objects do not track the flags: they are rebuilt here.
set -e
cd "$(dirname "$0")/.."
W=build/bench
mkdir -p $W
AUDIO="srcref/audio/Altered_Beast_(Sega_System_16B)"
SONG=${SONG:-"$AUDIO/01 Rise From Your Grave (Round 1, 4).vgz"}
SONG_SECS=${SONG_SECS:-20}
SONG_RATE=${SONG_RATE:-15625}
SONG_CH=${SONG_CH:-6,7}
python3 tools/opm_song_bench_gen.py --vgz "$SONG" --channels $SONG_CH --secs $SONG_SECS \
    --rate $SONG_RATE --out sndtest/sh/opm_bench_song.h > /dev/null

make -s sndtest OPM_BENCH=1 OPM_BENCH_SONG=1 OPM_BENCH_DEFS="$OPM_BENCH_DEFS" ROMDIR=$W 2>&1 | grep 'sndtest rom'

A=$(~/src/marsdev/mars/sh-elf/bin/sh-elf-nm $W/sndtest.elf | grep ' _opm_bench_out$' | cut -c1-8)
~/src/bin/ares-headless-ssf --frames ${FRAMES:-1500} --gameframe 0xFFF02A --gameframe-log $W/bench.csv \
    --gameframe-watch sdram:0x$A:64 --gameframe-watch-every 50 $W/sndtest.32x > $W/ares.log 2>&1 || true
python3 - "$W/bench.csv" "$(basename "$SONG" .vgz)" $SONG_CH $SONG_SECS <<'PY'
import sys, csv
rows = [r for r in csv.reader(open(sys.argv[1])) if len(r) > 2 and r[2]]
w = rows[-1][2]
v = [int(w[i:i + 8], 16) for i in range(0, len(w) - len(w) % 8, 8)]
if len(v) < 8 or v[7] != 0x0B3C0B3C:
    sys.exit('bench not done: ' + w)
# ticks of phi/128 over N samples -> cycles a sample = ticks * 128 / N
print('2ch@62.5k %d  8ch@62.5k %d  2ch@31.25k %d  2ch@15.6k %d cyc/sample' % (
    v[0] * 128 // v[1], v[2] * 128 // v[3], v[4] * 128 // v[5], v[6] * 128 // v[5]))
if len(v) >= 13 and v[9]:
    print('song %s ch%s %ss@%d: %d cyc/sample, costliest 512-sample chunk %d' % (
        sys.argv[2][:28], sys.argv[3], sys.argv[4], v[10], v[8] * 128 // v[9], v[12] * 128 // 512))
PY
