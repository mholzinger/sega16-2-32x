# -*- coding: utf-8 -*-
# Ghidra headless (Jython): give the object-table handlers their own function entries.
#   tools/ghidra_run.sh script handler_entries.py docs/proof/object_handlers_toghidra.txt
#
# WHY (FULL-PORT.md 12): object_dispatcher (0x398E) calls each of 64 slots' handler
# through a 32-bit pointer at slot+2. Ghidra splits functions on a DIRECT call, so a
# handler reached only through that table gets no entry of its own: either it is
# swallowed by the neighbour that falls through into it, or it is never disassembled
# at all. The C port needs one function per entry point, so every such address has to
# become a function before the corpus is regenerated.
#
# Measured 2026-09-28 over the union of three harvests (147 addresses): 128 were
# already function entries, 6 were junk (a data table, the exception vector table),
# and 13 needed this script.
#
# THIS SCRIPT IS THE AUTHORITY ON ALIGNMENT. tools/handler_plausible.py filters by
# A6-relative addressing, which cannot tell an instruction boundary from a byte two
# into the same instruction (0x08104 and 0x08106 are both inside the instruction at
# 0x08102). Ghidra has the length decoder, so anything not on a boundary is REFUSED
# here rather than silently made into a bogus function.
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd

args = getScriptArgs()
path = args[0] if args else 'docs/proof/object_handlers_toghidra.txt'

want = []
for line in open(path):
    line = line.split('#')[0].strip()
    if line:
        want.append(int(line, 16))

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()

created, split, already, refused, failed = [], [], [], [], []

for a in want:
    addr = af.getAddress(a)
    ins = listing.getInstructionContaining(addr)

    if ins is not None and ins.getAddress() != addr:
        # inside another instruction: not an entry point, whatever a byte filter said
        refused.append((a, 'inside the instruction at 0x%s' % ins.getAddress()))
        continue

    if ins is None:
        # never disassembled -- lay code down first, then make it a function
        cmd = DisassembleCommand(addr, None, True)
        if not cmd.applyTo(currentProgram, monitor):
            refused.append((a, 'will not disassemble'))
            continue

    f = fm.getFunctionAt(addr)
    if f is not None:
        already.append(a)
        continue

    owner = fm.getFunctionContaining(addr)
    # CreateFunctionCmd on an address inside an existing function splits it: the
    # owner's body is recomputed from flow and stops where the new entry begins
    if CreateFunctionCmd(addr).applyTo(currentProgram, monitor):
        (split if owner is not None else created).append(a)
    else:
        failed.append(a)

def show(label, xs):
    print('handler_entries: %-8s %3d  %s' % (label, len(xs),
          ' '.join('0x%05X' % x for x in xs)))

show('created', created)
show('split', split)
show('already', already)
show('failed', failed)
for a, why in refused:
    print('handler_entries: REFUSED 0x%05X -- %s' % (a, why))
print('handler_entries: %d of %d addresses now have their own function entry'
      % (len(created) + len(split) + len(already), len(want)))
