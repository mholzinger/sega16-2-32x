/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00004624(undefined4 *p_A0,undefined4 p_A1,int p_A2)
{
  uint _v = *(uint *)p_A0;
  char *_q = (char *)p_A2;
  byte _lead = 1;
  short _n;
  for (_n = 0; _n < 8; _n++) {
    byte _d = (byte)(_v >> 0x1c);
    _v = (_v << 4) | (_v >> 0x1c);
    _q = _q + 1;
    if (_d != 0) _lead = 0;
    *_q = _lead ? (char)0 : (char)(_d - 0x30);
    _q = _q + 1;
    FUN_00004664(p_A0,p_A1);
  }
  if (_lead) {
    _q = _q - 1;
    *_q = (char)-0x30;
    FUN_00004664(p_A0,p_A1);
  }
  return (undefined4)p_A0;
}
