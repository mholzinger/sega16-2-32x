/* FUN_00002580 -- TWO tile-RAM blits, not one, and the second one had no page mark.
 *
 *   2580: lea 0x2726c,%a0        the rectangle list
 *   2586: bsrw 0x258a            blit ONE rectangle; 0x258a leaves A0 past its data
 *   258a: movel #0x400000,%d0    ... and FALLS THROUGH into 0x258a a second time,
 *                                which blits the NEXT rectangle from the advanced A0
 *                                and whose own rts returns to 0x2580's caller.
 *
 * So this is exactly two sequential FUN_0000258a calls, and that is how it is written here.
 *
 * WHY IT IS A HAND FILE (2026-10-11). The decompiler could not see the fall-through as a
 * call: it emitted the first one as `FUN_0000258a(...)` and INLINED the second as its own
 * store loop (altbeast_port.c:3756, `A68VID(0x400000u | ...)` with the same 0x40-word row
 * step). FUN_0000258a is a hand file carrying the port_tile_dirty mark, so the first blit
 * marked its pages and the inlined second one marked nothing.
 *
 * MEASURED, PORTTILEDIRTY=1 with the 13-page hash kept on as the oracle: demo B frames
 * 1660-2300 read 25 hashed page changes, 12 of them with NO mark, miss bitmap 0x0FFE
 * (pages 1-11). FUN_00002552 was the suspect on the record and is NOT one: it has no store
 * loop at all, it calls 0x258a eight times in a dbf and inherits the mark.
 *
 * Over-marking costs a page upload; under-marking costs a wrong picture (STATE.md). */

uint FUN_00002580()
{
  /* the first blit returns A0 advanced past its own rectangle -- the convention
   * protos.h already carries and the dbf callers at 0x2552/0x2564/0x2572 rely on */
  undefined2 *puNext = (undefined2 *)FUN_0000258a((undefined2 *)(&DAT_0002726c));
  FUN_0000258a(puNext);                 /* 0x2586's fall-through into 0x258A */
  return 0;
}
