/* FUN_00005062 -- a player form's attack lookup (0x5062), hand-written like hand/FUN_0000a3b0.c:
 * from D7 (bits 6-7 = facing) and D6 (bits 1-2 = button edges) it leaves the transition
 * handler in A0 with carry SET, or carry CLEAR for none. The callers do `bsr ; bcc skip ;
 * jmp (a0)`; Ghidra returned a D0 fragment and a stale bVarN for the carry, so the port
 * never took an input-driven transition while walking (demo A tick 0xED: the arcade's hero
 * starts its dash with sound 0xB6 and the trail, the port keeps walking). Returns the
 * 68K handler address or 0; the call sites jump through port_call68_at. */
uint FUN_00005062(undefined4 p_D0,undefined4 p_D6,uint p_D7)
{
  byte *_a6 = (byte *)_r_unaff_A6;
  uint _d0 = (uint)p_D7 & 0xc0u;
  if (_d0 != 0) {
    _a6[0x2e] = _a6[0x2e] & 0x7f;
    if ((_d0 & 0x80u) != 0) _a6[0x2e] = _a6[0x2e] | 0x80;
  }
  _d0 = (uint)(byte)p_D6 & 6u;
  if (_d0 == 0) return 0;
  if ((_d0 & 2u) != 0) return (((uint)p_D7 & 0x10u) != 0) ? 0x50cau : 0x5002u;
  return (((uint)p_D7 & 0x10u) != 0) ? 0x51dau : 0x5178u;
}
