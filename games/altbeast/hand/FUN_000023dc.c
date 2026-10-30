/* FUN_000023dc -- the high-score step's object handler, from the 68K listing.
 *
 * WHY IT IS A HAND FILE. The script table at 0x2682 is read with four `movew %a0@+` off one
 * walked pointer, and the decompiler lost the walk on the last two:
 *     0x23FA  movew %a0@+,%fp@(34)    +0x22 <- tab[0]   decompile: &DAT_00002682 + idx
 *     0x2408  movew %a0@+,%fp@(20)    +0x14 <- tab[2]   decompile: &DAT_00002684 + idx
 *     0x2412  movew %a0@+,%fp@(22)    +0x16 <- tab[4]   decompile: (undefined *)0x2686u
 *     0x2416  movew %a0@+,%fp@(26)    +0x1A <- tab[6]   decompile: &LAB_00002686_2
 * The third reads through a BARE 68K CONSTANT, which on the SH-2 is an address in low
 * memory and not the table at all; the fourth reads 0x2686 AGAIN where the 68K reads
 * 0x2688. So this object's dx and dy came from garbage and from a duplicate.
 *
 * MEASURED, 2026-10-07, arcade frame 1170 (game frame 1160) against port frame 1220 (game
 * frame 1169): slot 1 held +0x0E = 0x30, +0x15 = 0x18 and +0x17 = 0x08 where the arcade
 * holds zero in all three -- the low halves of x and dx, i.e. a velocity fraction the
 * arcade never has, which is what a wrong dx integrates into. The arcade's own dumps show
 * those three bytes zero at frames 1166, 1167, 1168 and 1170, so nothing clears them: the
 * port was writing them.
 *
 * One table, one base, the listing's own offsets. */
uint FUN_000023dc()
{
  int unaff_A6 = (int)_r_unaff_A6;

  if (*(short *)(unaff_A6 + 0x22) == 0) {                  /* 0x23DC tstw %fp@(34) */
    byte bIdx = *(byte *)(unaff_A6 + 0x21);                /* 0x23EE moveb %fp@(33),%d0 */
    short sOff;
    const byte *pbTab;
    short sDelay;
    *(char *)(unaff_A6 + 0x21) = (char)(bIdx + 1);         /* 0x23F2 addqb #1,%fp@(33) */
    sOff = (short)((ushort)bIdx << 3);                     /* 0x23F6 lslw #3,%d0 */
    pbTab = (const byte *)&DAT_00002682 + sOff;            /* 0x23E8 lea 0x2682,%a0 ; addaw */
    sDelay = *(const short *)(pbTab + 0);                  /* 0x23FA movew %a0@+,%fp@(34) */
    *(short *)(unaff_A6 + 0x22) = sDelay;
    if (sDelay < 0) {                                      /* 0x23FE bpls */
      *(undefined *)(unaff_A6 + 0x20) = 0xff;              /* 0x2400 moveb #-1,%fp@(32) */
      return 0;
    }
    *(undefined2 *)(unaff_A6 + 0x14) = *(const undefined2 *)(pbTab + 2);   /* 0x2408 */
    *(undefined2 *)(unaff_A6 + 0x18) = 0x300;                              /* 0x240C */
    *(undefined2 *)(unaff_A6 + 0x16) = *(const undefined2 *)(pbTab + 4);   /* 0x2412 */
    *(undefined2 *)(unaff_A6 + 0x1a) = *(const undefined2 *)(pbTab + 6);   /* 0x2416 */
    *(short *)(unaff_A6 + 0x1e) = sOff;                                    /* 0x241A */
  }
  else {
    *(short *)(unaff_A6 + 0x22) = *(short *)(unaff_A6 + 0x22) + -1;        /* 0x23E2 */
  }
  clamp_x_velocity();                                      /* 0x241E jsr 0x3F40 */
  integrate_position();                                    /* 0x2424 jsr 0x3F20 */
  {
    undefined2 uSave = *(undefined2 *)(unaff_A6 + 0x14);   /* 0x242A movew %fp@(20),%sp@- */
    *(undefined2 *)(unaff_A6 + 0x14) = *(undefined2 *)(unaff_A6 + -0x6c);  /* 0x242E */
    FUN_00003f24();                                        /* 0x2434 */
    *(undefined2 *)(unaff_A6 + 0x14) = uSave;              /* 0x243A movew %sp@+,%fp@(20) */
  }
  _DAT_fffff124 = -((short)((uint)LD32U((unaff_A6 + 0xc)) >> 0x10) + -0x1000);  /* 0x243E */
  {
    ushort uVar4 = _DAT_fffff0e0 + _DAT_fffff124 + 0xc0;   /* 0x2452 / 0x2456 */
    _DAT_fffff0e4 = uVar4 & 0x1ff;                         /* 0x245C / 0x2460 */
    _DAT_fffff0f6 = *(undefined2 *)(&DAT_00002724 + (short)(uVar4 >> 8 & 0xe));  /* 0x246A */
  }
  _DAT_fffff12c = *(short *)(unaff_A6 + 0x10) + -0x1000;   /* 0x2472 / 0x247A */
  _DAT_fffff0ea = _DAT_fffff12c + _DAT_fffff0e6 & 0xff;    /* 0x247E / 0x248A */
  return 0;
}
