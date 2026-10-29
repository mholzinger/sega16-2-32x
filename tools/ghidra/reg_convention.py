# -*- coding: utf-8 -*-
# Ghidra headless (Jython): what registers does each function READ and WRITE?
#   tools/ghidra_run.sh script reg_convention.py OUT.json
#
# WHY (FULL-PORT.md 14): the port has 977 reads of a 68K pseudo-register
# (unaff_D3w, extraout_A0, in_D1) and ZERO writes, because Ghidra models the read
# side of register passing and not the write side. 82% of the ported functions
# read at least one, and every read returns zero.
#
# The information exists inside Ghidra: the decompiler computes each function's
# HighFunction, whose prototype carries the storage of every parameter and the
# return. A parameter stored in D3 IS the "unaff_D3w" the consumer reads. Dumping
# it turns the port's globals into real arguments.
#
# Output per function: entry, name, the registers its prototype takes as inputs
# (with storage), its return storage, and the pseudo-register names its decompiled
# body reads -- so the two can be reconciled offline rather than trusted.
import json

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

args = getScriptArgs()
OUT = args[0] if args else '/tmp/reg_convention.json'

ifc = DecompInterface()
ifc.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
fm = currentProgram.getFunctionManager()

out = []
n_ok = n_fail = 0
for f in fm.getFunctions(True):
    ent = f.getEntryPoint().getOffset()
    rec = {'entry': '%05X' % ent, 'name': f.getName(),
           'params': [], 'ret': None, 'pseudo': []}
    try:
        res = ifc.decompileFunction(f, 60, mon)
        if not res.decompileCompleted():
            n_fail += 1
            out.append(rec)
            continue
        hf = res.getHighFunction()
        proto = hf.getFunctionPrototype()
        for i in range(proto.getNumParams()):
            p = proto.getParam(i)
            vn = p.getStorage()
            rec['params'].append({'name': p.getName(),
                                  'size': p.getSize(),
                                  'storage': str(vn)})
        r = proto.getReturnStorage()
        rec['ret'] = str(r) if r is not None else None
        # THE PROTOTYPES ARE EMPTY (measured: 0 of 788 have params -- Decompiler
        # Parameter ID was never run on this program). The register identity is
        # still there, in the local symbol map: each unaff_/extraout_/in_ symbol
        # carries the STORAGE it came from, which is the 68K register the consumer
        # is really reading. That is the mapping the port needs.
        lsm = hf.getLocalSymbolMap()
        it = lsm.getSymbols()
        while it.hasNext():
            sym = it.next()
            nm = sym.getName()
            if not (nm.startswith('unaff_') or nm.startswith('extraout_')
                    or nm.startswith('in_')):
                continue
            try:
                st = sym.getStorage()
                rec['storage'] = rec.get('storage', {})
                rec['storage'][nm] = str(st)
            except Exception:
                pass
        # the pseudo-registers the BODY reads, straight out of the C text
        txt = res.getDecompiledFunction().getC()
        for tag in ('unaff_', 'extraout_', 'in_'):
            i = 0
            while True:
                i = txt.find(tag, i)
                if i < 0:
                    break
                j = i + len(tag)
                while j < len(txt) and (txt[j].isalnum() or txt[j] == '_'):
                    j += 1
                nm = txt[i:j]
                if nm not in rec['pseudo']:
                    rec['pseudo'].append(nm)
                i = j
        n_ok += 1
    except Exception as e:
        n_fail += 1
        rec['error'] = str(e)
    out.append(rec)

fh = open(OUT, 'w')
json.dump(out, fh, sort_keys=True)
fh.close()
npar = sum(1 for r in out if r.get('storage'))
nps  = sum(1 for r in out if r['pseudo'])
print('reg_convention: %d ok, %d failed; %d have prototype params, %d read a '
      'pseudo-register -> %s' % (n_ok, n_fail, npar, nps, OUT))
