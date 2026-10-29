/* FUN_00002362 -- the high-score step's position base, from the 68K listing.
 *
 *   2362: movew 0xffffc00c,%d0     slot 0's x
 *   2366: subw  0xffffc08c,%d0     minus slot 1's x      -> d0 = THE X DELTA
 *   236a: movew %d0,%d2
 *   236c: addw  %d2,%d1
 *   236e: movew %d1,%fp@(12)       x = d1 + the X delta
 *   2372: movew 0xffffc010,%d1     slot 0's y
 *   2376: subw  0xffffc090,%d1     minus slot 1's y      -> d1 = THE Y DELTA
 *   237a: movew %d1,%d2
 *   237c: addw  %d2,%d3
 *   237e: movew %d3,%fp@(16)       y = d3 + the Y delta
 *   2382: rts                      and it returns with BOTH deltas live
 *
 * IT RETURNS TWO VALUES AND THE DECOMPILER KEPT ONE. Its callers (0x228C, 0x22F8) read d0
 * for x's slide and d1 for y's; the generated code returned a single word and used it for
 * both, so the port added the Y delta to x as well. MEASURED 2026-10-07: arcade frame 1170
 * (game frame 1160) against port frame 1213 held 4 bytes -- slots 2 and 4's x low byte 8
 * apart (a=49 p=41, a=C9 p=C1) and sprite entry 0's +3 (a=66 p=5E) -- which is
 * (Ydelta - Xdelta) * 3/8 to the pixel.
 *
 * So BOTH deltas are published and the return value is left exactly as the generator had
 * it -- the Y delta. 0x2338 and 0x2344 call this too and ignore the return, but changing
 * it moved slot 3 by five bytes while they were still generated code (2026-10-07). */
short _port_2362_dx;        /* d0 at the rts: the X delta -- 0x228C and 0x22F8 slide x by it */
short _port_2362_dy;        /* d1 at the rts: the Y delta */

uint FUN_00002362(undefined4 p_D1,undefined4 p_D3)
{
  int unaff_A6 = (int)_r_unaff_A6;
  short sDx = (short)(_DAT_ffffc00c - _DAT_ffffc08c);      /* 0x2362 / 0x2366 */
  short sDy;

  *(short *)(unaff_A6 + 0xc) = (short)(sDx + (short)p_D1);     /* 0x236C/0x236E: d1's
                                                              * LOW word is the seed */
  sDy = (short)(_DAT_ffffc010 - _DAT_ffffc090);            /* 0x2372 / 0x2376 */
  *(short *)(unaff_A6 + 0x10) = (short)(sDy + (short)p_D3);    /* 0x237C/0x237E */
  _port_2362_dx = sDx;
  _port_2362_dy = sDy;
  /* THE RETURN STAYS WHAT THE GENERATOR RETURNED -- the Y delta. 0x2338 and 0x2344
   * also call this and ignore it, but changing it moved slot 3 by 5 bytes when they
   * were still generated code, so the deltas travel in the globals instead. */
  return (uint)(ushort)sDy;
}
