# HANDOFF — the decompile thread, session 3

Written 2026-09-12. Working log `docs/log/LOOP-DECOMPILE.md`, now 91
entries. Session 2 ran 72-91.

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
     `docs/audit/function_map2.md` by callers. 50 are read.
  2. **Sound. This thread has never touched it** and it is a stated
     deliverable — `docs/sound/`, and the goal is decoding music from the
     Z80 rom rather than tapping playback.
  3. **Three tile palettes — 19, 20 and 21 — are the most heavily
     animated thing in the game** (the cycler at 0x30B2, descriptors at
     0xFFF300) and belong to no scene's map. Nobody knows what they
     colour. Flagged three times, never done.
  4. **0x22000-0x232A0**, 4768 bytes of valid tile indices: no reference,
     and unread on all five rounds with the control firing (91). Dead data
     or a path the rig does not enter.
  5. **The bound repairs are not applied** to the Ghidra project (73).
     Do it with the rest of a Ghidra pass, not on its own.

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
