/* FUN_00012b08 -- kind 7's init (the level-2 four-part enemy, handler 0x12CBA), from the
 * 68K listing. A chain of THREE free-slot searches, each moving FP to the record it found:
 *
 *   12b08: movew #$8000,(fp)              the head is live
 *   12b0e: jsr e032 ; bccs 12b02          no free record -> clr.w (fp) ; rts
 *   12b16: movew a5,$78(fp)               head -> part 2
 *   12b1a: movew #$8000,(a5) ; movew fp,$3c(a5) ; movew $20(fp),$20(a5)
 *   12b2a: movew fp,-(sp) ; moveaw a5,fp  FP = PART 2
 *   12b2e: jsr e032 ; bccs 12afc          fail: clr.w (fp) ; fp = (sp)+ ; clr.w (fp) ; rts
 *   12b36: movew a5,$78(fp)               part 2 -> part 3, the same four stores
 *   12b4a: movew fp,-(sp) ; moveaw a5,fp  FP = PART 3
 *   12b4e: jsr e032 ; bccs 12af6          fail: clears part 3, part 2, head ; rts
 *   12b56: movew a5,$78(fp)               part 3 -> part 4, the same stores, then
 *   12b6a: movew #-1,$78(a5)              part 4 ends the chain
 *   12b70: addqw #2,sp ; moveaw (sp)+,fp  drop part 2, FP = THE HEAD again
 *   12b74..12bda: the head: d1/+0x1a/a2 by bit 7 of +0x20, +0x44 = +0x10 = d1,
 *          bsr 12c70 (template 0x13006, script a2), +0x50 = $f808d700, jsr d4b8,
 *          clr +0x1c +0x1e, +0x4a = 8 (0x10 and +0xb = 0x70 when bit 4), jsr 3b2e
 *   12be0: moveaw $78(fp),fp              part 2: template 0x13012, script by bit 7, jsr 3b2e
 *   12c10: moveaw $78(fp),fp              part 3: template 0x1301e, ...
 *   12c40: moveaw $78(fp),fp              part 4: template 0x1302a, ..., jmp 3b2e
 *
 * WHY A HAND FILE. Ghidra held A5 at the FIRST record for all three searches, so the
 * decompile wrote part 2's link to part 2 itself (`fp[$78] = fp`), and with the A5
 * re-read pass applied (46a0ed46) that self-link froze the attract in the level-2 demo
 * (measured 2026-10-22: tick stuck at 242 from frame 3000 with this function allowlisted
 * alone; every chain walk along +0x78 spins). Without the pass the three searches all
 * "found" part 2, two records were never installed, and the enemy count 0xFFF15D leaked
 * to the cap: level 2 then spawns nothing (Mike on the rig, sw294: "no enemies after
 * level 1"). It also passed the template addresses 0x13006..0x1302A to FUN_00012c70 as
 * bare SH-2 pointers; they are ROM, so they go through ROM_BASE here.
 *
 * FP on return is part 4 (success) or the head (failure), as on the 68K: the spawner
 * tests `(fp)` after the jsr (0xE0C8) to decide whether to give the count back. */
/* 0x12C70, inlined: copy the 12-byte template at ROM a1 into +0..+0xB (keeping +8), seed
 * the fields, store the script pointer a2 at +0x24, set bit 3 of +0. Byte-wise because the
 * templates sit at 0x13006/0x13012/0x1301E/0x1302A, 2 mod 4: a 32-bit read there is an
 * SH-2 address error, and the first build of this file (lv2_all) read them two bytes early. */
static void kind7_template(byte *fp, unsigned a1, unsigned a2)
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];
  fp[8] = keep;
  fp[0x2f] = 2;
  fp[0x2e] = 0x80;
  fp[0x22] = 1;
  fp[0x21] = 0;
  fp[0x4e] = 0; fp[0x4f] = 0;
  fp[0x7f] = 0;
  fp[0x14] = 0; fp[0x15] = 0;
  fp[0x23] = 0;
  ST32((fp + 0x24), (a2));
  fp[0] |= 8;
}

uint FUN_00012b08(undefined2 *p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *p2, *p3, *p4, *fp;
  unsigned a2;
  (void)p_A5;

  *(ushort *)head = 0x8000;                                /* 12b08 */
  if (!FUN_0000e032()) {                                   /* 12b14 bccs 12b02 */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p2 = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(p2);            /* 12b16 */
  *(ushort *)p2 = 0x8000;
  *(short *)(p2 + 0x3c) = (short)A68OF16(head);
  *(short *)(p2 + 0x20) = *(short *)(head + 0x20);
  if (!FUN_0000e032()) {                                   /* 12b34 bccs 12afc */
    *(ushort *)p2 = 0;
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p3 = _r_unaff_A5;
  *(short *)(p2 + 0x78) = (short)A68OF16(p3);              /* 12b36 */
  *(ushort *)p3 = 0x8000;
  *(short *)(p3 + 0x3c) = (short)A68OF16(p2);
  *(short *)(p3 + 0x20) = *(short *)(p2 + 0x20);
  if (!FUN_0000e032()) {                                   /* 12b54 bccs 12af6 */
    *(ushort *)p3 = 0;
    *(ushort *)p2 = 0;
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p4 = _r_unaff_A5;
  *(short *)(p3 + 0x78) = (short)A68OF16(p4);              /* 12b56 */
  *(ushort *)p4 = 0x8000;
  *(short *)(p4 + 0x3c) = (short)A68OF16(p3);
  *(short *)(p4 + 0x20) = *(short *)(p3 + 0x20);
  *(short *)(p4 + 0x78) = -1;                              /* 12b6a */

  /* 12b74: the head */
  fp = head;
  _r_unaff_A6 = fp;
  if (fp[0x20] & 0x80) {
    *(short *)(fp + 0x1a) = 0x0300;
    *(ushort *)(fp + 0x44) = 0x1020;
    *(ushort *)(fp + 0x10) = 0x1020;
    a2 = 0x131acu;
  } else {
    *(short *)(fp + 0x1a) = (short)0xfd00;
    *(ushort *)(fp + 0x44) = 0x1100;
    *(ushort *)(fp + 0x10) = 0x1100;
    a2 = 0x13126u;
  }
  kind7_template(fp, 0x13006u, a2);   /* 12ba6 bsr 12c70 */
  ST32((fp + 0x50), (0xf808d700u));                        /* 12baa */
  hitbox_b_expand();                                       /* 12bb2 jsr d4b8 */
  *(short *)(fp + 0x1c) = 0;
  *(short *)(fp + 0x1e) = 0;
  fp[0x4a] = 8;
  if (fp[0x20] & 0x10) {
    fp[0x4a] = 0x10;
    fp[0xb] = 0x70;
  }
  request_palette_update();                                /* 12bda jsr 3b2e */

  /* 12be0: part 2 */
  fp = p2;
  _r_unaff_A6 = fp;
  a2 = (fp[0x20] & 0x80) ? 0x130aeu : 0x13096u;
  kind7_template(fp, 0x13012u, a2);
  if (fp[0x20] & 0x10) fp[0xb] = 0x70;
  request_palette_update();                                /* 12c0a */

  /* 12c10: part 3 */
  fp = p3;
  _r_unaff_A6 = fp;
  a2 = (fp[0x20] & 0x80) ? 0x130deu : 0x130c6u;
  kind7_template(fp, 0x1301eu, a2);
  if (fp[0x20] & 0x10) fp[0xb] = 0x70;
  request_palette_update();                                /* 12c3a */

  /* 12c40: part 4 */
  fp = p4;
  _r_unaff_A6 = fp;
  a2 = (fp[0x20] & 0x80) ? 0x1310eu : 0x130f6u;
  kind7_template(fp, 0x1302au, a2);
  if (fp[0x20] & 0x10) fp[0xb] = 0x70;
  request_palette_update();                                /* 12c6a jmp 3b2e */
  _r_unaff_A6 = p4;                                        /* FP = part 4 on return */
  return 0;
}
