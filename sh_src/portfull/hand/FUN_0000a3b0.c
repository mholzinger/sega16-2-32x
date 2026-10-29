uint FUN_0000a3b0(undefined4 p_D0,undefined4 p_D6,uint p_D7)

{
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  uint _d0 = p_D7 & 0xc0;
  if (_d0 != 0) {
    unaff_A6[0x2e] = unaff_A6[0x2e] & 0x7f;
    if ((_d0 & 0x80) != 0) unaff_A6[0x2e] = unaff_A6[0x2e] | 0x80;
  }
  _d0 = (uint)(byte)p_D6 & 6;
  if (_d0 == 0) return 0;
  if ((_d0 & 2) != 0) return ((p_D7 & 0x10) != 0) ? 0xa416u : 0xa140u;
  return ((p_D7 & 0x10) != 0) ? 0xa664u : 0xa4dcu;
}
