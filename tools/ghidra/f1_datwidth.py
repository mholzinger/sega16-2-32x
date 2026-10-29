# -*- coding: utf-8 -*-
# Dump the WIDTH Ghidra assigned to every absolute data address the decompiler
# names, so declarations match what the decompiled C assumes.
#   tools/ghidra_run.sh script f1_datwidth.py OUT.json
#
# WHY (2026-09-27). cport_prep guessed widths from the symbol's first letter, so
# `_DAT_fffff014` -- the PRNG state, which rng_next multiplies by 0x29 and shifts
# right 16 -- was declared `byte`. 89 symbols were declared byte while used in
# wide arithmetic, and that alone can make a correct decompilation fail its
# differential: rng_next's C reproduces both its oracle vectors EXACTLY when
# evaluated at the right width. Ghidra already inferred a type at each address;
# reading it is derivation instead of a guess.
import json
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
out = {}

# 1. every DEFINED data item's own length
it = listing.getDefinedData(True)
while it.hasNext():
    d = it.next()
    try:
        out['%08X' % d.getAddress().getOffset()] = int(d.getLength())
    except Exception:
        pass

# 2. the ACCESS width at every reference, which covers addresses Ghidra never
#    turned into a defined datum. The widest access wins: a cell read as a long
#    somewhere and a byte elsewhere is a long.
from ghidra.program.model.lang import OperandType
itc = listing.getInstructions(True)
while itc.hasNext():
    i = itc.next()
    for r in i.getReferencesFrom():
        t = r.getReferenceType()
        if not (t.isRead() or t.isWrite()):
            continue
        a = r.getToAddress().getOffset()
        mn = i.getMnemonicString().lower()
        w = 4 if mn.endswith('.l') else 2 if mn.endswith('.w') else 1 if mn.endswith('.b') else 0
        if not w:
            continue
        k = '%08X' % a
        if out.get(k, 0) < w:
            out[k] = w

args = getScriptArgs()
# docs/proof/ is gitignored (ROM-derived) but lives in the working tree, so it
# survives a reboot. /tmp did not: both cport_prep.py and gen_selftest.py load
# this table, and a missing file used to make every global 1 byte wide.
p = args[0] if args else 'docs/proof/datwidth.json'
# sort_keys: same bytes under Jython (Ghidra 11.x) and PyGhidra (12.x)
json.dump(out, open(p, 'w'), sort_keys=True)
import collections
c = collections.Counter(out.values())
print('widths for %d addresses -> %s' % (len(out), dict(sorted(c.items()))))
print('wrote %s' % p)
