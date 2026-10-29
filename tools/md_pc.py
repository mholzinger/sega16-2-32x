#!/usr/bin/env python3
"""Disassemble the 68K code a rom carries at a RAM-resident PC -- from the rom
itself, never from a listing that may belong to another build.

    tools/md_pc.py rom/rd1.32x 0xFF1C6E [--md-elf rom/rd2.md.elf] [--before N] [--after N]

.data code lives at VMA 0xFF0100 and is copied from the rom at boot; the rom
offset of a VMA is  LMA(.data) - 0x880000 + (pc - VMA(.data)).  LMA/VMA are read
from the .md.elf saved beside the rom (tools/save_build.sh), default rom/md_start.elf
-- a build with the same .data layout is enough, the bytes come from the rom.
.text PCs (0x8Cxxxx) map straight to rom offset pc - 0x880000.
"""
import subprocess, sys, re, os
OD = os.path.expanduser('~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')

def sections(elf):
    out = subprocess.run([OD, '-h', elf], capture_output=True, text=True).stdout
    secs = {}
    for ln in out.splitlines():
        m = re.match(r'\s*\d+\s+(\.\w+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', ln)
        if m: secs[m.group(1)] = (int(m.group(2),16), int(m.group(3),16), int(m.group(4),16))  # size, vma, lma
    return secs

def main():
    args = sys.argv[1:]
    rom = args[0]; pc = int(args[1], 16)
    elf = args[args.index('--md-elf')+1] if '--md-elf' in args else 'rom/md_start.elf'
    before = int(args[args.index('--before')+1]) if '--before' in args else 0x40
    after = int(args[args.index('--after')+1]) if '--after' in args else 0x20
    secs = sections(elf)
    size, vma, lma = secs['.data']
    if vma <= pc < vma + size:
        off = lma - 0x880000 + (pc - vma); base = vma - (lma - 0x880000)
    elif 0x880000 <= pc < 0x900000:
        off = pc - 0x880000; base = 0x880000
    else:
        sys.exit(f"{pc:#x}: not in .data ({vma:#x}+{size:#x}) nor MD .text")
    out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'm68k', f'--start-address={off-before:#x}',
                          f'--stop-address={off+after:#x}', rom], capture_output=True, text=True).stdout
    for ln in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s*(.*)', ln)
        if not m: continue
        a = int(m.group(1), 16) + base
        print(f"{'>>' if a == pc else '  '} {a:06x}: {m.group(2)}")

if __name__ == '__main__':
    main()
