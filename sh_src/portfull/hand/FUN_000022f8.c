/* FUN_000022f8 -- the other high-score name row, from the 68K listing. Same reason as
 * hand/FUN_0000228c.c: two deltas out of 0x2362, one each for x and y.
 *
 *   22f8: movew #4264,%d1 ; movew #4240,%d3 ; bsr 0x2362
 *   2302: asrw #2,%d0 ; movew %d0,%d2 ; asrw #1,%d2 ; addw %d2,%d0   d0 = X*3/8
 *   230a: addw %d0,%fp@(12)
 *   230e: asrw #2,%d1 ; movew %d1,%d3 ; asrw #1,%d3 ; addw %d3,%d1   d1 = Y*3/8
 *   2316: addw %d1,%fp@(16)
 *   231a: cmpib #9,0xffffc0a1 ; bcss 0x2332
 *   2322: movew %fp@(78),%d0 ; addw %fp@(40),%d0 ; bpls -> keep, else 0
 *   232e: movew %d0,%fp@(78)
 *   2332: jmp 0x3DD8 = sprite_build_and_cull */
extern short _port_2362_dx, _port_2362_dy;

uint FUN_000022f8(undefined4 p_D1,undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  int unaff_A6 = (int)_r_unaff_A6;
  short sDx, sDy;

  (void)FUN_00002362(CONCAT22((short)((uint)p_D1 >> 0x10), 0x10a8),
                            CONCAT22((short)((uint)p_D3 >> 0x10), 0x1090));
  sDx = _port_2362_dx;    /* d0 at the rts */
  sDy = _port_2362_dy;    /* d1 at the rts */
  *(short *)(unaff_A6 + 0xc) = (short)((sDx >> 2) + (sDx >> 3)
                                       + *(short *)(unaff_A6 + 0xc));   /* 0x2302-0x230A */
  *(short *)(unaff_A6 + 0x10) = (short)((sDy >> 2) + (sDy >> 3)
                                        + *(short *)(unaff_A6 + 0x10)); /* 0x230E-0x2316 */
  if (DAT_ffffc0a1 >= 9) {                                 /* 0x231A cmpib #9 ; bcss */
    short sV = (short)(*(short *)(unaff_A6 + 0x4e) + *(short *)(unaff_A6 + 0x28));
    if (sV < 0) sV = 0;                                    /* 0x232A bpls ; moveq #0 */
    *(short *)(unaff_A6 + 0x4e) = sV;                      /* 0x232E */
  }
  /* d3 AT THE jmp is the Y delta shifted three: 0x2310 movew %d1,%d3 ; 0x2312 asrw #1,%d3,
   * and only its LOW WORD was written, so the caller's own high half rides along. */
  return sprite_build_and_cull(((uint)p_D3 & 0xFFFF0000u)
                               | (uint)(ushort)(short)(sDy >> 3), p_D6, p_D7);                                   /* 0x2332 jmp 0x3DD8 */
}
