/* 0x7DB0 -- the TAIL of player form 1's walking handler (FUN_00007d9a), the sibling of
 * hand/AT_FUN_00007cca.c: 0x7DA4 does `lea 0x7db0(pc),a0 ; andi.w #7,d6 ; bne 0x7cf6`, and
 * the shared block's `jmp (a0)` returns here. Written from the 68K:
 *
 *     7db0  btst #4,d7 ; bne 7d18          direction bit 4: state 1 (0x7D18), falls into 0x7D38
 *     7db8  andi.w #0xc0,d7 ; bne 7dd8
 *     7dbe  move.w #0x40,0x16(a6) ; tst.w 0x14(a6) ; beq 7ca0 ; bmi 7dd2 ; neg.w 0x16(a6)
 *     7dd2  clr.w 0x18(a6) ; rts
 *     7dd8  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne 7ca0
 *     7de2  move.w #0x40,0x16(a6) ; move.w #0x200,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq 7e08 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; 7e08 rts
 *
 * 0x7CA0 is form 1's standing entry (state 0, script 0x8562; FUN_00007ca0 falls into 0x7CC0). */
uint FUN_00007db0(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 7db0 btst #4,d7 ; bne 7d18 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x857au);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00007d38(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x7D38 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 7db8 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* 7dbc bne 7dd8 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x40;                             /* 7dbe move.w #0x40,0x16(a6) */
    if (vx == 0) return FUN_00007ca0(p_D0,p_D2,p_D5,p_D6,p_D7);   /* 7dc4 tst.w ; beq 7ca0 */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x40;                /* 7dcc bmi 7dd2 ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* 7dd2 clr.w 0x18(a6) */
    return p_D0;                                               /* 7dd6 rts */
  }
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 7dd8 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_00007ca0(d0,p_D2,p_D5,p_D6,p_D7);   /* 7dde bne 7ca0 */
    *(short *)(_a6 + 0x16) = 0x40; *(short *)(_a6 + 0x18) = 0x200;        /* 7de2 */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 7dee bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* 7df4 btst #7,d7 ; beq 7e08 */
      *(short *)(_a6 + 0x16) = -0x40; *(short *)(_a6 + 0x18) = -0x200;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* 7e08 rts */
  }
}
