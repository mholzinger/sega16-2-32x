/* HAND: FUN_00008358 -- the owner pointer at +0x3C is a 68K address; the generated body dereferenced the
 * sign-extended value raw (SH-2 0xFFFFC000, not work RAM) and copied zeros into +0x0C/+0x10/+0x2F.
 * First level-1 divergence at tick 0x69D2 (trail followers); 68K 0x8358. */
uint FUN_00008358(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)
{
  undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;
  byte bVar1;
  int iVar2;
  
  iVar2 = (int)W68((short)unaff_A6[0x1e]);
  unaff_A6[8] = *(undefined2 *)(iVar2 + 0x10);
  unaff_A6[6] = *(undefined2 *)(iVar2 + 0xc);
  *(undefined *)((int)unaff_A6 + 0x2f) = *(undefined *)(iVar2 + 0x2f);
  bVar1 = *(byte *)(unaff_A6 + 0x27);
  *(byte *)(unaff_A6 + 0x27) = bVar1 - 3;
  if (bVar1 < 3) {
    *unaff_A6 = 0;
    release_palette_slot();
    hide_object_sprite();
    return 0;
  }
  FUN_0000669c();
  sprite_build_and_cull(p_D3,p_D6,p_D7);
  return 0;
}
