/* FUN_00005f16 -- THE SEGMENTED ENEMY BUILDER (0x5F16). Hand-derived: it installs SIX
 * records and the generated body installed them all on ONE, then returned without
 * telling its caller where A6 had got to.
 *
 * THE 68K, 0x5F16-0x5FA6:
 *     ... fill the HEAD at (fp), handler 0x6038 ...
 *     movem.w d6-d7,-(sp) ; jsr 0x3B2E ; movem.w (sp)+,d6-d7      (0x5F7E-0x5F88)
 *     lea (0x80,fp),fp                                             (0x5F8C)  SLOT 2
 *     moveq #4,d0
 * .lp movem.w d0/d6-d7,-(sp) ; bsr 0x5FA8 ; movem.w (sp)+,d0/d6-d7 (0x5F92-0x5F98)
 *     lea (0x80,fp),fp                                             (0x5F9C)
 *     addq.w #1,d7 ; dbf d0,.lp                                    (0x5FA0-0x5FA2)
 *     rts                                                          (0x5FA6)
 *
 * so head + five segments, and it RETURNS with fp = entry + 6 * 0x80.
 *
 * THREE THINGS THE DECOMPILE LOST:
 *  1. BOTH `lea (0x80,fp),fp` are dropped. unaff_A6 is read once and never advanced,
 *     so every segment was written over the head's record -- and FUN_00005fa8 reaches
 *     its record through the GLOBAL _r_unaff_A6, which never moved either.
 *  2. The `movem.w` pair became a walking stack pointer (`puVar2 = puVar2 + -3`, stores
 *     at [-2]/[-4]/-6/-8 and reads at [-5]/-4), the same mismodelling as
 *     hand/FUN_00016f06.c. D0/D6/D7 are restored by the 68K, so they are locals here.
 *  3. A6 is not written back, so the caller cannot continue from it.
 *
 * WHAT IT COST (advisor, Mike's sw269 state): FUN_00005efc builds 0x5F16 / 0x60E6 /
 * 0x5F16 from 0xFFD980 with NO lea between them -- each callee leaves fp where the next
 * one starts. The release object 0x60E6 belongs in slot 57 and handler 0x6168 is the
 * ONLY writer of 0xFFF14B (`move.b #1,0xfffff14b` at 0x61AA). The player in 0x59D2
 * waits on it, read 0, and the round clear after Neff hangs.
 */
uint FUN_00005f16(undefined4 p_D6,undefined4 p_D7)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;
  byte *_a6entry = _r_unaff_A6;
  /* D0/D6/D7 as the 68K holds them: the movem.w pairs restore both, so the callee
   * cannot change them and they belong in locals. */
  ushort d6 = *(ushort *)((byte *)&p_D6 + 2);
  ushort d7 = *(ushort *)((byte *)&p_D7 + 2);
  short  d0;

  /* ---- the HEAD record, handler 0x6038 ---- */
  *unaff_A6 = 0x8000;
  ST32((unaff_A6 + 1), 0x6038u);
  *(undefined *)((int)unaff_A6 + 0xb) =
      *(undefined *)((short)(ushort)DAT_fffff142 + ((int)ROM_BASE + 0x73da));
  unaff_A6[0x20] = (ushort)(_DAT_fffff132 >> 0x10) + 0x100;
  unaff_A6[0x22] = 0x10e0;
  *(char *)((int)unaff_A6 + 0x2f) = (char)d6;
  *(undefined *)((int)unaff_A6 + 9) = 2;
  *(char *)(unaff_A6 + 0x10) = (char)d7;
  *(byte *)(unaff_A6 + 0x10) = *(byte *)(unaff_A6 + 0x10) | 0xf;
  ST32((unaff_A6 + 0x12), 0x7342u);
  ST32((unaff_A6 + 0x36), 0x736cu);
  *(undefined *)((int)unaff_A6 + 0x21) = 0;
  *(undefined *)(unaff_A6 + 0x27) = 0;
  *(undefined *)(unaff_A6 + 0x11) = 1;
  unaff_A6[0x1f] = 0x1e;
  _r_unaff_A6 = (byte *)unaff_A6;          /* 0x3B2E reaches its record through A6 */
  request_palette_update();

  unaff_A6 = unaff_A6 + 0x40;              /* lea (0x80,fp),fp -- 0x5F8C */

  /* ---- five segments, 0x5FA8 each ---- */
  d0 = 4;
  do {
    _r_unaff_A6 = (byte *)unaff_A6;        /* THIS segment's record, for 0x5FA8 */
    FUN_00005fa8((int)d6, (int)(short)d7);
    unaff_A6 = unaff_A6 + 0x40;            /* lea (0x80,fp),fp -- 0x5F9C */
    d7 = d7 + 1;                           /* addq.w #1,d7 */
    d0 = d0 - 1;                           /* dbf d0 */
  } while (d0 != -1);

  /* RETURN WITH A6 WHERE THE 68K LEFT IT: entry + 6 * 0x80. FUN_00005efc's next call
   * starts from exactly here -- there is no lea between its three bsr's. */
  _r_unaff_A6 = (byte *)unaff_A6;
  (void)_a6entry;
  return 0;
}
