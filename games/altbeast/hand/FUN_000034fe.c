/* FUN_000034fe -- 9-bit angle (D1 bits 8:0) to the velocity pair, table 0x3598 (0x34FE-0x3572):
 *   34fe: lea 0x346a,a0 ; btst #8,d1 ; bne 353c ; btst #7,d1 ; bne 3520
 *   350e: and.w #0x7f,d1 ; beq 3518 ; jmp (a0)                     q0: (D0,D1) = 346a(a)
 *   3518: D0 = 0x100, D1 = 0
 *   3520: and.w #0x7f,d1 ; beq 3534 ; d1 = 0x80 - a ; jsr (a0) ; neg.w d0   q1: (-D0, D1)
 *   3534: D0 = 0, D1 = 0x100
 *   353c: btst #7,d1 ; bne 3558
 *   3542: and.w #0x7f,d1 ; beq 3550 ; jsr (a0) ; neg.w d0 ; neg.w d1        q2: (-D0, -D1)
 *   3550: D0 = -0x100, D1 = 0
 *   3558: and.w #0x7f,d1 ; beq 356c ; d1 = 0x80 - a ; jsr (a0) ; neg.w d1   q3: (D0, -D1)
 *   356c: D0 = 0, D1 = -0x100
 * The 0x34F8 entry (a0 = 0x3464, table 0x3618) has no caller in the program.
 * Returns D1, publishes D0 in _r_extraout_D0 (hand/FUN_0000346a.c). The decompile returned
 * the entry-captured extraout_D0 on the q0 a > 0x40 path, dropped both `neg.w d0`, and never
 * set D0 on the four constant paths; its three callers (0xC8F8, 0x184F8, 0x1A636) were then
 * fed extraout_D0w from the D1 return, so vx == vy. */
uint FUN_000034fe(uint p_D1)
{
  unsigned a = p_D1 & 0x7Fu, d0, d1;
  if ((p_D1 & 0x100u) == 0) {
    if ((p_D1 & 0x80u) == 0) {                                  /* 350e */
      if (a == 0) { d0 = 0x100u; d1 = 0; }
      else { d1 = FUN_0000346a(a); d0 = _r_extraout_D0; }
    } else {                                                    /* 3520 */
      if (a == 0) { d0 = 0; d1 = 0x100u; }
      else { d1 = FUN_0000346a(0x80u - a); d0 = (unsigned)(ushort)(-(short)_r_extraout_D0); }
    }
  } else if ((p_D1 & 0x80u) == 0) {                             /* 3542 */
    if (a == 0) { d0 = 0xFF00u; d1 = 0; }
    else {
      d1 = (unsigned)(ushort)(-(short)FUN_0000346a(a));
      d0 = (unsigned)(ushort)(-(short)_r_extraout_D0);
    }
  } else {                                                      /* 3558 */
    if (a == 0) { d0 = 0; d1 = 0xFF00u; }
    else { d1 = (unsigned)(ushort)(-(short)FUN_0000346a(0x80u - a)); d0 = _r_extraout_D0; }
  }
  _r_extraout_D0 = d0;
  return d1;
}
