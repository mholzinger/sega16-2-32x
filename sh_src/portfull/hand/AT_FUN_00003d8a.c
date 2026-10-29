/* FUN_00003d8a -- the trail objects' sprite build (0x3D8A), reached only by `jmp (a0)` at
 * 0x6CE4 with A0 = +0x6c (stored by 0x6B34..0x6B9C and 0xBBC2). It is sprite_build_and_cull
 * (0x3DD8) with the X half not halved: `move.w d2,d4 ; btst #7,($2e,a6) ; bne ; sub.w d7,d2`
 * and `add.w d7,d4` instead of `asr #1,d7 ; subx d7,d2 ; ... add.w d7,d4`, joining it at
 * 0x3E1C. Ghidra attributed these bytes to FUN_0000bb2a. Text: the generated
 * sprite_build_and_cull with that one change. */
uint FUN_00003d8a(uint p_D3,undefined4 p_D6,undefined4 p_D7)
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
  sVar3 = sVar7 - ((ushort)(((*(ushort *)((byte *)&p_D6 + 2)) & 1) != 0) + ((short)(*(ushort *)((byte *)&p_D6 + 2)) >> 1));
  sVar7 = ((short)(*(ushort *)((byte *)&p_D6 + 2)) >> 1) + sVar7;
  if (sVar7 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32((W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10)), (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
    return 0;
  }
  sVar6 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  uVar5 = sVar6 - (((unaff_A6[0x2e] & 0x80) != 0) ? 0 : (*(ushort *)((byte *)&p_D7 + 2)));   /* 0x3DCA btst #7,($2e,a6) ; 0x3DD2 sub.w d7,d2: D7 not halved */
  if ((0x1ff < uVar5) || ((ushort)((*(ushort *)((byte *)&p_D7 + 2)) + sVar6) < 0xb9))      /* 0x3E24 add.w d7,d4 with the full D7 */
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
