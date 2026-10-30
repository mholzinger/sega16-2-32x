/* FUN_0000e42e -- THE 12-BYTE RECORD TEMPLATE COPIER (0xE42E). Hand-derived because its
 * SOURCE is a 68K ROM address and the generated body dereferenced it raw.
 *
 * THE 68K, 0xE42E-0xE43E:
 *     e42e: lea (0,fp),a0          destination = A6, the record being built
 *     e432: move.b (8,fp),d0       save the slot's own +8
 *     e436: moveq #2,d2
 *     e438: move.l (a1)+,(a0)+     x3 with the dbf -- TWELVE BYTES
 *     e43e: move.b d0,(8,fp)       and put +8 back
 *
 * A1 arrives as a `lea (0xeba2,pc),a1` ROM pointer. Kind 5's composite has three
 * templates: 0xEBA2 (handler 0xE474, the master), 0xEBAE and 0xEBBA (handler 0xEA5E,
 * the two parts). The generated body kept A1 raw and read `*p_A1` through it, landing
 * in SH-2 low memory near 0x0000EBA2 rather than the cart -- so the twelve bytes that
 * CARRY THE HANDLER never came out of the program ROM, and 0xE474 / 0xEA5E never
 * appeared on the port where an arcade harness saw them in 53 and 102 samples.
 *
 * Each template address is also 2 MOD 4, and the generated load was a plain aligned
 * `*(undefined4 *)`; LD32U/ST32 are required on the SH-2 even once it is mapped.
 *
 * Same one-line class as hand/FUN_0001a58a.c: the destination was mapped, the ROM
 * source was not. CPORT_ANYNORM reaches neither -- it keys on a function that already
 * carries an A68NORM line, and this one has none.
 *
 * The caller side is the other half, and is a pass, not a hand file: the composite's
 * init re-points A6 along the +0x78 pair link (`movea.w (0x78,fp),fp` at 0xE3CA and
 * 0xE3F4) and Ghidra tracked that into a LOCAL while this function reads the GLOBAL
 * _r_unaff_A6. 22 such sites in 12 functions.
 */
uint FUN_0000e42e(undefined4 *p_A1,undefined4 p_A2)
{
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  byte bVar1;
  short sVar2;
  byte *pbVar3;
  /* THE ONE CHANGE: A1 is a 68K ROM address, map it once on the way in. */
  const undefined4 *src = (const undefined4 *)A68ANY((unsigned)p_A1);
  
  bVar1 = unaff_A6[8];
  sVar2 = 2;
  pbVar3 = unaff_A6;
  do {
    ST32(pbVar3, LD32U(src));        /* 0xE438 move.l (a1)+,(a0)+ -- the template is 2 mod 4 */
    sVar2 = sVar2 + -1;
    pbVar3 = pbVar3 + 4;
    src = src + 1;
  } while (sVar2 != -1);
  unaff_A6[8] = bVar1;
  unaff_A6[0x2f] = 3;
  *(undefined2 *)(unaff_A6 + 0x40) = *(undefined2 *)(unaff_A6 + 0xc);
  *(undefined2 *)(unaff_A6 + 0x44) = *(undefined2 *)(unaff_A6 + 0x10);
  unaff_A6[0x21] = 0;
  unaff_A6[0x4e] = 0;
  unaff_A6[0x4f] = 0;
  unaff_A6[0x22] = 1;
  ST32((unaff_A6 + 0x24), (p_A2));
  unaff_A6[0x23] = 0;
  *unaff_A6 = *unaff_A6 | 8;
  return 0;
}
