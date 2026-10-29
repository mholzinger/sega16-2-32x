/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_000154c0()
{
  /* 0x154C0: walk self and its two +0x78 links; carry = none of the three is
     installed (bit 3). */
  byte *_a0 = (byte *)_r_unaff_A6;
  short _i;
  for (_i = 0; _i < 3; _i++) {
    if ((_a0[0] & 8) != 0) { port_carry = 0; return (uint)_a0; }
    _a0 = (byte *)W68(*(short *)(_a0 + 0x78));
  }
  port_carry = 1;
  return (uint)_a0;
}
