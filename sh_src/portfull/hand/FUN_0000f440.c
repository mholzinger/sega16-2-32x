/* FUN_0000f440 -- the ball's collection test against both players (0xF440), written from
 * the 68K: player 1 and player 2 each get `bsr 0xF486 ; bsr/bra 0xF4D0` with A4 = the
 * player record. Ghidra inlined player 2's pair, read its HUD link as a raw 68K address
 * (`(int)_DAT_ffffc43c`) and dropped the BCD adjust, so player 2's award (0xFFE084) never
 * landed (2026-10-04, demo A tick 0xC1: arcade 0x00001000, port 0). */
uint FUN_0000f440()
{  int unaff_A6 = (int)_r_unaff_A6;
  /* 0xF440..0xF484: for each live player whose +0x6b == +0x6a, the box test (0xF486) then
   * the attack-box test (0xF4D0), both with A4 = the player; Ghidra inlined player 2's. */
  if (((DAT_fffff028 & 1) != 0) && (LD32S((unaff_A6 + 0x30)) != 0) && (DAT_ffffc06b == DAT_ffffc06a)) {
    FUN_0000f486(0xffffc000);
    FUN_0000f4d0(0xffffc000);
  }
  if (((DAT_fffff029 & 1) != 0) && (LD32S((unaff_A6 + 0x30)) != 0) && (DAT_ffffc46b == DAT_ffffc46a)) {
    FUN_0000f486(0xffffc400);
    FUN_0000f4d0(0xffffc400);
  }
  return 0;
}
