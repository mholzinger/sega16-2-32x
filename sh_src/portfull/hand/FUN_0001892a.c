/* 0x1892A: the level-three boss's four records. Native 0x1899E/0x189D0/
 * 0x189FC advance A6 before each palette request. The decompile retained
 * global field stores but lost those advances, so parts 41..43 never acquired
 * their palettes and the weak point's normal/flash banks were both zero. */
uint FUN_0001892a()
{
  byte *saved = _r_unaff_A6;
  byte *base = (byte *)W68PTR(0xFFD400u);
  const byte *entries = (const byte *)ROM_BASE + 0x18f30;
  byte *s;
  int i, k;
  for (i = 0; i < 8; i++) {
    s = base + i * 0x80;
    for (k = 0; k < 0x80; k++) s[k] = 0;
    s[8] = entries[i]; s[0x2f] = 3; s[9] = 2;
  }
  s = base;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x18a48u);
  s[0xb] = 0x8d;
  *(short *)(s + 0x40) = *(short *)W68PTR(0xFFF132u) + 0x100;
  *(short *)(s + 0x44) = 0x10e8;
  s[0x4a] = 4; s[0x22] = 1; ST32(s + 0x24, 0x18f38u);
  _r_unaff_A6 = s; request_palette_update();

  s += 0x80;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x18afcu);
  s[0xb] = 0x8e; s[0x21] = 1; s[0x22] = 0x78;
  ST32(s + 0x24, 0x18f50u);
  _r_unaff_A6 = s; request_palette_update();

  s += 0x80;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x18b4au);
  s[0xb] = 0x8e; s[0x22] = 2; ST32(s + 0x24, 0x18f7eu);
  _r_unaff_A6 = s; request_palette_update();

  s += 0x80;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x18b90u);
  s[0x4a] = 0x80; s[0xb] = 0x91; s[0x22] = 1;
  ST32(s + 0x24, 0x19046u);
  _r_unaff_A6 = s; request_palette_update();
  s[0x6c] = s[10]; s[0xb] = 1; request_palette_update();
  s[0x6d] = s[10];
  _r_unaff_A6 = saved;
  return 0;
}
