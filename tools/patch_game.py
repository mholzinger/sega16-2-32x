#!/usr/bin/env python3
"""Patch altbeast 68K hardware references for the 32X memory model.

Scans the binary for even-aligned BE32 values in System 16B hardware
ranges and rewrites them to MD-visible shadow addresses. Every hit is
cross-referenced against the disassembly and reported for review.

Output: md_src/game_body.bin (bytes 0x400-0x3FFFF, cart-ready) and
tools/patch_report.txt.
"""
import os, struct, re
from pathlib import Path

# LOOP 20: FBSPR=1 in the environment (plumbed from `make FBSPR=1`)
# sends the game's sprite upload back to FB staging — see the remap.
FBSPR = bool(os.environ.get('FBSPR'))
FBTEXT = bool(os.environ.get('FBTEXT'))
K2FREE = bool(os.environ.get('K2FREE'))   # LOOP 24: reg/rowscroll split
R60 = bool(os.environ.get('R60'))         # REBUILD: same splits as K2FREE
K2FREE = K2FREE or R60
# LOOP 22: PAL32=1 regenerates the palette dirty thunks at 32-word
# granularity (64-bit bitmap as 8 BYTES at 0xFFBA00, code after) so the
# shim can ship dirty 32-word BLOCKS instead of 256-word region pairs.
PAL32 = bool(os.environ.get('PAL32'))
PAL_APOST = bool(os.environ.get('PAL_APOST'))
# LOOP 23: FMGATE=1 gates the game's MAIN-LOOP framebuffer writers on
# FM at their subsystem entry points, so the 68K never spins for the
# SH-2 window (the rte trampoline in md_start.s raises FM AFTER the
# game's vint upload). Derivation: tools/fmgate_derive.py.
FMGATE = bool(os.environ.get('FMGATE'))
# LOOP 27 q4: TXTWRAM=1 takes the text writers the frame timeline shows
# at the TOP of the game's pass (tools/frame_timeline.py: the credit line
# at pass line +2, the health bar at +25 — each spins ~60-100 lines on
# the SH-2's FM span) OFF the framebuffer. Per writer (tools/game_<GAME>.py
# TABLES['TXT_WRAM_WRITERS']): its text base operand is rebased to the
# WRAM text mirror 0xFF8000 (glyph area, unused since K2FREE), the entry
# gets a MARK thunk (dirty byte, plus the live text offset for writers
# that add a variable), and its FM gate/span are dropped; shared glyph
# loop heads it calls skip the FM wait for a WRAM destination. The shim
# copies each dirty writer's footprint into FB text staging at FM=0
# before the raise (md_main.c TXT_WRAM, table in fmgate_tab.h). Sega's
# code untouched beyond the rebase; derivation in docs/log/LOOP27.md 4.
TXTWRAM = bool(os.environ.get('TXTWRAM'))
FBXPEND = bool(os.environ.get('FBXPEND'))   # LOOP29 137: gate spin blasts the pending packet
GAMEGATE = bool(os.environ.get('GAMEGATE'))  # LOOP29 141: the game's frame release is ours
TXTMASK = bool(os.environ.get('TXTMASK'))    # LOOP29 147: text writers mark their 4-row group
TXTW_BASE = 0xFFB0C0       # 4 bytes per writer: [off word][dirty byte][pad]

ROOT = Path(__file__).resolve().parent.parent
GAME = os.environ.get('GAME', 'altbeast')
ROMS = ROOT / 'roms' / GAME
# Per-game tables (2026-09-05): the hand-derived site lists below are the
# US program's. tools/game_<GAME>.py may override any of them (a dict
# named TABLES); the US module is empty. The build fails loudly if a game
# module is missing so an underived title never ships silently.
import importlib.util as _ilu
_gp = ROOT / 'tools' / f'game_{GAME}.py'
if not _gp.exists():
    raise SystemExit(f'patch_game: no tools/game_{GAME}.py — derive the tables first')
_spec = _ilu.spec_from_file_location(f'game_{GAME}', _gp); _gm = _ilu.module_from_spec(_spec); _spec.loader.exec_module(_gm)
GT = getattr(_gm, 'TABLES', {})
def T(key):
    """Per-game table lookup: every program-specific offset comes from
    tools/game_<GAME>.py; a missing key is a derivation gap, not a default."""
    if key not in GT:
        raise SystemExit(f'patch_game: tools/game_{GAME}.py lacks TABLES[{key!r}] '
                         '— derive it (tools/game_derive.py) before building')
    return GT[key]

DATA_EXCLUDE = T('DATA_EXCLUDE')

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
    if 0x410000 <= a < 0x420000:            # text RAM
        # FBTEXT=1 (LOOP 20): straight into FB staging at 0x85F000 — the
        # slot FB_PAL vacated (its quarters copy no longer exists; the
        # DREQ palette pairs replaced it). The SH-2 captures the region
        # to TEXT_U each k1 and restores it into the fresh bank at the
        # flip, so sparse writes and read-backs stay coherent. Kills the
        # 256-word DREQ text chunks AND the 80-word regs/rowscroll
        # prefix (the S16 keeps its layer regs at text 0x740-0x7FF).
        if FBTEXT:
            # K2FREE (LOOP 24): the layer regs + rowscroll (text 0x740-
            # 0x7FF = byte offsets 0xE80+) are VINT-CONTEXT writes
            # (wpcatch_hv census: PCs 0x902AD8-0x902B18, every vint) and
            # the spin-kill removes the last FM=0 vint slot — an FB-staged
            # reg write would be DISCARDED at the source. Split them back
            # to the 0xFF8000 mirror, where the prefix-82 DREQ push reads
            # them (md_main reg/rowscroll loops); glyphs stay FB-staged
            # (census: every glyph writer sits inside the FMGATE spans —
            # main-loop, entry-gated, lands whenever FM=0). Boundary risk:
            # a LONG write straddling byte 0xE80 would split targets; the
            # census saw no writer within 0x40 bytes of the boundary.
            if K2FREE and (a & 0xFFF) >= 0xE80:
                return 0xFF8000 + (a & 0xFFF)
            return 0x85F000 + (a & 0xFFF)
        return 0xFF8000 + (a & 0xFFF)
    if 0x440000 <= a < 0x450000:            # sprite RAM
        # TWO TARGETS, selected by FBSPR=1 (LOOP 20).
        # History: originally FB staging (0x85E000); retired because the
        # game's vint upload crossed the FB window exactly while the
        # SH-2 blit owned the FB and ares/hardware DISCARD those MD
        # writes (savestate: 40/64 records torn — the broken-sprites
        # era). Rerouted to the 0xFF7000 MD RAM mirror + a DREQ push.
        # LOOP 20 re-audit: that hazard is EXTINCT under the current
        # window protocol — FM has ONE raise site, the 68K spins inside
        # the whole FM=1 span, and FM drops before every handler exit
        # including the spin timeout. Game code cannot run with FM=1,
        # so its FB-window writes always land now. The DREQ push this
        # reroute forced costs 48.6 of the 68K's ~64-line handler — the
        # single largest item in the pipeline — so FBSPR=1 sends the
        # upload back to FB staging for the SH-2 to read in place.
        if FBSPR:
            return 0x85E000 + (a & 0x7FF)
        return 0xFF7000 + (a & 0x7FF)
    if 0x840000 <= a < 0x850000:            # palette -> MD RAM mirror (4KB)
        # BANK-SKEW FIX: palette used to remap straight into FB staging
        # (0x85F000), but FB staging is per-bank — rows written while
        # the OTHER bank staged were zeros in the bank the SH-2
        # snapshot read (ares black actors; proven via savestate: the
        # group-12 S16 words existed in exactly ONE bank). Writes now
        # land in a stable MD RAM mirror; the shim vint copies the
        # mirror into the staging bank's FB_PAL in rotating quarters
        # (md_main.c), so every snapshot sees a complete palette.
        # Reads (fade RMW) hit real RAM — always coherent.
        return 0xFF9000 + (a & 0xFFF)
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
RLE = T('RLE_EVEN_PASS')
expect(RLE, [0x14,0x19,0x10,0x82,0x54,0x88])
rom[RLE+2:RLE+6] = bytes([0xE1,0x4A,0x30,0xC2])
expect(RLE+0xA, [0x51,0xC8,0xFF,0xF6]); rom[RLE+0xC:RLE+0xE] = bytes([0xFF,0xF8])

# ---- strip-blitter base idiom fix (movew clobbers the patched base) ----
# 0x258A: movel #0x400000(->0x852000),%d0 ; movew (a0)+,%d0 REPLACES the
# low word — the arcade relied on base low word 0000, our staging base
# has +0x2000 and lost it: every strip landed 2 pages low, spilling into
# the frame image (the long-standing "misassembled sky strips" artifact,
# present on main too). movew->ADDW (3018->D058): offsets are <=0xDFFF
# so 0x2000+off never carries.
SBM = T('STRIP_BLITTER_MOVEW')
expect(SBM, [0x30, 0x18])
rom[SBM:SBM+2] = bytes([0xD0, 0x58])

# ---- text-writer base idiom fixes (same movew-clobber family) ----
# The text base 0x410000 -> 0xFF8000 has low word 0x8000; these sites
# build dests with movew into %d0 (relying on base low word 0000).
# movew -> addw preserves the +0x8000 (offsets <= 0xFFF: no carry).
TEXT_IDIOM = T('TEXT_IDIOM')
for off, old, new in TEXT_IDIOM:
    expect(off, [old >> 8, old & 0xFF])
    struct.pack_into('>H', rom, off, new)

# ---- runtime jump-ins to the displaced 0x400-0x807 region ----
# ROM 0x400-0x7FF holds the Sega security program (BIOS-verified); the
# game's own bytes there execute from a RAM copy at 0xFFB400 (+0xB000).
# (offset, kind, target) from the game table; the RAM copy sits at
# 0xFFB000+target (abs.w sign-extends), so:
#   bsrw -> jsr (B000+t).w   braw -> jmp (B000+t).w
#   jmpl -> jmp 0xFFB000+t.l jmpw -> jmp (B000+t).w
for off, kind, tgt in T('BOOT_JUMPINS'):
    assert 0x400 <= tgt < 0x808, f"jump-in {off:#x}: target {tgt:#x} not in boot region"
    if kind == 'bsrw':
        expect(off, [0x61, 0x00] + list(struct.pack('>h', tgt - (off + 2))))
        rom[off:off+4] = struct.pack('>HH', 0x4EB8, 0xB000 + tgt)
    elif kind == 'braw':
        expect(off, [0x60, 0x00] + list(struct.pack('>h', tgt - (off + 2))))
        rom[off:off+4] = struct.pack('>HH', 0x4EF8, 0xB000 + tgt)
    elif kind == 'jmpl':
        expect(off, [0x4E, 0xF9] + list(struct.pack('>I', tgt)))
        struct.pack_into('>I', rom, off + 2, 0xFFB000 + tgt)
    elif kind == 'jmpw':
        expect(off, [0x4E, 0xF8] + list(struct.pack('>H', tgt)))
        struct.pack_into('>H', rom, off + 2, 0xB000 + tgt)
    elif kind == 'jsrl':
        expect(off, [0x4E, 0xB9] + list(struct.pack('>I', tgt)))
        struct.pack_into('>I', rom, off + 2, 0xFFB000 + tgt)
    elif kind == 'jsrw':
        expect(off, [0x4E, 0xB8] + list(struct.pack('>H', tgt)))
        struct.pack_into('>H', rom, off + 2, 0xB000 + tgt)
    else:
        raise SystemExit(f"jump-in {off:#x}: unknown kind {kind}")

# ---- boot RAM copy: game [0x400,0x808) + pc-rel -> absolute fixups ----
boot = bytearray(rom[0x400:0x808])
def bfix(off, old, new):
    o = off - 0x400
    assert boot[o:o+len(old)] == bytes(old), f"boot {off:#x}: {boot[o:o+len(old)].hex()}"
    boot[o:o+len(new)] = bytes(new)

for _o, _old, _new in T('BOOT_PCREL'):
    bfix(_o, _old, _new)
boot += bytes([0x4E,0xF8,0x08,0x08])                        # continuation: jmp (808).w

out = ROOT / 'tools' / 'patch_report.txt'
out.write_text(f"{hits} hardware refs patched\n\n" + "\n".join(report)
               + "\n\nWARNINGS (refs into shim RAM):\n" + "\n".join(warn) + "\n")
(ROOT / 'md_src' / 'game_body.bin').write_bytes(rom[0x808:0x40000])
(ROOT / 'md_src' / 'boot_copy.bin').write_bytes(boot)
# GAME_IRQ4 (2026-09-05): vector 0x70 -> 0x404 -> bra.w to the real handler;
# md_start.s jumps to the rebased copy (0x900000 + handler).
_disp = struct.unpack('>h', rom[0x406:0x408])[0]
_irq4 = 0x406 + _disp
(ROOT / 'md_src' / 'game_irq.h').write_text(
    f'/* generated by patch_game.py for {GAME} */\n'
    f'#define GAME_IRQ4 0x{0x900000 + _irq4:06X}\n'
    f'/* target of the one abs.w jmp the rebase cannot widen (ABSW_JMP) */\n'
    f'#define GAME_ABSW_JMP_TARGET 0x{0x900000 + T("ABSW_JMP")[1]:06X}\n'
    f'/* i8751 mailboxes in work RAM (MCU_BUSY / MCU_COINS / MCU_SND) */\n'
    f'#define GAME_MCU_BUSY  0x{T("MCU_BUSY"):06X}\n'
    f'#define GAME_MCU_COINS 0x{T("MCU_COINS"):06X}\n'
    f'#define GAME_MCU_SND   0x{T("MCU_SND"):06X}\n')
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
hrom[RLE+2:RLE+6] = bytes([0xE1, 0x4A, 0x30, 0xC2])
hrom[RLE+0xC:RLE+0xE] = bytes([0xFF, 0xF8])

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
spawn_meta, meta_end = T('SPAWN_META')       # per-round table pointers
walk_cap = T('SPAWN_CAP')
spawn_lo = T('SPAWN_LO')
meta_targets = set()
for a in range(spawn_meta, meta_end - 3, 4):
    v = struct.unpack_from('>I', orig_rom, a)[0]
    if spawn_lo <= v < walk_cap:
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
REBASE_TABLES = T('REBASE_TABLES')
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
hvals = set(T('HARVESTED_HANDLERS'))
if hvals:
    # VALUE BLACKLIST: harvested values whose byte patterns are common
    # DATA idioms — patching every ROM occurrence corrupts records.
    # 0x10000 ([0001][0000] word pairs: 72 hits, all inside movement/
    # spawn records — one broke the intro camera pan speed 0x0001 ->
    # 0x0091, shifting the whole cutscene cast 144px). 0x102-0x106:
    # the known packed-map collision family (also region-excluded).
    # A REAL handler with these values is normalized at call time by
    # the dispatcher thunks, so dropping them here is strictly safe.
    hvals -= set(T('HARVEST_BLACKLIST'))
    nh = 0
    # occurrences only BELOW the packed asset streams (round map srcs
    # start at 0x29E00): harvested values like 0x102/0x106 byte-collide
    # inside compressed data — 79 such hits desynced the round-1 RLE
    # (the sky-patch corruption).
    for off in range(0, T('HARVEST_BOUND') - 3, 2):
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
for start, cnt, stride, poff in T('STRIDE_TABLES'):
    for k in range(cnt):
        a = start + k * stride + poff
        v = struct.unpack_from('>I', orig_rom, a)[0]
        if 0x100 <= v < 0x40000 and struct.unpack_from('>I', hrom, a)[0] == v:
            struct.pack_into('>I', hrom, a, v + REBASE)
            reb += 1
            reb_report.append(f"R {a:06X}: stride-rec {v:08X} -> {v + REBASE:08X}")

# strip-blitter movew->addw (see low-copy pass; same fix)
assert hrom[SBM:SBM+2] == bytes([0x30, 0x18])
hrom[SBM:SBM+2] = bytes([0xD0, 0x58])

# text-writer movew->addw family (see low-copy pass)
for off, old, new in TEXT_IDIOM:
    assert struct.unpack_from('>H', hrom, off)[0] == old
    struct.pack_into('>H', hrom, off, new)

# SKIPPED-IMMEDIATE overrides (audited by trace): #imm values that ARE
# pointers despite landing in data registers. 0x3C92: movel #0x255E0,
# %d2 = sprite frame-table base consumed via adda.l D2 in the sprite
# list builder (poison-rig catch: adda.w (A0) address error at 0x3EA4).
for off, v in T('IMM_OVERRIDES'):
    assert struct.unpack_from('>I', hrom, off)[0] == v, hex(off)
    struct.pack_into('>I', hrom, off, v + REBASE)
    reb += 1
    reb_report.append(f"R {off:06X}: immediate override {v:08X} -> {v + REBASE:08X}")

# LOW-VECTOR reads (census: 4 sites): the game reads its own vector
# table as CONSTANTS (addal 0x0,%a4 adds vector[0]=0xFFFFFF00 = -0x100
# — a 68K size trick). At RV=0 MAME/ares serve DIFFERENT adapter bytes
# at low addresses -> ares-only position skew (the "P1 spawns at P2"
# field bug). Redirect to the high copy's authentic arcade vectors.
for off in T('LOW_VECTOR_READS'):
    v = struct.unpack_from('>I', hrom, off)[0]
    assert v == 0, f"{off:#x}: {v:#x}"
    struct.pack_into('>I', hrom, off, REBASE)
    reb += 1
    reb_report.append(f"R {off:06X}: low-vector ref 0 -> {REBASE:08X}")

# the one abs.w code ref that can't hold 0x94xxxx: thunk via shim RAM
aw_off, aw_tgt = T('ABSW_JMP')
assert hrom[aw_off:aw_off+4] == struct.pack('>HH', 0x4EF8, aw_tgt), hrom[aw_off:aw_off+4].hex()
hrom[aw_off:aw_off+4] = bytes([0x4E, 0xF8, 0xB3, 0xF0])   # jmp (FFFFB3F0).w
# (shim installs the thunk: 0xFFB3F0 = jmp 0x90047E.l)

# DISPATCHER NORMALIZATION (the total fix for handler-pointer data we
# can't enumerate): the two proven consumption funnels get re-pointed
# at shim-RAM thunks that add +0x900000 to any low handler pointer at
# call time. Static table rebases become best-effort; anything missed
# is corrected here.
#   0x39A8 object dispatcher: movea.l (2,A6),A0 ; jsr (A0)
#   0xD842 spawn walker:      movea.l (8,A0),A1 ; jsr (A1)
for _o, _want, _thunk in T('DISPATCHERS'):
    assert hrom[_o:_o+6] == bytes(_want), f"dispatcher {_o:#x}: {hrom[_o:_o+6].hex()}"
    hrom[_o:_o+6] = struct.pack('>HHH', 0x4EB8, _thunk, 0x4E71)
# (shim installs the thunks at 0xFFB3A0 / 0xFFB3C0)

# DATA-POINTER NORMALIZATION (wpcatch.lua finds, intro window): two
# readers consume STORED table pointers whose values live below the
# 0x28000 harvest bound (packed-art byte collisions forbid rebasing
# them statically). Normalize at use time via shim thunks:
#   0xDBA8 spawn walker (intro cast list @0xDD46):
#          movea.l (0x24,A6),A4 -> jsr (FFFFB340).w
#   0x30D0 palette-cycle streamer (glow scripts @0x1A78E):
#          movea.l (2,A5),A0    -> jsr (FFFFB360).w
for _o, _want, _thunk in T('DATA_PTR_NORM'):
    assert hrom[_o:_o+4] == bytes(_want), f"data-ptr site {_o:#x}: {hrom[_o:_o+4].hex()}"
    hrom[_o:_o+4] = struct.pack('>HH', 0x4EB8, _thunk)

# MDHSCR (LOOP-DECOMPILE 23-25): the game's two HORIZONTAL scroll stores
# write MD hscroll DIRECTLY, in addition to their original store.
#
# Why this is safe to do as a pure ADDITION: the shim/SH-2 still reads the
# text-RAM copies to build sc[3]/sc[7], so nothing downstream changes. The
# thunk writes the register too, one IRQ4 earlier than the packet would.
#
# The conversion is MD = S16 - 192, measured (LOOP-DECOMPILE 24): same
# sign, constant offset, and 192 is the 24-column visible-window origin.
# Row scroll is provably off — all four stores mask with #$1FF, which
# clears the row/column-scroll enable in bit 15, and the scroll tables at
# text 0xF00-0xFFF are never written (LOOP-DECOMPILE 23).
#
# d0 is dead at both sites: the next instruction reloads it from work RAM
# (0x2AD8 and 0x2AF4), and no conditional branch reads the CCR in between.
#
# ALTBEAST ADDRESSES. When this graduates, move the site table into
# tools/game_<GAME>.py like DISPATCHERS.
if os.environ.get('MDHSCR') == '1':
    if GAME not in ('altbeast', 'altbeastj'):
        raise SystemExit('MDHSCR: site table is altbeast-only')
    HSCR_BASE = 0xB300                   # two 32-byte thunks: A, then B
    _hs_words = []
    _hs_sites = [
        # (rom offset, S16 register, VRAM hscroll entry, thunk addr)
        (0x2AD2, 0x410E98, 0xFC00, HSCR_BASE),          # foreground -> plane A
        (0x2AEE, 0x410E9A, 0xFC02, HSCR_BASE + 0x20),   # background -> plane B
    ]
    for _off, _reg, _vram, _thunk in _hs_sites:
        _dst = remap(_reg)
        assert isinstance(_dst, int), f'MDHSCR: {_reg:#x} did not remap'
        _want = struct.pack('>HI', 0x33C0, _dst)         # move.w d0,<remapped>.l
        assert hrom[_off:_off+6] == _want, (
            f'MDHSCR site {_off:#x}: have {hrom[_off:_off+6].hex()}, '
            f'want {_want.hex()} — the remap changed, re-derive')
        _ctrl = ((0x4000 | (_vram & 0x3FFF)) << 16) | ((_vram >> 14) & 3)
        _thunk_bytes = (
            struct.pack('>HI', 0x33C0, _dst)             # move.w d0,<remapped>.l
            + struct.pack('>HH', 0x0440, 192)            # sub.w  #192,d0
            + struct.pack('>HH', 0x0240, 0x03FF)         # and.w  #$3FF,d0
            + struct.pack('>HII', 0x23FC, _ctrl, 0xC00004)   # move.l #ctrl,$C00004
            + struct.pack('>HI', 0x33C0, 0xC00000)       # move.w d0,$C00000
            + struct.pack('>H', 0x4E75))                 # rts
        assert len(_thunk_bytes) == 32, len(_thunk_bytes)
        _hs_words += list(struct.unpack('>16H', _thunk_bytes))
        hrom[_off:_off+6] = struct.pack('>HHH', 0x4EB8, _thunk, 0x4E71)
    with open(ROOT / 'md_src' / 'hscr_thunks.h', 'w') as th:
        th.write('/* generated by patch_game.py (MDHSCR) — direct MD hscroll\n'
                 ' * from the game\'s own scroll stores. Two 32-byte 68000\n'
                 f' * thunks installed at 0xFF{HSCR_BASE:04X} and '
                 f'0xFF{HSCR_BASE + 0x20:04X} by md_main.c.\n'
                 ' * See docs/log/LOOP-DECOMPILE.md 23-25. */\n')
        th.write(f'#define HSCR_THUNK_BASE 0x{0xFF0000 | HSCR_BASE:06X}\n')
        th.write(f'#define HSCR_THUNK_WORDS {len(_hs_words)}\n')
        th.write('static const unsigned short hscr_thunks[] = {\n')
        for i in range(0, len(_hs_words), 8):
            th.write('    ' + ', '.join(f'0x{w:04X}' for w in _hs_words[i:i+8]) + ',\n')
        th.write('};\n')
    print(f'MDHSCR: 2 sites rewritten, thunks at 0xFF{HSCR_BASE:04X}/'
          f'0xFF{HSCR_BASE + 0x20:04X}')

# MDSPRPROBE (LOOP-DECOMPILE 27) — COST PROBE, RENDERS WRONG BY DESIGN.
# The sprite upload at 0x2B16 copies 12 of each 16-byte record from the
# pool at 0xFFF800 into the hardware mirror, for every live entry in the
# 256-byte order list (entry 13). This blanks the COPY while keeping the
# list geometry identical — the destination still advances 16 bytes per
# live entry, so the same number of slots is produced and the end markers
# still land in the same place. The mirror therefore goes stale and the
# picture is wrong; the only thing this build measures is how much 68000
# time the copy itself costs, read off the game's own missed-frame
# counter at 0xFFF144 (entry 22).
#
#   2b30  lsl.w #4,d0          ]
#   2b32  lea (a1,d0.w),a3     ] 14 bytes replaced by
#   2b36  move.l (a3)+,(a2)+   ]   lea 16(a2),a2
#   2b38  move.l (a3)+,(a2)+   ]   + 5 nops
#   2b3a  move.l (a3)+,(a2)+   ]
#   2b3c  addq.w #4,a2         ]
if os.environ.get('MDSPRPROBE') == '1':
    if GAME not in ('altbeast', 'altbeastj'):
        raise SystemExit('MDSPRPROBE: site is altbeast-only')
    _o = 0x2B30
    _want = bytes([0xE9, 0x48, 0x47, 0xF1, 0x00, 0x00,
                   0x24, 0xDB, 0x24, 0xDB, 0x24, 0xDB, 0x58, 0x4A])
    assert hrom[_o:_o+14] == _want, f'MDSPRPROBE: {hrom[_o:_o+14].hex()}'
    hrom[_o:_o+14] = bytes([0x45, 0xEA, 0x00, 0x10]) + b'\x4E\x71' * 5
    print('MDSPRPROBE: sprite record copy blanked at 0x2B30 (renders wrong)')

# SCENESEL (LOOP-DECOMPILE 66) — PROBE. Forces every round to load one
# scene, so the later scenes can be reached without playing to them. No
# input script in discover/inputs reaches past scene 0, even at 6000
# frames, which made the per-scene palette analysis the tile plan needs
# unreachable.
#
# 0x662 does `lea $1CDA(pc),a0` then `move.b (a0,d0.w),$FFF142` at 0x670,
# where d0 is the round at 0xFFF14E masked to 7. The table is eight bytes:
#   round  0 1 2 3 4 5 6 7
#   scene  0 1 2 3 4 0 0 0
# Writing N to all eight makes every round load scene N. One byte per
# entry, in place.
#
# NOT A GAMEPLAY BUILD. The tilemap and palettes are the target scene's;
# the ACTORS are whatever the round would have spawned, so this is valid
# for tile and palette measurement and NOT for a sprite census.
_scene = os.environ.get('SCENESEL')
if _scene:
    if GAME not in ('altbeast', 'altbeastj'):
        raise SystemExit('SCENESEL: table address is altbeast-only')
    _n = int(_scene)
    if not 0 <= _n <= 4:
        raise SystemExit('SCENESEL must be 0-4 (five scenes, entry 10)')
    _t = 0x1CDA
    assert list(hrom[_t:_t+8]) == [0, 1, 2, 3, 4, 0, 0, 0], \
        f'SCENESEL: round table is {list(hrom[_t:_t+8])}, not the expected map'
    hrom[_t:_t+8] = bytes([_n] * 8)
    print(f'SCENESEL: every round now loads scene {_n}')

# TAS REPLACEMENT: the MD bus arbiter drops the write phase of the
# 68K's locked read-modify-write cycle, so TAS never sets its latch
# on 32X (works on System 16B). Every tas/bne latch in the game
# re-fires its one-shot forever. Proven live at 0x2268 (attract eye
# gate): the camera-park velocity add ran twice, the eye scene panned
# away, and the demo transition (x<0x1001 tested before the done
# flag) was locked out — the infinite title/eye loop. Each 2-word TAS
# becomes jsr to a shim-RAM thunk: tst.b (TAS's exact N/Z/V/C) then
# st (no CC) then rts. Full-binary opcode scan found exactly these
# five real sites (other 4AC8-4AFF words are data).
TAS_SITES = T('TAS_SITES')
for off, want, thunk in TAS_SITES:
    assert hrom[off:off+4] == want, f"TAS site {off:#x}: {hrom[off:off+4].hex()}"
    hrom[off:off+4] = bytes([0x4E, 0xB8, thunk >> 8, thunk & 0xFF])
# (shim installs the thunks at 0xFFB380/0xFFB38A/0xFFB394/0xFFB3F6)

# TILE DIRTY-BIT THUNKS (write-observer ring, docs/log/LOOP.md iteration 3c):
# every tile-RAM writer roots at a 6-byte lea/immediate whose target
# page is known AT PATCH TIME. Each site becomes jsr to an MD-RAM
# thunk (0xFFB820+16i) that ORs its page bits into the dirty bitmap
# at 0xFFB9FE (all >=0x8000: 68K abs.w SIGN-EXTENDS — 0x5E00.w would
# target low ROM poison, crashing at the first thunked site) and then runs the displaced instruction. The stores
# themselves are untouched (FB staging keeps full truth for the
# game's own read-backs: collision tst.w's, the scratch page). The
# shim ships the bitmap in the DREQ tail; the SH-2 copies ONLY dirty
# pages — steady-state copy_pages retires, shrinking the k1 FM-hold
# from 8-15ms (ares cadence spiral, 67% V-gate rejects) to ~2ms.
# Bits: page = (tileoff >> 12), ALL = 0x1FFF (loaders/clears whose
# extent is table-driven). Read-only leas (0x683C collision base,
# 0x1B7A4 scratch restore) are NOT thunked.
TILE_DIRTY_SITES = T('TILE_DIRTY_SITES')
thunk_words = []
for ti, (off, opw, tgt, bits) in enumerate(TILE_DIRTY_SITES):
    want = struct.pack('>HHH', opw, tgt >> 16, tgt & 0xFFFF)
    assert hrom[off:off+6] == want, \
        f"tile site {off:#x}: {hrom[off:off+6].hex()} != {want.hex()}"
    taddr = 0xB820 + ti * 16   # >=0x8000: abs.w sign-extends to 0xFFB820
    hrom[off:off+6] = struct.pack('>HHH', 0x4EB8, taddr, 0x4E71)
    # thunk: ori.w #bits,(0x5FFE).w ; <displaced lea/imm> ; rts ; pad
    thunk_words += [0x0078, bits, 0xB9FE, opw, tgt >> 16, tgt & 0xFFFF,
                    0x4E75, 0x4E71]
# 0x258A precise thunk (appended after the regular slots): D0 is dead
# at entry (the displaced instruction overwrites it), A0 = block table
# pointer whose first word is the tile-RAM offset. Marks page and
# page+1 (blocks can straddle) via a PC-relative mask table.
sp_addr = 0xB820 + len(TILE_DIRTY_SITES) * 16
want = struct.pack('>HHH', 0x203C, 0x0085, 0x2000)
SBB = T('STRIP_BLITTER_BASE')
assert hrom[SBB:SBB+6] == want, hrom[SBB:SBB+6].hex()
hrom[SBB:SBB+6] = struct.pack('>HHH', 0x4EB8, sp_addr, 0x4E71)
sp = [0x3010,           # move.w (A0),D0     offset
      0xE048,           # lsr.w #8,D0
      0xE848,           # lsr.w #4,D0        D0 = page
      0xD040,           # add.w D0,D0        word index
      0x303B, 0x0000,   # move.w (d8,PC,D0.w),D0  [disp patched below]
      0x8178, 0xB9FE,   # or.w D0,(0xB9FE).w
      0x203C, 0x0085, 0x2000,  # displaced: move.l #0x852000,D0
      0x4E75]           # rts
masks = [((1 << p) | (1 << min(p + 1, 12))) & 0x1FFF for p in range(16)]
# PC-rel base = address of the extension word (sp_addr + 10); table
# starts right after the thunk body.
table_off = len(sp) * 2 - 10
sp[5] = 0x0000 | (table_off & 0xFF)
thunk_words += sp + masks
with open(ROOT / 'md_src' / 'tile_thunks.h', 'w') as th:
    th.write("/* generated by patch_game.py — tile dirty-bit thunks,\n"
             " * installed at 0xFF5E00 by md_main.c */\n")
    th.write(f"#define TILE_THUNK_WORDS {len(thunk_words)}\n")
    th.write("static const unsigned short tile_thunks[] = {\n")
    for i in range(0, len(thunk_words), 8):
        th.write("    " + ", ".join(f"0x{w:04X}" for w in thunk_words[i:i+8])
                 + ",\n")
    th.write("};\n")

# ==== PALETTE DIRTY-BIT THUNKS (LOOP 8) ====
# Retires the 512-word/vint palette diff scan: 45 of the MD handler's 92
# tail scanlines, run on EVERY vint, finding NOTHING in steady state
# (1024 MD-RAM reads to discover that nothing changed). LOOP 6 negatives
# 3-5 proved it cannot be micro-optimised — with the loop body disabled
# the span goes 45.1 -> 0.1 lines, so the loop IS the whole cost. It has
# to stop existing, and the only way is to observe the WRITES instead.
#
# Same mechanism as TILE_DIRTY_SITES above: each site becomes a jsr into
# an MD-RAM thunk that ORs its region bits into a 16-bit dirty word at
# 0xFFB9FC, then runs the displaced instruction and returns. One bit per
# 128-word (256-byte) region of the 2048-word palette; the shim ships
# dirty regions on the DREQ TEXT packet and clears the bit.
#
# EXTENTS ARE MEASURED, NOT ASSUMED. An ALL-dirty (0xFFFF) mask is the
# thing to avoid — the tile thunks' ALL-dirty flood cost title parity
# 63-70% before 0x258A got a precise thunk. Every mask here is derived
# from the disassembled loop bound and CROSS-CHECKED against a live
# region census (tools/pal_tap.lua, 3000 frames of attract + play):
#   PC 902628 (the 0x2612 copy helper, shared by four callers) observed
#   region mask 0085 = exactly the union of the four per-caller masks
#   below (7 | 2 | 0); PC 903976 (site 3952) observed 00FF, exactly its
#   static mask. Table-driven sites take the union over the FIVE round
#   entries of their extent table (0x326E / 0x1724C).
#
# THE STATIC SITE LIST FROM patch_report IS NOT COMPLETE, and only the
# census showed it: 0x3C20 forms 0xFF9800+2+d0*32 and merely QUEUES the
# pointer at 0xFFF402 — the actual writers are the register-indirect
# loops at 0x2DC8 / 0x3C5A (observed regions 8-9), which no
# address-formation scan can attribute. They get a runtime thunk that
# derives the region from A1. 0x3C20 itself needs no thunk: marking at
# POINTER-FORMATION time would let the shim ship and clear the region
# before the write it predicted ever lands.
PAL_DIRTY = 0xB9FC                   # dirty word, abs.w (>=0x8000: 68K
                                     # abs.w SIGN-EXTENDS — see tiles)
PAL_THUNK_BASE = 0xBA00              # 0xFFBA00: free (tile thunks end at
                                     # 0xFFB9E8, bitmap 0xFFB9FE, game RAM
                                     # starts 0xFFC000). The boot stack top
                                     # WAS 0xFFBFF0 in this page; it is
                                     # 0xFF3FF0 since 2026-09-19 (the boot
                                     # vint overwrote the thunk tail). The
                                     # game runs on its OWN stack
                                     # (0xFFFFFF00), so only boot
                                     # shares this page — and only its top.
# (site, displaced length, region mask, note)
# LOOP29 182 — MISSKEEP. The game CLEARS its own missed-frame counter at
# 0x930, on the main loop's countdown-expiry arm (LOOP-DECOMPILE 67), so
# every 0.0% this port has ever read off 0xFFF144 was consistent with
# misses being counted and wiped. NOP the clear and the count is
# cumulative and honest. MEASUREMENT ONLY -- the game reads that counter
# in test mode, so a never-cleared value is a lie to the game.
# Asserts the exact bytes first, the rule that has kept every rebasing
# patch in this file correct.
if os.environ.get('MISSKEEP'):
    want_clr = struct.pack('>HH', 0x4278, 0xF144)     # clr.w $FFF144
    assert hrom[0x930:0x934] == want_clr, hrom[0x930:0x934].hex()
    struct.pack_into('>HH', hrom, 0x930, 0x4E71, 0x4E71)
    print('MISSKEEP: 0x00930 clr.w $FFF144 -> nop nop (counter is now cumulative)')

# LOOP29 185 — PASSCOUNT. A correct game-frame counter, because none of
# the three this repo has used is trustworthy across builds: the scene
# timer at 0xFFF02A is per-scene and scene-dependent in rate AND
# direction (183), the miss counter is cleared by the game itself (182),
# and a counter inside the shared wait at 0x397E counts attract's 120-
# and 240-frame delays as frames (184).
# 0x922 is the GAMEPLAY loop's own `jsr 0x397E` (LOOP-DECOMPILE 67's
# listing: 91A dispatcher, 920 moveq #0, 922 wait, 928 bra). Counting
# there counts one tick per gameplay frame and nothing else.
# Word at 0xFFA0EC. Bytes asserted before the rewrite.
if os.environ.get('PASSCOUNT'):
    # the target is ALREADY REBASED by the passes above (0x0090397E, not
    # 0x0000397E) -- read it rather than assume it, and assert only the
    # opcode and the low word, which rebasing does not touch.
    pc_op, pc_hi, pc_lo = struct.unpack_from('>HHH', hrom, 0x922)
    assert pc_op == 0x4EB9 and pc_lo == 0x397E, \
        hrom[0x922:0x928].hex()
    PASS_WAIT = (pc_hi << 16) | pc_lo

PAL_DIRTY_SITES = T('PAL_DIRTY_SITES')
pal_words = []
pal_report = []
pal_disp_saved = []                  # post-remap displaced instructions,
                                     # reused verbatim by the PAL32 pass
for pi, (off, dlen, bits, note) in enumerate(PAL_DIRTY_SITES):
    disp = list(struct.unpack_from(f'>{dlen // 2}H', hrom, off))
    pal_disp_saved.append(disp)
    tgt = (disp[-2] << 16) | disp[-1]
    assert 0xFF9000 <= tgt < 0xFFA000, f"pal site {off:#x}: target {tgt:#x}"
    taddr = PAL_THUNK_BASE + pi * 16
    # jsr (taddr).w over the displaced instruction, nop-padded to length
    struct.pack_into('>HH', hrom, off, 0x4EB8, taddr)
    for k in range(4, dlen, 2):
        struct.pack_into('>H', hrom, off + k, 0x4E71)
    # thunk: ori.w #bits,(PAL_DIRTY).w ; <displaced> ; rts ; pad to 16
    body = [0x0078, bits, PAL_DIRTY] + disp + [0x4E75]
    pal_words += body + [0x4E71] * (8 - len(body))
    pal_report.append(f"P {off:06X}: mask {bits:04X} -> thunk {taddr:04X}  {note}")

# PRECISE THUNK A — 0x30C2, the colour-cycle engine and by far the
# busiest writer (8818 writes over 3000 frames, ~3 per frame). Its lea
# base is 0xFF9000 but the write lands at +((D0 & 0x7F) << 4), so a
# static mask would be 00FF — eight regions marked EVERY FRAME, i.e. the
# whole palette shipped forever. The census read 0002: one region at a
# time, which is exactly what a runtime mask delivers. The 16-byte write
# is 16-byte aligned, so it can never straddle a 256-byte region.
# D0 is live (0x30C8 re-uses it); D1 is dead here but may be live in the
# CALLER, so it is saved.
pal_a = PAL_THUNK_BASE + len(PAL_DIRTY_SITES) * 16
want = struct.pack('>HHH', 0x43F9, 0x00FF, 0x9000)
PA = T('PAL_THUNK_A')
assert hrom[PA:PA+6] == want, hrom[PA:PA+6].hex()
struct.pack_into('>HHH', hrom, PA, 0x4EB8, pal_a, 0x4E71)
ta = [0x43F9, 0x00FF, 0x9000,   # displaced lea 0xFF9000,A1
      0x2F01,                   # move.l D1,-(SP)
      0x3200,                   # move.w D0,D1
      0x0241, 0x007F,           # andi.w #0x7F,D1
      0xE849,                   # lsr.w #4,D1      D1 = region 0..7
      0xD241,                   # add.w D1,D1      word index
      0x323B, 0x0000,           # move.w (d8,PC,D1.w),D1   [disp below]
      0x8378, PAL_DIRTY,        # or.w D1,(PAL_DIRTY).w
      0x221F,                   # move.l (SP)+,D1
      0x4E75]                   # rts
# Brief extension word: index register D1.w (0x1000) + the displacement
# from the extension word itself (PC base) to the table after the body.
ta[10] = 0x1000 | ((len(ta) - 10) * 2)
pal_words += ta + [1 << r for r in range(16)]

# PRECISE THUNK B — the queued-pointer palette writers at 0x2DC8 and
# 0x3C5A (identical duplicated routines). The region is only knowable
# from A1 at write time, so the thunk derives it: region = (A1 >> 8) & 15.
# Each site's `moveal (A2)+,A1 ; moveal (A2)+,A0` pair is 4 bytes — the
# exact size of a jsr (xxx).w, which is why the pair is displaced rather
# than the 2-byte store alone. The body writes 7 longs (28 bytes) from
# A1, so it can straddle one region boundary: the table marks r and r+1.
# Flags: the displaced moveals set none, and nothing between here and
# the next flag-setter (0x2DE8 subq.b) reads CCR, so the thunk's
# arithmetic is free to clobber it.
pal_b = pal_a + (len(ta) + 16) * 2
tb = [0x225A,                   # displaced: moveal (A2)+,A1
      0x205A,                   # displaced: moveal (A2)+,A0
      0x2F00,                   # move.l D0,-(SP)
      0x2009,                   # move.l A1,D0
      0xE088,                   # lsr.l #8,D0
      0x0240, 0x000F,           # andi.w #15,D0    D0 = region
      0xD040,                   # add.w D0,D0
      0x303B, 0x0000,           # move.w (d8,PC,D0.w),D0   [disp below]
      0x8178, PAL_DIRTY,        # or.w D0,(PAL_DIRTY).w
      0x201F,                   # move.l (SP)+,D0
      0x4E75]                   # rts
tb[9] = 0x0000 | ((len(tb) - 9) * 2)      # index D0.w
pal_words += tb + [(1 << r) | (1 << min(r + 1, 15)) for r in range(16)]
for off in T('PAL_THUNK_B'):
    assert hrom[off:off+4] == b'\x22\x5a\x20\x5a', hrom[off:off+4].hex()
    struct.pack_into('>HH', hrom, off, 0x4EB8, pal_b)
    pal_report.append(f"P {off:06X}: runtime mask from A1 -> thunk {pal_b:04X}")
pal_report.append(f"P {PA:06X}: runtime mask from D0 -> thunk {pal_a:04X}")

if PAL32:
    # LOOP 22 — 32-WORD DIRTY GRANULARITY. Everything above already
    # placed the jsr patches and nop pads; this pass REBUILDS the thunk
    # area and re-points the jsr targets (same 2-word spans), so the
    # region-pair generator stays byte-identical when PAL32 is off.
    # Layout at 0xFFBA00 (installed whole by md_main):
    #   +0x00  8-byte block bitmap, boot state all-dirty (data, not code)
    #   +0x08  pmark helper: D0 = block 0..63 -> bset in the bitmap
    #   then variable-length static-site slots, thunk A, thunk B.
    # Bit addressing is BYTE-space throughout (bset on memory is mod-8):
    # block k -> byte k>>3, bit k&7; region r -> byte r>>1, nibble
    # (r&1 ? 0xF0 : 0x0F). The C side reads the bitmap as bytes too.
    pal_words = [0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF]
    pal_report = []
    pmark = PAL_THUNK_BASE + len(pal_words) * 2
    pal_words += [0x2F01,                   # move.l D1,-(SP)
                  0x2F09,                   # move.l A1,-(SP)
                  0x3200,                   # move.w D0,D1
                  0xE649,                   # lsr.w #3,D1     byte index
                  0x43F8, PAL_THUNK_BASE,   # lea (bitmap).w,A1
                  0xD2C1,                   # adda.w D1,A1
                  0x01D1,                   # bset D0,(A1)    bit = D0 mod 8
                  0x225F,                   # movea.l (SP)+,A1
                  0x221F,                   # move.l (SP)+,D1
                  0x4E75]                   # rts
    for pi, (off, dlen, bits, note) in enumerate(PAL_DIRTY_SITES):
        # displaced instruction SAVED by the region-pair pass above —
        # the only copy that is both post-remap and pre-jsr. Two wrong
        # sources were built and caught: hrom here holds the pair
        # pass's jsr (68K wedged jumping into the reused thunk area),
        # and orig_rom holds the PRE-REMAP 0x840000 targets (palette
        # writes landed in the FB window over the display — full-screen
        # garbage from the attract on).
        disp = pal_disp_saved[pi]
        # region mask -> per-byte nibble masks
        bmask = [0] * 8
        for r in range(16):
            if bits & (1 << r):
                bmask[r >> 1] |= 0xF0 if (r & 1) else 0x0F
        taddr = PAL_THUNK_BASE + len(pal_words) * 2
        struct.pack_into('>H', hrom, off + 2, taddr)   # re-point the jsr
        body = []
        for b in range(8):
            if bmask[b]:
                body += [0x0038, bmask[b], PAL_THUNK_BASE + b]
        body += disp + [0x4E75]
        pal_words += body
        pal_report.append(f"P {off:06X}: blocks {bits:04X} -> thunk"
                          f" {taddr:04X}  {note}")
    pal_a = PAL_THUNK_BASE + len(pal_words) * 2
    struct.pack_into('>H', hrom, PA + 2, pal_a)        # re-point thunk A jsr
    pal_words += [0x2F00,                   # move.l D0,-(SP)
                  0x0240, 0x007F,           # andi.w #0x7F,D0
                  0xE448,                   # lsr.w #2,D0     block 0..31
                  0x4EB8, pmark,            # jsr (pmark).w
                  0x201F,                   # move.l (SP)+,D0
                  0x43F9, 0x00FF, 0x9000,   # displaced lea 0xFF9000,A1
                  0x4E75]                   # rts
    pal_b = PAL_THUNK_BASE + len(pal_words) * 2
    for off in T('PAL_THUNK_B'):
        struct.pack_into('>H', hrom, off + 2, pal_b)   # re-point thunk B jsr
    pal_words += [0x225A,                   # displaced: movea.l (A2)+,A1
                  0x205A,                   # displaced: movea.l (A2)+,A0
                  0x2F00,                   # move.l D0,-(SP)
                  0x2009,                   # move.l A1,D0
                  0xEC88,                   # lsr.l #6,D0
                  0x0240, 0x003F,           # andi.w #0x3F,D0  block 0..63
                  0x4EB8, pmark,            # jsr (pmark).w
                  0x5240,                   # addq.w #1,D0     28B write can
                  0x0240, 0x003F,           # straddle ONE block boundary
                  0x4EB8, pmark,
                  0x201F,                   # move.l (SP)+,D0
                  0x4E75]                   # rts
    pal_report.append(f"P {PA:06X}: 32-block from D0 -> thunk {pal_a:04X}")
    # THUNK A-POST (LOOP29 166). Thunk A marks the block BEFORE the
    # cycler's stores run, so a consume in that gap ships the old colours
    # and clears the bit: the mirror holds the PREVIOUS rotation step,
    # permanently, on every set the cycler drives. Measured stale at
    # sets 19/20/21 in every sampled frame (LOOP29 165).
    # Mark again AFTER the stores. D0 no longer holds the set index by
    # then (0x30F2 reloads it), so recover the block from A1, which has
    # been advanced past the 16 bytes written: block =
    # ((A1-1) >> 6) & 0x3F, the same arithmetic thunk B uses.
    if PAL_APOST:
        apost = PAL_THUNK_BASE + len(pal_words) * 2
        AP = T('PAL_THUNK_APOST')
        want_ap = struct.pack('>HH', 0x4BED, 0x0008)   # lea 8(a5),a5
        assert hrom[AP:AP+4] == want_ap, hrom[AP:AP+4].hex()
        struct.pack_into('>HH', hrom, AP, 0x4EB8, apost)
        pal_words += [0x2F00,                 # move.l D0,-(SP)
                      0x2009,                 # move.l A1,D0
                      0x5380,                 # subq.l #1,D0
                      0xEC88,                 # lsr.l #6,D0
                      0x0240, 0x003F,         # andi.w #0x3F,D0
                      0x4EB8, pmark,          # jsr (pmark).w
                      0x201F,                 # move.l (SP)+,D0
                      0x4BED, 0x0008,         # displaced lea 8(a5),a5
                      0x4E75]                 # rts
        pal_report.append(f"P {AP:06X}: 32-block from A1 AFTER the stores"
                          f" -> thunk {apost:04X}")
    pal_report.append("P " + "/".join(f"{o:06X}" for o in T('PAL_THUNK_B')) + f": 32-block from A1 -> {pal_b:04X}")
    assert PAL_THUNK_BASE + len(pal_words) * 2 <= 0xBFF0, \
        f"pal thunks overrun boot stack: end {PAL_THUNK_BASE + len(pal_words)*2:#x}"

# LOOP29 183 — RELBANK. The game's frame wait is four instructions
# (LOOP-DECOMPILE 67):
#     397E  clr.b  $FFF01C     <- DISCARDS any pending release
#     3982  tst.b  $FFF01C        spin while zero
#     3986  beq.s  0x3982
#     3988  dbf    d0,0x397E
# IRQ4 increments 0xFFF01C once per vint. The game finishes a pass,
# CLEARS the byte -- throwing away the release that arrived while it was
# working -- and waits for a fresh one. So a pass that takes slightly
# MORE than one vint costs exactly TWO, which is the 50.2% this port has
# measured on every build, with the game's own overrun counter honestly
# at zero (LOOP29 182, with its clear NOPed).
# Replace the clear with a CONSUME: decrement if non-zero, so a release
# banked during a long pass is honoured and the game runs at its own rate
# instead of quantising to the vint. Capped so it can never bank enough
# to run away.
if os.environ.get('RELBANK'):
    rb = PAL_THUNK_BASE + len(pal_words) * 2
    want_clr = struct.pack('>HH', 0x4238, 0xF01C)      # clr.b $FFF01C
    assert hrom[0x397E:0x3982] == want_clr, hrom[0x397E:0x3982].hex()
    struct.pack_into('>HH', hrom, 0x397E, 0x4EB8, rb)  # jsr (rb).w
    cap = int(os.environ.get('RELBANK', '1') or 1)
    if cap < 1:
        cap = 1
    # COUNT THE PASSES (LOOP29 184). This thunk runs once per completed
    # game pass, which is the only build-independent way to measure the
    # game's rate: the scene timer is per-scene and scene-dependent, so
    # comparing it across builds compares different scenes (183).
    # 0xFFA0EE, a word, read straight out of the wram dump.
    pal_words += [
        0x5278, 0xA0EE,                 # addq.w #1,$FFA0EE   passes
        0x0C38, 0x0000 | cap, 0xF01C,   # cmpi.b #cap,$FFF01C
        0x6306,                         # bls.s  .consume   (<= cap)
        0x11FC, 0x0000 | cap, 0xF01C,   # move.b #cap,$FFF01C  (clamp)
        0x4E75,                         # rts
        0x4A38, 0xF01C,                 # .consume: tst.b $FFF01C
        0x6704,                         # beq.s .done
        0x5338, 0xF01C,                 # subq.b #1,$FFF01C
        0x4E75,                         # .done: rts
    ]
    pal_report.append(f"P 00397E: clr.b -> CONSUME thunk {rb:04X} (cap {cap})")
    assert PAL_THUNK_BASE + len(pal_words) * 2 <= 0xBFF0, 'RELBANK overruns'
    print(f"RELBANK: 0x0397E clr.b $FFF01C -> jsr {rb:#06x} "
          f"(decrement, cap {cap})")

# (LOOP29 186: I wrote a SECOND FRAMEDONE here that patched the same
# site as the probe at line ~845 and asserted on its own predecessor's
# bytes. Removed. The decompile thread's version measures first, on
# purpose, and its note "no COMM register was free (all eight are in
# use)" is the thing to check before wiring any channel -- COMM10's low
# 13 bits are the dirty-page mask and bits 13-15 look spare, which is
# free BITS rather than a free register. Price it, then wire it.)

if os.environ.get('PASSCOUNT'):
    pc = PAL_THUNK_BASE + len(pal_words) * 2
    struct.pack_into('>HHH', hrom, 0x922, 0x4EB8, pc, 0x4E71)
    pal_words += [
        0x5278, 0xA0EC,                 # addq.w #1,$FFA0EC
        0x4EB9, PASS_WAIT >> 16, PASS_WAIT & 0xFFFF,   # jsr the real wait
        0x4E75,                         # rts
    ]
    pal_report.append(f"P 000922: gameplay wait -> pass counter {pc:04X}")
    assert PAL_THUNK_BASE + len(pal_words) * 2 <= 0xBFF0, 'PASSCOUNT overruns'
    print(f"PASSCOUNT: 0x00922 -> jsr {pc:#06x}, count at 0xFFA0EC")

# ---------------------------------------------------------------------------
# LOOP 23 — FMGATE: gate thunks at the MAIN-loop FB subsystems' entry
# points. Runs LAST so displaced instructions are captured post-remap,
# post-rebase, post-every-other-pass (the pal_disp_saved lesson: there
# is exactly ONE right moment to read them, and it is "just before OUR
# jsr goes in"). Spans and entries come from tools/fmgate_derive.py —
# a census + control-transfer fixpoint, not guesswork. The VINT-path
# writers (the sprite upload) are DELIBERATELY ungated: the trampoline
# guarantees FM=0 for the whole game vint.
#
# Thunk shape (CCR-safe by construction: the displaced run executes
# LAST and rts preserves CCR, so a follower reading the displaced op's
# flags is correct; no entry starts with a conditional — asserted):
#     gate: tst.w (0xA15100).l   ; N = FM
#           bmi.s gate
#           <displaced entry instruction(s)>
#           rts
# (off, dlen, expected first word, note). dlen >= 4 always.
FMGATE_ENTRIES = T('FMGATE_ENTRIES')
# spans for part B's defer check (runtime addresses; thunk range added
# below; zero-terminated)
FMGATE_SPANS = T('FMGATE_SPANS')
TXTW = GT.get('TXT_WRAM_WRITERS') if TXTWRAM else []
if TXTWRAM and not TXTW:
    raise SystemExit(f'patch_game: TXTWRAM needs tools/game_{GAME}.py TABLES["TXT_WRAM_WRITERS"]')
if TXTWRAM:
    assert FMGATE, "TXTWRAM builds on FMGATE (its marks live in the thunk block)"
    TXTW_SITES = {w['site'] for w in TXTW}
    TXTW_LOOPS = {l for w in TXTW for l in w.get('loops', [])}
    TXTW_DROP = {g for w in TXTW for g in w.get('drop_gates', [])}
    for w in TXTW:
        for sp in w.get('drop_spans', []):
            assert tuple(sp) in [tuple(x) for x in FMGATE_SPANS], f"txtwram: span {sp} not in FMGATE_SPANS"
    FMGATE_SPANS = [sp for sp in FMGATE_SPANS
                    if tuple(sp) not in {tuple(x) for w in TXTW for x in w.get('drop_spans', [])}]
fmgate_words = []
spin_fixups = []
fmgate_base = PAL_THUNK_BASE + len(pal_words) * 2
if FMGATE:
    for off, dlen, w0, note in FMGATE_ENTRIES:
        got = struct.unpack_from('>H', hrom, off)[0]
        # 0x4EB8 = an earlier pass (tile write-observer) already put a
        # jsr here. Displace the jsr itself — jsr (abs).w relocates
        # freely — so the gate CHAINS: poll, then the marking thunk,
        # then the original instruction it displaced.
        assert got in (w0, 0x4EB8), \
            f"fmgate {off:#x}: first word {got:04X} != {w0:04X}/4EB8"
        disp = list(struct.unpack_from(f'>{dlen // 2}H', hrom, off))
        if got == 0x41FA:
            # pc-relative lea: rebuild absolute (runtime high copy)
            tgt = off + 2 + struct.unpack_from('>h', hrom, off + 2)[0]
            disp = [0x41F9, 0x0090, tgt & 0xFFFF]
            assert 0 <= tgt < 0x40000
        taddr = fmgate_base + len(fmgate_words) * 2
        struct.pack_into('>HH', hrom, off, 0x4EB8, taddr)
        for k in range(4, dlen, 2):
            struct.pack_into('>H', hrom, off + k, 0x4E71)
        if TXTWRAM and (off in TXTW_SITES or off in TXTW_DROP):
            # the writer stores into WRAM now: no gate (its mark thunk is
            # installed below); undo the jsr just planted
            struct.pack_into(f'>{dlen // 2}H', hrom, off, *disp)
            continue
        if TXTWRAM and off in GT.get('TXT_WRAM_CLEAR_SITES', []):
            # a scene-level text fill invalidates every pending footprint:
            #   clr.b (slot+2).w  per writer  (CCR already owned by the gate)
            for wi2 in range(len(TXTW)):
                fmgate_words += [0x4238, (TXTW_BASE + 4 * wi2 + 2) & 0xFFFF]
        if TXTWRAM and off in TXTW_LOOPS:
            # shared glyph loop heads: a WRAM destination needs no FM
            #   cmpa.l #0x00FF0000,%a1 ; bhs.s <past the spin>
            # (CCR is rewritten by the displaced moveb/clrb anyway)
            fmgate_words += [0xB3FC, 0x00FF, 0x0000, 0x6408]
        if FBXPEND:
            # jsr (spin).w ; nop ; nop  -- same 8 bytes as the inline spin,
            # so the TXTWRAM bhs.s +8 above still lands past it. The spin
            # routine is appended after the table; its address is patched in
            # below once the table is complete.
            fmgate_words += [0x4EB8, 0x0000, 0x4E71, 0x4E71]
            spin_fixups.append(len(fmgate_words) - 3)
        else:
            fmgate_words += [0x4A79, 0x00A1, 0x5100,     # tst.w (0xA15100).l
                             0x6BF8]                      # bmi.s back to tst
        if TXTMASK:
            # TEXT ROW MASK (LOOP29 147). Every text write reaches the FB
            # through one of these gates; each marks the 4-row group it is
            # about to write in WRAM byte 0xFFA1A6 (bit = rows 4g..4g+3),
            # so the master captures only those rows before the flip
            # instead of all 928 longs. The two shared loop heads derive
            # the row from a1 at every iteration (and only for the text
            # page, 0x85Fxxx); the credit writer from its offset variable;
            # the health bar and the 0x153E writer are fixed rows; the two
            # clear-alls mark everything.
            _txt = remap(0x410000)
            assert isinstance(_txt, int) and (_txt & 0xFFF) == 0, _txt
            if TXTWRAM:
                raise SystemExit('TXTMASK: not with TXTWRAM (its writers bypass the FB)')
            if off in (0x3A9A, 0x3AA4):
                fmgate_words += [0x2F00,                          # move.l d0,-(sp)
                                 0x2009,                          # move.l a1,d0
                                 0x0280, 0x00FF, 0xF000,          # andi.l #0xFFF000,d0
                                 0x0C80, (_txt >> 16) & 0xFFFF, _txt & 0xFFFF,   # cmpi.l #text,d0
                                 0x660E,                          # bne.s  skip (+14)
                                 0x2009,                          # move.l a1,d0
                                 0x0240, 0x0FFF,                  # andi.w #0xFFF,d0   byte offset in page
                                 0xE048,                          # lsr.w  #8,d0
                                 0xE248,                          # lsr.w  #1,d0       /512 = 4-row group
                                 0x01F8, 0xA1A6,                  # bset   d0,(0xFFA1A6).w
                                 0x201F]                          # skip: move.l (sp)+,d0
            elif off == 0x3AAE:
                fmgate_words += [0x2F00,                          # move.l d0,-(sp)
                                 0x3038, 0xF024,                  # move.w (0xFFF024).w,d0  credit offset
                                 0x0240, 0x0FFF, 0xE048, 0xE248,  # -> group
                                 0x01F8, 0xA1A6,                  # bset   d0,(0xFFA1A6).w
                                 0x201F]                          # move.l (sp)+,d0
            elif off in (0x153E, 0x4D88):
                fmgate_words += [0x08F8, 0x0006, 0xA1A6]          # bset #6,(0xFFA1A6).w  rows 24-27
            elif off == 0x56E8:
                # THE ZEUS MESSAGE TYPEWRITER (2026-09-20). One glyph every
                # two frames at 0x4104B8 + 2*i, bit 6 of i folding to +256
                # (rows 9 and 11), 64 glyphs, then 64 ZERO stores over the
                # same cells (the erase). The site was gated but never
                # MARKED, so the master captured its rows only on the
                # forced full mask every 8th vint: a few letters landed,
                # most did not, and the erase stores were lost the same
                # way -- "R M U" burned in until the transform's clear-all.
                fmgate_words += [0x08F8, 0x0002, 0xA1A6]          # bset #2,(0xFFA1A6).w  rows 8-11
            elif off in (0x369C, 0x1ACCA, 0x45EC, 0x45FE, 0x4614, 0x4634, 0x4648, 0x4658, 0x46A2):
                fmgate_words += [0x50F8, 0xA1A6]                  # st.b  (0xFFA1A6).w    all rows (0x45EC..0x46A2: the high-score table paints a screen)
        fmgate_words += disp + [0x4E75]
        pal_report.append(f"G {off:06X}: gate -> {taddr:04X}  {note}")
    for wi, w in enumerate(TXTW):
        slot = TXTW_BASE + 4 * wi
        site = w['site']
        got = struct.unpack_from('>H', hrom, site)[0]
        lea_op = 0x41F9 | (w['reg'] << 9)
        assert got == lea_op, f"txtwram {site:#x}: first word {got:04X} != lea abs.l,a{w['reg']}"
        old = struct.unpack_from('>I', hrom, site + 2)[0]
        assert old >> 12 in (0x410, 0x85F, 0xFF8), f"txtwram {site:#x}: operand {old:#x} not text RAM"
        newop = 0x00FF8000 | (old & 0xFFF)
        taddr = fmgate_base + len(fmgate_words) * 2
        struct.pack_into('>HHH', hrom, site, 0x4EB8, taddr, 0x4E71)
        words = []
        if 'off_var' in w:      # record the offset the writer is about to add
            words += [0x31F8, w['off_var'] & 0xFFFF, slot & 0xFFFF]   # move.w (var).w,(slot).w
        words += [0x50F8, (slot + 2) & 0xFFFF]                         # st.b (slot+2).w
        words += [lea_op, newop >> 16, newop & 0xFFFF, 0x4E75]         # lea WRAM text,%aN ; rts
        fmgate_words += words
        for alt in w.get('alt_sites', []):
            g2 = struct.unpack_from('>H', hrom, alt)[0]
            o2 = struct.unpack_from('>I', hrom, alt + 2)[0]
            assert g2 == lea_op and o2 >> 12 in (0x410, 0x85F), f"txtwram alt {alt:#x}: {g2:04X} {o2:#x}"
            struct.pack_into('>I', hrom, alt + 2, 0x00FF8000 | (o2 & 0xFFF))
        pal_report.append(f"G {site:06X}: TXTWRAM mark -> {taddr:04X}  {w.get('note', '')}")
    if GAMEGATE:
        # THE FRAME GATE (LOOP29 141, from LOOP-DECOMPILE 22). IRQ4 at
        # 0x2AB8 tests the loop's frame flag: clear = the loop is waiting,
        # release it (0x2AC6 addq.b #1,$FFF01C); set = the loop overran,
        # count it (0x2ABE) and take the short path (0x2C06). We own the
        # decision now: the thunk returns Z=1 (release) only when the shim's
        # go token (0xFFA0F5) is set AND the loop is waiting, consuming the
        # token; otherwise Z=0 and IRQ4 takes its own short path. The
        # overrun count becomes nops so 0xFFF144 stays a pure overrun
        # counter on the arcade and never counts our pacing.
        _o = 0x2AB8
        _want = bytes.fromhex('4a38f01c' '6708' '5278f144' '60000142')
        assert hrom[_o:_o+14] == _want, f'GAMEGATE site: {hrom[_o:_o+14].hex()}'
        gg_addr = fmgate_base + len(fmgate_words) * 2
        if os.environ.get('TAILCENSUS'):
            # NOTES 49 / LOOP29 260: stamp V on the thunk's first visit after
            # a release (the game reached its frame-wait loop = FRAMEDONE);
            # the shim's IRQ4 clears the flag after reading the stamp.
            fmgate_words += [0x4A38, 0xA1F6,              # tst.b  (0xFFA1F6).w   stamped since the last release?
                             0x660C,                      # bne.s  +12
                             0x31F9, 0x00C0, 0x0008, 0xA1F4,   # move.w (0xC00008).l,(0xFFA1F4).w  (the pass ARRIVED here)
                             0x50F8, 0xA1F6]              # st.b   (0xFFA1F6).w
            # the flag clears when the token is consumed (below), so the
            # next stamp is the arrival after the NEXT pass, not the spin
            fmgate_words += [0x4A38, 0xA0F5,      # tst.b  (0xFFA0F5).w   go token?
                             0x6710,              # beq.s  nogo (+16)
                             0x4A38, 0xF01C,      # tst.b  (0xFFF01C).w   loop waiting?
                             0x660A,              # bne.s  nogo (+10)
                             0x4238, 0xA0F5,      # clr.b  (0xFFA0F5).w   consume, Z=1
                             0x4238, 0xA1F6,      # clr.b  (0xFFA1F6).w   arm the next arrival stamp
                             0x4E75,              # rts
                             0x7001,              # nogo: moveq #1,d0
                             0x4E75]              # rts
        else:
            fmgate_words += [0x4A38, 0xA0F5,      # tst.b  (0xFFA0F5).w   go token?
                         0x670C,              # beq.s  nogo
                         0x4A38, 0xF01C,      # tst.b  (0xFFF01C).w   loop waiting?
                         0x6606,              # bne.s  nogo           (still mid-frame: hold)
                         0x4238, 0xA0F5,      # clr.b  (0xFFA0F5).w   consume, Z=1
                         0x4E75,              # rts
                         0x7001,              # nogo: moveq #1,d0     Z=0 (d0 is reloaded at 0x2ACA)
                         0x4E75]              # rts
        struct.pack_into('>HH', hrom, _o, 0x4EB8, gg_addr)          # jsr (thunk).w
        struct.pack_into('>HH', hrom, _o + 6, 0x4E71, 0x4E71)       # overrun count -> nops
        pal_report.append(f"G {_o:06X}: GAMEGATE frame release -> {gg_addr:04X}")
    if FBXPEND:
        # THE SHARED GATE SPIN (LOOP29 137). Every gate thunk jsr's here
        # instead of spinning inline. Spin on FM as before; when FM reads 0
        # and the shim has a packet pending (WRAM word 0xFFA0FE, set by the
        # tail when it found FM up), call the shim's late blast through the
        # vector at 0xFFA0F8 with every register saved. The game was going
        # to wait for FM anyway; the blast rides the tail of that wait, so
        # the packet never has to take the pre-post slot and the post keeps
        # its early line.
        spin_addr = fmgate_base + len(fmgate_words) * 2
        for i in spin_fixups:
            fmgate_words[i] = spin_addr & 0xFFFF
        fmgate_words += [0x4A79, 0x00A1, 0x5100,      # spin: tst.w (0xA15100).l
                         0x6BF8,                      #       bmi.s spin
                         0x4A79, 0x00FF, 0xA0FE,      #       tst.w (0xFFA0FE).l   pending?
                         0x6710,                      #       beq.s done
                         0x48E7, 0xFFFE,              #       movem.l d0-d7/a0-a6,-(sp)
                         0x2079, 0x00FF, 0xA0F8,      #       movea.l (0xFFA0F8).l,a0
                         0x4E90,                      #       jsr (a0)
                         0x4CDF, 0x7FFF,              #       movem.l (sp)+,d0-d7/a0-a6
                         0x4E75]                      # done: rts
    fmgate_end = fmgate_base + len(fmgate_words) * 2
    assert fmgate_end <= 0xBFF0, \
        f"fmgate thunks overrun boot stack: end {fmgate_end:#x}"

with open(ROOT / 'md_src' / 'fmgate_tab.h', 'w') as fh:
    fh.write("/* generated by patch_game.py — LOOP 23 FMGATE tables.\n"
             " * Entry-gate thunks (installed after the pal thunks) and\n"
             " * the span table for part B's defer check. Derivation:\n"
             " * tools/fmgate_derive.py. */\n")
    fh.write(f"#define FMGATE_ON {1 if FMGATE else 0}\n")
    fh.write(f"#define FMGATE_THUNK_ADDR 0x{fmgate_base:04X}\n")
    fh.write(f"#define FMGATE_THUNK_WORDS {len(fmgate_words)}\n")
    fh.write(f"#define FMGATE_SPIN_ADDR 0x{(spin_addr if (FMGATE and FBXPEND) else 0):04X}\n")
    fh.write(f"#define GAMEGATE_ON {1 if (FMGATE and GAMEGATE) else 0}\n")
    fh.write(f"#define TXT_WRAM_ON {1 if TXTWRAM else 0}\n")
    fh.write(f"#define TXTW_N {len(TXTW) if TXTWRAM else 0}\n")
    fh.write("/* per writer: WRAM slot (word = live text byte offset, byte +2 = dirty),\n"
             " * fixed text byte offset (0xFFFF = read the slot word), words to copy,\n"
             " * selector byte (0 = none) and the alternate offset it selects when non-zero\n"
             " * (the health bar: P1 / P2 footprints, never both — a stale mirror of the\n"
             " * other player's bar must not be re-planted over the game's clear) */\n")
    fh.write("static const struct { unsigned short slot, off, words, sel, off2; } txtw[] = {\n")
    for wi, w in enumerate(TXTW if TXTWRAM else []):
        slot = TXTW_BASE + 4 * wi
        if 'off_var' in w:
            fh.write(f"    {{ 0x{slot & 0xFFFF:04X}, 0xFFFF, {w['words']}, 0, 0 }},\n")
        elif 'ranges' in w:
            (lo, hi), (lo2, hi2) = w['ranges']
            assert (hi - lo) == (hi2 - lo2)
            fh.write(f"    {{ 0x{slot & 0xFFFF:04X}, 0x{lo:04X}, {(hi - lo + 1) // 2}, 0x{w['sel_var'] & 0xFFFF:04X}, 0x{lo2:04X} }},\n")
        else:
            lo, hi = w['range']
            fh.write(f"    {{ 0x{slot & 0xFFFF:04X}, 0x{lo:04X}, {(hi - lo + 1) // 2}, 0, 0 }},\n")
    fh.write("    { 0, 0, 0, 0, 0 } };\n")
    fh.write("static const unsigned short fmgate_thunks[] = {\n")
    for i in range(0, max(len(fmgate_words), 1), 8):
        row = fmgate_words[i:i+8] or [0x4E75]
        fh.write("    " + ", ".join(f"0x{w:04X}" for w in row) + ",\n")
    fh.write("};\n")
    if FMGATE:
        spans = [(0x900000 + s, 0x900000 + e) for s, e in FMGATE_SPANS]
        spans.append((0xFF0000 | fmgate_base, 0xFF0000 | (fmgate_end - 2)))
        fh.write("#define FMGATE_SPANS { \\\n")
        for s, e in spans:
            fh.write(f"    0x{s:08X}u, 0x{e:08X}u, \\\n")
        fh.write("    0u }\n")
    else:
        fh.write("#define FMGATE_SPANS { 0u }\n")

with open(ROOT / 'md_src' / 'pal_thunks.h', 'w') as th:
    th.write("/* generated by patch_game.py — palette dirty-bit thunks\n"
             f" * (LOOP 8), installed at 0xFF{PAL_THUNK_BASE:04X} by"
             " md_main.c.\n"
             + (f" * PAL32: 8-byte block bitmap at 0xFF{PAL_THUNK_BASE:04X}"
                " (installed all-dirty),\n * one bit per 32-word block,"
                " byte-space addressing. */\n" if PAL32 else
                f" * Dirty word: 0xFF{PAL_DIRTY:04X}, one bit per 128-word"
                " region. */\n"))
    th.write(f"#define PAL_THUNKS_PAL32 {1 if PAL32 else 0}\n")
    th.write(f"#define PAL_THUNK_WORDS {len(pal_words)}\n")
    th.write("static const unsigned short pal_thunks[] = {\n")
    for i in range(0, len(pal_words), 8):
        th.write("    " + ", ".join(f"0x{w:04X}" for w in pal_words[i:i+8])
                 + ",\n")
    th.write("};\n")
print(f"pal_thunks.h: {len(PAL_DIRTY_SITES) + 3} sites, "
      f"{len(pal_words) * 2} bytes at 0xFF{PAL_THUNK_BASE:04X}")

# Palette-cycle LAUNCH TABLE at 0x1A6FA ([id.w][script.l] x3): the
# harvest pass caught entry 1 (0x1A70E) but missed entries 2/3, whose
# 0x0001A78E script pointers stayed low — the launcher reads the
# script header DIRECTLY (0x1A6E0: move.w (2,A1)) before the thunked
# streamer ever runs, so it read poison: no lightning/red-text glow.
_pl_offs, _pl_val = T('PAL_LAUNCH')
for off in _pl_offs:
    v = struct.unpack_from('>I', hrom, off)[0]
    assert v == _pl_val, f"{off:#x}: {v:#x}"
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
REBASE_EXCLUDE = T('REBASE_EXCLUDE')
for lo, hi_ in REBASE_EXCLUDE:
    if hrom[lo:hi_] != orig_rom[lo:hi_]:
        hrom[lo:hi_] = orig_rom[lo:hi_]
        reb_report.append(f"REVERT {lo:06X}-{hi_:06X}: excluded region restored")

(ROOT / 'md_src' / 'game_high.bin').write_bytes(hrom[:0x40000])
(ROOT / 'tools' / 'rebase_report.txt').write_text(
    f"{reb} refs rebased (+{REBASE:#x})\n\n"
    + "PALETTE DIRTY-BIT THUNKS (LOOP 8):\n" + "\n".join(pal_report) + "\n\n"
    + "\n".join(reb_report)
    + "\n\nSKIPPED long immediates (burn-down candidates):\n"
    + "\n".join(skipped_imm) + "\n")
print(f"game_high.bin: {reb} refs rebased, {len(skipped_imm)} immediates "
      "skipped -> tools/rebase_report.txt")
