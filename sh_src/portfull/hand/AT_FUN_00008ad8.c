/* 0x8AD8 -- the TAIL of player form 2's standing handler (FUN_00008ace), the same bytes as
 * form 1's 0x7CCA (hand/AT_FUN_00007cca.c) apart from addresses and the state-1 script store.
 * 0x8ACE does `lea 0x8ad8(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     8ad8  btst #4,d7 ; bne 8b26          direction bit 4 held: state 1, falls into 0x8B46
 *     8ae0  andi.w #0xc0,d7 ; beq 8b02     no left/right: rts
 *     8ae6  move.b 0x2c(a6),d0 ; and.b d7,d0 ; beq 8b90   not facing a wall: state 2, into 0x8BA8
 *     8af0  bclr #7,0x2e(a6) ; btst #7,d7 ; beq 8b02 ; bset #7,0x2e(a6) ; 8b02 rts
 *     8b26  move.b #1,0x20(a6) ; lea 0x942c(pc),a0 ; move.l a0,0x24(a6) ; clr.b 0x21(a6) ; move.b #1,0x22(a6)
 *           clr.w 0x14(a6) ; clr.l 0x16(a6)       (falls into 0x8B46)
 *     8b90  move.b #2,0x20(a6) ; move.l #0x9444,0x24(a6) ; clr.b 0x21(a6) ; move.b #5,0x22(a6)
 *           (falls into 0x8BA8)
 *
 * 0x8B26 loads the script with `lea ; move.l a0`, so the 68K leaves A0 = 0x942c where form 1
 * leaves the tail address. Nothing on the fall-through reads A0, and the decompile of the
 * same block (LAB_00008b26) drops it too.
 *
 * D7 is masked IN PLACE (andi.w) before the fall-throughs, as the 68K leaves it. */
uint FUN_00008ad8(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 8ad8 btst #4,d7 ; bne 8b26 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x942cu);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00008b46(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x8B46 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 8ae0 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) return p_D0;                        /* 8ae4 beq 8b02 (rts) */
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 8ae6 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) == 0) {                                   /* 8aec beq 8b90 */
      _a6[0x20] = 2; ST32((_a6 + 0x24), 0x9444u);
      _a6[0x21] = 0; _a6[0x22] = 5;
      return FUN_00008ba8(d0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x8BA8 */
    }
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 8af0 bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);   /* 8af6 btst #7,d7 ; bset */
    return d0;                                                 /* 8b02 rts */
  }
}
