/* FUN_00005776 -- THE PLAYER/CHILD INITIALISER (0x5776). Hand-derived for ONE line:
 * field +0x3C must hold a 68K ADDRESS and the generated body stored the low word of an
 * SH-2 POINTER there.
 *
 * The parameter arrives in both domains from different call sites, so the generator
 * inserts `p_A4 = (int)A68NORM(p_A4)` to make it an SH-2 pointer. Every later use wants
 * that -- `*(undefined2 *)(p_A4 + 0xc)` is a real dereference -- EXCEPT the word store
 * into +0x3C, which the game later feeds to W68 as a 68K address. Truncating the SH-2
 * pointer gave 0x0602E400 -> 0xE400, so the player's +0x3C named 0xFFE400 where the
 * arcade names 0xFFD800.
 *
 * WHAT IT COST, measured by the advisor 2026-10-19 (sw257, ares against MAME): the
 * player's 0x57BC handler writes 4 into byte +0x4B of the object its +0x3C names, every
 * 8 frames; the object at 0xFFD800 (handler 0xDABC) subtracts +0x4B from +0x4A, and when
 * its +0x4A reaches 0 (~frame 1330 on the arcade) it despawns, which is what lets the
 * player's `tst.b 74(a0)` move on to 0x5854 and set 0xFFF14C = 30 -- THE END OF THE
 * INTRO. Our damage went to 0xFFE400 instead, D800's +0x4A sat at 0x40 forever, and the
 * intro ran its full 720 frames.
 *
 * A68OF16 (w68.h:79) is A68NORM's exact mirror, domain-guarded, so a parameter that
 * arrived as a 68K address in the first place is returned unchanged.
 *
 * THE SHAPE IS WORTH A CENSUS, not just this site: a pointer parameter NORMALISED at
 * entry and later TRUNCATED to a word store into a 68K field. The normalisation is
 * inserted before the store, so a pass that looks for `(short)p_Ax` on an un-normalised
 * parameter will not see it. */
uint FUN_00005776(undefined4 p_A3,int p_A4)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A4 = (int)A68NORM(p_A4);
  *unaff_A6 = 0x8000;
  ST32((unaff_A6 + 1), 0x57bcu);
  unaff_A6[3] = 0x108;
  *(undefined *)((int)unaff_A6 + 0xb) = 0x49;
  unaff_A6[6] = *(undefined2 *)(p_A4 + 0xc);
  unaff_A6[8] = 0x105c;
  *(undefined *)((int)unaff_A6 + 0x2f) = 0;
  *(undefined *)((int)unaff_A6 + 9) = 2;
  unaff_A6[0x11] = 0xffff;
  unaff_A6[0x1e] = (short)A68OF16(p_A4);   /* +0x3C is a 68K address, not a pointer */
  ST32((unaff_A6 + 0x36), (p_A3));
  request_palette_update();
  return 0;
}
