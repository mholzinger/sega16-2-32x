/* FUN_0000ea5e -- generated text with one correction: the parent's script pointer is read
 * through the 16-bit parent link (+0x3c), which has to be mapped (W68) before +0x24 is
 * dereferenced; the generated text read an SH-2 address 0xFFFFCxxx. */
uint FUN_0000ea5e(uint p_D3,undefined4 p_D6,undefined4 p_D7)
{
  uint extraout_A1 = (uint)_r_extraout_A1;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  byte *pbVar1;
  byte bVar2;
  undefined uVar3;
  short sVar4;
  short sVar5;
  undefined4 p_D1;
  ushort uVar6;
  short sVar7;
  short sVar8;
  ushort uVar9;
  undefined *puVar10;
  undefined4 p_A0;
  int iVar11;
  uint uVar12;
  bool bVar13;
  undefined8 uVar14;
  
  sVar8 = *(short *)(unaff_A6 + 0x3c);
  bVar13 = *(short *)(unaff_A6 + 0x78) != -1;
  if (*(short *)(unaff_A6 + 0x78) == -1) {
    sVar8 = *(short *)(unaff_A6 + 0x7a);
  }
  p_A0 = FUN_000100b0((short int *)((int)sVar8));
  bVar13 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
  if (bVar13) {
    unaff_A6[0] = 0;
    unaff_A6[1] = 0;
    release_palette_slot();
    goto hide_object_sprite;
  }
  if (unaff_A6[0x23] == 6) {
    restore_position(p_A0,p_D1);
    unaff_A6[0x3f] = unaff_A6[0x3f] + 1;
    if (0x1d < unaff_A6[0x3f]) {
      *(short *)(unaff_A6 + 0x14) = -*(short *)(unaff_A6 + 0x14);
      unaff_A6[0x3f] = 0;
      unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
    }
    integrate_position();
  }
  else {
    uVar14 = FUN_0000e20c(p_D1);
    iVar11 = (int)((ulonglong)uVar14 >> 0x20);
    if (*(char *)((char *)P68(iVar11 + 0x23)) == '\x05') {
      if (unaff_A6[0x23] == 5) {
        if (LD32U(iVar11 + 0x24) != 0xec98u) goto LAB_0000eb36;
        iVar11 = LD32S((unaff_A6 + 0x24)) + -4;
      }
      else {
        unaff_A6[0x23] = 5;
        unaff_A6[0x21] = 0;
        unaff_A6[0x22] = 1;
LAB_0000eb2c:
        iVar11 = LD32S((unaff_A6 + 0x24));
      }
    }
    else {
      if (*(char *)((char *)P68(iVar11 + 0x23)) != '\x06') {
        pbVar1 = (byte *)(iVar11 + 0x3e);
        bVar2 = *pbVar1;
        *pbVar1 = *pbVar1 | 0x80;
        if (bVar2 == 0) {
          unaff_A6[0x23] = 0;
          unaff_A6[0x21] = 0;
          unaff_A6[0x22] = 1;
          if (*(short *)(unaff_A6 + 0x78) != -1) {
            *(undefined *)((char *)P68(iVar11 + 0x3e)) = 0;
          }
          goto LAB_0000eb2c;
        }
        goto LAB_0000eb36;
      }
      unaff_A6[0x23] = 6;
      unaff_A6[0x3f] = 0;
      unaff_A6[0x14] = 0xfe;
      unaff_A6[0x15] = 0x80;
      unaff_A6[0x1a] = 0xfe;
      unaff_A6[0x1b] = 0x80;
      iVar11 = LD32S((unaff_A6 + 0x24)) + -2;
    }
    ST32((unaff_A6 + 0x24), ((int)*(short *)A68PTR(iVar11 + -2) + iVar11 + -2));
  }
LAB_0000eb36:
  animate_variant();
  if (LD32U((char *)W68(*(short *)(unaff_A6 + 0x3c)) + 0x24) == 0xec98u) {
    *(short *)(unaff_A6 + 0x10) =
         *(short *)((short)((ushort)*(byte *)((char *)W68(*(short *)(unaff_A6 + 0x3c)) + 0x21) * 2) + 0xeda6) +
         *(short *)(unaff_A6 + 0x10);
  }
  order_list_insert();
  uVar14 = sprite_frame_lookup();
  extraout_A1 = (uint)uVar14;
  uVar14 = zoom_scale_lookup((int)((ulonglong)uVar14 >> 0x20),p_D3);
  p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */
  sVar8 = (_DAT_fffff0fa + *(short *)(unaff_A6 + 0x10) + -0x1000) - _DAT_fffff128;
  sVar4 = sVar8 - ((ushort)(((*(ushort *)((byte *)&p_D6 + 2)) & 1) != 0) + ((short)(*(ushort *)((byte *)&p_D6 + 2)) >> 1));
  sVar8 = ((short)(*(ushort *)((byte *)&p_D6 + 2)) >> 1) + sVar8;
  if (sVar8 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32((W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10)), (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
    return 0;
  }
  sVar7 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  uVar6 = sVar7 - ((ushort)(((*(ushort *)((byte *)&p_D7 + 2)) & 1) != 0) + ((short)(*(ushort *)((byte *)&p_D7 + 2)) >> 1));
  if ((0x1ff < uVar6) || ((ushort)(((short)(*(ushort *)((byte *)&p_D7 + 2)) >> 1) + sVar7) < 0xb9))
  goto hide_object_sprite;
  bVar2 = *unaff_A6;
  if ((DAT_fffff018 & 0x40) != 0) {
    bVar2 = bVar2 ^ 2;
  }
  uVar9 = 0;
  sVar7 = (short)p_D3;
  uVar12 = extraout_A1;
  if ((bVar2 & 1) != 0) {
    uVar12 = (int)sVar7 + (extraout_A1 - 2);
    uVar9 = 0x100;
  }
  if (0xdf < sVar4) goto hide_object_sprite;
  sVar5 = sVar4;
  if ((sVar4 < 0) && (sVar5 = 0, (DAT_fffff018 & 0x40) == 0)) {
    if ((bVar2 & 2) == 0) {
      uVar12 = (int)(short)(-sVar4 * sVar7) + uVar12;
    }
    else {
      uVar12 = uVar12 - (int)(short)(-sVar4 * sVar7);
    }
  }
  sVar4 = sVar8;
  if ((short)(sVar8 + -0xe0) < 0) {
LAB_00003e9c:
    uVar3 = (undefined)sVar4;
    if ((bVar2 & 2) == 0) goto LAB_00003eb2;
  }
  else {
    sVar4 = 0xe0;
    uVar3 = 0xe0;
    if ((DAT_fffff018 & 0x40) == 0) goto LAB_00003e9c;
    sVar8 = (sVar8 + -0xe0) * sVar7;
    if ((bVar2 & 2) == 0) {
      uVar12 = (int)sVar8 + uVar12;
      goto LAB_00003eb2;
    }
    uVar12 = uVar12 - (int)sVar8;
    uVar3 = 0xe0;
  }
  uVar12 = (int)*(short *)((ulonglong)uVar14 >> 0x20) + uVar12;
  p_D3 = (uint)(-sVar7 & 0xff);
LAB_00003eb2:
  *unaff_A6 = *unaff_A6 | 8;
  puVar10 = (undefined *)(W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10));
  *puVar10 = uVar3;
  puVar10[1] = (char)sVar5;
  *(ushort *)(puVar10 + 2) = uVar6;
  *(ushort *)(puVar10 + 4) = uVar9 | (ushort)CONCAT31((int3)(p_D3 >> 8),(char)p_D3 >> 1);
  *(short *)(puVar10 + 6) = (short)(uVar12 >> 1);
  puVar10[8] = (byte)((uVar12 >> 1) >> 0x10) | 0xf0;
  puVar10[9] = unaff_A6[10] | unaff_A6[9] << 6;
  uVar6 = (ushort)unaff_A6[0x4e];
  if (unaff_A6[0x4e] != 0) {
    uVar6 = (uVar6 & 0x1f) * 0x21;
  }
  *(ushort *)(puVar10 + 10) = uVar6;
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
  return 0;
}
