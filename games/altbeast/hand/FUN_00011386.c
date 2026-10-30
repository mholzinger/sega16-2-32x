/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00011386(byte *p_A0)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  byte _d0, _d1;
  const byte *_a0 = (const byte *)P68((unsigned)p_A0);   /* a 68K ROM table: cport_prep.py */
  short _v;
  if (_a6[0x7e] == 0) return 0;
  _d0 = _a6[0x7e] - 1;
  _a6[0x7e] = _d0;
  /* 0x11392 `move.b (a0)+,d1` ONCE (the period), then 0x11394 `sub.b d1,d0 ; bcs ;
     addq.l #1,a0 ; bra` -- the same d1 every pass: the delta is table[1 + d0/period].
     (The first derivation re-read d1 each pass: the pairs drifted 0x20 px by tick
     0x1FE, rd112 2026-10-04.) */
  _d1 = *_a0++;
  for (;;) {
    if (_d0 < _d1) break;
    _d0 = _d0 - _d1;
    _a0++;
  }
  _v = (short)(signed char)*_a0;
  if ((_a6[0x2e] & 0x80) != 0) _v = -_v;
  *(short *)(_a6 + 0xc) = *(short *)(_a6 + 0xc) + _v;
  return 0;
}
