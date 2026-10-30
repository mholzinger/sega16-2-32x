/* FUN_000033ba -- the velocity toward a point (0x33BA-0x343E), from the 68K listing:
 * in D0 = target x, D1 = target y, D2 = own x, D3 = own y; out D0 = vx, D1 = vy (8.8, +-0x100
 * = one pixel a frame on an axis, else the 0x3598 table pair scaled by the angle).
 *
 *   33ba: sub.w d2,d0 ; beq 3424 ; bmi 33f0            dx = 0 / dx < 0 / dx > 0
 *   33c0: sub.w d3,d1 ; beq 33e8 ; bmi 33d4            dx > 0: dy = 0 -> (0x100, 0)
 *   33c6: bsr 3440 ; beq 33e8 ; cmpi.w #0x80,d1 ; beq 3438 ; bra 346a      dy > 0
 *   33d4: neg.w d1 ; bsr 3440 ; beq 33e8 ; cmpi.w #0x80,d1 ; beq 3430 ; bsr 346a ; neg.w d1 ; rts
 *   33e8: (0x100, 0)   33f0: neg.w d0 ; sub.w d3,d1 ; beq 341c ; bmi 3408   dx < 0
 *   33f8: bsr 3440 ; beq 341c ; cmpi #0x80 ; beq 3438 ; bsr 346a ; neg.w d0 ; rts
 *   3408: neg.w d1 ; bsr 3440 ; beq 341c ; cmpi #0x80 ; beq 3430 ; bsr 346a ; neg.w d0 ; neg.w d1 ; rts
 *   341c: (-0x100, 0)  3424: d0 = 0 ; sub.w d3,d1 ; bmi 3430 ; (0, 0x100)   3430: (0, -0x100)   3438: (0, 0x100)
 *   3440 (|dx| in d0, |dy| in d1 -> the angle in d1, 0..0x80):
 *         cmp.w d0,d1 ; beq -> 0x40 ; bcc (d1 >= d0) -> d0 = ((d0 << 8) / d1) >> 2 ; d1 = 0x80 - d0
 *         else d1 = ((d1 << 8) / d0) >> 2            the caller's `beq` after the bsr tests THIS
 *         result for zero (lsr.w sets Z; the 0x80 - d0 path cannot be zero; moveq #0x40 is not)
 *
 * WHY A HAND FILE (2026-10-23). The routine returns a PAIR and the decompile returned one
 * value (D1): the dy == 0 paths came out as `return 0` (D0's 0x100 lost), the dx == 0 paths
 * returned D1 as if it were D0, `neg.w d0` at 0x3404 was dropped, and the `beq` after
 * `bsr 0x3440` was rendered on the flag from before the call. The kind-5 flyer (its only
 * caller, through 0xE7FE and 0xE974) hovered with vx == vy and then 0, never dived, was drawn
 * every frame and never culled: alive from frame 17580 to the end of the level-1 tape with the
 * scroll frozen at 0x15D0. D1 is the return value, D0 is published in _r_extraout_D0. */
static unsigned k5_angle(unsigned ax, unsigned ay)        /* 0x3440 */
{
  if (ay == ax) return 0x40u;
  if (ay < ax) return (((ay << 8) / ax) >> 2) & 0xFFFFu;
  return (0x80u - ((((ax << 8) / ay) >> 2) & 0xFFFFu)) & 0xFFFFu;
}

uint FUN_000033ba(undefined4 p_D0,undefined4 p_D1,undefined4 p_D2,undefined4 p_D3)
{
  short dx = (short)((ushort)p_D0 - (ushort)p_D2);        /* 33ba sub.w d2,d0 */
  short dy = (short)((ushort)p_D1 - (ushort)p_D3);        /* 33c0 / 33f2 / 3426 sub.w d3,d1 */
  unsigned d0, d1;
  if (dx == 0) {                                           /* 3424 */
    d0 = 0;
    d1 = (dy < 0) ? 0xFF00u : 0x100u;
  } else if (dy == 0) {                                    /* 33e8 / 341c */
    d0 = (dx < 0) ? 0xFF00u : 0x100u;
    d1 = 0;
  } else {
    unsigned ax = (unsigned)(ushort)(dx < 0 ? -dx : dx);
    unsigned ay = (unsigned)(ushort)(dy < 0 ? -dy : dy);
    unsigned a = k5_angle(ax, ay);
    if (a == 0) {                                          /* beq after bsr 3440 */
      d0 = (dx < 0) ? 0xFF00u : 0x100u;
      d1 = 0;
    } else if (a == 0x80u) {                               /* 3430 / 3438 */
      d0 = 0;
      d1 = (dy < 0) ? 0xFF00u : 0x100u;
    } else {
      d1 = FUN_0000346a(a);                                /* the pair: D1 returned, D0 published */
      d0 = _r_extraout_D0;
      if (dx < 0) d0 = (unsigned)(ushort)(-(short)d0);     /* 3404 / 3416 neg.w d0 */
      if (dy < 0) d1 = (unsigned)(ushort)(-(short)d1);     /* 33e4 / 3418 neg.w d1 */
    }
  }
  _r_extraout_D0 = d0;                                     /* D0 = vx */
  return d1;                                               /* D1 = vy */
}
