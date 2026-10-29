# -*- coding: utf-8 -*-
# EXHAUSTIVE decode of the code band, plus Ghidra's own analysers.
#   tools/ghidra_run.sh script f1_exhaust.py [lo] [hi]
# Disassemble at every even offset that is not already code or defined data,
# then run the function/reference analysers so the call graph closes.
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.program.model.address import AddressSet

args = getScriptArgs()
LO = int(args[0], 16) if len(args) > 0 else 0x400
HI = int(args[1], 16) if len(args) > 1 else 0x1B000

listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress

def ndecoded():
    n = 0
    it = listing.getInstructions(True)
    while it.hasNext():
        it.next(); n += 1
    return n

print('code band 0x%X-0x%X; instructions before: %d' % (LO, HI, ndecoded()))

# pass 1: disassemble every even gap start, repeatedly, until nothing new lands
for rnd in range(1, 7):
    tries = 0
    o = LO
    while o < HI:
        if listing.getInstructionAt(addr(o)) is None and listing.getDataAt(addr(o)) is None:
            DisassembleCommand(addr(o), None, True).applyTo(currentProgram)
            tries += 1
        o += 2
    n = ndecoded()
    print('  pass %d: %d attempts -> %d instructions' % (rnd, tries, n))
    if tries == 0:
        break

# pass 2: make a function wherever something is CALLED
made = 0
it = listing.getInstructions(True)
sites = []
while it.hasNext():
    i = it.next()
    fl = i.getFlowType()
    if fl.isCall():
        for r in i.getReferencesFrom():
            if r.getReferenceType().isFlow():
                sites.append(r.getToAddress())
fm = currentProgram.getFunctionManager()
for t in set(sites):
    if fm.getFunctionAt(t) is None and listing.getInstructionAt(t) is not None:
        if CreateFunctionCmd(t).applyTo(currentProgram):
            made += 1
print('functions created from call targets: %d' % made)
print('instructions after: %d; functions %d'
      % (ndecoded(), fm.getFunctionCount()))
