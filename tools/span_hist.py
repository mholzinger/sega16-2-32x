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

    # v3: WHO was in flight when the pickup missed. Slots [42..49] bin
    # every skipped pickup by the master's poll-loop stage (m_stage).
    stg = [d(i) for i in range(42, 50)]
    stot = sum(stg)
    if stot:
        print(f"\nMISSED PICKUPS BY MASTER STAGE  (n={stot})")
        sl = ["idle/poll (MD posted late)", "owed build_maps chunk",
              "shadow LUT chunk", "maintenance steal",
              "tile strip (ph 0/1)", "sprite strip (ph 2)",
              "single-shot (ph >= 3)", "post-ack window tail"]
        for lab, n in zip(sl, stg):
            print(f"  {lab:<27} {n:6d} {100.0 * n / stot:5.1f}%  "
                  f"{bar(n, stot)}")
        print("     strips/chunks dominant => tighten that stage's "
              "pre-vint quiet zone or chop it; idle dominant => the 68K "
              "posts late, look at the MD window/ack path (item 2); "
              "post-ack tail => the pivot's packet/copy work is the hold.")


if __name__ == "__main__":
    main()
