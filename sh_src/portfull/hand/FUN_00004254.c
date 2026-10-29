/* FUN_00004254 -- the high-score name entry for both players, from the 68K listing:
 *
 *   4254: lea f029,a3 ; lea e000,a4 ; lea fc74,a5 ; lea f028,a6 ; bsr 4276      P1
 *   4266: lea f028,a3 ; lea e080,a4 ; lea fc82,a5 ; lea f029,a6 ; (falls into 4276)  P2
 *
 * Ghidra inlined the P2 half as a second copy of 0x4276's body (with its unfed extraouts);
 * this calls the hand-derived FUN_00004276 twice instead. A6 is left on 0xFFF029, as the
 * 68K leaves it. */
uint FUN_00004254()
{
  _r_unaff_A6 = (byte *)W68PTR(0xFFF028u);
  FUN_00004276(0xfffff029u, (int)0xffffe000u, (undefined *)0xfffffc74u);
  _r_unaff_A6 = (byte *)W68PTR(0xFFF029u);
  FUN_00004276(0xfffff028u, (int)0xffffe080u, (undefined *)0xfffffc82u);
  return 0;
}
