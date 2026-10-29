uint FUN_0000dbb8(short *p_A4,undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  /* 0xDBB8: one death part per call, into the first free record walking DOWN from
     0xFFDE80 (14 records); the 12-byte part record is consumed through A4, which
     the caller continues from; the palette and sprite calls run on A6 = the part;
     carry = installed.  Returns the advanced A4. */
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_s = (byte *)W68PTR(0xFFDE80u);
  byte *_rec = (byte *)p_A4;
  short _i, _k;
  for (_i = 0; _i < 14; _i++, _s -= 0x80) {
    byte _spr, _b;
    short _v;
    if (*(short *)_s < 0) continue;
    _spr = _s[8];
    for (_k = 0; _k < 0x80; _k++) _s[_k] = 0;
    _s[8] = _spr;
    *(short *)_s = (short)0x8000;
    ST32((_s + 2), 0xdc70u);
    *(short *)(_s + 6) = *(short *)_rec; _rec += 2;
    _s[9] = 3;
    _s[0xb] = *_rec++;
    _b = *_rec++;
    _s[0x4e] = _b; _s[0x50] = _b; _s[0x51] = 0;
    ST32((_s + 0x24), (uint)((char *)_rec - (char *)ROM_BASE));   /* movel a4,($24,a5): a 68K ROM address */
    _v = *(short *)(_a6 + 0xc);
    *(short *)(_s + 0x40) = _v;
    _v = (short)(_v + *(short *)_rec); _rec += 2;
    *(short *)(_s + 0xc) = _v;
    _v = *(short *)(_a6 + 0x10);
    *(short *)(_s + 0x44) = _v;
    _v = (short)(_v + *(short *)_rec); _rec += 2;
    *(short *)(_s + 0x10) = _v;
    *(short *)(_s + 0x1c) = 0x100;
    *(short *)(_s + 0x1e) = 0x2000;
    *(short *)(_s + 0x14) = *(short *)_rec; _rec += 2;
    *(short *)(_s + 0x1a) = *(short *)_rec; _rec += 2;
    *(short *)(_s + 0x52) = (short)((ushort)_s[0x4e] << 4);
    _s[0x2f] = 2;
    _r_unaff_A6 = _s;
    request_palette_update();
    sprite_build_and_cull(p_D3,p_D6,p_D7);
    _r_unaff_A6 = _a6;
    port_carry = 1;
    return (uint)_rec;
  }
  port_carry = 0;
  return (uint)_rec;
}
