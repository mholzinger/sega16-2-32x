/* 0x8BBE -- the TAIL of player form 2's walking handler (FUN_00008ba8), the sibling of
 * hand/AT_FUN_00008ad8.c; the same bytes as form 1's 0x7DB0 apart from addresses and the
 * state-1 script store.
 * 0x8BB2 does `lea 0x8bbe(pc),a0 ; andi.w #7,d6 ; bne <shared>` and the shared button block
 * ends `jmp (a0)` when none of its three attack transitions fires, i.e. it RETURNS HERE.
 * Reached through port_call68_at from the routed `(*UNRECOVERED_JUMPTABLE_N)()` sites;
 * a counted miss (port_l2_out[13], [40..47]) until this file. Written from the 68K:
 *
 *     8bbe  btst #4,d7 ; bne 8b26          direction bit 4: state 1 (0x8B26), falls into 0x8B46
 *     8bc6  andi.w #0xc0,d7 ; bne 8be6
 *     8bcc  move.w #0x40,0x16(a6) ; tst.w 0x14(a6) ; beq 8aae ; bmi 8be0 ; neg.w 0x16(a6)
 *     8be0  clr.w 0x18(a6) ; rts
 *     8be6  move.b 0x2c(a6),d0 ; and.b d7,d0 ; bne 8aae
 *     8bf0  move.w #0x40,0x16(a6) ; move.w #0x200,0x18(a6) ; bclr #7,0x2e(a6)
 *           btst #7,d7 ; beq 8c16 ; neg.w 0x16(a6) ; neg.w 0x18(a6) ; bset #7,0x2e(a6) ; 8c16 rts
 *     8b26  move.b #1,0x20(a6) ; lea 0x942c(pc),a0 ; move.l a0,0x24(a6) ; clr.b 0x21 ; move.b #1,0x22
 *           clr.w 0x14 ; clr.l 0x16                (shared with the standing tail)
 *
 * 0x8AAE is form 2's standing entry (state 0; FUN_00008aae falls into its standing handler). */
uint FUN_00008bbe(uint p_D0,undefined4 p_D2,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  if ((p_D7 & 0x10u) != 0) {                                   /* 8bbe btst #4,d7 ; bne 8b26 */
    _a6[0x20] = 1; ST32((_a6 + 0x24), 0x942cu);
    _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);
    return FUN_00008b46(p_D0,p_D2,p_D5,p_D6,p_D7);             /* falls into 0x8B46 */
  }
  p_D7 = (p_D7 & 0xffff0000u) | (p_D7 & 0xc0u);                /* 8bc6 andi.w #0xc0,d7 */
  if ((p_D7 & 0xc0u) == 0) {                                   /* 8bca bne 8be6 not taken */
    short vx = *(short *)(_a6 + 0x14);
    *(short *)(_a6 + 0x16) = 0x40;                             /* 8bcc move.w #0x40,0x16(a6) */
    if (vx == 0) return FUN_00008aae(p_D0,p_D2,p_D5,p_D6,p_D7);   /* 8bd2 tst.w ; beq 8aae */
    if (vx > 0) *(short *)(_a6 + 0x16) = -0x40;                /* 8bda bmi 8be0 ; neg.w 0x16(a6) */
    *(short *)(_a6 + 0x18) = 0;                                /* 8be0 clr.w 0x18(a6) */
    return p_D0;                                               /* 8be4 rts */
  }
  {
    uint d0 = (p_D0 & 0xffffff00u) | (uint)(_a6[0x2c] & (byte)p_D7);   /* 8be6 move.b 0x2c(a6),d0 ; and.b d7,d0 */
    if ((d0 & 0xffu) != 0) return FUN_00008aae(d0,p_D2,p_D5,p_D6,p_D7);   /* 8bec bne 8aae */
    *(short *)(_a6 + 0x16) = 0x40; *(short *)(_a6 + 0x18) = 0x200;        /* 8bf0 */
    _a6[0x2e] = (byte)(_a6[0x2e] & 0x7fu);                     /* 8bfc bclr #7,0x2e(a6) */
    if ((p_D7 & 0x80u) != 0) {                                 /* 8c02 btst #7,d7 ; beq 8c16 */
      *(short *)(_a6 + 0x16) = -0x40; *(short *)(_a6 + 0x18) = -0x200;
      _a6[0x2e] = (byte)(_a6[0x2e] | 0x80u);
    }
    return d0;                                                 /* 8c16 rts */
  }
}
