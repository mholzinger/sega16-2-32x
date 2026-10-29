/* Hand-derived native 19C12..19EB6. Ghidra duplicates the fall-through
 * states into seven entries and loses both 1349A's D0 and 19CF8's D1.
 * Keep explicit register consumers and the native immediate fall-throughs. */
#ifndef FINAL_BOSS_AI_H
#define FINAL_BOSS_AI_H
static void final_boss_anim(byte *s, byte state, uint script)
{
  ST32(s + 0x24, script); s[0x20] = state; s[0x21] = 0; s[0x22] = 1;
}
static uint final_boss_idle(byte *s)
{
  final_boss_anim(s, 0, 0x1a12au);
  *(short *)(s + 0x14) = 0;
  return 0;
}
static uint final_boss_step(byte state)
{
  byte *s = _r_unaff_A6;
  byte facing;
  ushort distance;
  switch (state) {
  case 0:                         /* 19C2A */
    *(short *)(s + 0x14) = 0;
    if (s[0x21] != 0) return 0;
    if (s[0x22] != 2) {
      if (s[0x22] == 15) {
        facing = s[0x2e]; FUN_0001349a();
        if ((ushort)_port_reg[0] == 0x1000) s[0x2e] = facing;
      }
      return 0;
    }
    final_boss_anim(s, 1, 0x1a148u);
    /* fall through: 19C74 */
  case 1:
    facing = s[0x2e]; FUN_0001349a();
    if ((ushort)_port_reg[0] == 0x1000 || facing != s[0x2e]) return final_boss_idle(s);
    *(short *)(s + 0x14) = (s[0x21] & 1) ? ((s[0x2e] & 0x80) ? -0x280 : 0x280) : 0;
    distance = *(ushort *)((byte *)W68(*(short *)(s + 0x6c)) + 0x40) - *(ushort *)(s + 0x40);
    if (s[0x2e] & 0x80) distance = -distance;
    if (distance < 0x48) {
      FUN_00019cf8(0);
      if (_port_reg[1] & 2) { final_boss_anim(s, 4, 0x1a1bau); state = 4; break; }
      if (_port_reg[1] & 1) { final_boss_anim(s, 5, 0x1a264u); state = 5; break; }
    } else if (distance < 0x280 && (s[0x4a] <= 0x10 || distance >= 0xa0)) {
      final_boss_anim(s, 2, 0x1a30eu);
      sound_enqueue(0xffffffcdu); FUN_00019f66(); state = 2; break;
    }
    if ((s[0x21] & 1) && s[0x22] == 1) sound_enqueue(0xffffffbau);
    return 0;
  case 2:                         /* 19D5A */
    break;
  case 3:                         /* 19D88 */
    facing = s[0x2e]; FUN_0001349a();
    if (facing != s[0x2e] || (ushort)_port_reg[0] == 0x1000) return final_boss_idle(s);
    *(short *)(s + 0x14) = (s[0x2e] & 0x80) ? -0x500 : 0x500;
    if ((s[0x21] & 1) && s[0x22] == 1) sound_enqueue(0xffffffbau);
    return 0;
  case 4:                         /* 19DE6 */
  case 5:                         /* 19E34 */
    break;
  case 6:                         /* 19EAC */
    if (s[0x2c] & 0x10) return final_boss_idle(s);
    return 0;
  default:
    return 0;
  }
  *(short *)(s + 0x14) = 0;
  if (s[0x21] == 0 && s[0x22] == 2) {
    if (state != 2) return final_boss_idle(s);
    final_boss_anim(s, 3, 0x1a32cu);
    return final_boss_step(3);
  }
  if (state == 4 && s[0x22] == 1 && (s[0x21] == 2 || s[0x21] == 4)) sound_enqueue(0xffffffc6u);
  if (state == 5 && s[0x22] == 1 && s[0x21] == 2) sound_enqueue(0xffffffc6u);
  return 0;
}
#endif
