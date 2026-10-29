# -*- coding: utf-8 -*-
# Classify each undecoded gap by what PRECEDES it.
#   tools/ghidra_run.sh script f1_gapedge.py [OUT.json]
# A gap that begins immediately after rts/rte/jmp/bra is a CODE BOUNDARY: the
# previous function ended and something unreached starts. A gap that begins
# mid-flow (after an ordinary instruction) is more likely data the decompiler
# walked up to. 251 gaps of 33-128 bytes hold 46% of the unknown and nothing
# flow-references any of them, so the question is which are functions.
import json
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
addr = af.getAddress
LO, HI = 0x400, 0x1F000
TERM = ('rts', 'rte', 'rtr', 'jmp', 'bra', 'bra.w', 'bra.b', 'bra.s')

cov = bytearray(HI - LO)
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    a = i.getAddress().getOffset()
    if LO <= a < HI:
        for k in range(a, min(a + i.getLength(), HI)):
            cov[k - LO] = 1
gaps, s = [], None
for k in range(len(cov)):
    if not cov[k]:
        if s is None: s = k
    elif s is not None:
        gaps.append((s + LO, k + LO)); s = None
if s is not None: gaps.append((s + LO, HI))

after_term, mid_flow, unknown = [], [], []
for b, e in gaps:
    if e - b < 8:
        continue
    prev = listing.getInstructionBefore(addr(b))
    if prev is None:
        unknown.append((b, e)); continue
    mn = prev.getMnemonicString().lower()
    (after_term if any(mn.startswith(t) for t in TERM) else mid_flow).append((b, e))
def tot(x): return sum(e - b for b, e in x)
print('gaps >= 8 B: %d' % (len(after_term) + len(mid_flow) + len(unknown)))
print('  after a TERMINATOR (rts/jmp/bra) -> CODE boundary : %4d gaps, %6d B'
      % (len(after_term), tot(after_term)))
print('  mid-flow (after an ordinary instruction)          : %4d gaps, %6d B'
      % (len(mid_flow), tot(mid_flow)))
print('  no preceding instruction                          : %4d gaps, %6d B'
      % (len(unknown), tot(unknown)))
args = getScriptArgs()
if args:
    json.dump({'seeds': sorted('%X' % b for b, e in after_term),
               'function_seeds': sorted('%X' % b for b, e in after_term)},
              open(args[0], 'w'))
    print('  -> %s (the code-boundary gap starts, as seeds)' % args[0])
