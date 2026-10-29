# -*- coding: utf-8 -*-
# F1.1: expand F0(d)'s 15 word-offset jump tables and disassemble their targets.
#   tools/ghidra_run.sh script f1_tables.py
#
# STRUCTURE, derived 2026-09-26 by dumping the raw words (docs/design/
# F1-FRONTEND.md). Each table sits at the END of its code block and its entries
# are int16 offsets RELATIVE TO THE TABLE BASE, so they are NEGATIVE and point
# back into the block that precedes it:
#
#   table 0x11E34, preceded by the 2,248-byte gap 0x113B0-0x11C78:
#     [0] FE44 -444 -> 0x11C78    [4] FD76 -650 -> 0x11BAA
#     [1] FDB8 -584 -> 0x11BEC    [5] FD74 -652 -> 0x11BA8
#     [2] FD5A -678 -> 0x11B8E    [6] FBF4 -1036 -> 0x11A28
#     [3] FE3E -450 -> 0x11C72    [7] 8000  = SENTINEL, stop
#
# 0x8000 terminates. An earlier walk found ONE entry per table because it gated
# each target on being an already-decoded instruction start -- using the linear
# listing, which does not decode these blocks. That is the circularity this
# whole file exists to avoid: the targets are undecoded BECAUSE they are only
# reachable here.
from ghidra.app.cmd.disassemble import DisassembleCommand

BASES = [0x346a, 0x88ec, 0xd0ec, 0xd5a2, 0x100f6, 0x10104, 0x11e34, 0x128da,
         0x12ff2, 0x12ffc, 0x13926, 0x141b8, 0x148a2, 0x15e60, 0x16938]
CODE_LO, CODE_HI = 0x400, 0x1C000

listing = currentProgram.getListing()
mem = currentProgram.getMemory()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress

def s16(o):
    v = mem.getShort(addr(o)) & 0xFFFF
    return (v - 0x10000 if v & 0x8000 else v), v

targets = {}
for b in BASES:
    ents = []
    for k in range(256):
        try:
            off, raw = s16(b + k * 2)
        except Exception:
            break
        if raw == 0x8000:
            break                       # sentinel
        t = b + off
        if not (CODE_LO <= t < CODE_HI) or (t & 1):
            break
        ents.append(t)
    for t in ents:
        targets.setdefault(t, b)
    print('  table 0x%05X  %3d entries' % (b, len(ents)))

print('\ndistinct targets: %d' % len(targets))
undec = [t for t in targets if listing.getInstructionAt(addr(t)) is None]
print('not yet decoded : %d' % len(undec))
for t in undec:
    DisassembleCommand(addr(t), None, True).applyTo(currentProgram)
ok = sum(1 for t in undec if listing.getInstructionAt(addr(t)) is not None)
n = 0
it = listing.getInstructions(True)
while it.hasNext():
    it.next(); n += 1
print('newly decoded   : %d' % ok)
print('instructions now: %d' % n)
