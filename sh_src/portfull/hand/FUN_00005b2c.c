/* FUN_00005b2c -- THE ROUND-CLEAR POWER LOSS (0x5B2C): the flame around the player and one
 * flying orb per power level. Hand-derived because A5 is a register the callees ADVANCE.
 *
 * 0x5B2C `movea.l a6,a5`, then 0x5C64 (the flame) and each 0x5B72 (an orb) begin with
 * `lea 0x80(a5),a5` and fill the record at the NEW A5, so the four calls fill A6+0x80
 * (flame, handler 0x5CFE), A6+0x100, A6+0x180, A6+0x200 (orbs, handler 0x5C02), one orb
 * per `subq.b #1,0x6a(a6) ; bcs` that does not borrow. Ghidra passed unaff_A6 to every
 * call, so all three orbs were written over slot 1: the flame was overwritten by the
 * first orb and the other orbs landed on the same record (Mike's sw319 pass: no blue
 * flame, ONE orb instead of three). Arcade, level1_full tape, power 2: frame 34221
 * (tick 0x8200) slot 1 = 0x5CFE, slots 2 and 3 = 0x5C02. */
uint FUN_00005b2c()
{
  undefined2 extraout_D1u = (undefined2)_r_extraout_D1u;
  undefined2 extraout_D1u_00 = (undefined2)_r_extraout_D1u_00;
  undefined2 extraout_D1u_01 = (undefined2)_r_extraout_D1u_01;
  int unaff_A6 = (int)_r_unaff_A6;
  char cVar1;
  undefined4 p_A4;

  FUN_00005c64(unaff_A6);                    /* fills A6+0x80; A5 = A6+0x80 */
  p_A4 = 0x060264D4u;
  if (DAT_fffff109 != '\0') {
    p_A4 = 0x060264F0u;
  }
  cVar1 = *(char *)(unaff_A6 + 0x6a);
  *(char *)(unaff_A6 + 0x6a) = cVar1 + -1;
  if (cVar1 != '\0') {
    FUN_00005b72(p_A4,unaff_A6 + 0x80,CONCAT22(extraout_D1u,0x3c));      /* fills A6+0x100 */
    cVar1 = *(char *)(unaff_A6 + 0x6a);
    *(char *)(unaff_A6 + 0x6a) = cVar1 + -1;
    if (cVar1 != '\0') {
      FUN_00005b72(p_A4,unaff_A6 + 0x100,CONCAT22(extraout_D1u_00,0x50)); /* fills A6+0x180 */
      cVar1 = *(char *)(unaff_A6 + 0x6a);
      *(char *)(unaff_A6 + 0x6a) = cVar1 + -1;
      if (cVar1 != '\0') {
        FUN_00005b72(p_A4,unaff_A6 + 0x180,CONCAT22(extraout_D1u_01,100)); /* fills A6+0x200 */
      }
    }
  }
  return 0;
}
