#!/bin/zsh
# rigclock.sh <rom> [name] [shots] [spacing_s] : push a PORTRIGCLK=1 rom to the office rig, capture
# every <spacing_s> (default 20) <shots> times (default 16), name each shot by its wall second, then
# fit vints/s and game ticks/s with tools/rig_clock.py. ~6 minutes. Output: build/accept/rig/clock_<name>/
R=$1; N=${2:-$(basename $R .32x)}; K=${3:-16}; S=${4:-20}
D=${ACCEPT_OUT:-$(dirname $0)/../build/accept}/rig/clock_$N; mkdir -p $D; M=root@mister.office.local
cd "$(dirname "$0")/.."
if ! ssh -o ConnectTimeout=6 -o BatchMode=yes $M true 2>/dev/null; then
  echo "rigclock: rig unreachable (home LAN is 10.0.4.x): strip skipped"; exit 2
fi
tools/mister_push.sh --name mister.office.local $R 2>&1 | tail -1
T0=$(date +%s)
sleep 30   # past the boot and the title: the tape starts the game itself
for k in $(seq 1 $K); do
  ssh -o ConnectTimeout=8 $M 'echo screenshot > /dev/MiSTer_cmd'; T=$(( $(date +%s) - T0 )); sleep 2
  F=$(ssh -o ConnectTimeout=8 $M 'ls -t /media/fat/screenshots/S32X/*.png | head -1')
  scp -q $M:"$F" $D/$(printf '%02d' $k)_${T}s.png
  sleep $(( S - 2 ))
done
python3 tools/rig_clock.py $D | tee $D/clock.txt
