#!/usr/bin/env python3
"""LOOP 9 per-window blit-vs-slave-wait split from an ares savestate.

Usage: wait_split.py [path-to-.bs1]   (default rom/PROBE_wait.bs1)
Needs a `make WAITSPLIT=1` rom.

THE QUESTION. k=0 and k=2 hold at 27.3-27.6 lines of pickup->restore
span across a light session and a heavy one. k=1 went 31.2 -> 35.2 and
owns 100% of the restores landing past vblank in both. k=1 blits 40
master rows to the others' 36, but four extra rows is a FIXED ~3 lines
(0.745 lines/row at the measured 47.34 us/row) — a penalty that DOUBLES
under load is not the row count.

Inside the span, k=1 differs from k=0/k=2 in only two ways: those 4
rows, and the master's wait on SYNC[2] for the slave to pick up the
preempt mailbox. The slave services that mailbox BETWEEN COMPOSE STRIPS,
so its pickup latency scales with scene complexity by construction.
apply_cram and copy_pages are k1-only but land AFTER the restore, so
they cannot be the term.

READING IT. If `blit` tracks row count (k=1 about 11% over k=0/k=2) and
`wait` is what blows up at k=1, the slave's pickup is the mechanism and
it is on the master's critical path for free. If `blit` itself is what
grows, the FB write cost is load-dependent — contention with the other
CPU and the VDP — and that is a different arc entirely.
"""
import struct
import sys

TICK_LINES = 46.0        # FRT ticks per scanline
ROWS = {0: 36, 1: 40, 2: 36}


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/PROBE_wait.bs1"
    st = open(path, "rb").read()
    sd = 0x23B

    def r(off):
        a = sd + off
        return struct.unpack(">I", swap16(st[a:a + 4]))[0]

    w = [r(0x28F20 + i * 4) for i in range(9)]
    if not sum(w[6:9]):
        print("no WAIT_SPLIT data — was this a `make WAITSPLIT=1` rom?")
        return

    print(f"STATE: {path}   BUILD: {r(0x28000 + 18 * 4):08x}\n")
    print("            n     blit      wait     total   rows   blit/row")
    for k in range(3):
        n = w[6 + k]
        if not n:
            continue
        b = w[k] / n / TICK_LINES
        t = w[3 + k] / n / TICK_LINES
        print(f"  k={k}  {n:5d}  {b:6.2f}ln  {t:6.2f}ln  {b + t:6.2f}ln  "
              f"{ROWS[k]:4d}   {b / ROWS[k]:.3f}ln")

    n0, n1, n2 = (w[6 + k] or 1 for k in range(3))
    b0, b1, b2 = w[0] / n0, w[1] / n1, w[2] / n2
    t0, t1, t2 = w[3] / n0, w[4] / n1, w[5] / n2
    base_b, base_t = (b0 + b2) / 2, (t0 + t2) / 2
    print(f"\n  k=1 excess over k=0/k=2:  blit {(b1 - base_b) / TICK_LINES:+.2f}ln"
          f"   wait {(t1 - base_t) / TICK_LINES:+.2f}ln")
    print(f"  4 extra rows predicts     blit +{4 * (base_b / 36) / TICK_LINES:.2f}ln")
    # PICKUP SOURCE (`make PICKUPSRC=1`). The compose launched at window k
    # covers band R((k+2)%3), so the compose running during k=1 is R2 —
    # slave rows 144-184 — and slave_window_k at k=1 blits 144-184. The
    # same rows. If the slave answers the mailbox from INSIDE the compose
    # at k=1, the wait is a data dependency (finish composing R2 before
    # you may blit it) and no number of service points can help it. That
    # is the distinction 7f got wrong.
    ps = [r(0x28F50 + i * 4) for i in range(6)]
    if sum(ps):
        print("\nPICKUP SOURCE      in-compose    idle-loop")
        for k in range(3):
            tot = ps[k] + ps[3 + k]
            print(f"  k={k}            {ps[k]:8d} {100.0 * ps[k] / max(tot, 1):5.1f}%"
                  f"  {ps[3 + k]:8d}")
        if ps[1] > 0 and ps[0] + ps[2] == 0:
            print("  -> DATA DEPENDENCY CONFIRMED: only k=1 picks up mid-compose,"
                  " and it is composing the very rows it is told to blit.")
        elif sum(ps[0:3]) == 0:
            print("  -> no mid-compose pickups anywhere: the compose always "
                  "finishes first, so the k=1 wait is NOT a dependency.")

    if t1 > base_t * 1.5 and (t1 - base_t) > (b1 - base_b):
        print("  -> THE SLAVE'S PICKUP IS THE TERM. The master is waiting on "
              "SYNC[2], not blitting.")
    elif b1 - base_b > 1.5 * 4 * (base_b / 36):
        print("  -> THE BLIT ITSELF GROWS beyond its row count: FB write cost "
              "is load-dependent, not a fixed floor.")


if __name__ == "__main__":
    main()
