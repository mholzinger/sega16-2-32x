#!/usr/bin/env python3
"""Census of ARCADE-BEHAVIOUR DEPENDENCIES in a System 16 program.

    tools/hazard_census.py LISTING.dis [--code INSTRS.txt]

Finds the instruction classes that do not behave the same way on a 32X as
they do on a System 16B board. This is the generalisable half of the kit:
the reason each one matters is a property of the HARDWARE, so a rule
learned on one title applies to every title (docs/log/LOOP-DECOMPILE.md 37).

--code takes the address list from tools/ghidra/instr_list.py. Without it
the scan runs over the raw listing and WILL report phantoms, because a
linear sweep disassembles data as instructions: on Altered Beast that adds
4 false TAS sites, 50 false CHK and 673 false MOVEP. Always pass --code.

VALIDATION: with --code this reproduces tools/game_altbeast.py's
hand-derived TAS_SITES exactly — the same five addresses, no more and no
fewer. That is the check that the method is sound before it is pointed at
a title nobody has hand-derived.
"""
import argparse
import re
import sys

# mnemonic -> (what the game assumes, what a 32X does instead)
HAZARDS = {
    'tas': ('a locked read-modify-write sets the latch',
            'the MD bus arbiter DROPS the write phase, so the latch never '
            'sets — patch_game rewrites every site'),
    'stop': ('halts until an interrupt the arcade guarantees',
             'resumes only if the 32X-side interrupt actually arrives; a '
             'masked or re-vectored source hangs the 68000'),
    'reset': ('asserts RESET to the peripherals',
              'on 32X this reaches hardware the port does not model'),
    'movep': ('peripheral byte-lane access on a 68000 8-bit port',
              'no such peripheral in the 32X map'),
    'chk': ('bounds trap through the arcade vector table',
            'the vector must exist in our map'),
    'trapv': ('overflow trap through the arcade vector table',
              'same'),
}

LINE = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} )+)\s*([a-z]+)\s*(.*?)\s*$')

# TAS on a DATA REGISTER is not a bus operation at all, so the dropped
# write phase cannot bite it. patch_game.py correctly excludes 0xE1DC
# (`tas d2`) for exactly this reason; an earlier version of this scan
# reported it and was wrong (docs/log/LOOP-DECOMPILE.md 37).
REG_ONLY = re.compile(r'^%d[0-7]$')

# No code exists above this in Altered Beast (docs/log/LOOP-DECOMPILE.md
# 16: the highest instruction is 0x1EF1E and 52% of the rom is data). A
# candidate above it is data, with no hand check needed. Re-derive per
# title; it is the single cheapest filter in this scan.
CODE_CEILING = 0x1F000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('listing')
    ap.add_argument('--code', help='instruction address list (one hex per line)')
    a = ap.parse_args()

    code = None
    if a.code:
        code = set(int(x, 16) for x in open(a.code).read().split())
    else:
        print('WARNING: no --code, phantoms over data WILL be reported\n',
              file=sys.stderr)

    hits = {}
    phantom = {}
    for ln in open(a.listing):
        m = LINE.match(ln)
        if not m:
            continue
        addr = int(m.group(1), 16)
        mn = m.group(3)
        base = mn.split('.')[0]
        if base not in HAZARDS:
            continue
        ops = m.group(4)
        if base == 'tas' and REG_ONLY.match(ops):
            continue                      # register-only: no bus cycle
        where = hits if (code is None or addr in code) else phantom
        where.setdefault(base, []).append((addr, ln.rstrip()))

    total = 0
    for mn in sorted(HAZARDS):
        sites = hits.get(mn, [])
        if not sites:
            continue
        total += len(sites)
        assumes, instead = HAZARDS[mn]
        print('%s — %d site%s' % (mn.upper(), len(sites), '' if len(sites) == 1 else 's'))
        print('  the game assumes: %s' % assumes)
        print('  on 32X:           %s' % instead)
        for addr, _ in sites:
            print('    0x%05X' % addr)
        if mn in phantom:
            live = [a for a, _ in phantom[mn] if a < CODE_CEILING]
            dead = [a for a, _ in phantom[mn] if a >= CODE_CEILING]
            if live:
                print('  CANDIDATES below the code ceiling — VERIFY BY HAND,')
                print('  unreached real code looks exactly like this:')
                for a in live:
                    print('    0x%05X ?' % a)
            if dead:
                print('  %d more above the 0x%X code ceiling: data, no check '
                      'needed' % (len(dead), CODE_CEILING))
        print()
    print('%d real dependency sites across %d classes' % (total, len(hits)))
    if phantom:
        print('%d candidates outside the analysed code — NOT discarded, '
              'listed above for hand checking. Ghidra not reaching an '
              'address does not make it data: 0x150B6 is a real TAS that '
              'the seeded project never reached, and patch_game patches it.'
              % sum(len(v) for v in phantom.values()))


if __name__ == '__main__':
    main()
