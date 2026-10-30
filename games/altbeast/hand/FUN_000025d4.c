/* FUN_000025d4 -- THE TITLE LOGO'S FADE-IN, one palette step every 4 calls.
 *
 *   25d4: subqb #1,0xfffff02c ; bcss 0x25dc ; rts       the 4-call divider
 *   25dc: moveb #3,0xfffff02c
 *   25e2: lea 0x2734,%a0 ; moveq #9,%d1 ; lea 0x840250,%a1 ; bsrs 0x2612
 *                                     10 colour sets of the logo from step f02d's row
 *   25f0: lea 0x8400b0,%a1 ; lea 0x2834,%a0 ; moveq #0,%d1 ; bsrs 0x2612
 *   25fe: addqb #1,0xfffff02d ; cap at 15
 *
 * REGION 1 (altbeastj 0x2604/0x2606, tools/region_diff.py 2026-10-30): `moveq #6,%d1` and
 * `lea 0x8400c0,%a1` -- 7 sets at palette 0x060 for the Juuouki logo. The source tables are
 * byte-identical in both sets (altbeastj has them 0x1E later). */
uint FUN_000025d4()
{
  bool bVar1;

  bVar1 = DAT_fffff02c != '\0';
  DAT_fffff02c = DAT_fffff02c + -1;
  if (bVar1) {
    return 0;
  }
  DAT_fffff02c = 3;
  if (port_region == 1u)
    FUN_00002612((int)(&DAT_00002734),(undefined4 *)A68VIDA(0x8400c0u),6);
  else
    FUN_00002612((int)(&DAT_00002734),(undefined4 *)A68VIDA(0x840250u),9);
  FUN_00002612((int)(&DAT_00002834),(undefined4 *)A68VIDA(0x8400b0u),0);
  DAT_fffff02d = DAT_fffff02d + 1;
  if (0xf < DAT_fffff02d) {
    DAT_fffff02d = 0xf;
  }
  return 0;
}
