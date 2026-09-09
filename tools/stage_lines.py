#!/usr/bin/env python3
"""68K SHIM STAGE LINES from the V-stamps the shipping code already writes.

    python3 tools/stage_lines.py trace.csv [--frames a-b]

Input: an ares-debug `--trace-access` csv that covered the stamp words
(0xFFA09E-0xFFA0CC push autopsy + tail, 0xFFB0B0-0xFFB0B8 consume,
0xFFA170-0xFFA186 fine), the FM register (0xA15100), COMM0 (0xA15120),
the game's IRQ4 entry fetch (0x902AAC) and its miss counter (0xFFF144).

Prints, per vint, every stamp WRITE (and the FM raise / post / IRQ4
entry) in execution order with its beam line (0 = vblank start), then
a table of the median line of each boundary across the vints. The
stamps are one 68K word write each, so the trace is the shipping
build's own timing, not a probe's.
"""
import argparse, collections, csv, statistics, sys

NAMES = {
    0xFFB0B0: "cons.entry", 0xFFB0B2: "cons.scroll", 0xFFB0B4: "cons.dmas", 0xFFB0B6: "cons.cells",
    0xFFA170: "cons.spans", 0xFFA172: "cons.pal", 0xFFA174: "cons.mark", 0xFFA176: "cons.END",
    0xFFA0B4: "push.entry", 0xFFA0C4: "push.ndirty", 0xFFA0C6: "push.preglow", 0xFFA0C8: "push.sentinel",
    0xFFA0CA: "push.postglow", 0xFFA0C0: "push.rotor0", 0xFFA0BE: "push.rotorDONE", 0xFFA0C2: "push.c2",
    0xFFA178: "push.palnext", 0xFFA17A: "push.rs0", 0xFFA17C: "push.rs1", 0xFFA17E: "push.rs2",
    0xFFA182: "push.blast0", 0xFFA184: "push.blastDONE",
    0xFFA0B6: "push.selection", 0xFFA0B8: "push.regs", 0xFFA0BA: "push.pal", 0xFFA0BC: "push.records",
    0xFFA0A0: "tail.POST", 0xFFA0AA: "tail.prepush", 0xFFA0AC: "tail.postpush", 0xFFA09E: "tail.holdexit",
}


def line_of(v):
    if 0xE0 <= v <= 0xEA:
        return v - 0xE0
    if v >= 0x1E5:
        return v - 0x1E5 + 11
    return v + 38


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--frames", default=None)
    a = ap.parse_args()
    ev = list(csv.DictReader(open(a.csv)))
    lo = hi = None
    if a.frames:
        lo, hi = map(int, a.frames.split("-"))
    per = collections.defaultdict(list)
    for e in ev:
        f = int(e["frame"])
        if lo is not None and not (lo <= f < hi):
            continue
        addr = int(e["addr"], 16); L = line_of(int(e["v"], 16)); d = int(e["data"], 16)
        rn = e["range"]; rw = e["rw"]
        if rn == "fm":
            if rw == "W" and d & 0x8000:
                per[f].append((L, "FM.RAISE"))
        elif rn == "comm0":
            if rw == "W" and d == 0x2020:
                per[f].append((L, "POST.comm0"))
        elif rn == "irq4":
            per[f].append((L, "GAME.IRQ4"))
        elif rn == "miss":
            if rw == "W":
                per[f].append((L, "GAME.MISS"))
        elif rw == "W":
            per[f].append((L, NAMES.get(addr, f"w{addr:06x}")))
    bounds = collections.defaultdict(list)
    for f in sorted(per):
        seq = per[f]
        # first occurrence of each name in this vint (the shim runs once)
        seen = {}
        for L, n in seq:
            if n not in seen:
                seen[n] = L
        print(f"vint {f}: " + "  ".join(f"{n}@{L}" for L, n in seq if seen.get(n) == L and n not in ("GAME.IRQ4",)) +
              ("  IRQ4@%d" % seen["GAME.IRQ4"] if "GAME.IRQ4" in seen else "  (no IRQ4)"))
        for n, L in seen.items():
            bounds[n].append(L)
    print()
    print("boundary            n   median  min  max   (lines from vblank start)")
    for n, Ls in sorted(bounds.items(), key=lambda kv: statistics.median(kv[1])):
        print(f"{n:18s} {len(Ls):3d}  {statistics.median(Ls):6.1f} {min(Ls):4d} {max(Ls):4d}")


if __name__ == "__main__":
    main()
