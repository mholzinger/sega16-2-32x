/* tilemap_block_a5 -- 0x174E, from the 68K listing, with the page mark.
 *
 *   174e: lea 0x40a230,%a0      20 rows x 40 cells, stepping a row every 128 bytes
 *   1754: lea %pc@(0x199a),%a1  the low-byte table, read straight through
 *   1758: movew #19,%d1         rows - 1
 *   175c: movew #39,%d2         cells - 1   (loop top, a1 NOT restored: one long table)
 *   1760: moveb #-91,%a0@+      the high byte, 0xA5
 *   1764: moveb %a1@+,%a0@+     the low byte
 *   1766: dbf %d2,0x1760
 *   176a: lea %a0@(48),%a0      40 cells = 80 bytes, +48 = the 128-byte row stride
 *   176e: dbf %d1,0x175c
 *   1772: rts
 *
 * One of the two writers the unpackers do not cover: they fill pages 0..9 and this fills
 * 0x40A230 upwards, which is pages 10 and 11. Measured 2026-10-07, with only the unpackers
 * and the blit marking, the page marks caught 28 of 64 changes and every miss was a page
 * the unpackers never touch. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
#endif

uint tilemap_block_a5()
{
  byte *pbDst = (byte *)A68VID(0x40a230u);
  byte *pbTab = (byte *)&DAT_0000199a;
  int iRow;

  for (iRow = 0; iRow <= 19; iRow = iRow + 1) {          /* 0x1758 movew #19,%d1 */
    int iCell;
    for (iCell = 0; iCell <= 39; iCell = iCell + 1) {    /* 0x175C movew #39,%d2 */
      *pbDst++ = 0xA5;                                   /* 0x1760 moveb #-91,%a0@+ */
      *pbDst++ = *pbTab++;                               /* 0x1764 moveb %a1@+,%a0@+ */
    }
    pbDst = pbDst + 48;                                  /* 0x176A lea %a0@(48),%a0 */
  }
#ifdef PORT_TILEDIRTY
  {   /* 0xA230 through 0xA230 + 19*128 + 80: pages 10 and 11 */
    uint pg;
    for (pg = 0xA230u >> 12; pg <= (0xA230u + 19u * 128u + 80u - 1u) >> 12; pg = pg + 1)
      port_tile_dirty |= (ushort)(1u << pg);
  }
#endif
  return 0;
}
