/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_0000e266()
{
  byte *_a6 = (byte *)_r_unaff_A6;
  byte *_c = (byte *)W68PTR(0xFFF156u);
  short _s = *(short *)(_a6 + 0x7c);
  byte *_a0, *_a1;
  ushort _d0, _d1;
  if (_s != 0 && _s != -1) {
    if (_s != -0x4000) _c++;
    *_c -= 1;
  }
  _a1 = (byte *)W68PTR(0xFFC400u); _d1 = (ushort)FUN_0000e2c2((short *)_a1);
  _a0 = (byte *)W68PTR(0xFFC000u); _d0 = (ushort)FUN_0000e2c2((short *)_a0);
  if (_d0 > _d1) { byte *_t = _a0; _a0 = _a1; _a1 = _t; }
  if (*(short *)_a0 < 0) {
    _c = (byte *)W68PTR(0xFFF156u);
    if (_a0 != (byte *)W68PTR(0xFFC000u)) _c++;
    _d0 = (*_c < 2) ? (ushort)A68OF16(_a0) : 0;
  } else {
    _d0 = 0xffffu;
  }
  *(ushort *)(_a6 + 0x7c) = _d0;
  port_e_d0 = _d0;
  port_e_a2 = (uint)_c;   /* 0xE2A2/0xE2AC: A2 = the chosen player's chaser count; the
                             pair handler tests `cmpi.b #2,(a2)` through its A2 parameter */
  return (uint)_a0;
}
