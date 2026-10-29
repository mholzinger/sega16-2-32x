/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00004196(undefined4 *p_A5)
{
  undefined4 *unaff_A6 = (undefined4 *)_r_unaff_A6;
  p_A5 = (undefined4 *)A68PTR(p_A5);   /* 0xFFFC3A arrives as a bare 68K address */
  short _i;
  {
    byte *_to = (byte *)p_A5; const byte *_from = (const byte *)unaff_A6;
    for (_i = 0; _i < 0x3a; _i++) _to[_i] = _from[_i];
  }
  _DAT_fffff010 = _DAT_fffffc00;
  return 0;
}
