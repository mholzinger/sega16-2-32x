/* FUN_0000346a -- angle (1..0x7F in D1) to the velocity pair, table 0x3598 (0x346A-0x3486):
 *   346a: lea 0x3598,a0
 *   346e: cmpi.w #0x41,d1 ; bcc 347a
 *   3474: subq.w #1,d1 ; add.w d1,d1 ; bra 3488          angle < 0x41: (d1, d0) = (t[2a-2], t[2a-1])
 *   347a: moveq #0x7F,d0 ; sub.w d1,d0 ; move.w d0,d1 ; add.w d1,d1 ; bsr 3488 ; exg d0,d1 ; rts
 *                                                        angle >= 0x41: the mirror entry, SWAPPED
 * Returns D1, publishes D0 in _r_extraout_D0 (see hand/FUN_00003488.c). The decompile returned
 * a stale extraout_D0 on the >= 0x41 path. The 0x3464 entry (table 0x3618) is the other
 * velocity family and is not this function. */
uint FUN_0000346a(undefined4 p_D1)
{
  unsigned a = (ushort)p_D1, d0, d1;
  if (a < 0x41u) {
    d1 = FUN_00003488((int)(const byte *)ROM_BASE + 0x3598, (a - 1u) * 2u);
    d0 = _r_extraout_D0;
  } else {
    d0 = FUN_00003488((int)(const byte *)ROM_BASE + 0x3598, ((0x7Fu - a) & 0xFFFFu) * 2u);   /* exg: D0 = first byte */
    d1 = _r_extraout_D0;                                                                    /*      D1 = second   */
  }
  _r_extraout_D0 = d0;
  return d1;
}
