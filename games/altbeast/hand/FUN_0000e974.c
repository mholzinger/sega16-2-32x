/* FUN_0000e974 (20 B): `jsr 0x33ba ; move.w d0,0x14(fp) ; move.w d1,0x1a(fp) ; move.w d1,0x1e(fp) ; rts`.
 * vx from D0, vy from D1 (hand/FUN_000033ba.c publishes D0 in _r_extraout_D0 and returns D1).
 * The decompile stored the single return into all three fields (2026-10-23, the immortal
 * kind-5 flyer: vx == vy, then 0 at hover height). Callers: 0xE8A4 and 0xE91A, both kind 5. */
uint FUN_0000e974(undefined4 p_D0,undefined4 p_D1,undefined4 p_D2,undefined4 p_D3)
{
  byte *fp = (byte *)_r_unaff_A6;
  unsigned d1 = FUN_000033ba(p_D0, p_D1, p_D2, p_D3);
  *(short *)(fp + 0x14) = (short)_r_extraout_D0;
  *(short *)(fp + 0x1a) = (short)d1;
  *(short *)(fp + 0x1e) = (short)d1;
  return 0;
}
