uint FUN_0000e2dc(int p_A0)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_a0 = (byte *)A68NORM(p_A0);
  byte _d1 = _a0[0x2f];
  if (_d1 == _a6[0x2f]) {
    port_carry = 0;
  } else {
    _d1 = (byte)(_d1 + _a6[0x2f]);
    port_carry = (_d1 < 3);
  }
  return (uint)_a0;
}
