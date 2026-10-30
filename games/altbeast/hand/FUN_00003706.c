/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00003706(int p_A5)
{
  byte *_dst = (byte *)A68PTR(p_A5);
  const byte *_src = (const byte *)W68PTR(0xFFF032u);
  uint _x = 0;
  int _i;
  for (_i = 3; _i >= 0; _i--) {
    uint _a = _dst[4 + _i], _b = _src[_i];
    uint _lo = (_a & 0xFu) + (_b & 0xFu) + _x;
    uint _hi = (_a >> 4) + (_b >> 4);
    if (_lo > 9u) { _lo += 6u; }
    _hi += _lo >> 4; _lo &= 0xFu;
    _x = 0;
    if (_hi > 9u) { _hi += 6u; _x = 1; }
    _dst[4 + _i] = (byte)(((_hi & 0xFu) << 4) | _lo);
  }
  return 0;
}
