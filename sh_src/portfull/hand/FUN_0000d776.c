uint FUN_0000d776(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  /* 0xD776: the first free record of the 15 at 0xFFD800 takes a projectile from
     this object; `jsr d9f6` runs on A6 = the new record; carry = installed. */
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_s = (byte *)W68PTR(0xFFD800u);
  short _i, _k;
  for (_i = 0; _i < 15; _i++, _s += 0x80) {
    byte _spr;
    if (*(short *)_s != 0) continue;
    _spr = _s[8];
    for (_k = 0; _k < 0x80; _k++) _s[_k] = 0;
    _s[8] = _spr;
    _s[0] = 0x80; _s[1] = 0;
    _s[9] = 2;
    *(short *)(_s + 0xc) = *(short *)(_a6 + 0xc);
    *(short *)(_s + 0x28) = *(short *)(_a6 + 0xc);
    *(short *)(_s + 0x10) = *(short *)(_a6 + 0x10);
    *(short *)(_s + 0x2a) = *(short *)(_a6 + 0x10);
    _s[0x2f] = 0;
    _s[0x2e] = _a6[0x2e];
    if ((_a6[0x2e] & 0x80) == 0) _s[0] |= 1;
    _r_unaff_A6 = _s;
    FUN_0000d9f6(p_D3,p_D6,p_D7);
    _r_unaff_A6 = _a6;
    _DAT_fffff032 = 0x500;
    FUN_00003706((int)W68(*(short *)((char *)W68(*(short *)(_a6 + 0x4c)) + 0x3c)));
    port_carry = 1;
    return 0;
  }
  port_carry = 0;
  return 0;
}
