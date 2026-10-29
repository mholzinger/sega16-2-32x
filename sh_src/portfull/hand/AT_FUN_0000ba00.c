/* FUN_0000ba00 -- an attack/jump transition block of a player form (0xBA00), an entry Ghidra
 * never made a function of (it is reached only by `jmp (a0)` after the form's attack lookup,
 * hand/FUN_0000bbc6.c). Written from the 68K: set the state (+0x20) and script (+0x24), the
 * second variant on the D5 button bit, enqueue sound 0xB6, reset the animation, and fall
 * into the state handler that follows it. */
uint FUN_0000ba00(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  _a6[0x20] = 0x5; ST32((_a6 + 0x24), 0xc150u);
  if ((p_D5 & 2u) != 0 && *(short *)(_a6 + 0x80) == 0) {   /* 0xBA0E btst #1,d5 ; bne 0xBA52: tst.w +80 ; bne 0xBA16 */
    _a6[0x20] = 0x6; ST32((_a6 + 0x24), 0xc1aau);
  _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);   /* clr.b +21 ; move.b #1,+22 ; clr.w +14 ; clr.l +16 */
    return FUN_0000ba78(p_D0,p_D5,p_D6,p_D7);   /* 0xBA78 */
  }
  sound_enqueue(0xffffffb6u);   /* moveq #-74,d0 ; jsr 0x3352 */
  _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);   /* clr.b +21 ; move.b #1,+22 ; clr.w +14 ; clr.l +16 */
  return FUN_0000ba30(p_D0,p_D5,p_D6,p_D7);   /* 0xBA30 */
}
