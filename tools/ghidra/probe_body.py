# -*- coding: utf-8 -*-
# Is a function body CONTIGUOUS? getNumAddresses() counts the address SET,
# so entry+size is only the end when the body is one range.
args = getScriptArgs()
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
for tok in args:
    a = int(tok, 16)
    f = fm.getFunctionAt(af.getAddress(hex(a).rstrip('L')))
    if f is None:
        print('0x%X: no function' % a); continue
    b = f.getBody()
    lo = b.getMinAddress().getOffset(); hi = b.getMaxAddress().getOffset()
    n = b.getNumAddresses()
    last = None
    it = listing.getInstructions(b, True)
    while it.hasNext():
        last = it.next()
    print('0x%X: ranges=%d numAddr=%d span=%d contiguous=%s last=%s @0x%X'
          % (a, b.getNumAddressRanges(), n, hi - lo + 1,
             (hi - lo + 1) == n, last.getMnemonicString() if last else None,
             last.getAddress().getOffset() if last else 0))
