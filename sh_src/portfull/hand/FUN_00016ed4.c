/* Native16ED4 ->16EF6 saves the source in A5, selects puff slots48..63
 * at FFD800, calls16F06, then restores A6. The generated entry missed
 * this shared-tail context and overwrote the boss plus its partner. */
uint FUN_00016ed4(undefined4 p_D4)
{
  byte *source = _r_unaff_A6;
  ushort y = *(ushort *)(source + 0x44) + 0x60;
  _r_unaff_A6 = (byte *)W68PTR(0xFFD800u);
  FUN_00016f06((int)source, 15, (p_D4 & 0xffff0000u) | y, 0, 0, 0);
  _r_unaff_A6 = source;
  return 0;
}
