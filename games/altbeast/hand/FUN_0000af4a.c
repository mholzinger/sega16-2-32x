/* FUN_0000af4a -- a player form's attack lookup (0xAF4A), two targets: button-1 edge ->
 * 0xAEE4, button-2 edge -> 0xAEBC, else carry clear. See hand/FUN_00008d66.c. */
uint FUN_0000af4a(undefined4 p_D0,undefined4 p_D6,undefined4 p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  uint _d0 = (uint)p_D7 & 0xc0u;
  if (_d0 != 0) {
    _a6[0x2e] = _a6[0x2e] & 0x7f;
    if ((_d0 & 0x80u) != 0) _a6[0x2e] = _a6[0x2e] | 0x80;
  }
  _d0 = (uint)(byte)p_D6 & 6u;
  if (_d0 == 0) return 0;
  return ((_d0 & 2u) != 0) ? 0xaee4u : 0xaebcu;
}
