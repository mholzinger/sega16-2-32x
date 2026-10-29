/* FUN_0000f486 -- ball collection for the player at A4, generated text with the award restored:
 * the 68K ends `movea.w ($3c,a4),a5 ; move.l #$3000,0xFFF032 ; jmp 0x3706` (a packed-BCD
 * add of 0xFFF032 into the player's HUD delta at A5+4). Ghidra inlined 0x3706 as four byte
 * adds whose bcdAdjust() results it discarded, so the add was binary and carried nothing;
 * the port's hand FUN_00003706 does the BCD. */
uint FUN_0000f486(int p_A4)
{
  int unaff_A6 = (int)_r_unaff_A6;
  undefined *puVar1;
  char *pcVar2;
  int iVar3;
  

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A4 = (int)A68NORM(p_A4);
  if (((((*(byte *)(unaff_A6 + 0x4b) & 1) == 0) && (LD32S((p_A4 + 0x30)) != 0)) &&
      (*(ushort *)(unaff_A6 + 0x34) <= *(ushort *)(p_A4 + 0x36))) &&
     (((*(ushort *)(p_A4 + 0x34) <= *(ushort *)(unaff_A6 + 0x36) &&
       (*(ushort *)(unaff_A6 + 0x38) <= *(ushort *)(p_A4 + 0x3a))) &&
      (*(ushort *)(p_A4 + 0x38) <= *(ushort *)(unaff_A6 + 0x3a))))) {
    *(undefined *)(unaff_A6 + 0x4b) = 1;
    if (*(char *)(p_A4 + 0x6b) == '\x03') {
      *(undefined *)(unaff_A6 + 0x4b) = 0xff;
      _DAT_fffff032 = 0x3000;
      FUN_00003706((int)(short)*(short *)(p_A4 + 0x3c));   /* 0xF520 movea.w ($3c,a4),a5 ; 0xF52C jmp 3706: the BCD add of 0xFFF032 into the player's HUD delta (+4), hand/FUN_00003706.c */
      return 0;
    }
    *(char *)(p_A4 + 0x6b) = *(char *)(p_A4 + 0x6b) + '\x01';
  }
  return 0;
}
