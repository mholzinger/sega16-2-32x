/* FUN_0000aebc -- an attack/jump transition block of a player form (0xAEBC), an entry Ghidra
 * never made a function of (it is reached only by `jmp (a0)` after the form's attack lookup,
 * hand/FUN_0000af4a.c). Written from the 68K: set the state (+0x20) and script (+0x24), the
 * second variant on the D5 button bit, enqueue sound 0xB6, reset the animation, and fall
 * into the state handler that follows it. */
uint FUN_0000aebc(uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  _a6[0x20] = 0xb; ST32((_a6 + 0x24), 0xb74eu);
  if ((p_D5 & 4u) != 0) {
    _a6[0x20] = 0xc; ST32((_a6 + 0x24), 0xb74eu);
    FUN_0000b204();
  }
  sound_enqueue(0xffffffb6u);   /* moveq #-74,d0 ; jsr 0x3352 */
  _a6[0x21] = 0; _a6[0x22] = 1;
  return FUN_0000af1c(p_D5,p_D6,p_D7);   /* 0xAF1C */
}
