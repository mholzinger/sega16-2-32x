#!/usr/bin/env python3
"""LOOP 9/13 pickup-V histogram from an ares savestate.

Usage: span_hist.py [path-to-.bsN]   (default rom/PROBE_span.bs1)
Needs a `make SPANPROBE=1` rom; prints nothing useful otherwise.

WHY THIS EXISTS. The V-gate accepts window pickup anywhere in
V=DF..E4; anything outside is a silent skip and that band ships a
full-cycle-old frame — the jitter Mike sees. bs9 (LOOP 13) put 276 of
287 skips in the v<DF bucket, and a live heartbeat can only read <DF
once the MD's V-counter re-enters 00..DE — the NEXT frame's active
scan, >=25 lines after the raise. So v<DF pickups are WRAPPED-LATE,
not early: the master was busy for 25+ scanlines before it looked at
the window. v2 bins those V values ([42..48], ~38+v lines late) to
say HOW late.

RETIRED from v1 (do not re-add): the blit-span distribution (answered:
0 of 2655 spans past vblank — the blitter is innocent), the [50]/[51]
late-restore split and the [52..60] per-window split (their slots were
reclaimed by pivot-era counters — evictions, sprite/page/ISR work —
which made bs9's v1 readout self-contradictory: 104.6% overruns, 2046
late restores against 11 late pickups).
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
    labels = ["V<DF (WRAPPED)", "V=DF", "V=E0", "V=E1", "V=E2", "V=E3",
              "V=E4", "V>E4 / none"]
    tot = sum(pick)
    print(f"\nWINDOW PICKUP  (n={tot}) — gate accepts DF..E4")
    for lab, n in zip(labels, pick):
        print(f"  {lab:<14} {n:6d} {100.0 * n / tot:5.1f}%  {bar(n, tot)}")
    skips = pick[0] + pick[7]
    print(f"  -> gate rejects (skips): {skips} ({100.0 * skips / tot:.1f}%)")

    # HOW LATE the wrapped pickups are. Raise is at ~V=E0; the counter
    # runs E1..EA, jumps back E5..FF, then re-enters 00.. — so a wrapped
    # read of v is ~38+v lines after the raise. Bin = v>>5.
    wrap = [d(i) for i in range(42, 49)]
    wtot = sum(wrap)
    if wtot:
        print(f"\nWRAPPED PICKUP LATENESS  (n={wtot}) — ~38+v lines after "
              f"the raise; frame = 262")
        wl = ["v=00-1F (~ 38- 69 ln)", "v=20-3F (~ 70-101 ln)",
              "v=40-5F (~102-133 ln)", "v=60-7F (~134-165 ln)",
              "v=80-9F (~166-197 ln)", "v=A0-BF (~198-229 ln)",
              "v=C0-DE (~230-260 ln)"]
        for lab, n in zip(wl, wrap):
            print(f"  {lab:<22} {n:6d} {100.0 * n / wtot:5.1f}%  "
                  f"{bar(n, wtot)}")
        near = wrap[0]
        far = wtot - near
        print(f"  -> first bin (<=69 lines, a long compose strip) {near} "
              f"vs deeper {far}")
        print("     first-bin-heavy => shrink strip granularity / poll "
              "mid-strip; deep+flat => the master is saturated, cut its "
              "per-cycle work (handoff item 2).")


if __name__ == "__main__":
    main()
