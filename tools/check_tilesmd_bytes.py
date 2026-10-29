#!/usr/bin/env python3
"""Verify the COMPILED 68K art base in a built rom, not the source.

md_main.c picks TILESMD_CART by #ifdef PORT_FULL; tools/check_tilesmd_addr.py
compares the two source literals against mars.ld but cannot tell which branch
the 68K compiler took. 2026-10-04: a PORTFULL rom (rd1) carried
`addal #0x96AA00,%a0` -- the line's base -- because -DPORT_FULL was in SHCCFLAGS
only, and every baked tile the MD fetched came from 8 KB below the blob.

The fetch loop adds TILESMD_BANKED + TILESMD_INDEX_BYTES to the block offset
as one immediate: 0x96A000+0xA00 = 0x96AA00 (line) or 0x96C000+0xA00 = 0x96CA00
(PORTFULL). That immediate is searched for in the rom's MD program.

    tools/check_tilesmd_bytes.py rom/x.32x [--portfull]
"""
import os
import re
import sys

# THE INDEX SIZE IS GENERATED, SO DERIVE IT. It was spelled 0xA00 here, which is 10 scenes
# x 128 sets x 2 bytes -- true until tools/round_split.py added an 11th table and the
# immediate became 0x96CB00. The check then reported the rom as compiled with the wrong
# TILESMD_CART, which is a frightening and completely wrong message for a correct rom.
def index_bytes(default=0xA00):
    for p in ('build/port/sh_src/tiles_md.h', 'sh_src/tiles_md.h'):
        if os.path.exists(p):
            m = re.search(r'#define\s+TILESMD_SCENES\s+(\d+)', open(p).read())
            if m:
                return int(m.group(1)) * 128 * 2
    return default


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    portfull = '--portfull' in sys.argv
    rom = open(args[0], 'rb').read()
    md = rom[:0x100000]                       # the MD program precedes the SH-2 image
    ib = index_bytes()
    def pat(base):
        v = base + ib
        return b'\xd1\xfc' + v.to_bytes(4, 'big')   # addal #<base+index>,%a0
    line_base = pat(0x96A000)
    port_base = pat(0x96C000)
    nl, np_ = md.count(line_base), md.count(port_base)
    want = port_base if portfull else line_base
    label = 'PORTFULL (0x26C000)' if portfull else 'line (0x26A000)'
    if md.count(want) != 1 or md.count(line_base) + md.count(port_base) != 1:
        print(f"FATAL: {args[0]}: MD art base immediates: line={nl} portfull={np_}; "
              f"expected exactly one, the {label} one. The 68K was compiled with the "
              f"wrong TILESMD_CART (PORT_FULL not in MDCCFLAGS?)")
        sys.exit(1)
    print(f"ok: {args[0]} MD fetches baked art from the {label} base "
          f"(index {ib} bytes)")

if __name__ == '__main__':
    main()
