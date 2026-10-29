/* FUN_000132a0 -- kind 6's init (the level-2 enemy: head 0x13374 + two 0x137CC parts),
 * from the 68K listing. Two free-slot searches, FP moved to the first part between them:
 *
 *   132a0: jsr e032 ; bccs 1329a             no record -> clr.w (fp) ; rts
 *   132a8: movew a5,$78(fp) ; movew #$8000,(a5)
 *   132b2: movew fp,-(sp) ; moveaw a5,fp     FP = PART 2
 *   132b6: jsr e032 ; bccs 13294             fail: clr.w (fp) ; fp = (sp)+ ; clr.w (fp) ; rts
 *   132be: movew a5,$78(fp) ; movew fp,$7a(a5) ; movew #$8000,(a5)
 *   132cc: moveaw (sp)+,fp                   FP = THE HEAD
 *   132ce: bsr 13324 (template 0x13934) ; bsr 13340 (script 0x1394c, jmp 3b2e)
 *   132de: +0x4a = 0x18 ; +0x2e = 0x80 when (camera.hi + 0xA0) < +0xc else 0
 *   132fa: movew fp,-(sp) ; fp = $78(fp)     PART 2: template 0x13940, +0x3c = head
 *   1330c: fp = $78(fp)                       PART 3: template 0x13940, +0x3c = head,
 *   1331c: +0x78 = -1 ; rts                   FP = part 3 on return
 *
 * WHY A HAND FILE. Ghidra held A5 at part 2 for the second search, so the decompile
 * linked part 2 to ITSELF and never installed part 3; it passed the templates as bare
 * SH-2 pointers (reads of SH-2 0x13934, garbage), and it never moved A6 to the parts, so
 * every template landed on the head. With kind 7 fixed (hand/FUN_00012b08.c) the
 * attract's level-2 demo on the port held 12cba/12f0a and still no 13374/137cc
 * (lv2_all2, 2026-10-22) where the arcade holds all four. */
static void kind6_template(byte *fp, unsigned a1)        /* 0x13324 */
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];
  fp[8] = keep;
  fp[0x2f] = 2;
}

static void kind6_setup(byte *fp, unsigned a2)           /* 0x13340 */
{
  *(ushort *)(fp + 0x40) = *(ushort *)(fp + 0xc);
  *(ushort *)(fp + 0x44) = *(ushort *)(fp + 0x10);
  fp[0x21] = 0;
  fp[0x22] = 1;
  fp[0x23] = 0;
  *(ushort *)(fp + 0x4e) = 0;
  *(ushort *)(fp + 0x7e) = 0;
  ST32((fp + 0x24), (a2));
  fp[0] |= 8;
  _r_unaff_A6 = fp;
  request_palette_update();                              /* 1336e jmp 3b2e */
}

uint FUN_000132a0(undefined2 *p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *p2, *p3;
  const byte *cam = (const byte *)W68PTR(0xFFF132u);     /* movew 0xfffff132,d1 = the high word */
  unsigned d1;
  (void)p_A5;

  if (!FUN_0000e032()) {                                 /* 132a6 bccs 1329a */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p2 = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(p2);          /* 132a8 */
  *(ushort *)p2 = 0x8000;
  if (!FUN_0000e032()) {                                 /* 132bc bccs 13294 */
    *(ushort *)p2 = 0;
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p3 = _r_unaff_A5;
  *(short *)(p2 + 0x78) = (short)A68OF16(p3);            /* 132be */
  *(short *)(p3 + 0x7a) = (short)A68OF16(p2);
  *(ushort *)p3 = 0x8000;

  /* 132ce: the head */
  _r_unaff_A6 = head;
  kind6_template(head, 0x13934u);
  kind6_setup(head, 0x1394cu);
  head[0x4a] = 0x18;                                     /* 132de */
  d1 = ((((unsigned)cam[0] << 8) | cam[1]) + 0xa0u) & 0xFFFFu;
  head[0x2e] = (d1 < *(ushort *)(head + 0xc)) ? 0x80 : 0;   /* 132ee cmpw $c(fp),d1 ; bccs */

  /* 132fa: part 2 */
  _r_unaff_A6 = p2;
  kind6_template(p2, 0x13940u);
  *(short *)(p2 + 0x3c) = (short)A68OF16(head);          /* 13308 */

  /* 1330c: part 3 */
  _r_unaff_A6 = p3;
  kind6_template(p3, 0x13940u);
  *(short *)(p3 + 0x3c) = (short)A68OF16(head);          /* 13318 */
  *(short *)(p3 + 0x78) = -1;                            /* 1331c */
  _r_unaff_A6 = p3;                                      /* FP = part 3 on return */
  return 0;
}
