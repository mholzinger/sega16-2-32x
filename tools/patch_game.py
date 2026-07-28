#!/usr/bin/env python3
"""Patch altbeast 68K hardware references for the 32X memory model.

Scans the binary for even-aligned BE32 values in System 16B hardware
ranges and rewrites them to MD-visible shadow addresses. Every hit is
cross-referenced against the disassembly and reported for review.

Output: md_src/game_body.bin (bytes 0x400-0x3FFFF, cart-ready) and
tools/patch_report.txt.
"""
import struct, re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'

DATA_EXCLUDE = [
    (0x1986, 0x1998),   # boot mapper table (raw bytes, not code)
    (0x1D2DC, 0x1D520), # level event/spawn script: 12-byte records
                        # [camX.w][p1.w][p2.w][0x0040.w][handler.l] — the
                        # 0x0040,0x0000 word pairs LOOK like 0x00400000
                        # tile-RAM operands in objdump's misdisassembly;
                        # patching one corrupted a round-1 spawn (red blob)
]

def remap(v):
    if v >> 24:
        return None                     # real abs.l operands have high byte 0
    a = v & 0xFFFFFF
    if 0x400000 <= a < 0x410000:            # tile RAM -> 32X FB staging area
        # 0x840000 FB window + 0x12000 (past the 0x11A00 display image).
        # Game-touched span is 0x0000-0xBFFF (pages 0-11); 0xE000+ would
        # exceed the 128KB window -> warn.
        if (a & 0xFFFF) >= 0xE000:
            return 'WARN'
        return 0x852000 + (a & 0xFFFF)
    if 0x410000 <= a < 0x420000:            # text RAM -> shadow
        return 0xFF8000 + (a & 0xFFF)
    if 0x440000 <= a < 0x450000:            # sprite RAM -> FB staging (2KB mirror)
        # 0x840000 FB window + 0x1E000; game writes are all word/long
        # (verified: movel/movew upload loop at 0x2B1E), so the FB
        # zero-byte-drop hazard doesn't apply.
        return 0x85E000 + (a & 0x7FF)
    if 0x840000 <= a < 0x850000:            # palette -> FB staging (4KB mirror)
        # All game palette writes are word/long (verified: zero moveb
        # sites), so the FB zero-byte-drop hazard doesn't apply. Read
        # in-window by the SH-2 — no COMM streaming needed.
        return 0x85F000 + (a & 0xFFF)
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
orig_rom = bytes(rom)               # pristine copy for the rebase build
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
    # ONLY patch class A: the disassembly shows this exact value as an
    # instruction operand. Class B (pointer-ish neighbours) is UNSAFE — it
    # patches mid-instruction false positives. Real example that crashed the
    # game: 0x16BD0 held 0x0044422E (the 0x44 displacement of a move.w plus
    # the next opcode); class B rewrote it to a sprite-RAM shadow, turning
    # move.w D0,(0x44,A6) into move.w D0,(0xFF,A6) -> odd EA -> address error.
    # Genuine data-table pointers into HW RAM are rare (tables point at ROM
    # graphics, not hardware); dropping class B is the safe trade.
    # objdump prints abs.l EA operands in hex but IMMEDIATE operands in
    # DECIMAL (moveal #4261713,%a1 = #0x410751), so confirm either form.
    # The decimal match is exact, so coincidental byte patterns that
    # disassemble as a different immediate (oriw #0,%d1) still fail it.
    hexval = f"0x{v & 0xFFFFFF:x}"
    decval = f"#{v & 0xFFFFFF}"
    if hexval not in ctx and decval not in ctx:
        cls = 'B' if pointerish(off) else 'C'
        report.append(f"SKIP-{cls} {off:06X}: {v:08X} not a confirmed operand | {ctx}")
        continue
    struct.pack_into('>I', rom, off, new)
    report.append(f"A {off:06X}: {v:08X} -> {new:08X}   | {ctx}")
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

# ---- boot-region CONSTANT reads (0x400-0x807 is displaced by the blob) ----
# Any absolute reference that READS a game byte in 0x400-0x807 must hit the
# RAM copy at 0xFFB400 (+0xB000), not the blob now occupying native 0x400.
# WRITES to that range are skipped: on the arcade those addresses are program
# ROM (writes are no-ops), so leaving them pointing at cart ROM is faithful.
# Driven by the disassembly so we rewrite real operands, not table bytes.
READ_OPS = {'cmpiw','cmpib','cmpil','tstw','tstb','tstl',
            'moveb','movew','movel','pea','lea','btst'}
BOOT_DELTA = 0xFFB400 - 0x400
redir = 0
for line in (ROMS / 'prog68k.asm').read_text().splitlines():
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\w+)\s+(.*)', line)
    if not m:
        continue
    iaddr = int(m.group(1), 16)
    ibytes = bytes.fromhex(m.group(2).replace(' ', ''))
    op, operand = m.group(3), m.group(4).strip()
    if op not in READ_OPS:
        continue
    mt = re.search(r'0x([0-9a-f]+)', operand)
    if not mt:
        continue
    v = int(mt.group(1), 16)
    if not (0x400 <= v < 0x808):
        continue
    # for move, the boot addr must be the SOURCE (before the comma)
    if op.startswith('move') and ',' in operand:
        if f"0x{v:x}" not in operand.split(',', 1)[0]:
            continue
    tgt = v + BOOT_DELTA                     # 0xFFB400-based
    # locate the operand encoding by scanning the ROM after the opcode word
    # (objdump wraps long instructions across lines, so ibytes is unreliable).
    done = False
    for k in range(2, 12, 2):                 # abs.l (4 bytes, high word 0)
        if struct.unpack_from('>I', rom, iaddr + k)[0] == v:
            struct.pack_into('>I', rom, iaddr + k, tgt)
            done = True; break
    if not done:
        for k in range(2, 8, 2):              # abs.w (2 bytes; sign-extends)
            if struct.unpack_from('>H', rom, iaddr + k)[0] == (v & 0xFFFF):
                struct.pack_into('>H', rom, iaddr + k, tgt & 0xFFFF)
                done = True; break
    if done:
        redir += 1
        report.append(f"REDIR {iaddr:06X}: {op} 0x{v:x} -> 0x{tgt:06X} | {line.strip()}")
report.append(f"\n{redir} boot-region constant reads redirected to RAM copy\n")

def expect(off, old):
    got = rom[off:off+len(old)]
    assert got == bytes(old), f"{off:#x}: {got.hex()} != {bytes(old).hex()}"

# ---- tilemap RLE even-byte pass -> word writes (FB staging fix) ----
# Tile RAM now lives in the 32X framebuffer, where BYTE writes of ZERO are
# dropped by the hardware (MAME mega32x.cpp m68k_dram_w, "tested on real hw").
# The game loads tilemaps with two RLE passes: 0x16BE writes all EVEN (high)
# bytes, then 0x16DE writes all ODD (low) bytes; both streams contain zeros.
# Fix: make pass 1 write WORDS of (value<<8)|0x00 — word writes always land,
# so every odd byte is pre-zeroed; pass 2's zero writes are then no-ops on
# already-zero bytes and only its nonzero writes matter. Sole call site pair
# at 0x16AE/0x16B2 (bsrw 16be; bsrw 16de) — verified no other callers.
#   16cc: 1419  moveb (a1)+,d2   (kept)
#   16ce: 1082  moveb d2,(a0)    -> E14A  lslw #8,d2
#   16d0: 5488  addql #2,a0      -> 30C2  movew d2,(a0)+
#   16d6: 51c8 fff6 dbf d0,16ce  -> 51c8 fff8 dbf d0,16d0  (skip the reshift)
expect(0x16CC, [0x14,0x19,0x10,0x82,0x54,0x88])
rom[0x16CE:0x16D2] = bytes([0xE1,0x4A,0x30,0xC2])
expect(0x16D6, [0x51,0xC8,0xFF,0xF6]); rom[0x16D8:0x16DA] = bytes([0xFF,0xF8])

# ---- runtime jump-ins to the displaced 0x400-0x807 region ----
# ROM 0x400-0x7FF holds the Sega security program (BIOS-verified); the
# game's own bytes there execute from a RAM copy at 0xFFB400 (+0xB000).
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

# ==== UNPAIR REBASE (design v2, NOTES.md): game_high.bin ====
# Full 256KB image executing at 0x940000 (banked 0x900000 window,
# bank 2): every confirmed ROM-space reference gets +0x940000. Starts
# from a FRESH copy of the source ROM plus the HW passes ONLY — the
# displacement machinery (stash bfixes, jump-ins) must NOT be applied,
# so this section rebuilds those patches' preconditions itself.
REBASE = 0x940000

hrom = bytearray(orig_rom)          # pristine source image
# re-apply the HW staging/IO patches to the fresh copy by replaying
# the class-A sites recorded in `report` (offset -> new value)
for line in report:
    if not line.startswith('A '):
        continue
    off_s, rest = line[2:].split(':', 1)
    off = int(off_s, 16)
    new = int(rest.strip().split('->')[1].strip().split()[0], 16)
    struct.pack_into('>I', hrom, off, new)
# RLE even-byte word-write patch (same bytes as the low copy)
hrom[0x16CE:0x16D2] = bytes([0xE1, 0x4A, 0x30, 0xC2])
hrom[0x16D8:0x16DA] = bytes([0xFF, 0xF8])

reb_report = []
reb = 0
skipped_imm = []
for off in range(0, 0x40000 - 3, 2):
    v = struct.unpack_from('>I', hrom, off)[0]
    if not (0x100 <= v < 0x40000):
        continue
    ctx = ''
    for back in range(0, 10, 2):
        if off - back in asm:
            ctx = asm[off - back]
            break
    itext = ctx.split('\t')[-1].strip()      # text after the bytes column
    mn = itext.split()[0] if itext.split() else ''
    if mn.startswith('.'):
        continue                             # data-as-code lines
    hexval = f"0x{v:x}"
    decval = f"#{v}"
    if hexval in ctx:
        pass                                 # abs.l EA / jsr / jmp / lea
    elif decval in ctx:
        dst = itext.split(",")[-1].strip()
        if mn.startswith('movea') or mn.startswith('cmpa'):
            pass                             # pointer by type (load/compare)
        elif mn in ('cmpil', 'cmpl') and v >= 0x1000:
            pass                             # pointer-field compare (fp@(36)
                                             # handler slots, dN-held ptrs)
        elif mn in ('movel', 'pea') and (re.fullmatch(r'%a[0-7]', dst)
                                         or '@' in dst
                                         or dst.startswith('0x')):
            pass                             # pointer store
        else:
            skipped_imm.append(f"{off:06X}: {v:08X} | {ctx}")
            continue
    else:
        continue
    struct.pack_into('>I', hrom, off, v + REBASE)
    reb_report.append(f"R {off:06X}: {v:08X} -> {v + REBASE:08X} | {ctx}")
    reb += 1

# spawn-script handler pointers (data; excluded from operand scan)
for a in range(0x1D2DC, 0x1D520, 12):
    v = struct.unpack_from('>I', hrom, a + 8)[0]
    if v < 0x40000:
        struct.pack_into('>I', hrom, a + 8, v + REBASE)
        reb += 1
        reb_report.append(f"R {a + 8:06X}: spawn handler {v:08X} -> {v + REBASE:08X}")

# the one abs.w code ref that can't hold 0x94xxxx: thunk via shim RAM
assert hrom[0x1B5C6:0x1B5CA] == bytes([0x4E, 0xF8, 0x04, 0x7E])
hrom[0x1B5C6:0x1B5CA] = bytes([0x4E, 0xF8, 0xB3, 0xF0])   # jmp (FFFFB3F0).w

(ROOT / 'md_src' / 'game_high.bin').write_bytes(hrom[:0x40000])
(ROOT / 'tools' / 'rebase_report.txt').write_text(
    f"{reb} refs rebased (+{REBASE:#x})\n\n" + "\n".join(reb_report)
    + "\n\nSKIPPED long immediates (burn-down candidates):\n"
    + "\n".join(skipped_imm) + "\n")
print(f"game_high.bin: {reb} refs rebased, {len(skipped_imm)} immediates "
      "skipped -> tools/rebase_report.txt")
