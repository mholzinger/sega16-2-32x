# -*- coding: utf-8 -*-
# Ghidra headless (Jython) script ? DISCOVERY-TIMING arc census.
# Run via tools/ghidra_run.sh; do not run from the GUI.
#
# Emits one JSON with everything the timing-gate hunt needs from the
# analysed arcade program:
#   functions      : entry, name, size, callers, callees
#   loops          : every backward branch (dbf/dbra, bcc backwards) with
#                    its span and the memory operands inside the span ?
#                    a backward branch whose body reads a hardware/IO or
#                    work-RAM address and nothing else is a WAIT LOOP
#   hw_refs        : every instruction touching a System 16B hardware
#                    range (tile/text RAM, sprite RAM, palette, I/O,
#                    tile bank regs, MCU mailboxes) with its function
#   vectors        : the exception vector table as the loader sees it
# Addresses are the ARCADE map (the program as Sega assembled it).
import json, sys
from ghidra.program.model.symbol import RefType
from ghidra.program.model.address import AddressSet

args = getScriptArgs()
out_path = args[0] if args else '/tmp/timing_census.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
refmgr = currentProgram.getReferenceManager()
mem = currentProgram.getMemory()

HW = [  # (lo, hi, label) ? arcade addresses (NOTES.md decoded map)
    (0x3F0000, 0x3FFFFF, 'tilebank'),
    (0x400000, 0x40FFFF, 'tileram'),
    (0x410000, 0x410FFF, 'textram'),
    (0x440000, 0x44FFFF, 'spriteram'),
    (0x840000, 0x840FFF, 'palette'),
    (0xC40000, 0xC43FFF, 'io'),
    (0xFE0000, 0xFE003F, 'mapper'),
    (0xFFF0C0, 0xFFF0C7, 'mcu_mailbox'),
    (0xFFF01C, 0xFFF01F, 'frameflag'),
    (0xFFF144, 0xFFF145, 'misscounter'),
    (0xFFC000, 0xFFFFFF, 'workram'),
]

def hw_label(a):
    a &= 0xFFFFFF                      # 68000: 24-bit bus; short absolute
                                       # operands arrive sign-extended
    for lo, hi, lab in HW:
        if lo <= a <= hi:
            return lab
    return None

def func_of(addr):
    f = fm.getFunctionContaining(addr)
    return f.getName() if f else None

# ---- functions ----
funcs = []
for f in fm.getFunctions(True):
    callers = sorted(set(str(c.getName()) for c in f.getCallingFunctions(monitor)))
    callees = sorted(set(str(c.getName()) for c in f.getCalledFunctions(monitor)))
    funcs.append({'entry': '0x%X' % f.getEntryPoint().getOffset(), 'name': f.getName(),
                  'size': f.getBody().getNumAddresses(), 'callers': callers, 'callees': callees})

# ---- instruction walk: loops + hw refs ----
loops = []
hw_refs = []
ins = listing.getInstructions(True)
for i in ins:
    a = i.getAddress()
    mnem = i.getMnemonicString().lower()
    # memory operand refs
    for r in i.getReferencesFrom():
        if r.getReferenceType().isData() or r.getReferenceType() == RefType.READ or r.getReferenceType() == RefType.WRITE:
            to = r.getToAddress().getOffset() & 0xFFFFFF
            lab = hw_label(to)
            if lab:
                hw_refs.append({'pc': '0x%X' % a.getOffset(), 'mnem': mnem, 'to': '0x%X' % to,
                                'region': lab, 'rw': 'w' if r.getReferenceType().isWrite() else 'r',
                                'func': func_of(a)})
    # backward branches
    ft = i.getFlowType()
    if ft.isJump() or ft.isConditional():
        for r in i.getReferencesFrom():
            if r.getReferenceType().isFlow():
                tgt = r.getToAddress()
                if tgt.getOffset() < a.getOffset() and a.getOffset() - tgt.getOffset() <= 0x200:
                    body = []
                    j = listing.getInstructionAt(tgt)
                    while j is not None and j.getAddress().getOffset() <= a.getOffset():
                        for rr in j.getReferencesFrom():
                            if not rr.getReferenceType().isFlow():
                                body.append('0x%X' % (rr.getToAddress().getOffset() & 0xFFFFFF))
                        j = j.getNext()
                    loops.append({'branch_pc': '0x%X' % a.getOffset(), 'mnem': mnem,
                                  'top': '0x%X' % tgt.getOffset(),
                                  'span': a.getOffset() - tgt.getOffset(),
                                  'body_refs': sorted(set(body)),
                                  'hw_in_body': sorted(set(hw_label(int(x, 16)) for x in body if hw_label(int(x, 16)))),
                                  'func': func_of(a)})

# ---- vectors ----
vectors = {}
for n, name in ((0x64, 'irq1'), (0x68, 'irq2'), (0x6C, 'irq3'), (0x70, 'irq4'), (0x74, 'irq5'), (0x78, 'irq6'), (0x7C, 'irq7'), (0x04, 'reset_pc')):  # 68000 autovectors
    try:
        vectors[name] = '0x%X' % mem.getInt(toAddr(n))
    except Exception:
        pass

json.dump({'program': str(currentProgram.getName()), 'functions': funcs, 'loops': loops,
           'hw_refs': hw_refs, 'vectors': vectors}, open(out_path, 'w'), indent=1)
print('timing_census: %d functions, %d backward branches, %d hw refs -> %s' % (len(funcs), len(loops), len(hw_refs), out_path))
