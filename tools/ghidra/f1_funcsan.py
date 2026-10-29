# -*- coding: utf-8 -*-
# Sanity-check the function count. Nobody hand-wrote 2,121 routines; a count
# that large is fragmentation. Even a plausible count hides fragments: a 2-byte
# "function" is a bare rts, a 4-6 byte one is usually a jmp thunk.
#   tools/ghidra_run.sh script f1_funcsan.py
import collections
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
rows = []
for f in fm.getFunctions(True):
    ep = f.getEntryPoint()
    n = f.getBody().getNumAddresses()
    ins, mnem = 0, []
    it = listing.getInstructions(f.getBody(), True)
    while it.hasNext():
        i = it.next(); ins += 1
        if ins <= 3:
            mnem.append(i.getMnemonicString())
    rows.append((ep.getOffset(), n, ins, ' '.join(mnem)))
print('functions: %d' % len(rows))
buck = collections.Counter()
for _, n, _, _ in rows:
    b = ('1: 1-2 B' if n <= 2 else '2: 3-6 B' if n <= 6 else '3: 7-16 B'
         if n <= 16 else '4: 17-64 B' if n <= 64 else '5: 65-256 B'
         if n <= 256 else '6: 257-1K' if n <= 1024 else '7: >1K')
    buck[b] += 1
print('\nSIZE DISTRIBUTION')
for k in sorted(buck):
    print('  %-12s %4d  %5.1f%%' % (k, buck[k], 100.0 * buck[k] / len(rows)))
tiny = [r for r in rows if r[1] <= 6]
print('\n%d functions are <= 6 bytes. What are they?' % len(tiny))
shapes = collections.Counter(r[3] for r in tiny)
for k, v in shapes.most_common(10):
    print('  %4d  %s' % (v, k or '(no instructions)'))
real = [r for r in rows if r[1] > 6]
tot = sum(r[2] for r in rows)
print('\nfunctions > 6 bytes: %d   instructions in them: %d of %d'
      % (len(real), sum(r[2] for r in real), tot))
print('mean size of those: %.0f bytes, %.0f instructions'
      % (sum(r[1] for r in real) / max(1, len(real)),
         sum(r[2] for r in real) / max(1, len(real))))
