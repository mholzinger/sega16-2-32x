# -*- coding: utf-8 -*-
# CREATE THE SPAWN-TABLE INIT ROUTINES AS FUNCTIONS.
#   tools/ghidra_run.sh script make_spawn_inits.py
#
# WHY THEY ARE MISSING. The level's spawn tables are DATA: 12-byte entries of
# (trigger X, X, Y, flags, init routine). Ghidra followed the code that reads the
# table but not the addresses inside it, so the init routines were never discovered
# and never became functions -- six of them, 0x0DA0C..0x0DA6A, in no function at all.
#
# MEASURED 2026-10-03 (FULL-PORT.md 67): the arcade spawns an object into slot 48 at
# frame 158 when the camera reaches trigger 0x10E0. The port allocated the slot and
# filled its position from the table, then called the entry's init through
# port_call68_at -- which MISSED, because 0x0DA5C is not in the dispatch table, which
# is built from the functions in the generated unit. 23 bytes of the record stayed
# zero: the HANDLER at +0x02 (0x0000DABC), +0x4A, +0x50..+0x5B. An enemy with no
# behaviour.
#
#   da5c: lea %pc@(0xdd86),%a0        the template for this skin
#   da60: moveb #32,%fp@(74)          +0x4A = 0x20
#   da74: movel %a0@+,%fp@(2)         +0x02 = the handler
#   da78: movew %a0@+,%fp@(6)
#
# THE ADDRESSES ARE READ OUT OF THE TABLES, not listed by hand: the per-round table
# pointers are the longs at 0x1D32A, each table is 12-byte entries ending at a zero
# trigger, and entry+8 is the init routine. Five rounds, 47 entries, 6 distinct
# routines.
#
# THIS EDITS THE PROJECT, which is why tools/ghidra_run.sh's caller copies it first
# (LESSONS: in-place Ghidra edits are not reliably reversible). It only CREATES
# functions at addresses that have none; it renames nothing and retypes nothing.
from ghidra.program.model.address import AddressSet
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

targets, nent = set(), 0
for mode in range(8):
    t = rd(0x1D32A + mode * 4, 4) & 0x3FFFF
    if not (0x1D000 <= t < 0x20000):          # the real tables all sit in this block
        continue
    cur, n = t, 0
    while n < 200:
        trig = rd(cur, 2)
        init = rd(cur + 8, 4) & 0xFFFFFF
        if trig == 0 or trig == 0xFFFF:
            break
        if 0 < init < 0x40000:
            targets.add(init)
        cur += 12
        n += 1
    nent += n
    print('  mode %d: table 0x%05X, %d entries' % (mode, t, n))

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
        print('  FAILED to create a function at 0x%05X -- say so rather than '
              'continuing: a missing init routine is a spawned enemy with no '
              'behaviour, and the gate will not obviously say which' % a)

print('make_spawn_inits: %d table entries, %d distinct init routines, %d created, '
      '%d already functions' % (nent, len(targets), made, had))
