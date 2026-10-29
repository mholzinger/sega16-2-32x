/* FUN_0000cae0 -- an attack/jump transition block of a player form (0xCAE0), an entry Ghidra
 * never made a function of (it is reached only by `jmp (a0)` after the form's attack lookup,
 * hand/FUN_0000c9b4.c). Written from the 68K: set the state (+0x20) and script (+0x24), the
 * second variant on the D5 button bit, enqueue sound 0xB6, reset the animation, and fall
 * into the state handler that follows it. */
uint FUN_0000cae0(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  _a6[0x20] = 0xb; ST32((_a6 + 0x24), 0xd1fau);
  if ((p_D5 & 4u) != 0) {   /* 0xCB36 with d0 = 28 */
    *(short *)(_a6 + 0x10) = (short)(*(short *)(_a6 + 0x10) - 28);
    _a6[0x72] = _a6[0x72] & 0xef;
    _a6[0x20] = 0xc;
    sound_enqueue(0xffffffb8u);
    _a6[0x21] = 0; _a6[0x22] = 0x36;
    *(short *)(_a6 + 0x14) = 0; *(short *)(_a6 + 0x16) = 0; *(short *)(_a6 + 0x18) = 0;
    FUN_0000cbdc();
    _a6[0x2c] = _a6[0x2c] & 0xef;
    return FUN_0000cb70(p_D0,p_D5,p_D6,p_D7);   /* 0xCB70 */
  }
  sound_enqueue(0xffffffb6u);   /* moveq #-74,d0 ; jsr 0x3352 */
  _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);   /* clr.b +21 ; move.b #1,+22 ; clr.w +14 ; clr.l +16 */
  return FUN_0000cb14(p_D0,p_D5,p_D6,p_D7);   /* 0xCB14 */
}
