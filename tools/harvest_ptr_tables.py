#!/usr/bin/env python3
"""Harvest the absolute-pointer jump tables the port dispatches through.

    python3 tools/harvest_ptr_tables.py > docs/proof/ptr_table_targets.txt

WHY. A call-graph walk cannot cross an indirect call, and the object system has TWO
layers of them: object_dispatcher calls a handler through slot+2, and the handler
then dispatches a STATE MACHINE through a jump table --
`FUN_0000ee80` does `port_call68(*(uint *)(&DAT_0000f556 + (state & 0xf) * 4))`.
docs/proof/object_handlers_union.txt covers the first layer. This covers the second.

Measured 2026-09-28: without these the state routines were neither in the port's
reachable set nor in its dispatch table, so every state call was counted as a miss
and the state machines never ran.

THE SENTINEL IS "IS IT A KNOWN FUNCTION ENTRY". These tables are not terminated:
&DAT_0000f556 has 5 real entries (0x0EFD2, 0x0F024, 0x0F19A, 0x0F1AA, 0x0F1C0) and
then other data that reads as 0x80000000, 0xEE80011E, 0x0002002F... Stopping at the
first entry that is not an entry point in docs/audit/func_ranges.txt gets exactly
the 5, and needs no guess about table length.
"""
import re, struct, sys

PROG = 'roms/altbeast/prog68k.bin'
# table bases the port's indirect call sites actually use, with how they are
# reached. Addresses come from the call-site expressions in the generated unit.
BASES = [
    (0x0F556, 1, '&DAT_0000f556 + (state & 0xf) * 4  -- object state machine'),
    (0x1A076, 1, '0x1a076 + obj[0x20] * 4'),
    (0x06D90, 1, '&PTR_FUN_00006d90 + obj[0x6a] * 4'),
    # TWO LEVELS: 0x6DA0 holds pointers to TABLES, and those hold the function
    # pointers. Harvesting only level 1 put the level-2 targets in neither the
    # port's reachable set nor its dispatch table -- measured 2026-09-29, site 1
    # then missed 80 times a frame on target 0x053A8.
    (0x06DA0, 2, '&PTR_FUN_00006da0 + n * 4  (pointers to tables of pointers)'),
    (0x17E24, 1, '&PTR_FUN_00017e24 + DAT_fffff142 * 4'),
]

entries = set()
for line in open('docs/audit/func_ranges.txt'):
    if line.startswith('#'):
        continue
    f = line.split()
    if len(f) == 4:
        entries.add(int(f[2], 16))

d = open(PROG, 'rb').read()
out, rows, n_known = [], [], 0
def walk(base):
    """Entries of one absolute-pointer table, to the sentinel."""
    got = []
    for i in range(64):                      # a hard cap; the sentinel ends it
        a = base + i * 4
        if a + 4 > len(d):
            break
        v = struct.unpack('>I', d[a:a + 4])[0]
        # SENTINEL: a plausible 68K code address -- even, and inside the 256 KB
        # program. "Is it a known function entry" is the stricter test and was
        # tried first, but it is CIRCULAR here: 0x0EFD2 is not an entry until this
        # harvest makes it one, so the strict rule stopped 0xF556's table at zero
        # entries. The range rule ends that table at exactly 5, because entry 5
        # reads 0x80000000 and is out of range.
        if not (0x400 <= v < 0x40000) or (v & 1):
            break
        got.append(v)
    return got

for base, depth, why in BASES:
    lvl1 = walk(base)
    got = list(lvl1)
    if depth == 2:
        for t in lvl1:
            got.extend(walk(t))
    got = sorted(set(got))
    for v in got:
        if v in entries:
            n_known += 1
    rows.append((base, why, got))
    out.extend(got)

print('# %d jump-table targets, harvested by tools/harvest_ptr_tables.py.'
      % len(set(out)))
print('# %d of them are ALREADY function entries; the rest need '
      'tools/ghidra/handler_entries.py.' % n_known)
print('# Sentinel: stop at the first entry that is not an even address inside the 256 KB')
print('# program. These tables carry no terminator; see the source for why not the strict rule.')
for base, why, got in rows:
    print('# 0x%05X  %-46s %d entries' % (base, why, len(got)))
for v in sorted(set(out)):
    print('0x%05X' % v)
