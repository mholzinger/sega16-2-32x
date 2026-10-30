/* 0x7CCA -- the TAIL of player form 1's standing handler (FUN_00007cc0), an entry Ghidra
 * never made a function of. 0x7CC0 does `lea 0x7cca(pc),a0 ; andi.w #7,d6 ; bne 0x7cf6`
 * and the shared button block at 0x7CF6 ends `jmp (a0)` when none of its three attack
 * transitions fires, i.e. it RETURNS HERE. Reached through port_call68_at from the
 * `(*UNRECOVERED_JUMPTABLE_01)()` sites cport_prep routes (the code-pointer pass); the
 * decompile had the SH-2 jumping to cart address 0x7CCA, which froze the game (2026-10-24,
 * the R60ROWFAST tape stall). Written from the 68K:
 *
 *     7cca  btst #4,d7 ; bne 7d18          direction bit 4 held: state 1, falls into 0x7D38
 *     7cd2  andi.w #0xc0,d7 ; beq 7cf4     no left/right: rts
 *     7cd8  move.b 0x2c(a6),d0 ; and.b d7,d0 ; beq 7d82   not facing a wall: state 2, into 0x7D9A
 *     7ce2  bclr #7,0x2e(a6) ; btst #7,d7 ; beq 7cf4 ; bset #7,0x2e(a6) ; 7cf4 rts
 *     7d18  move.b #1,0x20(a6) ; move.l #0x857a,0x24(a6) ; clr.b 0x21(a6) ; move.b #1,0x22(a6)
 *           clr.w 0x14(a6) ; clr.l 0x16(a6)       (falls into 0x7D38)
 *     7d82  move.b #2,0x20(a6) ; move.l #0x8592,0x24(a6) ; clr.b 0x21(a6) ; move.b #5,0x22(a6)
 *           (falls into 0x7D9A)
 *
 * D7 is masked IN PLACE (andi.w) before the fall-throughs, as the 68K leaves it. */
uint FUN_00007cca(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 7cca btst #4,d7 ; bne 7d18 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x857au);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00007d38(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x7D38 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 7cd2 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) return p_D0;                        /* 7cd6 beq 7cf4 (rts) */
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 7cd8 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) == 0) {                                   /* 7cde beq 7d82 */
      _a6[0x20] = 2; ST32((_a6 + 0x24), 0x8592u);
      _a6[0x21] = 0; _a6[0x22] = 5;
      return FUN_00007d9a(d0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x7D9A */
    }
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 7ce2 bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);   /* 7ce8 btst #7,d7 ; bset */
    return d0;                                                 /* 7cf4 rts */
  }
}
