# -*- coding: utf-8 -*-
# CREATE THE SCRIPTED-SPAWN "KIND" INSTALLERS (and other known gaps) AS FUNCTIONS.
#   tools/ghidra_run.sh script make_kind_inits.py
#
# Sibling of make_spawn_inits.py. FUN_0000e052 (prog68k 0xE0BA) dispatches each
# scripted spawn record's kind byte (record+7) through the 21-entry long table at
# 0x1D2EA: `lea 0x1d2ea,a1; lsl.w #2,d0; movea.l (a1,d0.w),a1; jsr (a1)`. Ghidra never
# followed the table, so ten of its targets are in no function (2026-10-04:
# 0xEE0A 0xEDB0 0xF74A 0x102CC 0x10CD6 0x1D348 0x1D4D6 0x1D520 0x1D56A 0x1D56C). Kind 0
# (0xEE0A) installs the attract's demo director 0xEE80 -- the object that spawns the
# wolves (FUN_0000f28a) whose spirit balls morph the hero; without it the demo never
# reaches the morph, the fade, or the text clear. Also 0x24A4, the credit object's
# handler, 28 bytes between FUN_00002490 and FUN_000024c0 (LESSONS "A handler Ghidra
# never made a function of").
#
# ENTRIES 16-20 OF THE KIND TABLE ARE NOT CODE: 0x1D348 0x1D4D6 0x1D520 0x1D56A 0x1D56C are
# the per-round 0x1D32A spawn TABLES (the 68K reuses the slot for a data pointer; round
# 0's 12-byte records start at 0x1D348). Creating functions there yields 1-byte
# FUN_0001d348.. stubs -- inert (nothing dispatches kinds 16-20), but a re-run should
# skip addresses >= 0x1D000.
# THIS EDITS THE PROJECT (back it up first). It only CREATES functions at addresses
# that have none; it renames nothing and retypes nothing.
from ghidra.app.cmd.function import CreateFunctionCmd
mem = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = lambda v: af.getAddress(v)
def rd(off, n):
    v = 0
    for k in range(n):
        v = (v << 8) | (mem.getByte(addr(off + k)) & 0xFF)
    return v
targets = set([0x24A4])
i = 0
while i < 64:
    v = rd(0x1D2EA + i * 4, 4)
    if v == 0 or v >= 0x40000:
        break
    if v < 0x1D000:          # see the note above: the 0x1Dxxx entries are tables
        targets.add(v)
    i += 1
print('  kind table 0x1D2EA: %d entries' % i)
made, had = 0, 0
for a in sorted(targets):
    at = addr(a)
    f = fm.getFunctionContaining(at)
    if f is not None:
        had += 1
        print('  0x%05X already in %s' % (a, f.getName()))
        continue
    if CreateFunctionCmd(at).applyTo(currentProgram):
        nf = fm.getFunctionAt(at)
        made += 1
        print('  CREATED 0x%05X as %s' % (a, nf.getName() if nf else '?'))
    else:
        print('  FAILED  0x%05X' % a)
print('make_kind_inits: %d created, %d already functions' % (made, had))
