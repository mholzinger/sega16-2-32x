/* FUN_0000bc2c -- an attack/jump transition block of a player form (0xBC2C), an entry Ghidra
 * never made a function of (it is reached only by `jmp (a0)` after the form's attack lookup,
 * hand/FUN_0000bbc6.c). Written from the 68K: set the state (+0x20) and script (+0x24), the
 * second variant on the D5 button bit, enqueue sound 0xB6, reset the animation, and fall
 * into the state handler that follows it. */
uint FUN_0000bc2c(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  _a6[0x20] = 0x7; ST32((_a6 + 0x24), 0xc1eeu);
  if ((p_D5 & 2u) != 0) {
    _a6[0x20] = 0x8; ST32((_a6 + 0x24), 0xc25eu);
  }
  sound_enqueue(0xffffffb6u);   /* moveq #-74,d0 ; jsr 0x3352 */
  _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);   /* clr.b +21 ; move.b #1,+22 ; clr.w +14 ; clr.l +16 */
  return FUN_0000bc68(p_D0,p_D5,p_D6,p_D7);   /* falls into 0xBC68 */
}
