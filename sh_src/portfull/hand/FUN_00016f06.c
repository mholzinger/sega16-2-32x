/* FUN_00016f06 -- THE SMOKE PUFFS (0x16F06). Hand-derived: Ghidra MISMODELLED THE
 * `movem.w` SAVE/RESTORE and the loop counter came back as garbage.
 *
 * THE 68K, 0x16F06-0x17014 -- sixteen puffs, one record each:
 *     movem.w d0/d4-d7,-(sp)        save  (A6 is NOT in the list)
 *     ... build the record at (fp) from the table at 0x17260 (or 0x172C0 when d6) ...
 *     addq.w #2,d7 ; mulu.w #3,d7 ; move.w d7,(34,fp)     +0x22 = (D7 + 2) * 3
 *     ... four palette requests through (11,fp)/(10,fp) ...
 *     lea (0x80,fp),fp              NEXT RECORD
 *     movem.w (sp)+,d0/d4-d7        restore -- D7 is the INDEX again, not (D7+2)*3
 *     addq.w #1,d7
 *     dbf d0,0x16F06
 *
 * WHAT GHIDRA EMITTED: the pushes at a FOUR-byte stride (-4, -8, -0xc, -0x10, -0x14)
 * and the pops at a TWO-byte one (-0x12, -0x10, -0xe, -0xc), so every register came
 * back from the wrong slot. D7 is the puff index and it returned as rubbish.
 *
 * WHAT THAT COST (advisor, Mike's sw267 state after killing the boss): slot 42 runs
 * 0x17016 with +0x22 = 0x2EB9 -- an 11961-frame delay instead of (k+2)*3, at most 51.
 * The puff never frees, the round clear never completes, and the red silhouette stays
 * on screen.
 *
 * AND A SECOND DEFECT IN THE SAME BODY, the register-file gap: request_palette_update
 * (0x3B2E) reads the GLOBAL _r_unaff_A6 (altbeast_port.c, `int unaff_A6 = (int)
 * _r_unaff_A6`), while the generated loop advanced only its LOCAL. Every puff wrote
 * (11,fp) to its own record and then ran the request against PUFF 0. The global is
 * kept in step here, and restored on exit as the 68K caller's `movea.l a5,fp` does.
 */
uint FUN_00016f06(int p_A5,undefined4 p_D0,undefined4 p_D4,undefined4 p_D5,undefined4 p_D6,
                 undefined4 p_D7)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;
  byte *_a6sv = _r_unaff_A6;
  undefined **ppuVar1;
  short *psVar2;
  /* D0/D4-D7 as the 68K holds them across the movem: the body's edits to D0 and D7
   * are undone by the restore, so they live in locals and only the tail changes them. */
  ushort d0 = *(ushort *)((byte *)&p_D0 + 2);
  ushort d4 = *(ushort *)((byte *)&p_D4 + 2);
  ushort d5 = *(ushort *)((byte *)&p_D5 + 2);
  ushort d6 = *(ushort *)((byte *)&p_D6 + 2);
  ushort d7 = *(ushort *)((byte *)&p_D7 + 2);

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A5 = (int)A68NORM(p_A5);
  do {
    _r_unaff_A6 = (byte *)unaff_A6;      /* THIS puff's record, for 0x3B2E */
    ppuVar1 = (undefined **)&DAT_00017260;
    if (d6 != 0) {
      ppuVar1 = (undefined **)&PTR_LAB_000172c0;
    }
    ST32((unaff_A6 + 0x12), 0x17320u);
    psVar2 = (short *)((int)(short)(d7 * 6) + (int)ppuVar1);
    unaff_A6[6] = d5 + *psVar2 + *(short *)(p_A5 + 0x40);
    unaff_A6[8] = psVar2[1] + d4;
    unaff_A6[0x11] = (d7 + 2) * 3;                      /* +0x22, the free delay */
    *(char *)((int)unaff_A6 + 0x21) = (char)psVar2[2];
    *unaff_A6 = 0x8000;
    ST32((unaff_A6 + 1), 0x17016u);
    *(undefined *)((int)unaff_A6 + 9) = 3;
    *(undefined *)((int)unaff_A6 + 0x2f) = 3;
    *(undefined *)(unaff_A6 + 0x17) = 0;
    if (d6 == 0) {
      *(undefined *)((int)unaff_A6 + 0xb) = 0x58;
      request_palette_update();
      *(undefined *)(unaff_A6 + 0x36) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 1;
      request_palette_update();
      *(undefined *)((int)unaff_A6 + 0x6d) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 0x5a;
      request_palette_update();
      *(undefined *)(unaff_A6 + 0x37) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 3;
      request_palette_update();
      *(undefined *)((int)unaff_A6 + 0x6f) = *(undefined *)(unaff_A6 + 5);
    }
    else {
      *(undefined *)((int)unaff_A6 + 0xb) = 0x59;
      request_palette_update();
      *(undefined *)(unaff_A6 + 0x36) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 0x58;
      request_palette_update();
      *(undefined *)((int)unaff_A6 + 0x6d) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 0x59;
      request_palette_update();
      *(undefined *)(unaff_A6 + 0x37) = *(undefined *)(unaff_A6 + 5);
      *(undefined *)((int)unaff_A6 + 0xb) = 0x5a;
      request_palette_update();
      *(undefined *)((int)unaff_A6 + 0x6f) = *(undefined *)(unaff_A6 + 5);
    }
    *(undefined *)(unaff_A6 + 0x10) = 0;
    unaff_A6 = unaff_A6 + 0x40;          /* lea (0x80,fp),fp -- 0x40 undefined2 */
    d7 = d7 + 1;                         /* addq.w #1,d7 AFTER the restore */
    d0 = d0 - 1;                         /* dbf d0 */
  } while (d0 != 0xFFFFu);
  _r_unaff_A6 = _a6sv;
  return 0;
}
