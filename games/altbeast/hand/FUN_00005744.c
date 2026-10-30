/* FUN_00005744 -- THE PLAYER SPAWN (0x5744). Hand-derived because the one thing it does
 * that matters is adjust A6, and an A-register computed into a local never reaches the
 * callee's _r_unaff_A6 (the register-file gap). The generated body called FUN_00005776
 * twice with the right ARGUMENTS and the wrong A6, so 0x5776 -- which writes every field
 * through unaff_A6 and uses its arguments only as data -- initialised THE PARENT instead
 * of the child.
 *
 * WHAT THAT COST, measured by the advisor 2026-10-19 on sw256 against MAME: the arcade
 * spawns the player at parent+0x80 (0xFFDD80 -> 0xFFDE00) at frame 1196; that child
 * counts its +0x22 down and at 1443 its handler 0x5854 underflows and does
 * `move.w #30,0xFFF14C ; clr.w (a6)`, which is what ENDS THE INTRO (gameplay by ~1475).
 * Ours wrote 0x8000/0x57BC into the parent at 0xFFDD80, 0xFFDE00 stayed empty forever,
 * no player existed, nothing ever cut 0xFFF14C, and the intro ran its full 720 frames.
 * The intro timer itself was measured faithful -- 720, one tick a frame -- so this is the
 * cause and the timer was never the suspect.
 *
 * THE 68K, 0x5744-0x5770:
 *     move.l fp,-(sp)        save A6
 *     movea.l fp,a5
 *     lea 0x80(fp),fp        child 1 at A6 + 0x80
 *     lea 0xffffc000,a3 ; lea 0xffffd800,a4 ; bsr 0x5776
 *     cmpi.b #1,0xfffff029   two players?
 *     lea 0x80(fp),fp        child 2 at A6 + 0x100 -- a SECOND +0x80, not +0x80 again
 *     lea 0xffffc400,a3 ; lea 0xffffd880,a4 ; bsr 0x5776
 *     movea.l (sp)+,fp       restore A6
 *     moveq #68,d0 ; jmp 0x3352      the sound enqueue is a TAIL CALL
 * A5 is set to the original A6 and no callee here reads it, so it is not published. */
uint FUN_00005744()
{
  byte *_a6 = (byte *)_r_unaff_A6;          /* move.l fp,-(sp) */

  _r_unaff_A6 = _a6 + 0x80;                 /* lea 0x80(fp),fp */
  FUN_00005776(0xffffc000, (int)(&DAT_ffffd800));

  if (DAT_fffff029 == '\x01') {
    _r_unaff_A6 = _a6 + 0x100;              /* the second lea, cumulative */
    FUN_00005776(0xffffc400, (int)(&DAT_ffffd880));
  }

  _r_unaff_A6 = _a6;                        /* movea.l (sp)+,fp */
  return sound_enqueue(0x44);               /* moveq #68,d0 ; jmp 0x3352 */
}
