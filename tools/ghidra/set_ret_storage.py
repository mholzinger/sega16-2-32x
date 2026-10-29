# -*- coding: utf-8 -*-
# Ghidra headless (Jython): declare a function's RETURN to live in given registers.
#   tools/ghidra_run.sh script set_ret_storage.py NAME REG[,REG...] [NAME REG ...]
#
# WHY (FULL-PORT.md 14/15). A 68K routine whose whole purpose is to compute a
# register value decompiles to nothing, because the value is never used INSIDE it
# and no prototype says it is an output. sprite_frame_lookup is 34 bytes of code
# and Ghidra renders it `void sprite_frame_lookup(void) { return; }`. Its caller
# then reads `extraout_A0` / `extraout_A1` -- pseudo-registers that the port turns
# into globals nothing ever writes, so they read zero.
#
# Declaring the return storage makes the decompiler materialise the computation.
# Ghidra already does this where it inferred one: zoom_scale_lookup returns
# `D0:4,D1:4` as a packed CONCAT24, and 8 functions in the corpus return two
# registers that way.
#
# SAFETY. In-place edits to an analysed project are not reliably reversible
# (LESSONS, three occurrences). This script touches ONE function's signature and
# runs no analyzer, but COPY THE PROJECT FIRST:
#   cp -R <GHIDRA_PROJ> <GHIDRA_PROJ>_backup
from ghidra.program.model.data import (Undefined1DataType, Undefined2DataType,
                                       Undefined4DataType, Undefined8DataType)
from ghidra.program.model.listing import VariableStorage, ReturnParameterImpl
from ghidra.program.model.listing.Function import FunctionUpdateType
from ghidra.program.model.symbol import SourceType
from java.util import ArrayList

args = getScriptArgs()
if len(args) % 2:
    raise Exception('usage: set_ret_storage.py NAME REGS [NAME REGS ...]')

fm = currentProgram.getFunctionManager()
prog = currentProgram

def find(nm):
    for f in fm.getFunctions(True):
        if f.getName() == nm:
            return f
    return None

for i in range(0, len(args), 2):
    nm, regspec = args[i], args[i + 1]
    f = find(nm)
    if f is None:
        print('set_ret_storage: %s NOT FOUND' % nm)
        continue
    regs = []
    ok = True
    for rn in regspec.split(','):
        r = prog.getRegister(rn.strip())
        if r is None:
            print('set_ret_storage: %s: no register %r' % (nm, rn))
            ok = False
            break
        regs.append(r)
    if not ok:
        continue
    try:
        # custom storage is required for register returns: without it the return
        # is forced into the language's default location
        f.setCustomVariableStorage(True)
        st = VariableStorage(prog, regs)
        # THE DATA TYPE'S SIZE MUST EQUAL THE STORAGE'S. A packed two-register
        # return is 8 bytes (A0:4,D1:4) and handing it a 4-byte type throws.
        SZ = {1: Undefined1DataType.dataType, 2: Undefined2DataType.dataType,
              4: Undefined4DataType.dataType, 8: Undefined8DataType.dataType}
        dt = SZ.get(st.size())
        if dt is None:
            print('set_ret_storage: %s: no undefined type of size %d, skipped'
                  % (nm, st.size()))
            continue
        ret = ReturnParameterImpl(dt, st, prog)
        # ArrayList, not []: PyGhidra does not convert a Python list to java.util.List
        f.updateFunction(None, ret, ArrayList(), FunctionUpdateType.CUSTOM_STORAGE,
                         True, SourceType.USER_DEFINED)
        print('set_ret_storage: %s -> %s (%d bytes)' % (nm, st, st.size()))
    except Exception as e:
        print('set_ret_storage: %s FAILED: %s' % (nm, e))
