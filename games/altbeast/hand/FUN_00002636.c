/* FUN_00002636 -- THE TITLE LOGO'S COLOUR CYCLE after the fade-in, one step per call.
 *
 *   2636: lea 0x2934,%a0 ; moveq #9,%d1 ; lea 0x840250,%a1
 *   2642: moveb 0xfffff02d,%d0 ; subib #15,%d0 ; andiw #255,%d0 ; bsrs 0x2622
 *                                     10 colour sets from row (f02d - 15)
 *   2650: addqb #1,0xfffff02d ; cap at 37
 *
 * REGION 1 (altbeastj 0x2654-0x265A, tools/region_diff.py 2026-10-30): `moveq #6,%d1`,
 * `lea 0x8400c0,%a1`, and the table is altbeastj's own -- 97 of its bytes differ from US
 * 0x2934 -- so it comes from region1_002952 (tools/region_data.py, 29 rows: the most 7
 * sets read at row 22). */
extern const unsigned short region1_002952[];

uint FUN_00002636(undefined4 p_D0)
{
  uint uRow = (byte)(DAT_fffff02d - 0xf);

  if (port_region == 1u)
    FUN_00002622((int)region1_002952,(undefined4 *)A68VIDA(0x8400c0u),
                 CONCAT22((short)((uint)p_D0 >> 0x10),(ushort)uRow),6);
  else
    FUN_00002622((int)(&DAT_00002934),(undefined4 *)A68VIDA(0x840250u),
                 CONCAT22((short)((uint)p_D0 >> 0x10),(ushort)uRow),9);
  DAT_fffff02d = DAT_fffff02d + 1;
  if (0x25 < DAT_fffff02d) {
    DAT_fffff02d = 0x25;
  }
  return 0;
}
