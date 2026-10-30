/* clear_tileram -- 0x36B0, the whole tilemap to zero, WITH THE PAGE MARK.
 *
 *   36b0: moveal #0x400000,%a0      tile RAM base
 *   36b6: movew #16383,%d0          16384 longs = 65536 B = the ARCADE's 16 pages
 *   36ba: moveq #0,%d1
 *   36bc: movel %d1,%a0@+           one long
 *   36be: dbf %d0,0x36bc            ... 16384 times
 *   36c2: rts
 *
 * THE COUNT IS 0x33FF HERE, NOT 0x3FFF, and that is not a transcription slip: the port's
 * shadow is 13 pages, not the arcade's 16, and cport_prep already narrows the generated
 * body for exactly that reason (altbeast_port.c, "13 pages: the port's shadow"). Clearing
 * 16 would run three pages past the shadow.
 *
 * WHY IT IS A HAND FILE (2026-10-11). It is a TILE-RAM WRITER WITH NO PAGE MARK, and it was
 * the last one. MEASURED on rom 53ab3fbd over emu 440-480: clear_tileram ran (profile delta
 * 266,286 instructions) while every rectangle blitter read delta 0, and the tilemap went
 * from 11 of 13 pages all-zero at emu 440 to 13 of 13 at emu 460. The two pages that were
 * NOT already zero at 440 were 2 and 7 -- and pages 2 and 7 are exactly the pair the dirty
 * census reported as NEVER marked (pp_never_pages 0x0084). Clearing an already-zero page
 * changes nothing, so those two were the only pages whose content could move, and this
 * function moved them without naming either.
 *
 * THE YIELD STAYS WHERE cport_prep PUTS IT. _clear_pace (cport_prep.py:4525) inserts
 * `if ((sVar1 & 0x1FFF) == 0) wait_frame_flag();` after the decrement -- the 68K pace, so
 * the renderer is not starved through a 53 KB memset. The pace passes run BEFORE the hand
 * swap (cport_prep.py:4525 against :5397), so a hand body does not inherit it and has to
 * carry it. Counting down from 0x33FF the test fires at sVar1 == 0x2000 and 0, which splits
 * the clear FIVE pages then EIGHT.
 *
 * MARK ALL THIRTEEN, AND MARK THEM BEFORE EVERY YIELD -- not once at the end. Marking only
 * at the end was MEASURED WRONG (rom 57467f6f): page 7 came clean but page 2 stayed NEVER
 * marked, because page 2 is cleared in the FIRST chunk and the census runs inside the yield
 * that follows it, sees page 2 changed, and the end-of-function mark has not happened yet.
 * The census also CLEARS port_tile_dirty when it runs, so one mark at the top would not
 * survive either -- the mark has to be re-established on each side of every yield.
 * Over-marking costs a page upload, under-marking costs a wrong picture (STATE.md). */
#ifdef PORT_TILEDIRTY
#define CLEAR_MARK_ALL() do { uint _p; for (_p = 0; _p <= 12u; _p = _p + 1)                                   port_tile_dirty |= (ushort)(1u << _p); } while (0)
#else
#define CLEAR_MARK_ALL() ((void)0)
#endif
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;   /* the renderer's half, sh_src/m_main.c */
#endif

uint clear_tileram()
{
  undefined4 *puDst = (undefined4 *)&DAT_00400000;
  short sVar1 = 0x33ff;                   /* 13 pages of the port's shadow */

  do {
    *puDst = 0;                           /* 0x36BC movel %d1,%a0@+ */
    sVar1 = sVar1 + -1;
    if ((sVar1 & 0x1FFF) == 0) {
      CLEAR_MARK_ALL();                   /* before the census can run in the yield */
      wait_frame_flag();                  /* the 68K pace: cport_prep.py */
    }
    puDst = puDst + 1;
  } while (sVar1 != -1);                  /* 0x36BE dbf %d0 */

  CLEAR_MARK_ALL();                       /* and again after the last chunk */
  return 0;
}
