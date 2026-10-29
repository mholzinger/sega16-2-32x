/* Native16EEE ->16EF6: alternate puff table, source A6 restored. */
uint FUN_00016eee(undefined4 p_D4)
{
  byte *source = _r_unaff_A6;
  _r_unaff_A6 = (byte *)W68PTR(0xFFD800u);
  FUN_00016f06((int)source, 15, (p_D4 & 0xffff0000u) | 0x10d8u, 0, 1, 0);
  _r_unaff_A6 = source;
  return 0;
}
