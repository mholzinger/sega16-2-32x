#!/usr/bin/env python3
"""READ THE PORTRIGCLK WORDS OFF RIG SCREENSHOTS AND FIT THE TWO RATES (2026-11-03).

    tools/rig_clock.py shot1.png shot2.png ...        one line per shot, then the fit
    tools/rig_clock.py build/accept/rig/pass_psg6clk  every *.png in the folder

A PORTRIGCLK=1 rom (Makefile, sh_src/r60/r60_main.c) paints two raw 16-bit words into screen
rows 216..223 of the 32X framebuffer, bit 15 at the left, one 16-px block a bit, lit = 1:

  rows 216..219   the master's vint count (r60_frame)
  rows 220..223   the game's frame counter (port_gf_count, sh_src/port_l2.c)

The wall time of each shot is read from its file name, `NN_<seconds>s.png` as tools/rigclock.sh
writes it, else from the file's mtime. A least-squares line through (wall, word) for each word,
unwrapped mod 65536, gives vints per second (an honest handler reads 60.0) and game ticks per
second (the pace: 60 = arcade, 48 = 0.8x). Both are printed with the residual, and the ratio.

The lit pen is the brightest colour in the image (tools/rig_bars.py's rule: FB value 0 is
TRANSPARENT on the 32X, so a dark block shows the Mega Drive plane through it and the row is
never two colours). Each block votes over its 4 rows x 16 px; a block is 1 when a majority of
those pixels are the pen. Shots whose two words disagree across their rows are reported and
dropped from the fit.
"""
import os, re, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig_bars import unpng

ROWS_A = range(216, 220)
ROWS_B = range(220, 224)


def words(path):
    """The rig's screenshot is the bare 320x224 picture; ares-headless pads it (measured
    2026-11-03: 1415x243, picture at x 65..1344 = 4 px a pixel, rows 11..234 = 1 row a
    row). Both are read through the bounding box of the non-black picture, 320x224 inside."""
    w, h, ch, rows = unpng(path)

    def raw(y, x):
        o = x * ch
        return tuple(rows[y][o:o + 3])

    if (w, h) == (320, 224):
        x0, y0, sx, sy = 0, 0, 1.0, 1.0
    else:
        ys = [y for y in range(h) if any(raw(y, x) != (0, 0, 0) for x in range(0, w, 4))]
        xs = [x for x in range(w) if any(raw(y, x) != (0, 0, 0) for y in range(ys[0], ys[-1] + 1, 4))]
        x0, y0 = xs[0], ys[0]
        sx = (xs[-1] + 1 - x0) / 320.0
        sy = (ys[-1] + 1 - y0) / 224.0

    def px(y, x):
        return raw(min(h - 1, y0 + int(y * sy)), min(w - 1, x0 + int(x * sx)))

    best, pen = -1, None
    for y in range(0, 224, 2):
        for x in range(0, 320, 2):
            c = px(y, x)
            s = c[0] + c[1] + c[2]
            if s > best:
                best, pen = s, c

    def word(rr):
        v = 0
        for b in range(16):
            lit = 0
            for y in rr:
                for x in range(b * 16 + 2, b * 16 + 14):
                    if px(y, x) == pen:
                        lit += 1
            if lit * 2 > len(rr) * 12:
                v |= 1 << (15 - b)
        return v
    return word(ROWS_A), word(ROWS_B), pen


def wall(path):
    m = re.search(r'_(\d+)s\.png$', os.path.basename(path))
    return float(m.group(1)) if m else os.path.getmtime(path)


def fit(pts):
    """least squares through (t, v) after unwrapping v mod 65536 in t order"""
    pts = sorted(pts)
    out, off, prev = [], 0, None
    for t, v in pts:
        if prev is not None and v + off < prev - 32768:
            off += 65536
        out.append((t, v + off)); prev = v + off
    n = len(out)
    if n < 2:
        return None, None
    mt = sum(t for t, _ in out) / n
    mv = sum(v for _, v in out) / n
    sxx = sum((t - mt) ** 2 for t, _ in out)
    if sxx == 0:
        return None, None
    k = sum((t - mt) * (v - mv) for t, v in out) / sxx
    res = max(abs(v - (mv + k * (t - mt))) for t, v in out)
    return k, res


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    paths = []
    for a in args:
        paths += sorted(glob.glob(os.path.join(a, '*.png'))) if os.path.isdir(a) else [a]
    pa, pb = [], []
    for p in paths:
        a, b, pen = words(p)
        t = wall(p)
        print('%-24s wall %7.1f s  vints %5d  game %5d  pen %s' % (os.path.basename(p), t, a, b, pen))
        pa.append((t, a)); pb.append((t, b))
    ka, ra = fit(pa)
    kb, rb = fit(pb)
    if ka is None:
        raise SystemExit('rig_clock: need two shots with distinct wall times')
    print('vints/s   %6.2f  (max residual %.0f frames)' % (ka, ra))
    print('ticks/s   %6.2f  (max residual %.0f frames)' % (kb, rb))
    if ka:
        print('pace      %5.3f of 60 (ticks/s / 60)   ticks per vint %5.3f' % (kb / 60.0, kb / ka))


if __name__ == '__main__':
    main()
