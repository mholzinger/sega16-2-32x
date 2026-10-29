/* FUN_00006c44 -- the trail spawner callee (0x6C44), generated text with one correction.
 * Its first instruction `lea 0x80(a6),a6` advances A6 to the NEXT record and leaves it
 * there: the caller (0x6B0A / 0x6BA8) calls it five times in a row to fill five records
 * and restores A6 afterwards. The port read _r_unaff_A6 afresh each call and never moved
 * it, so all five writes landed on one record (measured 2026-10-04, demo A tick 0x74:
 * the arcade has slots 9-13, the port slot 9 with the fifth record's values). */
uint FUN_00006c44(undefined4 p_A0,undefined4 p_A4,int p_A5,undefined4 p_D0,undefined4 p_D1,
                 undefined4 p_D2)
{
  int unaff_A6 = (int)_r_unaff_A6;
  _r_unaff_A6 = (byte *)_r_unaff_A6 + 0x80;   /* 0x6C44 lea 0x80(a6),a6: A6 MOVES for the caller's next call and for the palette tail (0x6CB2 jmp 3b2e); the body keeps the +0x80 offsets of the old A6 */
  ushort uVar1;
  char cVar2;
  byte bVar3;
  byte bVar4;
  byte *pbVar5;
  ushort uVar6;
  ushort uVar7;
  uint p_D1_00;
  short sVar8;
  char *pcVar9;
  

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A5 = (int)A68NORM(p_A5);
  pbVar5 = (byte *)(unaff_A6 + 0x80);
  *(undefined *)(unaff_A6 + 0xa0) = (undefined)p_D0;
  pbVar5[0] = 0x80;
  pbVar5[1] = 0;
  ST32((unaff_A6 + 0x82), 0x6cb8u);
  *(undefined *)(unaff_A6 + 0x8b) = 0x35;
  sVar8 = *(short *)(p_A5 + 0x62);
  *(undefined *)(unaff_A6 + 0xae) = *(undefined *)(p_A5 + 0x2e);
  *pbVar5 = *pbVar5 | 1;
  if ((*(byte *)(p_A5 + 0x2e) & 0x80) != 0) {
    sVar8 = *(short *)(p_A5 + 0x60);
    (*(ushort *)((byte *)&p_D1 + 2)) = -(*(ushort *)((byte *)&p_D1 + 2));
    *pbVar5 = *pbVar5 & 0xfe;
  }
  *(short *)(unaff_A6 + 0x8c) = sVar8 - (*(ushort *)((byte *)&p_D1 + 2));
  *(undefined2 *)(unaff_A6 + 0x90) = (*(ushort *)((byte *)&p_D2 + 2));
  ST32((unaff_A6 + 0xec), (p_A0));
  ST32((unaff_A6 + 0xa4), (p_A4));
  *(undefined *)(unaff_A6 + 0x89) = *(undefined *)(p_A5 + 9);
  *(undefined *)(unaff_A6 + 0xaf) = *(undefined *)(p_A5 + 0x2f);
  bVar3 = *(byte *)(unaff_A6 + 0x8b);
  uVar6 = (ushort)bVar3;
  bVar4 = *(byte *)(W68PTR(0xFFF500u) + ((short)uVar6));
  uVar7 = (ushort)bVar4;
  if (0x3e < uVar7) {
    p_D1_00 = (uint)DAT_fffff400;
    if (DAT_fffff400 < 0x3f) {
      DAT_fffff400 = DAT_fffff400 + 1;
      allocate_palette_slot(0xfffff500,(uint)bVar3,p_D1_00);
      build_palette_upload_queue();
      return 0;
    }
    if ((char)DAT_fffff401 < '\0') {
      *(undefined *)(unaff_A6 + 0x8a) = 0;
      return 0;
    }
    allocate_palette_slot(0xfffff500,(uint)bVar3,(uint)DAT_fffff401);
    build_palette_upload_queue();
    return 0;
  }
  *(byte *)(unaff_A6 + 0x8a) = bVar4;
  *(byte *)(W68PTR(0xFFF500u) + ((short)uVar6)) = bVar4;
  uVar1 = *(ushort *)(W68PTR(0xFFF480u) + ((short)(uVar7 << 1)));
  if ((-1 < (short)uVar1) && (uVar1 != uVar6)) {
    *(undefined *)(W68PTR(0xFFF500u) + ((short)uVar1)) = 0xff;
  }
  *(ushort *)(W68PTR(0xFFF480u) + ((short)(uVar7 << 1))) = uVar6;
  *(char *)(W68PTR(0xFFF440u) + ((short)uVar7)) = *(char *)(W68PTR(0xFFF440u) + ((short)uVar7)) + '\x01';
  if (bVar4 != DAT_fffff401) {
    return 0;
  }
  sVar8 = 0x3e;
  pcVar9 = (char *)(W68PTR(0xFFF441u) + ((short)uVar7));
  while ((uVar7 != 0x3e && (sVar8 = sVar8 + -1, sVar8 != -1))) {
LAB_00003bac:
    uVar7 = uVar7 + 1;
    cVar2 = *pcVar9;
    pcVar9 = pcVar9 + 1;
    if (cVar2 == '\0') {
LAB_00003bc8:
      DAT_fffff401 = (char)uVar7;
      return 0;
    }
  }
  if (uVar7 == 0x3e) {
    pcVar9 = (char *)&DAT_fffff440;
    uVar7 = 0xffff;
    sVar8 = sVar8 + -1;
    if (sVar8 != -1) goto LAB_00003bac;
  }
  uVar7 = 0xffff;
  goto LAB_00003bc8;
}
