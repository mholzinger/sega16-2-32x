# -*- coding: utf-8 -*-
# Delete the functions listed in a file (one hex address per line).
# Used for the residue of seed_harvest's phantom sites: entries with no
# terminator, no fall-through, and which the reference disassembly does
# not consider code either (LOOP-DECOMPILE 54). Three independent reasons
# before removing anything.
args = getScriptArgs()
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()
n = 0
for tok in open(args[0]).read().split():
    a = af.getAddress(hex(int(tok, 16)).rstrip('L'))
    if fm.removeFunction(a):
        n += 1
print('removed %d functions' % n)
print('functions now: %d' % len(list(fm.getFunctions(True))))
