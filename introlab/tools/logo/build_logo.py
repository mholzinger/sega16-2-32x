#!/usr/bin/env python3
"""Build the SEGA logo as flat convex polygons for the intro doors.

    build_logo.py [reference.png x0 y0 x1 y1]   -> art/logo/logo.json, art/logo/logo_check.png

WHY GEOMETRY. The renderer draws flat convex polygons cheaply and textured ones dearly, and
the logo is one flat colour. WHY NOT A TRACE. The only logo on disk is 185 pixels wide inside
a marquee scan; tracing it gives ragged letters. Its proportions, though, measure cleanly,
and the logo is regular: every stroke is the same width with a hairline down its middle,
and parallel strokes sit one hairline apart. So the letters are written here as centre
lines, in the measured units, and the shape is generated from them:

    blue = within STROKE/2 of a centre line, but not within HAIR/2 of it
    underlay = within STROKE/2 + OUTLINE of a centre line (pale; drawn first, so it shows
               as the outline, the hairline and the gaps between strokes: the stripes)

The generated mask is then cut into horizontal trapezoids (flat top and bottom, so convex
by construction, holes included). With a reference crop given, logo_check.png shows the
reference's blue mask above the generated one for comparison.

logo.json: trapezoids [y0, y1, xl0, xr0, xl1, xr1] in a box 1.0 wide, y down."""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

LAB = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(LAB, "art", "logo")

# Measured on the marquee logo at 4x: 705 x 219 units.
W, H = 705, 219
STROKE = 64          # full stroke width, hairline included
HAIR = 8             # the hairline, and the gap between parallel strokes
OUTLINE = 9          # the pale underlay shows this far outside the letters, and through every gap
M = OUTLINE + 3      # canvas margin, so the underlay is not clipped
Y1, Y2, Y3 = 36, 109, 183        # centre lines of the top, middle and bottom bars
# Each letter: centre-line paths as (points, hairline stops short of the start, ...of the end).
# A path's free ends are cut square; where a path turns, the corner is rounded by the stroke
# itself. The hairline stops short of a closed end and runs through an open one.
LETTERS = {
    "S": [([(168, Y1), (36, Y1), (36, Y2), (132, Y2), (132, Y3), (0, Y3)], True, True)],
    "E": [([(336, Y1), (219, Y1), (219, Y3), (336, Y3)], True, True),
          ([(219, Y2), (336, Y2)], False, False)],          # joins the upright; open at the right
    "G": [([(506, Y1), (386, Y1), (386, Y3), (474, Y3), (474, Y2), (422, Y2)], True, True)],
    "A": [([(538, 219), (613, 34), (670, Y3), (632, Y3)], True, True)],
}
RES = 4              # raster units per logo unit while generating
TOL = 2.2            # how far an edge may stray from straight, raster units


def draw_paths(draw, width, inset, fill):
    """Stroke every path `width` wide on a canvas with margin M. Free ends are pulled in by `inset`."""
    return _draw_paths(draw, width, inset, fill)


def _draw_paths(draw, width, inset, fill):
    """Stroke every path `width` wide. Free ends are pulled in by `inset`."""
    for paths in LETTERS.values():
        for path, in_start, in_end in paths:
            pts = [((x + M) * RES, (y + M) * RES) for x, y in path]
            if inset:
                for end, nxt in [e for e, on in (((0, 1), in_start), ((-1, -2), in_end)) if on]:
                    (ex, ey), (nx, ny) = pts[end], pts[nxt]
                    d = math.hypot(nx - ex, ny - ey)
                    k = min(0.5, inset * RES / d)
                    pts[end] = (ex + (nx - ex) * k, ey + (ny - ey) * k)
            for a, b in zip(pts, pts[1:]):
                dx, dy = b[0] - a[0], b[1] - a[1]
                d = math.hypot(dx, dy)
                nx, ny = -dy / d * width * RES / 2, dx / d * width * RES / 2
                draw.polygon([(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny),
                              (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)], fill=fill)
            r = width * RES / 2
            for x, y in pts[1:-1]:                    # round the turns
                draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def runs(row):
    d = np.diff(np.concatenate(([0], row.astype(np.int8), [0])))
    return list(zip(np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]))


def breakpoints(vals, lo, hi, tol, out):
    if hi - lo < 2:
        return
    a, b = vals[lo], vals[hi]
    best, bi = 0.0, -1
    for i in range(lo + 1, hi):
        d = abs(vals[i] - (a + (b - a) * (i - lo) / (hi - lo)))
        if d > best:
            best, bi = d, i
    if best > tol:
        out.add(bi)
        breakpoints(vals, lo, bi, tol, out)
        breakpoints(vals, bi, hi, tol, out)


def trapezoids(mask):
    """Stack runs into strips (one run over exactly one run), then cut each strip where a
    side stops being straight."""
    done, open_ = [], []
    over = lambda a, b: a[0] < b[1] and b[0] < a[1]
    for y in range(mask.shape[0] + 1):
        rr = runs(mask[y]) if y < mask.shape[0] else []
        nxt, used = [], set()
        for s in open_:
            hit = [i for i, r in enumerate(rr) if over(s["rows"][-1], r)]
            if len(hit) == 1 and sum(over(t["rows"][-1], rr[hit[0]]) for t in open_) == 1:
                s["rows"].append(rr[hit[0]])
                used.add(hit[0])
                nxt.append(s)
            else:
                done.append(s)
        nxt += [dict(y0=y, rows=[r]) for i, r in enumerate(rr) if i not in used]
        open_ = nxt
    out = []
    for s in done:
        rows, n = s["rows"], len(s["rows"])
        xl = [r[0] for r in rows] + [rows[-1][0]]
        xr = [r[1] for r in rows] + [rows[-1][1]]
        cuts = {0, n}
        breakpoints(xl, 0, n, TOL, cuts)
        breakpoints(xr, 0, n, TOL, cuts)
        cuts = sorted(cuts)
        out += [(s["y0"] + a, s["y0"] + b, xl[a], xr[a], xl[b], xr[b]) for a, b in zip(cuts, cuts[1:])]
    return out


def main():
    CW, CH = W + 2 * M, H + 2 * M
    img = Image.new("L", (CW * RES, CH * RES), 0)
    d = ImageDraw.Draw(img)
    draw_paths(d, STROKE, 0, 255)
    draw_paths(d, HAIR, (STROKE - HAIR) / 2, 0)
    mask = np.array(img) > 127
    traps = trapezoids(mask)
    under = Image.new("L", (CW * RES, CH * RES), 0)
    draw_paths(ImageDraw.Draw(under), STROKE + 2 * OUTLINE, 0, 255)      # square ends grow sideways only...
    under = under.filter(ImageFilter.MaxFilter(2 * (OUTLINE * RES // 2) + 1))   # ...so grow every edge a little too
    umask = np.array(under) > 127
    utraps = trapezoids(umask)
    wpx = float(mask.shape[1])
    os.makedirs(OUT, exist_ok=True)
    json.dump(dict(aspect=CH / CW, trapezoids=[[round(v / wpx, 5) for v in t] for t in traps],
                   underlay=[[round(v / wpx, 5) for v in t] for t in utraps]),
              open(os.path.join(OUT, "logo.json"), "w"))

    crop = (M * RES, M * RES, (M + W) * RES, (M + H) * RES)
    rows = [Image.fromarray((mask * 255).astype(np.uint8)).crop(crop).resize((W, H), Image.LANCZOS).convert("RGB")]
    redraw = Image.new("RGB", (CW * RES, CH * RES), (20, 20, 20))
    rd = ImageDraw.Draw(redraw)
    for ya, yb, xla, xra, xlb, xrb in utraps:
        rd.polygon([(xla, ya), (xra, ya), (xrb, yb), (xlb, yb)], fill=(235, 240, 255))
    for i, (ya, yb, xla, xra, xlb, xrb) in enumerate(traps):
        rd.polygon([(xla, ya), (xra, ya), (xrb, yb), (xlb, yb)], fill=((40, 60, 200), (90, 130, 255), (20, 30, 140))[i % 3])
    rows.append(redraw.resize((CW, CH), Image.LANCZOS).crop((M, M, M + W, M + H)))
    if len(sys.argv) >= 6:                             # the measured original, same size, on top
        ref = Image.open(sys.argv[1]).convert("RGB").crop(tuple(int(v) for v in sys.argv[2:6]))
        a = np.array(ref.resize((ref.width * 4, ref.height * 4), Image.LANCZOS)).astype(int)
        blue = (a[:, :, 2] > 110) & (a[:, :, 2] - a[:, :, 0] > 60) & (a[:, :, 2] - a[:, :, 1] > 60)
        ys, xs = np.nonzero(blue)
        blue = blue[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        rows.insert(0, Image.fromarray((blue * 255).astype(np.uint8)).resize((W, H), Image.NEAREST).convert("RGB"))
    chk = Image.new("RGB", (W, (H + 6) * len(rows)), (90, 0, 90))
    for i, r in enumerate(rows):
        chk.paste(r, (0, i * (H + 6)))
    chk.save(os.path.join(OUT, "logo_check.png"))
    print(f"logo: {len(traps)} trapezoids + {len(utraps)} underlay -> {os.path.join(OUT, 'logo.json')}")


if __name__ == "__main__":
    main()
