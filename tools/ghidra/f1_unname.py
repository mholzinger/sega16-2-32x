# -*- coding: utf-8 -*-
# Revert functions named after a CLASS TAG rather than a real name. An earlier
# pass of f1_decompile.py treated function_map.md's class column as a name, so
# functions became `hardware`, `motion`, `palette` -- colliding, meaningless,
# and they broke compilation as implicit function calls.
#   tools/ghidra_run.sh script f1_unname.py
from ghidra.program.model.symbol import SourceType
TAGS = ('hardware','motion','palette','sound','collision','animation','draws',
        'despawns','signature')
fm = currentProgram.getFunctionManager()
n = 0
for f in fm.getFunctions(True):
    nm = f.getName()
    base = nm.rstrip('0123456789_')
    if base in TAGS or nm in TAGS:
        f.setName('FUN_%08x' % f.getEntryPoint().getOffset(), SourceType.DEFAULT)
        n += 1
print('reverted %d tag-named functions' % n)
