# -*- coding: utf-8 -*-
# Ghidra headless (Jython) probe: dump operand representations for a few
# known addresses so a real scanner can be written against the real API
# output rather than a guess. Run via tools/ghidra_run.sh script probe_ops.py
from ghidra.program.model.address import GenericAddress

listing = currentProgram.getListing()
af = currentProgram.getAddressFactory()

for a in (0x1FE0, 0x253C, 0x24D4, 0x3B6C, 0x9090, 0x5D18):
    addr = af.getAddress(hex(a).rstrip('L'))
    ins = listing.getInstructionAt(addr)
    if ins is None:
        print('0x%X: NO INSTRUCTION' % a)
        continue
    reps = []
    for i in range(ins.getNumOperands()):
        reps.append('op%d[type=0x%X]=%r' % (i, ins.getOperandType(i),
                                            ins.getDefaultOperandRepresentation(i)))
    print('0x%X: %s | %s | %s' % (a, ins.getMnemonicString(), str(ins), ' ; '.join(reps)))
