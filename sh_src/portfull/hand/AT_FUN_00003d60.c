/* FUN_00003d60 -- the trail objects' sprite build (0x3D60), reached only by `jmp (a0)` at
 * 0x6CE4 with A0 = +0x6c (stored by 0x6BD0..0x6C38). It is sprite_build_and_cull (0x3DD8)
 * with the Y half not halved: `move.w d0,d1 ; add.w d6,d0` instead of `asr #1,d6 ; subx
 * d6,d1 ; add.w d6,d0`, then joins it at 0x3E02. Ghidra made no function here, so the
 * dispatcher counted the call as unknown and the trail never drew (2026-10-04, demo A
 * tick 0x74: +0x00 bit 3 clear, order list 0xFFED09.. = 0xFF). Text: the generated
 * sprite_build_and_cull with that one change. */
uint FUN_00003d60(uint p_D3,undefined4 p_D6,undefined4 p_D7)
{
  uint extraout_A1 = (uint)_r_extraout_A1;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  byte bVar1;
  undefined uVar2;
  short sVar3;
  short sVar4;
  ushort uVar5;
  short sVar6;
  short sVar7;
  ushort uVar8;
  undefined *puVar9;
  uint uVar10;
  undefined8 uVar11;
  
  order_list_insert();
  uVar11 = sprite_frame_lookup();
  extraout_A1 = (uint)uVar11;
  uVar11 = zoom_scale_lookup((int)((ulonglong)uVar11 >> 0x20),p_D3);
  p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */
  sVar7 = (_DAT_fffff0fa + *(short *)(unaff_A6 + 0x10) + -0x1000) - _DAT_fffff128;
  sVar3 = sVar7;                                                         /* 0x3D7C move.w d0,d1: D6 not halved */
  sVar7 = (short)(*(ushort *)((byte *)&p_D6 + 2)) + sVar7;   /* 0x3D7E add.w d6,d0 */
  if (sVar7 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32((W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10)), (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
    return 0;
  }
  sVar6 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  uVar5 = sVar6 - ((ushort)(((*(ushort *)((byte *)&p_D7 + 2)) & 1) != 0) + ((short)(*(ushort *)((byte *)&p_D7 + 2)) >> 1));
  if ((0x1ff < uVar5) || ((ushort)(((short)(*(ushort *)((byte *)&p_D7 + 2)) >> 1) + sVar6) < 0xb9))
  goto hide_object_sprite;
  bVar1 = *unaff_A6;
  if ((DAT_fffff018 & 0x40) != 0) {
    bVar1 = bVar1 ^ 2;
  }
  uVar8 = 0;
  sVar6 = (short)p_D3;
  uVar10 = extraout_A1;
  if ((bVar1 & 1) != 0) {
    uVar10 = (int)sVar6 + (extraout_A1 - 2);
    uVar8 = 0x100;
  }
  if (0xdf < sVar3) goto hide_object_sprite;
  sVar4 = sVar3;
  if ((sVar3 < 0) && (sVar4 = 0, (DAT_fffff018 & 0x40) == 0)) {
    if ((bVar1 & 2) == 0) {
      uVar10 = (int)(short)(-sVar3 * sVar6) + uVar10;
    }
    else {
      uVar10 = uVar10 - (int)(short)(-sVar3 * sVar6);
    }
  }
  sVar3 = sVar7;
  if ((short)(sVar7 + -0xe0) < 0) {
LAB_00003e9c:
    uVar2 = (undefined)sVar3;
    if ((bVar1 & 2) == 0) goto LAB_00003eb2;
  }
  else {
    sVar3 = 0xe0;
    uVar2 = 0xe0;
    if ((DAT_fffff018 & 0x40) == 0) goto LAB_00003e9c;
    sVar7 = (sVar7 + -0xe0) * sVar6;
    if ((bVar1 & 2) == 0) {
      uVar10 = (int)sVar7 + uVar10;
      goto LAB_00003eb2;
    }
    uVar10 = uVar10 - (int)sVar7;
    uVar2 = 0xe0;
  }
  uVar10 = (int)*(short *)((ulonglong)uVar11 >> 0x20) + uVar10;
  p_D3 = (uint)(-sVar6 & 0xff);
LAB_00003eb2:
  *unaff_A6 = *unaff_A6 | 8;
  puVar9 = (undefined *)(W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10));
  *puVar9 = uVar2;
  puVar9[1] = (char)sVar4;
  *(ushort *)(puVar9 + 2) = uVar5;
  *(ushort *)(puVar9 + 4) = uVar8 | (ushort)CONCAT31((int3)(p_D3 >> 8),(char)p_D3 >> 1);
  *(short *)(puVar9 + 6) = (short)(uVar10 >> 1);
  puVar9[8] = (byte)((uVar10 >> 1) >> 0x10) | 0xf0;
  puVar9[9] = unaff_A6[10] | unaff_A6[9] << 6;
  uVar5 = (ushort)unaff_A6[0x4e];
  if (unaff_A6[0x4e] != 0) {
    uVar5 = (uVar5 & 0x1f) * 0x21;
  }
  *(ushort *)(puVar9 + 10) = uVar5;
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
  return 0;
}
