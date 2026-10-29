/* FUN_00003488 -- the velocity table read (14 B): `ext.l d1 ; adda.l d1,a0 ; moveq #0,d0 ;
 * moveq #0,d1 ; move.b (a0)+,d1 ; move.b (a0),d0 ; rts`. TWO results: D1 = the first byte,
 * D0 = the second. The decompile returned the first byte only and its callers took D0 from
 * a stale extraout (2026-10-23, the immortal kind-5 flyer). The port's convention for a
 * second returned register is the register file: D0 goes to _r_extraout_D0, D1 is the
 * return value (the register the decompile already calls the return). */
uint FUN_00003488(int p_A0,undefined4 p_D1)
{
  const byte *t = (const byte *)p_A0 + (unsigned)((ushort)p_D1);
  _r_extraout_D0 = (uint)t[1];
  return (uint)t[0];
}
