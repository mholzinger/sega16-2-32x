#!/usr/bin/env python3
"""Feasibility scan for the 0x880000 rebase (the RV=0 "unpair" project).

Classifies 68K program references into ROM space (0x000000-0x03FFFF) by
how safely they can be rebased to 0x880000+:

  J   jsr/jmp absolute long          -> mechanical rebase (safe)
  L   lea/pea absolute long          -> mechanical rebase (safe)
  A   long-immediate into %aN        -> pointer by type (safe)
  E   other abs.l EA operands        -> data access, mechanical (safe)
  W   abs.w EA operands into ROM     -> 16-bit slot CANNOT hold 0x88xxxx:
                                        needs instruction rewrite/thunk
  M   move.l/pea long-immediate elsewhere -> ambiguous pointer/constant
  D   spawn-table handler longs      -> known data pointers

PC-relative branches and displacements are excluded (no rebase needed).
Encoding check: an operand is abs.l only if the 32-bit big-endian value
appears in the instruction bytes; abs.w if only the 16-bit form does.
"""
import re
import struct
from pathlib import Path
from collections import Counter

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'
ASM = (ROMS / 'prog68k.asm').read_text().splitlines()
ROM_TOP = 0x40000

line_re = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} )+)\s*(\S.*)$')
BRANCHES = ('bra', 'brs', 'bsr', 'bhi', 'bls', 'bcc', 'bcs', 'bne', 'beq',
            'bvc', 'bvs', 'bpl', 'bmi', 'bge', 'blt', 'bgt', 'ble', 'dbf',
            'dbra', 'dbcc', 'dbcs', 'dbne', 'dbeq', 'dbhi', 'dbls', 'dbpl',
            'dbmi', 'dbge', 'dblt', 'dbgt', 'dble', 'dbvc', 'dbvs', 'dbt')

counts = Counter()
samples = {}


def note(cls, off, insn):
    counts[cls] += 1
    samples.setdefault(cls, []).append((off, insn.strip()))


for line in ASM:
    m = line_re.match(line)
    if not m:
        continue
    off = int(m.group(1), 16)
    ibytes = bytes.fromhex(m.group(2).replace(' ', ''))
    insn = m.group(3)
    mn = insn.split()[0] if insn.split() else ''
    if mn.startswith(BRANCHES) or mn.startswith('.'):
        continue                           # pc-relative, or data pseudo-ops

    # strip pc-relative / register-displacement operand text
    clean = re.sub(r'%(?:pc|a[0-7]|fp|sp)@\([^)]*\)', '', insn)

    for hexm in re.finditer(r'(?<![#\w])0x([0-9a-f]+)\b', clean):
        v = int(hexm.group(1), 16)
        if not (0x100 <= v < ROM_TOP):
            continue
        has_l = struct.pack('>I', v) in ibytes
        has_w = struct.pack('>H', v) in ibytes if v < 0x10000 else False
        if mn in ('jsr', 'jmp'):
            note('J' if has_l else 'W', off, insn)
        elif mn in ('lea', 'pea'):
            note('L' if has_l else 'W', off, insn)
        elif has_l:
            note('E', off, insn)
        elif has_w:
            note('W', off, insn)

    for imm in re.finditer(r'#(\d+)\b', clean):
        v = int(imm.group(1))
        if not (0x100 <= v < ROM_TOP) or (v & 1):
            continue
        if struct.pack('>I', v) not in ibytes:
            continue                       # not a long immediate
        dst = clean.split(',')[-1].strip()
        if mn.startswith('movea') or re.fullmatch(r'%a[0-7]', dst):
            note('A', off, insn)
        elif mn in ('movel', 'pea'):
            note('M', off, insn)

rom = bytearray((ROOT / 'md_src' / 'game_body.bin').read_bytes())
base = 0x808
n = 0
for a in range(0x1D2DC, 0x1D520, 12):
    i = a - base + 8
    if 0 <= i < len(rom) - 3:
        v = struct.unpack_from('>I', rom, i)[0]
        if v < ROM_TOP:
            n += 1
counts['D(spawn)'] = n

print('=== rebase site census (refined) ===')
for cls in ('J', 'L', 'A', 'E', 'W', 'M', 'D(spawn)'):
    print(f'{cls:9s} {counts.get(cls, 0):5d}')
print()
for cls in ('W', 'M'):
    print(f'--- {cls} samples (first 30) ---')
    for off, insn in samples.get(cls, [])[:30]:
        print(f'  {off:06X}: {insn}')
    print()
