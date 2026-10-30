/* FUN_00004276 -- the high-score NAME ENTRY for one player, 0x4276-0x432E with its helpers
 * 0x4212 (rank), 0x4238 (insert), 0x4330 (one frame of entry), 0x442E (index -> glyph),
 * 0x451E (timer digits) and the commit at 0x4202, from the 68K listing. Registers:
 * A6 = the player's state byte (0xFFF028 / 0xFFF029, read from _r_unaff_A6), A3 = the OTHER
 * player's, A4 = the player record (0xFFE000 / 0xFFE080: +0 BCD score, +8 text pointer,
 * +0x1E input, +0x20 last input), A5 = the entry record (0xFFFC74 / 0xFFFC82):
 *   +0 frame count 59..0   +1 BCD seconds from 30   +2 auto-repeat count
 *   +3 letter index 0..31  +4 cursor 0..3           +5 sink   +6..8 the name
 *   +9 rank                +10 text offset of the cursor (word, from 0x410000)
 *   +12 the name cursor (word, a 68K work-RAM address)
 *
 *   4276: bclr #1,(a6) ; bne start ; btst #2,(a6) ; bne frame ; rts
 *   start: bsr 4212 ; bcc qualify ; 8 bytes of 0x47BF -> text+0x81 ; clr.b (a6) ; rts
 *   qualify: bsr 4238 ; bset #2,(a6) ; a0 = text+0x107 ; a1 = a5+6 ; a5[0]=59 ; a5[1]=0x30 ;
 *            clr.l a5[2] ; move.l #"    ",(a1) ; a5[9]=d1 ; bsr 451e ; -> rank
 *   frame:  a0 = 0x410000 + (short)a5[10] ; a1 = (short)a5[12] ; bsr 4212 ; while d1 < 7
 *           and (a1).w != a4.w: a1 += 8, d1++ ; a5[9] = d1
 *   rank:   3 bytes of 0x47A7 + 3*d1 -> text+0x83 ; bsr 4330 ; a5[10]=a0.w ; a5[12]=a1.w
 *
 * WHY A HAND FILE (2026-10-25, Mike on sw320: "in a game over countdown we never see the
 * countdown or the player name entry"). The decompile cannot express this cluster: 0x4212
 * answers in the CARRY and in A0/A1/D0/D1, 0x4238 reads the A0 and D0 0x4212 left, 0x4330
 * returns A0/A1 that only some paths write, 0x442E answers from the Z flag its caller left,
 * and the end path branches into the middle of 0x41F0. Measured on ares (game-over tape,
 * scratchpad go/gameover3.csv): the port inserted score 0 with tag 0x3400, the BCD timer
 * stuck at 00 and the state byte stayed 0x05 for ever, so the entry never ended. */
static byte *hs_text(const byte *a4)                       /* the player's text pointer */
{
  return (byte *)A68PTR(LD32U(a4 + 8));
}

static void hs_timer(const byte *a4, const byte *a5)       /* 0x451E */
{
  byte *a2 = hs_text(a4) + 0x8d;
  byte d = a5[1];
  a2[0] = (byte)(((d >> 4) & 0xFu) + 0x30u);
  a2[2] = (byte)((d & 0xFu) + 0x30u);
}

static byte hs_glyph(byte idx, int z)                      /* 0x442E; z = the Z flag at entry */
{
  if (z) return 0x3b;
  if (idx < 0x1b) return (byte)(idx + 0x40);
  return ((const byte *)ROM_BASE)[0x4773 + (byte)(idx - 0x1b)];
}

/* 0x4212: D1 = the first table row the score is >= (unsigned BCD long), D0 = the dbf count
 * left, A1 = that row's +4; carry SET (return 1) when no row qualifies (D1 = 7). */
static int hs_rank(const byte *a4, uint *d0, uint *d1, byte **a1)
{
  byte *row = (byte *)W68PTR(0xFFFC00u);
  uint score = LD32U(a4), n;
  for (n = 0; n < 7; n++, row += 8) {
    if (score >= LD32U(row)) {
      *d0 = 6 - n; *d1 = n; *a1 = row + 4;
      return 0;
    }
  }
  *d0 = 0xFFFFu; *d1 = 7; *a1 = (byte *)W68PTR(0xFFFC38u);
  return 1;
}

static void hs_commit(void)                                /* 0x4202 -> 0x41AA, 0x4196 */
{
  byte *t = (byte *)W68PTR(0xFFFC00u);
  ushort c = 0;
  int i;
  for (i = 0; i < 0x1c; i++) {
    ushort x = (ushort)(c ^ (ushort)((t[2 * i] << 8) | t[2 * i + 1]));
    c = (ushort)(x >> 1);
    if (x & 1) c = (ushort)(c ^ 0x8810u);
  }
  t[0x38] = (byte)(c >> 8); t[0x39] = (byte)c;
  for (i = 0; i < 0x3a; i++) t[0x3a + i] = t[i];
  ST32(W68PTR(0xFFF010u), LD32U(t));
}

static byte hs_sbcd1(byte d)                               /* sbcd #1 with X clear */
{
  int lo = (int)(d & 0xFu) - 1, hi = (int)(d >> 4);
  if (lo < 0) { lo += 10; hi -= 1; }
  if (hi < 0) hi += 10;
  return (byte)((hi << 4) | lo);
}

/* 0x4330: one frame of the entry. a0 = the cursor in text RAM, a1 = the name cursor.
 * Returns 1 when the entry ended through the table commit (0x4202). */
static int hs_frame(byte **pa0, byte **pa1, byte *a6, const byte *a3, byte *a4, byte *a5)
{
  byte *a0 = *pa0, *a1 = *pa1;
  byte c = a5[0], d0, d1, held;
  int z;
  a5[0] = (byte)(c - 1);
  if (c == 0) {                                            /* 0x4334 bcc not taken */
    a5[0] = 0x3b;
    d0 = hs_sbcd1(a5[1]);
    a5[1] = d0;
    if (d0 & 0x80u) goto end;                              /* 0x434C bmi: time up */
    hs_timer(a4, a5);
  }
  if (DAT_fffff026 & 0x20) goto end;                       /* 0x4354 */
  if (!(DAT_00c41001 & (a6 == (byte *)W68PTR(0xFFF028u) ? 0x10 : 0x20))) goto end; /* START */
  held = a4[0x1e];
  d1 = (byte)(~a4[0x20] & held);
  if (d1 & 7) {                                            /* 0x4452: a button */
    d0 = a5[3];
    if (d0 == 0) goto end;
    if (d0 == 0x1f) {                                      /* 0x447E: back */
      a5[4] = (byte)(a5[4] - 1);
      *a0 = 0x20; *a1 = 0x20;
      a0 -= 2;
      if (a5[4] == 2) { a1 = a5 + 8; z = 1; }
      else {
        a1 -= 1;
        if (a5[4] != 0) z = 0;
        else { a5[3] = 0; z = 1; }
      }
    } else {                                               /* a letter: next position */
      a0 += 2;
      a5[4] = (byte)(a5[4] + 1);
      if (a5[4] < 3) { a1 += 1; z = 0; }
      else { a1 = a5 + 5; a5[3] = 0; z = 1; }
    }
    c = hs_glyph(d0, z);                                   /* 0x44A6 */
    *a0 = c; *a1 = c;
    *pa0 = a0; *pa1 = a1;
    return 0;
  }
  if (d1 & 0xC0) {
    /* newly pressed left/right: move now, direction from the new bits */
  } else if (!(held & 0xC0)) {
    a5[2] = 0;
    d0 = a5[3];
    goto store;
  } else {
    d1 = held;
    d0 = a5[3];
    a5[2] = (byte)(a5[2] + 1);
    if (a5[2] < 10) goto store;
  }
  a5[2] = 0;                                               /* 0x43C0 */
  d0 = a5[3];
  if (d1 & 0x80) {
    if (a5[4] == 0 && d0 == 0) d0 = 0x1e; else d0 = (byte)(d0 - 1);
  } else {
    if (a5[4] == 0 && d0 == 0x1e) d0 = 0; else d0 = (byte)(d0 + 1);
  }
store:                                                     /* 0x43F2 */
  d0 &= 0x1f;
  if (a5[4] >= 3 && d0 != 0) d0 = (d0 == 0x1e) ? 0 : 0x1f;
  a5[3] = d0;
  c = hs_glyph(d0, d0 == 0);
  *a0 = c;
  *a1 = (c == 0x3b || c == 0x3a) ? 0x20 : c;
  *pa0 = a0; *pa1 = a1;
  return 0;
end:                                                       /* 0x44AE */
  *a6 = 0;
  if (a5[3] == 0 || a5[3] == 0x3a) *a0 = 0x20;
  {
    byte *a2 = hs_text(a4) + 0x10d;
    a2[0] = 0x20; a2[2] = 0x20;
  }
  a1 = (byte *)W68PTR(0xFFFBFCu);                          /* 0x44D4 */
  if (a5[9] < 7) {
    byte *row = (byte *)W68PTR(0xFFFC04u) + 8u * a5[9];
    row[0] = (byte)(DAT_fffff142 + 1);
    row[1] = a5[6]; row[2] = a5[7]; row[3] = a5[8];
    a1 = row + 3;                                          /* the last `move.b a5@(8),(a1)` */
    if (!(*a3 & 4)) {
      hs_commit();
      /* 0x4202 -> 0x41AA -> 0x4196 leave A0 = 0xFFFC38 (past the checksummed words),
       * A6 = 0xFFFC3A and A5 = 0xFFFC74 (the copy loop's post-increments): the caller's
       * `move.w a0,a5@(10) ; move.w a1,a5@(12)` then lands in 0xFFFC74's record. */
      *pa0 = a0; *pa1 = a1;
      return 1;
    }
  }
  *pa0 = a0; *pa1 = a1;
  return 0;
}

/* SIGNATURE MATCHES protos.h. */
uint FUN_00004276(undefined4 p_A3,int p_A4,undefined *p_A5)
{
  byte *a6 = (byte *)A68PTR(_r_unaff_A6);
  const byte *a3 = (const byte *)A68PTR(p_A3);
  byte *a4 = (byte *)A68PTR(p_A4);
  byte *a5 = (byte *)A68PTR(p_A5);
  byte *text, *tbase = (byte *)A68PTR(0x410000u);
  byte *a0, *a1, *row;
  uint d0, d1, i;
  ushort a4w = (ushort)A68OF16((unsigned)a4);
  byte was = *a6;
  *a6 = (byte)(was & ~2u);
  text = hs_text(a4);
  if (was & 2) {                                           /* 0x4284: the entry starts */
    if (hs_rank(a4, &d0, &d1, &row)) {                     /* not on the table */
      const byte *s = (const byte *)ROM_BASE + 0x47bf;
      for (i = 0; i < 8; i++) text[0x81 + 2 * i] = s[i];
      *a6 = 0;
      return 0;
    }
    {                                                      /* 0x4238: open row d1 */
      byte *at = (byte *)W68PTR(0xFFFC00u) + 8u * d1;
      d0 &= 0xFFFFu;
      for (i = 8u * d0; i > 0; i--) at[8 + i - 1] = at[i - 1];
      ST32(at, LD32U(a4));
      at[4] = (byte)(a4w >> 8); at[5] = (byte)a4w;
    }
    *a6 |= 4;
    a0 = text + 0x107;
    a1 = a5 + 6;
    a5[0] = 0x3b; a5[1] = 0x30;
    a5[2] = 0; a5[3] = 0; a5[4] = 0; a5[5] = 0;
    a1[0] = 0x20; a1[1] = 0x20; a1[2] = 0x20; a1[3] = 0x20;
    a5[9] = (byte)d1;
    hs_timer(a4, a5);
  } else {
    if (!(*a6 & 4)) return 0;
    a0 = tbase + (short)((a5[10] << 8) | a5[11]);          /* 0x42D8 */
    a1 = (byte *)W68((short)((a5[12] << 8) | a5[13]));
    hs_rank(a4, &d0, &d1, &row);
    while (d1 < 7 && (ushort)((row[0] << 8) | row[1]) != a4w) { row += 8; d1++; }
    a5[9] = (byte)d1;
  }
  {                                                        /* 0x4302: the rank, "1ST".."8TH" */
    const byte *s = (const byte *)ROM_BASE + 0x47a7 + 3u * d1;
    for (i = 0; i < 3; i++) text[0x83 + 2 * i] = s[i];
  }
  {
    ushort o, w;
    if (hs_frame(&a0, &a1, a6, a3, a4, a5)) {              /* committed: see hs_frame */
      o = 0xFC38u;
      a5 = (byte *)W68PTR(0xFFFC74u);
      _r_unaff_A6 = (byte *)W68PTR(0xFFFC3Au);
    } else {
      o = (ushort)(a0 - tbase);                            /* the low word of 0x41xxxx */
    }
    w = (ushort)A68OF16((unsigned)a1);
    a5[10] = (byte)(o >> 8); a5[11] = (byte)o;
    a5[12] = (byte)(w >> 8); a5[13] = (byte)w;
  }
  return 0;
}
