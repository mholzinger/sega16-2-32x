/* FUN_0000250a -- THE OTHER TITLE SPRITE OBJECT'S SETUP (handler 0x24A4), altbeastj values.
 *
 *   250a: movew #0x1220,12(a6) ; movew #0x1034,16(a6) ; movew #0xf400,20(a6)
 *   251c: lea 0x24a4,a0 ; movew #0x402,8(a6)
 *   2526: movew #0x900,78(a6)         US only
 *   252c: the tail shared with FUN_000024ec: +6 = 0x27F (altbeastj 0x1F0)
 *
 * REGION 1 (altbeastj 0x252E, tools/region_diff.py 2026-10-30): as FUN_000024ec. */
uint FUN_0000250a(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;

  unaff_A6[6] = 0x1220;
  unaff_A6[8] = 0x1034;
  unaff_A6[10] = 0xf400;
  unaff_A6[4] = 0x402;
  if (port_region != 1u)
    unaff_A6[0x27] = 0x900;
  *unaff_A6 = 0x8000;
  ST32((unaff_A6 + 1), 0x24a4u);
  unaff_A6[3] = (port_region == 1u) ? 0x1f0 : 0x27f;
  unaff_A6[5] = 0x84;
  *(undefined *)(unaff_A6 + 0x11) = 0;
  request_palette_update();
  sprite_build_and_cull(p_D3,p_D6,p_D7);
  return 0;
}
