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
          f"dreq_incomplete={rd32(0x28000 + 17 * 4)} "
          f"push_aborts={rdmd16(0xB0F4)}")
    print(f"dirty bitmap now={rdmd16(0xB9FE):04X}")
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
    packed = rdmd16(0xB0F4)
    tail, strm = packed >> 8, packed & 0xFF
    dreq_scan = (tail - strm) & 0xFF
    verdict = ("OVERRUNS FRAME (>=262 wrapped) -> tail IS the load"
               if tail >= 250 or tail < strm else
               f"{262 - tail} lines of frame slack left")
    print(f"tail span: whole={tail} stream={strm} dreq+scan~{dreq_scan} "
          f"(frame=262) -> {verdict}")


if __name__ == "__main__":
    main()
