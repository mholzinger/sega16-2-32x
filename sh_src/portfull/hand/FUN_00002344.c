/* FUN_00002344 -- a high-score step object, from the 68K listing.
 *
 *   2344: movew #4240,%d1          d1's low word = 0x1090, the x seed
 *   2348: movew #4224,%d3          d3's low word = 0x1080, the y seed
 *   234c: bsr 0x2362               and 0x2362 does `addw %d2,%d3`, so d3 COMES BACK as
 *                                  0x1080 + the Y delta -- the object's y, the same value
 *                                  it stored at +0x10
 *   234e: (falls straight into the shared tail)
 *   234e: bchg #0,%fp@(34)         toggle bit 0 of the byte at +0x22, Z = its OLD value
 *   2354: beqs 0x235c              old bit 0 -> draw
 *   2356: jmp 0x3f04               old bit 1 -> hide_object_sprite
 *   235c: jmp 0x3dd8               sprite_build_and_cull, WITH THE UPDATED d3
 *
 * WHY IT IS A HAND FILE. The decompile passed sprite_build_and_cull the ORIGINAL seed
 * (CONCAT22(high, 0x1080)) instead of the d3 that 0x2362 updated, so this object's sprite
 * was built against a y of 0x1080 rather than its own. MEASURED 2026-10-07: with 0x2362's
 * two deltas fixed (ae08da63) the later high-score target went exact and the earlier one
 * kept 13 bytes -- slots 1, 3 and 5 plus sprite entry e1 -- at arcade frame 1170 (game
 * frame 1160) against port frame 1208. Slots 3 and 5 are 0x2338's object and this one. */
uint FUN_00002344(undefined4 p_D1,undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  int unaff_A6 = (int)_r_unaff_A6;
  byte bOld;

  FUN_00002362(CONCAT22((short)((uint)p_D1 >> 0x10), 0x1090),
               CONCAT22((short)((uint)p_D3 >> 0x10), 0x1080));
  bOld = *(byte *)(unaff_A6 + 0x22);                       /* 0x234E bchg #0,%fp@(34) */
  *(byte *)(unaff_A6 + 0x22) = (byte)(bOld ^ 1);
  if ((bOld & 1) != 0) {                                   /* 0x2354 beqs -- Z = the old bit */
    hide_object_sprite();                                  /* 0x2356 jmp 0x3F04 */
    return 0;
  }
  /* d3 at the jmp: the y 0x2362 left in it, high half untouched because addw is a word op */
  sprite_build_and_cull(((uint)p_D3 & 0xFFFF0000u)
                        | (uint)(ushort)*(short *)(unaff_A6 + 0x10), p_D6, p_D7);
  return 0;
}
