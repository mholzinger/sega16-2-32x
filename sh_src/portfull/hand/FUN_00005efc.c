/* FUN_00005efc -- THE ROUND-CLEAR OBJECT BUILDER (0x5EFC). Hand-derived because its
 * three calls SHARE ONE A6 and the generated body reset it between them.
 *
 * THE 68K, 0x5EFC-0x5F14 -- note there is NO lea between the calls:
 *     move.l fp,-(sp)
 *     lea 0xffffd980,fp          slot 51
 *     moveq #0,d6 ; moveq #0,d7
 *     bsr 0x5F16                 head + 5 segments, returns fp = +6*0x80 (0xFFDC80)
 *     bsr 0x60E6                 THE RELEASE OBJECT, returns fp = +0x80  (0xFFDD00)
 *     moveq #3,d6 ; moveq #-16,d7
 *     bsr 0x5F16                 six more
 *     movea.l (sp)+,fp
 *     rts
 *
 * Each callee leaves fp where the next one starts. The a6_callsites wrapper gave the
 * FIRST call its 0xFFD980 and then RESTORED the caller's A6 (that is what the wrapper
 * is for -- a dropped `lea` at one call site), so 0x60E6 and the second 0x5F16 ran on
 * whatever A6 the caller had. The release object never existed.
 *
 * It installs handler 0x6168, the ONLY writer of 0xFFF14B (`move.b #1,0xfffff14b` at
 * 0x61AA). The player in 0x59D2 waits on that byte, so the round clear after Neff hung
 * (advisor, Mike's sw269 state: slot 0 reads 0xFFF14B == 0, no slot runs 0x6168).
 *
 * The callees now hand A6 on: hand/FUN_00005f16.c writes it back, and FUN_000060e6's
 * trailing `lea (0x80,fp),fp ; rts` (0x6162, and 0x62B0 on its other path) is restored
 * by cport_prep's A6-writeback pass.
 */
uint FUN_00005efc()
{
  byte *_a6sv = _r_unaff_A6;               /* move.l fp,-(sp) */
  _r_unaff_A6 = (byte *)W68PTR(0xFFD980u); /* lea 0xffffd980,fp */
  FUN_00005f16(0, 0);
  FUN_000060e6();
  FUN_00005f16(3, 0xfffffff0);
  _r_unaff_A6 = _a6sv;                     /* movea.l (sp)+,fp */
  return 0;
}
