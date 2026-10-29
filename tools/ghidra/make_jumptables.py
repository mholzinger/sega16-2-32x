# -*- coding: utf-8 -*-
# GIVE THE DECOMPILER THE 68K's PC-RELATIVE JUMP TABLES.
#   tools/ghidra_run.sh script make_jumptables.py
#
# The object handlers dispatch their state machines through
#     lea table(pc),a0 ; move.b idx,d0 ; add.w d0,d0 ; adda.w d0,a0 ; adda.w (a0),a0 ; jmp (a0)
# (a word-offset table, 0xFDDA and 40 siblings) or
#     lea table(pc),a0 ; ... ; movea.l (a0,d0.w),a0 ; jmp (a0)
# (a long-address table, 0x1769C). Ghidra's switch recovery gave up on every one
# ("Could not recover jumptable ... Too many branches", 702 warnings in the unit,
# 2026-10-04) and the decompiled C calls `(*(code *)(*psVar + psVar))()` -- a 68K
# address computed from SH-2-raw reads. rd107 hung at tick 0x51 in FUN_0000fd6c the
# first time a zombie child reached its table.
# The table is derived from the bytes: entries are read from the lea's address while
# the target they name stays inside the jumping function's body; the first entry
# that leaves it is the next table (0x100F6's seven stop where 0x10104's begin).
# A JumpTable override per site makes the decompiler emit the switch as labels
# inside the function, which is what the 68K does.
from ghidra.program.model.pcode import JumpTable
from java.util import ArrayList
from ghidra.program.model.address import Address
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
listing = currentProgram.getListing()
mem = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
def addr(a): return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(a)
done = skipped = 0
ins = listing.getInstructions(True)
for jmp in ins:
    if jmp.getMnemonicString() != 'jmp':
        continue
    b = jmp.getBytes()
    if len(b) != 2 or (b[0] & 0xff) != 0x4e or (b[1] & 0xf8) != 0xd0:
        continue                      # jmp (An) only
    an = b[1] & 7
    f = fm.getFunctionContaining(jmp.getAddress())
    if f is None:
        continue
    # look back for the lea and the table shape
    base = None; kind = None; cur = jmp
    for _ in range(10):
        cur = listing.getInstructionBefore(cur.getAddress())
        if cur is None: break
        mn = cur.getMnemonicString()
        if mn.startswith('adda') and ('(A%d)' % an) in cur.toString() and kind is None:
            kind = 'word'
        if mn.startswith('movea') and ('A%d,D' % an) in cur.toString().replace(' ', '') and kind is None:
            kind = 'long'
        if mn == 'lea' and cur.toString().endswith('A%d' % an):
            base = cur.getAddress(0)
            if base is None and ',PC)' in cur.toString():
                # (d16,PC): PC is the lea's address + 2
                for o in cur.getOpObjects(0):
                    if hasattr(o, 'getSignedValue'):
                        base = cur.getAddress().add(2 + o.getSignedValue())
            break
        if mn.startswith('jsr') or mn.startswith('bsr') or mn.startswith('rts'):
            break
    if base is None or kind is None:
        skipped += 1
        print('  SKIP 0x%05X in %s (no lea/table shape)' % (jmp.getAddress().getOffset(), f.getName()))
        continue
    body = f.getBody()
    # A target is this function's own code (in its body, or in the gap between its
    # entry and the table that no function owns -- the states Ghidra never reached)
    # or the ENTRY of another function (a tail jump). Anything else ends the table.
    def _ok(t):
        if t.getOffset() >= 0x40000: return False
        if body.contains(t): return True
        g = fm.getFunctionContaining(t)
        if g is None: return f.getEntryPoint().compareTo(t) <= 0 and t.compareTo(base) < 0
        return g.getEntryPoint().equals(t)
    targets = ArrayList(); gaps = []; i = 0
    while i < 64:
        e = base.add(2 * i) if kind == 'word' else base.add(4 * i)
        if kind == 'word':
            # `adda.w d0,a1 ; adda.w (a1),a1`: the word is relative to ITS OWN entry
            # address, not the table base (2026-10-04: base-relative gave 0xFE86 and
            # 0xFED4, both mid-instruction; entry-relative gives 0xFE88/0xFEDA).
            w = mem.getShort(e)
            t = e.add(w)
        else:
            t = addr(mem.getInt(e) & 0xffffffff)
        if not _ok(t):
            break
        if body.contains(e) and e.compareTo(jmp.getAddress()) < 0:
            break                     # ran into code
        if fm.getFunctionContaining(t) is None: gaps.append(t)
        targets.add(t); i += 1
    if targets.size() == 0:
        skipped += 1
        print('  SKIP 0x%05X in %s (table at 0x%05X names nothing inside the function)' % (jmp.getAddress().getOffset(), f.getName(), base.getOffset()))
        continue
    # the gaps between the function's fragments may hold a previous run's misaligned
    # disassembly (targets computed base-relative); clear them before disassembling.
    prev = None
    for r in body:
        if prev is not None and r.getMinAddress().compareTo(prev) > 0:
            listing.clearCodeUnits(prev, r.getMinAddress().subtract(1), False)
        prev = r.getMaxAddress().add(1)
    for t in gaps:
        if listing.getInstructionAt(t) is None:
            DisassembleCommand(t, None, True).applyTo(currentProgram)
    try:
        jt = JumpTable(jmp.getAddress(), targets, True, 0)  # 12.x: + display format
    except TypeError:
        jt = JumpTable(jmp.getAddress(), targets, True)     # 11.x
    jt.writeOverride(f)
    CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor)
    done += 1
    print('  JUMPTABLE 0x%05X in %s: %s table at 0x%05X, %d targets: %s' % (
        jmp.getAddress().getOffset(), f.getName(), kind, base.getOffset(), targets.size(),
        ' '.join('%05X' % targets.get(k).getOffset() for k in range(targets.size()))))
    print('      body now: %s' % ' '.join('%05X-%05X' % (r.getMinAddress().getOffset(), r.getMaxAddress().getOffset()) for r in f.getBody()))
print('make_jumptables: %d overrides written, %d sites skipped' % (done, skipped))
