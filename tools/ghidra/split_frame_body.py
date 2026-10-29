# -*- coding: utf-8 -*-
# Make the ROTATED GAMEPLAY FRAME a function of its own, entered where the arcade
# resumes.
#   tools/ghidra_run.sh script split_frame_body.py [--dry]
#
# WHY. The per-frame loop lives inside 0x005BE (`test_mode_screen`, which is the main
# routine, mis-named) and is ROTATED around its vsync:
#
#     0xAE2 -> tail -> 0xB7C -> 0x97C -> body -> 0xADC (jsr 0x397E wait_frame_flag)
#
# The L2 captures are taken while the 68K is suspended in that wait, which is why
# the arcade's own progress marker at 0xFFFFFEFE reads 0x0AE2 -- the return address
# of that jsr. So ONE ARCADE FRAME BEGINS AT 0xAE2, and 0xAE2 is the only entry that
# yields exactly one frame. Entering at 0x005BE runs the state-initialisation pass
# instead (measured 2026-10-02: marker 0x0094, 1163 wrong, net +1111, of which 868
# cells are RAM that pass legitimately cleared). Entering at the loop head 0x97C
# omits the tail, including `bsr 0x144A` at 0xB04.
#
# GHIDRA FUNCTION BODIES CANNOT OVERLAP, so this REPLACES the function at 0x005BE.
# That is deliberate and costs nothing the L2 gate needs: the gate seeds work RAM
# from a mid-gameplay capture and never boots the game, so the reset/init path at
# 0x005BE..0x63E is not on its path. The driver roots `frame_rotated` instead.
#
# FULL-PORT.md 37. The project is copied and docs/proof/altbeast.c backed up before
# this runs (CLAUDE.md: in-place Ghidra edits are not reliably reversible).
import sys

ENTRY   = 0x0AE2
NEWNAME = 'frame_rotated'
OLD     = 0x005BE

args = getScriptArgs()
DRY = '--dry' in args

sp = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
ea = sp.getAddress(ENTRY)
oa = sp.getAddress(OLD)

def describe(f, tag):
    if f is None:
        print('%s: (none)' % tag)
        return
    b = f.getBody()
    print('%s: %s at %s, body %s, %d byte(s) in %d range(s)'
          % (tag, f.getName(), f.getEntryPoint(), b.getMinAddress(),
             b.getNumAddresses(), b.getNumAddressRanges()))

describe(fm.getFunctionContaining(ea), 'BEFORE, containing 0x%04X' % ENTRY)
describe(fm.getFunctionAt(oa), 'BEFORE, at 0x%05X' % OLD)

if DRY:
    print('--dry: nothing changed')
else:
    owner = fm.getFunctionContaining(ea)
    if owner is not None:
        print('removing %s at %s' % (owner.getName(), owner.getEntryPoint()))
        fm.removeFunction(owner.getEntryPoint())
    f = createFunction(ea, NEWNAME)
    if f is None:
        print('*** createFunction at 0x%04X FAILED -- nothing was split' % ENTRY)
        sys.exit(1)
    describe(f, 'AFTER, new function')
    # A FUNCTION THAT NEVER RETURNS WOULD MAKE EVERY CALLER DEAD CODE. The decompiled
    # body ends at the vsync wait, which the port REDIRECTS to its own hook, so from
    # C's point of view it returns normally. Say so explicitly rather than letting
    # Ghidra's no-return analysis decide from the 68K's infinite loop.
    f.setNoReturn(False)
    print('set no-return = false')

# ---- AND GIVE THE PROLOGUE BACK ITS OWN FUNCTION -----------------------------
#
# Removing the function at 0x005BE left two callers pointing at a non-function:
# `braw 0x62a` at 0xBD2 (the state restart) and the tail of 0x1E54. The decompiler
# renders those as `func_0x000005be()` and the link fails with an undefined
# reference. Stubbing that to `return 0` would silently delete a state transition.
#
# 0x005BE..0x00629 is the PROLOGUE -- it sets SP and SR, checks the soft-reset word
# at 0x45A, calls 0x1306, zeroes a block of work RAM, picks 0xFFFFF030 from the
# table at 0x1844, and falls through into the loop at 0x62A. `frame_rotated`'s body
# begins at 0x62A, so that span is free and the bodies do not overlap. The body has
# to be given EXPLICITLY: auto-analysis would follow the fall-through into 0x62A and
# collide.
#
# It is the program's MAIN ENTRY (the reset vector chain reaches it and it installs
# the stack pointer), which is what the name says. Ghidra's `test_mode_screen` was
# always wrong.
PRO_LO, PRO_HI, PRO_NAME = 0x005BE, 0x00629, 'main_entry'

if not DRY:
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SourceType
    pl, ph = sp.getAddress(PRO_LO), sp.getAddress(PRO_HI)
    if fm.getFunctionContaining(pl) is None:
        body = AddressSet(pl, ph)
        cmd = CreateFunctionCmd(PRO_NAME, pl, body, SourceType.USER_DEFINED)
        if cmd.applyTo(currentProgram):
            describe(fm.getFunctionAt(pl), 'AFTER, prologue')
        else:
            print('*** CreateFunctionCmd at 0x%05X failed: %s'
                  % (PRO_LO, cmd.getStatusMsg()))
    else:
        describe(fm.getFunctionContaining(pl), 'prologue already owned')
