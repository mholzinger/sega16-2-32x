#!/usr/bin/env python3
"""Patch altbeast 68K hardware references for the 32X memory model.

Scans the binary for even-aligned BE32 values in System 16B hardware
ranges and rewrites them to MD-visible shadow addresses. Every hit is
cross-referenced against the disassembly and reported for review.

Output: md_src/game_body.bin (bytes 0x400-0x3FFFF, cart-ready) and
tools/patch_report.txt.
"""
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'

DATA_EXCLUDE = [(0x1986, 0x1998)]   # boot mapper table (raw bytes, not code)

def remap(v):
    if v >> 24:
        return None                     # real abs.l operands have high byte 0
    a = v & 0xFFFFFF
    if 0x400000 <= a < 0x410000:            # tile RAM -> cart hole bit-bucket
        return 0x300000 + (a & 0xFFFF)
    if 0x410000 <= a < 0x420000:            # text RAM -> shadow
        return 0xFF8000 + (a & 0xFFF)
    if 0x440000 <= a < 0x450000:            # sprite RAM -> shadow (2KB mirror)
        return 0xFF9800 + (a & 0x7FF)
    if 0x840000 <= a < 0x850000:            # palette -> shadow (4KB mirror)
        return 0xFFA000 + (a & 0xFFF)
    if 0x3F0000 <= a < 0x400000:            # tile bank regs -> shadow words
        return 0xFFB040 + (a & 0xF)
    if 0xC40000 <= a < 0xC44000:            # I/O -> mailbox bytes
        unit = (a >> 12) & 3                # c4X00Y
        low = a & 0xFFF
        if low >= 0x10:
            return 'WARN'                   # data straddle or unknown port
        return 0xFFB000 + (unit << 4) + low
    return None

rom = bytearray((ROMS / 'prog68k.bin').read_bytes())
asm = {}
for line in (ROMS / 'prog68k.asm').read_text().splitlines():
    if ':\t' in line:
        addr = line.split(':', 1)[0].strip()
        try:
            asm[int(addr, 16)] = line.strip()
        except ValueError:
            pass

def pointerish(off):
    # neighbor longs (aligned +/-4) that also look like hw/rom pointers
    n = 0
    for d in (-8, -4, 4, 8):
        o = off + d
        if 0 <= o < len(rom) - 3:
            w = struct.unpack_from('>I', rom, o)[0]
            a = w & 0xFFFFFF
            if w >> 24 == 0 and (0x3F0000 <= a < 0x450000 or
                                 0x840000 <= a < 0x850000 or
                                 0xC40000 <= a < 0xC44000 or
                                 0x400 <= a < 0x40000):
                n += 1
    return n

report = []
hits = 0
for off in range(0, len(rom) - 3, 2):
    v = struct.unpack_from('>I', rom, off)[0]
    if any(lo <= off < hi for lo, hi in DATA_EXCLUDE):
        continue
    new = remap(v)
    if new is None:
        continue
    if new == 'WARN':
        report.append(f"SKIP {off:06X}: odd IO-like value {v:08X}")
        continue
    # class A: disassembly shows this value as an instruction operand
    ctx = ''
    for back in range(0, 10, 2):
        if off - back in asm:
            ctx = asm[off - back]
            break
    hexval = f"0x{v & 0xFFFFFF:x}"
    cls = 'A' if hexval in ctx else ('B' if pointerish(off) else 'C')
    if cls == 'C':
        report.append(f"SKIP-C {off:06X}: {v:08X} isolated, not patched | {ctx}")
        continue
    struct.pack_into('>I', rom, off, new)
    report.append(f"{cls} {off:06X}: {v:08X} -> {new:08X}   | {ctx}")
    hits += 1

# collision check: game refs into our shim RAM area 0xFF0000-0xFFBFFF
warn = []
for off in range(0, len(rom) - 3, 2):
    v = struct.unpack_from('>I', rom, off)[0]
    a = v & 0xFFFFFF
    if 0xFF0000 <= a < 0xFFC000 and (v >> 24) in (0x00, 0xFF):
        warn.append(f"{off:06X}: ref {v:08X} into shim RAM range")
# abs.w sign-extended refs 0xFF8000-0xFFBFFF would collide too
for line in (ROMS / 'prog68k.asm').read_text().splitlines():
    for pat in ('0xffff8', '0xffff9', '0xffffa', '0xffffb'):
        if pat in line:
            warn.append("ASM " + line.strip())

out = ROOT / 'tools' / 'patch_report.txt'
out.write_text(f"{hits} hardware refs patched\n\n" + "\n".join(report)
               + "\n\nWARNINGS (refs into shim RAM):\n" + "\n".join(warn) + "\n")
(ROOT / 'md_src' / 'game_body.bin').write_bytes(rom[0x400:0x40000])
print(f"{hits} refs patched, {len(warn)} warnings -> tools/patch_report.txt")
print("game_body.bin:", 0x40000 - 0x400, "bytes")
