/* FUN_0000258a -- THE TILE-RAM BLIT, written from the 68K listing, with the page mark.
 *
 *   258a: movel #0x400000,%d0        d0 = tile RAM base
 *   2590: movew %a0@+,%d0            d0's low word = the destination offset -> 0x0040xxxx
 *   2592: moveal %d0,%a1             a1 = destination
 *   2594: movew %a0@+,%d0            width - 1, in words
 *   2596: movew %a0@+,%d1            height - 1, in rows
 *   2598: moveal %a1,%a2             a2 = this row's start
 *   259a: movew %d0,%d2
 *   259c: movew %a0@+,%a2@+          one word
 *   259e: dbf %d2,0x259c             ... width times
 *   25a2: lea %a1@(128),%a1          next row: +128 bytes = 64 cells, one tilemap row
 *   25a6: dbf %d1,0x2598             ... height times
 *   25aa: rts
 *
 * A0 ADVANCES PAST THE DATA AND THE CALLER RELIES ON IT: 0x2552 calls this eight times in a
 * dbf loop with one `lea`, and 0x2564/0x2572 two and four times, so the walked A0 is the
 * return value (the convention protos.h already carries).
 *
 * WHY IT IS A HAND FILE: this is the only place the port writes tile RAM, so it is the only
 * place that can NAME the page it dirtied. The renderer's half (port_tile_dirty, m_main.c)
 * has been waiting for a producer since 2026-10-06: marking from the ADDRESS macros instead
 * caught 6 of 90 page changes, because the port keeps already-translated SH-2 pointers in
 * work RAM and never re-forms a 68K address. A page is 0x1000 bytes of the 13-page shadow;
 * a rectangle can cross pages, so every page from the first word to the last is marked.
 * Over-marking costs a page upload, under-marking costs a wrong picture. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
extern volatile uint pp_marks_p2;         /* page-2 marks, counted at the store */
extern volatile uint port_tile_load_serial;
#endif

uint FUN_0000258a(undefined2 *p_A0)
{
  ushort *puSrc = (ushort *)A68PTR(p_A0);
  uint uOff = (ushort)puSrc[0];
  int iW = (short)puSrc[1];
  int iH = (short)puSrc[2];
  byte *pbRow = (byte *)A68VID(0x400000u | uOff);
  int y;

  puSrc = puSrc + 3;
  for (y = 0; y <= iH; y = y + 1) {
    ushort *puDst = (ushort *)pbRow;
    int x;
    for (x = 0; x <= iW; x = x + 1)
      *puDst++ = *puSrc++;
    pbRow = pbRow + 128;                      /* 0x25A2 lea 0x80(a1),a1 */
  }
#ifdef PORT_TILEDIRTY
  if (uOff < 0xD000u) {                       /* A68VID leaves anything past the shadow bare */
    if (((uint)iW + 1u) * ((uint)iH + 1u) >= 128u) port_tile_load_serial++;
    uint uLast = uOff + (uint)iH * 128u + ((uint)iW + 1u) * 2u - 1u;
    uint pg = uOff >> 12;
    uint pgEnd = uLast >> 12;
    if (pgEnd > 12u) pgEnd = 12u;
    for (; pg <= pgEnd; pg = pg + 1) {
      port_tile_dirty |= (ushort)(1u << pg);
      if (pg == 2u) pp_marks_p2++;        /* 2026-10-11: does the page-2 mark happen? */
    }
  }
#endif
  return (uint)puSrc;                         /* A0 as the dbf callers expect it */
}
