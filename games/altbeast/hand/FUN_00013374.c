/* FUN_00013374 -- the level-2 enemy's head handler (kind 6: head 0x13374, two 0x137CC
 * parts installed by hand/FUN_000132a0.c), from the 68K listing 0x13374-0x1390C.
 *
 * WHY A HAND FILE. The handler dispatches its state machine through the PC-relative
 * word table at 0x13926:
 *     133da: lea 0x13926(pc),a0 ; moveq #0,d0 ; move.b $23(fp),d0 ; add.w d0,d0
 *     133e6: adda.w (a0,d0.w),a0 ; jmp (a0)
 * seven entries: 0 -> 13442, 1 -> 13476, 2,3 -> 1355E, 4 -> 1384E, 5 -> 1364A, 6 -> 13720.
 * tools/ghidra/make_jumptables.py never registered it (it looks for `jmp (An)` after an
 * `adda.w (An),An`; this one is `adda.w (a0,d0.w),a0 ; jmp (a0)`), so the decompile called
 * `(*(code *)(... ROM_BASE + 0x13926 ...))()` and the SH-2 ran ROM bytes: ares --profile
 * put the master at PC 0x02313444 for the whole run once kind 6 was installed (lv2_all3,
 * 2026-10-23). The six state blocks were unreachable to Ghidra and are not in the
 * generated body; FUN_0001349a and FUN_00013776 are its pieces carved out as functions.
 *
 * Record fields used (fp = the head): +0 flags (bit 3 drawn, bit 0 set on part 3 at
 * death), +6 sprite set, +9, +0xB palette, +0xC x, +0x10 y, +0x14 dx, +0x1A dy, +0x1C/+0x1E
 * gravity/terminal, +0x20 (bit 7 facing), +0x21 phase, +0x22 frame, +0x23 STATE, +0x24
 * script, +0x2C contact flags (bit 4 on the floor), +0x2D previous, +0x2E facing 0x80,
 * +0x2F depth band, +0x3C the grabbed player, +0x3E counter, +0x3F, +0x40/+0x44 home x/y,
 * +0x48, +0x4A health, +0x4B hit flags (bit 1 a grab, bits 2-7 damage), +0x4C attacker,
 * +0x4E animation index, +0x6C the nearest player, +0x78 part 2. Players at 0xFFC000 /
 * 0xFFC400; their grab counts at 0xFFF156 / 0xFFF157. */
static byte *k6_nearest_player(byte *fp, unsigned *dist)       /* 0x1349A: a0, d0; writes +0x6C, +0x2E */
{
  byte *base = (byte *)WRAM_BASE;
  byte *a0 = base + (0xFFC000u - 0xFFC000u), *a1 = base + (0xFFC400u - 0xFFC000u);
  unsigned d0 = 0x1000, d1 = 0x1000, x = *(ushort *)(fp + 0xc);
  byte d2 = 0, d3 = 0;
  if (base[0xFFF028u - 0xFFC000u] == 1 && a0[0x20] < 0x11) {
    unsigned x0 = *(ushort *)(a0 + 0x40);
    if (x0 >= x) d0 = (x0 - x) & 0xFFFFu; else { d0 = (x - x0) & 0xFFFFu; d2 = 0x80; }
  }
  if (base[0xFFF029u - 0xFFC000u] == 1 && a1[0x20] < 0x11) {
    unsigned x1 = *(ushort *)(a1 + 0x40);
    if (x1 >= x) d1 = (x1 - x) & 0xFFFFu; else { d1 = (x - x1) & 0xFFFFu; d3 = 0x80; }
  }
  if (d1 < d0) { a0 = a1; d2 = d3; d0 = d1; }              /* 134ea cmpw d0,d1 ; bccs */
  *(short *)(fp + 0x6c) = (short)A68OF16(a0);
  fp[0x2e] = d2;
  *dist = d0;
  return a0;
}

static void k6_part_launch(byte *part, const byte *head)     /* 0x13776, fp = the part, a5 = the head */
{
  ST32((part + 2), (0x137e0u));                            /* the part's handler, a 68K address */
  *(ushort *)(part + 6) = 0x01a5;
  part[9] = 3;
  part[0x2f] = 3;
  *(ushort *)(part + 0xc) = (ushort)(*(ushort *)(head + 0xc) - 0x10);
  *(ushort *)(part + 0x10) = (ushort)(*(ushort *)(head + 0x10) - 0x10);
  *(short *)(part + 0x14) = -0x300;
  *(short *)(part + 0x1a) = 0x200;
  part[0x22] = 0x78;
  part[0x3e] = 0x20;
  part[0xb] = 0x3f;
  _r_unaff_A6 = part;
  request_palette_update();                                /* 137c6 jmp 3b2e */
}

uint FUN_00013374(undefined4 p_A0,undefined4 p_D1,uint p_D3,undefined4 p_D6,undefined4 p_D7)
{
  byte *fp = (byte *)_r_unaff_A6;
  byte *base = (byte *)WRAM_BASE;
  byte *a0, *a1, *a5;
  unsigned d0, cam;
  byte *cnt;

  restore_position(p_A0, p_D1);                            /* 13374 jsr 65aa */
  if (!(fp[0] & 8)) {                                      /* 1337a btst #3,(fp) ; bnes 133b0 */
    cam = (((unsigned)base[0xFFF132u - 0xFFC000u] << 8) | base[0xFFF133u - 0xFFC000u]);
    d0 = (cam - 0x40) & 0xFFFFu;
    if (d0 >= *(ushort *)(fp + 0x40)) goto despawn;        /* 1338a cmpw $40(fp),d0 ; bccs 1339a */
    d0 = (d0 + 0x1c0) & 0xFFFFu;
    if (d0 < *(ushort *)(fp + 0x40)) goto despawn;         /* 13394 cmpw $40(fp),d0 ; bccs 133b0 */
  }
  /* 133b0 */
  if (fp[0x23] == 0) fp[0x4b] = 0;
  d0 = fp[0x4b];
  if (fp[0x23] < 6 && d0 != 0) {                           /* 133ba..133d4 */
    if (fp[0x23] != 4) goto hit;
    if (d0 & 0xfcu) goto hit;
  }
  fp[0x4b] = 0;                                            /* 133d6 */
  switch (fp[0x23]) {                                      /* 133da the 0x13926 table */
    case 0: goto s0;
    case 1: goto s1;
    case 2: case 3: goto s23;
    case 4: goto s4;
    case 5: goto s5;
    case 6: goto s6;
    default: _r_unaff_A6 = fp; _port_reg[3] = (uint)p_D3; return 0;
  }

hit:                                                       /* 133ec, d0 = the hit flags */
  fp[0x4b] = 0;
  if (d0 & 2u) {                                           /* a grab attempt by the attacker */
    a0 = (byte *)W68(*(short *)(fp + 0x4c));
    a1 = (byte *)W68(*(short *)(a0 + 0x3c));
    if (a1[0x2e] == 0) goto s4_enter;                      /* 13402 beqw 13800 */
  }
  if (fp[0x23] == 4) {                                     /* 13406: drop the held player */
    a0 = (byte *)W68(*(short *)(fp + 0x3c));
    a1 = (byte *)W68(*(short *)(a0 + 0x3c));
    a1[0x2e] = 0;
    cnt = base + ((*(short *)(fp + 0x3c) == (short)0xC000 ? 0xFFF156u : 0xFFF157u) - 0xFFC000u);
    *cnt = (byte)(*cnt - 1);
  }
  if ((d0 & 0xfcu) == 0) {                                 /* 1342a andiw #252 ; beqw 135ee */
    *(short *)(fp + 0x1a) = -0x200;
    goto thrown;
  }
  {   unsigned h = fp[0x4a], dmg = d0 & 0xfcu;             /* 13432 subb d0,$4a(fp) ; bhiw 135f6 */
      fp[0x4a] = (byte)(h - dmg);
      if (h > dmg) goto thrown_hurt;
      fp[0x4a] = 0;
      goto s_die;                                          /* 1343e braw 136dc */
  }

s0:                                                        /* 13442: rising from the ground */
  *(ushort *)(fp + 0x10) = (ushort)(*(ushort *)(fp + 0x10) - 1);
  if (*(ushort *)(fp + 0x10) >= 0x10d8) {                  /* 13446 cmpiw ; bcss 13460 */
    *(short *)(fp + 0x1c) = 0;
    goto tail;                                             /* 13452 braw 13678 */
  }
  goto s0_launch;
s_reset:                                                   /* 13456 */
  fp[0x21] = 0;
  fp[0x22] = 1;
s0_launch:                                                 /* 13460 */
  ST32((fp + 0x24), (0x1394cu));
  *(short *)(fp + 0x14) = 0;
  fp[0x23] = 1;
  fp[0x4e] = 0;
  /* falls into 13476 */
s1:                                                        /* 13476: walking, looking for a player */
  if (fp[0x21] < 1) {                                      /* bcss 1348c */
    if (fp[0x22] == 2) goto s1_approach;                   /* 1348c cmpib #2 ; beqw 134fe */
    goto tail;
  }
  if (fp[0x21] != 1) goto tail;                            /* 1347e bnes 13496 */
  if (fp[0x22] == 15) { unsigned dd; k6_nearest_player(fp, &dd); }   /* 13488 bsrs 1349a */
  goto tail;

s1_approach:                                               /* 134fe */
  a0 = (byte *)W68(*(short *)(fp + 0x6c));
  {   unsigned x0 = *(ushort *)(a0 + 0x40), x = *(ushort *)(fp + 0xc);
      d0 = (x0 >= x) ? (x0 - x) : (x - x0);
      d0 &= 0xFFFFu;
  }
  fp[0x23] = 2;                                            /* 1350e */
  if (d0 < 0x50) {                                         /* 13514 cmpiw #80 ; bccs 13544 */
    a1 = (byte *)W68(*(short *)(a0 + 0x3c));
    if (a1[0x2e] != 0) goto s_reset;                       /* 13522 bnew 13456 */
    fp[0x23] = 3;
    cnt = base + ((*(short *)(fp + 0x6c) == (short)0xC000 ? 0xFFF156u : 0xFFF157u) - 0xFFC000u);
    if (*cnt >= 2) goto s_reset;                           /* 1353c cmpib #2,(a0) ; bccw 13456 */
  }
  /* 13544 */
  ST32((fp + 0x24), (0x1399eu));
  *(short *)(fp + 0x14) = 0;
  fp[0x21] = 0;
  fp[0x22] = 1;
  fp[0x4e] = 0;
  /* falls into 1355e */
s23:                                                       /* 1355e: closing in (2) / grabbing (3) */
  if (fp[0x21] == 0 && fp[0x22] == 2) {                    /* 1355e..1356a */
    unsigned dd;
    a0 = k6_nearest_player(fp, &dd);                       /* 1356c bsrw 1349a */
    {   unsigned x0 = *(ushort *)(a0 + 0x40), x = *(ushort *)(fp + 0xc);
        d0 = ((x0 >= x) ? (x0 - x) : (x - x0)) & 0xFFFFu;
    }
    if (d0 < 0x80) goto s_reset;                           /* 13580 bcsw 13456 */
    goto s1_approach;                                      /* 13584 braw 134fe */
  }
  if (fp[0x21] == 3) {                                     /* 13588 cmpib #3 ; bnes 135ea */
    if (fp[0x22] >= 8) {                                   /* 13590 cmpib #8 ; bcss 135d4 */
      *(short *)(fp + 0x1a) = -0x400;
      d0 = 0x200;
      if (fp[0x23] != 2) {                                 /* 135a2 cmpib #2,$23(fp) ; beqs 135b4 */
        *(short *)(fp + 0x1a) = -0xc00;
        d0 = 0x180;
      }
      if (fp[0x2e] & 0x80) d0 = (unsigned)(-(int)d0);      /* 135b4 btst #7,$2e(fp) ; negw d0 */
      *(short *)(fp + 0x14) = (short)d0;
      *(short *)(fp + 0x1c) = 0x80;
      *(short *)(fp + 0x1e) = 0x800;
      fp[0x2c] &= (byte)~0x10;
    }
    fp[0x22] = 4;                                          /* 135d4 */
    if (fp[0x2c] & 0x10) {                                 /* 135da btst #4 ; beqs 135ea */
      fp[0x21] = (byte)(fp[0x21] + 1);
      *(short *)(fp + 0x14) = 0;
    }
  }
  goto tail;                                               /* 135ea braw 13678 */

thrown_hurt:                                               /* 135f6 */
  sound_enqueue(0xffffffb4u);
thrown:                                                    /* 135fe: knocked back */
  *(short *)(fp + 0x14) = -0x200;
  ST32((fp + 0x24), (0x13a12u));
  *(short *)(fp + 0x1c) = 0x80;
  *(short *)(fp + 0x1e) = 0x800;
  fp[0x21] = 0;
  fp[0x22] = 1;
  fp[0x23] = 5;
  fp[0x2e] = 0;
  fp[0x2c] &= (byte)~0x10;
  a0 = (byte *)W68(*(short *)(fp + 0x4c));                 /* 13632 the attacker */
  if (*(ushort *)(a0 + 0x40) < *(ushort *)(fp + 0x40)) {   /* 1363a cmpw $40(fp),d0 ; bccs 1364a */
    fp[0x2e] = 0x80;
    *(short *)(fp + 0x14) = (short)(-*(short *)(fp + 0x14));
  }
  /* falls into 1364a */
s5:                                                        /* 1364a: in the air after a hit */
  if (fp[0x2c] & 0x10) {                                   /* landed */
    *(short *)(fp + 0x14) = 0;
    ST32((fp + 0x24), (0x13a00u));
  }
  d0 = fp[0x22];                                           /* 1365e */
  if (d0 == 2) goto s_reset;                               /* 13666 beqw 13456 */
  d0 &= 7u;
  d0 = (d0 >= 4u) ? (d0 - 4u) : (4u - d0);                 /* 1366e subqb #4,d0 ; bccs ; negb d0 */
  fp[0x4e] = (byte)d0;
  /* falls into 13678 */
tail:                                                      /* 13678: the common motion + draw */
  clamp_y_velocity();
  integrate_position();
  hitbox_b_expand();
  fp[0x2d] = fp[0x2c];
  fp[0x2c] = 0;
  collide_box_b();
  floor_collide();
  FUN_0000669c();
  hitbox_a_expand();
  fp[0x48] = 0;
  if (fp[0x23] >= 2 && fp[0x23] <= 3 && fp[0x21] == 3) {   /* 136b6..136cc */
    FUN_0000d5a2();                                        /* 0xD5A2: the walk with D3 set */
  } else {
    walk_player_slots();                                   /* 0xD5A6 */
  }
  sprite_build_depth_banded(p_D3, p_D6, p_D7);             /* 136d6 jmp 3d14 */
  _r_unaff_A6 = fp;
  return 0;

s_die:                                                     /* 136dc */
  sound_enqueue(0xffffffc2u);
  fp[0x23] = 6;
  *(ushort *)(fp + 6) = 0x01a4;
  fp[9] = 3;
  fp[0x2f] = 3;
  *(ushort *)(fp + 0x10) = (ushort)(*(ushort *)(fp + 0x10) - 0x30);
  *(short *)(fp + 0x14) = 0;
  *(short *)(fp + 0x1a) = -0x400;
  *(short *)(fp + 0x1c) = -0x400;
  *(short *)(fp + 0x1e) = -0x400;
  fp[0x3e] = 22;
  /* 13752: launch the two parts; FP is saved and restored around them */
  {   byte *p2 = (byte *)W68(*(short *)(fp + 0x78));
      byte *p3 = (byte *)W68(*(short *)(p2 + 0x78));
      k6_part_launch(p2, fp);                              /* 1375a bsrs 13776 */
      k6_part_launch(p3, fp);                              /* 13760 bsrs 13776 */
      p3[0] |= 1;                                          /* 13762 bset #0,(fp) */
      *(ushort *)(p3 + 0xc) = (ushort)(*(ushort *)(p3 + 0xc) + 0x20);
      *(short *)(p3 + 0x14) = (short)(-*(short *)(p3 + 0x14));
  }
  _r_unaff_A6 = fp;                                        /* 13772 movea.l (sp)+,fp */
  _port_reg[3] = (uint)p_D3;
  return 0;

s6:                                                        /* 13720: dying */
  {   byte h = fp[0x3e];
      fp[0x3e] = (byte)(h - 4);
      if (h < 4) goto despawn;                             /* 13724 bcsw 1339a */
  }
  fp[0x4e] = fp[0x3e] & 0x1f;
  clamp_y_velocity();
  integrate_position();
  *(ushort *)(fp + 0x40) = *(ushort *)(fp + 0xc);
  *(ushort *)(fp + 0x44) = *(ushort *)(fp + 0x10);
  sprite_build_and_cull(p_D3, p_D6, p_D7);                 /* 1374c jmp 3dd8 */
  _r_unaff_A6 = fp;
  return 0;

s4_enter:                                                  /* 13800: grab the attacker (a0, its +0x3c in a1) */
  *(short *)(fp + 0x3c) = (short)A68OF16(a0);
  cnt = base + ((A68OF16(a0) == 0xFFC000u ? 0xFFF156u : 0xFFF157u) - 0xFFC000u);
  *cnt = (byte)(*cnt + 1);
  a1[0x2e] = base[0xFFF109u - 0xFFC000u];
  fp[0x2e] = (byte)(~a0[0x2e] & 0x80);
  ST32((fp + 0x24), (0x13a24u));
  fp[0x22] = 1;
  fp[0x23] = 4;
  fp[0x3e] = 19;
  fp[0x3f] = 0;
  sound_enqueue(0xffffffd5u);
  /* falls into 1384e */
s4:                                                        /* 1384e: holding a player */
  a5 = (byte *)W68(*(short *)(fp + 0x3c));
  if (a5[0x20] >= 0x11) goto s4_release;                   /* 13852 cmpib #17 ; bccs 1388a */
  if ((a5[0x2e] & 0x80) != fp[0x2e]) fp[0x2e] ^= 0x80;     /* 1385a..13868 */
  a0 = (byte *)W68(*(short *)(a5 + 0x3c));
  d0 = (unsigned)(~a0[0x29] & a0[0x28]) & 0xFFu;           /* 13872: buttons newly pressed */
  if (d0 != 0) {
    for (;;) {                                             /* 1387c..13888 */
      if (d0 == 0) break;
      {   unsigned bit = d0 & 1u;
          d0 >>= 1;
          if (!bit) continue;
      }
      {   byte h = fp[0x3e];
          fp[0x3e] = (byte)(h - 1);
          if (h == 0) goto s4_release;                     /* subqb borrowed */
      }
    }
  }
  /* 138aa */
  d0 = (fp[0x2e] & 0x80) ? *(ushort *)(a5 + 0x36) : *(ushort *)(a5 + 0x34);
  *(ushort *)(fp + 0xc) = (ushort)d0;
  *(ushort *)(fp + 0x10) = (ushort)(*(ushort *)(a5 + 0x38) + 0x10);
  fp[0x3f] = (byte)(fp[0x3f] + 1);
  if (fp[0x3f] >= 15) {                                    /* 138ce cmpib #15 ; bcss 138ee */
    fp[0x3f] = 0;
    if (*(ushort *)(a5 + 0x3e) == 0) {                     /* 138da tstw $3e(a5) ; bnes 138ee */
      unsigned h = a5[0x4a];
      a5[0x4a] = (byte)(h - 4);
      if (!(h > 4)) {                                      /* 138e4 bhis 138ee */
        a5[0x4a] = 0;
        a5[0x4b] = (byte)(a5[0x4b] + 4);
      }
    }
  }
  fp[0x2f] = a5[0x2f];                                     /* 138ee */
  FUN_0000669c();
  hitbox_a_expand();
  FUN_0000d5a2();
  {   /* 13906 jmp 3d36: sprite_build_depth_banded WITHOUT its depth-band step (0x3D14-0x3D32,
       * taken only when +0x2C bit 4 is set); the band was just copied from the player. */
      byte c = fp[0x2c];
      fp[0x2c] = (byte)(c & ~0x10);
      sprite_build_depth_banded(p_D3, p_D6, p_D7);
      fp[0x2c] = c;
  }
  _r_unaff_A6 = fp;
  return 0;

s4_release:                                                /* 1388a */
  {   byte *a2 = (byte *)W68(*(short *)(a5 + 0x3c));
      a2[0x2e] = 0;
  }
  cnt = base + ((A68OF16(a5) == 0xFFC000u ? 0xFFF156u : 0xFFF157u) - 0xFFC000u);
  *cnt = (byte)(*cnt - 1);
  fp[0x4a] = 0;
  goto s_die;                                              /* 138a6 braw 136dc */

despawn:                                                   /* 1339a */
  tick_kill_counters();
  *(ushort *)fp = 0;
  release_palette_slot();
  hide_object_sprite();
  _r_unaff_A6 = fp;
  _port_reg[3] = (uint)p_D3;
  return 0;
}
