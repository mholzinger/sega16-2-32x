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


if __name__ == "__main__":
    main()
