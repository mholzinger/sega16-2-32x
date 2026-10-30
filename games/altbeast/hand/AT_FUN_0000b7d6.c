/* 0xB7D6 -- the TAIL of player form 4's standing handler (FUN_0000b7cc), the same bytes as
 * form 1's 0x7CCA (hand/AT_FUN_00007cca.c) apart from addresses.
 * 0xB7CC does `lea 0xb7d6(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     b7d6  btst #4,d7 ; bne b824          direction bit 4 held: state 1, falls into 0xB844
 *     b7de  andi.w #0xc0,d7 ; beq b800     no left/right: rts
 *     b7e4  move.b 0x2c(a6),d0 ; and.b d7,d0 ; beq b88e   not facing a wall: state 2, into 0xB8A6
 *     b7ee  bclr #7,0x2e(a6) ; btst #7,d7 ; beq b800 ; bset #7,0x2e(a6) ; b800 rts
 *     b824  move.b #1,0x20(a6) ; move.l #0xc06a,0x24(a6) ; clr.b 0x21(a6) ; move.b #1,0x22(a6)
 *           clr.w 0x14(a6) ; clr.l 0x16(a6)       (falls into 0xB844)
 *     b88e  move.b #2,0x20(a6) ; move.l #0xc082,0x24(a6) ; clr.b 0x21(a6) ; move.b #5,0x22(a6)
 *           (falls into 0xB8A6)
 *
 * D7 is masked IN PLACE (andi.w) before the fall-throughs, as the 68K leaves it. */
uint FUN_0000b7d6(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* b7d6 btst #4,d7 ; bne b824 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0xc06au);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_0000b844(p_D0,p_D5,p_D6,p_D7);             /* falls into 0xB844 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* b7de andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) return (uint)p_D0;                        /* b7e2 beq b800 (rts) */
  {
    uint d0 = ((uint)p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* b7e4 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) == 0) {                                   /* b7ea beq b88e */
      _a6[0x20] = 2; ST32((_a6 + 0x24), 0xc082u);
      _a6[0x21] = 0; _a6[0x22] = 5;
      return FUN_0000b8a6(d0,p_D5,p_D6,p_D7);             /* falls into 0xB8A6 */
    }
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* b7ee bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);   /* b7f4 btst #7,d7 ; bset */
    return d0;                                                 /* b800 rts */
  }
}
