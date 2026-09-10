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
    doc = json.load(fh)
seeds = [int(s, 16) for s in doc['seeds']]
# Only GENUINE ENTRY POINTS become functions. Seeding a function at every
# address that merely needs disassembling fragments the program: it took
# 433 real functions to 2121 and made the call graph useless
# (docs/log/LOOP-DECOMPILE.md 19). Everything else is disassembled and
# left to fall inside whatever function reaches it.
entries = set(int(s, 16) for s in doc.get('function_seeds', doc['seeds']))

listing = currentProgram.getListing()
af = currentProgram.getAddressFactory()

# REPAIR PASS. A speculative seed can land one word into a real
# instruction; Ghidra then disassembles from there and the whole run is
# misaligned (docs/log/LOOP-DECOMPILE.md 19: 0x1580 was the second half of
# the lea at 0x157E). The main loop's guard only rejects a seed that lands
# inside an instruction that ALREADY exists, so a bad seed applied first
# wins. Any repair_seeds address found mid-instruction is therefore
# authoritative: clear what covers it and disassemble from it instead.
repaired = 0
for s in [int(x, 16) for x in doc.get('repair_seeds', [])]:
    addr = af.getAddress(hex(s).rstrip('L'))
    if listing.getInstructionAt(addr) is not None:
        continue
    ci = listing.getInstructionContaining(addr)
    if ci is not None:
        listing.clearCodeUnits(ci.getAddress(), addr, False)
    else:
        # the address is data, but a bad instruction may start just after
        # it and block disassembly; clear the window either way
        nxt = listing.getInstructionAfter(addr)
        if nxt is None or nxt.getAddress().getOffset() > s + 8:
            continue
        listing.clearCodeUnits(addr, nxt.getAddress(), False)
    if DisassembleCommand(addr, None, True).applyTo(currentProgram, monitor):
        repaired += 1
if doc.get('repair_seeds'):
    print('repair pass: %d misaligned runs re-disassembled' % repaired)


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
    if s in entries and getFunctionAt(addr) is None:
        c = CreateFunctionCmd(addr)
        if c.applyTo(currentProgram, monitor):
            made += 1

print('seeds %d (%d entry points): %d already code, %d newly disassembled, '
      '%d functions created, %d failed'
      % (len(seeds), len(entries), already, disassembled, made, len(failed)))
if failed:
    print('failed seeds: %s' % ' '.join('0x%X' % f for f in failed[:40]))
