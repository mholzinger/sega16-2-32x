/* Native 0x19F66 saves A6, keeps the boss in A5, then initializes two
 * effect records starting at boss+0x100. Each 19F86 advances A6 by 0x80.
 * Ghidra folded the first script store but lost the initial A6 advance,
 * overwriting the boss/partner handlers and leaving their sprites behind. */
uint FUN_00019f66()
{
  byte *boss = _r_unaff_A6;
  _r_unaff_A6 = boss + 0x100;
  ST32(_r_unaff_A6 + 0x24, 0x1a39eu);
  FUN_00019f86((int)boss);
  ST32(_r_unaff_A6 + 0x24, 0x1a3d0u);
  FUN_00019f86((int)boss);
  _r_unaff_A6 = boss;
  return 0;
}
