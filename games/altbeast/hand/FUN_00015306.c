/* FUN_00015306 -- kind 15's init (a head and two parts, templates 0x15E80/0x15E8C/0x15E98),
 * from the 68K listing 0x152E2-0x15432. thunk_FUN_00015306 (0x152E2 `bras 15306`) is the
 * kind table's entry and stays generated.
 *
 *   15306: d0 = 0xFFF15D + (bit 4 of +0x20 ? 4 : 3) ; cmpib #11 ; bccs 15300   cap: clr.w, rts
 *   15322: 0xFFF15D = d0 ; jsr e032 ; bccs 152ea          the count back by the same, clr.w, rts
 *   1532e: movew a5,$78(fp) ; movew #$8000,(a5) ; moveb $20(fp),$20(a5) ; push fp ; fp = a5
 *   15342: jsr e032 ; bccw 152e4                           clr.w (part 2) ; fp = (sp)+ ; 152ea
 *   1534c: movew a5,$78(fp) ; moveb $20(fp),$20(a5) ; movew fp,$7a(a5) ; moveaw (sp)+,fp
 *   1535c: the head: template 0x15E80, script 0x15EA8 (bsr 153ec), +0x50 = $F40CB800,
 *          +0x1C = 0x80, +0x1E = 0x800, +0x4A = 0x20, bit 2 of +0x20 -> +0x77 = 2, jsr 3b2e
 *   15396: push fp ; fp = $78(fp)   part 2: template 0x15E8C, script 0x15F02, +0x4A = 0x10,
 *          jsr 3b2e, +0x3C = (sp) the head, +0x2E = the head's
 *   153be: fp = $78(fp)             part 3: template 0x15E98, script 0x15F5C, +0x4A = 0x18,
 *          jsr 3b2e, (sp)+ the head: +0x2E = its, +0x3C = it, +0x78 = -1 ; rts
 *
 * WHY A HAND FILE. The same three defects as hand/FUN_000132a0.c: A5 held at part 2 for
 * the second search, bare SH-2 template pointers, A6 never moved to the parts (2026-10-23). */
static void kind15_template(byte *fp, unsigned a1, unsigned a2)     /* 0x153EC */
{
  const byte *t = (const byte *)ROM_BASE + a1;
  byte keep = fp[8];
  unsigned i;
  for (i = 0; i < 12u; i++) fp[i] = t[i];
  fp[8] = keep;
  fp[0x2f] = 2;
  *(ushort *)(fp + 0x4e) = 0;
  fp[0x21] = 0;
  fp[0x22] = 1;
  fp[0x23] = 1;
  ST32((fp + 0x24), (a2));
  if (fp[0x20] & 1) fp[0x2e] = 0x80;                       /* 1541e btst #0,$20(fp) */
  fp[0] |= 8;
}

uint FUN_00015306(undefined2 *p_A5)
{
  byte *head = (byte *)_r_unaff_A6;
  byte *base = (byte *)WRAM_BASE;
  byte *cnt = base + (0xFFF15Du - 0xFFC000u);
  byte *p2, *p3;
  unsigned inc = (head[0x20] & 0x10) ? 4u : 3u;            /* 1530a btst #4,$20(fp) */
  unsigned d0;
  (void)p_A5;

  d0 = ((unsigned)*cnt + inc) & 0xFFu;
  if (d0 >= 11u) {                                         /* 1531c cmpib #11 ; bccs 15300 */
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  *cnt = (byte)d0;
  if (!FUN_0000e032()) {                                   /* 1532c bccs 152ea */
    *cnt = (byte)(*cnt - inc);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p2 = _r_unaff_A5;
  *(short *)(head + 0x78) = (short)A68OF16(p2);            /* 1532e */
  *(ushort *)p2 = 0x8000;
  p2[0x20] = head[0x20];
  if (!FUN_0000e032()) {                                   /* 15348 bccw 152e4 */
    *(ushort *)p2 = 0;
    *cnt = (byte)(*cnt - inc);
    *(ushort *)head = 0;
    _r_unaff_A6 = head;
    return 0;
  }
  p3 = _r_unaff_A5;
  *(short *)(p2 + 0x78) = (short)A68OF16(p3);              /* 1534c */
  p3[0x20] = p2[0x20];
  *(short *)(p3 + 0x7a) = (short)A68OF16(p2);

  /* 1535c: the head */
  _r_unaff_A6 = head;
  kind15_template(head, 0x15e80u, 0x15ea8u);
  ST32((head + 0x50), (0xf40cb800u));
  *(short *)(head + 0x1c) = 0x80;
  *(short *)(head + 0x1e) = 0x800;
  head[0x4a] = 0x20;
  if (head[0x20] & 4) head[0x77] = 2;                      /* 15382 btst #2 ; beqs 15390 */
  request_palette_update();                                /* 15390 */

  /* 15396: part 2 */
  _r_unaff_A6 = p2;
  kind15_template(p2, 0x15e8cu, 0x15f02u);
  p2[0x4a] = 0x10;
  request_palette_update();                                /* 153ac */
  *(short *)(p2 + 0x3c) = (short)A68OF16(head);            /* 153b4 */
  p2[0x2e] = head[0x2e];

  /* 153be: part 3 */
  _r_unaff_A6 = p3;
  kind15_template(p3, 0x15e98u, 0x15f5cu);
  p3[0x4a] = 0x18;
  request_palette_update();                                /* 153d2 */
  p3[0x2e] = head[0x2e];                                   /* 153da */
  *(short *)(p3 + 0x3c) = (short)A68OF16(head);
  *(short *)(p3 + 0x78) = -1;
  _r_unaff_A6 = p3;                                        /* FP = part 3 on return */
  return 0;
}
