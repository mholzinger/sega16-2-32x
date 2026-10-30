/* FUN_0000f1c0 -- the enemy whose death releases the spirit balls (0xF1C0), generated text
 * with one correction: `movea.w ($4c,a6),a0 ; movea.w ($3c,a0),a5` reads the killer's HUD
 * link through the killer's record; the generated text dereferenced the 16-bit killer link
 * as an SH-2 address, so the 1000-point award (0xFFF032 -> HUD +4) went nowhere. */
uint FUN_0000f1c0(undefined4 p_D2)
{
  undefined2 extraout_D0u = (undefined2)_r_extraout_D0u;
  undefined2 extraout_D0u_00 = (undefined2)_r_extraout_D0u_00;
  undefined2 extraout_D1u = (undefined2)_r_extraout_D1u;
  undefined2 extraout_D1u_00 = (undefined2)_r_extraout_D1u_00;
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;
  undefined2 uVar1;
  undefined2 uVar2;
  
  *(undefined *)((int)unaff_A6 + 0x4b) = 0;
  *(undefined *)(unaff_A6 + 0x27) = 0;
  if ((*(char *)((int)unaff_A6 + 0x21) == '\0') && (*(char *)(unaff_A6 + 0x11) == '\x02')) {
    _DAT_fffff032 = 0x1000;
    FUN_00003706((int)(short)*(short *)((char *)W68((short)unaff_A6[0x26]) + 0x3c));   /* +0x4c = the killer's record (16-bit link), its +0x3c = its HUD struct: both links mapped */
    *unaff_A6 = 0;
    *(undefined *)(unaff_A6 + 5) = *(undefined *)(unaff_A6 + 0x36);
    release_palette_slot();
    *(undefined *)(unaff_A6 + 5) = *(undefined *)((int)unaff_A6 + 0x6d);
    release_palette_slot();
    hide_object_sprite();
    if (*(char *)(unaff_A6 + 0x1f) == '\0') {
      DAT_fffff15d = DAT_fffff15d + -2;
#ifdef R60_LOCKPROBE
      { byte _f154o = DAT_fffff154; DAT_fffff154 = DAT_fffff154 + -1; port_f154_note(99, _f154o); }   /* F154LOG site 99: hand FUN_0000f1c0 (0xF230) */
#else
      DAT_fffff154 = DAT_fffff154 + -1;
#endif
      return 0;
    }
    DAT_fffff15a = 0;
    uVar1 = extraout_D0u;
    uVar2 = extraout_D1u;
    if ((DAT_fffff028 & 1) != 0) {
      p_D2 = CONCAT22((short)((uint)p_D2 >> 0x10),0xff00);
      uRamffffdf21 = 0;
      FUN_0000f28a(0xffffc000,(short int *)(0xffffdf00),CONCAT22(extraout_D0u,0x180),CONCAT22(extraout_D1u,0xfff8),
                   p_D2);
      uVar1 = extraout_D0u_00;
      uVar2 = extraout_D1u_00;
    }
    if ((DAT_fffff029 & 1) != 0) {
      uRamffffdfa1 = 2;
      FUN_0000f28a(0xffffc400,(short int *)(0xffffdf80),CONCAT22(uVar1,0xfe80),CONCAT22(uVar2,8),
                   CONCAT22((short)((uint)p_D2 >> 0x10),0x100));
    }
    DAT_fffff15b = DAT_fffff15b | 1;
    return 0;
  }
  *(byte *)(unaff_A6 + 0x27) = (0x11U - *(char *)(unaff_A6 + 0x11) & 0xf) * '\x02';
  return 0;
}
