#!/usr/bin/env python3
"""Grade TESTCART's pen grid from a screenshot (2026-09-27).

WHY A HOST-SIDE GRADER. Every other test in the cart grades itself, because it
can read back what it wrote. This one cannot: there is no path from the 68K to
the COMPOSITED OUTPUT, so "the pen displays" is a claim the cart can make and
not check. T24/T25 prove CRAM HOLDS a value; a palette that reads back
perfectly and reaches the DAC wrong is exactly the pink-trees failure. The
project's acceptance gate is pixel truth on ares, so the grading belongs where
the pixels are.

WHAT THE CART PAINTS (md_src/testcart.c, tc_pattern): four ramps, one per MD
palette -- grey, red, green, blue -- sixteen pens across, with level = pen >> 1
so each ramp walks all eight levels of its gun TWICE. The pair-per-level is
deliberate: a dropped LSB shows up as a step that fails to repeat rather than
as a colour nobody can name.

    GRID origin tile col 4, tile row 4; cell 2 tiles wide, 3 tiles tall;
    16 columns (pen 0..15) x 4 rows (palette 0..3)
    => cell (p,n) centre in screen pixels is x = 40 + 16n, y = 44 + 24p

THREE CHECKS, in increasing strength:
  1. every cell is FLAT (the 3x2-tile block is one colour) -- catches a tile or
     name-table fault that a centre-pixel sample would walk straight past;
  2. the three coloured ramps use the SAME eight levels on their own gun as the
     grey ramp does on all three, and the other two guns stay at zero;
  3. those eight measured levels match the DAC ramp m_main.c records --
     parsed from the source, not restated here, so the comment is under test
     rather than quoted.

    python3 tools/tc_pattern.py shot.png
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# The established ares crop: active area rows 11-234, 4:1 horizontally, and
# NEAREST -- BILINEAR once nearly produced a fictional 9.6% divergence report.
CROP = (65, 11, 65 + 1280, 11 + 224)


def recorded_ramp():
    """The MD DAC ramp as m_main.c states it, parsed rather than retyped."""
    src = (ROOT / "sh_src" / "m_main.c").read_text(errors="replace")
    m = re.search(r"DAC ramp[\s\S]{0,200}?measured\s+([0-9,\s*]+?)\s*\(ares", src)
    if not m:
        return None
    vals = [int(x) for x in re.findall(r"\d+", m.group(1))]
    return vals if len(vals) == 8 else None


def load(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    if im.size[0] >= CROP[2] and im.size[1] >= CROP[3]:
        im = im.crop(CROP).resize((320, 224), Image.NEAREST)
    elif im.size != (320, 224):
        sys.exit(f"tc_pattern: {path} is {im.size}, expected a 320x224 rig "
                 f"capture or a {CROP[2]}x{CROP[3]}+ ares frame")
    return im.load()


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1].strip())
    px = load(argv[1])
    rc = 0

    # 1. FLATNESS. Sample the cell's interior, not just its centre.
    ragged = []
    cell = {}
    for p in range(4):
        for n in range(16):
            cx, cy = 40 + 16 * n, 44 + 24 * p
            seen = {px[cx + dx, cy + dy]
                    for dx in (-6, 0, 6) for dy in (-9, 0, 9)}
            cell[(p, n)] = px[cx, cy]
            if len(seen) != 1:
                ragged.append((p, n, sorted(seen)))
    if ragged:
        rc = 1
        print(f"FAIL  {len(ragged)} of 64 cells are not a flat colour")
        for p, n, seen in ragged[:4]:
            print(f"        palette {p} pen {n:2d}: {seen}")
    else:
        print("PASS  all 64 cells are a flat colour across the 3x2-tile block")

    # 2. THE RAMPS AGREE. Grey defines the eight levels; each coloured ramp must
    #    reproduce them on its own gun and hold the other two at zero.
    grey = [cell[(0, n)] for n in range(16)]
    levels = []
    for L in range(8):
        a, b = grey[L * 2], grey[L * 2 + 1]
        if a != b:
            rc = 1
            print(f"FAIL  grey level {L} differs between its two pens: {a} {b}")
        if not (a[0] == a[1] == a[2]):
            rc = 1
            print(f"FAIL  grey level {L} is not neutral: {a}")
        levels.append(a[0])

    for p, gun in ((1, 0), (2, 1), (3, 2)):
        bad = 0
        for L in range(8):
            c = cell[(p, L * 2)]
            want = tuple(levels[L] if g == gun else 0 for g in range(3))
            if c != want:
                bad += 1
                if bad == 1:
                    print(f"FAIL  palette {p} level {L}: got {c}, want {want}")
        if bad:
            rc = 1
        else:
            print(f"PASS  palette {p} reproduces all 8 grey levels on gun "
                  f"{'RGB'[gun]}, other guns at 0")

    print(f"INFO  the eight levels this machine produced: {levels}")

    # 3. AGAINST THE RECORDED RAMP.
    rec = recorded_ramp()
    if rec is None:
        print("INFO  could not parse the DAC ramp out of m_main.c -- skipped")
    elif rec == levels:
        print(f"PASS  matches the ramp m_main.c records: {rec}")
    else:
        rc = 1
        print(f"FAIL  m_main.c records {rec}")
        print(f"        this machine     {levels}")
        print(f"        deltas           {[a - b for a, b in zip(levels, rec)]}")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
