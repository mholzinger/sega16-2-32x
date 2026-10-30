/* Native19596..195C4: detached level4 parts die when the head's STATE
 * byte is negative or they pass camera-32. DATffffd420 is an unsigned
 * byte; Ghidra's `-1 < DAT...` could never see the native TST.B/BMI.
 * Restoring the body makes this cleanup path reachable again. */
uint FUN_00019596(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  byte *part = _r_unaff_A6;
  if ((signed char)*(byte *)W68PTR(0xFFD420u) < 0 ||
      *(ushort *)(part + 0xc) <= (ushort)(*(ushort *)W68PTR(0xFFF132u) - 0x20)) {
    *(short *)part = 0;
    release_palette_slot();
    hide_object_sprite();
    return 0;
  }
  part[0x23]++;
  FUN_000195c6((int)((byte *)ROM_BASE + 0x1985a), part[0x23], p_D3, p_D6, p_D7);
  return 0;
}
