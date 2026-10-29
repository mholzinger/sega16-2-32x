uint FUN_0000e2c2(short *p_A0)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_s = (byte *)A68NORM(p_A0);
  ushort _d0;
  if (*(short *)_s >= 0) { port_e_d0 = 0xffffu; return 0xffffu; }
  _d0 = (ushort)(*(ushort *)(_s + 0x40) - *(ushort *)(_a6 + 0x40));
  if (*(ushort *)(_s + 0x40) < *(ushort *)(_a6 + 0x40)) _d0 = (ushort)(-_d0);
  port_e_d0 = _d0;
  return _d0;
}
