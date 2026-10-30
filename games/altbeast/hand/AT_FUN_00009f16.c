/* 0x9F16 -- the TAIL of player form 3's standing handler (FUN_00009f0c), the same bytes as
 * form 1's 0x7CCA (hand/AT_FUN_00007cca.c) apart from addresses.
 * 0x9F0C does `lea 0x9f16(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     9f16  btst #4,d7 ; bne 9f64          direction bit 4 held: state 1, falls into 0x9F84
 *     9f1e  andi.w #0xc0,d7 ; beq 9f40     no left/right: rts
 *     9f24  move.b 0x2c(a6),d0 ; and.b d7,d0 ; beq 9fce   not facing a wall: state 2, into 0x9FE6
 *     9f2e  bclr #7,0x2e(a6) ; btst #7,d7 ; beq 9f40 ; bset #7,0x2e(a6) ; 9f40 rts
 *     9f64  move.b #1,0x20(a6) ; move.l #0xa90c,0x24(a6) ; clr.b 0x21(a6) ; move.b #1,0x22(a6)
 *           clr.w 0x14(a6) ; clr.l 0x16(a6)       (falls into 0x9F84)
 *     9fce  move.b #2,0x20(a6) ; move.l #0xa924,0x24(a6) ; clr.b 0x21(a6) ; move.b #5,0x22(a6)
 *           (falls into 0x9FE6)
 *
 * D7 is masked IN PLACE (andi.w) before the fall-throughs, as the 68K leaves it. */
uint FUN_00009f16(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 9f16 btst #4,d7 ; bne 9f64 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0xa90cu);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00009f84(p_D0,p_D5,p_D6,p_D7);             /* falls into 0x9F84 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 9f1e andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) return (uint)p_D0;                        /* 9f22 beq 9f40 (rts) */
  {
    uint d0 = ((uint)p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 9f24 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) == 0) {                                   /* 9f2a beq 9fce */
      _a6[0x20] = 2; ST32((_a6 + 0x24), 0xa924u);
      _a6[0x21] = 0; _a6[0x22] = 5;
      return FUN_00009fe6(d0,p_D5,p_D6,p_D7);             /* falls into 0x9FE6 */
    }
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 9f2e bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);   /* 9f34 btst #7,d7 ; bset */
    return d0;                                                 /* 9f40 rts */
  }
}
