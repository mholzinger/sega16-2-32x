#!/usr/bin/env python3
"""The program's instruction stream, with no Ghidra (LOOP-DECOMPILE 76).

`hazard_census.py` needs an instruction address list so a linear sweep
does not report data as code -- on Altered Beast that alone is 4 false
TAS, 50 false CHK and 673 false MOVEP. That list used to come from
`tools/ghidra/instr_list.py`, i.e. from opening the analysed project.

This produces the same thing from the function map and the rom: objdump
each function from its OWN entry, so boundaries are the function's rather
than a linear listing's, and union the bodies.

    python3 tools/code_stream.py > docs/audit/code_stream.txt
    python3 tools/code_stream.py --addrs > docs/audit/code_addrs.txt

Addresses come from `repair_seeds` in `docs/audit/altbeast_seeds.json`,
which is the REFERENCE disassembly's instruction address list: 19137
entries, 0x400 to 0x1EF1E, the same count and the same ceiling the
reference reports. That is wider than the function map -- the bodies of
the 555 bounded functions hold 15159 of those -- and the difference
matters, because 0x150B6 is a real TAS site in code the seeded Ghidra
project never reached.
"""
import re, subprocess, sys, os

OD = os.environ.get('M68K_OBJDUMP',
     '/Users/mikeholzinger/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')
BIN = 'roms/altbeast/prog68k.bin'
SEEDS = 'docs/audit/altbeast_seeds.json'

HEAD = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*?)\s*$')
CONT = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*$')


def disasm(start, stop):
    o = subprocess.run(
        [OD, '-D', '-b', 'binary', '-m', 'm68k:68000', BIN,
         '--start-address=0x%X' % start, '--stop-address=0x%X' % stop],
        capture_output=True, text=True).stdout
    r = []
    for ln in o.splitlines():
        m = HEAD.match(ln)
        if m:
            r.append([int(m.group(1), 16),
                      len(m.group(2).replace(' ', '')) // 2,
                      m.group(3), m.group(4)])
            continue
        # objdump WRAPS anything over six bytes onto a line with no
        # mnemonic; fold it or every long instruction measures short.
        m = CONT.match(ln)
        if m and r:
            r[-1][1] += len(m.group(2).replace(' ', '')) // 2
    return r


def main():
    import json
    want_addrs = '--addrs' in sys.argv
    want = sorted(int(x, 16)
                  for x in json.load(open(SEEDS))['repair_seeds'])
    wantset = set(want)
    seen = {}
    i = 0
    runs = 0
    while i < len(want):
        start = want[i]
        # decode forward from a known instruction address and keep every
        # decode that the reference also calls an instruction; the moment
        # the two disagree, resync at the next address we have not seen.
        for a, n, mn, ops in disasm(start, min(start + 0x2000, 0x1F000)):
            if a in wantset and a not in seen:
                seen[a] = (n, mn, ops)
            elif a not in wantset:
                break
        runs += 1
        while i < len(want) and want[i] in seen:
            i += 1
    out = sys.stdout
    if want_addrs:
        for a in sorted(seen):
            out.write('%X\n' % a)
    else:
        for a in sorted(seen):
            n, mn, ops = seen[a]
            out.write('%05X\t%d\t%s\t%s\n' % (a, n, mn, ops))
    sys.stderr.write('%d of %d reference instructions decoded in %d runs\n'
                     % (len(seen), len(want), runs))


if __name__ == '__main__':
    main()
