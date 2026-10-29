# -*- coding: utf-8 -*-
# Ghidra headless (Jython): dump every function entry point, one hex
# address per line, for cross-checking against another disassembly.
#   tools/ghidra_run.sh script func_list.py OUT.txt
args = getScriptArgs()
out = args[0] if args else '/tmp/funcs.txt'
fm = currentProgram.getFunctionManager()
eps = sorted(f.getEntryPoint().getOffset() for f in fm.getFunctions(True))
with open(out, 'w') as fh:
    for e in eps:
        fh.write('%X\n' % e)
print('func_list: %d entry points -> %s' % (len(eps), out))
