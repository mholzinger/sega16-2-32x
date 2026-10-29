#!/usr/bin/env python3
"""Declared return storage vs the registers the ARCADE actually changes.

    python3 tools/ret_storage_audit.py [vectors.jsonl ...]

WHY. Declaring a function's return storage is what makes Ghidra materialise a
register-producing body (FULL-PORT.md 16: sprite_frame_lookup was 34 bytes rendered
`{ return; }`). But a declaration can be WRONG, and a wrong one is worse than none:

    zoom_scale_lookup   declared ret A0:4,A1:4
                        actually changes D0,D1,D3,D6,D7,A0,A2 -- A1 NEVER changes

Its caller reads the low half of the returned pair as a VALUE, so it reads A1 --
which the function does not touch -- where the real output is D1 (0x40, the sprite
scale). That is why sprite-record fields +6/+7 come out zero (FULL-PORT 27a).

THE HIGH-CONFIDENCE CLASS IS "DECLARED BUT NEVER CHANGES". A register the function
never writes cannot be its output, so the caller is reading whatever was there
before -- no judgement required. "Changes but is not declared" is only a CANDIDATE:
the 68000 has no formal ABI here and a changed register may be scratch.
"""
import collections, glob, json, os, sys

RN = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)]
vecs = sys.argv[1:] or (['docs/proof/l1_vectors.jsonl'] + glob.glob('/tmp/l1_seed*.jsonl'))

changed = collections.defaultdict(lambda: collections.defaultdict(int))
calls = collections.Counter()
for path in vecs:
    if not os.path.exists(path):
        continue
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        v = json.loads(line)
        e, x = v.get('entry'), v.get('exit')
        if not e or not x:
            continue
        c = v['callee']
        calls[c] += 1
        for i in range(8):
            if (e['d'][i] & 0xFFFFFFFF) != (x['d'][i] & 0xFFFFFFFF):
                changed[c]['D%d' % i] += 1
        for i in range(7):
            if (e['a'][i] & 0xFFFFFFFF) != (x['a'][i] & 0xFFFFFFFF):
                changed[c]['A%d' % i] += 1

conv = {}
for e in json.load(open('docs/audit/reg_convention.json')):
    conv[e['entry'].upper().lstrip('0').rjust(5, '0')] = e

print("RETURN-STORAGE AUDIT -- %d callees with entry+exit vectors" % len(calls))
print()
bad = []
for c, n in calls.most_common():
    key = c.upper().lstrip('0').rjust(5, '0')
    e = conv.get(key)
    if not e:
        continue
    decl = set()
    for part in (e.get('ret') or '').split(','):
        part = part.strip()
        if part and part != '<VOID>':
            decl.add(part.split(':')[0].upper().rstrip('WUB'))
    # ONLY `extraout_*` IS AN OUTPUT. Ghidra's own prefixes say which is which:
    # unaff_X means the register is UNAFFECTED (an input that passes through) and
    # in_X is an input. Counting the whole `storage` map as outputs put A6 in nearly
    # every row -- `unaff_A6` never changes BY DEFINITION, which is the third time
    # today a test of mine needed a discriminator rather than a shape.
    for nm, st in (e.get('storage') or {}).items():
        if nm.startswith('extraout_'):
            decl.add(st.split(':')[0].upper().rstrip('WUB'))
    if not decl:
        continue
    never = sorted(r for r in decl if r in RN and changed[c].get(r, 0) == 0)
    if never:
        bad.append((n, c, e.get('name'), e.get('ret'), never,
                    sorted(changed[c], key=lambda r: -changed[c][r])[:6]))

print("DECLARED AS AN OUTPUT BUT NEVER CHANGED BY THE FUNCTION: %d" % len(bad))
print("%6s  %-7s %-28s %-16s %-14s %s" % ("calls", "68K", "name", "declared ret", "NEVER changes", "actually changes"))
for n, c, name, ret, never, act in sorted(bad, key=lambda r: -r[0]):
    print("%6d  0x%-5s %-28s %-16s %-14s %s"
          % (n, c, (name or '?')[:28], (ret or '')[:16], ",".join(never), ",".join(act)))
