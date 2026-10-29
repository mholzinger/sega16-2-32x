# -*- coding: utf-8 -*-
# Ghidra headless (Jython): which CALL produces each `extraout_` value?
#   tools/ghidra_run.sh script extraout_producers.py OUT.json
#
# WHY (FULL-PORT.md 14/15). `extraout_A0` in a caller means "the routine I just
# called left a value in A0". The port turns it into a global nothing ever writes,
# so all 267 such reads return zero, and supplying just three of them by hand took
# the L2 frame from 207 wrong bytes to 122.
#
# To supply them in general, two things are needed per site: WHICH call produced
# the value, and WHICH register. Both are exact in Ghidra's SSA -- the varnode for
# `extraout_A0` has a defining PcodeOp, and for these it is the CALL itself, whose
# input[0] is the callee address. No heuristic, no text-order guessing.
#
# The output drives set_ret_storage.py: declaring the callee's return storage
# materialises the computation Ghidra otherwise discards. Proven on
# sprite_frame_lookup, which is 34 bytes that decompiled to `{ return; }` and
# became a real table lookup once its return was declared to live in A0.
import json

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.pcode import PcodeOp

args = getScriptArgs()
OUT = args[0] if args else '/tmp/extraout_producers.json'

ifc = DecompInterface()
ifc.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
fm = currentProgram.getFunctionManager()

rows = []
n_fn = n_sym = n_call = n_other = 0
for f in fm.getFunctions(True):
    try:
        res = ifc.decompileFunction(f, 60, mon)
        if not res.decompileCompleted():
            continue
        hf = res.getHighFunction()
        n_fn += 1
        it = hf.getLocalSymbolMap().getSymbols()
        while it.hasNext():
            sym = it.next()
            nm = sym.getName()
            if not nm.startswith('extraout_'):
                continue
            n_sym += 1
            hv = sym.getHighVariable()
            if hv is None:
                continue
            for vn in hv.getInstances():
                d = vn.getDef()
                if d is None:
                    continue
                # GHIDRA DOES NOT DEFINE THESE WITH A CALL OP. Measured: 375 of
                # 375 extraout symbols are defined by something else, and that
                # something is INDIRECT -- the op Ghidra uses for "a call may have
                # changed this". Its input[1] is an `iop` varnode whose ADDRESS is
                # the sequence number of the real CALL, which has to be looked up
                # in the op list.
                op = d
                if d.getOpcode() == PcodeOp.INDIRECT:
                    iop = d.getInput(1)
                    if iop is not None and iop.isConstant():
                        seq = iop.getOffset()
                        found = None
                        ops = hf.getPcodeOps()
                        while ops.hasNext():
                            o = ops.next()
                            if o.getSeqnum().getTime() == seq:
                                found = o
                                break
                        if found is not None:
                            op = found
                if op.getOpcode() in (PcodeOp.CALL, PcodeOp.CALLIND):
                    d = op
                    tgt = None
                    if d.getOpcode() == PcodeOp.CALL:
                        a = d.getInput(0)
                        if a is not None and a.isAddress():
                            tgt = a.getAddress().getOffset()
                    rows.append({
                        'caller': '%05X' % f.getEntryPoint().getOffset(),
                        'caller_name': f.getName(),
                        'site': '%05X' % d.getSeqnum().getTarget().getOffset(),
                        'callee': ('%05X' % tgt) if tgt is not None else None,
                        'indirect': d.getOpcode() == PcodeOp.CALLIND,
                        'pseudo': nm,
                        'storage': str(sym.getStorage()),
                    })
                    n_call += 1
                else:
                    n_other += 1
                break
    except Exception:
        pass

fh = open(OUT, 'w')
json.dump(rows, fh, sort_keys=True)
fh.close()
callees = set(r['callee'] for r in rows if r['callee'])
print('extraout_producers: %d functions, %d extraout symbols, %d defined by a '
      'CALL, %d defined by something else; %d distinct producer functions -> %s'
      % (n_fn, n_sym, n_call, n_other, len(callees), OUT))
