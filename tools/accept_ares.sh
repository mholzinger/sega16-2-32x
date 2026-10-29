#!/bin/zsh
# gates.sh <rom>  -> soak_<n>.txt (65k frames by default, past the 2026-10-24 stack-leak hang at ~55,400; ACCEPT_SOAK=<frames> overrides; it flags ticks that stop tracking vints) parity_<n>.txt temporal_<n>.txt in this dir
cd /Users/mikeholzinger/src/sega16-2-32x/.claude/worktrees/advisor-bisect
R=$1; N=$(basename $R .32x); D=${ACCEPT_OUT:-$(dirname $0)/../build/accept}; mkdir -p $D; A=~/src/ares-debug/build_macos/headless-ui/Release/ares-headless
( $A --frames ${ACCEPT_SOAK:-65001} --dump "32X SDRAM:0x28E40:0x200:$D/soak_$N.bin" $R >/dev/null 2>&1; python3 -c "
import struct;d=open('$D/soak_$N.bin','rb').read();D=struct.unpack('>128I',d[:512]);print('$N %dk soak: vints'%round(D[0]/1000),D[0],'flips',D[1],'flips/vint %.3f'%(D[1]/D[0]),'windows',D[7],'passes',D[9],'ticks',D[11],'TICKS TRACK' if abs(D[11]-D[0])<200 else 'TICKS STOPPED: HANG (ticks %d vs vints %d)'%(D[11],D[0]))" > $D/soak_$N.txt ) &
( python3 tools/attract_parity.py $R --port > $D/parity_$N.txt 2>&1 ) &
( python3 tools/temporal_metric.py $R --start 600 --n 60 --quiet > $D/temporal_$N.txt 2>&1; python3 tools/temporal_metric.py $R --start 1700 --n 60 --quiet >> $D/temporal_$N.txt 2>&1 ) &
( for F in 1200 3700; do python3 tools/artaudit.py $R $F | head -1; done > $D/artaudit_$N.txt 2>&1 ) &
wait
echo done > $D/gates_$N.done
