#!/usr/bin/env python3
"""A two-lane sync sequence: the same click on two YM2151 channels at the same instants. KIT.

One channel goes through the sound cart's YM2612 lane (the Z80 player), the other through
the SH-2 synth lane (PWM); the captures of both streams then give the offset between the
lanes at every click and how it moves over a minute: the number OPM_LANE_LEAD is set from,
and whether the Z80's catch-up clock and the PWM clock hold together (tools/opm_lanesync.sh).

  tools/opm_syncseq.py --ym 0 --lane 6 --every 1.0 --secs 58 --out build/lanesync/opm_syncseq
"""
import argparse
import gzip
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from opm_testseq import Seq, to_vgm, KEY, A4, RATE, CLOCK     # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ym', type=int, default=0, help='channel on the YM2612 lane')
    ap.add_argument('--lane', type=int, default=6, help='channel on the SH-2 synth lane')
    ap.add_argument('--every', type=float, default=1.0)
    ap.add_argument('--secs', type=float, default=58.0)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'lanesync', 'opm_syncseq'))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    ev = []
    clicks = []
    for ch in (a.ym, a.lane):
        s = Seq(ch)
        if ch == a.ym:
            s.w(0x0F, 0); s.w(0x18, 0); s.w(0x19, 0); s.w(0x19, 0x80); s.w(0x14, 0)
        s.quiet()
        s.con(7); s.op(0, mul=2); s.op(1, tl=127); s.op(2, tl=127); s.op(3, tl=127)
        s.note(A4)
        t = 0.5
        while t < a.secs:
            s.t = t
            s.op(0, mul=2)                     # play() closes its window with TL 127: re-arm the carrier
            s.play('click', 'sync', None, KEY[0], 0.05, 0.15)
            if ch == a.ym:
                clicks.append(round(t, 4))
            t += a.every
        ev += s.ev
    open(a.out + '.vgz', 'wb').write(gzip.compress(to_vgm(ev)))
    json.dump({'ym': a.ym, 'lane': a.lane, 'clock': CLOCK, 'rate': RATE, 'clicks': clicks,
               'seconds': a.secs + 1.0}, open(a.out + '.json', 'w'), indent=1)
    print('%s.vgz: %d clicks on channels %d and %d, every %.2f s' % (a.out, len(clicks), a.ym, a.lane, a.every))


if __name__ == '__main__':
    main()
