#!/usr/bin/env python3
"""Census of ARCADE-BEHAVIOUR DEPENDENCIES in a System 16 program.

    tools/hazard_census.py LISTING.dis [--code INSTRS.txt]
    tools/hazard_census.py --stream docs/audit/code_stream.txt

Finds the instruction classes that do not behave the same way on a 32X as
they do on a System 16B board. This is the generalisable half of the kit:
the reason each one matters is a property of the HARDWARE, so a rule
learned on one title applies to every title (docs/log/LOOP-DECOMPILE.md 37).

--code takes the address list from tools/ghidra/instr_list.py. Without it
the scan runs over the raw listing and WILL report phantoms, because a
linear sweep disassembles data as instructions: on Altered Beast that adds
4 false TAS sites, 50 false CHK and 673 false MOVEP. Always pass --code.

--stream takes tools/code_stream.py's output, which is the REFERENCE
disassembly's own 19137 instruction addresses decoded one by one. It needs
no Ghidra and no listing, and it is wider than the function map: 0x150B6
is a real TAS in code the seeded project never reached.

VALIDATION: with --code this reproduces tools/game_altbeast.py's
hand-derived TAS_SITES exactly — the same five addresses, no more and no
fewer. With --stream it finds the same five, and the same twelve STOP
sites entry 37 reported. That is the check that the method is sound before
it is pointed at a title nobody has hand-derived.

THE SECOND HALF (LOOP-DECOMPILE 76) is not about mnemonics. A port has to
answer "what arcade hardware does this program touch, in which direction,
at what width" for every address, because each one needs somewhere to land
in the 32X map. --stream prints that surface. It is the part that
transfers: the S16B hardware map is the same for every title on the board,
so the regions and their rules are written once and the addresses are
re-derived per game.
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


# System 16B hardware map. The regions are a property of the BOARD, so
# this table is the same for every title; only which addresses inside them
# a given game touches changes. Sources: the accesses this census finds,
# cross-read against srcref/jtcores' s16b memory decode.
# A16 inside the VRAM region is the whole split: jts16b_main.v:354 has
# `vram_cs <= ... active[REG_VRAM] && !A[16]` and :329 has
# `char_cs <= active[REG_VRAM] && A[16]`. So 0x400000 is the 32 kB tilemap
# and 0x410000 is the 4 kB TEXT layer — the same chip select, not a
# mirror. Sizes are in the comments at :314-323 (char 4 kB, objram 2 kB,
# pal 4 kB, vram 32 kB), so anything above a region's size aliases.
REGIONS = [
    (0x3F0000, 0x400000, 'tile bank latch (tbank_cs)',
     'reached once, by the MOVEP at 0x0047A'),
    (0x400000, 0x410000, 'tilemap VRAM, 32 kB — scr1 and scr2',
     'jts16b_main.v:354 (A16=0). The port stages it in work RAM and ships '
     'it over the FB packet; nothing addresses it absolutely, which is why '
     'the interception is at the RLE loop heads and not at an address.'),
    (0x410000, 0x440000, 'TEXT layer, 4 kB — char_cs',
     'jts16b_main.v:329 (A16=1). Same chip select as the tilemap, not a '
     'mirror of it.'),
    (0x440000, 0x480000, 'sprite RAM, 2 kB',
     'one writer, the IRQ4 loop at 0x2B16 (LOOP-DECOMPILE 13)'),
    (0x840000, 0x850000, 'palette RAM, 4 kB',
     'the live palette the port reads from work RAM 0xFF9000 instead'),
    (0xC40000, 0xC50000, 'I/O: coin, DIPs, controls, display gate',
     'byte-wide and ODD addresses only — jts16b_main.v:489 returns '
     '{8\'hff, cab_dout}, so the board wires the low byte and the high '
     'byte reads as 0xFF'),
    (0xC60000, 0xC70000, 'watchdog', 'not touched by this title'),
]


def region_of(v):
    for lo, hi, name, note in REGIONS:
        if lo <= v < hi:
            return name, note
    return None, None


ABS = re.compile(r'0x([0-9a-f]{4,8})')
RMW = ('bset', 'bclr', 'bchg', 'add', 'sub', 'and', 'or', 'eor', 'neg',
       'not', 'asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'nbcd', 'tas')


def classify(mn, ops, tok, span):
    """ADDRESS-TAKEN / READ / WRITE / RMW for one absolute operand."""
    base = mn.rstrip('bwl') if mn[-1:] in 'bwl' else mn
    if base in ('lea', 'pea'):
        return 'addr'
    if base in ('cmp', 'cmpi', 'tst', 'btst'):
        return 'read'
    is_dest = span[1] == len(ops.rstrip())
    if base in ('clr', 'st', 'sf', 'move', 'movea', 'moveq', 'movem', 'movep'):
        return 'write' if is_dest else 'read'
    if base in RMW:
        return 'rmw' if is_dest else 'read'
    return 'write' if is_dest else 'read'


def width_of(mn):
    # btst/bset/bclr/bchg on MEMORY are always byte on a 68000, whatever
    # the assembler prints.
    if mn.startswith(('btst', 'bset', 'bclr', 'bchg')):
        return 'b'
    return mn[-1] if mn[-1:] in 'bwl' else '-'


def surface(rows):
    import collections
    tab = collections.defaultdict(
        lambda: {'read': set(), 'write': set(), 'rmw': set(),
                 'addr': 0, 'sites': [], 'rmw_sites': []})
    sr = []
    romstore = []
    for addr, mn, ops in rows:
        # `.short` is objdump saying it could not decode the word. The
        # reference's instruction list carries five of them; they are not
        # stores and must not be counted as any class.
        if mn.startswith('.'):
            continue
        if mn.startswith(('b', 'db', 'jsr', 'jmp')) and mn not in (
                'bset', 'bclr', 'bchg', 'btst'):
            continue
        # ONLY the SR. A write to the CCR sets X/C for the next
        # instruction and has nothing to do with interrupts; there are 76
        # of those and listing them buries the 12 that matter. `move
        # %sr,%dn` is unprivileged on a 68000 and the 32X's 68000 is the
        # same part, so it is not a hazard either -- kept separate, not
        # dropped, because it IS one on a 68010.
        if re.search(r',\s*%sr$', ops):
            sr.append((addr, mn, ops))
        for m in ABS.finditer(ops):
            v = int(m.group(1), 16)
            if v >= 0x80000000:
                v &= 0xFFFFFFFF
            kind = classify(mn, ops, m.group(0), m.span())
            if v < 0x40000 and kind in ('write', 'rmw'):
                romstore.append((addr, mn, ops))
            if region_of(v)[0] is None:
                continue
            e = tab[v]
            if kind == 'addr':
                e['addr'] += 1
            else:
                e[kind].add(width_of(mn))
                if kind == 'rmw':
                    e['rmw_sites'].append(addr)
            e['sites'].append(addr)
    return tab, sr, romstore


def print_surface(tab, sr, romstore):
    direct = {v: e for v, e in tab.items()
              if e['read'] or e['write'] or e['rmw']}
    print('=' * 68)
    print('ARCADE HARDWARE SURFACE — %d addresses accessed directly, '
          '%d more only as a base' % (len(direct), len(tab) - len(direct)))
    print('Every one needs somewhere to land in the 32X map.')
    print('=' * 68)
    cur = None
    for v in sorted(tab):
        name, note = region_of(v)
        if name != cur:
            cur = name
            print()
            print('--- %s' % name)
            print('    %s' % note)
        e = tab[v]
        if not (e['read'] or e['write'] or e['rmw']):
            continue
        d = []
        for k, lab in (('read', 'R'), ('write', 'W'), ('rmw', 'RMW')):
            if e[k]:
                d.append('%s%s' % (lab, ''.join(sorted(e[k]))))
        print('    0x%06X  %-12s %2d site%-1s  first 0x%s'
              % (v, ' '.join(d), len(e['sites']),
                 '' if len(e['sites']) == 1 else 's', e['sites'][0]))
    print()
    print('--- INTERRUPT MASK — %d writes to the SR' % len(sr))
    print('    the game assumes: the arcade\'s interrupt levels')
    print('    on 32X:           the IRQ sources differ, so a mask written'
          ' here can hide')
    print('                      an interrupt the port needs, or admit one'
          ' the game has')
    print('                      no handler for. #0x2700 masks everything,'
          ' #0x2300')
    print('                      admits levels 4 and up -- the vblank the'
          ' port drives.')
    import collections
    byval = collections.Counter(ops for _, _, ops in sr)
    for v, c in byval.most_common():
        print('    %-22s %d site%s' % (v, c, '' if c == 1 else 's'))
    for addr, mn, ops in sr:
        print('    0x%s  %s %s' % (addr, mn, ops))
    print()
    wo = [(v, e) for v, e in tab.items()
          if 0xC40000 <= v < 0xC50000 and e['rmw']]
    print('--- READ-MODIFY-WRITE ON A WRITE-ONLY I/O LATCH — %d site%s'
          % (len(wo), '' if len(wo) == 1 else 's'))
    print('    the game assumes: a read of 0xC40001 returns 0xFF.')
    print('                      jts16b_cabinet.v:199-202 — for'
          ' A[13:12]==0 the case')
    print('                      arm only LATCHES flip and video_en from'
          ' cpu_dout and')
    print('                      never assigns cab_dout, so the read takes'
          ' the 8\'hff')
    print('                      default at :189. The bclr therefore'
          ' writes 0xBF:')
    print('                      flip off, video ON. It only looks'
          ' harmless.')
    print('    on 32X:           the substitute MUST return 0xFF for this'
          ' read. Anything')
    print('                      else and the write half puts an arbitrary'
          ' byte in the')
    print('                      video latch. Every other site writes the'
          ' shadow at')
    print('                      0xFFF018 instead; these bypass it.')
    for v, e in sorted(wo):
        print('    0x%06X  %s'
              % (v, ' '.join('0x' + x for x in sorted(set(e['rmw_sites'])))))
    print()
    print('--- STORES INTO ROM SPACE — %d sites' % len(romstore))
    if not romstore:
        print('    NONE. The program never writes below 0x40000, so the'
              ' +0x900000')
        print('    rebase cannot be undone at runtime and no code is'
              ' self-modifying.')
        print('    A clean class is still a kit rule: check it per title,'
              ' it is cheap.')
    for addr, mn, ops in romstore:
        print('    0x%s  %s %s' % (addr, mn, ops))


# Raw opcode patterns, for the sweep over addresses the reference does NOT
# call code. --stream alone cannot see those, and entry 37's whole lesson
# is that 0x150B6 is a real TAS that no analysis reached: patch_game found
# it by hand. A class that only looks where the disassembler already looked
# will miss the next one too.
#
# ONLY DISTINCTIVE OPCODES ARE SWEPT, and that is a deliberate limit.
# STOP/RESET/TRAPV are exact words and TAS is a tight range with a mode
# filter, so a hit in data is rare and cheap to dismiss. CHK and MOVEP are
# loose patterns — MOVEP's `(w & 0xF038) == 0x0008` matches the `0000 xxxx`
# longword that every rom pointer table is made of, which is where entry
# 37's "673 false MOVEP" came from. Sweeping them produces a list nobody
# will read, and a list nobody reads is worse than no list.
OPC = {
    'tas':   lambda w: (w & 0xFFC0) == 0x4AC0 and w != 0x4AFC
                       and (w & 0x38) != 0x00 and (w & 0x38) != 0x08,
    'stop':  lambda w: w == 0x4E72,
    'reset': lambda w: w == 0x4E70,
    'trapv': lambda w: w == 0x4E76,
}
# the 68000 vector table is data by definition; sweeping it is pure noise.
VECTORS = 0x400


def sweep(rom, known, ceiling):
    """Hazard opcodes at addresses the reference does not call code."""
    out = {}
    for a in range(VECTORS, min(len(rom) - 1, ceiling), 2):
        if a in known:
            continue
        w = (rom[a] << 8) | rom[a + 1]
        for mn, test in OPC.items():
            if test(w):
                out.setdefault(mn, []).append(a)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('listing', nargs='?')
    ap.add_argument('--code', help='instruction address list (one hex per line)')
    ap.add_argument('--stream', help='tools/code_stream.py output')
    ap.add_argument('--rom', help='rom image for the candidate sweep')
    a = ap.parse_args()

    if a.stream:
        rows = []
        for ln in open(a.stream):
            f = ln.rstrip('\n').split('\t')
            if len(f) >= 4:
                rows.append((f[0], f[2], f[3]))
        known = set(int(r[0], 16) for r in rows)
        romfile = a.rom or 'roms/altbeast/prog68k.bin'
        cands = sweep(open(romfile, 'rb').read(), known, CODE_CEILING)
        hits = {}
        for addr, mn, ops in rows:
            base = mn.rstrip('bwl') if mn[-1:] in 'bwl' else mn
            if base in HAZARDS:
                if base == 'tas' and REG_ONLY.match(ops):
                    continue
                hits.setdefault(base, []).append(int(addr, 16))
        for mn in sorted(hits):
            assumes, instead = HAZARDS[mn]
            print('%s — %d site%s' % (mn.upper(), len(hits[mn]),
                                      '' if len(hits[mn]) == 1 else 's'))
            print('  the game assumes: %s' % assumes)
            print('  on 32X:           %s' % instead)
            for v in hits[mn]:
                print('    0x%05X' % v)
            cand = cands.get(mn, [])
            if cand:
                print('  CANDIDATES the reference does not call code — '
                      'VERIFY BY HAND.')
                print('  Unreached real code looks exactly like this: '
                      '0x150B6 is a')
                print('  live TAS that patch_game had to find by hand '
                      '(entry 37).')
                for v in cand:
                    print('    0x%05X ?' % v)
            print()
        for mn in sorted(cands):
            if mn in hits:
                continue
            print('%s — 0 sites in the reference code, %d candidate%s outside '
                  'it' % (mn.upper(), len(cands[mn]),
                          '' if len(cands[mn]) == 1 else 's'))
            for v in cands[mn][:12]:
                print('    0x%05X ?' % v)
            if len(cands[mn]) > 12:
                print('    ... %d more' % (len(cands[mn]) - 12))
            print()
        print_surface(*surface(rows))
        return

    if not a.listing:
        ap.error('give a listing, or --stream')

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
