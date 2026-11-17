# -*- coding: utf-8 -*-
# Ghidra headless (Jython 11.x / PyGhidra 12.x): who touches these addresses, and what do they do?
#   tools/ghidra_run.sh script refs_decomp.py OUT.txt 0xFFFE20,0xFFFE1A [callers_depth]
#   ghidra-run PROJ NAME -process z80drv.bin -noanalysis -scriptPath tools/ghidra -postScript refs_decomp.py OUT.txt 0x1D03 1
# For every address: each reference to it (from, type, enclosing function), then the decompiled
# C of every distinct enclosing function and, to callers_depth (default 1), of their callers,
# plus a disassembly of any referencing code that no function contains. Processor-agnostic:
# the Mega Drive Altered Beast's 68K (the sound mailbox 0xFFFE20 / bank 0xFFFE1A, 2026-11-01)
# and its Z80 driver (the mailbox mirror 0x1D03) go through the same script. KIT.
import sys
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

args = list(getScriptArgs())
out_path = args[0]
# an address written @0x1234 is CODE: the function containing it is decompiled (with callers) instead of its references
addrs = [int(a, 16) for a in args[1].split(',') if not a.startswith('@')]
code_addrs = [int(a[1:], 16) for a in args[1].split(',') if a.startswith('@')]
depth = int(args[2]) if len(args) > 2 else 1
prog = currentProgram
fm = prog.getFunctionManager()
rm = prog.getReferenceManager()
listing = prog.getListing()
space = prog.getAddressFactory().getDefaultAddressSpace()
mon = ConsoleTaskMonitor()
dec = DecompInterface()
dec.openProgram(prog)

lines = []
funcs = []            # ordered, distinct
def add_func(f):
    if f is not None and f not in funcs:
        funcs.append(f)

for a in addrs:
    addr = space.getAddress(a)
    lines.append('== references to 0x%X' % a)
    refs = list(rm.getReferencesTo(addr))
    # a 68K `.w` absolute address lives in the sign-extended space too
    if a >= 0xFFFF8000 and a <= 0xFFFFFFFF:
        pass
    for r in refs:
        src = r.getFromAddress()
        f = fm.getFunctionContaining(src)
        ins = listing.getInstructionAt(src)
        lines.append('  %s %-8s %-26s in %s' % (src, r.getReferenceType(), ins if ins else '(data)', f.getName() if f else '(no function)'))
        if f is not None:
            add_func(f)
        elif ins is not None:
            lines.append('    (no function: 16 instructions from the reference)')
            cur = ins
            for i in range(16):
                if cur is None:
                    break
                lines.append('      %s  %s' % (cur.getAddress(), cur))
                cur = cur.getNext()
    if not refs:
        lines.append('  (none)')

for a in code_addrs:
    f = fm.getFunctionContaining(space.getAddress(a))
    lines.append('== code 0x%X is in %s' % (a, f.getName() if f else '(no function)'))
    add_func(f)

# callers
frontier = list(funcs)
for d in range(depth):
    nxt = []
    for f in frontier:
        for c in f.getCallingFunctions(mon):
            if c not in funcs:
                funcs.append(c)
                nxt.append(c)
    frontier = nxt

for f in funcs:
    lines.append('')
    lines.append('==== %s @ %s (callers: %s)' % (f.getName(), f.getEntryPoint(), ', '.join(sorted(c.getName() for c in f.getCallingFunctions(mon)))))
    res = dec.decompileFunction(f, 60, mon)
    if res is not None and res.decompileCompleted():
        lines.append(res.getDecompiledFunction().getC())
    else:
        lines.append('  (decompile failed: %s)' % (res.getErrorMessage() if res else '?'))

with open(out_path, 'w') as fh:
    fh.write('\n'.join(lines) + '\n')
print('refs_decomp: %d addresses, %d functions -> %s' % (len(addrs), len(funcs), out_path))
