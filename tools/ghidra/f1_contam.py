# -*- coding: utf-8 -*-
# How much "decoded code" is actually DATA misread? 0x0000 disassembles as
# `ori.b #0,d0`, so runs of ori.b are the signature of zero-fill or tables
# decoded as instructions (2026-09-26: 73 of 98 sub-7-byte "functions" start
# with one).
#   tools/ghidra_run.sh script f1_contam.py
import collections
listing = currentProgram.getListing()
mem = currentProgram.getMemory()
runs, cur, tot, n = [], None, 0, 0
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next(); n += 1
    a = i.getAddress().getOffset()
    suspect = False
    if i.getMnemonicString() == 'ori.b':
        try:
            suspect = (mem.getShort(i.getAddress()) & 0xFFFF) == 0
        except Exception:
            suspect = True
    if suspect:
        tot += 1
        if cur and a - cur[-1] <= 4: cur.append(a)
        else:
            if cur: runs.append(cur)
            cur = [a]
    elif cur:
        runs.append(cur); cur = None
if cur: runs.append(cur)
runs.sort(key=len, reverse=True)
byts = sum(len(r) * 2 for r in runs)
print('decoded instructions: %d' % n)
print('ori.b on a 0x0000 word: %d instructions in %d runs, ~%d bytes (%.2f%% of code)'
      % (tot, len(runs), byts, 100.0 * byts / 79676))
print('\nlongest runs (a long run of zero words is DATA, not code):')
for r in runs[:10]:
    print('  0x%05X-0x%05X  %4d words' % (r[0], r[-1], len(r)))
