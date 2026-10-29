#!/usr/bin/env python3
"""Generate the level-1 gameplay input tape, tools/inputs/level1_full.csv.

The tape is the timeline of the gameplay oracle: the arcade (tools/gameplay_dump.lua in
MAME) and the port (ares-headless --input) both replay it from a cold boot, so a frame
number on the tape is the same moment on both machines. The format is the ares csv,
one transition per line, `frame,button,value`, 6-button pad names (y = coin, start,
right, a = punch, b = kick, c = jump). Only EDGES are written: a button line appears
when its state changes, never while it is held.

The cadence is the one tools/arcade_level_run.lua reached level 2 with on 2026-10-21,
frozen into a tape so that the port can replay it:

  coins   y down for 12 frames at 500, 540, 580, 620, 660, 700 (six credits)
  start   901..940 held
  walk    from WALK_FROM (1500): right held, except every 4th 400-frame block
          ((frame // 400) % 4 == 3) where it is released and the hero stands and fights;
          every 16 frames: a (punch) frames 0-3, b (kick) frames 8-11
  boss    from BOSS_FROM (default 28000): right held throughout; every 40 frames:
          c (jump) frames 0-3, b (kick) frames 8-11, a (punch) frames 24-27

The lua script switched to the boss pattern on scroll 0xFFF132 >= 0x18A0; a tape cannot
read memory, so BOSS_FROM is a frame and must sit after the walk reaches the boss gate.
MEASURED 2026-10-22 on the arcade (tests/gameplay/arcade_oracle.sh, DIPs ff/fd, 36000
frames): the walk reaches scroll 0x18C0 at frame 27694. BOSS_FROM 20000 and 26000 never
kill Neff (the sparse boss cadence lets the hero die on the walk and he arrives with his
power byte 0xFFE01E back at 0x40, so Neff only spawns enemies, handler 0x16C6C); 27000
reaches the transform countdown 0x1794C at 34775 but not level 2 by 36000; 28000 gives
0x1794C at 28316, 0xFFF14B at 31113 and level 2 (0xFFF142 = 1) at 31804; 29000 and
30000 also reach level 2, later (32124, 33644). The default is 28000.

There is no randomness anywhere: the same arguments always produce the same tape
(--seed-free is accepted for the record and changes nothing).

    python3 tools/make_tape.py [--out tools/inputs/level1_full.csv] [--boss-from 20000]
                               [--length 36000] [--walk-from 1500] [--seed-free]
"""
import argparse
import os

COINS = (500, 540, 580, 620, 660, 700)
COIN_HOLD = 12
START_DOWN, START_UP = 901, 941


def desired(frame, walk_from, boss_from, fight_back=0):
    """The pad state wanted on this frame: {button: 0/1} for right, left, a, b, c."""
    st = {'right': 0, 'left': 0, 'a': 0, 'b': 0, 'c': 0}
    if frame < walk_from:
        return st
    if frame >= boss_from:
        q = frame % 40
        st['right'] = 1
        st['c'] = 1 if q < 4 else 0
        st['b'] = 1 if 8 <= q < 12 else 0
        st['a'] = 1 if 24 <= q < 28 else 0
    else:
        k = frame % 16
        st['right'] = 0 if (frame // 400) % 4 == 3 else 1
        if fight_back and st['right'] == 0 and (frame % 128) < fight_back:
            st['left'] = 1      # turn round: the stand-and-fight block also hits what follows behind
        st['a'] = 1 if k < 4 else 0
        st['b'] = 1 if 8 <= k < 12 else 0
    return st


def make(length, walk_from, boss_from, fight_back=0):
    """The tape as a list of (frame, button, value) edges, in frame order."""
    ev = []
    for c in COINS:
        ev.append((c, 'y', 1))
        ev.append((c + COIN_HOLD, 'y', 0))
    ev.append((START_DOWN, 'start', 1))
    ev.append((START_UP, 'start', 0))
    cur = {'right': 0, 'left': 0, 'a': 0, 'b': 0, 'c': 0}
    for f in range(length):
        want = desired(f, walk_from, boss_from, fight_back)
        for name in ('right', 'left', 'a', 'b', 'c'):
            if want[name] != cur[name]:
                ev.append((f, name, want[name]))
                cur[name] = want[name]
    ev.sort(key=lambda e: e[0])
    return ev


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                  'inputs', 'level1_full.csv'))
    ap.add_argument('--boss-from', type=int, default=28000,
                    help='first frame of the boss pattern (default 28000)')
    ap.add_argument('--length', type=int, default=36000, help='tape length in frames')
    ap.add_argument('--walk-from', type=int, default=1500, help='first frame of the walk')
    ap.add_argument('--fight-back', type=int, default=0, metavar='N',
                    help='in each stand-and-fight block, hold left for N frames every 128 so the '
                         'hero also hits enemies that follow behind (level-3 tapes, 2026-10-25: '
                         'level3_fightback.csv is N=6, level3_fightback40.csv N=40)')
    ap.add_argument('--seed-free', action='store_true',
                    help='accepted for the record: the tape is deterministic regardless')
    a = ap.parse_args()
    ev = make(a.length, a.walk_from, a.boss_from, a.fight_back)
    with open(a.out, 'w') as fh:
        fh.write('# altbeast level 1 gameplay tape, generated by tools/make_tape.py\n')
        fh.write('# frame,button,value; 6-button names; y = coin. Edges only.\n')
        fh.write('# length %d walk-from %d boss-from %d\n' % (a.length, a.walk_from, a.boss_from))
        fh.write('# coins y 12 frames at %s; start %d-%d\n'
                 % (','.join(str(c) for c in COINS), START_DOWN, START_UP))
        fh.write('# walk: right held except (frame//400)%4==3; per 16: a 0-3, b 8-11\n')
        fh.write('# boss: right held; per 40: c 0-3, b 8-11, a 24-27\n')
        if a.fight_back:
            fh.write('# fight-back: in the stand blocks, left for %d frames every 128\n' % a.fight_back)
        for f, name, v in ev:
            fh.write('%d,%s,%d\n' % (f, name, v))
    print('%s: %d events, %d frames, boss from %d' % (a.out, len(ev), a.length, a.boss_from))


if __name__ == '__main__':
    main()
