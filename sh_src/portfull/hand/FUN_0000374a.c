/* FUN_0000374a -- THE SCORE / HI-SCORE UPDATE (0x374A). Hand-derived because the 68K
 * carries A0 ACROSS a bsr that changes it, and the decompiler tracks neither half.
 *
 *     374a: movea.l 8(fp),a2        A2 = the text-RAM destination out of the block
 *     374e: lea 4(fp),a0            A0 = A6 + 4, the PENDING DELTA
 *     3752: bsr 0x3774              the adder: `abcd -(a1),-(a0)` FOUR TIMES, so it
 *                                   RETURNS WITH A0 = A6 + 0 -- the score itself
 *     3754: move.l (a0),d3          D3 = the score (nothing downstream reads D3 here)
 *     3756: bsr 0x37d0              THE DIGIT DRAWER, with A0 = A6 + 0
 *     375a: lea 0xfffff010,a1
 *     375e: cmpm.l (a0)+,(a1)+      hi - score
 *     3760: bhi 0x3770              hi still higher -> skip the promote
 *     3762: move.l -(a0),-(a1)      else hi = score
 *     3764: movea.l a1,a0 ; 3766: lea 0x4100d2,a2 ; 376c: bsr 0x37d0   draw the hi
 *     3770: lea 0x10(fp),a0 ; 3774: (fall into the adder) -- the SECOND block, +0x0C
 *
 * TWO DEFECTS, both measured on a scripted run of the candidate, 2026-10-22.
 *
 * 1. THE SCORE DREW AS ZERO. The earlier hand file passed `unaff_A6 + 4` to the drawer
 *    -- the value `lea` put in A0, not the value the adder LEFT there. A6+4 is the
 *    pending delta and port_score_tick clears it every tick (`clr.l (0x4,A6)`, 0x3730),
 *    so the drawer read 0. FUN_000037d0 prints seven spaces and a single '0' when all
 *    eight nibbles are zero (0x37E8 `bra 0x3800`), which is exactly what text RAM held:
 *        row 1 |..........................       0.......   50000......|
 *    while the score at 0xFFE000 read 00 03 13 00. The HI score drew correctly all
 *    along, which is why this looked like "the score is never drawn".
 *
 * 2. THE SECOND BLOCK WAS A BINARY ADD. The four `bcdAdjust()` calls the decompiler
 *    emits are an INTRINSIC IT DOES NOT DEFINE -- defs.c stubs it `return 0` -- so the
 *    port lost the decimal adjust and the X-flag carry chain, exactly the class
 *    sh_src/port_l2.c:379 documents. 0xFFE00C read 00 02 A1 00, and nibble `A` is not
 *    a decimal digit. FUN_00003774 is stubbed to port_bcd_score_add (CPORT_STUBS),
 *    which implements abcd properly, so the second block just calls it too: A0 = A6+0x10
 *    gives dst = A6+0x0C and src = A6+4, the 68K's operands exactly.
 */
uint FUN_0000374a()
{
  int unaff_A6 = (int)_r_unaff_A6;
  undefined4 p_A2;
  uint *score;

  p_A2 = LD32U((unaff_A6 + 8));              /* 0x374A movea.l 8(fp),a2 */
  FUN_00003774(unaff_A6 + 4);                /* 0x374E lea 4(fp),a0 ; 0x3752 bsr */
  /* THE ADDER LEFT A0 AT A6+0. Every use below reads it from there. */
  score = (uint *)(unaff_A6);
  FUN_000037d0(score, p_A2);                 /* 0x3756, A0 = A6 + 0 */

  if (_DAT_fffff010 <= *score) {             /* 0x375E cmpm.l ; 0x3760 bhi */
    _DAT_fffff010 = *score;                  /* 0x3762 move.l -(a0),-(a1) */
    FUN_000037d0((uint *)(0xfffff010), 0x4100d2);   /* 0x3764/0x3766/0x376C */
  }

  FUN_00003774(unaff_A6 + 0x10);             /* 0x3770 lea 0x10(fp),a0, into the adder */
  return 0;
}
