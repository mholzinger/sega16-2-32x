#!/bin/sh
# Capture a rig run and census it for BLACK CELLS in the backstop's band.
#   tools/rig_blackcells.sh <rom> <name> <nshots> <gap-seconds> <outdir>
#
# Mike's black tiles are confined to view rows 24-27 (measured over his own
# captures, 2026-09-27: 14/13/12/14 cells in rows 24/25/26/27 and NOTHING above
# row 24), which is exactly the bottom-band backstop's domain. ares does not
# reproduce them -- 40 clean control frames on the level-1 bot route -- so the rig
# is the instrument.
#
# Hazards this respects (LESSONS "MiSTer rig hazards"): distinct rom names so the
# launch API cannot no-op on an already-running path; the launch proven in
# /tmp/remote.log rather than ACTIVEGAME; and the rig's OWN rom-named screenshots
# pulled afterwards rather than trusting a sampler's read count.
set -e
cd "$(dirname "$0")/.."
ROM=$1; NAME=$2; N=${3:-8}; GAP=${4:-6}; OUT=$5
# THERE IS NO DEFAULT RIG, ON PURPOSE (2026-09-27). mister.office.local is the
# DECOMPILE thread's and mister.tv.local is the BUILDER's. A default sends every
# un-flagged invocation at one of them, which is how the wrong rig got a rom
# more than once -- and the push that misfires is invisible from here, because
# the launch API answers "game started" whichever machine it was. Naming the rig
# is one word; guessing it wrong costs somebody else's session. So: name it.
if [ -z "$MISTER" ]; then
    echo "$0: name the rig -- there is no default." >&2
    echo "  MISTER=root@mister.tv.local      the BUILDER's rig" >&2
    echo "  MISTER=root@mister.office.local  the DECOMPILE thread's rig" >&2
    exit 2
fi
M="$MISTER"
SHOTS=/media/fat/screenshots/S32X
mkdir -p "$OUT"
ssh -o ConnectTimeout=8 "$M" "rm -f $SHOTS/*$NAME* 2>/dev/null; mkdir -p $SHOTS" || true
tools/mister_push.sh "$ROM" "$NAME"
i=0
while [ "$i" -lt "$N" ]; do
  sleep "$GAP"
  ssh -o ConnectTimeout=8 "$M" 'echo screenshot > /dev/MiSTer_cmd' || true
  i=$((i+1))
done
sleep 2
echo "pulling the rig's own rom-named shots"
scp -q "$M:$SHOTS/*$(basename "$NAME" .32x)*" "$OUT/" 2>/dev/null || \
  scp -q "$M:$SHOTS/*" "$OUT/" 2>/dev/null || echo "  (no shots pulled)"
ls -l "$OUT" | tail -n +2 | awk '{print "  "$9"  "$5}'
