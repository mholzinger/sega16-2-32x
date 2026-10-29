# -*- coding: utf-8 -*-
# WHICH DECOMPILED LOCAL IS WHICH 68K ADDRESS REGISTER.
#   tools/ghidra_run.sh script areg_locals.py docs/audit/areg_locals.json
#
# WHY. The port's object dispatcher establishes A6 and only A6 -- "the only one
# derivable without the oracle". Every other address register a handler computes is
# a decompiler LOCAL, and an INDIRECT call then goes through a generated thunk that
# reads _port_reg[n]. Nothing writes those, so the callee gets a stale register.
#
# Measured 2026-10-03 (FULL-PORT.md 62): that is why the player is never respawned.
# FUN_000049BA does `0049CC: movea.w ($3c,A6),A5`, which Ghidra renders as
# `iVar12 = (int)W68(*(short *)(unaff_A6 + 0x3c))` -- correct arithmetic, no register
# identity -- and then calls FUN_000053A8 indirectly. That callee's only parameter is
# A5, the thunk passes _port_reg[13], and the `bset #$5,(A4)` it needs A5 for never
# sets the "player died" flag the respawn is gated on.
#
# THE TEXTUAL SHORTCUT IS UNSOUND and that was checked against the instruction
# encodings before writing this: A6+0x3C is loaded into A0 25 times, A1 3 times and
# A5 8 times across the program. A pass that guessed from the offset would corrupt 28
# sites to fix 8. The decompiler is the only thing that knows, so ask it.
#
# READ-ONLY: nothing here renames, retypes or sets anything. It walks each function's
# high-level symbol map and reports storage. The project is still copied before the
# run, because that is the standing rule and a 4 MB copy is cheaper than being wrong
# about which Ghidra APIs mutate.
import json, os, sys, time
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

args = getScriptArgs()
OUT = args[0] if args else '/tmp/areg_locals.json'
TMO = int(args[1]) if len(args) > 1 else 60

# the port's register file order: D0..D7 = [0..7], A0..A6 = [8..14]
#
# DATA REGISTERS ARE COLLECTED TOO (added 2026-10-03). The first version took only
# A0-A6, on the reading that the dispatcher's gap was address registers. It is not:
# the player's handler reaches its second state dispatch with D7 = 0x40, and D7 is
# what decides whether the state routine NEGATES the new velocity -- so the port
# negated it and the arcade did not (FULL-PORT.md 69). D3 and D6 are deliberately
# THREADED handler to handler by the driver and must not be published from here;
# the caller-side list (CPORT_AREG) decides which indices are used, and the gate
# arbitrates each.
RIX = {'D0': 0, 'D1': 1, 'D2': 2, 'D3': 3, 'D4': 4, 'D5': 5, 'D6': 6, 'D7': 7,
       'A0': 8, 'A1': 9, 'A2': 10, 'A3': 11, 'A4': 12, 'A5': 13, 'A6': 14}

fm = currentProgram.getFunctionManager()
di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()

out, nfn, nsym, t0 = {}, 0, 0, time.time()
for f in fm.getFunctions(True):
    res = di.decompileFunction(f, TMO, mon)
    if not res or not res.decompileCompleted():
        continue
    hf = res.getHighFunction()
    if hf is None:
        continue
    nfn += 1
    rows = []
    sm = hf.getLocalSymbolMap()
    it = sm.getSymbols()
    while it.hasNext():
        sym = it.next()
        st = sym.getStorage()
        if st is None or not st.isRegisterStorage():
            continue
        reg = st.getRegister()
        if reg is None:
            continue
        # a 68K register, by its own name or its parent's (A5w inside A5, D7w in D7)
        nm = reg.getName().upper()
        base = reg.getParentRegister()
        bnm = base.getName().upper() if base is not None else ''
        key = nm if nm in RIX else (bnm if bnm in RIX else None)
        if key is None:
            continue
        rows.append({
            'name': sym.getName(),
            'reg': key,
            'ix': RIX[key],
            'size': st.size(),
            'param': bool(sym.isParameter()),
        })
        nsym += 1
    if rows:
        out['%05X' % f.getEntryPoint().getOffset()] = {
            'fn': f.getName(),
            'locals': sorted(rows, key=lambda r: (r['ix'], r['name'])),
        }

with open(OUT, 'w') as fh:
    json.dump(out, fh, indent=0, sort_keys=True, separators=(',', ': '))
    fh.write('\n')
print('areg_locals: %d function(s) decompiled, %d register symbol(s) in %d '
      'function(s), %.0fs -> %s' % (nfn, nsym, len(out), time.time() - t0, OUT))
