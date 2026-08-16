#!/usr/bin/env python3
"""Summarize a sprite_census.lua CSV against the MD sprite engine's
hard ceilings. Usage: sprite_census.py [csv]  (default /tmp/sprite_census.csv)

MD VDP H40 ceilings: 80 sprites/frame, 20 sprites/line, 320 sprite-px
fetched/line, sprite max 32px wide (mdeq = ceil(width/32) MD sprites
per S16 record). A sampled frame "fits" when the unzoomed non-shadow
subset alone respects all three; zoomed + shadow records are the
residue the SH-2s (or S/H mode) must still carry.
"""
import csv
import sys


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sprite_census.csv"
    rows = list(csv.DictReader(open(path)))
    if not rows:
        print("no samples")
        return
    n = len(rows)
    g = lambda r, k: int(r[k])
    tot = sum(g(r, "nrec") for r in rows)
    unz = sum(g(r, "nunz") for r in rows)
    shad = sum(g(r, "nshad") for r in rows)
    zoom = sum(g(r, "nzoom") for r in rows)
    print(f"samples={n} (every 10 frames)")
    print(f"records/frame mean={tot / n:.1f} "
          f"max={max(g(r, 'nrec') for r in rows)}")
    if tot:
        print(f"unzoomed non-shadow {100 * unz / tot:.1f}%  "
              f"zoomed {100 * zoom / tot:.1f}%  shadow {100 * shad / tot:.1f}%")
    fit_frame = sum(1 for r in rows if g(r, "md_equiv_total") <= 80)
    fit_line = sum(1 for r in rows if g(r, "worst_line_mdeq") <= 20)
    fit_px = sum(1 for r in rows if g(r, "worst_line_px") <= 320)
    print(f"MD-equiv sprites/frame (unzoomed subset): "
          f"mean={sum(g(r, 'md_equiv_total') for r in rows) / n:.1f} "
          f"max={max(g(r, 'md_equiv_total') for r in rows)} "
          f"(ceiling 80; fits {100 * fit_frame / n:.1f}% of samples)")
    print(f"worst line MD-equiv: "
          f"max={max(g(r, 'worst_line_mdeq') for r in rows)} "
          f"(ceiling 20; fits {100 * fit_line / n:.1f}% of samples)")
    print(f"worst line px (ALL sprites, strip width): "
          f"max={max(g(r, 'worst_line_px') for r in rows)} "
          f"(MD fetch ceiling 320; fits {100 * fit_px / n:.1f}%)")
    print(f"colour sets/frame: max={max(g(r, 'ncolsets') for r in rows)}")
    pps = [sum(g(r, f"pp{i}") for r in rows) for i in range(4)]
    if tot:
        print("priority mix: " + "  ".join(
            f"pp{i}={100 * pps[i] / tot:.1f}%" for i in range(4)))
    # the ten frames that stress the line ceiling hardest
    worst = sorted(rows, key=lambda r: -g(r, "worst_line_mdeq"))[:10]
    print("hardest samples (frame: line-mdeq/line-px/frame-mdeq):")
    print("  " + "  ".join(
        f"{r['frame']}:{r['worst_line_mdeq']}/{r['worst_line_px']}"
        f"/{r['md_equiv_total']}" for r in worst))


if __name__ == "__main__":
    main()
