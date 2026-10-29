#!/usr/bin/env python3
"""romsyms.py -- the SH-2 symbol map that belongs to a rom, or a refusal.

Symbols move every build, and `make port PORT_NAME=x` renames only the .32x: the s16.lst
beside it is whatever was built LAST. Read against another build's map, a tool reads the
wrong addresses and prints numbers anyway (2026-10-19: artaudit read a 0x430-off r60_smap
and reported 1001 garbage cells; sw264ph "had 900 bad cells" for the same reason).

    import romsyms
    lst  = romsyms.lst_for(rom)              # the map, proven to be this rom's build
    addr = romsyms.sym(rom, 'r60_census')    # leading underscore optional

Resolution, first match wins:
  1. <rom>.lst with <rom>.elf      (tools/save_build.sh keeps both): the elf must match the rom
  2. <rom>.lst without an elf      (older save_build.sh pairs): trusted, they were copied as a pair
  3. s16.lst with s16.elf beside the rom: the elf must match the rom
Anything else exits with the reason. "Match" = every loaded ELF segment equals the rom at its
p_paddr (the rom offset): a matching build differs in 0 bytes, ctl1 against belt1's elf in 1.2 MB.
"""
import os, struct, sys

_cache = {}

def elf_matches(rom_bytes, elf):
    e = open(elf, 'rb').read()
    phoff, = struct.unpack('>I', e[28:32]); phsz, phn = struct.unpack('>HH', e[42:46])
    for i in range(phn):
        typ, off, va, pa, fsz = struct.unpack('>5I', e[phoff + i*phsz:phoff + i*phsz + 20])
        if typ != 1 or fsz == 0: continue
        if rom_bytes[pa:pa + fsz] != e[off:off + fsz]: return False
    return True

def lst_for(rom, lst=None):
    """An explicit `lst` is the caller's claim and is returned as given."""
    if lst: return lst
    rom = os.path.abspath(rom)
    if rom in _cache: return _cache[rom]
    stem, d = os.path.splitext(rom)[0], os.path.dirname(rom)
    rb = open(rom, 'rb').read()
    why = []
    named, named_elf = stem + '.lst', stem + '.elf'
    if os.path.exists(named):
        if not os.path.exists(named_elf): _cache[rom] = named; return named
        if elf_matches(rb, named_elf): _cache[rom] = named; return named
        why.append(f'{os.path.basename(named_elf)} is a different build')
    shared, shared_elf = os.path.join(d, 's16.lst'), os.path.join(d, 's16.elf')
    if os.path.exists(shared):
        if not os.path.exists(shared_elf): why.append('s16.lst has no s16.elf beside it to verify')
        elif elf_matches(rb, shared_elf): _cache[rom] = shared; return shared
        else: why.append('s16.lst/s16.elf beside it are a later build (make port renames only the .32x)')
    else:
        why.append('no s16.lst beside it')
    sys.exit(f'romsyms: no symbol map proven to be {os.path.basename(rom)}\'s build: ' + '; '.join(why) +
             '. Rebuild it, or audit the last-built rom.')

def symbols(rom, lst=None):
    out = {}
    for line in open(lst_for(rom, lst)):
        f = line.split()
        if len(f) == 3: out[f[2]] = int(f[0], 16)
    return out

def sym(rom, name, lst=None, hint=''):
    s = symbols(rom, lst)
    for k in (name, '_' + name.lstrip('_')):
        if k in s: return s[k]
    sys.exit(f'romsyms: {name} not in {lst_for(rom, lst)}' + (f' -- {hint}' if hint else ''))

if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit('usage: romsyms.py ROM [SYMBOL ...]')
    print(lst_for(sys.argv[1]))
    for n in sys.argv[2:]: print(f'{n} {sym(sys.argv[1], n):#010x}')
