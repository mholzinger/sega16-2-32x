uint FUN_0000f77c(undefined4 p_A0,undefined4 p_D1)
{
  ulonglong _r = FUN_0000e23c((int)p_A0, p_D1);
  byte *_a0 = (byte *)(uint)(_r >> 0x20);
  /* 0xF77C: `jsr e23c ; cmpi.w #32,d0 ; bcc clear ; tst.b ($2f,a0) ; beq clear ; set` */
  port_carry = (port_e_d0 < 0x20u) && (_a0[0x2f] != 0);
  return (uint)_a0;
}
