/* Native 0x152E2 is only `bra.s 0x15306`. Ghidra inlined the old initializer here, so the wave
 * table's kind 15 bypassed hand/FUN_00015306.c (7633eee3) and ran the generated copy, whose
 * FUN_000153ec read the 12-byte template through a raw 68K ROM address: zeros landed, word 0
 * never went negative, every part's free-slot search returned the same slot, and the spawner
 * backed F154 out. Measured on the level-5 replay (write-watch, tick 0x04D8): the group the
 * arcade spawns at frame 2160 (head 0x15434, parts 0x15B1E) never appeared on the port.
 * Same repair as hand/thunk_FUN_00014a8c.c: a real tail call into the repaired body. */
uint thunk_FUN_00015306(undefined2 *p_A5)
{
  return FUN_00015306(p_A5);
}
