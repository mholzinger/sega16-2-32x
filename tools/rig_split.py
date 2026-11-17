#!/usr/bin/env python3
"""READ THE R60RIGSPLIT PHASE BARS OFF A SCREENSHOT (ares or the rig).

    tools/rig_split.py shot.png [shot2.png ...]
    tools/rig_split.py --wide shot.png   # R60RIGSPLIT=3,8lines/pixel
    tools/rig_split.py --health shot.png # mode4: clocks and peak plot instead of IRQ

A `R60RIGSPLIT=1 R60NOBARS=` rom (r60_main.c r60_rs_bars) paints 28 bars, one row each, in
screen rows 196..223, 1 px a SCANLINE, refreshed every 32 passes (the window average):

  row  value
  196  keep (t0 -> clear start)          lines a pass
  197  CLEAR                              lines a pass
  198  prep (clear done -> hold fill)     lines a pass
  199  PLOT                               lines a pass
  200  PUNCH                              lines a pass
  201  sprite phase minus plot and punch  lines a pass
  202  band wait   203 TEXT   204 these bars   205 publish   206 cram commit   207 flip + span
  208  (wait + no-snapshot + 68K-span + flip waits) / 2
  209  vints a pass x 262 / 2             (262 = one vint)
  210  slave total lines a pass / 2
  211  the no-snapshot wait               lines a pass
  212..218  master idle tick snapshot cleared-wait band planes late-band, in 1/262 of its
            window (x 1 px)
  219  master tick cycles / vint / 256    220 passes in the window   221 vints / 2
  222  the 68K-span wait   223 the flip wait     lines a pass

Mode3 (--wide): slave duration rows use8lines/pixel, including totals/waits;
master shares and counters retain their units. Row219 is the mean C V-int body
FRT ticks (8CPU cycles each), excluding assembly entry/exit and meter bookkeeping.
Saturated rows are lower bounds, never exact measurements.

Each bar is the leading run of the forced-white bar pen; the rig's native 320x224 shot is
1:1, ares-headless's bordered 4x-wide shot is scaled by its active width. A row with no run
is 0.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig_bars import unpng

WHITE = (255, 255, 255)
ROWS = ['keep', 'CLEAR', 'prep', 'PLOT', 'PUNCH', 'spr-rest', 'band', 'TEXT', 'bars', 'publish',
        'cram', 'flip', 'waits/2', 'vints*131', 'slave/2', 'nosnap', 'm.idle', 'm.tick', 'm.snap',
        'm.clrd', 'm.band', 'm.planes', 'm.late', 'm.tick/v', 'passes', 'vints/2', 'w.68k', 'w.flip']


def read(path):
    w, h, ch, rows = unpng(path)

    def px(y, x):
        o = x * ch
        return tuple(rows[y][o:o + 3])

    def active(y):
        x0 = 0
        while x0 < w and px(y, x0) == (0, 0, 0):
            x0 += 1
        if x0 >= w:
            return None
        x1 = w - 1
        while px(y, x1) == (0, 0, 0):
            x1 -= 1
        return x0, x1
    if w == 320 and h == 224:
        x0, sx, y0, sy = 0, 1.0, 0, 1.0
    else:
        ext = [e for e in (active(y) for y in range(h // 4, 3 * h // 4)) if e]
        if not ext:
            raise SystemExit('rig_split: %s: the picture is black' % path)
        x0 = min(e[0] for e in ext); x1 = max(e[1] for e in ext)
        sx = (x1 - x0 + 1) / 320.0
        # the picture's bottom edge: the last row with anything in it
        yb = h - 1
        while yb >= 0 and active(yb) is None:
            yb -= 1
        sy = 1.0
        y0 = yb + 1 - 224
    out = []
    for r in range(28):
        y = int(y0 + (196 + r + 0.5) * sy)
        n = 0
        while x0 + n < w and px(y, x0 + n) == WHITE:
            n += 1
        out.append(round(n / sx))
    return out


def fmt(v):
    # the window repaints when 32 passes have gone by; a strip whose passes row is not 32..63
    # is a scene without passes (title, high-score table) and its rows are picture, not bars
    if not 32 <= v[24] < 64:
        return '  NO WINDOW (passes row %d): a scene without slave passes, rows are picture' % v[24]
    a = ' | '.join('%s %d' % (ROWS[k], v[k]) for k in (0, 1, 2, 3, 4, 5, 7, 11))
    b = 'pass total %d lines (vints a pass %.2f), waits %d, passes %d' % (2 * v[14], 2 * v[13] / 262.0, 2 * v[12], v[24])
    c = 'master: ' + ' '.join('%s %d' % (ROWS[k], v[k]) for k in range(16, 23))
    return '  %s\n  %s\n  %s' % (a, b, c)


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith('-')]
    plot2 = '--plot2' in sys.argv        # a R60RIGSPLIT=2 rom: PLOT and PUNCH at 2 lines a px
    health = '--health' in sys.argv
    wide = '--wide' in sys.argv or health
    if plot2 and wide:
        raise SystemExit('choose --plot2 OR --wide to match the ROM flags')
    if health:
        ROWS[23] = 'PLOT.max'
    elif wide:
        ROWS[23] = 'irq.FRT'
    if not paths:
        print(__doc__); return 2
    for p in paths:
        try:
            v = read(p)
            if health:
                from rig_health import read as health_read
                clocks = health_read(p)  # rejects missing marker or torn/invalid words
            clipped = [ROWS[k] for k, value in enumerate(v) if value == 319]
            if plot2:
                v[3] *= 2; v[4] *= 2
            if wide:
                for k in list(range(12)) + [15, 26, 27]:
                    v[k] *= 8
                for k in (12, 13, 14):
                    v[k] *= 4
                if health:
                    v[23] *= 8
            print('%s\n%s' % (os.path.basename(p), fmt(v)))
            if health:
                print('  peak plot %d lines/pass; clocks %s' % (v[23], clocks))
            elif wide:
                print('  V-int body avg ~%d CPU cycles; already included in master phases' % (v[23] * 8))
            if clipped:
                print('  SATURATED (lower bounds only): ' + ', '.join(clipped))
        except SystemExit as e:
            print(e)
    return 0


if __name__ == '__main__':
    sys.exit(main())
