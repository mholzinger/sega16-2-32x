/* FUN_00002ee4 -- one coin on one chute: the coinage switch at 0x2EF0, 0x2EE4-0x2F80,
 * and the credit add at 0x2F82-0x2FB6, from the 68K listing.
 *
 *   A0 = the chute's coinage record (0xFFF008 coin A, 0xFFF00C coin B):
 *        +0 the setting, 2 * n from 0x14D8 (DIP nibble, inverted; 15 -> 0)
 *        +1 the coins counted toward an N-coins-1-credit setting
 *        +2 the position in a table-driven sequence
 *   A1 = the chute's coin-meter record (0xFFF004 / 0xFFF006); +0 counts pulses to send
 *
 *   2ee4: d2 = (a0) ; cmpi.b #20,d2 ; bge 2f52 ; moveq #0,d1 ; jmp (2ef4,pc,d2.l)
 *         d2  0..10 : 1..6 credits per coin        (2f08-2f12 addq chain, bsr 2f82)
 *         d2 12/14/16: 2/3/4 coins 1 credit         (+1 counts; full -> clr, 1 credit)
 *         d2 18     : 2 coins 3 credits             (first coin counts, second -> 3)
 *   2f52: d2 >= 20: a2 = 0x324A + word[0x324A + d2 - 20] ; a2[0] = sequence length,
 *         a2[1..len] = credits for each coin of the cycle (0 = none):
 *           0x3254  6: 0 1 0 1 1 1   (2C1C, 4C2C 5C3C 6C4C)
 *           0x325B  4: 0 1 0 2       (2C1C, 4C3C)
 *           0x3260  5: 1 1 1 1 2     (1C1C, 5C6C)
 *           0x3266  4: 1 1 1 2       (1C1C, 4C5C)
 *           0x326B  2: 1 2           (1C1C, 2C3C)
 *         d1 = +2 + 1 ; a4 = a2 + d1 ; d1 >= len (signed) -> d1 = 0 ; +2 = d1 ;
 *         credits = (a4) ; nonzero -> bsr 2f82
 *   2f7e: addq.b #1,(a1) ; rts
 *   2f82: f002 = d1 ; d2 = d1 ; d1 += f000 ; > 9 (unsigned): d2 = 9 - f000, f000 = 9,
 *         bra 3052 ; else f000 = d1, sound 0xBD (0x2DF2, movem around it), bra 3052
 *
 * DIPs 66/54 (the arcade's and the port's): coin A nibble 6 -> 0x14D8 gives 18, two coins
 * three credits, nothing on the first. MAME altbeast, 5 coins: F000 0,3,3,6,6, F009 1,0,1,0,1.
 *
 * WHY A HAND FILE (2026-10-25). Ghidra truncated the jump table (halt_baddata on every odd
 * case) and input_edges never set A0/A1 (the `lea`s were dropped), so the switch read the
 * setting through an uninitialised local: every coin was one credit, +1 never counted and
 * the meter pulse went nowhere. The generated 0x2F82 also passed the new total, not the
 * credits added, to 0x3052 (bookkeeping 0xFFFF14). */
static void ee4_credit_add(uint d0, byte n)   /* 0x2F82 */
{
  byte old = DAT_fffff000;
  byte sum = (byte)(n + old);
  DAT_fffff002 = n;
  if (sum > 9) {
    DAT_fffff000 = 9;
    FUN_00003052(d0, sum, (byte)(9 - old));
  } else {
    DAT_fffff000 = sum;
    FUN_00002df2((d0 & 0xFFFFFF00u) | 0xBDu);
    FUN_00003052(d0, sum, n);
  }
}

/* SIGNATURE MATCHES protos.h. A0/A1 may arrive as 68K addresses (input_edges.c, the
 * dispatcher thunk) or SH-2 pointers: A68PTR is the identity on the latter. */
uint FUN_00002ee4(byte *p_A0,byte *p_A1,uint *p_A2,byte *p_A4,int p_D0,int p_D4)
{
  byte *a0 = (byte *)A68PTR(p_A0);
  byte *a1 = (byte *)A68PTR(p_A1);
  const byte *rom = (const byte *)ROM_BASE;
  uint d0 = (uint)p_D0;
  byte d2 = a0[0];
  if (d2 < 20) {
    /* 0x14D8 doubles the setting, so d2 is even; an odd one is an address error on
     * the 68K and cannot be produced */
    switch (d2 >> 1) {
    case 0: case 1: case 2: case 3: case 4: case 5:
      ee4_credit_add(d0, (byte)(1 + (d2 >> 1)));
      break;
    case 6: case 7: case 8:      /* 2f18 tst.b bne / 2f20 cmpi #2 bge / 2f2a cmpi #3 bge */
      if ((d2 == 12) ? (a0[1] != 0) : ((signed char)a0[1] >= (signed char)((d2 >> 1) - 5))) {
        a0[1] = 0;
        ee4_credit_add(d0, 1);
      } else
        a0[1] = (byte)(a0[1] + 1);
      break;
    default:                                      /* 9: 2f34 */
      if (a0[1] != 0) {
        a0[1] = 0;
        ee4_credit_add(d0, 3);
      } else
        a0[1] = (byte)(a0[1] + 1);
      break;
    }
  } else {
    uint t = 0x324Au, a2, idx = (uint)(d2 - 20);
    short off = (short)((rom[t + idx] << 8) | rom[t + idx + 1]);
    byte d1, n;
    a2 = (uint)((int)t + off);
    d1 = (byte)(a0[2] + 1);
    n = rom[a2 + d1];                             /* a4 = a2 + d1, before the wrap */
    if ((signed char)d1 >= (signed char)rom[a2])
      d1 = 0;
    a0[2] = d1;
    if (n != 0)
      ee4_credit_add(d0, n);
  }
  a1[0] = (byte)(a1[0] + 1);                       /* 2f7e: one meter pulse */
  return 0;
}
