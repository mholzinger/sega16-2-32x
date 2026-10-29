/* 0xC5FA -- the TAIL of player form 5's walking handler (FUN_0000c5e4), the sibling of
 * hand/AT_FUN_0000c514.c; the same bytes as form 1's 0x7DB0 apart from addresses and the walk
 * speeds (0x80 / 0x40,0x300 where form 1 has 0x40 / 0x40,0x200).
 * 0xC5EE does `lea 0xc5fa(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     c5fa  btst #4,d7 ; bne c562          direction bit 4: state 1 (0xC562), falls into 0xC582
 *     c602  andi.w #0xc0,d7 ; bne c622
 *     c608  move.w #0x80,0x16(a6) ; tst.w 0x14(a6) ; beq c4ea ; bmi c61c ; neg.w 0x16(a6)
 *     c61c  clr.w 0x18(a6) ; rts
 *     c622  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne c4ea
 *     c62c  move.w #0x40,0x16(a6) ; move.w #0x300,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq c652 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; c652 rts
 *     c562  move.b #1,0x20(a6) ; move.l #0xcf26,0x24(a6) ; clr.b 0x21 ; move.b #1,0x22
 *           clr.w 0x14 ; clr.l 0x16                (shared with the standing tail)
 *
 * 0xC4EA is form 5's standing entry (state 0; FUN_0000c4ea falls into its standing handler). */
uint FUN_0000c5fa(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* c5fa btst #4,d7 ; bne c562 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0xcf26u);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_0000c582(p_D0,p_D5,p_D6,p_D7);             /* falls into 0xC582 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* c602 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* c606 bne c622 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x80;                             /* c608 move.w #0x80,0x16(a6) */
    if (vx == 0) return FUN_0000c4ea(p_D0,p_D5,p_D6,p_D7);   /* c60e tst.w ; beq c4ea */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x80;                /* c616 bmi c61c ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* c61c clr.w 0x18(a6) */
    return (uint)p_D0;                                               /* c620 rts */
  }
  {
    uint d0 = ((uint)p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* c622 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_0000c4ea(d0,p_D5,p_D6,p_D7);   /* c628 bne c4ea */
    *(short *)(_a6 + 0x16) = 0x40; *(short *)(_a6 + 0x18) = 0x300;        /* c62c */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* c638 bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* c63e btst #7,d7 ; beq c652 */
      *(short *)(_a6 + 0x16) = -0x40; *(short *)(_a6 + 0x18) = -0x300;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* c652 rts */
  }
}
