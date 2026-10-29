/* tilemap_unpack_lo -- 0x16DE, from the 68K listing, with the page mark.
 *
 *   16de: lea 0x400001,%a0        the LOW byte of every tilemap word
 *   16e4: movew #20479,%d1        the same 20480 words = pages 0..9
 *   16e8: moveb %a1@+,%d0         the value
 *   16ea: bnes 0x1700             non-zero: one store, no run
 *   16ec: moveq #0,%d2
 *   16ee: moveb %a1@+,%d2         zero is the escape: a run length follows
 *   16f0: beqs 0x1700             a zero run is still one store
 *   16f2: subqw #1,%d2
 *   16f4: moveb %d0,%a0@          the run
 *   16f6: addql #2,%a0
 *   16f8: subqw #1,%d1
 *   16fa: bcss 0x1708             the word count ran out: done
 *   16fc: dbf %d2,0x16f4
 *   1700: moveb %d0,%a0@          and ALWAYS one more after the run
 *   1702: addql #2,%a0
 *   1704: dbf %d1,0x16e8          next value
 *   1708: rts
 *
 * See tilemap_unpack_hi for why these two are hand files. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
#endif

uint tilemap_unpack_lo(byte *p_A1)
{
  byte *pbSrc = (byte *)A68PTR(p_A1);
  byte *pbDst = &DAT_00400001;
  int iLeft = 0x4FFF;                      /* 0x16E4 movew #20479,%d1 */

  for (;;) {
    byte bVal = *pbSrc++;                  /* 0x16E8 moveb %a1@+,%d0 */
    if (bVal == 0) {                       /* 0x16EA bnes 0x1700 */
      int iRun = *pbSrc++;                 /* 0x16EE moveb %a1@+,%d2 */
      if (iRun != 0) {                     /* 0x16F0 beqs 0x1700 */
        iRun = iRun - 1;                   /* 0x16F2 subqw #1,%d2 */
        do {
          *pbDst = bVal;                   /* 0x16F4 moveb %d0,%a0@ */
          pbDst = pbDst + 2;               /* 0x16F6 addql #2,%a0 */
          iLeft = iLeft - 1;               /* 0x16F8 subqw #1,%d1 */
          if (iLeft < 0) goto done;        /* 0x16FA bcss -- the borrow */
        } while (iRun-- != 0);             /* 0x16FC dbf %d2,0x16F4 */
      }
    }
    *pbDst = bVal;                         /* 0x1700 moveb %d0,%a0@ */
    pbDst = pbDst + 2;                     /* 0x1702 addql #2,%a0 */
    if (iLeft-- == 0) goto done;           /* 0x1704 dbf %d1,0x16E8 */
  }
done:
#ifdef PORT_TILEDIRTY
  {
    uint pg;
    for (pg = 0; pg <= (0x4FFFu * 2u) >> 12; pg = pg + 1)
      port_tile_dirty |= (ushort)(1u << pg);
  }
#endif
  return 0;
}
