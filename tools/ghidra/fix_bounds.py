# -*- coding: utf-8 -*-
# Ghidra headless (Jython): repair function bounding.
#   tools/ghidra_run.sh script fix_bounds.py
#
# Two defects, both from tools/ghidra/seed_harvest.py reading the LINEAR
# objdump listing without filtering source sites to real code — a phantom
# `move.l #imm,d(aN)` over data yields a bogus function seed
# (docs/log/LOOP-DECOMPILE.md 52):
#
#   DELETE  a function whose body is mostly zero bytes and which nothing
#           calls. Those sit in padding; 0x0000 disassembles as
#           `ori.b #0,d0`, which is why they looked like code at all.
#   EXTEND  a function whose body ends mid-stream at an address that is
#           NOT another function's entry. Ghidra stopped early; deleting
#           and recreating makes it recompute the body from flow.
#
# A function ending mid-stream exactly where the next one begins is a
# FALL-THROUGH and is left alone — normal hand-written 68000.
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

TERM = set(['rts', 'jmp', 'bra.w', 'bra.b', 'rte', 'rtr'])
ZERO_FRAC = 0.45

funcs = list(fm.getFunctions(True))
entries = set(f.getEntryPoint().getOffset() for f in funcs)

to_delete, to_extend = [], []
for f in funcs:
    body = f.getBody()
    last = None
    it = listing.getInstructions(body, True)
    while it.hasNext():
        last = it.next().getMnemonicString()
    if last in TERM:
        continue
    ep = f.getEntryPoint()
    n = body.getNumAddresses()
    if (ep.getOffset() + n) in entries:
        continue                       # fall-through, correct as it stands
    zeros = 0
    for k in range(n):
        try:
            if mem.getByte(ep.add(k)) == 0:
                zeros += 1
        except Exception:
            break
    callers = len(list(f.getCallingFunctions(monitor)))
    if n and float(zeros) / n > ZERO_FRAC and callers == 0:
        to_delete.append(ep)
    else:
        to_extend.append(ep)

print('mis-bounded: %d to delete (zero-bodied, uncalled), %d to extend'
      % (len(to_delete), len(to_extend)))

deleted = 0
for ep in to_delete:
    if fm.removeFunction(ep):
        deleted += 1

extended = 0
for ep in to_extend:
    fm.removeFunction(ep)
    DisassembleCommand(ep, None, True).applyTo(currentProgram, monitor)
    if CreateFunctionCmd(ep).applyTo(currentProgram, monitor):
        extended += 1

print('deleted %d, recreated %d' % (deleted, extended))
print('functions now: %d' % len(list(fm.getFunctions(True))))
