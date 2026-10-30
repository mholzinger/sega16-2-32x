/* HAND: FUN_000143dc. Today's generated body (cport_prep at 8c29d7ef) with ONE change at the second
 * FUN_000145a2 call: A6 = the record FUN_0000e032 claimed (p_A5), as 68K 0x14598-0x1459E does. */
uint FUN_000143dc(undefined4 p_A0,undefined2 *p_A5,undefined4 p_D1,uint p_D3,uint p_D6,uint p_D7)
{
  uint extraout_A1 = (uint)_r_extraout_A1;
  undefined4 extraout_D0 = (undefined4)_r_extraout_D0;
  undefined4 extraout_D0_00 = (undefined4)_r_extraout_D0_00;
  undefined4 extraout_D0_01 = (undefined4)_r_extraout_D0_01;
  undefined4 extraout_D0_02 = (undefined4)_r_extraout_D0_02;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  byte bVar1;
  byte bVar2;
  short sVar3;
  byte bVar5;
  undefined4 uVar4;
  short sVar6;
  undefined2 uVar7;
  undefined4 p_D1_00;
  ushort uVar8;
  short sVar9;
  undefined3 uVar10;
  short sVar11;
  ushort uVar12;
  undefined *puVar13;
  undefined4 uVar14;
  uint uVar15;
  int iVar16;
  short *psVar17;
  undefined uVar18;
  bool bVar19;
  undefined8 uVar20;
  
  restore_position(p_A0,p_D1);
  uVar4 = extraout_D0;
  if ((*unaff_A6 & 8) == 0) {
    unaff_A6[0x4b] = 0;
    if ((*(ushort *)(unaff_A6 + 0x40) <= (ushort)((ushort)(_DAT_fffff132 >> 0x10) - 0x50U)) ||
       (uVar4 = CONCAT22((short)((uint)extraout_D0 >> 0x10),(ushort)(_DAT_fffff132 >> 0x10) + 400U),
       (ushort)((ushort)(_DAT_fffff132 >> 0x10) + 400U) < *(ushort *)(unaff_A6 + 0x40))) {
      if (unaff_A6[0x3e] == 0) {
        DAT_fffff15d = DAT_fffff15d + -2;
        tick_kill_counters();
      }
      unaff_A6[0] = 0;
      unaff_A6[1] = 0;
      release_palette_slot();
      goto hide_object_sprite;
    }
  }
  if (unaff_A6[0x23] == 5) {
override_jmp_000146ee_case_10:
    if (unaff_A6[0x2c] != 0) {
LAB_00014528:
      sound_enqueue(CONCAT31((int3)((uint)uVar4 >> 8),0xba));
      unaff_A6[0x3e] = 0;
      unaff_A6[0x3f] = 0;
      uVar18 = 0;
      FUN_000145a2(p_D3,p_D6,p_D7);
      uVar18 = FUN_0000e032();   /* its carry: cport_prep.py */
      if (uVar18) p_A5 = (typeof(p_A5))_r_unaff_A5;   /* A5 moved by the callee, ONLY when it found one: cport_prep.py */
      if ((bool)uVar18) {
        *(undefined *)((int)p_A5 + 0x3f) = 1;
        *p_A5 = 0x8000;
        p_A5[0x3c] = 0xffff;
        p_A5[6] = *(undefined2 *)(unaff_A6 + 0xc);
        p_A5[8] = *(undefined2 *)(unaff_A6 + 0x10);
        p_A5[0xe] = 0x80;
        p_A5[0xf] = 0x800;
        *(undefined *)(p_A5 + 0x1f) = 0xff;
        {   /* 0x14598-0x1459E: move.w a6,-(sp); movea.w a5,a6; bsr 145a2; movea.w (sp)+,a6. The init
             * runs on the record e032 just claimed. Ghidra dropped the movea, so the port initialised
             * the PARENT a second time and left the claimed slot active (0x8000) with its previous
             * owner's stale handler: it ran that dead enemy's death path and decremented F154 again.
             * Measured on the level-5 fuzz (power 3, seed 27): slot 18 freed at f3140 and again at
             * f3222 with one counted spawn -> F154 0 -> FF, the FPGA level-5 camera lock. */
            byte *_parent = (byte *)_r_unaff_A6;
            _r_unaff_A6 = (byte *)p_A5;
            FUN_000145a2(p_D3,p_D6,p_D7);
            _r_unaff_A6 = _parent;
        }
  _port_reg[3] = (uint)p_D3;
  _port_reg[6] = (uint)p_D6;
        return 0;
      }
  _port_reg[3] = (uint)p_D3;
  _port_reg[6] = (uint)p_D6;
      return 0;
    }
LAB_000144f4:
    sVar11 = (short)p_D6;
    clamp_y_velocity();
    integrate_position();
    hitbox_b_expand();
    unaff_A6[0x2d] = unaff_A6[0x2c];
    unaff_A6[0x2c] = 0;
    collide_box_b();
    floor_collide();
    animate_variant();
  }
  else {
    bVar5 = unaff_A6[0x4b];
    uVar4 = CONCAT31((int3)((uint)uVar4 >> 8),bVar5);
    if (bVar5 == 0) {
      if ((unaff_A6[0x23] == 0) || ((unaff_A6[0x21] == 0 && (unaff_A6[0x22] == 1)))) {
        uVar14 = FUN_0000e266();
        p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
        extraout_D0_00 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
        unaff_A6[0x7c] = 0;
        unaff_A6[0x7d] = 0;
        uVar4 = extraout_D0_00;
        if ((ushort)extraout_D0_00 == 0) {
LAB_000146d6:
          if (unaff_A6[0x3f] == 0) {
            iVar16 = 0x148a2;
          }
          else {
LAB_000146d0:
            iVar16 = 0x14898;
          }
          goto LAB_000146e2;
        }
        bVar19 = false;
        uVar8 = ~(ushort)extraout_D0_00;
        uVar4 = CONCAT22((short)((uint)extraout_D0_00 >> 0x10),uVar8);
        if ((uVar8 == 0) || (uVar14 = FUN_0000e2dc(uVar14), uVar4 = extraout_D0_01, bVar19))
        goto LAB_000146d6;
        bVar5 = unaff_A6[0x2c];
        uVar10 = (undefined3)(p_D6 >> 8);
        bVar19 = false;
        p_D6 = CONCAT31(uVar10,bVar5) & 0xffffffc0;
        if ((bVar5 & 0xc0) == 0) {
          FUN_0000e23c(uVar14,p_D1_00);
          bVar19 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
          extraout_D0_02 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
          if (bVar19) {
            unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
          }
          uVar4 = extraout_D0_02;
          if ((ushort)extraout_D0_02 < 0xa0) {
            if (0x6f < (ushort)extraout_D0_02) goto LAB_000146d6;
            unaff_A6[0x3f] = 0xff;
          }
          goto LAB_000146d0;
        }
        p_D6 = CONCAT31(uVar10,bVar5) & 0xffffff80;
        if ((bVar5 & 0x80) == 0) {
          if ((unaff_A6[0x2e] & 0x80) == 0) {
LAB_0001469e:
            unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
          }
        }
        else if ((unaff_A6[0x2e] & 0x80) != 0) goto LAB_0001469e;
        if (unaff_A6[0x23] != 0) goto LAB_000146d6;
        unaff_A6[0x18] = 3;
        unaff_A6[0x19] = 0;
        unaff_A6[0x16] = 0;
        unaff_A6[0x17] = 0x18;
        unaff_A6[0x23] = 0;
        ST32((unaff_A6 + 0x24), 0x148c4u);
        if ((unaff_A6[0x2e] & 0x80) != 0) {
          *(short *)(unaff_A6 + 0x16) = -*(short *)(unaff_A6 + 0x16);
          *(short *)(unaff_A6 + 0x18) = -*(short *)(unaff_A6 + 0x18);
        }
      }
      else {
        iVar16 = 0x148ac;
LAB_000146e2:
        psVar17 = (short *)((short)((ushort)unaff_A6[0x23] * 2) + iVar16);
                    /* WARNING: Switch is manually overridden */
        switch((int)*(short *)A68PTR(psVar17) + (int)psVar17) {
        case 0x144ee:
          goto override_jmp_000146ee_case_10;
        case 0x146f0:
          if (*(short *)(unaff_A6 + 0x14) == 0) goto override_jmp_000146ee_case_2;
          unaff_A6[0x16] = 0xff;
          unaff_A6[0x17] = 0xe0;
          unaff_A6[0x18] = 0;
          unaff_A6[0x19] = 0;
          if ((unaff_A6[0x2e] & 0x80) != 0) {
            *(short *)(unaff_A6 + 0x16) = -*(short *)(unaff_A6 + 0x16);
          }
          break;
        case 0x146fc:
          unaff_A6[0x14] = 0;
          unaff_A6[0x15] = 0x80;
          unaff_A6[0x18] = 0;
          unaff_A6[0x19] = 0x80;
          unaff_A6[0x23] = 3;
          unaff_A6[0x21] = 0;
          unaff_A6[0x22] = 1;
          ST32((unaff_A6 + 0x24), 0x149a6u);
          if ((unaff_A6[0x2e] & 0x80) != 0) {
            *(short *)(unaff_A6 + 0x14) = -*(short *)(unaff_A6 + 0x14);
            *(short *)(unaff_A6 + 0x18) = -*(short *)(unaff_A6 + 0x18);
          }
          break;
        case 0x14736:
override_jmp_000146ee_case_2:
          unaff_A6[0x14] = 0;
          unaff_A6[0x15] = 0;
          unaff_A6[0x16] = 0;
          unaff_A6[0x17] = 0;
          unaff_A6[0x18] = 0;
          unaff_A6[0x19] = 0;
          unaff_A6[0x21] = 0;
          unaff_A6[0x23] = 1;
          unaff_A6[0x22] = 1;
          ST32((unaff_A6 + 0x24), 0x1494au);
          break;
        case 0x147e4:
          if ((unaff_A6[0x2c] & 0x10) == 0) {
            unaff_A6[0x22] = unaff_A6[0x22] + 1;
          }
        }
      }
    }
    else {
      unaff_A6[0x16] = 0;
      unaff_A6[0x17] = 0;
      unaff_A6[0x4b] = 0;
      unaff_A6[0x1a] = 0xfa;
      unaff_A6[0x1b] = 0;
      uVar7 = 0x280;
      unaff_A6[0x2e] = 0x80;
      if (*(ushort *)(unaff_A6 + 0x40) <= *(ushort *)((char *)W68(*(short *)(unaff_A6 + 0x4c)) + 0x40)) {
        unaff_A6[0x2e] = 0;
        uVar7 = 0xfd80;
      }
      *(undefined2 *)(unaff_A6 + 0x14) = uVar7;
      *(undefined2 *)(unaff_A6 + 0x18) = uVar7;
      unaff_A6[0x1a] = 0xfa;
      unaff_A6[0x1b] = 0;
      bVar5 = bVar5 & 0xfc;
      if (bVar5 != 0) {
        bVar1 = unaff_A6[0x4a];
        bVar2 = bVar1 - bVar5;
        unaff_A6[0x4a] = bVar2;
        if ((bVar1 < bVar5) || (bVar2 == 0)) {
          unaff_A6[0x23] = 5;
          p_A5 = (undefined2 *)(int)*(short *)((char *)W68(*(short *)(unaff_A6 + 0x4c)) + 0x3c);
          _DAT_fffff032 = 0x100;
          uVar4 = FUN_00003706((int)(p_A5));
          if ((unaff_A6[8] & 1) != 0) goto LAB_00014528;
          sound_enqueue(CONCAT31((int3)((uint)uVar4 >> 8),0xb4));
          unaff_A6[0x4a] = 0;
          ST32((unaff_A6 + 0x24), 0x14a5au);
          unaff_A6[0x21] = 0;
          unaff_A6[0x22] = 1;
          unaff_A6[0x1a] = 0xfa;
          unaff_A6[0x1b] = 0;
          goto LAB_000144f4;
        }
        sound_enqueue(0xffffffb4);
      }
      unaff_A6[0x23] = 4;
      unaff_A6[0x21] = 0;
      unaff_A6[0x22] = 1;
      ST32((unaff_A6 + 0x24), 0x14a00u);
    }
    sVar11 = (short)p_D6;
    clamp_x_velocity();
    clamp_y_velocity();
    integrate_position();
    animation_peek_next();
    hitbox_b_expand();
    unaff_A6[0x2d] = unaff_A6[0x2c];
    unaff_A6[0x2c] = 0;
    collide_box_b();
    floor_collide();
    FUN_0000669c();
    hitbox_a_expand();
    animation_frame_fetch();
    walk_player_slots();
  }
  if ((unaff_A6[0x2c] & 0x10) != 0) {
    bVar5 = 0;
    if ((0x1087 < *(ushort *)(unaff_A6 + 0x10)) &&
       (bVar5 = 1, 0x10c7 < *(ushort *)(unaff_A6 + 0x10))) {
      bVar5 = 2;
    }
    unaff_A6[0x2f] = bVar5;
  }
  order_list_insert();
  uVar20 = sprite_frame_lookup();
  extraout_A1 = (uint)uVar20;
  uVar20 = zoom_scale_lookup((int)((ulonglong)uVar20 >> 0x20),p_D3);
  p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */
  sVar3 = (_DAT_fffff0fa + *(short *)(unaff_A6 + 0x10) + -0x1000) - _DAT_fffff128;
  sVar11 = sVar3 - (short)p_D6;   /* 0x3D46 subw d6,d1: the height the zoom lookup just returned, not sVar11: cport_prep.py */
  if (sVar3 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32(SPRREC((ushort)unaff_A6[8])   /* lsl #4 ; addw #$F800 ; movea.w: a 16-bit sum, cport_prep.py */, (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3;
  _port_reg[6] = (uint)p_D6;
    return 0;
  }
  sVar9 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  sVar6 = (short)p_D7 >> 1;
  uVar8 = sVar9 - ((ushort)((p_D7 & 1) != 0) + sVar6);
  if ((0x1ff < uVar8) || ((ushort)(sVar6 + sVar9) < 0xb9)) goto hide_object_sprite;
  bVar5 = *unaff_A6;
  if ((DAT_fffff018 & 0x40) != 0) {
    bVar5 = bVar5 ^ 2;
  }
  uVar12 = 0;
  sVar9 = (short)p_D3;
  uVar15 = extraout_A1;
  if ((bVar5 & 1) != 0) {
    uVar15 = (int)sVar9 + (extraout_A1 - 2);
    uVar12 = 0x100;
  }
  if (0xdf < sVar11) goto hide_object_sprite;
  sVar6 = sVar11;
  if ((sVar11 < 0) && (sVar6 = 0, (DAT_fffff018 & 0x40) == 0)) {
    if ((bVar5 & 2) == 0) {
      uVar15 = (int)(short)(-sVar11 * sVar9) + uVar15;
    }
    else {
      uVar15 = uVar15 - (int)(short)(-sVar11 * sVar9);
    }
  }
  sVar11 = sVar3;
  if ((short)(sVar3 + -0xe0) < 0) {
LAB_00003e9c:
    uVar18 = (undefined)sVar11;
    if ((bVar5 & 2) == 0) goto LAB_00003eb2;
  }
  else {
    sVar11 = 0xe0;
    uVar18 = 0xe0;
    if ((DAT_fffff018 & 0x40) == 0) goto LAB_00003e9c;
    sVar11 = (sVar3 + -0xe0) * sVar9;
    if ((bVar5 & 2) == 0) {
      uVar15 = (int)sVar11 + uVar15;
      goto LAB_00003eb2;
    }
    uVar15 = uVar15 - (int)sVar11;
    uVar18 = 0xe0;
  }
  uVar15 = (int)*(short *)((ulonglong)uVar20 >> 0x20) + uVar15;
  p_D3 = (uint)(-sVar9 & 0xff);
LAB_00003eb2:
  *unaff_A6 = *unaff_A6 | 8;
  puVar13 = (undefined *)SPRREC((ushort)unaff_A6[8])   /* lsl #4 ; addw #$F800 ; movea.w: a 16-bit sum, cport_prep.py */;
  *puVar13 = uVar18;
  puVar13[1] = (char)sVar6;
  *(ushort *)(puVar13 + 2) = uVar8;
  *(ushort *)(puVar13 + 4) = uVar12 | (ushort)CONCAT31((int3)(p_D3 >> 8),(char)p_D3 >> 1);
  *(short *)(puVar13 + 6) = (short)(uVar15 >> 1);
  puVar13[8] = (byte)((uVar15 >> 1) >> 0x10) | 0xf0;
  puVar13[9] = unaff_A6[10] | unaff_A6[9] << 6;
  uVar8 = (ushort)unaff_A6[0x4e];
  if (unaff_A6[0x4e] != 0) {
    uVar8 = (uVar8 & 0x1f) * 0x21;
  }
  *(ushort *)(puVar13 + 10) = uVar8;
  _port_reg[3] = (uint)p_D3;
  _port_reg[6] = (uint)p_D6;
  return 0;
}
