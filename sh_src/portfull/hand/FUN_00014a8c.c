/* FUN_00014a8c -- kind 11's init (a head and two parts, templates 0x15164/0x15170/0x1517C),
 * from the 68K listing 0x14A7A-0x14BB8. thunk_FUN_00014a8c (0x14A78 `bras 14a8c`) is the
 * kind table's entry; its hand thunk delegates here rather than retaining
 * Ghidra's separate inlined copy of the broken initializer.
 *
 *   14a8c: d0 = 0xFFF15D + 3 ; cmpib #11 ; bccs 14a86      the count's cap: clr.w (fp), rts
 *   14a9a: 0xFFF15D = d0 ; jsr e032 ; bccs 14a80           count back by 3, clr.w (fp), rts
 *   14aa6: movew a5,$78(fp) ; movew #$8000,(a5) ; movew fp,-(sp) ; moveaw a5,fp   FP = PART 2
 *   14aba: jsr e032 ; bccs 14a7a                           clr.w (part 2), fp = (sp)+, count back, clr.w, rts
 *   14ac2: movew a5,$78(fp) ; movew fp,$7a(a5) ; moveb $20(fp),$20(a5) ; moveaw (sp)+,fp   FP = THE HEAD
 *   14ad2: a1 = 0x15164 ; a2 = 0x15188 ; +0x40 = +0xC ; +0x44 = +0x10 ; +0x4A = 0x20 ;
 *          bsr 14b5e (template, script, jmp 3b2e) ; +0x50 = $F404E21E ; jsr d4b8 ;
 *          +0x1E = 0x800 ; +0x3E = 0 ; +0x7B = 4
 *   14b1a: movew fp,-(sp) ; fp = $78(fp)                   PART 2: template 0x15170, script
 *          0x15294, +0x7F = 0, +0x3C = the head
 *   14b32: fp = $78(fp)                                    PART 3: template 0x1517C, script
 *          0x152D0, jsr 3b2e, +0x3C = (sp)+ the head, +0x78 = -1, +0x7F = 0,
 *          jmp 3352 with d0 = 0xB2
 *
 * WHY A HAND FILE. Ghidra held A5 at part 2 for the second search (part 2 linked to
 * itself under the re-read pass), passed the templates as bare SH-2 pointers and never
 * moved A6 to the parts. The twin of hand/FUN_000132a0.c (2026-10-23). */
static void kind11_template(byte *fp, unsigned a1, unsigned a2)     /* 0x14B5E */
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];
  fp[8] = keep;
  fp[0x2f] = 3;
  *(ushort *)(fp + 0x40) = *(ushort *)(fp + 0xc);
  *(ushort *)(fp + 0x44) = *(ushort *)(fp + 0x10);
  fp[0x21] = 0;
  *(ushort *)(fp + 0x4e) = 0;
  fp[0x22] = 1;
  ST32((fp + 0x24), (a2));
  fp[0x23] = 0;
  fp[0x2e] = 0;
  if (fp[0x20] & 1) fp[0x2e] = 0x80;                       /* 14b9e btst #0,$20(fp) */
  fp[0] |= 8;
  _r_unaff_A6 = fp;
  request_palette_update();                                /* 14bb2 jmp 3b2e */
}

uint FUN_00014a8c(undefined2 *p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *base = (byte *)WRAM_BASE;
  byte *cnt = base + (0xFFF15Du - 0xFFC000u);
  byte *p2, *p3;
  unsigned d0;
  (void)p_A5;

  d0 = ((unsigned)*cnt + 3u) & 0xFFu;
  if (d0 >= 11u) {                                         /* 14a94 cmpib #11 ; bccs 14a86 */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  *cnt = (byte)d0;
  if (!FUN_0000e032()) {                                   /* 14aa4 bccs 14a80 */
    *cnt = (byte)(*cnt - 3);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p2 = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(p2);            /* 14aa6 */
  *(ushort *)p2 = 0x8000;
  p2[0x20] = head[0x20];
  if (!FUN_0000e032()) {                                   /* 14ac0 bccs 14a7a */
    *(ushort *)p2 = 0;
    *cnt = (byte)(*cnt - 3);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p3 = _r_unaff_A5;
  *(short *)(p2 + 0x78) = (short)A68OF16(p3);              /* 14ac2 */
  *(short *)(p3 + 0x7a) = (short)A68OF16(p2);
  p3[0x20] = p2[0x20];

  /* 14ad2: the head */
  *(ushort *)(head + 0x40) = *(ushort *)(head + 0xc);
  *(ushort *)(head + 0x44) = *(ushort *)(head + 0x10);
  head[0x4a] = 0x20;                                       /* 14ae6..14af2: 0x20 either way */
  kind11_template(head, 0x15164u, 0x15188u);               /* 14af6 bsrw 14b5e */
  ST32((head + 0x50), (0xf404e21eu));
  _r_unaff_A6 = head;
  hitbox_b_expand();                                       /* 14b02 jsr d4b8 */
  *(short *)(head + 0x1e) = 0x800;
  *(ushort *)(head + 0x3e) = 0;
  head[0x7b] = 4;

  /* 14b1a: part 2 */
  kind11_template(p2, 0x15170u, 0x15294u);                 /* 14b28 bsrs 14b5e */
  p2[0x7f] = 0;
  *(short *)(p2 + 0x3c) = (short)A68OF16(head);            /* 14b2e movew (sp),$3c(fp) */

  /* 14b32: part 3 */
  kind11_template(p3, 0x1517cu, 0x152d0u);                 /* 14b3e bsrs 14b5e */
  _r_unaff_A6 = p3;
  request_palette_update();                                /* 14b40 jsr 3b2e, a second time on part 3 */
  *(short *)(p3 + 0x3c) = (short)A68OF16(head);            /* 14b46 movew (sp)+,$3c(fp) */
  *(short *)(p3 + 0x78) = -1;
  p3[0x7f] = 0;
  _r_unaff_A6 = p3;
  sound_enqueue(0xffffffb2u);                              /* 14b54 moveb #-78,d0 ; jmp 3352 */
  _r_unaff_A6 = p3;                                        /* FP = part 3 on return */
  return 0;
}
