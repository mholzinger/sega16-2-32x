# -*- coding: utf-8 -*-
# Ghidra headless (Jython): disassemble the seeds harvested by
# tools/ghidra/seed_harvest.py and turn each into a function.
#   tools/ghidra_run.sh script seed_apply.py SEEDS.json
# Re-run seed_harvest against a fresh listing afterwards: newly reached
# code carries its own tables and pointers, so this is a fixpoint loop.
import json

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd

args = getScriptArgs()
if not args:
    raise Exception('seed_apply: needs the seeds json path')

with open(args[0]) as fh:
    seeds = [int(s, 16) for s in json.load(fh)['seeds']]

listing = currentProgram.getListing()
af = currentProgram.getAddressFactory()

already = 0
disassembled = 0
made = 0
failed = []

for s in seeds:
    addr = af.getAddress(hex(s).rstrip('L'))
    if listing.getInstructionAt(addr) is None:
        if listing.getInstructionContaining(addr) is not None:
            # lands inside an existing instruction: a bad seed, not a miss
            failed.append(s)
            continue
        d = listing.getDataContaining(addr)
        if d is not None and d.isDefined():
            listing.clearCodeUnits(addr, addr, False)
        cmd = DisassembleCommand(addr, None, True)
        if not cmd.applyTo(currentProgram, monitor):
            failed.append(s)
            continue
        disassembled += 1
    else:
        already += 1
    if getFunctionAt(addr) is None:
        c = CreateFunctionCmd(addr)
        if c.applyTo(currentProgram, monitor):
            made += 1

print('seeds %d: %d already code, %d newly disassembled, %d functions created, %d failed'
      % (len(seeds), already, disassembled, made, len(failed)))
if failed:
    print('failed seeds: %s' % ' '.join('0x%X' % f for f in failed[:40]))
