/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00017bf8()
{
  /* 0x17BF8: the first free of the 8 records at 0xFFD800 -> A5, carry set. */
  byte *_s = (byte *)W68PTR(0xFFD800u);
  short _i;
  for (_i = 0; _i < 8; _i++, _s += 0x80) {
    if (*(short *)_s == 0) { _r_unaff_A5 = _s; port_carry = 1; return 0; }
  }
  port_carry = 0;
  return 0;
}
