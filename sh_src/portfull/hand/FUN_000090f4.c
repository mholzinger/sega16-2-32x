/* FUN_000090f4 -- the beast transformation's effect spawner (0x90F4), generated text with
 * two corrections: the per-round script table at 0x99A2 is a ROM table (Ghidra also has a
 * function there and the text added the round to the SH-2 symbol address), and the script
 * pointer stored into the effect record's +0x24 is the 68K ROM address, not the SH-2 one;
 * and A6 is the effect record from 0x9110 on, so its four palette requests allocate for
 * it (the port allocated the hero's palette four times: slot 1's +0x6c..+0x6f read 09 09
 * 09 09 where the arcade holds 0B 0C 0D 0E); and the four palette numbers are read with
 * LD32U, the table entry being 2 mod 4 (the plain load gave xx xx 16 99: +0x0b read 0x99
 * where the arcade holds 0x9D).
 * Measured 2026-10-04, demo A tick 0x24E: slot 1's +0x24 read 0x61402516 on the port. */
uint FUN_000090f4()
{
  undefined2 extraout_D0u = (undefined2)_r_extraout_D0u;
  int unaff_A6 = (int)_r_unaff_A6;
  undefined4 *puVar1;
  
  sound_enqueue(0);
  wait_frame_flag();
  DAT_fffff148 = DAT_fffff109 + 1;
  _r_unaff_A6 = (byte *)(unaff_A6 + 0x80);   /* 0x910E move.l a6,-(sp) ; 0x9110 lea 0x80(a6),a6: the four palette requests below run on the EFFECT record */
  *(undefined2 *)(unaff_A6 + 0x80) = 0x8000;
  ST32((unaff_A6 + 0x82), 0x91ceu);
  ST32((unaff_A6 + 0xa4), 0x99a2u);
  puVar1 = (undefined4 *)P68(LD32U((char *)ROM_BASE + 0x99a2 + (short)((ushort)DAT_fffff142 << 2)));   /* 0x912A lea 0x99A2(pc),a0 ; movea.l (a0,d0.w),a0: a ROM table of ROM pointers, not the function symbol */
  ST32((unaff_A6 + 0xec), LD32U(puVar1));   /* 0x913C move.l (a0)+,($6c,fp): the entry sits at 2 mod 4 (0x99B6); a plain 32-bit load reads the aligned word */
  *(undefined2 *)(unaff_A6 + 0xbe) = *(undefined2 *)(puVar1 + 1);
  ST32((unaff_A6 + 0xa4), (uint)((char *)puVar1 - (char *)ROM_BASE) + 6u);   /* 0x9144 move.l a0,($24,fp): the 68K ROM address of the script */
  *(undefined *)(unaff_A6 + 0xa1) = 0;
  *(undefined2 *)(unaff_A6 + 0xa2) = 1;
  *(undefined *)(unaff_A6 + 0xce) = 0;
  *(undefined *)(unaff_A6 + 0x89) = 2;
  *(undefined *)(unaff_A6 + 0xaf) = 3;
  if (7 < DAT_fffff109) {
    *(char *)(unaff_A6 + 0xec) = *(char *)(unaff_A6 + 0xec) + '\x01';
    *(char *)(unaff_A6 + 0xed) = *(char *)(unaff_A6 + 0xed) + '\x01';
    *(char *)(unaff_A6 + 0xee) = *(char *)(unaff_A6 + 0xee) + '\x01';
    *(char *)(unaff_A6 + 0xef) = *(char *)(unaff_A6 + 0xef) + '\x01';
  }
  *(undefined *)(unaff_A6 + 0x8b) = *(undefined *)(unaff_A6 + 0xec);
  request_palette_update();
  *(undefined *)(unaff_A6 + 0xec) = *(undefined *)(unaff_A6 + 0x8a);
  *(undefined *)(unaff_A6 + 0x8b) = *(undefined *)(unaff_A6 + 0xed);
  request_palette_update();
  *(undefined *)(unaff_A6 + 0xed) = *(undefined *)(unaff_A6 + 0x8a);
  *(undefined *)(unaff_A6 + 0x8b) = *(undefined *)(unaff_A6 + 0xee);
  request_palette_update();
  *(undefined *)(unaff_A6 + 0xee) = *(undefined *)(unaff_A6 + 0x8a);
  *(undefined *)(unaff_A6 + 0x8b) = *(undefined *)(unaff_A6 + 0xef);
  request_palette_update();
  *(undefined *)(unaff_A6 + 0xef) = *(undefined *)(unaff_A6 + 0x8a);
  sound_enqueue(CONCAT22(extraout_D0u,*(undefined2 *)(unaff_A6 + 0xbe)));
  _r_unaff_A6 = (byte *)unaff_A6;   /* movea.l (sp)+,a6 */
  return 0;
}
