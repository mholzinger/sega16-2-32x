/* FUN_0000bcf2 -- an attack/jump transition block of a player form (0xBCF2), an entry Ghidra
 * never made a function of (it is reached only by `jmp (a0)` after the form's attack lookup,
 * hand/FUN_0000bbc6.c). Written from the 68K: set the state (+0x20) and script (+0x24), the
 * second variant on the D5 button bit, enqueue sound 0xB6, reset the animation, and fall
 * into the state handler that follows it. */
uint FUN_0000bcf2(undefined4 p_D0,uint p_D5,uint p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  _a6[0x20] = 0xb; ST32((_a6 + 0x24), 0xc2feu);
  if ((p_D5 & 4u) != 0) {   /* 0xBD48 with d0 = 28: the jump attack */
    *(short *)(_a6 + 0x10) = (short)(*(short *)(_a6 + 0x10) - 28);
    _a6[0x72] = _a6[0x72] & 0xef;
    _a6[0x20] = 0xc; ST32((_a6 + 0x24), 0xc40cu);
    sound_enqueue(0xffffffb8u);
    _a6[0x21] = 0; _a6[0x22] = 1;
    if ((_a6[0x2c] & 0x10) != 0) {
      short _v = 0x200;
      if ((_a6[0x2e] & 0x80) != 0) _v = (short)-_v;
      *(short *)(_a6 + 0x14) = _v; *(short *)(_a6 + 0x16) = _v; *(short *)(_a6 + 0x18) = _v;
      *(short *)(_a6 + 0x1a) = (short)0xf600;
    }
    _a6[0x2c] = _a6[0x2c] & 0xef;
    return 0;   /* 0xBDA0 rts */
  }
  sound_enqueue(0xffffffb6u);   /* moveq #-74,d0 ; jsr 0x3352 */
  _a6[0x21] = 0; _a6[0x22] = 1; *(short *)(_a6 + 0x14) = 0; ST32((_a6 + 0x16), 0);   /* clr.b +21 ; move.b #1,+22 ; clr.w +14 ; clr.l +16 */
  return FUN_0000bd26(p_D0,p_D5,p_D6,p_D7);   /* 0xBD26 */
}
