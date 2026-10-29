/* 0x4EBE -- the TAIL of player form 0's walking handler (FUN_00004ea8), the sibling of
 * hand/AT_FUN_00004dce.c; the same bytes as form 1's 0x7DB0 apart from addresses.
 * 0x4EB2 does `lea 0x4ebe(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     4ebe  btst #4,d7 ; bne 4e1c          direction bit 4: state 1 (0x4E1C), falls into 0x4E3C
 *     4ec6  andi.w #0xc0,d7 ; bne 4ee6
 *     4ecc  move.w #0x40,0x16(a6) ; tst.w 0x14(a6) ; beq 4d9a ; bmi 4ee0 ; neg.w 0x16(a6)
 *     4ee0  clr.w 0x18(a6) ; rts
 *     4ee6  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne 4d9a
 *     4ef0  move.w #0x40,0x16(a6) ; move.w #0x200,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq 4f16 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; 4f16 rts
 *     4e1c  move.b #1,0x20(a6) ; move.l #0x75e2,0x24(a6) ; clr.b 0x21 ; move.b #1,0x22
 *           clr.w 0x14 ; clr.l 0x16                (shared with the standing tail)
 *
 * 0x4D9A is form 0's standing entry (state 0; FUN_00004d9a falls into its standing handler). */
uint FUN_00004ebe(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 4ebe btst #4,d7 ; bne 4e1c */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x75e2u);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00004e3c(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x4E3C */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 4ec6 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* 4eca bne 4ee6 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x40;                             /* 4ecc move.w #0x40,0x16(a6) */
    if (vx == 0) return FUN_00004d9a(p_D0,p_D2,p_D5,p_D6,p_D7);   /* 4ed2 tst.w ; beq 4d9a */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x40;                /* 4eda bmi 4ee0 ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* 4ee0 clr.w 0x18(a6) */
    return p_D0;                                               /* 4ee4 rts */
  }
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 4ee6 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_00004d9a(d0,p_D2,p_D5,p_D6,p_D7);   /* 4eec bne 4d9a */
    *(short *)(_a6 + 0x16) = 0x40; *(short *)(_a6 + 0x18) = 0x200;        /* 4ef0 */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 4efc bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* 4f02 btst #7,d7 ; beq 4f16 */
      *(short *)(_a6 + 0x16) = -0x40; *(short *)(_a6 + 0x18) = -0x200;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* 4f16 rts */
  }
}
