# -*- coding: utf-8 -*-
# Ghidra headless (Jython): declare a function's PARAMETERS to live in registers.
#   tools/ghidra_run.sh script set_param_storage.py NAME REG[,REG...] [NAME REGS ...]
#
# WHY (FULL-PORT.md 14/18). A routine that reads an inherited register shows it as
# `in_A0` / `unaff_D3w`, which the port turns into a global nothing writes -- 902
# such mappings, every one reading zero. FUN_0000673e is four bytes:
#     return (short)(in_D0w * 0x10) + in_A0;
# and with both inputs zero it returns 0.
#
# The caller's value cannot be recovered from the decompiled C, because THE
# DECOMPILER DELETES THE ARGUMENT SETUP: the prototype says the function takes
# nothing, so the instructions that load D0 and A0 before the call are dead and
# are folded away. Measured 2026-09-30 -- the high-level SSA at those two call
# sites shows no D0 and no A0 at all.
#
# Declaring the parameters is what makes the setup live again, and then the
# decompiler emits the arguments at every call site. It is the same move that
# worked for returns (set_ret_storage.py, extraout_ reads 912 -> 292).
#
# SAFETY: copy the project first. In-place edits to an analysed project are not
# reliably reversible (LESSONS, three occurrences). This sets one signature and
# runs no analyzer.
from ghidra.program.model.data import (Undefined1DataType, Undefined2DataType,
                                       Undefined4DataType)
from ghidra.program.model.listing import ParameterImpl, VariableStorage
from ghidra.program.model.listing.Function import FunctionUpdateType
from ghidra.program.model.symbol import SourceType
from java.util import ArrayList

args = getScriptArgs()
# a single argument is a FILE of "NAME REGS" lines -- 462 functions is 924 argv
# items and that is not a thing to put on a command line
if len(args) == 1 and not args[0].startswith('_') and '/' in args[0]:
    toks = []
    for line in open(args[0]):
        line = line.split('#')[0].strip()
        if line:
            toks.extend(line.split())
    args = toks
if len(args) % 2:
    raise Exception('usage: set_param_storage.py NAME REGS [...] | FILE')

fm = currentProgram.getFunctionManager()
prog = currentProgram
SZ = {1: Undefined1DataType.dataType, 2: Undefined2DataType.dataType,
      4: Undefined4DataType.dataType}

def find(nm):
    for f in fm.getFunctions(True):
        if f.getName() == nm:
            return f
    return None

for i in range(0, len(args), 2):
    nm, regspec = args[i], args[i + 1]
    f = find(nm)
    if f is None:
        print('set_param_storage: %s NOT FOUND' % nm)
        continue
    params = []
    ok = True
    for rn in regspec.split(','):
        rn = rn.strip()
        r = prog.getRegister(rn)
        if r is None:
            print('set_param_storage: %s: no register %r' % (nm, rn))
            ok = False
            break
        st = VariableStorage(prog, r)
        dt = SZ.get(st.size())
        if dt is None:
            print('set_param_storage: %s: no undefined type of size %d'
                  % (nm, st.size()))
            ok = False
            break
        params.append(ParameterImpl('p_%s' % rn, dt, st, prog))
    if not ok:
        continue
    try:
        f.setCustomVariableStorage(True)
        # keep whatever return storage set_ret_storage.py may have given it
        # a java.util.List, not a Python list: PyGhidra (Ghidra 12.x) does not
        # convert one implicitly the way Jython did
        jparams = ArrayList()
        for p in params:
            jparams.add(p)
        f.updateFunction(None, f.getReturn(), jparams,
                         FunctionUpdateType.CUSTOM_STORAGE, True,
                         SourceType.USER_DEFINED)
        print('set_param_storage: %s(%s)' % (nm, regspec))
    except Exception as e:
        print('set_param_storage: %s FAILED: %s' % (nm, e))
