/* FUN_0000380a -- THE SMALL TITLE LOGO in the text layer, with the altbeastj one.
 *
 *   380a: btst #0,0xfffff026 ; bnes 0x3814 ; rts
 *   3814: movew #0xAD30,%d0          the first cell's code (pal 6, tile 0x130)
 *   3818: lea 0x41024c,%a1           text RAM, row 4 column 38
 *   381e: movew #5,%d2               6 rows ...
 *   3822: movew #11,%d1              ... of 12 cells, codes counting up
 *   382e: lea %a1@(104),%a1          next row: 64 cells less the 12 written
 *
 * REGION 1 (altbeastj 0x3824, tools/region_diff.py 2026-10-30): `movew #0xAD00,%d0` and
 * `movew #3,%d2` -- the Juuouki logo is 4 rows of 12 starting at tile 0x100. Both sets'
 * glyphs are in the shared text art. Everything else is the same instruction for
 * instruction. */
uint FUN_0000380a()
{
  ushort uCode = 0xad30;
  int iRows = 6;
  ushort *puRow = (ushort *)A68VIDA(0x41024cu);
  int y, x;

  if ((DAT_fffff026 & 1) == 0)
    return 0;
  if (port_region == 1u) {
    uCode = 0xad00;
    iRows = 4;
  }
  for (y = 0; y < iRows; y = y + 1) {
    for (x = 0; x < 12; x = x + 1)
      puRow[x] = uCode++;
    puRow = puRow + 64;                       /* 0x382E: one text row is 64 cells */
  }
  return 0;
}
