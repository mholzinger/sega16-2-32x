/* FUN_0001176a -- kind 10's init (handler 0x11886, a head and one part), from the 68K
 * listing 0x1176A-0x11884:
 *
 *   1176a: d0 = 0xFFF15D + 1 ; cmpib #11 ; bccs 1178a      the enemy count's cap: refuse
 *   11778: 0xFFF15D = d0 ; jsr e032 ; bcss 11790           no record: count back, clr.w (fp), rts
 *   11790: movew a5,$78(fp) ; movew #-1,$78(a5) ; +0x20 +0xC +0x10 copied ; movew fp,$3c(a5)
 *   117b0: a1 = 0x11E42 ; a2 = 0x11E5E ; +0x23 = 2 ; bit 1 of +0x20 clear -> +0x14 = 0x40,
 *          a2 = 0x11EF4, +0x23 = 0 ; bsr 11836 (template + script, on the head)
 *   117d8: +0x4A = 0x18 ; bit 7 of +0x20 -> +0xB = 0x72, +0x4A = 0x20 ; jsr 3b2e
 *   117f8: +0x1C = 0x80 ; +0x1E = 0x800 ; +0x50 = $DA26FF00
 *   1180c: moveaw a5,fp                                    FP = THE PART
 *          a1 = 0x11E4E ; a2 = 0x11E76 ; +0x23 = 3 ; bsr 11836 ; +0x14 = 0 ;
 *          bit 7 of +0x20 -> +0xB = 0x71 ; jmp 3b2e
 *
 * WHY A HAND FILE. The decompile passed the templates 0x11E42/0x11E4E (both 2 mod 4) to
 * FUN_00011836 as bare SH-2 pointers, so the record heads were read from SH-2 0x11E42,
 * and it never moved A6 to the part, so the part's template landed on the head. Same
 * family as hand/FUN_00012b08.c and hand/FUN_000132a0.c (2026-10-23). */
static void kind10_template(byte *fp, unsigned a1, unsigned a2)     /* 0x11836 */
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];
  fp[8] = keep;
  fp[0x2f] = 2;
  ST32((fp + 0x24), (a2));
  *(ushort *)(fp + 0x4e) = 0;
  fp[0x21] = 0;
  fp[0x7f] = 0;
  fp[0x22] = 1;
  fp[0x2e] = 0;
  if (fp[0x20] & 1) {                                      /* 1186c btst #0,$20(fp) */
    *(short *)(fp + 0x14) = (short)(-*(short *)(fp + 0x14));
    fp[0x2e] = 0x80;
  }
  fp[0] |= 8;
}

uint FUN_0001176a(int p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *base = (byte *)WRAM_BASE;
  byte *cnt = base + (0xFFF15Du - 0xFFC000u);
  byte *part;
  unsigned a2, d0;
  (void)p_A5;

  d0 = ((unsigned)*cnt + 1u) & 0xFFu;
  if (d0 >= 11u) {                                         /* 11772 cmpib #11 ; bccs 1178a */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  *cnt = (byte)d0;
  if (!FUN_0000e032()) {                                   /* 11782 bcss 11790 */
    *cnt = (byte)(*cnt - 1);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  part = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(part);          /* 11790 */
  *(short *)(part + 0x78) = -1;
  part[0x20] = head[0x20];
  *(ushort *)(part + 0xc) = *(ushort *)(head + 0xc);
  *(ushort *)(part + 0x10) = *(ushort *)(head + 0x10);
  *(short *)(part + 0x3c) = (short)A68OF16(head);

  /* 117b0: the head */
  a2 = 0x11e5eu;
  head[0x23] = 2;
  if (!(head[0x20] & 2)) {                                 /* 117be btst #1 ; bnes 117d6 */
    *(short *)(head + 0x14) = 0x40;
    a2 = 0x11ef4u;
    head[0x23] = 0;
  }
  _r_unaff_A6 = head;
  kind10_template(head, 0x11e42u, a2);                     /* 117d6 bsrs 11836 */
  head[0x4a] = 0x18;
  if (head[0x20] & 0x80) {                                 /* 117de btst #7 ; beqs 117f2 */
    head[0xb] = 0x72;
    head[0x4a] = 0x20;
  }
  request_palette_update();                                /* 117f2 */
  *(short *)(head + 0x1c) = 0x80;
  *(short *)(head + 0x1e) = 0x800;
  ST32((head + 0x50), (0xda26ff00u));

  /* 1180c: the part */
  _r_unaff_A6 = part;
  part[0x23] = 3;
  kind10_template(part, 0x11e4eu, 0x11e76u);               /* 1181c bsrs 11836 */
  *(short *)(part + 0x14) = 0;
  if (part[0x20] & 0x80) part[0xb] = 0x71;
  request_palette_update();                                /* 11830 jmp 3b2e */
  _r_unaff_A6 = part;                                      /* FP = the part on return */
  return 0;
}
