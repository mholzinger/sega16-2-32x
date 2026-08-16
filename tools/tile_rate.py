#!/usr/bin/env python3
"""LOOP 11 step 1 — how often does the game dirty a tilemap page?

Needs `make TILERATE=1` (rom/PROBE_tilerate.32x). NEVER SHIP that rom.
Usage: tile_rate.py [path-to-.bs1]   (default rom/PROBE_tilerate.bs1)

THE PIVOT RESTS ON THIS NUMBER. The arcade game's tile RAM is staged in
the 32X framebuffer, which is the only reason the 68K needs FM, which is
the only reason we hand the framebuffer back every frame, which is why
the 68K stalls and why we blit at all. If the tilemap changes rarely, it
can be STREAMED instead — like the sprite list and palette already are —
and the whole chain unwinds.

MAME attract said 4.2% of cycles dirty, 0.22 pages copied per cycle. But
Mike's complaint is "framerate in the first half of level gameplay", and
a scroll-heavy stretch is exactly where a tilemap COULD churn. Attract is
not the answer; this is.

READ IT AS A GO/NO-GO. Low single-digit percent => stream it, the pivot
proceeds. Tens of percent, or pages-per-cycle above ~3 (the per-window
copy budget), => the tilemap is genuinely live, streaming it is a
different and much larger problem, and the pivot needs rethinking BEFORE
any code moves. Either answer is worth the run.
"""
import struct
import sys
import os


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_tilerate.bs1"
    if not os.path.exists(path):
        print(f"no such state: {path}")
        return 1
    st = open(path, "rb").read()
    sd = 0x23B
    if st.find(swap16(bytes.fromhex("4a38c02050f8c0204e75"))) < 0:
        print("TAS thunk signature not found — wrong/old build state?")
        return 1

    def d(i):
        o = sd + 0x28000 + i * 4
        return struct.unpack(">I", swap16(st[o:o + 4]))[0]

    cycles, copied, dirty, pending = d(9), d(54), d(55), d(56)
    if not cycles:
        print("no cycles recorded — wrong state?")
        return 1
    if not (copied or dirty or pending):
        print("all three counters are zero — is this a TILERATE=1 build? "
              "(a zero from the wrong rom looks exactly like a great result)")
        return 1
    print(f"STATE: {path}   BUILD: {d(18):08x}")
    print(f"cycles={cycles}")
    print(f"  cycles with ANY dirty page : {dirty:>8}  ({100*dirty/cycles:.1f}%)")
    print(f"  pages pending per cycle    : {pending/cycles:>8.2f}")
    print(f"  pages copied per cycle     : {copied/cycles:>8.2f}")
    pct = 100.0 * dirty / cycles
    print()
    if pct < 10 and copied / cycles < 3:
        print("-> GO. The tilemap is nearly static; streaming it is far "
              "cheaper than the blit it would let us delete.")
    else:
        print("-> STOP AND RETHINK. The tilemap is genuinely live at this "
              "rate; streaming it is a much larger problem than LOOP11.md "
              "assumes. Do not move code until this is understood.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
