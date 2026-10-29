/* hide_object_sprite (0x3F04, 28 bytes): clear the object's sprite and drop it from the
 * four order lists. From the 68K listing:
 *
 *   3f04: bclr #3,(fp)
 *   3f0a: moveq #0,d6 ; moveb $8(fp),d6 ; lslw #4,d6 ; addw #$F800,d6 ; moveaw d6,a0
 *   3f18: clrl (a0)
 *   3f1c: braw 3cb6                       the order-list marks, on +8 as well
 *
 * WHY A HAND FILE (2026-10-23). The sprite index +8 is a byte and the address is formed in
 * 16 bits: 0xF800 + index*16 WRAPS for an index of 0x80 or more, and movea.w sign-extends
 * the result, so the clear lands at 0x000000+ (ROM, dropped) and never at 0xFFF800 + 0x800
 * and beyond. The decompile wrote W68PTR(0xFFF800) + index*16, which for the 0xFF the
 * despawn walk 0xE16A stamps on every unindexed record reaches 0x313F0: inside .ramtext
 * (r60_punch). The same walk can arrive here with A6 outside work RAM (a 68K address in ROM,
 * see port_despawn_index in sh_src/port_l2.c); the 68000's bclr on ROM is a no-op and the
 * SH-2's must be too. The order-list marks (FUN_00003cb6) read +8 only and are left as the
 * decompile has them. */
uint hide_object_sprite()
{
  byte *a6 = (byte *)_r_unaff_A6;
  unsigned w;
  if (A68SH(a6)) *a6 &= 0xf7;                              /* 3f04: work RAM only */
  w = (0xF800u + ((unsigned)a6[8] << 4)) & 0xFFFFu;        /* 3f0a-3f16: a 16-bit sum, sign-extended */
  if (w & 0x8000u) ST32((W68PTR(0xFF0000u | w)), (0));     /* 0xFFF800..0xFFFFF0 (index < 0x80) */
  /* index >= 0x80: a0 = 0x000000..0x0007F0, ROM: the clr.l is dropped */
  FUN_00003cb6();                                          /* 3f1c */
  return 0;
}
