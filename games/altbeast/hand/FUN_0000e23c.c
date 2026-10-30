ulonglong FUN_0000e23c(int p_A0,undefined4 p_D1)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_a0 = (byte *)A68NORM(p_A0);
  ushort _d0 = (ushort)(*(ushort *)(_a0 + 0x40) - *(ushort *)(_a6 + 0x40));
  uint _facing = (_a6[0x2e] & 0x80) != 0;
  (void)p_D1;
  if (*(ushort *)(_a0 + 0x40) < *(ushort *)(_a6 + 0x40)) { _d0 = (ushort)(-_d0); port_carry = !_facing; }
  else port_carry = _facing;
  port_e_d0 = _d0;
  return CONCAT44((uint)_a0, (uint)_d0);
}
