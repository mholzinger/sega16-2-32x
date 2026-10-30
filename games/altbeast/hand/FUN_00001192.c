/* FUN_00001192 -- take a credit for the player whose state byte A0 points at
 * (0xFFF028 = P1, 0xFFF029 = P2), from the 68K listing. The result is the CARRY:
 *
 *   1192: moveb 0xfffff01a,%d0 ; beqs 11d2      coinage DIP 0 = free play: straight to success
 *   1198: tstb 0xfffff000 ; beqw 11ea           no credits -> fail
 *   11a0: btst #4,%a0@ ; bnes 11c0              the continue window is open -> one credit
 *   11a6: moveb 0xfffff01b,%d0 ; notb ; andib #1 ; beqs 11c0
 *   11b2: cmpib #2,0xfffff000 ; bltw 11ea       a two-credit start with one credit -> fail
 *   11bc: subqb #1,0xfffff000
 *   11c0: subqb #1,0xfffff000
 *   11c4: cmpib #9,0xfffff000 ; bcss 11d2 ; moveb #9,0xfffff000
 *   11d2: moveq #0,%d0 ; clear f009 f00a f00d f00e
 *   11e4: movew #1,%ccr ; rts                   SUCCESS: carry set, D0 = 0
 *   11ea: movew #0,%ccr ; rts                   FAIL: carry clear
 *
 * Both callers test exactly that carry: 0xFA6 `bsr 0x1192 ; bcc 0x1028` (P1, FUN_00000ece)
 * and 0x10FC `bsr 0x1192 ; bcc 0x10a2` (P2). Ghidra could not see a CCR result, returned
 * D0 (the DIP byte, or `~f01b & 1`, or 0) and let the callers test a stale `bVar4` it had
 * set false before the call, so on the port the credit was never taken and the start
 * never ran: Mike on the rig (2026-10-22, sw292): "when the player loses the last life the
 * game can't pick up the credits and start again". The return value is now the carry
 * (1 = taken) and cport_prep's credit-carry pass makes the two callers test it. */
uint FUN_00001192(byte *p_A0)
{
  p_A0 = (byte *)A68PTR(p_A0);
  if (DAT_fffff01a != 0) {
    if (DAT_fffff000 == 0) {
      return 0;                                            /* 0x119C beq 0x11EA: carry clear */
    }
    if (((*p_A0 & 0x10) == 0) && ((~DAT_fffff01b & 1) != 0)) {
      if ((char)DAT_fffff000 < '\x02') {
        return 0;                                          /* 0x11B8 blt 0x11EA: carry clear */
      }
      DAT_fffff000 = DAT_fffff000 - 1;
    }
    DAT_fffff000 = DAT_fffff000 - 1;
    if (8 < DAT_fffff000) {
      DAT_fffff000 = 9;
    }
  }
  DAT_fffff009 = 0;
  DAT_fffff00a = 0;
  DAT_fffff00d = 0;
  DAT_fffff00e = 0;
  return 1;                                                /* 0x11E4 move #1,CCR: carry set */
}
