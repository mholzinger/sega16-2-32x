#!/usr/bin/env python3
"""LOOP 10 — does the sprite list land short BECAUSE the master acks late?

Mike's hypothesis, and it is a causal chain worth testing before building
anything against it:

    master picks up the window late  ->  ack lands late
      ->  the MD's tail is squeezed  ->  its sprite-list push truncates
      ->  landed < plen              ->  SPR_SNAP keeps last cycle's list
      ->  sprites pop in and out     ->  "sprite flicker, not screen flicker"

Needs a `-DDREQ_CORR` build (rom/PROBE_dreqcorr.32x). NEVER SHIP it.
Usage: dreq_corr.py [path-to-.bs1]   (default rom/PROBE_dreqcorr.bs1)

Every harvest is bucketed by the PREVIOUS window's pre-ack span — which
is precisely the time the MD did not have. If the chain is real, the
short lands sit behind visibly longer windows than the complete ones.

READ THE SEPARATION, NOT THE VERDICT LINE. A few lines of difference in
the means is not a mechanism; it is two overlapping distributions. What
would confirm the chain is a clear gap, ideally with the short-land mean
above the complete-land MAX. What would kill it is the two means landing
on top of each other — in which case the truncation has nothing to do
with when the master acks, and `state_health`'s own advice applies
instead: push_aborts==0 means SPLIT THE PACKET.

~46 FRT ticks per scanline.
"""
import struct
import sys
import os

TPL = 46.0


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_dreqcorr.bs1"
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

    d = [rd(0x28D80 + i * 4) for i in range(10)]
    n_ok, n_short = d[0], d[1]
    n = n_ok + n_short
    if not n:
        print("no harvests recorded — is this a DREQ_CORR build?")
        return 1
    print(f"STATE: {path}   BUILD: {rd(0x28000 + 18 * 4):08x}")
    print(f"harvests={n}   short={n_short} ({100 * n_short / n:.1f}%)")
    ok_mean = d[2] / n_ok / TPL if n_ok else 0.0
    sh_mean = d[3] / n_short / TPL if n_short else 0.0
    print("\nPREVIOUS window's pre-ack span, by outcome:")
    print(f"  complete land   n={n_ok:<7} mean={ok_mean:6.1f} lines"
          f"   max={d[4] / TPL:6.1f}")
    print(f"  SHORT land      n={n_short:<7} mean={sh_mean:6.1f} lines"
          f"   max={d[5] / TPL:6.1f}")
    if n_short:
        print(f"\nshortfall: mean {d[6] / n_short:.0f} words, "
              f"worst {d[7]} words")
    if not (n_ok and n_short):
        print("\n-> one bucket is empty; no comparison possible.")
        return 0
    sep = sh_mean - ok_mean
    print(f"\nseparation: {sep:+.1f} lines")
    if sh_mean > d[4] / TPL:
        print("-> short lands sit above the COMPLETE-land maximum. The chain "
              "is real: fix the ack latency and the sprite list follows.")
    elif sep > 5.0:
        print("-> short lands do follow longer windows, but the "
              "distributions overlap. Contributing cause, not sole cause.")
    else:
        print("-> the two are indistinguishable. Ack latency is NOT why the "
              "list truncates; take state_health's advice and SPLIT THE "
              "PACKET (push_aborts==0 means the 68K pushed it all).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
