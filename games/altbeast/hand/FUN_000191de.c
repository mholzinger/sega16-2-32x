/* Native 0x191DE..192D8. Keep the per-part A6 palette contexts: the
 * generated globals retained stores but lost the two relative A6 advances. */
uint FUN_000191de()
{
  byte *saved = _r_unaff_A6;
  byte *base = (byte *)W68PTR(0xFFD400u), *s;
  const byte *entries = (const byte *)ROM_BASE + 0x19852;
  int i, k;
  for (i = 0; i < 8; i++) {
    s = base + i * 0x80;
    for (k = 0; k < 0x80; k++) s[k] = 0;
    s[8] = entries[i]; s[0x2f] = 3; s[9] = 2;
  }
  s = base;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x192deu);
  s[0xb] = 0x74;
  *(short *)(s + 0x40) = *(short *)W68PTR(0xFFF132u) + 0x118;
  *(short *)(s + 0x44) = 0x10b8;
  s[0x22] = 1; ST32(s + 0x24, 0x19922u); s[0x4a] = 0x40;
  *(short *)(s + 0x16) = -*(const short *)((const byte *)ROM_BASE + 0x19848);
  *(short *)(s + 0x18) = -*(const short *)((const byte *)ROM_BASE + 0x19846);
  *(short *)(s + 0x1c) = -*(const short *)((const byte *)ROM_BASE + 0x19850);
  *(short *)(s + 0x1e) = -*(const short *)((const byte *)ROM_BASE + 0x1984c);
  _r_unaff_A6 = s; request_palette_update(); s[0x6c] = s[0xa];
  s[0xb] = 1; request_palette_update(); s[0x6d] = s[0xa];
  s += 0x80; _r_unaff_A6 = s;
  *(short *)s = (short)0x8000; ST32(s + 2, 0x1946cu);
  *(short *)(s + 6) = 0x200; request_palette_update();
  s += 0x80; _r_unaff_A6 = s;
  *(short *)s = (short)0x8800; ST32(s + 2, 0x194a0u);
  *(short *)(s + 6) = 0x201; s[0xb] = 0x75; request_palette_update();
  _r_unaff_A6 = saved;
  return 0;
}
