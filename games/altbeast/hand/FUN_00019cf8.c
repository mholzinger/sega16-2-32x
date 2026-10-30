/* Native 19CF8..19D34: D1 bits 0/1 say whether the boss's low/high
 * attack heights overlap the target's unsigned [top,bottom) hitbox.
 * The generated 19D1E discarded carry and 19CF8 discarded both bits. */
uint FUN_00019cf8(undefined4 p_D0)
{
  byte *boss = _r_unaff_A6;
  byte *target = (byte *)W68(*(short *)(boss + 0x6c));
  ushort top = *(ushort *)(target + 0x38), bottom = *(ushort *)(target + 0x3a);
  ushort low = *(ushort *)(boss + 0x44) - 8;
  ushort high = *(ushort *)(boss + 0x44) - 0x33;
  _port_reg[1] = (low >= top && low < bottom ? 1u : 0u)
              | (high >= top && high < bottom ? 2u : 0u);
  return 0;
}
