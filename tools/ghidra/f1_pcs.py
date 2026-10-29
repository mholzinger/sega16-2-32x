# -*- coding: utf-8 -*-
# F1.1: disassemble addresses the ORACLE was observed EXECUTING.
#   tools/ghidra_run.sh script f1_pcs.py /tmp/missing_pcs.json
#
# Executed code is ground truth: it needs no heuristic and resolves every
# dispatch by construction. A MAME trace of `mame altbeast` (the always-valid
# oracle, tools/arcade_trace.lua with AT_PLAY=1 driving a level-1 playthrough)
# is therefore the one seed source that cannot be circular.
#
# ASYMMETRY TO REMEMBER: an executed PC PROVES code. The ABSENCE of executed
# PCs in a gap proves nothing -- one playthrough does not reach every routine.
# So this closes the set from below and never licenses calling a gap data.
import json
from ghidra.app.cmd.disassemble import DisassembleCommand

args = getScriptArgs()
pcs = [int(s, 16) for s in json.load(open(args[0]))]
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress

todo = [p for p in pcs if listing.getInstructionAt(addr(p)) is None]
print('executed PCs given: %d; undecoded: %d' % (len(pcs), len(todo)))
for p in todo:
    DisassembleCommand(addr(p), None, True).applyTo(currentProgram)
ok = sum(1 for p in todo if listing.getInstructionAt(addr(p)) is not None)
n = 0
it = listing.getInstructions(True)
while it.hasNext():
    it.next(); n += 1
print('now decoded: %d of %d; total instructions %d' % (ok, len(todo), n))
still = [p for p in todo if listing.getInstructionAt(addr(p)) is None]
if still:
    print('STILL UNDECODED (misaligned or mid-instruction): %s'
          % ' '.join('0x%05X' % p for p in still[:20]))
