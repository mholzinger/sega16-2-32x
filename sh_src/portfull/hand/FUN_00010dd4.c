/* FUN_00010dd4 -- the flame object's handler (0x10DD4), the decompiled body with two
 * corrections at 0x11006-0x11018: `jsr e266 ; not.w d0 ; beq 11186 ; jsr e2dc ; bcs
 * 11186`. The decompiler merged the register d0 (~D0, 16 bits) with the e23c distance
 * into uVar23 and, on the e2dc-carry path, loaded a stale pseudo-register into it, so
 * at 0x11186 `cmpi.w #96,d0` compared 0 and the flame took the "player within 0x60"
 * script 0x11452 (dx 0, wait 0x1D) on its first tick; the arcade's flame, 0x139 px
 * from the player, takes 0x113F8 and walks. Measured 2026-10-04 with CPORT_PROBE.
 * Everything else is the generated text of that build; regenerate by deleting this
 * file and re-applying the two lines marked 0x1100C / 0x11018. */
uint FUN_00010dd4(undefined4 p_A0,byte *p_A2,undefined4 p_D1,uint p_D3,uint p_D6,uint p_D7)
{
  uint extraout_A1 = (uint)_r_extraout_A1;
  ushort extraout_D0w = (ushort)_r_extraout_D0w;
  ushort extraout_D0w_00 = (ushort)_r_extraout_D0w_00;
  ushort extraout_D0w_01 = (ushort)_r_extraout_D0w_01;
  ushort extraout_D0w_02 = (ushort)_r_extraout_D0w_02;
  undefined2 extraout_D1u = (undefined2)_r_extraout_D1u;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  byte bVar1;
  undefined2 uVar2;
  undefined uVar4;
  short sVar3;
  byte bVar5;
  short sVar6;
  undefined2 uVar7;
  undefined4 p_D1_00;
  ushort uVar8;
  short sVar9;
  undefined3 uVar10;
  short sVar11;
  undefined *puVar12;
  undefined4 uVar13;
  int iVar14;
  code *pcVar15;
  uint uVar16;
  char cVar17;
  char cVar18;
  char cVar19;
  bool bVar20;
  char cVar21;
  byte bVar22;
  ushort uVar23;
  undefined8 uVar24;
  
  restore_position(p_A0,p_D1);
  if (((*unaff_A6 & 8) == 0) &&
     (((0x117f < *(ushort *)(unaff_A6 + 0x10) ||
       (unaff_A6[0x4b] = 0, *(ushort *)(unaff_A6 + 0x40) <= (ushort)((ushort)(_DAT_fffff132 >> 0x10) - 0x50U))) ||
      ((ushort)((ushort)(_DAT_fffff132 >> 0x10) + 400U) < *(ushort *)(unaff_A6 + 0x40))))) {
code_r0x00010e06:
    FUN_00010cfe();
    tick_kill_counters();
LAB_00010e10:
    unaff_A6[0] = 0;
    unaff_A6[1] = 0;
    release_palette_slot();
    goto hide_object_sprite;
  }
  if (unaff_A6[0x23] == 5) {
    if (unaff_A6[0x22] == 1) {
      if (unaff_A6[0x21] == 0) goto code_r0x00010e06;
      if (unaff_A6[0x21] == 1) {
        unaff_A6[0x14] = 0;
        unaff_A6[0x15] = 0;
        FUN_0001124e(p_D3,p_D6,p_D7);
      }
    }
LAB_00010f10:
    uVar23 = (ushort)p_D7;
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
    if (bVar5 == 0) {
      cVar21 = unaff_A6[0x23] < 4;
      if (unaff_A6[0x23] == 4) {
        if ((unaff_A6[0x2c] & 0x10) == 0) {
          unaff_A6[0x22] = unaff_A6[0x22] + 1;
        }
        else {
          unaff_A6[0x14] = 0;
          unaff_A6[0x15] = 0;
          if ((unaff_A6[0x21] == 0) && (unaff_A6[0x22] == 1)) {
            if ((LD32S((unaff_A6 + 0x24)) == 0x11730) ||
               (bVar5 = unaff_A6[0x76], unaff_A6[0x76] = bVar5 | 1, (bVar5 & 1) != 0))
            goto LAB_00010fec;
            unaff_A6[0x22] = unaff_A6[0x22] + 0x16;
          }
        }
      }
      else {
        bVar5 = unaff_A6[0x2c];
        uVar10 = (undefined3)(p_D6 >> 8);
        p_D6 = CONCAT31(uVar10,bVar5) & 0xffffffc0;
        if ((bVar5 & 0xc0) == 0) goto LAB_00010fec;
        uVar13 = FUN_0000e266();
        extraout_D0w_00 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
        p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
        unaff_A6[0x7c] = 0;
        unaff_A6[0x7d] = 0;
        cVar17 = '\0';
        cVar18 = '\x01';
        cVar19 = '\0';
        bVar22 = false;
        uVar13 = FUN_0000e2dc(uVar13);
        extraout_D0w_01 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
        bVar22 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
        if ((bool)bVar22) {
LAB_00010fd0:
          p_D6 = CONCAT31(uVar10,bVar5) & 0xffffff80;
          if ((bVar5 & 0x80) == 0) {
            if ((unaff_A6[0x2e] & 0x80) != 0) {
LAB_00010fec:
              if ((unaff_A6[0x23] != 0) && ((unaff_A6[0x21] != 0 || (unaff_A6[0x22] != 1))))
              goto LAB_000111fa;
              uVar13 = FUN_0000e266();
              p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
              extraout_D0w_00 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
              bVar20 = false;
              uVar23 = (ushort)~extraout_D0w_00;   /* 0x1100C not.w d0: 16 bits; 0 iff no live player */
              if ((uVar23 == 0) || (iVar14 = FUN_0000e2dc(uVar13), (bVar20 = (byte)port_carry))   /* 0x11018 bcs: d0 is still ~D0 at 0x11186 */
                 ) {
LAB_00011186:
                if (uVar23 < 0x60) {
                  bVar5 = 0;
                  uVar7 = 0xfe80;
                  pcVar15 = (code *)0x11452;
                  if (unaff_A6[0x23] != 1) {
                    bVar22 = 0;
                    uVar2 = 0xfe80;
                    if (unaff_A6[0x23] != 0) goto LAB_000111d4;
                    if ((LD32S((unaff_A6 + 0x24)) == 0x11452) && ((unaff_A6[0x21] & 1) != 0)) {
                      uVar7 = 0;
                    }
                  }
                }
                else {
                  bVar5 = 0;
                  uVar7 = 0xff00;
                  pcVar15 = (code *)0x113f8u;   /* the 68K address: cport_prep.py */
                  if (unaff_A6[0x23] != 1) {
                    bVar22 = 0;
                    uVar2 = 0xff00;
                    if (unaff_A6[0x23] == 0) {
                      pcVar15 = (code *)0x113f8u;   /* the 68K address: cport_prep.py */
                      uVar2 = uVar7;
                      if (LD32S((unaff_A6 + 0x24)) == 0x11452) {
LAB_00011162:
                        bVar5 = bVar22;
                        uVar7 = uVar2;
                        pcVar15 = (code *)0x113f8u;   /* the 68K address: cport_prep.py */
                        if ((unaff_A6[0x21] & 1) == 0) {
LAB_00011176:
                          bVar22 = unaff_A6[0x22];
                          bVar1 = bVar22 - 4;
                          unaff_A6[0x22] = bVar1;
                          if ((bVar1 != 0) && (3 < bVar22)) goto LAB_000111de;
                        }
                        else {
                          bVar22 = unaff_A6[0x22];
                          unaff_A6[0x22] = bVar22 - 0x18;
                          if ((0x17 < bVar22) && ((byte)(bVar22 - 0x18) != 0)) {
                            if (0x17 < bVar22) goto LAB_000111de;
                            goto LAB_00011176;
                          }
                        }
                        unaff_A6[0x22] = 1;
                      }
                    }
                    else {
LAB_000111d4:
                      uVar7 = uVar2;
                      bVar5 = bVar22;
                      unaff_A6[0x22] = 1;
                      unaff_A6[0x21] = 0;
                    }
                  }
                }
              }
              else {
                bVar20 = *(ushort *)(iVar14 + 0x10) < *(ushort *)(unaff_A6 + 0x10);
                uVar8 = *(ushort *)(iVar14 + 0x10) - *(ushort *)(unaff_A6 + 0x10);
                if ((!bVar20) && (bVar20 = uVar8 < 0x38, !bVar20)) goto LAB_00011186;
                uVar24 = FUN_0000e23c(iVar14,CONCAT22(extraout_D1u,uVar8));
                extraout_D0w_02 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
                extraout_D0w = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
                bVar20 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
                iVar14 = (int)((ulonglong)uVar24 >> 0x20);
                if (bVar20) {
                  unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
                }
                bVar5 = *(byte *)(iVar14 + 0x20);
                uVar24 = CONCAT44(iVar14,(uint)bVar5);
                uVar23 = extraout_D0w_02;
                if (4 < bVar5) {
                  if (bVar5 < 0x11) {
                    if (0x32 < extraout_D0w_02) goto joined_r0x00011084;
                    goto LAB_000110a8;
                  }
                  if (extraout_D0w_02 < 0x60) {
                    if (extraout_D0w_02 < 0x50) goto LAB_00011146;
LAB_00011116:
                    unaff_A6[0x7c] = 0;
                    unaff_A6[0x7d] = 0;
                    unaff_A6[0x14] = 0;
                    unaff_A6[0x15] = 0;
                    if (unaff_A6[0x23] != 3) {
                      unaff_A6[0x23] = 3;
                      unaff_A6[0x21] = 0;
                      unaff_A6[0x22] = 1;
                      unaff_A6[0x24] = 0;
                      unaff_A6[0x25] = 1;
                      unaff_A6[0x26] = 0x13;
                      unaff_A6[0x27] = 0xe0;
                    }
                    goto LAB_000111fa;
                  }
                  goto LAB_00011186;
                }
                if (extraout_D0w_02 < 0x33) {
LAB_000110a8:
                  if (*p_A2 < 2) {
                    *p_A2 = *p_A2 + 1;
                  }
                  else {
                    unaff_A6[0x7c] = 0;
                    unaff_A6[0x7d] = 0;
                    uVar24 = CONCAT44(iVar14,(uint)bVar5);
                  }
                  goto LAB_000110b6;
                }
                if (*p_A2 < 2) {
joined_r0x00011084:
                  if ((0x5f < extraout_D0w_02) || (*p_A2 < 2)) goto LAB_00011186;
                }
                else {
                  if (0x5f < extraout_D0w_02) goto LAB_00011186;
                  if (0x4f < extraout_D0w_02) goto LAB_00011116;
                }
LAB_00011146:
                uVar7 = 0x100;
                bVar5 = 1;
                pcVar15 = (code *)0x113f8u;   /* the 68K address: cport_prep.py */
                if (unaff_A6[0x23] != 1) {
                  bVar22 = 1;
                  uVar2 = 0x100;
                  if (unaff_A6[0x23] == 0) {
                    if (LD32U(unaff_A6 + 0x24) == 0x113f8u) goto LAB_000111de;
                    goto LAB_00011162;
                  }
                  goto LAB_000111d4;
                }
              }
LAB_000111de:
              *(undefined2 *)(unaff_A6 + 0x14) = uVar7;
              ST32((unaff_A6 + 0x24), ((uint)pcVar15));
              unaff_A6[0x23] = bVar5;
              unaff_A6[0x7c] = 0;
              unaff_A6[0x7d] = 0;
              if ((unaff_A6[0x2e] & 0x80) == 0) {
                *(short *)(unaff_A6 + 0x14) = -*(short *)(unaff_A6 + 0x14);
              }
              goto LAB_000111fa;
            }
          }
          else if ((unaff_A6[0x2e] & 0x80) == 0) goto LAB_00010fec;
LAB_000110e0:
          uVar13 = 0x115e2;
          if (LD32S((unaff_A6 + 0x24)) == 0x115e2) goto LAB_000111fa;
        }
        else {
          uVar24 = FUN_0000e23c(uVar13,p_D1_00);
          extraout_D0w = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
          bVar22 = (byte)port_carry;   /* 0x10FB6 move sr,d7: the carry e23c returns, not e2dc's */
          uVar23 = (ushort)(byte)(cVar21 << 4 | cVar17 << 3 | cVar18 << 2 | cVar19 << 1 | bVar22);
          p_D7 = (uint)uVar23;
          if (0x32 < extraout_D0w) goto LAB_00010fd0;
          p_D7 = (uint)(uVar23 & 1);
          if ((bVar22 & 1) != 0) {
            unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
          }
LAB_000110b6:
          bVar5 = *(byte *)((int)((ulonglong)uVar24 >> 0x20) + 0x2f);
          if ((unaff_A6[0x2f] <= bVar5) &&
             (((bVar5 != unaff_A6[0x2f] || (bVar5 = (byte)uVar24, bVar5 == 1)) ||
              ((6 < bVar5 && ((bVar5 < 9 || ((0xc < bVar5 && (bVar5 < 0xf))))))))))
          goto LAB_000110e0;
          uVar13 = 0x114ac;
          if (LD32S((unaff_A6 + 0x24)) == 0x114ac) goto LAB_000111fa;
        }
        unaff_A6[0x21] = 0;
        unaff_A6[0x22] = 1;
        unaff_A6[0x14] = 0;
        unaff_A6[0x15] = 0;
        unaff_A6[0x23] = 2;
        ST32((unaff_A6 + 0x24), (uVar13));
      }
    }
    else {
      unaff_A6[0x4b] = 0;
      uVar7 = 0x280;
      unaff_A6[0x2e] = 0x80;
      sVar11 = *(short *)(unaff_A6 + 0x4c);
      if (*(ushort *)(unaff_A6 + 0x40) <= *(ushort *)((char *)W68(sVar11) + 0x40)) {   /* 0x10E4A: a5 = the attacker, a 68K address */
        unaff_A6[0x2e] = 0;
        uVar7 = 0xfd80;
      }
      *(undefined2 *)(unaff_A6 + 0x14) = uVar7;
      unaff_A6[0x1a] = 0xfb;
      unaff_A6[0x1b] = 0;
      uVar13 = 0x11730;
      bVar5 = bVar5 & 0xfc;
      if (bVar5 != 0) {
        if (0xef < bVar5) {
          p_D7 = 3;
          bVar20 = false;
          bVar5 = FUN_0000d776(p_D3,p_D6,3);
          bVar20 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
          if (bVar20) {
            FUN_00010e06();
            unaff_A6 = (byte *)(int)W68(*(short *)(unaff_A6 + 0x78));
            goto LAB_00010e10;
          }
        }
        unaff_A6[0x1a] = 0;
        unaff_A6[0x1b] = 0;
        unaff_A6[0x7e] = 9;
        bVar22 = unaff_A6[0x4a];
        bVar1 = bVar22 - bVar5;
        unaff_A6[0x4a] = bVar1;
        if ((bVar22 < bVar5) || (bVar1 == 0)) {
          unaff_A6[0x1a] = 0xfb;
          unaff_A6[0x1b] = 0;
          unaff_A6[0x4a] = 0;
          _DAT_fffff032 = 0x300;
          FUN_00003706((int)W68(*(short *)((char *)W68(sVar11) + 0x3c)));
          unaff_A6[0x24] = 0;
          unaff_A6[0x25] = 1;
          unaff_A6[0x26] = 0x17;
          unaff_A6[0x27] = 0x48;
          unaff_A6[0x23] = 5;
          unaff_A6[0x21] = 0;
          unaff_A6[0x22] = 1;
          goto LAB_00010f10;
        }
        sound_enqueue(0xffffffb4);
        unaff_A6[0x14] = 0;
        unaff_A6[0x15] = 0;
        uVar13 = 0x11702;
      }
      ST32((unaff_A6 + 0x24), (uVar13));
      unaff_A6[0x23] = 4;
      unaff_A6[0x21] = 0;
      unaff_A6[0x22] = 1;
    }
LAB_000111fa:
    uVar23 = (ushort)p_D7;
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
    FUN_00011386((byte *)(0x11762));
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
  uVar24 = sprite_frame_lookup();
  extraout_A1 = (uint)uVar24;
  uVar24 = zoom_scale_lookup((int)((ulonglong)uVar24 >> 0x20),p_D3);
  p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */
  extraout_D1u = (uint)uVar24;
  uVar23 = (ushort)p_D7;   /* the cull below used Ghidra's pre-call copy of D7 (the dispatch register, 0 in the port): the flame hid on the ticks its screen x was under 0xB9 (demo B 0x15C/0x16F) */
  sVar11 = (_DAT_fffff0fa + *(short *)(unaff_A6 + 0x10) + -0x1000) - _DAT_fffff128;
  sVar3 = sVar11 - (short)p_D6;
  if (sVar11 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32((W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10)), (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
  _port_reg[6] = (uint)p_D6; _port_reg_n++;
  _port_reg[7] = (uint)p_D7; _port_reg_n++;
    return 0;
  }
  sVar9 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  uVar8 = sVar9 - ((ushort)((uVar23 & 1) != 0) + ((short)uVar23 >> 1));
  if ((0x1ff < uVar8) || ((ushort)(((short)uVar23 >> 1) + sVar9) < 0xb9)) goto hide_object_sprite;
  bVar5 = *unaff_A6;
  if ((DAT_fffff018 & 0x40) != 0) {
    bVar5 = bVar5 ^ 2;
  }
  uVar23 = 0;
  sVar9 = (short)p_D3;
  uVar16 = extraout_A1;
  if ((bVar5 & 1) != 0) {
    uVar16 = (int)sVar9 + (extraout_A1 - 2);
    uVar23 = 0x100;
  }
  if (0xdf < sVar3) goto hide_object_sprite;
  sVar6 = sVar3;
  if ((sVar3 < 0) && (sVar6 = 0, (DAT_fffff018 & 0x40) == 0)) {
    if ((bVar5 & 2) == 0) {
      uVar16 = (int)(short)(-sVar3 * sVar9) + uVar16;
    }
    else {
      uVar16 = uVar16 - (int)(short)(-sVar3 * sVar9);
    }
  }
  sVar3 = sVar11;
  if ((short)(sVar11 + -0xe0) < 0) {
LAB_00003e9c:
    uVar4 = (undefined)sVar3;
    if ((bVar5 & 2) == 0) goto LAB_00003eb2;
  }
  else {
    sVar3 = 0xe0;
    uVar4 = 0xe0;
    if ((DAT_fffff018 & 0x40) == 0) goto LAB_00003e9c;
    sVar11 = (sVar11 + -0xe0) * sVar9;
    if ((bVar5 & 2) == 0) {
      uVar16 = (int)sVar11 + uVar16;
      goto LAB_00003eb2;
    }
    uVar16 = uVar16 - (int)sVar11;
    uVar4 = 0xe0;
  }
  uVar16 = (int)*(short *)((ulonglong)uVar24 >> 0x20) + uVar16;
  p_D3 = (uint)(-sVar9 & 0xff);
LAB_00003eb2:
  *unaff_A6 = *unaff_A6 | 8;
  puVar12 = (undefined *)(W68PTR(0xFFF800u) + ((ushort)unaff_A6[8] * 0x10));
  *puVar12 = uVar4;
  puVar12[1] = (char)sVar6;
  *(ushort *)(puVar12 + 2) = uVar8;
  *(ushort *)(puVar12 + 4) = uVar23 | (ushort)CONCAT31((int3)(p_D3 >> 8),(char)p_D3 >> 1);
  *(short *)(puVar12 + 6) = (short)(uVar16 >> 1);
  puVar12[8] = (byte)((uVar16 >> 1) >> 0x10) | 0xf0;
  puVar12[9] = unaff_A6[10] | unaff_A6[9] << 6;
  uVar23 = (ushort)unaff_A6[0x4e];
  if (unaff_A6[0x4e] != 0) {
    uVar23 = (uVar23 & 0x1f) * 0x21;
  }
  *(ushort *)(puVar12 + 10) = uVar23;
  _port_reg[3] = (uint)p_D3; _port_reg_n++;
  _port_reg[6] = (uint)p_D6; _port_reg_n++;
  _port_reg[7] = (uint)p_D7; _port_reg_n++;
  return 0;
}
