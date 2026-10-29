# -*- coding: utf-8 -*-
# Ghidra headless (Jython): second-stage bounding repair.
#   tools/ghidra_run.sh script fix_bounds2.py
#
# fix_bounds.py re-disassembles from the function ENTRY, which cannot get
# past a DATA block sitting in the middle of the body — and that is why
# its 69 extension candidates never moved (LOOP-DECOMPILE 52). The bytes
# after those truncation points are plainly code: 0x16CFC is `jsr 0x3F20`,
# 0x5A12 is `jmp 0x3D14`.
#
# So work at the TRUNCATION POINT instead: clear whatever code unit covers
# it, disassemble there with flow, then recreate the function at its entry
# so the body recomputes. Repeat while progress is being made.
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()

TERM = set(['rts', 'jmp', 'bra.w', 'bra.b', 'rte', 'rtr'])


def survey():
    out = []
    entries = set(f.getEntryPoint().getOffset() for f in fm.getFunctions(True))
    for f in fm.getFunctions(True):
        body = f.getBody()
        last = None
        it = listing.getInstructions(body, True)
        while it.hasNext():
            last = it.next().getMnemonicString()
        if last in TERM:
            continue
        ep = f.getEntryPoint().getOffset()
        end = ep + body.getNumAddresses()
        if end in entries:
            continue
        out.append((ep, end))
    return out


total = 0
for rnd in range(6):
    work = survey()
    if not work:
        break
    fixed = 0
    for ep, end in work:
        addr = af.getAddress(hex(end).rstrip('L'))
        cu = listing.getCodeUnitContaining(addr)
        if cu is not None:
            try:
                listing.clearCodeUnits(cu.getAddress(), addr, False)
            except Exception:
                pass
        if not DisassembleCommand(addr, None, True).applyTo(currentProgram, monitor):
            continue
        epa = af.getAddress(hex(ep).rstrip('L'))
        fm.removeFunction(epa)
        if CreateFunctionCmd(epa).applyTo(currentProgram, monitor):
            fixed += 1
    print('round %d: %d mis-bounded, %d re-formed' % (rnd + 1, len(work), fixed))
    total += fixed
    if fixed == 0:
        break

print('remaining mis-bounded: %d' % len(survey()))
print('functions: %d' % len(list(fm.getFunctions(True))))
