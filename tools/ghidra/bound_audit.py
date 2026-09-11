# -*- coding: utf-8 -*-
# Honest bounding audit. A Ghidra function body is an ADDRESS SET, often
# several ranges, so `entry + numAddresses` is NOT the end and any test
# built on it is measuring nothing (LOOP-DECOMPILE 53).
# Ask instead: does the body CONTAIN a terminator, and does its last
# instruction end a flow?
args = getScriptArgs()
out = args[0] if args else '/tmp/bound_audit.txt'
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
TERM = set(['rts', 'jmp', 'bra.w', 'bra.b', 'rte', 'rtr'])
tot = noterm = multi = fell = 0
entries = set(f.getEntryPoint().getOffset() for f in fm.getFunctions(True))
lines = []
for f in fm.getFunctions(True):
    b = f.getBody()
    has = False
    last = None
    lastins = None
    it = listing.getInstructions(b, True)
    while it.hasNext():
        i = it.next()
        lastins = i
        last = i.getMnemonicString()
        if last in TERM:
            has = True
    tot += 1
    if b.getNumAddressRanges() > 1:
        multi += 1
    # A function that runs off its last instruction straight into ANOTHER
    # FUNCTION'S ENTRY is a fall-through and is correctly bounded — it
    # shares the next function's exit. 0x3952 (set_level_palettes, read by
    # hand in LOOP-DECOMPILE 3) is exactly this: it ends at 0x3972, which
    # is a function, and the rts at 0x397C belongs to that one.
    if not has and lastins is not None:
        nxt = lastins.getAddress().getOffset() + lastins.getLength()
        if nxt in entries:
            has = True
            fell += 1
    if not has:
        noterm += 1
        lines.append('0x%X %d %s' % (f.getEntryPoint().getOffset(),
                                     b.getNumAddresses(), last))
print('functions %d' % tot)
print('  bodies with MORE THAN ONE RANGE (normal, not a defect): %d' % multi)
print('  no terminator, but FALL THROUGH to another entry (fine): %d' % fell)
print('  bodies containing NO terminator and no fall-through:     %d' % noterm)
with open(out, 'w') as fh:
    fh.write('\n'.join(lines))
