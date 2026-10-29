/* HAND: FUN_00015434, the kind-15 head handler. Today's generated body (cport_prep at ff880425)
 * with ONE change, at LAB_00015492: the despawn walks A6 to the two parts as 68K 0x15494-0x154A2
 * does. Without it every despawned kind-15 group left two live, undrawn parts linked to a head slot
 * that is reused. Part 3 is read from part 2's +0x78 BEFORE the head is freed, matching the 68K,
 * which reads it after freeing part 2 but from part 2 itself (freeing a part clears only word 0). */
uint FUN_00015434(int p_A0,byte *p_A2,undefined4 p_D1,uint p_D3,undefined4 p_D6,undefined4 p_D7)
{
  undefined4 extraout_A0 = (undefined4)_r_extraout_A0;
  uint extraout_A1 = (uint)_r_extraout_A1;
  char extraout_D0b = (char)_r_extraout_D0b;
  short extraout_D0w = (short)_r_extraout_D0w;
  ushort extraout_D0w_00 = (ushort)_r_extraout_D0w_00;
  ushort extraout_D0w_01 = (ushort)_r_extraout_D0w_01;
  ushort extraout_D0w_02 = (ushort)_r_extraout_D0w_02;
  ushort extraout_D0w_03 = (ushort)_r_extraout_D0w_03;
  undefined4 extraout_D1 = (undefined4)_r_extraout_D1;
  byte *unaff_A6 = (byte *)_r_unaff_A6;
  undefined2 uVar1;
  ushort uVar2;
  byte bVar3;
  short sVar4;
  byte bVar5;
  short sVar6;
  undefined2 uVar7;
  undefined4 p_D1_00;
  undefined4 p_D1_01;
  ushort uVar8;
  short sVar9;
  byte bVar10;
  undefined3 uVar11;
  short sVar12;
  undefined3 uVar13;
  ushort uVar14;
  undefined *puVar15;
  int iVar16;
  undefined4 uVar17;
  uint uVar18;
  code *pcVar19;
  bool bVar20;
  undefined uVar21;
  undefined8 uVar22;
  undefined6 uVar23;
  

  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from
   * different call sites; they are disjoint by range. See w68.h. */
  p_A0 = (int)A68NORM(p_A0);
  uVar8 = (ushort)p_D6;
  uVar14 = (ushort)p_D7;
  if ((unaff_A6[0x23] == 0) || ((3 < unaff_A6[0x23] && (unaff_A6[0x23] < 6)))) {
    p_A0 = (int)W68(*(short *)(unaff_A6 + 0x78));
    bVar5 = unaff_A6[8];
    unaff_A6[8] = *(byte *)(p_A0 + 8);
    *(byte *)(p_A0 + 8) = bVar5;
  }
  uVar22 = restore_position(p_A0,p_D1);
  iVar16 = (int)((ulonglong)uVar22 >> 0x20);
  if ((*unaff_A6 & 8) == 0) {
    unaff_A6[0x4b] = 0;
    bVar20 = (ushort)((ushort)(_DAT_fffff132 >> 0x10) - 0x50U) < *(ushort *)(unaff_A6 + 0x40);
    if (bVar20) {
      bVar20 = (ushort)((ushort)(_DAT_fffff132 >> 0x10) + 400U) < *(ushort *)(unaff_A6 + 0x40);
      if ((bVar20) && (iVar16 = FUN_000154c0(), (bVar20 = (byte)port_carry))) goto LAB_00015492;
    }
    else {
      iVar16 = FUN_000154c0();
      bVar20 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
      if (bVar20) goto LAB_00015492;
    }
  }
  if (unaff_A6[0x23] == 7) {
    if ((unaff_A6[0x2c] & 0x10) == 0) {
      unaff_A6[0x22] = unaff_A6[0x22] + 1;
    }
    else {
      unaff_A6[0x14] = 0;
      unaff_A6[0x15] = 0;
      unaff_A6[0x18] = 0;
      unaff_A6[0x19] = 0;
      if ((unaff_A6[0x22] == 1) && (bVar20 = false, unaff_A6[0x21] == 0)) {
        *unaff_A6 = *unaff_A6 & 0xf7;
        FUN_000154c0();
        bVar20 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
        if (bVar20) {
LAB_00015492:
          {   /* 0x15492-0x154A2: free the head, then A6 = part 2 (+0x78) and part 3 (part 2's +0x78)
               * and free each, then restore A6. Ghidra dropped both `movea.w (0x78,a6),a6`, so the
               * port freed the head three times and leaked both parts live and undrawn (nine such
               * records at the level-5 lock on the FPGA, 2026-10-28). */
            byte *_head = (byte *)_r_unaff_A6;
            byte *_p2 = (byte *)W68(*(short *)(_head + 0x78));
            byte *_p3 = (byte *)W68(*(short *)(_p2 + 0x78));
            FUN_000154a6();
            _r_unaff_A6 = _p2;
            FUN_000154b0();
            _r_unaff_A6 = _p3;
            FUN_000154b0();
            _r_unaff_A6 = _head;
          }
  _port_reg[3] = (uint)p_D3;
          return 0;
        }
        goto hide_object_sprite;
      }
    }
LAB_000155e4:
    clamp_y_velocity();
    integrate_position();
    hitbox_b_expand();
    unaff_A6[0x2d] = unaff_A6[0x2c];
    unaff_A6[0x2c] = 0;
    floor_collide();
    unaff_A6[0x70] = unaff_A6[0x21];
    unaff_A6[0x71] = unaff_A6[0x22];
    animate_variant();
  }
  else {
    bVar5 = unaff_A6[0x4b];
    if (bVar5 == 0) {
      uVar13 = (undefined3)((uint)p_D7 >> 8);
      if (unaff_A6[0x23] == 6) {
        if ((unaff_A6[0x2c] & 0x10) == 0) {
          unaff_A6[0x22] = unaff_A6[0x22] + 1;
        }
        else {
          unaff_A6[0x14] = 0;
          unaff_A6[0x15] = 0;
          unaff_A6[0x18] = 0;
          unaff_A6[0x19] = 0;
          if ((unaff_A6[0x21] == 0) && (unaff_A6[0x22] == 1)) {
            bVar20 = LD32S((unaff_A6 + 0x24)) == 0x1641a;
            if (!bVar20) {
              bVar5 = unaff_A6[0x76];
              bVar20 = (bVar5 & 1) == 0;
              unaff_A6[0x76] = bVar5 | 1;
              if (bVar20) {
                unaff_A6[0x22] = unaff_A6[0x22] + 0x16;
                goto override_jmp_00015682_case_0;
              }
            }
            goto LAB_000156ec;
          }
        }
      }
      else {
        uVar11 = (undefined3)((uint)p_D6 >> 8);
        if ((unaff_A6[0x23] == 1) || ((unaff_A6[0x21] == 0 && (unaff_A6[0x22] == 1)))) {
          bVar5 = unaff_A6[0x2c];
          uVar8 = (ushort)CONCAT31(uVar11,bVar5) & 0xffc0;
          bVar20 = (bVar5 & 0xc0) == 0;
          if (bVar20) {
LAB_000156ec:
            iVar16 = FUN_0000e266();
            extraout_D0w = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
            p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
            uVar17 = p_D1_00;
            if ((port_e_d0 == 0)) {
LAB_000158e8:
              bVar5 = *(byte *)(iVar16 + 0x2f);
              bVar20 = bVar5 < unaff_A6[0x2f];
              if (bVar5 != unaff_A6[0x2f]) goto LAB_000157c0;
              FUN_0000e23c(iVar16,CONCAT31((int3)((uint)uVar17 >> 8),bVar5));
              extraout_D0w_03 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
              bVar20 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
              if (bVar20) {
                unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
              }
              if (0x7f < extraout_D0w_03) goto LAB_000157c0;
              if (0x4f < extraout_D0w_03) {
                unaff_A6[0x7c] = 0;
                unaff_A6[0x7d] = 0;
                if ((unaff_A6[0x22] == 1) && (unaff_A6[0x23] != 0)) {
                  unaff_A6[0x23] = 0;
                  unaff_A6[0x14] = 0;
                  unaff_A6[0x15] = 0;
                  unaff_A6[0x18] = 0;
                  unaff_A6[0x19] = 0;
                  unaff_A6[0x21] = 0;
                  unaff_A6[0x22] = 1;
                  ST32((unaff_A6 + 0x24), 0x1610au);
                }
                goto override_jmp_00015682_case_0;
              }
              sVar12 = -0xa0;
              bVar5 = 2;
            }
            else {
              if (extraout_D0w == 0xffff) goto LAB_000157c0;
              if (0x10 < *(byte *)(iVar16 + 0x20)) goto LAB_000158e8;
              bVar5 = *(byte *)(iVar16 + 0x2f);
              uVar14 = (ushort)CONCAT31(uVar13,bVar5);
              uVar21 = bVar5 < unaff_A6[0x2f];
              if (bVar5 == unaff_A6[0x2f]) {
                uVar22 = FUN_0000e23c(iVar16,p_D1_00);
                extraout_D0w_00 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
                uVar21 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
                uVar17 = (undefined4)((ulonglong)uVar22 >> 0x20);
                if ((bool)uVar21) {
                  uVar21 = extraout_D0w_00 < 0x20;
                  if ((bool)uVar21) goto LAB_000157c0;
                  unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
                }
LAB_0001580c:
                iVar16 = FUN_0000e2dc(uVar17);
                extraout_D0w_02 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
                uVar21 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
                if (!(bool)uVar21) {
                  bVar5 = 4;
                  uVar8 = uVar8 & 0xff00;
                  bVar10 = 1;
                  if (*(byte *)(iVar16 + 0x2f) < unaff_A6[0x2f]) {
LAB_000158ac:
                    if (unaff_A6[0x77] == 2) {
                      if (extraout_D0w_02 < 0x40) goto LAB_000158c6;
                    }
                    else if (extraout_D0w_02 < 0x30) {
LAB_000158c6:
                      pcVar19 = (code *)0x16126u;   /* the 68K address: cport_prep.py */
                      goto LAB_000158ca;
                    }
                  }
                  else {
                    if (*(byte *)(iVar16 + 0x2f) == unaff_A6[0x2f]) {
                      uVar17 = extraout_D1;
                      if (1 < *p_A2) goto LAB_000158e8;
                      bVar3 = *(byte *)(iVar16 + 0x20);
                      if ((bVar3 != 1) &&
                         ((bVar3 < 7 || ((8 < bVar3 && ((bVar3 < 0xd || (0xe < bVar3))))))))
                      goto LAB_000158ac;
                    }
                    if (unaff_A6[0x77] == 0) {
                      if (extraout_D0w_02 < 0x24) {
LAB_000158a6:
                        pcVar19 = (code *)0x16238;
                        goto LAB_000158ca;
                      }
                    }
                    else if (extraout_D0w_02 < 0x30) goto LAB_000158a6;
                  }
                }
              }
              else if ((unaff_A6[0x2c] & 0x10) != 0) {
                uVar22 = FUN_0000e23c(iVar16,p_D1_00);
                extraout_D0w_01 = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */
                uVar21 = (byte)port_carry;   /* the callee's carry: cport_prep.py */
                extraout_A0 = (uint)((ulonglong)uVar22 >> 0x20);
                uVar17 = (undefined4)((ulonglong)uVar22 >> 0x20);
                if ((bool)uVar21) {
                  uVar21 = extraout_D0w_01 < 0x40;
                  if ((bool)uVar21) goto LAB_000157c0;
                  unaff_A6[0x2e] = unaff_A6[0x2e] ^ 0x80;
                }
                else {
                  uVar21 = extraout_D0w_01 < 0x60;
                  if (!(bool)uVar21) goto LAB_0001580c;
                }
                uVar23 = FUN_00014152();
                uVar17 = extraout_A0;
                if (((uint)uVar23 & 1 << (bVar5 & 0x1f)) != 0) {
                  if ((unaff_A6[0x2f] <= bVar5) && (unaff_A6[0x74] != 0)) goto LAB_000157c0;
                  uVar21 = (ushort)((uint6)uVar23 >> 0x20) < 0x40;
                  if (!(bool)uVar21) {
                    unaff_A6[0x72] = 2;
                    goto LAB_0001577e;
                  }
                }
                goto LAB_0001580c;
              }
LAB_000157c0:
              sVar12 = 0xe0;
              bVar5 = 1;
            }
            unaff_A6[0x7c] = 0;
            unaff_A6[0x7d] = 0;
            if ((unaff_A6[0x23] != 1) && (unaff_A6[0x23] != 2)) {
              unaff_A6[0x21] = 0;
              unaff_A6[0x22] = 1;
              unaff_A6[0x24] = 0;
              unaff_A6[0x25] = 1;
              unaff_A6[0x26] = 0x5e;
              unaff_A6[0x27] = 0xa8;
            }
            unaff_A6[0x23] = bVar5;
            if ((unaff_A6[0x2e] & 0x80) != 0) {
              sVar12 = -sVar12;
            }
            *(short *)(unaff_A6 + 0x14) = sVar12;
            *(short *)(unaff_A6 + 0x18) = sVar12;
            unaff_A6[0x16] = 0;
            unaff_A6[0x17] = 0;
          }
          else {
            uVar8 = (ushort)CONCAT31(uVar11,bVar5) & 0xff80;
            if ((bVar5 & 0x80) == 0) {
              bVar5 = unaff_A6[0x2e];
            }
            else {
              bVar5 = unaff_A6[0x2e];
            }
            bVar20 = (bVar5 & 0x80) == 0;
            if (bVar20) goto LAB_000156ec;
            if ((unaff_A6[0x20] & 8) == 0) {
              unaff_A6[0x72] = 0;
              uVar14 = (ushort)CONCAT31(uVar13,*(undefined *)(iVar16 + 0x2f));
LAB_0001577e:
              unaff_A6[0x73] = (byte)uVar14;
              unaff_A6[0x3e] = unaff_A6[0x2f];
              unaff_A6[0x3f] = unaff_A6[0x2f];
              unaff_A6[0x7c] = 0;
              unaff_A6[0x7d] = 0;
              unaff_A6[0x23] = 3;
              unaff_A6[0x14] = 0;
              unaff_A6[0x15] = 0;
              unaff_A6[0x18] = 0;
              unaff_A6[0x19] = 0;
              unaff_A6[0x21] = 0;
              unaff_A6[0x22] = 1;
              ST32((unaff_A6 + 0x24), 0x15fbau);
              goto override_jmp_00015682_case_0;
            }
            FUN_0000e266();
            p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
            unaff_A6[0x7c] = 0;
            unaff_A6[0x7d] = 0;
            bVar5 = 4;
            uVar8 = (short)((uint)p_D6 >> 8) << 8;
            bVar10 = 1;
            pcVar19 = (code *)0x16238;
LAB_000158cc:
            unaff_A6[0x21] = (byte)uVar8;
            unaff_A6[0x22] = bVar10;
            ST32((unaff_A6 + 0x24), ((uint)pcVar19));
            unaff_A6[0x23] = bVar5;
            unaff_A6[0x14] = 0;
            unaff_A6[0x15] = 0;
            unaff_A6[0x18] = 0;
            unaff_A6[0x19] = 0;
          }
        }
        else {
                    /* WARNING: Switch is manually overridden */
          switch((int)*(byte *)((char *)(unaff_A6) + 0x23) - 0x23) {   /* the state byte, see cport_prep.py */
          case (int)0xffffffe0u:
            if (1 < unaff_A6[0x21]) {
              if (unaff_A6[0x21] == 3) {
                unaff_A6[0x22] = 0xf8;
                if ((unaff_A6[0x2c] & 0x10) != 0) {
                  unaff_A6[0x22] = 1;
                  unaff_A6[0x14] = 0;
                  unaff_A6[0x15] = 0;
                  unaff_A6[0x18] = 0;
                  unaff_A6[0x19] = 0;
                }
              }
              else if ((unaff_A6[0x21] < 3) && (unaff_A6[0x22] == 1)) {
                unaff_A6[0x3f] = unaff_A6[0x73];
                sVar12 = *(short *)(unaff_A6 + 0x16);
                unaff_A6[0x16] = 0;
                unaff_A6[0x17] = 0;
                if ((unaff_A6[0x2e] & 0x80) != 0) {
                  sVar12 = -sVar12;
                }
                *(short *)(unaff_A6 + 0x14) = sVar12;
                *(short *)(unaff_A6 + 0x18) = sVar12;
                if (unaff_A6[0x72] == 1) {
                  uVar7 = 0xf980;
                }
                else if (unaff_A6[0x72] == 0) {
                  uVar7 = 0xf780;
                }
                else {
                  uVar7 = 0xfd40;
                  bVar5 = unaff_A6[0x3f];
                  bVar10 = unaff_A6[0x3e];
                  if (bVar5 == bVar10) {
LAB_000159c2:
                    uVar7 = 0xf780;
                  }
                  else if (bVar5 < bVar10) {
                    if ((byte)(bVar5 - bVar10) != -2) goto LAB_000159c2;
                    uVar7 = 0xf500;
                  }
                }
                *(undefined2 *)(unaff_A6 + 0x1a) = uVar7;
                if (unaff_A6[0x72] == 0) {
                  unaff_A6[0x16] = 0;
                  unaff_A6[0x17] = 8;
                  unaff_A6[0x18] = 1;
                  unaff_A6[0x19] = 0;
                  if ((unaff_A6[0x2e] & 0x80) != 0) {
                    *(short *)(unaff_A6 + 0x16) = -*(short *)(unaff_A6 + 0x16);
                    *(short *)(unaff_A6 + 0x18) = -*(short *)(unaff_A6 + 0x18);
                  }
                }
              }
            }
            break;
          case (int)0xffffffe1u:
            if (((unaff_A6[0x77] != 0) && (unaff_A6[0x21] == 3)) &&
               (bVar20 = unaff_A6[0x22] == 1, bVar20)) {
              uVar17 = FUN_0000e266();
              p_A2 = (typeof(p_A2))port_e_a2;   /* A2 after 0xE266: cport_prep.py */
              if ((((port_e_d0 == 0)) || (extraout_D0b == 0xff)) || (1 < *p_A2)) {
                unaff_A6[0x7c] = 0;
                unaff_A6[0x7d] = 0;
                break;
              }
              FUN_0000e23c(uVar17,p_D1_01);
              bVar5 = 5;
              uVar8 = (ushort)CONCAT31(uVar11,3);
              bVar10 = 9;
              pcVar19 = (code *)LD32U(unaff_A6 + 0x24);
LAB_000158ca:
              *p_A2 = *p_A2 + 1;
              goto LAB_000158cc;
            }
            break;
          case (int)0xffffffe2u:
            if ((unaff_A6[0x21] == 3) && (unaff_A6[0x22] == 1)) {
              unaff_A6[0x23] = 4;
            }
          }
        }
      }
    }
    else {
      unaff_A6[0x4b] = 0;
      unaff_A6[0x16] = 0;
      unaff_A6[0x17] = 0;
      unaff_A6[0x18] = 0;
      unaff_A6[0x19] = 0;
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
      uVar17 = 0x1641a;
      bVar5 = bVar5 & 0xfc;
      if (bVar5 != 0) {
        bVar10 = unaff_A6[0x4a];
        bVar3 = bVar10 - bVar5;
        unaff_A6[0x4a] = bVar3;
        if ((bVar10 < bVar5) || (bVar3 == 0)) {
          unaff_A6[0x23] = 7;
          _DAT_fffff032 = 0x500;
          FUN_00003706((int)*(short *)((char *)W68(*(short *)(unaff_A6 + 0x4c)) + 0x3c));
          unaff_A6[0x4a] = 0;
          unaff_A6[0x50] = 0xf8;
          unaff_A6[0x51] = 8;
          ST32((unaff_A6 + 0x24), 0x16462u);
          unaff_A6[0x21] = 0;
          unaff_A6[0x22] = 1;
          goto LAB_000155e4;
        }
        sound_enqueue(0xffffffb4);
        unaff_A6[0x7e] = 9;
        uVar17 = 0x163e8;
        unaff_A6[0x1a] = 0;
        unaff_A6[0x1b] = 0;
        unaff_A6[0x14] = 0;
        unaff_A6[0x15] = 0;
        unaff_A6[0x18] = 0;
        unaff_A6[0x19] = 0;
      }
      ST32((unaff_A6 + 0x24), (uVar17));
      unaff_A6[0x23] = 6;
      unaff_A6[0x21] = 0;
      unaff_A6[0x22] = 1;
    }
override_jmp_00015682_case_0:
    clamp_x_velocity();
    clamp_y_velocity();
    integrate_position();
    animation_peek_next();
    hitbox_b_expand();
    unaff_A6[0x2d] = unaff_A6[0x2c];
    unaff_A6[0x2c] = 0;
    unaff_A6[0x74] = 0;
    collide_box_b();
    if ((unaff_A6[0x2c] & 0x10) != 0) {
      unaff_A6[0x74] = 0xff;
    }
    if (unaff_A6[0x23] == 3) {
      uVar17 = LD32U((unaff_A6 + 0x10));
      uVar7 = *(undefined2 *)(unaff_A6 + 0x1a);
      uVar1 = *(undefined2 *)(unaff_A6 + 0x2c);
      floor_collide();
      uVar2 = *(ushort *)(unaff_A6 + 0x10);
      if (((((unaff_A6[0x2c] & 0x10) != 0) &&
           (uVar2 != *(ushort *)((short)((ushort)unaff_A6[0x3f] * 2) + ((int)ROM_BASE + 0x141b2)))) &&
          ((uVar2 != *(ushort *)((short)((ushort)unaff_A6[0x3e] * 2) + ((int)ROM_BASE + 0x141b2)) ||
           ((char)((ushort)unaff_A6[0x3e] * 2) < (char)((ushort)unaff_A6[0x3f] * 2))))) &&
         (uVar2 < uRam000141b6)) {
        *(undefined2 *)(unaff_A6 + 0x2c) = uVar1;
        *(undefined2 *)(unaff_A6 + 0x1a) = uVar7;
        ST32((unaff_A6 + 0x10), (uVar17));
        hitbox_b_expand();
      }
    }
    else {
      floor_collide();
    }
    unaff_A6[0x70] = unaff_A6[0x21];
    unaff_A6[0x71] = unaff_A6[0x22];
    FUN_0000669c();
    hitbox_a_expand();
    animation_frame_fetch();
    walk_player_slots();
    FUN_00011386((byte *)(0x1647c));
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
  uVar22 = sprite_frame_lookup();
  extraout_A1 = (uint)uVar22;
  uVar22 = zoom_scale_lookup((int)((ulonglong)uVar22 >> 0x20),p_D3);
  p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */
  extraout_D1 = (uint)uVar22;
  sVar12 = (_DAT_fffff0fa + *(short *)(unaff_A6 + 0x10) + -0x1000) - _DAT_fffff128;
  sVar4 = sVar12 - (short)p_D6;   /* 0x3D46 subw d6,d1: the height the zoom lookup just returned, not uVar8: cport_prep.py */
  if (sVar12 < 0) {
hide_object_sprite:
    *unaff_A6 = *unaff_A6 & 0xf7;
    ST32(SPRREC((ushort)unaff_A6[8])   /* lsl #4 ; addw #$F800 ; movea.w: a 16-bit sum, cport_prep.py */, (0));
    FUN_00003cb6();
  _port_reg[3] = (uint)p_D3;
    return 0;
  }
  sVar9 = _DAT_fffff120 + _DAT_fffff0f8 + *(short *)(unaff_A6 + 0xc) + -0xf40;
  uVar8 = sVar9 - ((ushort)(((short)p_D7 & 1) != 0) + ((short)p_D7 >> 1));   /* 0x3E18 asrw #1,d7 ; subxw d7,d2: the width the zoom lookup just returned, not uVar14: cport_prep.py */
  if ((0x1ff < uVar8) || ((ushort)(((short)p_D7 >> 1) + sVar9) < 0xb9)) goto hide_object_sprite;
  bVar5 = *unaff_A6;
  if ((DAT_fffff018 & 0x40) != 0) {
    bVar5 = bVar5 ^ 2;
  }
  uVar14 = 0;
  sVar9 = (short)p_D3;
  uVar18 = extraout_A1;
  if ((bVar5 & 1) != 0) {
    uVar18 = (int)sVar9 + (extraout_A1 - 2);
    uVar14 = 0x100;
  }
  if (0xdf < sVar4) goto hide_object_sprite;
  sVar6 = sVar4;
  if ((sVar4 < 0) && (sVar6 = 0, (DAT_fffff018 & 0x40) == 0)) {
    if ((bVar5 & 2) == 0) {
      uVar18 = (int)(short)(-sVar4 * sVar9) + uVar18;
    }
    else {
      uVar18 = uVar18 - (int)(short)(-sVar4 * sVar9);
    }
  }
  sVar4 = sVar12;
  if ((short)(sVar12 + -0xe0) < 0) {
LAB_00003e9c:
    uVar21 = (undefined)sVar4;
    if ((bVar5 & 2) == 0) goto LAB_00003eb2;
  }
  else {
    sVar4 = 0xe0;
    uVar21 = 0xe0;
    if ((DAT_fffff018 & 0x40) == 0) goto LAB_00003e9c;
    sVar12 = (sVar12 + -0xe0) * sVar9;
    if ((bVar5 & 2) == 0) {
      uVar18 = (int)sVar12 + uVar18;
      goto LAB_00003eb2;
    }
    uVar18 = uVar18 - (int)sVar12;
    uVar21 = 0xe0;
  }
  uVar18 = (int)*(short *)((ulonglong)uVar22 >> 0x20) + uVar18;
  p_D3 = (uint)(-sVar9 & 0xff);
LAB_00003eb2:
  *unaff_A6 = *unaff_A6 | 8;
  puVar15 = (undefined *)SPRREC((ushort)unaff_A6[8])   /* lsl #4 ; addw #$F800 ; movea.w: a 16-bit sum, cport_prep.py */;
  *puVar15 = uVar21;
  puVar15[1] = (char)sVar6;
  *(ushort *)(puVar15 + 2) = uVar8;
  *(ushort *)(puVar15 + 4) = uVar14 | (ushort)CONCAT31((int3)(p_D3 >> 8),(char)p_D3 >> 1);
  *(short *)(puVar15 + 6) = (short)(uVar18 >> 1);
  puVar15[8] = (byte)((uVar18 >> 1) >> 0x10) | 0xf0;
  puVar15[9] = unaff_A6[10] | unaff_A6[9] << 6;
  uVar8 = (ushort)unaff_A6[0x4e];
  if (unaff_A6[0x4e] != 0) {
    uVar8 = (uVar8 & 0x1f) * 0x21;
  }
  *(ushort *)(puVar15 + 10) = uVar8;
  _port_reg[3] = (uint)p_D3;
  return 0;
}
