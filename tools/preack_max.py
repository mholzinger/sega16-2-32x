#!/usr/bin/env python3
"""LOOP 10 — why is the MD handler 8 lines off the end of the frame?

Needs a `-DPREACK_MAX` build (rom/PROBE_preack.32x). NEVER SHIP it.

Usage: preack_max.py [path-to-.bs1]     (default rom/PROBE_preack.bs1)

state_health reports `worst handler total=254 window/ack=211 (frame=262)`
and nothing about WHICH term produced it. This splits the master's
pre-ack window — blit, the SYNC[2] pickup wait, the SYNC[5] echo wait,
copy_pages, apply_cram — and keeps the breakdown of the single WORST
window, plus a tail histogram.

The histogram is the point. A lone freak window at 254 lines and a fat
tail sitting near 250 need opposite responses, and the max alone cannot
tell them apart. If windows over 200 lines are a handful out of thousands,
the margin is a sampling artifact of a long run and the mean is healthy.
If they are percent-scale, the port is genuinely running at the edge.

~46 FRT ticks per scanline (1 tick = 1.37us, 262 lines at 60Hz).
"""
import struct
import sys
import os

TPL = 46.0


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_preack.bs1"
    if not os.path.exists(path):
        print(f"no such state: {path}")
        return 1
    st = open(path, "rb").read()
    sd = 0x23B
    if st.find(swap16(bytes.fromhex("4a38c02050f8c0204e75"))) < 0:
        print("TAS thunk signature not found — wrong/old build state?")
        return 1

    def rd(off):
        return struct.unpack(">I", swap16(st[sd + off:sd + off + 4]))[0]

    p = [rd(0x28D80 + i * 4) for i in range(15)]
    tot, n = p[8], p[9]
    if not n:
        print("no windows recorded — is this a PREACK_MAX build?")
        return 1
    print(f"STATE: {path}   BUILD: {rd(0x28000 + 18 * 4):08x}")
    print(f"windows={n}   mean pre-ack={tot / n / TPL:.1f} lines"
          f"   worst={p[0] / TPL:.1f} lines (k={p[6]}, window #{p[7]})")
    # p[1] is the diag_add(5) term, which is "blit+preempt" — it ALREADY
    # CONTAINS the SYNC[2] pickup wait p[2]. Subtract, or the two double
    # count and the remainder goes negative.
    print("\nWORST WINDOW, by term:")
    blit_only = p[1] - p[2]
    named = blit_only + p[2] + p[3] + p[4] + p[5]
    for lab, v in (("blit (excl. wait)", blit_only),
                   ("SYNC[2] pickup wait", p[2]),
                   ("SYNC[5] echo wait", p[3]), ("copy_pages", p[4]),
                   ("apply_cram", p[5])):
        print(f"  {lab:<22}{v / TPL:>8.1f} lines"
              f"{100 * v / max(p[0], 1):>7.1f}%")
    print(f"  {'everything else':<22}{(p[0] - named) / TPL:>8.1f} lines"
          f"{100 * (p[0] - named) / max(p[0], 1):>7.1f}%")
    print("\nTAIL — how often does the window get near the frame?")
    for lab, v in ((">150 lines", p[10]), (">175 lines", p[11]),
                   (">200 lines", p[12]), (">225 lines", p[13]),
                   (">250 lines", p[14])):
        print(f"  {lab:<12}{v:>8}  ({100 * v / n:.3f}% of windows)")
    if p[12] * 1000 < n:
        print("\n-> windows over 200 lines are under 0.1%: the 8-line margin "
              "is the TAIL of a long run, not the operating point.")
    else:
        print("\n-> windows over 200 lines are NOT rare: the port is running "
              "near the frame edge and this needs fixing, not explaining.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
