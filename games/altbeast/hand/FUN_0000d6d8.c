/* FUN_0000d6d8 -- an enemy's box-C test against every attacker (0xD6D8), generated
 * text with the loop counter the 68K really uses: `moveq #23,d0 ... dbf d0` over the 24
 * enemy records, and the callee (0xD73C) overwrites D0.b with the attacker's power on a
 * hit, so the loop then runs `power` more records. Ghidra wrote `sVar1 = FUN_0000d73c()`,
 * which in the port returned 0 and ended the loop at the first attacker-capable record,
 * hit or not. */
uint FUN_0000d6d8()
{
  short sVar1;
  undefined *p_A4;
  
  if (((DAT_fffff028 & 1) != 0) && (cRamffffc049 != '\0')) {
    FUN_0000d73c(0xffffc000);
  }
  if (((DAT_fffff029 & 1) != 0) && (cRamffffc449 != '\0')) {
    FUN_0000d73c(0xffffc400);
  }
  p_A4 = &DAT_ffffc800;
  sVar1 = 0x17;
  do {
    if (p_A4[0x49] != '\0') {
      uint _r = FUN_0000d73c((int)(p_A4));   /* 0x100|power on a hit: D0 becomes the dbf counter */
      if (_r & 0x100u) sVar1 = (short)(_r & 0xffu);
    }
    p_A4 = p_A4 + 0x80;
    sVar1 = sVar1 + -1;
  } while (sVar1 != -1);
  if ((_DAT_ffffc080 != 0) && (cRamffffc0c9 != '\0')) {
    FUN_0000d73c((int)(&DAT_ffffc080));
  }
  if ((_DAT_ffffc480 != 0) && (cRamffffc4c9 != '\0')) {
    FUN_0000d73c((int)(&DAT_ffffc480));
  }
  return 0;
}
