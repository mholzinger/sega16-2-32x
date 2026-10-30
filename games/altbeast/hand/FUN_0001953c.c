/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_0001953c()
{
  /* 0x1953C: copy this object into the first free of the 5 records at 0xFFD580
     (handler 0x19596, dy 0xFE00, +0x23 - 1), sound 0xD2, carry = done. */
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_s = (byte *)W68PTR(0xFFD580u);
  short _i, _k;
  if ((char)DAT_ffffd420 < 0) { port_carry = 0; return 0; }
  for (_i = 0; _i < 5; _i++, _s += 0x80) {
    byte _spr;
    if (_s[0] != 0) continue;
    _spr = _s[8];
    for (_k = 0; _k < 0x80; _k++) _s[_k] = _a6[_k];
    ST32((_s + 2), 0x19596u);
    *(short *)(_s + 0x14) = (short)0xfe00;
    _s[8] = _spr;
    _s[0x23] = _s[0x23] - 1;
    sound_enqueue(0xffffffd2);
    port_carry = 1;
    return 0;
  }
  port_carry = 0;
  return 0;
}
