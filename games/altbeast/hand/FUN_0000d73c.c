/* FUN_0000d73c -- box-C collision of A6 against attacker A4 (0xD73C), generated text
 * plus the D0 the 68K leaves behind: on a hit `move.b ($49,a4),d0` (0xD764) loads the
 * power into D0, and FUN_0000d6d8's `dbf d0` loop over the 24 enemy records counts that
 * register, so a hit re-arms the loop to `power` more records. Returns 0x100|power on
 * a hit, 0 otherwise; see hand/FUN_0000d6d8.c. */
uint FUN_0000d73c(int p_A4)
{
  int unaff_A6 = (int)_r_unaff_A6;
  byte bVar1;
  byte bVar2;
  

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A4 = (int)A68NORM(p_A4);
  if ((((*(ushort *)(unaff_A6 + 0x54) <= *(ushort *)(p_A4 + 0x62)) &&
       (*(ushort *)(p_A4 + 0x60) <= *(ushort *)(unaff_A6 + 0x56))) &&
      (*(ushort *)(unaff_A6 + 0x58) <= *(ushort *)(p_A4 + 0x66))) &&
     ((*(ushort *)(p_A4 + 100) <= *(ushort *)(unaff_A6 + 0x5a) &&
      (bVar1 = *(byte *)(p_A4 + 0x49), bVar2 = *(byte *)(unaff_A6 + 0x4b),
      *(byte *)(unaff_A6 + 0x4b) = bVar1 + bVar2, CARRY1(bVar1,bVar2))))) {
    *(undefined *)(unaff_A6 + 0x4b) = 0xff;
  }
  /* 0xD764 move.b ($49,a4),d0 runs only on a hit: D0 = the attacker's power, and the
   * caller's dbf counts THAT register. Returned as 0x100|power; no hit returns 0. */
  if (*(ushort *)(unaff_A6 + 0x54) <= *(ushort *)(p_A4 + 0x62) && *(ushort *)(p_A4 + 0x60) <= *(ushort *)(unaff_A6 + 0x56)
      && *(ushort *)(unaff_A6 + 0x58) <= *(ushort *)(p_A4 + 0x66) && *(ushort *)(p_A4 + 100) <= *(ushort *)(unaff_A6 + 0x5a))
    return 0x100u | *(byte *)(p_A4 + 0x49);
  return 0;
}
