/* tilemap_block_04 -- 0x170A, from the 68K listing, with the page mark.
 *
 *   170a: lea 0x40b230,%a0
 *   1710: movew #4,%d4            5 outer passes
 *   1714: lea %pc@(0x1cba),%a1    (outer top) the table restarts every pass
 *   1718: movew #3,%d3            4 inner groups
 *   171c: moveal %a0,%a2          (inner top)
 *   171e: bsrw 0x1732             writes 5 rows x 8 cells of (0x04, table byte), 80 bytes,
 *                                 the SAME 8 table bytes reused per row, a0 left advanced
 *   1722: addql #8,%a1            the next 8-byte entry
 *   1724: lea %a0@(48),%a0        80 + 48 = the 128-byte row stride
 *   1728: dbf %d3,0x171c
 *   172c: dbf %d4,0x1714
 *   1730: rts
 *
 * 20 groups of 128 bytes from 0x40B230 = page 11 only. See tilemap_block_a5 for why these
 * two are hand files.
 *
 * THE DESTINATION IS A68VID, NOT A DAT_ SYMBOL. The first version of this file wrote
 * through &DAT_0040b230, which dat_syms.h does not declare -- and cport_prep's feedback
 * loop silently invented it in extras.h ("from gcc's undeclared list"), outside the
 * renderer's shadow, so the writes landed off-map and scribbled: the high-score step's
 * records went 7 bytes wrong to 57 and the FB staging went stale on eight pages and never
 * converged (2026-10-07). A68VID is what `lea 0x40b230,%a0` actually means and the
 * generator already places it. FUN_00001732 is left to the generated unit: it writes through the
 * pointer it is given and the page range is decided here. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
#endif

uint tilemap_block_04()
{
  byte *pbDst = (byte *)A68VID(0x40b230u);
  int iOuter;

  for (iOuter = 0; iOuter <= 4; iOuter = iOuter + 1) {       /* 0x1710 movew #4,%d4 */
    byte *pbTab = (byte *)&DAT_00001cba;                     /* 0x1714 -- restarts */
    int iGroup;
    for (iGroup = 0; iGroup <= 3; iGroup = iGroup + 1) {     /* 0x1718 movew #3,%d3 */
      int iRow;
      for (iRow = 0; iRow <= 4; iRow = iRow + 1) {           /* 0x1732 movew #4,%d1 */
        int iCell;
        for (iCell = 0; iCell <= 7; iCell = iCell + 1) {     /* 0x1738 movew #7,%d2 */
          *pbDst++ = 0x04;                                   /* 0x173C moveb #4,%a0@+ */
          *pbDst++ = pbTab[iCell];                           /* 0x1740 moveb %a1@+,%a0@+
                                                              * (0x1746 restores a1: the
                                                              *  same 8 bytes every row) */
        }
      }
      pbTab = pbTab + 8;                                     /* 0x1722 addql #8,%a1 */
      pbDst = pbDst + 48;                                    /* 0x1724 lea %a0@(48),%a0 */
    }
  }
#ifdef PORT_TILEDIRTY
  {   /* 0xB230 through 20 x 128 bytes: page 11 */
    uint pg;
    for (pg = 0xB230u >> 12; pg <= (0xB230u + 20u * 128u - 1u) >> 12; pg = pg + 1)
      port_tile_dirty |= (ushort)(1u << pg);
  }
#endif
  return 0;
}
