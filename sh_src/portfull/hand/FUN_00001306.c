/* SIGNATURE MATCHES protos.h:27, 2026-10-11. This file declared `void FUN_00001306(void)`
 * while protos.h and therefore every generated caller use `uint FUN_00001306()`. cport_prep
 * refuses the mismatch -- but only on a CLEAN port build, so it never fired: no clean port
 * build has succeeded since d696b228 introduced this file, and every rom since has come from
 * an incremental build reusing build/port. Found 2026-10-11 by deleting build/port to test
 * whether a reference rom was reproducible; it was not, and neither was HEAD. */
uint FUN_00001306()

{
  short _i;
  undefined4 *_p = (undefined4 *)&DAT_ffffe000;
  /* volatile: gcc, like Ghidra, does not see the pointer loop aliasing the two
     globals and would fold the restore away as a no-op store. */
  byte _lives = *(volatile byte *)&DAT_ffffe018;
  byte _e098 = *(volatile byte *)&DAT_ffffe098;
  for (_i = 0; _i < 0x40; _i++) ((volatile undefined4 *)_p)[_i] = 0;
  *(volatile byte *)&DAT_ffffe018 = _lives;
  *(volatile byte *)&DAT_ffffe098 = _e098;
  _DAT_ffffe008 = (undefined4)A68VIDA(0x4100b4u);   /* 0x1326 move.l #0x4100B4 (text RAM) */
  _DAT_ffffe088 = (undefined4)A68VIDA(0x4100ecu);
  return 0;
}
