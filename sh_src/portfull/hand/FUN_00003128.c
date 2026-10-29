/* FUN_00003128 -- the lives counter in the HUD (portrait, "x", digits) for both players,
 * 0x3128-0x3220 from the 68K listing; FUN_00003858 is the same code with its own tables
 * (0x3858-0x3950) and clears with 0xA000 instead of 0.
 *
 *   3128: btst #0,f026 ; bne rts
 *         f028 bit 0 set and bits 2/4 clear: a5=e000 d2.b=c06b d3=a400 ; bsr 316c
 *         f029 likewise:                     a5=e080 d2.b=c46b d3=a600 ; 316c
 *   316c: a0 = (a5+8).l + 0x80 ; bsr 317a (clear 7 cells on two rows) ; bra 3190
 *   3190: d4 = (a5+24).b ; beq rts ; d4-- ; d2 &= 3 ; d2 -= 2 ; eq: d2=0 ; cs: d2=0 ;
 *         else d3 += 0x400, d2 += f142 ; a1 = icons + 4*d2 ; four icon cells (low byte
 *         from the table, high byte d3), the "x" cells ac7c-ac7f, then the hundreds and
 *         tens (drawn only once a higher digit was) and the ones from the digit table
 *         (top row a1[d], bottom row a1[d+10], high byte 0xac)
 *
 * WHY A HAND FILE (2026-10-25, game-over lane). Ghidra split 316c/3190 so A0 never reached
 * 0x3190 (the port passed an uninitialised local), the P1 record arrived as the bare
 * 0xffffe000 (SH-2 on-chip registers), and the ones digit indexed with a stale byte where
 * the 68K uses D4: the P1 lives icon ("x3" top left) never drew. */
static uint hud_lives(uint rec, byte lvl, ushort d3, uint icons, uint digits, ushort clr)
{
  const byte *a5 = (const byte *)W68PTR(rec);
  const byte *rom = (const byte *)ROM_BASE;
  ushort *a0 = (ushort *)(A68PTR(LD32U(a5 + 8)) + 0x80);
  const byte *a1;
  int i, shown = 0;
  byte d4, d2, d;
  static const byte pw[2] = { 100, 10 };
  for (i = 0; i < 7; i++) { a0[i] = clr; a0[64 + i] = clr; }   /* 0x317A */
  d4 = a5[24];
  if (d4 == 0) return 0;
  d4 = (byte)(d4 - 1);
  d2 = (byte)(lvl & 3);
  if (d2 <= 2) d2 = 0;
  else { d3 = (ushort)(d3 + 0x400); d2 = (byte)(1 + DAT_fffff142); }
  a1 = rom + icons + 4u * d2;
  a0[0] = (ushort)((d3 & 0xFF00u) | a1[0]);
  a0[1] = (ushort)((d3 & 0xFF00u) | a1[1]);
  a0[64] = (ushort)((d3 & 0xFF00u) | a1[2]);
  a0[65] = (ushort)((d3 & 0xFF00u) | a1[3]);
  a0[2] = 0xac7c; a0[3] = 0xac7d; a0[66] = 0xac7e; a0[67] = 0xac7f;
  a0 += 4;
  a1 = rom + digits;
  for (i = 0; i < 2; i++) {                                 /* 0x31FA, hundreds then tens */
    if (d4 < pw[i] && !shown) continue;
    for (d = 0; d4 >= pw[i]; d++) d4 = (byte)(d4 - pw[i]);
    shown = 1;
    *a0 = (ushort)(0xac00u | a1[d]); a0[64] = (ushort)(0xac00u | a1[d + 10]); a0++;
  }
  *a0 = (ushort)(0xac00u | a1[d4]); a0[64] = (ushort)(0xac00u | a1[d4 + 10]);   /* ones: D4 */
  return 0;
}

/* SIGNATURE MATCHES protos.h. No 68K caller reads D0 after 0x3128 (0x2BE2 reloads). */
uint FUN_00003128(uint p_D0,undefined4 p_D2,undefined4 p_D3)
{
  if (DAT_fffff026 & 1) return p_D0;
  if ((DAT_fffff028 & 1) && !(DAT_fffff028 & 0x14))
    hud_lives(0xFFE000u, *(const byte *)W68PTR(0xFFC06Bu), 0xa400, 0x3282, 0x329a, 0);
  if ((DAT_fffff029 & 1) && !(DAT_fffff029 & 0x14))
    hud_lives(0xFFE080u, *(const byte *)W68PTR(0xFFC46Bu), 0xa600, 0x3282, 0x329a, 0);
  return 0;
}
