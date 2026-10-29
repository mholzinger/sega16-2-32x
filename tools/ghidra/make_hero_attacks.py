# -*- coding: utf-8 -*-
# CREATE THE HERO'S ATTACK HANDLERS AND THE BOOT CARD'S DOT LOOP AS FUNCTIONS.
#   tools/ghidra_run.sh script make_hero_attacks.py
#
# Sibling of make_kind_inits.py. prog68k 0xA3B0 returns one of four handlers in A0
# (0xA140 0xA416 0xA4DC 0xA664) that the hero's state code reaches by `jmp (a0)`
# (0xA1DE and 23 siblings); Ghidra never followed the register jump, so none is a
# function and the port's dispatcher has nothing to call (rd78 2026-10-04: the hero
# never attacks, the partner object at slot 1 never spawns). 0x15B6-0x166F is the
# boot card's bouncing text dots (twelve 4-byte records at 0xFFE800 stepped into
# text RAM 0x410030 for 165 frames), reached through a 16-bit table at 0x1CFB2.
from ghidra.app.cmd.function import CreateFunctionCmd
fm = currentProgram.getFunctionManager()
def addr(a): return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(a)
targets = [0xA140, 0xA416, 0xA4DC, 0xA664]
made, had = 0, 0
for a in sorted(targets):
    at = addr(a)
    f = fm.getFunctionContaining(at)
    if f is not None and f.getEntryPoint() == at:
        had += 1
        print('  0x%05X already %s' % (a, f.getName()))
        continue
    if CreateFunctionCmd(at).applyTo(currentProgram):
        nf = fm.getFunctionAt(at)
        made += 1
        print('  CREATED 0x%05X as %s' % (a, nf.getName() if nf else '?'))
    else:
        print('  FAILED  0x%05X' % a)
print('make_hero_attacks: %d created, %d already functions' % (made, had))
