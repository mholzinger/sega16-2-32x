/* FUN_0000f28a -- spawner of the 0xF352 objects (0xF28A), generated text plus the A6
 * switch the 68K makes at 0xF302: `move.l a6,-(sp) ; movea.l a5,a6` before its two
 * release_palette_slot and two request_palette_update calls, restored at 0xF34E. Ghidra
 * kept A5 for the record and the port ran those calls on the PARENT, so the new record
 * never got its palette slot (+0x0a, +0x74, +0x75 read 0; measured 2026-10-04, demo A
 * tick 0xC4 slots 62/63). */
uint FUN_0000f28a(undefined4 p_A0,short *p_A5,undefined4 p_D0,undefined4 p_D1,undefined4 p_D2)
{
  p_A5 = (short *)A68PTR(p_A5);
  int unaff_A6 = (int)_r_unaff_A6;
  short sVar1;
  
  p_A5[0x30] = (*(ushort *)((byte *)&p_A0 + 2));
  p_A5[10] = (*(ushort *)((byte *)&p_D0 + 2));
  p_A5[0xb] = (*(ushort *)((byte *)&p_D1 + 2));
  p_A5[0xc] = (*(ushort *)((byte *)&p_D2 + 2));
  ST32((p_A5 + 1), 0xf352u);
  p_A5[0xd] = -0x40;
  *(undefined *)(p_A5 + 0x11) = 1;
  ST32((p_A5 + 0x12), 0xf69eu);
  *(undefined *)((int)p_A5 + 0x2f) = *(undefined *)(unaff_A6 + 0x2f);
  p_A5[0x20] = *(short *)(unaff_A6 + 0xc);
  p_A5[0x22] = *(short *)(unaff_A6 + 0x10);
  sVar1 = 0x80;
  if (((ushort)((ushort)(_DAT_fffff132 >> 0x10) + 0x50U) < *(ushort *)(unaff_A6 + 0xc)) &&
     (sVar1 = 0, (ushort)((ushort)(_DAT_fffff132 >> 0x10) + 0xf0U) < *(ushort *)(unaff_A6 + 0xc))) {
    sVar1 = -0x40;
  }
  p_A5[0x35] = sVar1;
  *(undefined *)((int)p_A5 + 0x4b) = 0;
  *(undefined *)(p_A5 + 0x10) = 0;
  if (DAT_fffff15b == '\0') {
    *(undefined *)(p_A5 + 0x10) = 1;
  }
  {
  void *_a6_saved = _r_unaff_A6;
  _r_unaff_A6 = (byte *)p_A5;   /* 0xF302 move.l a6,-(sp) ; movea.l a5,a6: the palette calls below run on the NEW record */
  if (*p_A5 < 0) {
    *(undefined *)(p_A5 + 5) = *(undefined *)(p_A5 + 0x3a);
    release_palette_slot();
    *(undefined *)(p_A5 + 5) = *(undefined *)((int)p_A5 + 0x75);
    release_palette_slot();
  }
  *p_A5 = -0x8000;
  *(undefined *)((int)p_A5 + 0xb) = 0x30;
  request_palette_update();
  *(undefined *)(p_A5 + 0x3a) = *(undefined *)(p_A5 + 5);
  *(undefined *)((int)p_A5 + 0xb) = 3;
  request_palette_update();
  *(undefined *)((int)p_A5 + 0x75) = *(undefined *)(p_A5 + 5);
  _r_unaff_A6 = _a6_saved;   /* 0xF34E movea.l (sp)+,a6 */
  }
  return 0;
}
