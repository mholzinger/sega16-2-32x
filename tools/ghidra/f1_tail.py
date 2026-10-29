# -*- coding: utf-8 -*-
# Who owns the shared-tail labels that 7 functions `goto` into?
#   tools/ghidra_run.sh script f1_tail.py
# Seven of the 14 functions with a cross-function goto jump into the SAME label
# set 0x228C..0x2490, which is the signature of a shared tail rather than wrong
# bounds. Report what contains those addresses and who reaches them.
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
refs = currentProgram.getReferenceManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress
LABS = [0x228c, 0x22f8, 0x2338, 0x2344, 0x2384, 0x23dc, 0x2490,
        0x97c, 0xa38, 0x24c0, 0x24a4, 0x7032]
for a in LABS:
    ad = addr(a)
    f = fm.getFunctionContaining(ad)
    ins = listing.getInstructionAt(ad)
    src = []
    for r in refs.getReferencesTo(ad):
        fr = fm.getFunctionContaining(r.getFromAddress())
        src.append('%s@0x%05X' % (r.getReferenceType().getName(),
                                  r.getFromAddress().getOffset()))
    print('0x%05X  in %-22s  %-26s  refs: %s'
          % (a, f.getName() if f else '(NO FUNCTION)',
             ins.toString() if ins else '(not code)',
             ', '.join(src[:4]) + (' ...' if len(src) > 4 else '')))
print()
# what is the extent of the tail? walk from 0x228C to its terminator
ad = addr(0x228c)
f = fm.getFunctionContaining(ad)
if f:
    print('function containing 0x228C: %s  body 0x%05X-0x%05X (%d bytes)'
          % (f.getName(), f.getBody().getMinAddress().getOffset(),
             f.getBody().getMaxAddress().getOffset(),
             f.getBody().getNumAddresses()))
    print('  callers: %d' % len(f.getCallingFunctions(None)))
