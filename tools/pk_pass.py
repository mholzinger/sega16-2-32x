#!/usr/bin/env python3
"""LOOP 10 — which slave pass does the master's SYNC[2] wait sit behind?

Needs a `make ... -DPICKUP_PASS` build (rom/PROBE_pkpass.32x). NEVER SHIP
that rom: the probe adds a per-window read and two SDRAM accumulates.

Usage: pk_pass.py [path-to-.bs1]        (default rom/PROBE_pkpass.bs1)

The master stamps the pass the SLAVE was running at the moment it posted
the blit command in SYNC[4], then charges the whole SYNC[2] pickup wait
to that pass. So this answers the question LOOP 9 got wrong by measuring
"is the slave inside a compose" (busy is not collided): it reports where
the wait actually GOES, per pass, with a mean.

Read it against the striping rule. Four of the six passes are cut into
12-row strips with a mailbox service point between them, so their means
should be small. `cat1 WHOLE` is the one deliberate exception — LOOP 7f
striped it, hit its target metric and lost the play pass on sprite
artifacts, so it runs whole-band and uninterruptible. If the wait is
concentrated there, candidate (a) has its target; if it is spread evenly,
the band is simply too big and the split has to be structural.

MAME CANNOT ANSWER THIS. Measured there, 99.9% of pickups find the slave
already idle — MAME does not model framebuffer write cost, so the compose
finishes inside the gap that ares blows through. Run it on ares.
"""
import struct
import sys
import os
import glob

PASSES = ["idle", "BG opaque", "FG cat0", "sprites",
          "cat1 WHOLE", "text", "other"]
TICKS_PER_LINE = 46.0          # FRT ticks/scanline. 1 tick = 1.37us,
                               # 262 lines at 60Hz = 63.6us/line -> 46.4.
                               # m_main.c's own vblank test uses 1748 =
                               # 38 lines x 46. An earlier 21.4 here was
                               # wrong and overstated every span by 2.17x.


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_pkpass.bs1"
    if not os.path.exists(path):
        print(f"no such state: {path}")
        return 1
    st = open(path, "rb").read()
    age = os.path.getmtime(path)
    newer = [r for r in glob.glob("rom/*.32x") if os.path.getmtime(r) > age]
    print(f"STATE: {path}")
    if newer:
        print(f"  !! this state is OLDER than {len(newer)} rom(s) — check "
              f"the BUILD below is the pkpass rom you ran.")

    sd = 0x23B
    sig = swap16(bytes.fromhex("4a38c02050f8c0204e75"))
    if st.find(sig) < 0:
        print("TAS thunk signature not found — wrong/old build state?")
        return 1

    def rd32(off):
        return struct.unpack(">I", swap16(st[sd + off:sd + off + 4]))[0]

    print(f"BUILD: {rd32(0x28000 + 18 * 4):08x}")
    ticks = [rd32(0x28D80 + i * 4) for i in range(7)]
    counts = [rd32(0x28D80 + (8 + i) * 4) for i in range(7)]
    tot, ntot = sum(ticks), sum(counts)
    if ntot == 0:
        print("no pickups recorded — is this a PICKUP_PASS build?")
        return 1
    print(f"total pickup wait {tot} ticks over {ntot} pickups "
          f"({tot / ntot:.1f} ticks = {tot / ntot / TICKS_PER_LINE:.2f} "
          f"lines mean)")
    print(f"{'pass':<12}{'n':>7}{'n%':>7}{'ticks':>10}{'wait%':>7}"
          f"{'mean':>8}{'lines':>8}")
    for i, name in enumerate(PASSES):
        if not counts[i]:
            continue
        mean = ticks[i] / counts[i]
        print(f"{name:<12}{counts[i]:>7}{100 * counts[i] / ntot:>6.1f}%"
              f"{ticks[i]:>10}{100 * ticks[i] / max(tot, 1):>6.1f}%"
              f"{mean:>8.1f}{mean / TICKS_PER_LINE:>8.2f}")
    busy = ntot - counts[0]
    print(f"\npickups landing in a RUNNING pass: {busy}/{ntot} "
          f"({100 * busy / ntot:.1f}%)")
    if busy:
        bt = tot - ticks[0]
        print(f"they carry {100 * bt / max(tot, 1):.1f}% of all wait, "
              f"mean {bt / busy / TICKS_PER_LINE:.2f} lines each")
    return 0


if __name__ == "__main__":
    sys.exit(main())
