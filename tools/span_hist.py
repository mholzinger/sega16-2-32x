#!/usr/bin/env python3
"""LOOP 9 pickup-V + blit-span histograms from an ares savestate.

Usage: span_hist.py [path-to-.bs1]   (default rom/PROBE_span.bs1)
Needs a `make SPANPROBE=1` rom; prints nothing useful otherwise.

WHY THIS EXISTS. The blit-span counter (DIAG[26..28]) reports a RATE
and a MAX, and both were misleading on their own. Measured on ares:
the mean span is 26.7 lines against the 38 vblank lines available, so
the mean already fits — the strobe is entirely in the tail. A max of
62 lines cannot distinguish a rare 2.3x spike (fix the spike) from a
fat shoulder sitting just past 38 (fix the mean), and those want
opposite work.

The span is also measured from window PICKUP, not from the start of
vblank, so a late pickup is invisible to it while eating the same
vblank. The V-gate accepts pickup anywhere in V=DF..E4 — a 6-line
spread — and under load the master is mid-compose-strip when the
window arrives. DIAG[50]/[51] split the late restores by pickup half
to test exactly that.
"""
import struct
import sys


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def bar(n, tot, width=40):
    if not tot:
        return ""
    return "#" * max(1, round(width * n / tot)) if n else ""


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_span.bs1"
    st = open(path, "rb").read()
    sd = 0x23B

    def d(i):
        o = sd + 0x28000 + i * 4
        return struct.unpack(">I", swap16(st[o:o + 4]))[0]

    print(f"STATE: {path}   BUILD: {d(18):08x}")
    pick = [d(i) for i in range(34, 42)]
    if not sum(pick):
        print("no SPAN_PROBE data — was this a `make SPANPROBE=1` rom?")
        return

    # PICKUP. The gate accepts DF..E4; anything outside is a silent skip.
    labels = ["V<DF (early)", "V=DF", "V=E0", "V=E1", "V=E2", "V=E3",
              "V=E4", "V>E4 / none"]
    tot = sum(pick)
    print(f"\nWINDOW PICKUP  (n={tot}) — gate accepts DF..E4")
    for lab, n in zip(labels, pick):
        print(f"  {lab:<14} {n:6d} {100.0 * n / tot:5.1f}%  {bar(n, tot)}")
    late_pick = pick[5] + pick[6] + pick[7]
    print(f"  -> pickup at E3 or later: {late_pick} "
          f"({100.0 * late_pick / tot:.1f}%)")

    # SPAN. 38 lines of vblank is the budget; buckets straddle it.
    span = [d(i) for i in range(42, 50)]
    sl = ["<=20 lines", " 21-30", " 31-38  (fits)", " 39-45  (over)",
          " 46-55", " 56-70", " 71-100", "  >100"]
    stot = sum(span)
    print(f"\nBLIT SPAN pickup->restore  (n={stot}) — vblank = 38 lines")
    for lab, n in zip(sl, span):
        print(f"  {lab:<15} {n:6d} {100.0 * n / max(stot, 1):5.1f}%  "
              f"{bar(n, stot)}")
    over = sum(span[3:])
    print(f"  -> past vblank: {over} ({100.0 * over / max(stot, 1):.1f}%)")

    # THE CORRELATION this probe was built for.
    lp, ep = d(50), d(51)
    print(f"\nLATE RESTORES BY PICKUP HALF")
    print(f"  pickup E3/E4 (late)  {lp:6d}")
    print(f"  pickup DF..E2 (early){ep:6d}")
    if lp + ep:
        share = 100.0 * lp / (lp + ep)
        base = 100.0 * late_pick / tot
        print(f"  late-pickup share of overruns: {share:.1f}%  "
              f"(late pickups are {base:.1f}% of all windows)")
        if share > base * 1.5:
            print("  -> LATE PICKUP IS ENRICHED among overruns: the master "
                  "arriving late is a cause, not a coincidence.")
        elif lp + ep > 20:
            print("  -> overruns are NOT enriched for late pickup: the "
                  "window starts on time and the blit itself is the spike.")

    # PER-WINDOW SPLIT. k=1 blits 40 rows where k=0/k=2 blit 36, because
    # 224 rows do not divide into three tile-aligned bands. That is ~3
    # lines against a deficit of ~8, so if the overruns concentrate on
    # k=1 a third of the problem is free.
    if sum(d(i) for i in range(55, 58)):
        print("\nBY WINDOW  (k=1 blits 40 master rows, k=0/k=2 blit 36)")
        for k in range(3):
            n, ov, tk = d(55 + k), d(52 + k), d(58 + k)
            mean = tk / n / 46.0 if n else 0.0
            print(f"  k={k}  n={n:5d}  mean={mean:5.1f} lines  "
                  f"over={ov:4d} ({100.0 * ov / max(n, 1):4.1f}%)")
        ov1, ovo = d(53), d(52) + d(54)
        if ov1 + ovo:
            print(f"  -> k=1 share of overruns: "
                  f"{100.0 * ov1 / (ov1 + ovo):.0f}% (even split would be 33%)")


if __name__ == "__main__":
    main()
