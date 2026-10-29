/* FUN_00010cd6 -- the flame spawner (0x10CD6): the generated text with one correction.
 * Ghidra read the immediate 0xF808BC00 (`move.l #$F808BC00,0x50(a6)`: the sprite
 * descriptor the flame handler reads back at +0x50) as the address of a symbol, and the
 * port stored that symbol's SH-2 address: measured 2026-10-04, the port's flame record
 * held 83 48 BE 40 at +0x50 where the arcade holds F8 08 BC 00. */
uint FUN_00010cd6()
{
  undefined2 *unaff_A5 = (undefined2 *)_r_unaff_A5;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  ushort uVar1;
  char cVar2;
  byte bVar3;
  ushort uVar4;
  byte bVar5;
  ushort uVar6;
  uint p_D1;
  short sVar7;
  char *pcVar8;
  byte *pbVar9;
  undefined4 *puVar10;
  bool bVar11;
  
  if ((unaff_A6[0x20] & 0x80) == 0) {
    bVar5 = DAT_fffff15d + 4;
  }
  else {
    bVar5 = DAT_fffff15d + 2;
  }
  if (bVar5 < 0xb) {
    bVar11 = false;
    DAT_fffff15d = bVar5;
    bVar11 = FUN_0000e032();   /* its carry: cport_prep.py */
    unaff_A5 = (typeof(unaff_A5))_r_unaff_A5;   /* A5 moved by the callee: cport_prep.py */
    if (bVar11) {
      *(short *)(unaff_A6 + 0x78) = (short) A68OF16(unaff_A5);
      *unaff_A5 = 0x8000;
      *(undefined *)((int)unaff_A5 + 0x7f) = 0;
      unaff_A5[0x3c] = 0xffff;
      unaff_A5[0x1e] = (short) A68OF16(unaff_A6);
      ST32((unaff_A5 + 1), 0x10d1eu);
      bVar5 = unaff_A6[8];
      sVar7 = 2;
      pbVar9 = unaff_A6;
      puVar10 = (undefined4 *)((char *)ROM_BASE + 0x113d4);   /* ROM template: cport_prep.py */
      do {
        ST32(pbVar9, LD32U(puVar10));   /* 0x113D4 template: cport_prep.py */
        sVar7 = sVar7 + -1;
        pbVar9 = pbVar9 + 4;
        puVar10 = puVar10 + 1;
      } while (sVar7 != -1);
      unaff_A6[8] = bVar5;
      unaff_A6[0x2f] = 2;
      unaff_A6[0x4a] = 0x32;
      if ((unaff_A6[0x20] & 0x80) != 0) {
        unaff_A6[0xb] = 0x67;
        unaff_A6[0x4a] = 0x3c;
      }
      ST32((unaff_A6 + 0x50), 0xf808bc00u);   /* 0x10D90 move.l #$F808BC00,0x50(a6): an immediate, not an address */
      unaff_A6[0x14] = 0xff;
      unaff_A6[0x15] = 0;
      unaff_A6[0x1c] = 0;
      unaff_A6[0x1d] = 0x80;
      unaff_A6[0x1e] = 8;
      unaff_A6[0x1f] = 0;
      unaff_A6[0x21] = 0;
      unaff_A6[0x4e] = 0;
      unaff_A6[0x4f] = 0;
      unaff_A6[0x22] = 1;
      ST32((unaff_A6 + 0x24), 0x113f8u);
      if ((unaff_A6[0x20] & 1) != 0) {
        unaff_A6[0x2e] = 0x80;
      }
      *unaff_A6 = *unaff_A6 | 8;
      bVar5 = unaff_A6[0xb];
      uVar4 = (ushort)bVar5;
      bVar3 = *(byte *)(W68PTR(0xFFF500u) + ((short)uVar4));
      uVar6 = (ushort)bVar3;
      if (0x3e < uVar6) {
        p_D1 = (uint)DAT_fffff400;
        if (DAT_fffff400 < 0x3f) {
          DAT_fffff400 = DAT_fffff400 + 1;
          allocate_palette_slot(0xfffff500,(uint)bVar5,p_D1);
          build_palette_upload_queue();
          return 0;
        }
        if ((char)DAT_fffff401 < '\0') {
          unaff_A6[10] = 0;
          return 0;
        }
        allocate_palette_slot(0xfffff500,(uint)bVar5,(uint)DAT_fffff401);
        build_palette_upload_queue();
        return 0;
      }
      unaff_A6[10] = bVar3;
      *(byte *)(W68PTR(0xFFF500u) + ((short)uVar4)) = bVar3;
      uVar1 = *(ushort *)(W68PTR(0xFFF480u) + ((short)(uVar6 << 1)));
      if ((-1 < (short)uVar1) && (uVar1 != uVar4)) {
        *(undefined *)(W68PTR(0xFFF500u) + ((short)uVar1)) = 0xff;
      }
      *(ushort *)(W68PTR(0xFFF480u) + ((short)(uVar6 << 1))) = uVar4;
      *(char *)(W68PTR(0xFFF440u) + ((short)uVar6)) = *(char *)(W68PTR(0xFFF440u) + ((short)uVar6)) + '\x01';
      if (bVar3 != DAT_fffff401) {
        return 0;
      }
      sVar7 = 0x3e;
      pcVar8 = (char *)(W68PTR(0xFFF441u) + ((short)uVar6));
      while ((uVar6 != 0x3e && (sVar7 = sVar7 + -1, sVar7 != -1))) {
LAB_00003bac:
        uVar6 = uVar6 + 1;
        cVar2 = *pcVar8;
        pcVar8 = pcVar8 + 1;
        if (cVar2 == '\0') {
LAB_00003bc8:
          DAT_fffff401 = (char)uVar6;
          return 0;
        }
      }
      if (uVar6 == 0x3e) {
        pcVar8 = (char *)&DAT_fffff440;
        uVar6 = 0xffff;
        sVar7 = sVar7 + -1;
        if (sVar7 != -1) goto LAB_00003bac;
      }
      uVar6 = 0xffff;
      goto LAB_00003bc8;
    }
    FUN_00010d06();
  }
  unaff_A6[0] = 0;
  unaff_A6[1] = 0;
  return 0;
}
