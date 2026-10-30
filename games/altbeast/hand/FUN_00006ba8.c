/* FUN_00006ba8 -- five-record trail spawner (0x6BA8), generated text with the A6 save/restore
 * the 68K does around its five calls of FUN_00006c44 (which moves A6 by 0x80 each call;
 * see hand/FUN_00006c44.c). */
uint FUN_00006ba8(undefined4 p_D0,undefined4 p_D1,undefined4 p_D2)
{
  undefined2 extraout_D1u = (undefined2)_r_extraout_D1u;
  undefined2 extraout_D1u_00 = (undefined2)_r_extraout_D1u_00;
  undefined2 extraout_D1u_01 = (undefined2)_r_extraout_D1u_01;
  undefined2 extraout_D1u_02 = (undefined2)_r_extraout_D1u_02;
  int unaff_A6 = (int)_r_unaff_A6;
  void *_a6_saved = _r_unaff_A6;   /* 0x6BA8+e move.l a6,-(sp) */
  undefined4 uVar1;
  undefined2 uVar2;
  
  if (*(char *)(unaff_A6 + 0x6a) == '\0') {
    return 0;
  }
  if (*(char *)(unaff_A6 + 0x49) != '\0') {
    uVar2 = (undefined2)((uint)p_D2 >> 0x10);
    uVar1 = FUN_00006c44(0x3d60u,0x7b96u,unaff_A6,CONCAT31((int3)((uint)p_D0 >> 8),0x10)
                         ,CONCAT22((short)((uint)p_D1 >> 0x10),4),
                         CONCAT22(uVar2,*(short *)(unaff_A6 + 100) + 0x1c));
    extraout_D1u_02 = (uint)uVar1;
    extraout_D1u_01 = (uint)uVar1;
    extraout_D1u_00 = (uint)uVar1;
    extraout_D1u = (uint)uVar1;
    uVar1 = FUN_00006c44(0x3d60u,0x7b96u,unaff_A6,
                         CONCAT31((int3)((uint)uVar1 >> 8),0x14),CONCAT22(extraout_D1u,4),
                         CONCAT22(uVar2,*(short *)(unaff_A6 + 100) + 0x14));
    uVar1 = FUN_00006c44(0x3d60u,0x7b96u,unaff_A6,
                         CONCAT31((int3)((uint)uVar1 >> 8),0x18),CONCAT22(extraout_D1u_00,4),
                         CONCAT22(uVar2,*(short *)(unaff_A6 + 100) + 0xc));
    uVar1 = FUN_00006c44(0x3d60u,0x7b96u,unaff_A6,
                         CONCAT31((int3)((uint)uVar1 >> 8),0x1c),CONCAT22(extraout_D1u_01,4),
                         CONCAT22(uVar2,*(short *)(unaff_A6 + 100) + 4));
    FUN_00006c44(0x3d60u,0x7b96u,unaff_A6,CONCAT31((int3)((uint)uVar1 >> 8),0x20),
                 CONCAT22(extraout_D1u_02,4),CONCAT22(uVar2,*(short *)(unaff_A6 + 100) + -4));
  }
  _r_unaff_A6 = _a6_saved;   /* movea.l (sp)+,a6: A6 back to the player after five 0x6C44 calls moved it */
  return 0;
}
