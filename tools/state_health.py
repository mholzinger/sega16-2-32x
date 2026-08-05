#!/usr/bin/env python3
"""Ares savestate health report (see TOOLKIT: savestate forensics).

Usage: state_health.py [path-to-.bs1]   (default rom/s16.bs1)

Locates SDRAM (probe-relocated) and MD RAM (TAS-thunk signature) in
the BST1 state and prints the pipeline health counters: build ID,
cadence (vints per k1 cycle), V-gate rejects, blit skips, deferrals,
DREQ health, dirty bitmap.
"""
import struct
import sys


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/s16.bs1"
    st = open(path, "rb").read()
    # STALENESS GUARD. The default path silently reads an OLD state when
    # you meant the probe rom you just launched — the readings then look
    # plausible and identical, and the only tell is the BUILD hash. Name
    # the file, and shout if a rom is newer than the state being read.
    import glob
    import os
    age = os.path.getmtime(path)
    newer = [r for r in glob.glob("rom/*.32x") if os.path.getmtime(r) > age]
    print(f"STATE: {path}")
    if newer:
        print(f"  !! WARNING: this state is OLDER than {len(newer)} rom(s) "
              f"({', '.join(os.path.basename(r) for r in sorted(newer)[:4])}"
              f"{'...' if len(newer) > 4 else ''}).")
        print("     If you meant a probe rom, pass ITS state explicitly, "
              "e.g. state_health.py rom/PROBE_spin0.bs1 — and check the "
              "BUILD line below matches the rom you ran.")
    sd = 0x23B
    sig = swap16(bytes.fromhex("4a38c02050f8c0204e75"))
    i = st.find(sig)
    if i < 0:
        print("TAS thunk signature not found — wrong/old build state?")
        return
    md = i - 0xB380

    def rd32(off):
        return struct.unpack(">I", swap16(st[sd + off:sd + off + 4]))[0]

    def rdmd16(off):
        return struct.unpack(">H", swap16(st[md + off:md + off + 2]))[0]

    vints = rdmd16(0xB0F0)
    cycles = rd32(0x28000 + 9 * 4)
    skips = rd32(0x28000 + 7 * 4)
    gates = rdmd16(0xB0FC)
    print(f"BUILD: {rd32(0x28000 + 18 * 4):08x}")
    print(f"vints={vints} cycles={cycles} -> "
          f"vints/cycle={vints / max(cycles, 1):.2f} (healthy ~3)")
    print(f"V-gate rejects={gates} ({100.0 * gates / max(vints, 1):.1f}% "
          f"of vints)")
    print(f"blit skips={skips} ({100.0 * skips / max(cycles, 1):.1f}% "
          f"of cycles)")
    print(f"deferrals={rd32(0x28000 + 13 * 4)} "
          f"dreq_incomplete={rd32(0x28000 + 17 * 4)}")
    print(f"dirty bitmap now={rdmd16(0xB9FE):04X}")
    # PREEMPT-BLIT TIMEOUTS (builds >= 1152c7d1). The master's SYNC[2]
    # pickup and SYNC[5] echo waits used to be unbounded: if the slave
    # failed to answer, the master spun forever with FM=1 and took the
    # 68K down with it — a dead machine. They are bounded now, so a
    # NONZERO count here is the hang, caught and survived: it says the
    # slave missed the preempt mailbox and the frame dropped instead.
    # Zero on a healthy run, so this is the hang localiser.
    t_pick = rd32(0x28000 + 21 * 4)
    t_echo = rd32(0x28000 + 22 * 4)
    if t_pick or t_echo:
        print(f"!! preempt-blit TIMEOUTS: pickup(SYNC2)={t_pick} "
              f"echo(SYNC5)={t_echo} — the slave missed the preempt "
              f"mailbox. Pre-1152c7d1 this was an unrecoverable HANG.")
    else:
        print(f"preempt-blit timeouts=0 (pickup/echo both answered)")
    # HV at the last blit-phase vint entry (md_main 0xFFB0FE, written
    # BEFORE the gate check). The gate accepts V in 0xDF..0xE2 (MAME-
    # tuned). If V clusters just past 0xE2 with the handler otherwise
    # fast -> the gate is mis-calibrated for ares (fires at ares's
    # natural H-int V), NOT a latency overrun. If V is deep in-frame
    # (0x00..0x20 / 0xF0+) -> genuine handler overrun. THE decider.
    hv = rdmd16(0xB0FE)
    v = hv >> 8
    gated = "REJECT" if (v < 0xDF or v > 0xE2) else "accept"
    print(f"HV at last vint={hv:04X} (V={v:02X} -> {gated}; "
          f"gate accepts DF..E2)")
    # ITER5 tail probe (0xFFB0F4): high byte = max whole-tail span, low
    # byte = max stream-section span, both in scanlines (post-window ->
    # end of the per-vint tail: DREQ push + palette scan + COMM stream).
    # Frame = 262. A whole-tail span at/over 262 on ares => the handler
    # overruns the frame -> the V-gate reject band. MAME floor ~227/120.
    # MAX total handler span (high byte) + the window/ack span of that
    # same worst vint (low byte), scanlines. tail = total - window.
    packed = rdmd16(0xB0F4)
    total, win = packed >> 8, packed & 0xFF
    real_total = total if total >= win else total + 256  # wrap past 256
    tail = (real_total - win) & 0x1FF
    dom = "WINDOW/ack-spin" if win > tail else "TAIL (DREQ+scan+stream)"
    verdict = ("LAPS THE FRAME -> entry drifts -> reject band"
               if real_total >= 262 else f"{262 - real_total} lines margin")
    print(f"worst handler: total={real_total} window/ack={win} tail={tail} "
          f"(frame=262) dominated by {dom} -> {verdict}")


if __name__ == "__main__":
    main()
