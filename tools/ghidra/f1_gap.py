# -*- coding: utf-8 -*-
# F1.1: is the UNDEFINED space below 0x20000 data, or code we have not reached?
# Big contiguous blobs = data. Scattered small gaps = unreached code.
#   tools/ghidra_run.sh script f1_gap.py
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
LO, HI = 0x400, 0x20000
covered = bytearray(HI - LO)
it = listing.getInstructions(True)
ni = 0
for _ in iter(int, 1):
    if not it.hasNext():
        break
    i = it.next(); ni += 1
    a = i.getAddress().getOffset()
    if LO <= a < HI:
        for k in range(a, min(a + i.getLength(), HI)):
            covered[k - LO] = 1
it = listing.getDefinedData(True)
while it.hasNext():
    d = it.next()
    a = d.getAddress().getOffset()
    if LO <= a < HI:
        for k in range(a, min(a + d.getLength(), HI)):
            covered[k - LO] = 2
gaps = []
s = None
for k in range(len(covered)):
    if covered[k] == 0:
        if s is None: s = k
    else:
        if s is not None: gaps.append((s + LO, k + LO)); s = None
if s is not None: gaps.append((s + LO, HI))
tot = sum(e - b for b, e in gaps)
gaps.sort(key=lambda g: g[1] - g[0], reverse=True)
print('instructions %d; UNDEFINED bytes in 0x%X-0x%X: %d in %d gaps'
      % (ni, LO, HI, tot, len(gaps)))
big = [g for g in gaps if g[1] - g[0] >= 256]
small = [g for g in gaps if g[1] - g[0] < 32]
print('  gaps >= 256 B : %d, totalling %d B (%.0f%% of the undefined space)'
      % (len(big), sum(e - b for b, e in big), 100.0 * sum(e - b for b, e in big) / max(1, tot)))
print('  gaps <  32 B  : %d, totalling %d B' % (len(small), sum(e - b for b, e in small)))
print('\nTOP 12 GAPS:')
for b, e in gaps[:12]:
    print('  0x%05X-0x%05X  %6d B' % (b, e, e - b))
