/* HAND: FUN_00019a04 (Neff -> final boss). Today's generated body with ONE change at the tail: the
 * two hides run on slots 42 and 43, as 68K 0x19ACA/0x19AD4 do (lea 0x80(a6),a6 before each 0x3F04);
 * Ghidra dropped both advances, so the port hid slot 40 twice and left the old Neff sprite on screen. */
uint FUN_00019a04()
{
  _r_unaff_A6 = (byte *)W68PTR(0xFFD400u);   /* entry lea into A6: a6_callsites.json */
  undefined uVar1;
  undefined uVar2;

  uVar2 = DAT_ffffd488;
  uVar1 = DAT_ffffd408;
  DAT_ffffd408 = DAT_ffffd508;
  DAT_ffffd508 = uVar1;
  DAT_ffffd488 = DAT_ffffd588;
  DAT_ffffd588 = uVar2;
  _DAT_ffffd400 = 0x8000;
  _DAT_ffffd402 = 0x19adeu;
  DAT_ffffd409 = 2;
  _DAT_ffffd41c = 0x80;
  _DAT_ffffd41e = 0x800;
  _DAT_ffffd440 = (ushort)(_DAT_fffff132 >> 0x10) + 0x100;
  _DAT_ffffd444 = 0x10d8;
  DAT_ffffd420 = 0;
  DAT_ffffd421 = 0;
  DAT_ffffd422 = 0x1e;
  _DAT_ffffd424 = 0x1a12au;
  DAT_ffffd42e = 0x80;
  DAT_ffffd42f = 2;
  DAT_ffffd44b = 0;
  DAT_ffffd44a = 0x40;
  _DAT_ffffd43e = 0x78;
  DAT_ffffd40b = 0x7e;
  {   /* the caller loads A6 with 0xFFD400 here and Ghidra drops it;
       * docs/audit/a6_callsites.json */
      byte *_a6sv = _r_unaff_A6;
      _r_unaff_A6 = (byte *)W68PTR(0xFFD400u);
      request_palette_update();
      _r_unaff_A6 = _a6sv;
  }
  DAT_ffffd46e = DAT_ffffd40a;
  DAT_ffffd40b = 1;
  request_palette_update();
  DAT_ffffd46f = DAT_ffffd40a;
  _DAT_ffffd480 = 0x8000;
  _DAT_ffffd482 = 0x19bf0u;
  DAT_ffffd489 = 2;
  DAT_ffffd4af = 2;
  _r_unaff_A6 = (byte *)W68PTR(0xFFD500u);   /* 0x19ACA lea 0x80(a6),a6 (A6 was 0xFFD480) */
  hide_object_sprite();
  _r_unaff_A6 = (byte *)W68PTR(0xFFD580u);   /* 0x19AD4 lea 0x80(a6),a6; jmp 0x3F04 leaves A6 here */
  hide_object_sprite();
  return 0;
}
