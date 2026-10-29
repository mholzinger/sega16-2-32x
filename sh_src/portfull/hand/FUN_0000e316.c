/* FUN_0000e316 -- kind 5's init (the level-1 three-record enemy: head 0xE474 + two 0xEA5E
 * parts, templates 0xEBA2/0xEBAE/0xEBBA), from the 68K listing 0xE2F0-0xE472. The kind
 * table 0x1D2EA[5] points at 0xE316 directly.
 *
 *   e316: d0 = 0xFFF15D + (bit 1 of +0x20 ? 3 : 4) ; cmpib #11 ; bccs e310   cap: clr.w (fp), rts
 *   e332: 0xFFF15D = d0 ; jsr e032 ; bccs e30e              count back (e2f0), clr.w, rts
 *   e33e: movew a5,$78(fp) ; movew #$8000,(a5) ; moveb $20(fp),$20(a5) ; push fp ; fp = a5
 *   e352: jsr e032 ; bccs e308                              clr.w (part 2) ; fp = (sp)+ ; e30e
 *   e35a: movew a5,$78(fp) ; movew fp,$7a(a5) ; moveb $20(fp),$20(a5) ; moveaw (sp)+,fp
 *   e36a: the head: a1 = 0xEBA2, a2 = 0xEBC6 ; +0x40 = +0xC ; +0x44 = +0x10 ;
 *         +0x7B = bit 0 of +0x20 ? 4 : 2 ; bsr e42e ; +0x4A = 0x10, or 0x20 and +0xB = 0x69
 *         when bit 1 of +0x20 ; jsr 3b2e ; +0x50 = $F010EE17 ; jsr d4b8 ; +0x1E = 0x800 ; +0x3E = 0
 *   e3c8: push fp ; fp = $78(fp)   part 2: a1 = 0xEBAE, a2 = 0xED3C ; bsr e42e ;
 *         bit 1 -> +0xB = 0x68 ; jsr 3b2e ; clr.b +0x7F ; +0x3C = (sp) the head
 *   e3f4: fp = $78(fp)             part 3: a1 = 0xEBBA, a2 = 0xED8C ; bsr e42e ;
 *         bit 1 -> +0xB = 0x68 ; jsr 3b2e ; +0x3C = (sp)+ the head ; +0x78 = -1 ;
 *         clr.b +0x7F ; d0 = 0xB2 ; jmp 3352
 *
 * WHY A HAND FILE (2026-10-23). The decompile held A5 at part 2 for the second search and
 * wrote `puVar7[0x3c] = sVar4` -- part 2's +0x78 = PART 2. The port's object came up as a
 * head linked to a part linked to itself; the part's handler 0xEA5E killed it on its first
 * frame (slot 24 cleared at tape frame 18993, sw303g) and the head lived on with a +0x78
 * pointing at a dead record. The per-tick despawn walk 0xE16A diverts through that link,
 * never returns to slots 18-23, and runs off the object table through the sprite list and
 * the globals; on the 68K that ends in ROM and does nothing, on the SH-2 it stepped into
 * .ramtext and rewrote r60_line_for (the sw303g tick-0x46A5 freeze, found with the SDRAM
 * write watch). The parity census had the symptom all along: e474/ea5e "arcade only" in
 * level 1. Same family as hand/FUN_000132a0.c and hand/FUN_00014a8c.c. */
static void kind5_template(byte *fp, unsigned a1, unsigned a2)      /* 0xE42E */
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];                 /* 3 longs, (a1)+ -> (a0)+ */
  fp[8] = keep;
  fp[0x2f] = 3;
  *(ushort *)(fp + 0x40) = *(ushort *)(fp + 0xc);
  *(ushort *)(fp + 0x44) = *(ushort *)(fp + 0x10);
  fp[0x21] = 0;
  *(ushort *)(fp + 0x4e) = 0;
  fp[0x22] = 1;
  ST32((fp + 0x24), (a2));
  fp[0x23] = 0;
  fp[0] |= 8;                                              /* e46c bset #3,(fp) */
}

uint FUN_0000e316(undefined2 *p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *base = (byte *)WRAM_BASE;
  byte *cnt = base + (0xFFF15Du - 0xFFC000u);
  byte *p2, *p3;
  unsigned inc = (head[0x20] & 2) ? 3u : 4u;               /* e31a btst #1,$20(fp) */
  unsigned d0;
  (void)p_A5;

  d0 = ((unsigned)*cnt + inc) & 0xFFu;
  if (d0 >= 11u) {                                         /* e32c cmpib #11 ; bccs e310 */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  *cnt = (byte)d0;
  if (!FUN_0000e032()) {                                   /* e33c bccs e30e */
    *cnt = (byte)(*cnt - inc);                             /* e2f0: by the head's bit 1 */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p2 = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(p2);            /* e33e */
  *(ushort *)p2 = 0x8000;
  p2[0x20] = head[0x20];
  if (!FUN_0000e032()) {                                   /* e358 bccs e308 */
    *(ushort *)p2 = 0;
    *cnt = (byte)(*cnt - inc);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p3 = _r_unaff_A5;
  *(short *)(p2 + 0x78) = (short)A68OF16(p3);              /* e35a: PART 3, the second search's A5 */
  *(short *)(p3 + 0x7a) = (short)A68OF16(p2);
  p3[0x20] = p2[0x20];

  /* e36a: the head */
  *(ushort *)(head + 0x40) = *(ushort *)(head + 0xc);
  *(ushort *)(head + 0x44) = *(ushort *)(head + 0x10);
  head[0x7b] = (head[0x20] & 1) ? 4 : 2;                   /* e37e..e38a */
  _r_unaff_A6 = head;
  kind5_template(head, 0xeba2u, 0xebc6u);                  /* e38e bsrw e42e */
  if (head[0x20] & 2) { head[0x4a] = 0x20; head[0xb] = 0x69; }   /* e394..e3a4 */
  else head[0x4a] = 0x10;
  request_palette_update();                                /* e3a8 jsr 3b2e */
  ST32((head + 0x50), (0xf010ee17u));
  _r_unaff_A6 = head;
  hitbox_b_expand();                                       /* e3b6 jsr d4b8 */
  *(short *)(head + 0x1e) = 0x800;
  *(ushort *)(head + 0x3e) = 0;

  /* e3c8: part 2 */
  _r_unaff_A6 = p2;
  kind5_template(p2, 0xebaeu, 0xed3cu);                    /* e3d6 bsrs e42e */
  if (p2[0x20] & 2) p2[0xb] = 0x68;
  request_palette_update();                                /* e3e6 */
  p2[0x7f] = 0;
  *(short *)(p2 + 0x3c) = (short)A68OF16(head);            /* e3f0 movew (sp),$3c(fp) */

  /* e3f4: part 3 */
  _r_unaff_A6 = p3;
  kind5_template(p3, 0xebbau, 0xed8cu);                    /* e400 bsrs e42e */
  if (p3[0x20] & 2) p3[0xb] = 0x68;
  request_palette_update();                                /* e410 */
  *(short *)(p3 + 0x3c) = (short)A68OF16(head);            /* e416 movew (sp)+,$3c(fp) */
  *(short *)(p3 + 0x78) = -1;
  p3[0x7f] = 0;
  _r_unaff_A6 = p3;
  sound_enqueue(0xffffffb2u);                              /* e424 moveb #-78,d0 ; jmp 3352 */
  _r_unaff_A6 = p3;                                        /* FP = part 3 on return */
  return 0;
}
