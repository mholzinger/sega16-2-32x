/* 0xB8BC -- the TAIL of player form 4's walking handler (FUN_0000b8a6), the sibling of
 * hand/AT_FUN_0000b7d6.c; the same bytes as form 1's 0x7DB0 apart from addresses and the walk
 * speeds (0x80 / 0x80,0x180 where form 1 has 0x40 / 0x40,0x200).
 * 0xB8B0 does `lea 0xb8bc(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     b8bc  btst #4,d7 ; bne b824          direction bit 4: state 1 (0xB824), falls into 0xB844
 *     b8c4  andi.w #0xc0,d7 ; bne b8e4
 *     b8ca  move.w #0x80,0x16(a6) ; tst.w 0x14(a6) ; beq b7ac ; bmi b8de ; neg.w 0x16(a6)
 *     b8de  clr.w 0x18(a6) ; rts
 *     b8e4  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne b7ac
 *     b8ee  move.w #0x80,0x16(a6) ; move.w #0x180,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq b914 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; b914 rts
 *     b824  move.b #1,0x20(a6) ; move.l #0xc06a,0x24(a6) ; clr.b 0x21 ; move.b #1,0x22
 *           clr.w 0x14 ; clr.l 0x16                (shared with the standing tail)
 *
 * 0xB7AC is form 4's standing entry (state 0; FUN_0000b7ac falls into its standing handler). */
uint FUN_0000b8bc(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* b8bc btst #4,d7 ; bne b824 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0xc06au);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_0000b844(p_D0,p_D5,p_D6,p_D7);             /* falls into 0xB844 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* b8c4 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* b8c8 bne b8e4 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x80;                             /* b8ca move.w #0x80,0x16(a6) */
    if (vx == 0) return FUN_0000b7ac(p_D0,p_D5,p_D6,p_D7);   /* b8d0 tst.w ; beq b7ac */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x80;                /* b8d8 bmi b8de ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* b8de clr.w 0x18(a6) */
    return (uint)p_D0;                                               /* b8e2 rts */
  }
  {
    uint d0 = ((uint)p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* b8e4 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_0000b7ac(d0,p_D5,p_D6,p_D7);   /* b8ea bne b7ac */
    *(short *)(_a6 + 0x16) = 0x80; *(short *)(_a6 + 0x18) = 0x180;        /* b8ee */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* b8fa bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* b900 btst #7,d7 ; beq b914 */
      *(short *)(_a6 + 0x16) = -0x80; *(short *)(_a6 + 0x18) = -0x180;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* b914 rts */
  }
}
