#!/usr/bin/env python3
"""PER-VINT CALL CENSUS: what the 68K game code reaches, and what WRAM it needs.

Answers one question with a number: if the game's logic were moved to an SH-2,
how many bytes of state would have to live in memory BOTH CPUs can address?
The SH-2s cannot see 68000 work RAM and the 68000 cannot see SH-2 SDRAM, so the
only shared memory is the framebuffer hole (~58 KB per bank). That hole is the
budget; this tool measures the bill.

Method
  1. Parse the whole program disassembly into instructions.
  2. Build the static call graph: jsr/bsr/jmp/bra/Bcc/DBcc with resolvable
     targets. Register-indirect calls (jsr (aN)) cannot be resolved statically
     and are counted separately -- the game dispatches object handlers that way.
  3. Recover indirect targets from pointer tables: any 4-byte-aligned longword
     in the image whose value lands on an instruction boundary that also looks
     like a routine entry (preceded by rts/rte/jmp/bra, or already a call
     target).
  4. Walk transitively from the vint entry points. Report TWO tiers:
       DIRECT  closure over resolvable edges only          -- lower bound
       +TABLE  DIRECT plus every recovered pointer target  -- upper bound
  5. For every reached instruction, collect absolute operands in 0xFF0000-
     0xFFFFFF (including `lea` table bases, which is how the object arrays are
     addressed) and cluster them into regions.

Limits, stated because they bound the answer
  - A register-indirect target not present in any pointer table is invisible.
  - Register-indirect DATA access (a4/a6 walking an object array) is counted at
    its BASE only; array extent comes from the stride tables where known, so
    the per-region byte count is a floor, not a ceiling.
  - Static reachability is a superset of one vint's actual execution. For the
    port question that is the right direction: a port must handle every path.

    python3 tools/vint_census.py [--asm roms/altbeast/prog68k.asm]
"""
import re, sys, struct, collections

ASM = 'roms/altbeast/prog68k.asm'
BIN = 'roms/altbeast/prog68k.bin'
ENTRIES = {0x2AAC: 'IRQ4 / vblank handler', 0x40E: 'reset / main loop'}

# The game's WRAM state, as REGIONS rather than as the symbol bases the
# disassembly names. The object table is one 64-entry x 128-byte array
# (rom 0x398E: "Table $ffc000-$ffe000 ( 64 sprites 128bytes entry)"), so the
# symbol map -- which names only slot 0's fields -- understates it 50x. An
# `lea (abs).w,aN` followed by a register walk is counted against the whole
# region its base falls in.
REGIONS = [
    (0xFFC000, 0xFFE000, 'object table, 64 slots x 128 B (players, enemies, objects)'),
    (0xFFE000, 0xFFF000, 'secondary object/sprite tables'),
    (0xFFF000, 0xFFF300, 'game globals (scene timer, scroll, camera, level state)'),
    (0xFFF300, 0xFFF400, 'palette-cycle slot timers, 8 B per slot'),
    (0xFFF400, 0xFFF600, 'palette slot allocator: next-free, rotor, set->slot, refcount'),
    (0xFFF600, 0xFFF800, 'palette update queue, 64 entries x 8 B'),
    (0xFFF800, 0xFFFC84, 'misc / high globals'),
    (0xFFFF00, 0xFFFF84, 'top-of-RAM globals'),
]

INS = re.compile(r'^\s*([0-9a-f]+):\t((?:[0-9a-f]{2,4} ?)+)\s*\t(.*)$')
CALL = re.compile(r'^(jsr|bsr[sw]?|jmp|bra[sw]?|b[a-z]{2}[sw]?|db[a-z]{2})\b')
TERM = re.compile(r'^(rts|rte|rtr|jmp|bra[sw]?)\b')
IND  = re.compile(r'^(jsr|jmp)\b.*%a\d@')


def load():
    ins, order = {}, []
    for l in open(ASM, encoding='utf-8', errors='replace'):
        m = INS.match(l)
        if not m:
            continue
        a = int(m.group(1), 16)
        if a in ins:
            continue
        nb = len(m.group(2).replace(' ', '')) // 2
        ins[a] = (nb, m.group(3).strip())
        order.append(a)
    return ins, sorted(order)


def edges(ins):
    """direct -> set of targets; indirect -> count of unresolvable call sites."""
    out, ind = collections.defaultdict(set), []
    for a, (nb, tx) in ins.items():
        if IND.match(tx):
            ind.append(a)
            continue
        if not CALL.match(tx):
            continue
        m = re.search(r'0x([0-9a-f]+)\s*$', tx)
        if m:
            t = int(m.group(1), 16)
            if t in ins:
                out[a].add(t)
    return out, ind


def pointer_targets(ins, order):
    """Longwords in the image that point at a plausible routine entry."""
    img = open(BIN, 'rb').read()
    starts = set(order)
    prev = {}
    for i, a in enumerate(order):
        prev[a] = order[i - 1] if i else None
    entry_like = set()
    for a in order:
        p = prev[a]
        if p is None or TERM.match(ins[p][1]):
            entry_like.add(a)
    found = set()
    for off in range(0, len(img) - 3, 2):          # word-aligned scan
        v = struct.unpack('>I', img[off:off + 4])[0]
        if v in starts and (v in entry_like) and 0x400 <= v < len(img):
            found.add(v)
    return found


def routine_of(a, order, ins):
    """Walk forward to the routine's terminator; return (end, instruction count)."""
    i = order.index(a) if a in order else None
    if i is None:
        return a, 0
    n = 0
    while i < len(order):
        addr = order[i]
        n += 1
        if TERM.match(ins[addr][1]):
            return addr + ins[addr][0], n
        i += 1
    return order[-1], n


def walk(roots, adj, ins, order, extra=()):
    seen, stack = set(), list(roots) + list(extra)
    while stack:
        a = stack.pop()
        if a in seen or a not in ins:
            continue
        seen.add(a)
        # fall through the routine body, following every edge out of it
        i = order.index(a) if a in order else None
        if i is None:
            continue
        while i < len(order):
            addr = order[i]
            seen.add(addr)
            for t in adj.get(addr, ()):
                if t not in seen:
                    stack.append(t)
            if TERM.match(ins[addr][1]):
                break
            i += 1
    return seen


WRAMOP = re.compile(r'0x(ff[0-9a-f]{4,6})\b')


def wram_footprint(addrs, ins):
    hits = collections.Counter()
    for a in addrs:
        for m in WRAMOP.finditer(ins[a][1]):
            v = int(m.group(1), 16)
            if v < 0xFF0000:
                v |= 0xFF0000
            hits[v & 0xFFFFFF] += 1
    return hits


def cluster(addrs, gap=64):
    out = []
    for a in sorted(addrs):
        if out and a - out[-1][1] <= gap:
            out[-1][1] = a
        else:
            out.append([a, a])
    return out


def main():
    ins, order = load()
    adj, ind = edges(ins)
    tbl = pointer_targets(ins, order)
    print(f'program {len(ins)} instructions disassembled, '
          f'{sum(len(v) for v in adj.values())} direct edges, '
          f'{len(ind)} register-indirect call sites, '
          f'{len(tbl)} pointer-table targets recovered\n')
    for label, extra in (('DIRECT (resolvable edges only)', ()),
                         ('+TABLE (plus recovered pointer targets)', tbl)):
        seen = walk(ENTRIES, adj, ins, order, extra)
        code = sum(ins[a][0] for a in seen)
        hits = wram_footprint(seen, ins)
        regs = cluster(hits)
        span = sum(hi - lo + 2 for lo, hi in regs)
        print(f'== {label} ==')
        print(f'   reached instructions : {len(seen):>7}')
        print(f'   reached code bytes   : {code:>7}')
        print(f'   distinct WRAM addrs  : {len(hits):>7}')
        print(f'   WRAM regions (gap<=64): {len(regs):>6}')
        print(f'   WRAM span covered    : {span:>7} bytes')
        print(f'   vs the 58 KB shared hole: {100*span/(58*1024):.1f}%\n')
    seen = walk(ENTRIES, adj, ins, order, tbl)
    hits = wram_footprint(seen, ins)
    print('== THE PORT BILL: WRAM regions the reached set touches ==')
    total = 0
    for lo, hi, name in REGIONS:
        refs = sum(n for a, n in hits.items() if lo <= a < hi)
        if not refs:
            continue
        total += hi - lo
        print(f'   {lo:06X}-{hi:06X}  {hi-lo:>6} B  {refs:>4} static refs  {name}')
    hole = 58 * 1024
    print(f'\n   TOTAL state that must be shared : {total:>6} B '
          f'({total/1024:.1f} KB)')
    print(f'   the framebuffer hole            : {hole:>6} B (58.0 KB)')
    print(f'   => {100*total/hole:.0f}% of the only memory both CPUs can address\n')
    print('== the 12 most-referenced WRAM addresses ==')
    for v, n in hits.most_common(12):
        print(f'   {v:06X}  {n:4} references')
    print('\n== WRAM regions, largest first ==')
    for lo, hi in sorted(cluster(hits), key=lambda r: r[0] - r[1])[:14]:
        print(f'   {lo:06X}-{hi:06X}  {hi-lo+2:>6} bytes')


if __name__ == '__main__':
    main()
