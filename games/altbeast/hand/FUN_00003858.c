/* FUN_00003858 -- the lives counter, second copy (0x3858-0x3950): the code of 0x3128 with
 * the icon table at 0x4024, the digit table at 0x403C and the clear value 0xA000 (0x38B0).
 * Called at a game start, a continue and a life lost (0x8A4 0xE06 0xE70 0x100C 0x1162
 * 0x379A); no caller reads D0 after it. hud_lives() is defined in hand/FUN_00003128.c,
 * which the unit includes first (hand files are included in address order). */
uint FUN_00003858(uint p_D0,undefined4 p_D2,undefined4 p_D3)
{
  if (DAT_fffff026 & 1) return p_D0;
  if ((DAT_fffff028 & 1) && !(DAT_fffff028 & 0x14))
    hud_lives(0xFFE000u, *(const byte *)W68PTR(0xFFC06Bu), 0xa400, 0x4024, 0x403c, 0xa000);
  if ((DAT_fffff029 & 1) && !(DAT_fffff029 & 0x14))
    hud_lives(0xFFE080u, *(const byte *)W68PTR(0xFFC46Bu), 0xa600, 0x4024, 0x403c, 0xa000);
  return 0;
}
