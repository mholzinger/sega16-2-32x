/* Native 0x14A78 is only `bra.s 0x14A8C`. Ghidra inlined the old initializer
 * here, bypassing hand/FUN_00014a8c.c when the wave table selected kind 11.
 * Keep the entry as a real tail call so both paths use the repaired allocator,
 * record links and ROM templates. */
uint thunk_FUN_00014a8c(undefined2 *p_A5)
{
  return FUN_00014a8c(p_A5);
}
