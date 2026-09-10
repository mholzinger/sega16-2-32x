# -*- coding: utf-8 -*-
# Ghidra headless (Jython): dump every instruction start address, one hex
# per line, for diffing coverage against another disassembly.
#   tools/ghidra_run.sh script instr_list.py OUT.txt
args = getScriptArgs()
out = args[0] if args else '/tmp/instrs.txt'
listing = currentProgram.getListing()
n = 0
with open(out, 'w') as fh:
    it = listing.getInstructions(True)
    while it.hasNext():
        fh.write('%X\n' % it.next().getAddress().getOffset())
        n += 1
print('instr_list: %d instructions -> %s' % (n, out))
