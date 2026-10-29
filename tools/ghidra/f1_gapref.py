# -*- coding: utf-8 -*-
# F1.1: are the undecoded gaps CODE (unreached routines) or DATA (level layout)?
#   tools/ghidra_run.sh script f1_gapref.py
#
# Mike's framing 2026-09-26: the unreached material is enemy AI and level
# layout. Those are different KINDS -- AI is code reached by dispatch, layout is
# data reached by a pointer load -- so what REFERENCES a gap classifies it,
# and no amount of extra tracing is needed.
listing = currentProgram.getListing()
refs = currentProgram.getReferenceManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress
LO, HI = 0x400, 0x20000

cov = bytearray(HI - LO)
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    a = i.getAddress().getOffset()
    if LO <= a < HI:
        for k in range(a, min(a + i.getLength(), HI)):
            cov[k - LO] = 1
gaps, s = [], None
for k in range(len(cov)):
    if not cov[k]:
        if s is None: s = k
    elif s is not None:
        gaps.append((s + LO, k + LO)); s = None
if s is not None: gaps.append((s + LO, HI))
big = sorted([g for g in gaps if g[1] - g[0] >= 256],
             key=lambda g: g[1] - g[0], reverse=True)

print('%d gaps >= 256 B. Classifying each by the references INTO it.\n' % len(big))
print('%-19s %7s %6s %6s %6s  %s' % ('gap', 'bytes', 'DATA', 'FLOW', 'other', 'verdict'))
tot = {'DATA': 0, 'CODE': 0, 'NONE': 0}
for b, e in big:
    d = f = o = 0
    srcs = set()
    for off in range(b, e, 2):
        for r in refs.getReferencesTo(addr(off)):
            t = r.getReferenceType()
            srcs.add(r.getFromAddress().getOffset())
            if t.isData(): d += 1
            elif t.isFlow(): f += 1
            else: o += 1
    if f and not d:      v = 'CODE (flow refs)'; tot['CODE'] += e - b
    elif d and not f:    v = 'DATA (data refs)'; tot['DATA'] += e - b
    elif d and f:        v = 'MIXED %d/%d' % (d, f); tot['CODE'] += e - b
    else:                v = 'UNREFERENCED'; tot['NONE'] += e - b
    print('0x%05X-0x%05X %7d %6d %6d %6d  %s' % (b, e, e - b, d, f, o, v))
print('\nbytes by verdict: DATA %d  CODE/MIXED %d  UNREFERENCED %d'
      % (tot['DATA'], tot['CODE'], tot['NONE']))
