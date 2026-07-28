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

# ---- strip-blitter base idiom fix (movew clobbers the patched base) ----
# 0x258A: movel #0x400000(->0x852000),%d0 ; movew (a0)+,%d0 REPLACES the
# low word — the arcade relied on base low word 0000, our staging base
# has +0x2000 and lost it: every strip landed 2 pages low, spilling into
# the frame image (the long-standing "misassembled sky strips" artifact,
# present on main too). movew->ADDW (3018->D058): offsets are <=0xDFFF
# so 0x2000+off never carries.
expect(0x2590, [0x30, 0x18])
rom[0x2590:0x2592] = bytes([0xD0, 0x58])

# ---- text-writer base idiom fixes (same movew-clobber family) ----
# The text base 0x410000 -> 0xFF8000 has low word 0x8000; these sites
# build dests with movew into %d0 (relying on base low word 0000).
# movew -> addw preserves the +0x8000 (offsets <= 0xFFF: no carry).
TEXT_IDIOM = [
    (0x1B4CC, 0x3039, 0xD079),   # movew 0x1bf56,%d0
    (0x1B660, 0x3039, 0xD079),   # movew 0x1bf06,%d0
    (0x1B95C, 0x303C, 0xD07C),   # movew #imm,%d0 (attract text rows)
    (0x1B964, 0x303C, 0xD07C),
    (0x1B96E, 0x303C, 0xD07C),
    (0x1B97C, 0x303C, 0xD07C),
    (0x1B986, 0x303C, 0xD07C),
    (0x1BAE8, 0x301B, 0xD05B),   # movew (a3)+,%d0
]
for off, old, new in TEXT_IDIOM:
    expect(off, [old >> 8, old & 0xFF])
    struct.pack_into('>H', rom, off, new)

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
# Full 256KB image executing at 0x900000+ (banked 0x900000 window,
# bank 3 -> cart 0x300000): every confirmed ROM-space reference gets +0x900000. Starts
# from a FRESH copy of the source ROM plus the HW passes ONLY — the
# displacement machinery (stash bfixes, jump-ins) must NOT be applied,
# so this section rebuilds those patches' preconditions itself.
REBASE = 0x900000

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
        elif mn in ('movel', 'pea') and re.fullmatch(r'%a[0-7]', dst):
            pass                             # pointer into address register
        elif mn == 'movel' and re.match(r'%(a[0-7]|fp)@\(\d+\)$', dst):
            pass                             # pointer into object field
            # (register-indirect dst: the @(2) handler and @(36) sprite-
            # definition slots — the latter is consumed as DATA, no
            # thunk covers it; dropping this rule made the demo player
            # invisible. ABSOLUTE dsts stay excluded: those were the
            # BCD score-award mailboxes.)
        else:
            skipped_imm.append(f"{off:06X}: {v:08X} | {ctx}")
            continue
    else:
        continue
    struct.pack_into('>I', hrom, off, v + REBASE)
    reb_report.append(f"R {off:06X}: {v:08X} -> {v + REBASE:08X} | {ctx}")
    reb += 1

# spawn-script region 0x1D2DC-0x1D520 (data; excluded from operand
# scan): TWO-LEVEL layout discovered in burn-down catch #3 — a per-
# round META-TABLE of table pointers (consumed at 0xD80E: lea 0x1d32a;
# movea.l (a0,d0*4),a0) plus stride-12 record lists with the handler
# long at +8 (walker at 0xD842: movea.l (8,A0),A1; jsr (A1)).
# 1) meta entries = longs pointing INSIDE the region -> rebase;
# 2) walk each pointed-to record list, rebasing +8 handlers until the
#    record shape breaks.
spawn_meta, meta_end = 0x1D32A, 0x1D33E     # 5 per-round table pointers
walk_cap = 0x1E000
meta_targets = set()
for a in range(spawn_meta, meta_end - 3, 4):
    v = struct.unpack_from('>I', orig_rom, a)[0]
    if 0x1D300 <= v < walk_cap:
        struct.pack_into('>I', hrom, a, v + REBASE)
        reb += 1
        reb_report.append(f"R {a:06X}: spawn meta {v:08X} -> {v + REBASE:08X}")
        meta_targets.add(v)
for tbl in sorted(meta_targets):
    a = tbl
    while a + 12 <= walk_cap:
        # STRICT record shape: the documented spawn record carries the
        # 0x0040 marker word at +6 (see DATA_EXCLUDE note). Walking on
        # "plausible handler" alone over-ran table ends and rebased
        # spawn PARAMS — the field-reported red-silhouette mis-spawns.
        if struct.unpack_from('>H', orig_rom, a)[0] == 0xFFFF:
            break                            # camX terminator
        if struct.unpack_from('>H', orig_rom, a + 6)[0] not in (0x0000, 0x0040):
            break                            # not a spawn record
        h = struct.unpack_from('>I', orig_rom, a + 8)[0]
        if not (0x100 <= h < 0x40000) or (h & 1):
            break
        if struct.unpack_from('>I', hrom, a + 8)[0] == h:   # not yet done
            struct.pack_into('>I', hrom, a + 8, h + REBASE)
            reb += 1
            reb_report.append(
                f"R {a + 8:06X}: spawn handler {h:08X} -> {h + REBASE:08X}")
        a += 12

# jump tables of ABSOLUTE code pointers (lea %pc@(tbl); movea.l (A0,D0);
# jmp (A0) dispatch idiom — harvested from the disassembly; the word-
# OFFSET variant of the idiom self-heals and needs nothing). First one
# found the hard way: the mode dispatcher at 0x26DC sent the boot to
# un-rebased 0x1F80 (MAME trace hunt.tr line 7545).
REBASE_TABLES = [(0x26DC, 8), (0x6D70, 8), (0x6D90, 12), (0x92F0, 6),
                 (0xF556, 5), (0x17E24, 5), (0x1A076, 9)]
# 0x6D70/0x6D90 extents are HARD-BOUNDED: 0x6DC0+ is a WORD index
# table; the old 21/13 extents pair-read it as longs and injected
# +0x90 into an index word (round-2 attract jsr-to-zero crash).
tbl_offs = set()
for start, n in REBASE_TABLES:
    for k in range(n):
        tbl_offs.add(start + k * 4)

# HANDLER-POINTER TABLES (burn-down catch #2, attract object spawner):
# the object system stores handlers from pc-lea'd pointer tables
# (lea %pc@(tbl),%aN ... movel %aN@...,%xx@(2)). The pc-lea itself
# self-heals in the high copy; the TABLE CONTENT (absolute code
# pointers) does not. Harvest: every pc-lea within 10 instructions
# before a memory-sourced store into an object handler slot (@(2)),
# table extent = consecutive plausible code pointers.
insns_l = []
for line in (ROMS / 'prog68k.asm').read_text().splitlines():
    m = re.match(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S.*)$', line)
    if m:
        insns_l.append((int(m.group(1), 16), m.group(2).strip()))
for i, (ia, it) in enumerate(insns_l):
    m = re.match(r'movel %(a[0-7]|fp)@.*,%(?:a[0-7]|fp)@\(2\)$', it)
    if not m:
        continue
    src = m.group(1)
    for j in range(max(0, i - 10), i):
        lm = re.search(r'lea %pc@\(0x([0-9a-f]+)\),%' + src + r'\b',
                       insns_l[j][1])
        if not lm:
            continue
        tbl = int(lm.group(1), 16)
        n = 0
        while tbl + n * 4 + 4 <= 0x40000:
            v = struct.unpack_from('>I', orig_rom, tbl + n * 4)[0]
            if not (0x100 <= v < 0x40000) or (v & 1):
                break
            n += 1
        if n:
            reb_report.append(f"HARVEST handler table {tbl:06X} x{n} "
                              f"(store at {ia:06X})")
            for k in range(n):
                tbl_offs.add(tbl + k * 4)

# GLOBAL LEA-TABLE SWEEP (burn-down catch #5 — ares field probe): DATA-
# read pointers were the remaining miss class. MAME hid them (it maps
# the cart at address 0 even at RV=0, so un-rebased low reads silently
# return correct bytes); ares maps the adapter region there and the
# reads return junk — the RLE tile loader decompressed a constant and
# tiled the whole screen with one garbage pattern. Root every lea
# (pc-relative or absolute) whose target looks like a pointer table
# (>=3 plausible even ROM pointers); recursive expansion below chases
# nesting. Odd/even data pointers both count here: these are READ
# pointers, not jump targets.
def entry_class(v):
    """1 = ROM pointer (rebase), 2 = runtime/HW address (pass), 0 = stop."""
    if 0x100 <= v < 0x40000:
        return 1
    a = v & 0xFFFFFF
    if (v >> 24) == 0 and (0xFF0000 <= a <= 0xFFFFFF or
                           0x400000 <= a < 0x450000 or
                           0x840000 <= a < 0x860000):
        return 2
    return 0

def table_extent_any(at):
    n = 0
    rom_ptrs = 0
    while at + n * 4 + 4 <= 0x40000:
        v = struct.unpack_from('>I', orig_rom, at + n * 4)[0]
        c = entry_class(v)
        if c == 0:
            break
        if c == 1:
            rom_ptrs += 1
        n += 1
    return n if rom_ptrs >= 2 else 0
lea_roots = 0
for ia, it in insns_l:
    lm = re.search(r'lea (?:%pc@\()?0x([0-9a-f]+)\)?,%a[0-7]', it)
    if not lm:
        continue
    tbl = int(lm.group(1), 16)
    if not (0x100 <= tbl < 0x40000):
        continue
    n = table_extent_any(tbl)
    if n >= 3:
        lea_roots += 1
        reb_report.append(f"LEA-TABLE {tbl:06X} x{n} (lea at {ia:06X})")
        for k in range(n):
            tbl_offs.add(tbl + k * 4)
reb_report.append(f"(lea-table sweep: {lea_roots} roots)")
# RECURSIVE expansion (burn-down catch #4: two-level dispatch at
# 0x4C00 — table of SUB-TABLE pointers at 0x6DA0, each sub-table =
# handler pointers): any harvested entry that POINTS AT >=3 further
# plausible pointers is itself a table — harvest transitively.
def looks_like_table(at):
    n = 0
    while at + n * 4 + 4 <= 0x40000:
        v = struct.unpack_from('>I', orig_rom, at + n * 4)[0]
        if not (0x100 <= v < 0x40000) or (v & 1):
            break
        n += 1
    return n
work = sorted(tbl_offs)
seen_tbl = set()
while work:
    a = work.pop()
    if a in seen_tbl:
        continue
    seen_tbl.add(a)
    v = struct.unpack_from('>I', orig_rom, a)[0]
    if not (0x100 <= v < 0x40000):
        reb_report.append(f"SKIP-T {a:06X}: {v:08X} not a ROM pointer")
        continue
    if not (v & 1):
        n = looks_like_table(v)
        if n >= 3:
            reb_report.append(f"NESTED table {v:06X} x{n} (via entry {a:06X})")
            for k in range(n):
                work.append(v + k * 4)
tbl_offs = seen_tbl
for a in sorted(tbl_offs):
    v = struct.unpack_from('>I', orig_rom, a)[0]
    if not (0x100 <= v < 0x40000):
        continue                    # runtime addrs pass through untouched
    if struct.unpack_from('>I', hrom, a)[0] == v:       # not yet rebased
        struct.pack_into('>I', hrom, a, v + REBASE)
        reb += 1
        reb_report.append(f"R {a:06X}: table entry {v:08X} -> {v + REBASE:08X}")

# RUNTIME-HARVESTED handler values (tools/harvested_handlers.txt: every
# distinct object-handler pointer observed live in the WORKING RV=1
# build across attract + coined gameplay — see TOOLKIT.md). For each
# value, every data occurrence in ROM gets rebased; occurrences the
# operand pass already changed are skipped automatically.
hh = ROOT / 'tools' / 'harvested_handlers.txt'
if hh.exists():
    hvals = set(int(x, 16) for x in hh.read_text().split() if x.strip())
    # VALUE BLACKLIST: harvested values whose byte patterns are common
    # DATA idioms — patching every ROM occurrence corrupts records.
    # 0x10000 ([0001][0000] word pairs: 72 hits, all inside movement/
    # spawn records — one broke the intro camera pan speed 0x0001 ->
    # 0x0091, shifting the whole cutscene cast 144px). 0x102-0x106:
    # the known packed-map collision family (also region-excluded).
    # A REAL handler with these values is normalized at call time by
    # the dispatcher thunks, so dropping them here is strictly safe.
    hvals -= {0x10000, 0x102, 0x104, 0x106}
    nh = 0
    # occurrences only BELOW the packed asset streams (round map srcs
    # start at 0x29E00): harvested values like 0x102/0x106 byte-collide
    # inside compressed data — 79 such hits desynced the round-1 RLE
    # (the sky-patch corruption).
    for off in range(0, 0x28000 - 3, 2):
        v = struct.unpack_from('>I', orig_rom, off)[0]
        if v not in hvals:
            continue
        if struct.unpack_from('>I', hrom, off)[0] != v:
            continue                        # already rebased by another pass
        struct.pack_into('>I', hrom, off, v + REBASE)
        reb += 1
        nh += 1
        reb_report.append(f"R {off:06X}: harvested handler {v:08X} -> "
                          f"{v + REBASE:08X}")
    reb_report.append(f"(harvested-handler pass: {nh} sites from "
                      f"{len(hvals)} live values)")

# STRIDE-RECORD asset tables (mixed word+long records the 4-stride
# sweeps can't see). 0x1CE2: the tilemap RLE loader's per-round table,
# 8 records of [bank.w][srcptr.l] — THE source of the ares garbage
# tilemap (unrebased srcptr -> 68K read junk from adapter space).
for start, cnt, stride, poff in [(0x1CE2, 8, 6, 2)]:
    for k in range(cnt):
        a = start + k * stride + poff
        v = struct.unpack_from('>I', orig_rom, a)[0]
        if 0x100 <= v < 0x40000 and struct.unpack_from('>I', hrom, a)[0] == v:
            struct.pack_into('>I', hrom, a, v + REBASE)
            reb += 1
            reb_report.append(f"R {a:06X}: stride-rec {v:08X} -> {v + REBASE:08X}")

# strip-blitter movew->addw (see low-copy pass; same fix)
assert hrom[0x2590:0x2592] == bytes([0x30, 0x18])
hrom[0x2590:0x2592] = bytes([0xD0, 0x58])

# text-writer movew->addw family (see low-copy pass)
for off, old, new in TEXT_IDIOM:
    assert struct.unpack_from('>H', hrom, off)[0] == old
    struct.pack_into('>H', hrom, off, new)

# SKIPPED-IMMEDIATE overrides (audited by trace): #imm values that ARE
# pointers despite landing in data registers. 0x3C92: movel #0x255E0,
# %d2 = sprite frame-table base consumed via adda.l D2 in the sprite
# list builder (poison-rig catch: adda.w (A0) address error at 0x3EA4).
for off, v in [(0x3C92, 0x255E0)]:
    assert struct.unpack_from('>I', hrom, off)[0] == v, hex(off)
    struct.pack_into('>I', hrom, off, v + REBASE)
    reb += 1
    reb_report.append(f"R {off:06X}: immediate override {v:08X} -> {v + REBASE:08X}")

# LOW-VECTOR reads (census: 4 sites): the game reads its own vector
# table as CONSTANTS (addal 0x0,%a4 adds vector[0]=0xFFFFFF00 = -0x100
# — a 68K size trick). At RV=0 MAME/ares serve DIFFERENT adapter bytes
# at low addresses -> ares-only position skew (the "P1 spawns at P2"
# field bug). Redirect to the high copy's authentic arcade vectors.
for off in (0x14932, 0x1493E, 0x307A, 0xABC4):
    v = struct.unpack_from('>I', hrom, off)[0]
    assert v == 0, f"{off:#x}: {v:#x}"
    struct.pack_into('>I', hrom, off, REBASE)
    reb += 1
    reb_report.append(f"R {off:06X}: low-vector ref 0 -> {REBASE:08X}")

# the one abs.w code ref that can't hold 0x94xxxx: thunk via shim RAM
assert hrom[0x1B5C6:0x1B5CA] == bytes([0x4E, 0xF8, 0x04, 0x7E])
hrom[0x1B5C6:0x1B5CA] = bytes([0x4E, 0xF8, 0xB3, 0xF0])   # jmp (FFFFB3F0).w
# (shim installs the thunk: 0xFFB3F0 = jmp 0x90047E.l)

# DISPATCHER NORMALIZATION (the total fix for handler-pointer data we
# can't enumerate): the two proven consumption funnels get re-pointed
# at shim-RAM thunks that add +0x900000 to any low handler pointer at
# call time. Static table rebases become best-effort; anything missed
# is corrected here.
#   0x39A8 object dispatcher: movea.l (2,A6),A0 ; jsr (A0)
#   0xD842 spawn walker:      movea.l (8,A0),A1 ; jsr (A1)
assert hrom[0x39A8:0x39AE] == bytes([0x20, 0x6E, 0x00, 0x02, 0x4E, 0x90])
hrom[0x39A8:0x39AE] = bytes([0x4E, 0xB8, 0xB3, 0xA0, 0x4E, 0x71])
assert hrom[0xD842:0xD848] == bytes([0x22, 0x68, 0x00, 0x08, 0x4E, 0x91])
hrom[0xD842:0xD848] = bytes([0x4E, 0xB8, 0xB3, 0xC0, 0x4E, 0x71])
# (shim installs the thunks at 0xFFB3A0 / 0xFFB3C0)

# DATA-POINTER NORMALIZATION (wpcatch.lua finds, intro window): two
# readers consume STORED table pointers whose values live below the
# 0x28000 harvest bound (packed-art byte collisions forbid rebasing
# them statically). Normalize at use time via shim thunks:
#   0xDBA8 spawn walker (intro cast list @0xDD46):
#          movea.l (0x24,A6),A4 -> jsr (FFFFB340).w
#   0x30D0 palette-cycle streamer (glow scripts @0x1A78E):
#          movea.l (2,A5),A0    -> jsr (FFFFB360).w
assert hrom[0xDBA8:0xDBAC] == bytes([0x28, 0x6E, 0x00, 0x24])
hrom[0xDBA8:0xDBAC] = bytes([0x4E, 0xB8, 0xB3, 0x40])
assert hrom[0x30D0:0x30D4] == bytes([0x20, 0x6D, 0x00, 0x02])
hrom[0x30D0:0x30D4] = bytes([0x4E, 0xB8, 0xB3, 0x60])

# Palette-cycle LAUNCH TABLE at 0x1A6FA ([id.w][script.l] x3): the
# harvest pass caught entry 1 (0x1A70E) but missed entries 2/3, whose
# 0x0001A78E script pointers stayed low — the launcher reads the
# script header DIRECTLY (0x1A6E0: move.w (2,A1)) before the thunked
# streamer ever runs, so it read poison: no lightning/red-text glow.
for off in (0x1A704, 0x1A70A):
    v = struct.unpack_from('>I', hrom, off)[0]
    assert v == 0x0001A78E, f"{off:#x}: {v:#x}"
    struct.pack_into('>I', hrom, off, v + REBASE)
    reb += 1
    reb_report.append(f"R {off:06X}: palette launch entry -> {v + REBASE:08X}")

# REBASE_EXCLUDE: regions no pass may touch (word tables whose pairs
# forge valid-looking pointers — heuristics cannot reject them).
# 0x6DC0-0x6DCA: round-index WORD table of the two-level dispatcher
# (pair 0x00030004 passed every classifier; +0x90 in an index word
# crashed round-2 attract with jsr-to-zero).
# 0x1AD10-0x1AD18: cutscene record words [0000][0E10][0000][1C20] —
# the 0x0000 words forged longs 0x00000E10/0x00001C20 and took +0x90,
# shifting the intro camera AND the player spawn X by 144px (the
# field "P1 spawns at P2" bug). Values are frame counts, not handlers.
# 0x7358-0x73A0: record fields 0x102/0x104/0x106 (the known collision
# value family) misread as harvested handler longs. If any ever IS a
# handler, the B3A0/B3C0 call-time thunks normalize it anyway.
# 0xEC32-0xEC46 / 0xECAC-0xECB0: object animation records ("harvested
# handler" 0x0003000A / 0x00010000 — collision-family values, data).
# (NOT 0x1989E-0x198AE: those ascending longs 0x0000F0EE..0x0003F1EF
# are a REAL per-round pointer table — reverting them stalled boot.)
# 0x1AD10-0x1AD34: BYTE RAMP 0E 10 1C 20 23 28 ... (animation easing
# curve for the intro emergence arc) — ascending byte pairs forged
# ascending "pointer" longs and fooled the lea-table sweep. Also the
# source of the +0x90 camera/spawn skew (first two longs).
REBASE_EXCLUDE = [(0x6DC0, 0x6DCA), (0x1AD10, 0x1AD34), (0x7358, 0x73A0),
                  (0xEC32, 0xEC46), (0xECAC, 0xECB0)]
for lo, hi_ in REBASE_EXCLUDE:
    if hrom[lo:hi_] != orig_rom[lo:hi_]:
        hrom[lo:hi_] = orig_rom[lo:hi_]
        reb_report.append(f"REVERT {lo:06X}-{hi_:06X}: excluded region restored")

(ROOT / 'md_src' / 'game_high.bin').write_bytes(hrom[:0x40000])
(ROOT / 'tools' / 'rebase_report.txt').write_text(
    f"{reb} refs rebased (+{REBASE:#x})\n\n" + "\n".join(reb_report)
    + "\n\nSKIPPED long immediates (burn-down candidates):\n"
    + "\n".join(skipped_imm) + "\n")
print(f"game_high.bin: {reb} refs rebased, {len(skipped_imm)} immediates "
      "skipped -> tools/rebase_report.txt")
