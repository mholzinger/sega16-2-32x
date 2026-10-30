static ushort port_cc_step(ushort d2, ushort d3, ushort d4, ushort d6, ushort d7)
{
  ushort d0 = (ushort)(d6 & d2), d1 = (ushort)(d7 & d2), bit = (ushort)(1u << d3);
  if (d1 == d0) return d7;
  if (d1 < d0) { ushort old = (ushort)(d7 & bit); d7 = (ushort)(d7 ^ bit); if (old) d7 = (ushort)(d7 + d4); }
  else         { ushort old = (ushort)(d7 & bit); d7 = (ushort)(d7 ^ bit); if (!old) d7 = (ushort)(d7 - d4); }
  DAT_ffffe800 = 1;
  return d7;
}
/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00017188(undefined4 p_D2,undefined4 p_D6,undefined4 p_D7)
{
  const byte *_t;
  ushort *_a0;
  ushort *_a1;
  short _n;
  if (DAT_ffffe801 != '\0') return 0;
  DAT_ffffe800 = '\0';
  _t = (const byte *)ROM_BASE + 0x1724c + ((uint)DAT_fffff142 << 2);
  _n = (short)(((ushort)_t[2] << 8) | _t[3]);
  _a0 = (ushort *)&DAT_ffffe600;
  _a1 = (ushort *)&DAT_ffffe400;
  do {
    ushort d6 = *_a0, d7 = *_a1;
    _a0 = _a0 + 1;
    d6 = (ushort)((d6 << 4) | (d6 >> 12));
    d7 = (ushort)((d7 << 4) | (d7 >> 12));
    d7 = port_cc_step(0x00f1u, 0u, 0x10u, d6, d7);
    d7 = port_cc_step(0x0f02u, 1u, 0x100u, d6, d7);
    d7 = port_cc_step(0xf004u, 2u, 0x1000u, d6, d7);
    d7 = (ushort)((d7 >> 4) | (d7 << 12));
    *_a1 = d7;
    _a1 = _a1 + 1;
  } while (_n-- != 0);
  if (DAT_ffffe800 != '\0') DAT_ffffe801 = '\x01';
  return 0;
}
