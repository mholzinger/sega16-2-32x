#!/usr/bin/env python3
"""The width the ARCADE actually accesses each address at, from the L1 oracle.

    python3 tools/dat_width_observed.py            # -> docs/proof/datwidth_observed.json

WHY THIS EXISTS. `docs/proof/datwidth.json` is GHIDRA's width per address, and
cport_prep.py's own note says guessing widths was worse than using it. But Ghidra
can be wrong, and when it is wrong WIDE the error is not subtle: 0xFFF142 is
declared `ushort`, holds the bytes 01 00, and is used as `(DAT_fffff142 << 2)`
indexing a FIVE-entry pointer table. As a byte the index is 1; as a ushort it is
0x100, so `<< 2` lands 1 KB past the table and floor_collide reads garbage and
never fires its branch (FULL-PORT.md 22).

This table is the arcade's own answer: every read and write in
`docs/proof/l1_vectors.jsonl` carries its WIDTH, so the maximum observed width at
an address is a measurement rather than an inference. It is evidence of a CEILING
only -- the oracle is one window, so an address it never accessed wide may still be
accessed wide elsewhere. cport_prep.py therefore clamps only when a SECOND
declared symbol sits inside the declared span, which makes the wide reading
impossible rather than merely unobserved.
"""
import collections, json, os, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'docs/proof/l1_vectors.jsonl'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'docs/proof/datwidth_observed.json'
if not os.path.exists(SRC):
    raise SystemExit('dat_width_observed: %s missing -- capture it with '
                     'tools/l1_oracle.lua' % SRC)

mx = collections.defaultdict(int)
mn = {}
n = 0
for line in open(SRC):
    line = line.strip()
    if not line:
        continue
    v = json.loads(line)
    n += 1
    for key in ('reads', 'writes'):
        for e in (v.get(key) or []):
            a = int(e[0], 16) & 0xFFFFFF
            w = int(e[2])
            if w > mx[a]:
                mx[a] = w
            if a not in mn or w < mn[a]:
                mn[a] = w

out = {'%08X' % (0xFF000000 | a if a >= 0xFF0000 else a): w for a, w in mx.items()}
json.dump(out, open(OUT, 'w'), indent=0, sort_keys=True)
# AND THE NARROWEST, which is a different authority for a different question. The
# MAX bounds what a scalar can be (declared wider than ever accessed = wrong). The
# MIN is the element size of a symbol used as an ARRAY BASE: `(&sym)[i]` indexes in
# the declared type, so the narrowest access the arcade makes there is the stride
# the 68K meant. tools/gen_selftest.py:665 has used the narrowest since 2026-09-27.
out_min = {'%08X' % (0xFF000000 | a if a >= 0xFF0000 else a): w for a, w in mn.items()}
json.dump(out_min, open(OUT.replace('.json', '_min.json'), 'w'), indent=0, sort_keys=True)
print('  narrowest -> %s' % OUT.replace('.json', '_min.json'))
print('dat_width_observed: %d vectors -> %d addresses with an observed width' % (n, len(out)))
print('  -> %s' % OUT)
