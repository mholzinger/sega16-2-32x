/* Native16EE2 ->16EF6: same puff-band context, fixed Y and X offset. */
uint FUN_00016ee2(undefined4 p_D4,undefined4 p_D5)
{
  byte *source = _r_unaff_A6;
  _r_unaff_A6 = (byte *)W68PTR(0xFFD800u);
  FUN_00016f06((int)source, 15, (p_D4 & 0xffff0000u) | 0x10d8u,
              (p_D5 & 0xffff0000u) | 0x20u, 0, 0);
  _r_unaff_A6 = source;
  return 0;
}
