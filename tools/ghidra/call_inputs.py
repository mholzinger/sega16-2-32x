# -*- coding: utf-8 -*-
# Ghidra headless (Jython): at each CALL, what does the CALLER hold in the
# registers the CALLEE reads?
#   tools/ghidra_run.sh script call_inputs.py OUT.json
#
# WHY (FULL-PORT.md 14/18). The port has 902 `in_*` / `unaff_*` mappings -- 68K
# registers a routine inherits -- and every one reads ZERO, because nothing
# supplies them. Declaring the producer's return storage fixed the `extraout_*`
# half (912 -> 292 reads); this half needs the CALLER, at the CALL SITE.
#
# It is not academic. FUN_0000673e is four bytes:
#     return (short)(in_D0w * 0x10) + in_A0;
# With both inputs zero it returns 0, and the animation script read through that
# pointer is meaningless -- which is why slot 16's +0x0c ends the frame at 0x126A
# instead of the arcade's 0x1263.
#
# WHERE THE ANSWER LIVES. Ghidra's prototypes are empty (Decompiler Parameter ID
# cannot infer register arguments for a language whose convention is stack-based --
# see param_id.py, a measured dead end), so the CALL PcodeOp carries no parameter
# inputs. But the HighFunction is in SSA form: every register has varnodes with a
# defining op, and the one live at the call is the caller's value. Its HighVariable
# has the NAME the decompiled C uses, which is exactly what the port needs to
# assign.
import json

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.pcode import PcodeOp

args = getScriptArgs()
OUT = args[0] if args else '/tmp/call_inputs.json'

ifc = DecompInterface()
ifc.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
fm = currentProgram.getFunctionManager()

# which registers each function READS as an inherited input
need = {}
for f in fm.getFunctions(True):
    pass

rows = []
n_fn = n_call = n_named = 0
for f in fm.getFunctions(True):
    try:
        res = ifc.decompileFunction(f, 60, mon)
        if not res.decompileCompleted():
            continue
        hf = res.getHighFunction()
        n_fn += 1
        # every register varnode in this function, with its defining op's address
        live = {}
        vit = hf.getVarnodes(currentProgram.getRegister('D0').getAddressSpace())
        while vit.hasNext():
            vn = vit.next()
            hv = vn.getHigh()
            nm = hv.getName() if hv is not None else None
            if nm in (None, 'UNNAMED'):
                continue
            d = vn.getDef()
            # A VARNODE WITH NO DEFINING OP IS THE FUNCTION'S OWN INPUT -- the
            # register it inherited. Those matter as much as computed ones: the
            # value FUN_0000673e needs is not produced by its caller either, it is
            # passed through, so the plumbing is TRANSITIVE and a pass-through link
            # is a real link. Skipping them made every site look empty.
            seq = d.getSeqnum().getTarget().getOffset() if d is not None else -1
            live.setdefault(vn.getAddress().getOffset(), []).append(
                (seq, nm, vn.getSize()))
        ops = hf.getPcodeOps()
        while ops.hasNext():
            op = ops.next()
            if op.getOpcode() != PcodeOp.CALL:
                continue
            a = op.getInput(0)
            if a is None or not a.isAddress():
                continue
            site = op.getSeqnum().getTarget().getOffset()
            n_call += 1
            # the caller's value for each register: the varnode of that register
            # whose definition most closely PRECEDES this call
            vals = {}
            for regoff, cands in live.items():
                best = None
                inp = None
                for seq, nm, sz in cands:
                    if seq < 0:
                        inp = (seq, nm, sz)          # the function's own input
                        continue
                    # STRICTLY BEFORE. `<=` includes the varnode the call itself
                    # DEFINES -- its own output -- so every site reported the
                    # result variable as if it were the input.
                    if seq < site and (best is None or seq > best[0]):
                        best = (seq, nm, sz)
                if best is None:
                    best = inp                       # passed straight through
                if best:
                    vals['%X' % regoff] = {'name': best[1], 'size': best[2],
                                           'passthrough': best[0] < 0}
            named = {}
            for off, v in vals.items():
                r = currentProgram.getRegister(
                    currentProgram.getAddressFactory()
                        .getRegisterSpace().getAddress(int(off, 16)), v['size'])
                v['reg'] = r.getName() if r is not None else ('off_%s' % off)
                named[v['reg']] = v
            vals = named
            if vals:
                n_named += 1
            rows.append({'caller': '%05X' % f.getEntryPoint().getOffset(),
                         'caller_name': f.getName(),
                         'site': '%05X' % site,
                         'callee': '%05X' % a.getAddress().getOffset(),
                         'regs': vals})
    except Exception:
        pass

fh = open(OUT, 'w')
json.dump(rows, fh)
fh.close()
print('call_inputs: %d functions, %d direct calls, %d with a named caller value '
      '-> %s' % (n_fn, n_call, n_named, OUT))
