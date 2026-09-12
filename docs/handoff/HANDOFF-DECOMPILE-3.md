# HANDOFF — the decompile thread, session 3

Written 2026-09-12. Working log `docs/log/LOOP-DECOMPILE.md`, now 96
entries. Session 2 ran 72-91; session 3 starts at 92.

**Read entries 50, 71 and 88 before anything else.** 50 is the provenance
rule, 71 is the duplication trap, and 88 is the night this thread found a
load-bearing number that had been wrong for weeks.

---------------------------------------------------------------------
## THE ONE THING THAT MATTERS RIGHT NOW

**`r60_push` is 2,621 instructions a vint and the 60 Hz gap on the 68000
is 1,898.** One routine is bigger than the whole shortfall.
`docs/handoff/PLAN-68K-BUDGET.md` has the arithmetic and its assumptions;
the handoff to the rendering thread is at the end of
`docs/handoff/NOTES-FROM-DECOMPILE.md`.

This only became visible because the figure everyone was working from —
"the game needs 2780 instructions per vint and the budget covers it" — was
2.5x low. `tools/arcade_trace.py` counted the lines MAME lists, and MAME
collapses a tight loop into one `(loops for N instructions)` line. Fixed.
ARCHITECTURE.md and START-HERE carry the correction in place.

---------------------------------------------------------------------
## SESSION 2 CLOSED ALL SIX OF SESSION 1'S OPEN ITEMS

  1. Scene 3 loads fine — a stale rom, not a game fact (72).
  2. The eleven bounding defects are 6 code + 5 data, diagnosed, not
     applied (73). `docs/audit/bound_repairs.md`.
  3. Rom data 74.2% named, computed rather than estimated (77, 83, 85).
  4. The dependency census is done for everything an opcode can signal,
     plus the hardware surface (76), plus the timing classes (82).
  5. The function map's biggest class was wrong and is rebuilt (78).
  6. The framebuffer bank figure was our own packet; retracted (74).

---------------------------------------------------------------------
## THE PIPELINE, AND IT NEEDS NO GHIDRA

The analysed Ghidra project is the artefact to open least — its own rule
is import once, seed once, never re-analyse. Everything below runs from
the rom and objdump. `TOOLKIT.md` carries it as a numbered recipe.

    code_walk.py       instruction addresses: descent to a fixpoint
                       (29.3% recall, 0 wrong) or --linear (99.8%, noisy)
    code_stream.py     addresses -> addr, length, mnemonic, operands
    hazard_census.py   TAS/STOP/MOVEP + the arcade hardware surface
    timing_hazards.py  cycle delays, interrupt delays, busy-waits
    bound_ref.py       function bounding audit
    func_profile_ref.py the map, arcade surface kept apart from work RAM
    rom_map.py         who points into every unattributed run
    actor_palettes.py  the 176-record sprite palette table
    round_workload.py/.lua, round_profile.py/.lua   per-round cost
    rom_reader_wp.lua  who READS a block nothing points at
    arcade_palram.lua, palette_oracle.py   the board's own palette RAM

**Take the linear superset for a hazard census** (missing a TAS is the
failure, a false positive costs one hand check) **and the verified set for
a rom map** (a false instruction inflates coverage and hides data).

---------------------------------------------------------------------
## WHAT SESSION 2 GAVE THE RENDERING THREAD

  - All five per-scene palette packs, with the attract-step gate that
    keeps them clean (75). 0xFFF031 says what is ON SCREEN; 0xFFF142 only
    says what is loaded.
  - The sprite palette table: 176 records of 28 bytes at 0x242A0, indexed
    by object $0B, copied verbatim. The transform is records 132-137
    cycled by the table at 0x26CC off the object's own anim timer, so it
    steps every SECOND frame (79). `docs/audit/actor_pal.h`.
  - The palette split is enforced by the program: tile palettes are 128 of
    EIGHT colours at 0x840000+p*16, sprite palettes 64 of sixteen at
    0x840800+s*32, and every tile writer is bounded below the boundary
    (80, 81). A refuse rule must stop there.
  - The arcade's own palette RAM as a reference, and a diff tool (81).
  - Level 4 is not heavy: below average on objects, sprites, drawn
    scanlines, zoom and 68K instructions (86, 87).

---------------------------------------------------------------------
## OPEN

  1. **~460 functions still classified by signature, not read.** The
     ranked list is the `arcade hw` rows of
     `docs/audit/function_map2.md` by callers. 50 are read. **And the map
     holds 78% of the code bytes (95)**: 16 KB in 230 runs — object
     routines reached by record pointers, never by a call — have no row.
     Bounding those is the first step of a Ghidra pass (item 5).
  2. **Sound.** Session 3 added the 68K side (95): `tools/sound_posts.py`
     names every sound-post site, the per-round music table at 0x1858,
     the round-clear jingle, the cutscene speech and the game-driven
     fade; handed over at the end of `docs/sound/HANDOFF-SOUND.md`. The
     Z80 driver decode itself is the sound thread's.
  3. ~~Tile palettes 19, 20, 21~~ DONE, session 3 (92): they colour the
     cutscene pages 10/11 (the chevron plane and its flames), laid on
     every scene load by 0x170A/0x174E. The cutscene switch is WRAM byte
     0xFFF148, and 0x3A00 is the page-select writer entry 59 could not
     find. Handed to the rendering thread in NOTES-FROM-DECOMPILE 17.
  4. **0x22000-0x232A0**, 4768 bytes of valid tile indices: no reference,
     and unread on all five rounds with the control firing (91). Its
     fields are level-class (palettes 47-121, no priority) and it abuts
     the rom palette block (93). Dead data or a path the rig does not
     enter; no cheaper method is left.
  5. **The bound repairs are not applied** to the Ghidra project (73).
     Do it with the rest of a Ghidra pass, not on its own. **Sized (96):**
     the pass has 16 KB of unmapped code to bound — 90 seeded entries
     reach 5 KB of it by rule, 11 KB in 218 fragments has no seed — and
     36 nested rows to untangle.

---------------------------------------------------------------------
## TRAPS SESSION 2 PAID FOR

  1. **A tool can commit the provenance failure too, and worse.**
     `rom_map.py` invented 62 pointers into one block by reading two
     record words as one longword. Sixty-two, all alike, all wrong. The
     tell is that a straddle can only ever produce ONE high word (83).
  2. **A number that is 2.5x low looks completely reasonable.** The
     collapsed-loop undercount gave a plausible instruction count and a
     plausible top four, twice, before a second measurement of the same
     thing disagreed with it (87, 88).
  3. **Put a control in every watch run.** The first read-tap rig reported
     nothing for two blocks AND for a table read every frame. Without the
     control that silence was a finding (85).
  4. **Verify a symbol map against the traced image.** `md_start.lst` and
     vi39 differ by 2453 bytes. Snapshot the rom WITH its map (90).
  5. **objdump wraps long instructions** onto a line with no mnemonic, and
     **MAME prints trace addresses in UPPERCASE**. Each one silently drops
     most of the data (73, 87).
  6. **Absence is only evidence with a scope.** "Not read" means nothing
     until you say on which levels, for how long, and that the control
     fired (85, 91).
