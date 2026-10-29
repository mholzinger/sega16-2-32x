# -*- coding: utf-8 -*-
# Ghidra headless probe: what does the analysed program hold at addresses
# where a linear objdump sweep reports a palette-field write? Distinguishes
# "unreached code" from "objdump phantom over data".
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory()
import sys
args = getScriptArgs()
for tok in args:
    a = int(tok, 16)
    addr = af.getAddress(hex(a).rstrip('L'))
    ins = listing.getInstructionAt(addr)
    if ins is not None:
        print('0x%X: INSTR %s' % (a, str(ins)))
        continue
    ci = listing.getInstructionContaining(addr)
    if ci is not None:
        print('0x%X: MID-INSTR of 0x%X %s' % (a, ci.getAddress().getOffset(), str(ci)))
        continue
    d = listing.getDataContaining(addr)
    if d is not None:
        print('0x%X: DATA %s at 0x%X' % (a, d.getDataType().getName(), d.getAddress().getOffset()))
    else:
        print('0x%X: UNDEFINED (never analysed)' % a)
