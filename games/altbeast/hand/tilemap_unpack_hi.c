/* tilemap_unpack_hi -- 0x16BE, from the 68K listing, with the page mark.
 *
 *   16be: lea 0x400000,%a0        the HIGH byte of every tilemap word
 *   16c4: movew #20479,%d1        words - 1: 20480 words = 40960 bytes = pages 0..9
 *   16c8: moveq #0,%d0            run top
 *   16ca: moveb %a1@+,%d0         run length
 *   16cc: moveb %a1@+,%d2         the value
 *   16ce: moveb %d2,%a0@          store
 *   16d0: addql #2,%a0            step 2 -- high bytes only
 *   16d2: subqw #1,%d1
 *   16d4: bcss 0x16dc             the word count ran out: done
 *   16d6: dbf %d0,0x16ce          repeat the run (d0+1 stores)
 *   16da: bras 0x16c8             next run
 *   16dc: rts
 *
 * THIS IS THE TILEMAP'S BULK WRITER and the reason the blit's mark caught only 8 of 104
 * changed pages (2026-10-06): a scene load comes through here, not through 0x258A, and it
 * rewrites ten whole pages in one call. The write goes through DAT_00400000, a symbol
 * cport_layout places in the shadow, which is why the A68VID census never saw it. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
#endif

uint tilemap_unpack_hi(byte *p_A1)
{
  byte *pbSrc = (byte *)A68PTR(p_A1);
  byte *pbDst = &DAT_00400000;
  int iLeft = 0x4FFF;                      /* 0x16C4 movew #20479,%d1 */

  for (;;) {
    int iRun = *pbSrc++;                   /* 0x16CA moveb %a1@+,%d0 */
    byte bVal = *pbSrc++;                  /* 0x16CC moveb %a1@+,%d2 */
    do {
      *pbDst = bVal;                       /* 0x16CE moveb %d2,%a0@ */
      pbDst = pbDst + 2;                   /* 0x16D0 addql #2,%a0 */
      iLeft = iLeft - 1;                   /* 0x16D2 subqw #1,%d1 */
      if (iLeft < 0) goto done;            /* 0x16D4 bcss -- the borrow */
    } while (iRun-- != 0);                 /* 0x16D6 dbf %d0 -- d0+1 stores */
  }
done:
#ifdef PORT_TILEDIRTY
  {   /* 20480 words from 0x400000, stepping 2: 0x400000..0x40A000, pages 0..9 */
    uint pg;
    for (pg = 0; pg <= (0x4FFFu * 2u) >> 12; pg = pg + 1)
      port_tile_dirty |= (ushort)(1u << pg);
  }
#endif
  /* THE LO STREAM STARTS WHERE THE HI STREAM STOPPED, 2026-10-10. The 68K never
   * reloads A1 between the two calls -- prog68k.bin 0x16AC moveal %a1@+,%a1, then
   * bsr 0x16BE (hi), then bsr 0x16DE (lo) -- so A1 is hi's advanced cursor and lo
   * decodes from it. unpack_level_tilemap (altbeast_port.c:2934) carries that in
   * the return value, and the generated body returned it (:2970). This hand file
   * returned 0 from b97a8947 (2026-10-07), so lo decoded from pointer 0.
   * Returning the host cursor is safe past lo's own A68PTR: that macro is the
   * identity outside the four 68K video ranges and work RAM (w68.h:155). */
  return (uint)pbSrc;
}
