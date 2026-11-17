#!/bin/sh
# Build the sound cart with the SH-2 synth lane (hybrid 3): channels 6/7 synthesized on the
# secondary SH-2 from the rips' register writes, the Z80 stream without them. Output is
# build/lane/sndtest.32x, never rom/. The music tables are regenerated (render_music_lane.h,
# opm_music.h, both ignored) and every object the flags touch is removed before and after,
# because the objects do not track the flags.
#
#   tools/opm_lane_build.sh                       # the lane cart
#   OPM_LANE_DEFS='-DOPM_LANE_LEAD=800' tools/opm_lane_build.sh    # a probe
#   OPM_LANE_RATE=16002 tools/opm_lane_build.sh   # the lane clock (tools/opm_stage3.sh measures it)
set -e
cd "$(dirname "$0")/.."
W=${ROMDIR:-build/lane}
mkdir -p $W
OBJS="sndtest/md/md_main.o sndtest/sh/sound.o sndtest/sh/s_main.o sndtest/sh/opm_lane.o sndtest/opm/opm.o"
make -s sndtest-music-lane OPM_LANE_RATE=${OPM_LANE_RATE:-16002.3} OPM_LANE_ROWSCALE=${OPM_LANE_ROWSCALE:-1.00028} 2>&1 | grep -v "^  0x"

make -s sndtest OPM_LANE=1 OPM_LANE_DEFS="$OPM_LANE_DEFS" ROMDIR=$W 2>&1 | grep 'sndtest rom'

