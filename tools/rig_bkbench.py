#!/usr/bin/env python3
"""READ THE R60BKBENCH BARS OFF A SCREENSHOT (ares or the rig).

    tools/rig_bkbench.py shot.png [shot2.png ...]

A `R60BKBENCH=1 R60NOBARS=` rom (r60_main.c r60_bkb_bars) paints seven bars, four rows each,
in screen rows 196..223: a 16 px floor plus the value, capped at 300, in the forced-white
bar pen. Each bar is the leading run of white; the median of its four rows is taken, the
floor subtracted, and the value scaled back:

  bars 0..4 (rows 196 200 204 208 212)   the microbenchmark, 512 groups: value = FRT ticks / 32,
                                         so SH-2 cycles a group = value / 2
                                         (SDRAM->SDRAM, cart->SDRAM first touch, SDRAM->FB,
                                          cart->FB, cart->SDRAM again)
  bar 5 (rows 216)                       the live copy: cycles a copied row = value x 4
  bar 6 (rows 220)                       the live plotter: cycles per 4 rowpix units = value

Bars at 300 are CLIPPED (the value is at least that). Bench bars all at 0 mean the bench has
not run yet (it runs at the first baked record): shoot later.

Geometry: the rig writes the native 320x224 picture; ares-headless writes a bordered,
scaled picture (1415x243: 65 px black left, 4x wide, 1x tall, 11 rows down). The strip is
found from the bottom as the contiguous rows whose first active pixel is white, and its
height gives the vertical scale; the horizontal scale is the active width over 320.
"""
import os, statistics, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig_bars import unpng

NAMES = ['sdram->sdram', 'cart->sdram 1st', 'sdram->fb', 'cart->fb', 'cart->sdram again',
         'copy cycles/row', 'plot cycles/4px']
WHITE = (255, 255, 255)


def read(path):
    w, h, ch, rows = unpng(path)

    def px(y, x):
        o = x * ch
        return tuple(rows[y][o:o + 3])

    def active(y):
        """first and last non-black column of a row, or None"""
        x0 = 0
        while x0 < w and px(y, x0) == (0, 0, 0):
            x0 += 1
        if x0 >= w:
            return None
        x1 = w - 1
        while px(y, x1) == (0, 0, 0):
            x1 -= 1
        return x0, x1
    # the active width: the widest non-black extent over the picture's middle rows
    ext = [active(y) for y in range(h // 4, 3 * h // 4)]
    ext = [e for e in ext if e]
    if not ext:
        raise SystemExit('rig_bkbench: %s: the picture is black' % path)
    x0 = min(e[0] for e in ext); x1 = max(e[1] for e in ext)
    # the rig writes the native picture: 1:1, whatever the scene's black edges (a shot with a
    # black right edge read 28% high before this)
    sx = 1.0 if w == 320 else (x1 - x0 + 1) / 320.0
    # the strip: from the bottom, rows whose first active pixel is white
    strip = []
    y = h - 1
    while y >= 0 and active(y) is None:
        y -= 1
    while y >= 0:
        a = active(y)
        if a is None or px(y, a[0]) != WHITE:
            break
        strip.append(y); y -= 1
    if len(strip) < 7:
        raise SystemExit('rig_bkbench: %s: no bar strip at the bottom (found %d white-led rows)' % (path, len(strip)))
    strip.sort()
    sy = len(strip) / 28.0
    out = []
    for k in range(7):
        runs = []
        for yy in strip[int(round(4 * k * sy)):int(round(4 * (k + 1) * sy))]:
            a = active(yy); n = 0
            while a[0] + n < w and px(yy, a[0] + n) == WHITE:
                n += 1
            if 0 < n < w - a[0]:
                runs.append(n / sx)
        out.append(max(round(statistics.median(runs)) - 16, 0) if runs else None)
    return out


PUNCHPROF = None   # set by --punchprof: (names, scales) of a R60PUNCHPROF rom


def fmt(v):
    parts = []
    if PUNCHPROF:
        names, scales = PUNCHPROF
        for name, x, s in zip(names, v, scales):
            parts.append('%s: no bar' % name if x is None else '%s %d%s' % (name, x * s, '+' if x >= 300 else ''))
        return '  punch a pass: %s\n  cycles: %s' % (' | '.join(parts[k] for k in (0, 1, 6, 2)), ' | '.join(parts[k] for k in (3, 4, 5)))
    for k, (name, x) in enumerate(zip(NAMES, v)):
        if x is None:
            parts.append('%s: no bar' % name); continue
        clip = '+' if x >= 300 else ''
        if k < 5:
            parts.append('%s %.1f%s' % (name, x / 2.0, clip))
        elif k == 5:
            parts.append('%s %d%s' % (name, x * 4, clip))
        else:
            parts.append('%s %d%s' % (name, x, clip))
    return '  bench cycles/group: %s\n  live: %s' % (' | '.join(parts[:5]), ' | '.join(parts[5:]))


def main():
    global NAMES, PUNCHPROF
    paths = [a for a in sys.argv[1:] if not a.startswith('-')]
    if '--punchprof' in sys.argv:  # a R60PUNCHPROF rom (r60_main.c r60_pup_bars)
        PUNCHPROF = (['prio cells', 'rows iterated', 'cold rebuilds', 'resolve cyc/cell', 'cyc/row',
                      'cyc/rebuild', 'rows hit'], [1, 4, 1, 2, 1, 16, 4])
    if '--punch' in sys.argv:      # a R60BKBENCH=2 rom: the punch's memory (r60_main.c)
        NAMES = ['sdram->sdram', 'FB(u)->sdram read', 'sdram->fb', 'FB(u) rmw', 'sdram(u) rmw',
                 'copy cycles/row', 'plot cycles/4px']
    if not paths:
        print(__doc__); return 2
    for p in paths:
        try:
            print('%s\n%s' % (os.path.basename(p), fmt(read(p))))
        except SystemExit as e:
            print(e)
    return 0


if __name__ == '__main__':
    sys.exit(main())
