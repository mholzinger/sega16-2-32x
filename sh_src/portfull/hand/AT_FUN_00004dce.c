/* 0x4DCE -- the TAIL of player form 0's standing handler (FUN_00004dba), the same bytes as
 * form 1's 0x7CCA (hand/AT_FUN_00007cca.c) apart from addresses.
 * 0x4DC4 does `lea 0x4dce(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     4dce  btst #4,d7 ; bne 4e1c          direction bit 4 held: state 1, falls into 0x4E3C
 *     4dd6  andi.w #0xc0,d7 ; beq 4df8     no left/right: rts
 *     4ddc  move.b 0x2c(a6),d0 ; and.b d7,d0 ; beq 4e90   not facing a wall: state 2, into 0x4EA8
 *     4de6  bclr #7,0x2e(a6) ; btst #7,d7 ; beq 4df8 ; bset #7,0x2e(a6) ; 4df8 rts
 *     4e1c  move.b #1,0x20(a6) ; move.l #0x75e2,0x24(a6) ; clr.b 0x21(a6) ; move.b #1,0x22(a6)
 *           clr.w 0x14(a6) ; clr.l 0x16(a6)       (falls into 0x4E3C)
 *     4e90  move.b #2,0x20(a6) ; move.l #0x75fa,0x24(a6) ; clr.b 0x21(a6) ; move.b #5,0x22(a6)
 *           (falls into 0x4EA8)
 *
 * D7 is masked IN PLACE (andi.w) before the fall-throughs, as the 68K leaves it. */
uint FUN_00004dce(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 4dce btst #4,d7 ; bne 4e1c */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x75e2u);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00004e3c(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x4E3C */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 4dd6 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) return p_D0;                        /* 4dda beq 4df8 (rts) */
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 4ddc move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) == 0) {                                   /* 4de2 beq 4e90 */
      _a6[0x20] = 2; ST32((_a6 + 0x24), 0x75fau);
      _a6[0x21] = 0; _a6[0x22] = 5;
      return FUN_00004ea8(d0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x4EA8 */
    }
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 4de6 bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);   /* 4dec btst #7,d7 ; bset */
    return d0;                                                 /* 4df8 rts */
  }
}
