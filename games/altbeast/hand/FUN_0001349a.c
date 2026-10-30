/* Native 1349A..134FC returns BOTH closest player A0 and distance D0.w.
 * Ghidra retained A0 but dropped D0, leaving the final boss AI's extraout
 * reads stale. Explicit state consumers read the published distance. */
uint FUN_0001349a()
{
  byte *boss = _r_unaff_A6;
  byte *p1 = (byte *)W68PTR(0xFFC000u);
  byte *p2 = (byte *)W68PTR(0xFFC400u);
  ushort d0 = 0x1000, d1 = 0x1000;
  byte f0 = 0, f1 = 0;
  uint target = 0xFFFFC000u;
  if (DAT_fffff028 == 1 && p1[0x20] < 0x11) {
    d0 = *(ushort *)(p1 + 0x40) - *(ushort *)(boss + 0xc);
    if (*(ushort *)(p1 + 0x40) < *(ushort *)(boss + 0xc)) { d0 = -d0; f0 = 0x80; }
  }
  if (DAT_fffff029 == 1 && p2[0x20] < 0x11) {
    d1 = *(ushort *)(p2 + 0x40) - *(ushort *)(boss + 0xc);
    if (*(ushort *)(p2 + 0x40) < *(ushort *)(boss + 0xc)) { d1 = -d1; f1 = 0x80; }
  }
  if (d1 < d0) { target = 0xFFFFC400u; d0 = d1; f0 = f1; }
  *(short *)(boss + 0x6c) = (short)target;
  boss[0x2e] = f0;
  _port_reg[0] = (_port_reg[0] & 0xffff0000u) | d0;
  return target;
}
