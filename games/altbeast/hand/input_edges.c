/* input_edges -- the coin/service edge detector, 0x2E74-0x2EE3 from the 68K listing.
 * irq4_handler calls it every vint with D0.b = ~MCU_COINS (0xFFF0C2).
 *
 *   2e74: f000 >= 10 (unsigned) -> f000 = 0
 *   2e80: d0 = ~d0 & 0x0B (coin A b0, coin B b1, service b3 as the MCU posts them)
 *         d1 = f003 ; f003 = d0 ; d0 = ~d0 & d1        (bits that were set and are now clear)
 *         d0 == 0 -> 2ed0
 *   2e94: b3: d1 = 1 ; bsr 2fba (the service credit)
 *   2ea0: a0 = 0xFFF008, a1 = 0xFFF004
 *         b0: bsr 3036 (bookkeeping) ; bsr 2ee4 (one coin, chute A)
 *   2eb4: f00c != (a0) -> a0 = 0xFFF00C, a1 = 0xFFF006   (coin B shares A's record when
 *         both chutes have the same setting)
 *         b1: bsr 3044 ; bsr 2ee4
 *   2ed0: bsr 2fe4 with a1 = 0xFFF004, d1 = 1 ; bra 2fe4 with a1 = 0xFFF006, d1 = 2
 *         (the coin-meter pulses)
 * D0 is preserved by every callee (movem), so the b1 test reads the same edge byte.
 *
 * WHY A HAND FILE (2026-10-25). Ghidra dropped both `lea`s, so FUN_00002ee4 received
 * uninitialised locals as A0/A1 and read the coinage setting from nowhere: every coin was
 * one credit where DIPs 66/54 give two coins three (see hand/FUN_00002ee4.c). */
uint input_edges(undefined4 p_A2,undefined4 p_A4,undefined4 p_D0,undefined4 p_D1,undefined4 p_D4)
{
  uint d0 = (uint)p_D0;
  uint a0 = 0xFFF008u, a1 = 0xFFF004u;
  byte cur, edge;
  if (DAT_fffff000 >= 10)
    DAT_fffff000 = 0;
  cur = (byte)(~d0 & 0x0Bu);
  edge = (byte)(~cur & DAT_fffff003);
  DAT_fffff003 = cur;
  d0 = (d0 & 0xFFFFFF00u) | edge;
  if (edge != 0) {
    if (edge & 8)
      switchD_00002ef0__caseD_c6(d0, 1);
    if (edge & 1) {
      FUN_00003036(d0, 1);
      FUN_00002ee4((byte *)a0, (byte *)a1, (uint *)p_A2, (byte *)p_A4, (int)d0, (int)p_D4);
    }
    if (DAT_fffff00c != *(const byte *)W68PTR(a0)) {
      a0 = 0xFFF00Cu;
      a1 = 0xFFF006u;
    }
    if (edge & 2) {
      FUN_00003044(d0, DAT_fffff00c);
      FUN_00002ee4((byte *)a0, (byte *)a1, (uint *)p_A2, (byte *)p_A4, (int)d0, (int)p_D4);
    }
  }
  switchD_00002ef0__caseD_f0((byte *)0xFFF004u, 1);
  switchD_00002ef0__caseD_f0((byte *)0xFFF006u, 2);
  return 0;
}
