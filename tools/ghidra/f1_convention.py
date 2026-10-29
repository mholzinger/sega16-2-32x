# -*- coding: utf-8 -*-
# Declare the 68K calling convention for functions that need it, then the
# decompiler stops inventing `unaff_A6` / `extraout_A0` pseudo-variables.
#   tools/ghidra_run.sh script f1_convention.py NEED.json
#
# THE CONVENTION, which is already documented rather than guessed:
# docs/design/collision_sh2.s pins r14 = state base and r4 = a4 = the current
# object slot, and the game's idiom is the CURRENT OBJECT in a6 with the slot
# base in a4. A6 is UNAFFECTED across calls -- set by the caller, read by the
# callee, never modified -- which in a single-threaded game loop means it is
# semantically a parameter (and could equally be modelled as a global).
#
# NEED.json maps a function entry (hex, no 0x) to the registers its decompiled
# body referenced as a pseudo-variable, so only affected functions are touched.
import json
from ghidra.program.model.data import PointerDataType, UnsignedIntegerDataType
from ghidra.program.model.listing import ParameterImpl, ReturnParameterImpl
from ghidra.program.model.listing import Function
from ghidra.program.model.symbol import SourceType

args = getScriptArgs()
need = json.load(open(args[0]))
prog = currentProgram
fm = prog.getFunctionManager()
af = prog.getAddressFactory().getDefaultAddressSpace()
pr = prog.getProgramContext()

ptr = PointerDataType(); u32 = UnsignedIntegerDataType()
ok = skip = fail = 0
for hexa, regs in need.items():
    a = af.getAddress(int(hexa, 16))
    f = fm.getFunctionAt(a)
    if f is None:
        skip += 1; continue
    params = []
    for rn in regs:
        reg = pr.getRegister(rn.upper())
        if reg is None:
            continue
        # address registers carry pointers, data registers carry values
        dt = ptr if rn.upper().startswith('A') else u32
        try:
            params.append(ParameterImpl(rn.lower(), dt, reg, prog))
        except Exception:
            pass
    if not params:
        skip += 1; continue
    try:
        d0 = pr.getRegister('D0')
        ret = ReturnParameterImpl(u32, d0, prog) if d0 else None
        f.updateFunction(None, ret, params,
                         Function.FunctionUpdateType.CUSTOM_STORAGE,
                         True, SourceType.USER_DEFINED)
        ok += 1
    except Exception as e:
        fail += 1
print('convention applied: %d ok, %d skipped, %d failed (of %d requested)'
      % (ok, skip, fail, len(need)))
