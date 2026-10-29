uint FUN_000100b0(short *p_A0)
{
  /* 0x100B0: the tracked object gone (flags >= 0) marks +0x7f; carry = not
     installed (bit 3 clear) and +0x7f set. */
  byte *_a6 = (byte *)_r_unaff_A6;
  short *_a0 = (short *)A68NORM(p_A0);
  if (*_a0 >= 0) _a6[0x7f] = 0xff;
  port_carry = ((_a6[0] & 8) == 0) && (_a6[0x7f] != 0);
  return (uint)_a0;
}
