static ushort port_hs_crc(const byte *p)
{
  ushort c = 0; short n;
  for (n = 0; n < 0x1c; n++) {
    ushort t = (ushort)(c ^ (ushort)((p[2 * n] << 8) | p[2 * n + 1]));
    c = (ushort)(t >> 1);
    if (t & 1) c = (ushort)(c ^ 0x8810);
  }
  return c;
}
static byte port_hs_valid(const byte *p)
{
  ushort c = port_hs_crc(p), st = (ushort)((p[0x38] << 8) | p[0x39]);
  short n;
  if (c != st) return 0;
  if (c != 0) return 1;
  for (n = 0; n < 0x38; n++) if (p[n]) return 1;
  return 0;
}
/* SIGNATURE MATCHES protos.h, 2026-10-11. cport_prep refuses a hand file whose signature
 * disagrees with the prototype its callers use -- but only on a CLEAN port build, so ten
 * of these never fired: no clean port build had succeeded since d696b228 and every rom
 * came from an incremental build reusing build/port. */
uint FUN_00004178()
{
  byte *_prim = (byte *)&DAT_fffffc00;
  byte *_back = _prim + 0x3a;
  byte *_from; byte *_to; short _i;
  if (port_hs_valid(_prim)) { _from = _prim; _to = _back; }
  else if (port_hs_valid(_back)) { _from = _back; _to = _prim; }
  else {
    const byte *_rom = (const byte *)ROM_BASE + 0x4732;
    ushort _c;
    for (_i = 0; _i < 0x38; _i++) _prim[_i] = _rom[_i];
    _c = port_hs_crc(_prim); _prim[0x38] = (byte)(_c >> 8); _prim[0x39] = (byte)_c;
    _from = _prim; _to = _back;
  }
  for (_i = 0; _i < 0x3a; _i++) _to[_i] = _from[_i];
  _DAT_fffff010 = _DAT_fffffc00;
  return 0;
}
