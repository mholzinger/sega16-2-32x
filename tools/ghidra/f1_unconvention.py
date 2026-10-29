# -*- coding: utf-8 -*-
# Revert f1_convention.py. MEASURED 2026-09-26: declaring A6/A0/D0... as
# custom-storage parameters AND forcing a D0 return on 451 functions dropped the
# SH-2 compile rate from 415/623 to 253/623 and raised whole-corpus errors from
# 589 to 812. Pseudo-registers fell 45 -> 29, so it half-worked, but forcing a
# return type on functions that return nothing, and adding parameters the call
# sites do not pass, costs more than it buys.
#   tools/ghidra_run.sh script f1_unconvention.py
from ghidra.program.model.listing import Function
from ghidra.program.model.symbol import SourceType
fm = currentProgram.getFunctionManager()
n = 0
for f in fm.getFunctions(True):
    if f.hasCustomVariableStorage():
        try:
            f.updateFunction(None, None, [],
                             Function.FunctionUpdateType.DYNAMIC_STORAGE_FORMAL_PARAMS,
                             True, SourceType.DEFAULT)
            f.setCustomVariableStorage(False)
            n += 1
        except Exception:
            pass
print('reverted custom storage on %d functions' % n)
