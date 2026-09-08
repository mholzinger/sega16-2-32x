#!/usr/bin/env python3
"""Ares savestate DEADLOCK forensics (LOOP 6d).

    state_deadlock.py hung.bs1              # witness dump
    state_deadlock.py hung1.bs1 hung2.bs1   # THE decisive test

Two states captured ~10s apart answer the only question that matters
first: is the machine FROZEN or merely crawling? Every counter that
moves rules out a whole class of hang. If vints move but cycles do not,
the 68K is alive and the SH-2 side is stuck; if neither moves, the 68K
is blocked (historically: an unconditional FIFO group-write into an
undrained DREQ FIFO, or a COMM0 ack-spin whose acker died).

Prints the mailboxes both sides block on, so the stuck handshake names
itself. See LOOP.md iter3/iter4 for the two deadlocks this reproduces.
"""
import struct
import sys


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


class State:
    """One savestate: locates SDRAM (fixed offset) and MD RAM (by the
    TAS-thunk signature the shim installs at 0xFFB380)."""

    def __init__(self, path):
        self.path = path
        self.st = open(path, "rb").read()
        self.sd = 0x23B
        sig = swap16(bytes.fromhex("4a38c02050f8c0204e75"))
        i = self.st.find(sig)
        if i < 0:
            raise SystemExit(f"{path}: TAS thunk signature not found "
                             "— wrong or pre-shim state?")
        self.md = i - 0xB380

    def rd32(self, off):
        o = self.sd + off
        return struct.unpack(">I", swap16(self.st[o:o + 4]))[0]

    def rd16(self, off):
        o = self.sd + off
        return struct.unpack(">H", swap16(self.st[o:o + 2]))[0]

    def md16(self, off):
        o = self.md + off
        return struct.unpack(">H", swap16(self.st[o:o + 2]))[0]

    def diag(self, n):
        return self.rd32(0x28000 + n * 4)

    def sync(self, n):
        return self.rd16(0x28800 + n * 2)

    def counters(self):
        """The liveness set — anything that moves proves that side runs."""
        return {
            "MD vint entries (B0F0)":  self.md16(0xB0F0),
            "MD windows done (B0F2)":  self.md16(0xB0F2),
            "MD gate skips  (B0FC)":   self.md16(0xB0FC),
            "SH2 k1 cycles  (DIAG9)":  self.diag(9),
            "SH2 blit skips (DIAG7)":  self.diag(7),
            "SH2 deferrals  (DIAG13)": self.diag(13),
            "SH2 dreq_incmp (DIAG17)": self.diag(17),
        }


def witnesses(s):
    print(f"--- {s.path} ---")
    print(f"BUILD: {s.diag(18):08x}")
    for k, v in s.counters().items():
        print(f"  {k:26s} = {v}")
    # The handshakes each side blocks on.
    hv = s.md16(0xB0FE)
    v = hv >> 8
    # Preempt-blit timeout counters (builds >= 1152c7d1). THIS, not the
    # mailbox snapshot, is the evidence: a bounded wait that expired.
    tp, te = s.diag(21), s.diag(22)
    if tp or te:
        print(f"  !! preempt-blit TIMEOUTS pickup={tp} echo={te} — the "
              "slave missed the mailbox (pre-1152c7d1: unrecoverable hang)")
    else:
        print("  preempt-blit timeouts = 0 (slave answered every time)")
    print("  SYNC[0..5] (master<->slave mailbox) = "
          + " ".join(f"{s.sync(i):04X}" for i in range(6)))
    # NOT A HANG SIGNATURE. SYNC[4] posted with SYNC[2]/SYNC[5] still 0
    # looks like "master stuck waiting on the slave", and it is NOT: the
    # master spends ~64 of its 241 handler lines in exactly that wait, so
    # a savestate lands there routinely. Measured live — a state showing
    # it went on to run 3661 more vints. Judge by the DELTA and the
    # timeout counters above, never by this snapshot.
    print(f"    SYNC[0] cmd / [1] echo: slave is {'BUSY on a command'
          if s.sync(0) else 'idle'}"
          f"; preempt-blit SYNC[4]={s.sync(4):04X} echo SYNC[5]={s.sync(5):04X}")
    print(f"  HV at last vint = {hv:04X} (V={v:02X}, "
          f"{'accept' if 0xDF <= v <= 0xE2 else 'REJECT'}; gate DF..E2)")
    packed = s.md16(0xB0F4)
    tot, win = packed >> 8, packed & 0xFF
    if tot < win:
        tot += 256
    print(f"  worst handler: total={tot} window={win} tail={tot - win} "
          f"(frame=262)")
    print(f"  dirty tile bitmap (B9FE) = {s.md16(0xB9FE):04X}")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    a = State(sys.argv[1])
    witnesses(a)
    if len(sys.argv) < 3:
        print("\nOnly one state given. Capture a SECOND ~10s later and "
              "re-run with both — frozen vs crawling is the first fork.")
        return
    b = State(sys.argv[2])
    print()
    witnesses(b)

    print("\n=== FROZEN OR CRAWLING? (delta between the two states) ===")
    if a.diag(18) != b.diag(18):
        print(f"  !! BUILD MISMATCH {a.diag(18):08x} vs {b.diag(18):08x} — "
              "these are different builds, so deltas are meaningless.\n"
              "     Capture BOTH states from the SAME running machine.")
        return
    ca, cb = a.counters(), b.counters()
    moved = []
    negative = False
    for k in ca:
        d = cb[k] - ca[k]
        flag = "MOVED" if d > 0 else ("frozen" if d == 0 else "WENT BACKWARDS")
        if d > 0:
            moved.append(k)
        if d < 0:
            negative = True
        print(f"  {k:26s} {ca[k]:>10} -> {cb[k]:>10}  ({d:+d}) {flag}")
    if negative:
        print("  !! a counter DECREASED — the second state is older than the"
              " first, or it is from a different run. Pass them in capture"
              " order.")
        return

    md_alive = any("MD" in k for k in moved)
    sh_alive = any("SH2" in k for k in moved)
    print()
    if md_alive and sh_alive:
        print("VERDICT: both sides RUNNING — not a deadlock. This is the "
              "cadence pathology (crawl), so read the reject rate and the "
              "handler span, not the mailboxes.")
    elif md_alive and not sh_alive:
        print("VERDICT: 68K alive, SH-2 side STUCK. Suspect the slave "
              "mailbox: a SYNC[0] command never echoed to SYNC[1], or a "
              "preempt blit posted to SYNC[4] never echoed to SYNC[5] — "
              "the master then spins in-window forever (FM stays 1).")
    elif sh_alive and not md_alive:
        print("VERDICT: SH-2 alive, 68K BLOCKED. Historically this is the "
              "DREQ FIFO: the 68K's group write (fifo[0]=s[n]) is "
              "unconditional, so an unarmed/undrained FIFO blocks it "
              "forever. Check that the master re-armed DMAC0 (dreq_rearm) "
              "and that pushes only happen on gate-accepted vints.")
    else:
        print("VERDICT: BOTH SIDES FROZEN — a mutual deadlock. The known "
              "shape: 68K blocked mid-DREQ-push while the master waits "
              "in-window on the slave. SYNC[] above names which half.")


if __name__ == "__main__":
    main()
