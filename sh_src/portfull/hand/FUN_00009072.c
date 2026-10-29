/* FUN_00009072 -- form 2 to the beast (0x9072), generated text with one correction: the 68K
 * clears D6 and D7 (0x90B6/0x90B8) before jumping into the per-round continuation at
 * 0x92F0[round], so no button edge reaches it; the port passed the register file's D6/D7 and
 * the fresh beast took the attack lookup (sound 0xB6) in the arcade's transformation tick
 * (demo A 0x24E, sound 0x46). */
uint FUN_00009072()
{
  int unaff_A6 = (int)_r_unaff_A6;
  
  FUN_00009052();
  FUN_000090f4();
  *(undefined *)(unaff_A6 + 0x4b) = 0;
  *(undefined *)(unaff_A6 + 0x2e) = 0;
  *(undefined *)(unaff_A6 + 0x6a) = 3;
  *(undefined *)(unaff_A6 + 0xb) = (&DAT_000092ea)[(short)(ushort)DAT_fffff142];
  if (DAT_fffff109 != '\0') {
    *(char *)(unaff_A6 + 0xb) = *(char *)(unaff_A6 + 0xb) + '\x01';
  }
  release_palette_slot();
  request_palette_update();
  *(undefined *)(unaff_A6 + 0x74) = *(undefined *)(unaff_A6 + 10);
  *(undefined *)(unaff_A6 + 0x2e) = 0;
                    /* WARNING: Could not recover jumptable at 0x000090cc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  port_set_reg(6, 0u); port_set_reg(7, 0u);   /* 0x90B6 moveq #0,d6 ; moveq #0,d7: the beast's continuation sees no button edges this tick */
  port_call68_at(6, LD32U((int)&PTR_FUN_000092f0 + (int)(short)((ushort)DAT_fffff142 << 2)));
  return 0;
}
