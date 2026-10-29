# -*- coding: utf-8 -*-
# F1.1 seeding, GHIDRA-NATIVE (docs/design/F1-FRONTEND.md).
#
#   tools/ghidra_run.sh script f1_seeds.py [rounds]
#
# Why this exists rather than extending tools/ghidra/seed_harvest.py: that
# script regex-matches the OBJDUMP listing to find seeds for Ghidra, i.e. it
# uses the unreliable listing to repair the reliable one. Measured: the linear
# listing is 0.6% undecoded below 0x20000 and 36% above it, and 573 of its
# branch edges into the high region originate at bytes Ghidra never reaches.
# So match on Ghidra's own instruction objects instead of on text.
#
# Three dispatch shapes. A and B are seed_harvest's; C is the DOMINANT one it
# does not know (F0(d): 11 sites, 15 bases):
#
#   A  lea TABLE(pc),aN ; movea.l (aN,dM.w),aN ; jmp/jsr (aN)
#      table of LONGS, absolute targets
#   C  lea TABLE(pc),aN ; ... addaw dM,aN ; addaw (aN),aN ; jmp (aN)
#      table of int16 offsets RELATIVE TO THE TABLE BASE
#   B  move.l #ADDR,d(aN)  -- a routine pointer stored into an object field,
#      later reached by movea.l d(a6),a0 ; jsr (a0)
#
# Each round: find shapes, add the targets as flow references so Ghidra's own
# analysis will follow them, disassemble, and report. Repeat to a fixpoint.
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.program.model.symbol import RefType, SourceType

args = getScriptArgs()
ROUNDS = int(args[0]) if args else 4

listing = currentProgram.getListing()
mem = currentProgram.getMemory()
refs = currentProgram.getReferenceManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
CODE_LO, CODE_HI = 0x400, 0x20000        # above 0x20000 is DATA (measured)

def addr(o):
    return af.getAddress(o)

def s16(o):
    v = mem.getShort(addr(o)) & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v

def u32(o):
    return mem.getInt(addr(o)) & 0xFFFFFFFF

def plausible(t):
    return CODE_LO <= t < CODE_HI and not (t & 1)

def prev_instrs(i, n):
    """the n instructions before i, nearest first"""
    out = []
    p = i
    for _ in range(n):
        p = listing.getInstructionBefore(p.getAddress())
        if p is None:
            break
        out.append(p)
    return out

def find_base(i):
    """walk back for `lea (0xNNN,PC),aN`; return (base, sawRelIndirect)"""
    saw_rel = False
    for p in prev_instrs(i, 24):
        t = p.toString()
        if t.startswith('addaw') or t.startswith('adda.w'):
            if '(' in t and ')' in t and '),' in t:
                saw_rel = True
        if t.startswith('lea') and 'PC' in t:
            for k in range(p.getNumOperands()):
                for r in p.getOperandReferences(k):
                    a = r.getToAddress().getOffset()
                    if CODE_LO <= a < CODE_HI:
                        return a, saw_rel
            import re as _re
            m = _re.search(r'0x([0-9a-fA-F]+)', t)
            if m:
                return int(m.group(1), 16), saw_rel
        if t.startswith('rts') or t.startswith('rte'):
            break
    return None, saw_rel

def table_targets(base, relative):
    """read entries until one stops being plausible code"""
    out = []
    for k in range(256):
        o = base + (k * 2 if relative else k * 4)
        if not (0 <= o < 0x40000 - 4):
            break
        try:
            t = base + s16(o) if relative else u32(o)
        except Exception:
            break
        if not plausible(t):
            break
        out.append(t)
    return out

total_new = 0
for rnd in range(1, ROUNDS + 1):
    found = {}
    it = listing.getInstructions(True)
    while it.hasNext():
        i = it.next()
        fl = i.getFlowType()
        if not (fl.isComputed() and (fl.isJump() or fl.isCall())):
            continue
        base, rel = find_base(i)
        if base is None:
            continue
        for t in table_targets(base, rel):
            found.setdefault(t, (i.getAddress().getOffset(), base, rel))

    new = [t for t in found if listing.getInstructionAt(addr(t)) is None]
    # add a flow reference from each dispatch site so Ghidra follows it itself
    for t, (site, base, rel) in found.items():
        try:
            refs.addMemoryReference(addr(site), addr(t),
                                    RefType.COMPUTED_JUMP, SourceType.ANALYSIS, 0)
        except Exception:
            pass
    for t in new:
        DisassembleCommand(addr(t), None, True).applyTo(currentProgram)
    ok = sum(1 for t in new if listing.getInstructionAt(addr(t)) is not None)
    total_new += ok
    n_i = 0
    itc = listing.getInstructions(True)
    while itc.hasNext():
        itc.next(); n_i += 1
    print('round %d: %d table targets, %d were undisassembled, %d now code; '
          'instructions %d' % (rnd, len(found), len(new), ok, n_i))
    if not new:
        print('  fixpoint reached')
        break

print('\nf1_seeds: %d newly disassembled across %d rounds' % (total_new, rnd))
