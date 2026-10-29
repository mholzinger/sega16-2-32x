/* 0x9FFC -- the TAIL of player form 3's walking handler (FUN_00009fe6), the sibling of
 * hand/AT_FUN_00009f16.c; the same bytes as form 1's 0x7DB0 apart from addresses and the walk
 * speeds (0x80 / 0x80,0x400 where form 1 has 0x40 / 0x40,0x200).
 * 0x9FF0 does `lea 0x9ffc(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     9ffc  btst #4,d7 ; bne 9f64          direction bit 4: state 1 (0x9F64), falls into 0x9F84
 *     a004  andi.w #0xc0,d7 ; bne a024
 *     a00a  move.w #0x80,0x16(a6) ; tst.w 0x14(a6) ; beq 9eec ; bmi a01e ; neg.w 0x16(a6)
 *     a01e  clr.w 0x18(a6) ; rts
 *     a024  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne 9eec
 *     a02e  move.w #0x80,0x16(a6) ; move.w #0x400,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq a054 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; a054 rts
 *     9f64  move.b #1,0x20(a6) ; move.l #0xa90c,0x24(a6) ; clr.b 0x21 ; move.b #1,0x22
 *           clr.w 0x14 ; clr.l 0x16                (shared with the standing tail)
 *
 * 0x9EEC is form 3's standing entry (state 0; FUN_00009eec falls into its standing handler). */
uint FUN_00009ffc(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 9ffc btst #4,d7 ; bne 9f64 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0xa90cu);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00009f84(p_D0,p_D5,p_D6,p_D7);             /* falls into 0x9F84 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* a004 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* a008 bne a024 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x80;                             /* a00a move.w #0x80,0x16(a6) */
    if (vx == 0) return FUN_00009eec(p_D0,p_D5,p_D6,p_D7);   /* a010 tst.w ; beq 9eec */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x80;                /* a018 bmi a01e ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* a01e clr.w 0x18(a6) */
    return (uint)p_D0;                                               /* a022 rts */
  }
  {
    uint d0 = ((uint)p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* a024 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_00009eec(d0,p_D5,p_D6,p_D7);   /* a02a bne 9eec */
    *(short *)(_a6 + 0x16) = 0x80; *(short *)(_a6 + 0x18) = 0x400;        /* a02e */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* a03a bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* a040 btst #7,d7 ; beq a054 */
      *(short *)(_a6 + 0x16) = -0x80; *(short *)(_a6 + 0x18) = -0x400;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* a054 rts */
  }
}
