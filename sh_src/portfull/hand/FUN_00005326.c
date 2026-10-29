/* HAND: FUN_00005326 -- 68K 0x5336 'cmpal #-11264,a0 ; bccs': the attacker pointer at +0x4C is compared as a
 * NUMBER (a0 >= 0xFFFFD400, i.e. an object at or above 0xD400). The generated body wrapped it in W68 first,
 * comparing a host pointer with 0xFFFFD3FF (always false), so the hit flipped the hero the wrong way.
 * First level-1 divergence after 9d535d94: tick 0x6A8F. Only the range test changes; the deref in the
 * second clause keeps W68. */
uint FUN_00005326(undefined4 p_D6,undefined4 p_D7)
{
  int unaff_A6 = (int)_r_unaff_A6;
  
  *(undefined *)(unaff_A6 + 0x21) = 0;
  *(undefined *)(unaff_A6 + 0x22) = 1;
  *(undefined *)(unaff_A6 + 0x2e) = 0x80;
  if (((DAT_fffff142 != '\x04') && (0xffffd3ff < (uint)(int)(*(short *)(unaff_A6 + 0x4c)))) ||
     (*(ushort *)(unaff_A6 + 0xc) <= *(ushort *)((int)W68(*(short *)(unaff_A6 + 0x4c)) + 0x40))) {
    (*(ushort *)((byte *)&p_D6 + 2)) = -(*(ushort *)((byte *)&p_D6 + 2));
    *(undefined *)(unaff_A6 + 0x2e) = 0;
  }
  *(short *)(unaff_A6 + 0x14) = (*(ushort *)((byte *)&p_D6 + 2));
  *(short *)(unaff_A6 + 0x16) = (*(ushort *)((byte *)&p_D6 + 2));
  *(short *)(unaff_A6 + 0x18) = (*(ushort *)((byte *)&p_D6 + 2));
  *(undefined2 *)(unaff_A6 + 0x1a) = (*(ushort *)((byte *)&p_D7 + 2));
  *(byte *)(unaff_A6 + 0x2c) = *(byte *)(unaff_A6 + 0x2c) & 0xef;
  return 0;
}
