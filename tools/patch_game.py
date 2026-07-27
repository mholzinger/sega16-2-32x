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

# ---- runtime jump-ins to the displaced 0x400-0x807 region ----
# ROM 0x400-0x7FF holds the Sega security program (BIOS-verified); the
# game's own bytes there execute from a RAM copy at 0xFFB400 (+0xB000).
def expect(off, old):
    got = rom[off:off+len(old)]
    assert got == bytes(old), f"{off:#x}: {got.hex()} != {bytes(old).hex()}"

expect(0x0978, [0x61,0x00,0xFC,0x04]); rom[0x0978:0x097C] = bytes([0x4E,0xB8,0xB5,0x7E])  # bsrw 57e -> jsr (B57E).w
expect(0x0BD2, [0x60,0x00,0xFA,0x56]); rom[0x0BD2:0x0BD6] = bytes([0x4E,0xF8,0xB6,0x2A])  # braw 62a -> jmp (B62A).w
expect(0x1EEC, [0x4E,0xF9,0x00,0x00,0x05,0xBE]); rom[0x1EEE:0x1EF2] = bytes([0x00,0xFF,0xB5,0xBE])  # jmp 5be -> jmp FFB5BE
expect(0x1B5C6,[0x4E,0xF8,0x04,0x7E]); rom[0x1B5C6:0x1B5CA] = bytes([0x4E,0xF8,0xB4,0x7E])  # jmp 47e -> jmp (B47E).w

# ---- boot RAM copy: game [0x400,0x808) + pc-rel -> absolute fixups ----
boot = bytearray(rom[0x400:0x808])
def bfix(off, old, new):
    o = off - 0x400
    assert boot[o:o+len(old)] == bytes(old), f"boot {off:#x}: {boot[o:o+len(old)].hex()}"
    boot[o:o+len(new)] = bytes(new)

bfix(0x404, [0x60,0x00,0x26,0xA6], [0x4E,0xF8,0x2A,0xAC])  # braw 2aac -> jmp (2AAC).w
bfix(0x440, [0x41,0xFA,0x15,0x44], [0x41,0xF8,0x19,0x86])  # lea pc(1986) -> lea (1986).w
bfix(0x4D2, [0x61,0x00,0x10,0x04], [0x4E,0xB8,0x14,0xD8])  # bsrw 14d8 -> jsr (14D8).w
bfix(0x4DE, [0x61,0x00,0x0F,0xF8], [0x4E,0xB8,0x14,0xD8])
bfix(0x57A, [0x43,0xFA,0x17,0x8E], [0x43,0xF8,0x1D,0x0A])  # lea pc(1d0a) -> abs
bfix(0x5D2, [0x61,0x00,0x0D,0x32], [0x4E,0xB8,0x13,0x06])
bfix(0x60C, [0x41,0xFA,0x12,0x36], [0x41,0xF8,0x18,0x44])
bfix(0x658, [0x41,0xFA,0x11,0xEE], [0x41,0xF8,0x18,0x48])
bfix(0x662, [0x41,0xFA,0x16,0x76], [0x41,0xF8,0x1C,0xDA])
bfix(0x6B8, [0x61,0x00,0x0C,0x7E], [0x4E,0xB8,0x13,0x38])
bfix(0x728, [0x41,0xFA,0x11,0x26], [0x41,0xF8,0x18,0x50])
bfix(0x758, [0x61,0x00,0x06,0x86], [0x4E,0xB8,0x0D,0xE0])
bfix(0x75C, [0x61,0x00,0x0F,0x36], [0x4E,0xB8,0x16,0x94])
bfix(0x7F4, [0x41,0xFA,0x10,0x56], [0x41,0xF8,0x18,0x4C])
boot += bytes([0x4E,0xF8,0x08,0x08])                        # continuation: jmp (808).w

out = ROOT / 'tools' / 'patch_report.txt'
out.write_text(f"{hits} hardware refs patched\n\n" + "\n".join(report)
               + "\n\nWARNINGS (refs into shim RAM):\n" + "\n".join(warn) + "\n")
(ROOT / 'md_src' / 'game_body.bin').write_bytes(rom[0x808:0x40000])
(ROOT / 'md_src' / 'boot_copy.bin').write_bytes(boot)
print(f"{hits} refs patched, {len(warn)} warnings -> tools/patch_report.txt")
print("game_body.bin:", 0x40000 - 0x808, "bytes; boot_copy.bin:", len(boot), "bytes")
