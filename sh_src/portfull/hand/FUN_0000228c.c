/* FUN_0000228c -- a high-score name row, from the 68K listing. Hand-written for the two
 * deltas 0x2362 returns: x slides by the X delta, y by the Y delta, and the generated code
 * used one value for both. See hand/FUN_00002362.c for the measurement.
 *
 *   228c: movew #4648,%d1 ; movew #4240,%d3 ; bsr 0x2362
 *   2298: asrw #2,%d0 ; movew %d0,%d2 ; asrw #1,%d2 ; addw %d2,%d0   d0 = X*3/8
 *   22a0: addw %d0,%fp@(12)
 *   22a4: asrw #2,%d1 ; movew %d1,%d3 ; asrw #1,%d3 ; addw %d3,%d1   d1 = Y*3/8
 *   22ac: addw %d1,%fp@(16)
 *   22b0: moveb 0xffffc0a1,%d0 ; the stage gate: 4 <= d0 < 7 and d0 != 5
 *   22c8: (d0 < 5)  movew %fp@(78),%d0 ; addw %fp@(40),%d0 ; clamp UP to 0x1000
 *   22dc: (d0 > 5)  the same, clamped UP to 0x1F00
 *   22ee: movew %d0,%fp@(78)
 *   22f2: jmp 0x3DD8 = sprite_build_and_cull */
extern short _port_2362_dx, _port_2362_dy;

uint FUN_0000228c(undefined4 p_D1,undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  int unaff_A6 = (int)_r_unaff_A6;
  short sDx, sDy;
  byte bStage;

  (void)FUN_00002362(CONCAT22((short)((uint)p_D1 >> 0x10), 0x1228),
                            CONCAT22((short)((uint)p_D3 >> 0x10), 0x1090));
  sDx = _port_2362_dx;    /* d0 at the rts */
  sDy = _port_2362_dy;    /* d1 at the rts */
  *(short *)(unaff_A6 + 0xc) = (short)((sDx >> 2) + (sDx >> 3)
                                       + *(short *)(unaff_A6 + 0xc));   /* 0x2298-0x22A0 */
  *(short *)(unaff_A6 + 0x10) = (short)((sDy >> 2) + (sDy >> 3)
                                        + *(short *)(unaff_A6 + 0x10)); /* 0x22A4-0x22AC */
  bStage = DAT_ffffc0a1;                                   /* 0x22B0 */
  if (bStage >= 4 && bStage < 7 && bStage != 5) {          /* 0x22B4-0x22C4 */
    short sV = (short)(*(short *)(unaff_A6 + 0x4e) + *(short *)(unaff_A6 + 0x28));
    if (bStage < 5) {                                      /* 0x22C6 bccs */
      if ((ushort)sV >= 0x1100) sV = 0x1000;               /* 0x22D0-0x22D6 */
    } else {
      if ((ushort)sV >= 0x1F00) sV = 0x1F00;               /* 0x22E4-0x22EA */
    }
    *(short *)(unaff_A6 + 0x4e) = sV;                      /* 0x22EE */
  }
  /* d3 AT THE jmp is the Y delta shifted three: 0x22A6 movew %d1,%d3 ; 0x22A8 asrw #1,%d3,
   * and only its LOW WORD was written, so the caller's own high half rides along. */
  return sprite_build_and_cull(((uint)p_D3 & 0xFFFF0000u)
                               | (uint)(ushort)(short)(sDy >> 3), p_D6, p_D7);                                   /* 0x22F2 jmp 0x3DD8 */
}
