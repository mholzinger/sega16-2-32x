# -*- coding: utf-8 -*-
# F1.1 (docs/design/F1-FRONTEND.md): does Ghidra's recursive-descent analysis
# give the translator the front end a LINEAR disassembly cannot?
#
#   tools/ghidra_run.sh script f1_export.py OUT.json
#
# Emits, and prints the decisive comparison:
#   * code/data/undefined coverage BY REGION -- the linear listing is 0.6%
#     undecoded below 0x20000 and 36% above it, so the region split is the test.
#   * every instruction with its P-CODE. P-code makes the 68K's flag writes
#     explicit, so dead-flag elimination over it IS the fusion pass that
#     collision_sh2.s does by hand -- generalised instead of pattern-matched.
#   * the switch tables the analyser recovered, to check against F0(d)'s 15.
import json
args = getScriptArgs()
out = args[0] if args else '/tmp/f1_export.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()

BOUND = 0x20000
def bucket(off):
    return 'lo' if off < BOUND else 'hi'

cov = {'lo': {'code': 0, 'data': 0}, 'hi': {'code': 0, 'data': 0}}
total = {'lo': 0, 'hi': 0}
for b in mem.getBlocks():
    s = b.getStart().getOffset()
    e = s + b.getSize()
    for o in (s, e):
        pass
    # split the block at BOUND
    total['lo'] += max(0, min(e, BOUND) - min(s, BOUND))
    total['hi'] += max(0, e - max(s, BOUND))

instrs = []
pcode_ops = 0
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    off = i.getAddress().getOffset()
    cov[bucket(off)]['code'] += i.getLength()
    ops = []
    try:
        for p in i.getPcode():
            ops.append(p.toString())
        pcode_ops += len(ops)
    except Exception:
        pass
    instrs.append({'a': off, 'n': i.getLength(),
                   'm': i.getMnemonicString(),
                   't': i.toString(),
                   'p': ops})

it = listing.getDefinedData(True)
while it.hasNext():
    d = it.next()
    cov[bucket(d.getAddress().getOffset())]['data'] += d.getLength()

funcs = []
for f in fm.getFunctions(True):
    funcs.append({'a': f.getEntryPoint().getOffset(), 'name': f.getName(),
                  'bytes': f.getBody().getNumAddresses()})

# switch tables: Ghidra records computed-jump destinations as references
tables = {}
for ins in instrs:
    pass
refmgr = currentProgram.getReferenceManager()
comp = []
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    fl = i.getFlowType()
    if fl.isComputed() and (fl.isJump() or fl.isCall()):
        dests = []
        for r in refmgr.getReferencesFrom(i.getAddress()):
            if r.getReferenceType().isFlow():
                dests.append(r.getToAddress().getOffset())
        comp.append({'a': i.getAddress().getOffset(),
                     't': i.toString(), 'dests': sorted(dests)})

print('== F1 FRONT-END COVERAGE, by region (bound 0x%X) ==' % BOUND)
for k in ('lo', 'hi'):
    t = total[k] or 1
    c, d = cov[k]['code'], cov[k]['data']
    print('  %s  total %7d   code %7d (%5.1f%%)  data %7d (%4.1f%%)  undefined %7d (%4.1f%%)'
          % (k, total[k], c, 100.0 * c / t, d, 100.0 * d / t,
             t - c - d, 100.0 * (t - c - d) / t))
print('\n  instructions %d, functions %d, P-code ops %d (%.1f per instruction)'
      % (len(instrs), len(funcs), pcode_ops, float(pcode_ops) / max(1, len(instrs))))
print('  COMPUTED jumps/calls %d; with at least one recovered destination: %d'
      % (len(comp), sum(1 for c in comp if c['dests'])))
tot_d = sum(len(c['dests']) for c in comp)
print('  total recovered destinations: %d' % tot_d)

with open(out, 'w') as fh:
    json.dump({'coverage': cov, 'total': total, 'funcs': funcs,
               'computed': comp, 'instrs': instrs}, fh, sort_keys=True)
print('\nwrote %s' % out)
