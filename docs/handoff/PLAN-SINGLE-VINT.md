# PLAN — one generation per vint

Written 2026-09-12 by the decompile thread at Mike's request. Every number
below is someone's measurement with its entry named; the two steps marked
UNMEASURED are proposals with a kill condition, not results.

---------------------------------------------------------------------
## The bar, in one line

**The generation wall must cross BELOW 1.00 vint.** It is a threshold
(LOOP29 168): ships are vint-quantised, so a generation finishing at 1.2
vints costs 2 and flips at 30 Hz. Nothing is paid until the wall is under
1.00; then everything is paid at once. The ablation that proved it:

    vi16 (full)                     wall 1.60v   15% single-vint
    - master maps drain                  1.31v   27%
    - maps AND sprite compose            0.90v   98%   <- 58.3 fps
    - maps, sprites AND cat1             0.45v   99%

So the transport, flip, DREQ, echo chain and 68K handler fit in one vint
with room. The compute is what stands over the line.

---------------------------------------------------------------------
## Where the line is today (vi62b)

    generation wall                 1.47v        single-vint 20%
      slave compose                 1.09v/gen    sprites 0.59 + cat1 0.51
      master maps drain             0.44v/gen    worth 0.29 of wall by ablation
      handoffs / quantisation       the rest

The best measured build is not on the line: CAT1MD + C1NOFB (tiles drawn
by the VDP, no FB cat1 pass) reads **wall 1.12, 40-45% single-vint**
(LOOP29 175, 189) and was withheld because its backgrounds broke — the
cat1 sets went to the dynamic allocator and it churned.

---------------------------------------------------------------------
## The steps, in order, each with its gate and its kill

### Step 1 — the static tables, whole level. UNBLOCKS THE 1.12 BUILD.

LOOP-DECOMPILE 98: every round's baked table (`pal_rounds_md.h`) was
built from PAGE 0 of each plane (`bake_tilecram.py:105`, `range(64)`);
levels are five pages. Round 0 lacks six FG sets on 1,226 cells — the
ramps and ledges Mike stood on with nothing under him. Feed the bake all
five pages per plane (`tools/scene_sets.py` lists what is missing).

    Gate:  the ledges draw; C1NOFB on the line; wall ~1.12, single-vint
           ~40% on ares (LOOP29 175's rig); Mike's pass for missing tiles.
    Kill:  a round's whole-level colours do not pack into four lines.
           Then the table is keyed on the PAGE, not the round: the game
           publishes which pages are on screen every frame in its own
           shadows 0xFFF0F4/F6 (LOOP-DECOMPILE 92, the tables at
           0x40F0/0x4100), and the shim already carries the round in
           COMM10 bits 13-15. Two nibbles more.

### Step 2 — the master maps drain: bake the name tables. UNMEASURED.

At 1.12 the remaining mass is slave sprites+text 0.60 and the master's
`build_maps_chunk` at 0.44 v/gen, 0.29 of wall by ablation (LOOP29 168),
never optimised (177). It derives MD name-table words from the tilemap
and the set->line assignment. After step 1 BOTH inputs are static rom
data: the tilemap is unpacked from the rom once per scene (LOOP-DECOMPILE
10) and the line table is baked. The name-table image for every page is
therefore computable at bake time; the drain becomes a copy of the
visible slice on scroll change, plus the runtime residue (the tile-RAM
writers the census names: LOOP-DECOMPILE 20, 85 — the 14 upload blocks
and the two cutscene writers).

    Expected: wall 1.12 - up to 0.29 = 0.83-0.95. On paper this is the
              step that crosses the threshold.
    Gate:     wall < 1.00 and single-vint > 90% on the 168 rig; pixel
              parity of the name tables against the live build (a diff of
              VRAM, exact, no eyes needed).
    Kill:     the residue of runtime tile writes is large enough that the
              copy is not cheaper than the build. MEASURED (LOOP-DECOMPILE
              99): the residue in play is ZERO. Tile RAM is written at
              scene load, round clear, attract steps and boot only, each
              from rom data indexed by one WRAM byte. The kill does not
              fire.

### Step 3 — margin under the threshold. Only if step 2 lands at 0.9-1.0.

A generation at 0.95 that jitters to 1.02 costs 2 vints; 168's 0.90 gave
98%. Two reserves, both measured elsewhere, neither needed on paper:

  - the slave's clear (part of the 0.59): clear only what was drawn.
  - sprites to the VDP for the unscaled 96% (HANDOFF-20260910 2c) —
    capped by palette lines, which section 3 there proved exhausted. Do
    not reopen without new physics.

### Step 4 — the 68000 at true 60 Hz. RUNS IN PARALLEL, decompile thread.

Once a generation is one vint the 68K must run a FULL game frame and the
shim every vint. Measured (LOOP-DECOMPILE 89-90, 97):

    game frame           8,900-9,800 instructions, by content (arcade 8,184;
                                     LOOP-DECOMPILE 100 -- the "20% protocol
                                     cost" of entry 89 was two unaligned windows)
    shim, R60TIGHT       3,590
    needed              12,500-13,400   against 12,420 available
    over by              1-8% by instruction count

One lever left here, and it is enough at the low end: the rest of
r60_push (rotor ~430, mask walk ~330). R60TIGHT is staged
(`rom/night/r60tight1.32x`) and off. The 68000 is not what stands
between the line and one vint; step 2 is.

    Gate:  gameplay_speed.py logic 100% at single-vint, no GAMEGATE stall.

### Step 5 — hardware truth, every step.

ares-headless for the wall and single-vint; the rig's BOOTGAMERATE for
the game's own dropped-frame count; Mike's play pass as the acceptance
gate. MAME gates nothing on the SH-2 side (CLAUDE.md), and every ares
number here is an upper bound on hardware (LOOP29 140).

---------------------------------------------------------------------
## What could make this wrong

  - Step 2 is a proposal. The 0.29 is an ablation (the build renders
    wrong), so the saving is an upper bound on what a correct copy gets.
  - The 1.12 was measured on vi26's line; the transport has changed
    since. Re-measure on the step-1 build before subtracting from it.
  - The 68K cycle figure is MAME's; hardware adds the adapter fetch tax
    RAMCODE was built to dodge (md_main.c 1312). Read the rig's counter.
  - Whole-level colour packing has not been run. Step 1's kill is live.

## Order of work

    rendering thread   step 1 now (the bake), then step 2
    decompile thread   step 2's census (runtime tile writers, exact),
                       step 4's two levers, the page-keyed table if
                       step 1's kill fires
