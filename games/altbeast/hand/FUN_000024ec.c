/* FUN_000024ec -- A TITLE SPRITE OBJECT'S SETUP (handler 0x24C0), with the altbeastj values.
 *
 *   24ec: movew #0x14a0,12(a6) ; movew #0x1030,16(a6) ; lea 0x24c0,a0 ; movew #2,8(a6)
 *   2502: movew #0x1200,78(a6)        US only
 *   252c: movew #0x8000,(a6) ; movel a0,2(a6) ; movew #0x27f,6(a6) ; movew #0x84,10(a6)
 *   2542: clrb 34(a6) ; jsr 0x3b2e ; jmp 0x3dd8
 *
 * REGION 1 (altbeastj 0x2516, tools/region_diff.py 2026-10-30): no write to +0x4E (the
 * object keeps what it had) and +6 = 0x1F0 instead of 0x27F; everything else is the same.
 * The shared tail is 0x252C in US, so FUN_0000250a carries the same switch. */
uint FUN_000024ec(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;

  unaff_A6[6] = 0x14a0;
  unaff_A6[8] = 0x1030;
  unaff_A6[4] = 2;
  if (port_region != 1u)
    unaff_A6[0x27] = 0x1200;
  *unaff_A6 = 0x8000;
  ST32((unaff_A6 + 1), 0x24c0u);
  unaff_A6[3] = (port_region == 1u) ? 0x1f0 : 0x27f;
  unaff_A6[5] = 0x84;
  *(undefined *)(unaff_A6 + 0x11) = 0;
  request_palette_update();
  sprite_build_and_cull(p_D3,p_D6,p_D7);
  return 0;
}
