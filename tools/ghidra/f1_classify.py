# -*- coding: utf-8 -*-
# Emit a per-byte classification of the program image for the coverage heat map.
#   tools/ghidra_run.sh script f1_classify.py OUT.bin
# One byte per program byte:
#   0 unknown (undecoded, unreferenced)   1 decoded code
#   2 defined data                        3 undecoded but DATA-referenced
listing = currentProgram.getListing()
refs = currentProgram.getReferenceManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress
N = 0x40000
cls = bytearray(N)

it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    a = i.getAddress().getOffset()
    for k in range(a, min(a + i.getLength(), N)):
        cls[k] = 1
it = listing.getDefinedData(True)
while it.hasNext():
    d = it.next()
    a = d.getAddress().getOffset()
    for k in range(a, min(a + d.getLength(), N)):
        if cls[k] == 0:
            cls[k] = 2
# undecoded bytes that something references as DATA
for off in range(0, N, 2):
    if cls[off]:
        continue
    for r in refs.getReferencesTo(addr(off)):
        if r.getReferenceType().isData():
            cls[off] = 3
            if off + 1 < N and not cls[off + 1]:
                cls[off + 1] = 3
            break

args = getScriptArgs()
out = args[0] if args else '/tmp/f1_class.bin'
with open(out, 'wb') as fh:
    fh.write(cls)
from collections import Counter
c = Counter(cls)
print('classified %d bytes -> %s' % (N, out))
for k, name in ((1, 'decoded code'), (2, 'defined data'),
                (3, 'data-referenced'), (0, 'UNKNOWN')):
    print('  %-16s %7d  %5.1f%%' % (name, c[k], 100.0 * c[k] / N))
