/* FUN_0000d8d6 -- THE BOX-B COLLISION TEST (0xD8D6). Hand-derived for ONE reason:
 * four of its six return paths handed back a register the 68K never wrote, and the
 * caller uses that return as a LOOP COUNTER.
 *
 * collide_box_b (0xD8BA) is the loop:
 *     lea 0xffffd800,a4 ; moveq #13,d1
 *  .lp tst.w (a4) ; bpl .nx ; bsr 0xd8d6
 *  .nx lea (0x80,a4),a4 ; dbf d1,.lp
 *
 * D1 is the counter and the 68K keeps it across the bsr: `m68k-elf-objdump` over
 * 0xD8D6-0xD99B finds NO write to D1 at all, and all four tails are `bra 0xD4B8`
 * (hitbox_b_expand, 0xD4B8-0xD4F1), which does not write D1 either.
 *
 * Ghidra modelled those four tails as returning a value, so the port emitted
 * `return extraout_D1/_00/_01/_02`, and those read _r_extraout_D1* -- globals NOTHING
 * IN THE UNIT EVER ASSIGNS (measured: 0 assignments across the whole corpus; defs.c
 * initialises them to 0). The generated collide_box_b does
 * `p_D1 = FUN_0000d8d6(p_A4,p_D1)`, so the counter became 0 the first time a box
 * actually touched, `dbf` fell through, and every later slot was skipped THAT FRAME.
 *
 * WHAT IT COST (Mike, 2026-10-20): walking the risen headstones he falls through
 * halfway across. Slots 48-54 (handler 0xDABC) match the arcade byte for byte --
 * 0x20-wide boxes at 0x28 spacing, 8 px gaps -- and the player box is 0x10 wide, so
 * he always straddles two. One stone pushed him sideways and the stone under him was
 * never tested.
 *
 * NOT APPLIED AS A PASS, and that is deliberate. The same `return extraout_*` shape
 * appears at ten sites in six functions, and the rule "return the caller's value" is
 * only correct where the callee leaves the register alone. It does NOT hold at
 * 0x33BA: that return follows a call to FUN_00003488, which DOES write D0
 * (`moveq #0,%d0` / `moveb (a0),%d0`, 0x348C/0x3492), so the right value there is
 * that call's result, not p_D0. 0x2F82 (A1 x2), 0x346A, 0x34FE and 0x41AA do not even
 * take the register as a parameter, so there is nothing to hand back without adding
 * one. Each site needs its own reading of the listing.
 */
uint FUN_0000d8d6(int p_A4,undefined4 p_D1)
{
  int unaff_A6 = (int)_r_unaff_A6;
  ushort uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A4 = (int)A68NORM(p_A4);
  uVar1 = *(ushort *)(p_A4 + 0x56) - *(ushort *)(unaff_A6 + 0x54);
  if ((((*(ushort *)(p_A4 + 0x56) < *(ushort *)(unaff_A6 + 0x54)) ||
       (uVar2 = *(ushort *)(unaff_A6 + 0x56) - *(ushort *)(p_A4 + 0x54),
       *(ushort *)(unaff_A6 + 0x56) < *(ushort *)(p_A4 + 0x54))) ||
      (uVar3 = *(ushort *)(p_A4 + 0x5a) - *(ushort *)(unaff_A6 + 0x58),
      *(ushort *)(p_A4 + 0x5a) < *(ushort *)(unaff_A6 + 0x58))) ||
     (uVar4 = *(ushort *)(unaff_A6 + 0x5a) - *(ushort *)(p_A4 + 0x58),
     *(ushort *)(unaff_A6 + 0x5a) < *(ushort *)(p_A4 + 0x58))) {
    return p_D1;
  }
  if (uVar2 < uVar1) {
    if (uVar4 < uVar3) {
      if (uVar2 < uVar4) goto LAB_0000d948;
      goto LAB_0000d97e;
    }
    if (uVar2 < uVar3) {
LAB_0000d948:
      *(byte *)(unaff_A6 + 0x2c) = *(byte *)(unaff_A6 + 0x2c) | 0x40;
      *(short *)(unaff_A6 + 0xc) = *(short *)(unaff_A6 + 0xc) - uVar2;
      *(undefined2 *)(unaff_A6 + 0xe) = 0;
      *(undefined2 *)(unaff_A6 + 0x14) = 0;
      hitbox_b_expand();
      return p_D1;   /* D1 is untouched: see the header */
    }
  }
  else {
    if (uVar4 < uVar3) {
      if (uVar1 < uVar4) goto LAB_0000d922;
LAB_0000d97e:
      *(short *)(unaff_A6 + 0x10) = *(short *)(unaff_A6 + 0x10) - uVar4;
      *(undefined2 *)(unaff_A6 + 0x12) = 0;
      if (-1 < *(short *)(unaff_A6 + 0x1a)) {
        *(byte *)(unaff_A6 + 0x2c) = *(byte *)(unaff_A6 + 0x2c) | 0x10;
        *(undefined2 *)(unaff_A6 + 0x1a) = 0;
        hitbox_b_expand();
        return p_D1;   /* D1 is untouched: see the header */
      }
      goto code_r0x0000d4b8;
    }
    if (uVar1 < uVar3) {
LAB_0000d922:
      if ((*(byte *)(unaff_A6 + 0x2d) & 2) != 0) {
        *(undefined *)(p_A4 + 0x4b) = 0xff;
      }
      *(byte *)(unaff_A6 + 0x2c) = *(byte *)(unaff_A6 + 0x2c) | 0x80;
      *(short *)(unaff_A6 + 0xc) = uVar1 + *(short *)(unaff_A6 + 0xc);
      *(undefined2 *)(unaff_A6 + 0xe) = 0xffff;
      *(undefined2 *)(unaff_A6 + 0x14) = 0;
      hitbox_b_expand();
      return p_D1;   /* D1 is untouched: see the header */
    }
  }
  *(short *)(unaff_A6 + 0x10) = uVar3 + *(short *)(unaff_A6 + 0x10);
  *(undefined2 *)(unaff_A6 + 0x12) = 0xffff;
  if (*(short *)(unaff_A6 + 0x1a) < 0) {
    *(byte *)(unaff_A6 + 0x2c) = *(byte *)(unaff_A6 + 0x2c) | 0x20;
    *(undefined2 *)(unaff_A6 + 0x1a) = 0;
    hitbox_b_expand();
    return p_D1;   /* D1 is untouched: see the header */
  }
code_r0x0000d4b8:
  *(short *)(unaff_A6 + 0x54) = *(short *)(unaff_A6 + 0xc) + (short)*(char *)(unaff_A6 + 0x50);
  *(short *)(unaff_A6 + 0x56) = *(short *)(unaff_A6 + 0xc) + (short)*(char *)(unaff_A6 + 0x51);
  *(short *)(unaff_A6 + 0x58) = *(short *)(unaff_A6 + 0x10) + (short)*(char *)(unaff_A6 + 0x52);
  *(short *)(unaff_A6 + 0x5a) = *(short *)(unaff_A6 + 0x10) + (short)*(char *)(unaff_A6 + 0x53);
  return p_D1;
}
