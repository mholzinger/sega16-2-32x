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

### Step 2 — the master maps drain: bake its tilemap scan. STATIC HALF BAKED (LOOP-DECOMPILE 101).

At 1.12 the remaining mass is slave sprites+text 0.60 and the master's
`build_maps_chunk` at 0.44 v/gen, 0.29 of wall by ablation (LOOP29 168),
never optimised (177). Read (101): it is the compositor's colour-group
and priority-LUT builder. Its scan walks 2,464 tilemap cells per plane
per generation to learn which sets the viewport holds and at which cat
bits; its tail scans text and sprites and allocates groups. Tile RAM is
static in play (99), so the scan is a function of rom + scroll:
`tools/bake_setcols.py` / `sh_src/setcols_md.h` hold it as per-column
set extents, 9-17 KB a scene, proven exact on 4,000 windows. The tail
stays.

    Expected: wall 1.12 minus the SCAN's share of 0.29 (scan/tail split
              unmeasured; the scan is the cell walk, the tail is text +
              sprites + groups). On paper this is the step that crosses
              the threshold if the scan is most of the 0.29.
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

---------------------------------------------------------------------
## THE FOLDS, EVALUATED AFTER LOOP29 215-227 (decompile thread, 2026-09-12 15:30)

The line is vi70 (accepted 14:50): the whole-level tables, MDSREFUSE,
no edge-out wipe, batch 24 everywhere. vi72 adds the typewriter gate and
the trampoline's COMM10 mask and is on the rig. What the builder's
entries since 215 say about the machine, read as IPC facts:

  1. **COMM10 is a shared word with three writers and no field
     discipline.** The dirty mask's regions 13-15 leaked into the round
     field (222); the assembly trampoline posted the word raw (226); and
     after both masks the same-round return STILL moves with any 68K
     layout change (227: two gate entries that never run shift the
     plane's arrival by 50 frames). That is a read-side race: the SH-2
     samples COMM10 at an edge and gets whichever writer posted last.
     Every "flaky return" since 217 is this one hazard.
  2. **Text RAM writers are gated one at a time.** The round-clear
     typewriter (225) is the latest of 61 text writers (LOOP-DECOMPILE
     14) found outside an FM gate; TEXTCAPMASK was withdrawn for dropping
     a cutscene writer (171). The class is not closed by any gate.
  3. **The transport lives in the game's enforced idle.** RELBANK (183)
     crossed the quantum -- wall 0.81 -- and the screen stopped because
     the discarded vint was where the post and blast ran (184).

### The folds, in order, with what each needs

**Fold 1 — tiles to the VDP on the accepted line.** vi70's line still
runs the FB cat-1 pass: CAT1MD/C1NOFB are not in its build line. The
whole-level tables were the blocker (route 1's "pack and install
disagree" is what 215-223 fixed), so `CAT1MD=1 DIRTYROW=1 C1NOFB=1` on
vi70's line is the next build. Expected wall 1.47 -> ~1.12, single-vint
20 -> 40-45% (175, 189). Owner: rendering. Gate: Mike, for the hole
punch over sprites.

**Fold 2 — the maps scan from the bake.** `sh_src/setcols_md.h` (101),
exact, unwired. Expected: the scan's share of 0.29. Measure the
scan/tail split first (PHASECENSUS); if the tail is most of it, this
fold is small and the sprite scan in the tail is the next thing to look
at. Owner: rendering.

**Fold 3 — consume the release (RELBANK), the crossing.** 184 concluded
"removing our handler entirely still leaves ~1.05" from the 2,780 x 46
arithmetic that entry 88 retracted. Re-derived with the measured
numbers (LOOP-DECOMPILE 100, 97):

    the game's pass     8,184-9,800 instr x 10.08 cyc   0.64-0.77 vint
    our shim, R60TIGHT  3,573 x 10.08                   0.28
    together                                            0.92-1.05

So the 68000 side of the crossing is within reach: the rotor (~430) and
r60_blast/md_consume (~860 together) are the margin. What is NOT solved is 184's real finding, now read (LOOP-DECOMPILE 104): the
transport already runs in IRQ4 and in the gate spin, not in the idle.
What RELBANK adds to the game's pass is the FM-gate spins -- five entries
a vint into three text writers, each a wait on the master's window (8-120
lines on hardware) -- and a 4.9% post deferral from the span test. **Fold
5 is therefore the precondition of fold 3**: the three writers go to the
WRAM mirror first, then RELBANK, measured on the rig with BOOTGATECHK. That is a
protocol change on the MD side, and it is the fold that pays: 0.81 was
measured, 53 fps, and everything before it is preparation. Owner:
rendering for the schedule, this thread for the 68K budget.

**Fold 4 — one state word, one writer.** Replace the COMM10 round field
and the claim-mix detector with a state word the 68K posts once per vint
from IRQ4, no field shared with the dirty mask: round (0xFFF142),
cutscene (0xFFF148), the page nibbles (0xFFF0F4/F6), and a sequence
number so the SH-2 can tell a fresh post from a stale one. Retires the
same-round-return race (227) by construction and the claim mix with it.
Cheap; the bytes are all read already. Owner: rendering, from
NOTES-FROM-DECOMPILE 17.

**Fold 5 — the text path.** Route text writers through the WRAM mirror
(TXTWRAM) and ship from there, instead of FM-gating writers as they are
found. 61 writers (LOOP-DECOMPILE 14); the census names every one.
Closes the class the typewriter belongs to. Owner: rendering; the
writer list is this thread's.

### What this thread does while the builder wraps up

  - the transport-scheduling census for fold 3: which 68K work runs in
    the game's wait path versus IRQ4, from the shim source and the
    patch table, so the move is a list and not a search;
  - the rotor, when fold 3 is scheduled and not before;
  - the text-writer list for fold 5, with each writer's gate state.

---------------------------------------------------------------------
## AFTER FOLD 1 ON THE RIG (decompile thread, 2026-09-12 21:30, LOOP29 230-231)

Two checks from the rom on vi75's black sets (76-79, 93-101 black;
74, 75, 85, 86, 92 drawn), so nobody re-runs them: no round's table
matches the split (round 0 keeps all fifteen; every other round drops
drawn sets), and it is not the cat-1 category either (93-101 are BG
sets with 0% priority cells; drawn 85/86 are 94%). Per cell, timing
only, as 231 says.

Two thoughts, both about ORDER:

  1. **Fold 4 before the 24-bit channel.** The two SH-2 mechanisms that
     depend on timing alone are the claim-mix flag (mds_onscreen, per
     window, from t > n) and the COMM10 edge read. When the flag reads
     OFF in play, the level's sets go to the dynamic allocator and churn;
     when the edge read lands on a stale or torn word, a wrong table is
     installed. Both produce per-cell black that persists, and both
     change with windows-per-vint, which is what the rig changes. Fold 4
     (round + 0xFFF148 + page nibbles + a sequence number, one writer,
     posted from IRQ4, nothing shared with the dirty mask) deletes both
     mechanisms rather than observing them. It is one COMM field and
     bytes the shim already reads. If the black survives fold 4, the
     channel is the right next instrument and it will have two fewer
     suspects.
  2. **Measure the wall on vi75.** Nothing since 175 has read the
     generation wall on any build; 230 has motion and flips. One
     PHASECENSUS run on vi75 and on vi70 puts "where are we" in vints
     instead of proxies. By 175's measurement of the same flags, vi75
     should sit near 1.12; if it does not, fold 1 did not land the way
     the plan priced it.

The Zeus text and the round-clear bonus text on vi75 are the parked
text gates (225/229), not fold 1: vi75 is vi70's line without them.
Fold 5 is the fix that does not re-roll the 68K phase every time a
writer is found.

---------------------------------------------------------------------
## STATE 2026-09-13 00:30, AND THE NEXT THREE BUILDS

    line (presentation)   vi70    wall 1.48   18% single-vint   (ares, 232)
    fold-1 build          vi95    wall ~1.11  44%               vi75's line +
                                                                 the state word from
                                                                 the game's bytes
    rig, presented frames per 64 vints   fr75 21 19 7 22 16   fr95e 19 15 22 21 14

What is measured and closed since the plan was written: fold 1 lands
the speed it was priced at (vi75, 230); fold 4's two free halves are in
(the round outside the dirty mask, the transformation forced off; 233,
240); the credited/demo decision is the game's own bytes (NOTES 29);
the transport is proven intact on the FPGA (231); fold 3's blocker is
the FM-gate spins, not the idle (104); fold 5's copy must ride the
packet side (237). Open on vi95: sprites over cat-1 ground (fold 1's
hole punch), residual black tile drops on the rig (a ship-rate knob,
237), the parked text gates (fold 5).

**Build A -- the hole punch (fold 1's gate).** Sprite loop: for a pixel
over a FOREGROUND cell, `CAT1HOLE_GET` from `sh_src/cat1hole.bin`
(NOTES 31): 0 draw, 1 skip the cell, 2 test the tile pixel. 31% of
scene 0's cat-1 cells take the per-pixel path; the rest are one
compare. Owner: rendering. Gate: Mike sees the zombies rise behind the
ground; the wall does not move (this is correctness, priced at ~0).
Kill: none -- the data is rom and the rule is the RTL's (jts16_prio.v).

**Build B -- fold 2, the maps scan from the bake.** Replace
`bm_scan_rows`'s cell walk with the per-column extent lookup in
`sh_src/setcols_md.h` (NOTES 21), keyed on the scene from the state
word. Before building, one PHASECENSUS run on vi95 splitting
`build_maps_chunk` into scan and tail: the scan's share of the 0.44
v/gen is the ceiling of this build. Owner: rendering. Gate: wall on
ares, single-vint share; the name tables byte-identical to vi95's on
the same input script (a VRAM dump diff, no eyes). Kill: the scan is
under 0.1 v/gen -- then the tail (text and sprite scan, group
allocation) is the mass and this bake is a small win.

**Build C -- fold 5 with the copy on the packet side.** The three
credited-play text writers (NOTES 27: score at [record+8], high score
at 0x4100D2, the lives icons at [record+8]+128; plus the credit line
and health bar TXTWRAM already mirrors) marked with (offset, words) and
shipped in the r60 packet from WRAM, never a 68K framebuffer write
before the post. Owner: rendering. Gate: fr on the rig >= fr75 (237's
cost gone), text complete on the round-clear screen. Then fold 3
(RELBANK) on the rig with BOOTGATECHK, which this fold unblocks (104).

**This thread, in parallel:** the rotor in r60_push once B has crossed
or C is scheduled; the fold-3 rig probe's reading when it runs; any
byte the builder asks for, answered from the consuming instruction
first (entries 50, 105).

The bar is unchanged: one generation per vint, wall under 1.00. B is the
build that moves it; A and C are the builds that make B shippable.

---------------------------------------------------------------------
## STATUS 2026-09-13 00:50 (builder, LOOP29 240-242)

    fold   state                                   ares wall     single-vint
    1      vi75 line, plays (vi95 = + fold 4)       1.11          44%
    4      MDSTATE=1: round + transformation from   free          --
           the word; attract decisions from the
           game's bytes (NOTES 29); claim mix kept
    2      SETCOLS=1 (sc95): scan 0.279 -> 0.113,    1.03          55%
           tail 0.113 unchanged; picture unchanged
    5      TXTWRAM as written halves the rig's       --            --
           frame rate (237): copy must move to the
           packet side before RELBANK
    3      after fold 5 (census, NOTES 24)

Rig frame rate (BOOTFLIPRATE, presented frames per 64 vints, attract):
fr75 21 19 7 22 16; fr95e 19 15 22 21 14; sc95 pending. Open on the
fold-1 line: the hole punch (C1PUNCH=1 built, unmeasured), the rig's
black drops (ship rate, 237).

---------------------------------------------------------------------
## HOW A BUILD LANDS (2026-09-13)

One build, one change against the line, one name, one md5. The line
stays `rom/s16.32x` until Mike names a new one.

  1. **The builder ships with a card:** name, md5, the exact make line,
     the ONE change against the line in a sentence, and the gates run
     (ares wall / single-vint, the aligned attract return, the play
     path, the rig frame-rate probe where the change touches the 68K or
     the ship rate). A change that decides anything from a game byte
     cites the instruction that consumes that byte.
  2. **The decompile thread reads the diff before Mike plays:** the
     code compiled into the rom against the previous build (not the
     log's description of it), any game byte the build reads, and
     whether anything retracted in the logs re-entered. Reply in one
     paragraph: matches the card / does not, and what to look for.
  3. **Mike plays with the card's one question** -- for Build A: do the
     zombies rise behind the ground; for B: does it look like vi95 and
     what does the wall read; for C: is the round-clear text complete
     and the rig frame rate at or above fr75.
  4. **Decision:** pass on Mike's eye plus the card's numbers -> the
     line moves. Anything else -> withdrawn with the number that failed,
     in the log, and the next build is built on the line, not on the
     withdrawn build.
  5. **Never** two changes in one build, never a probe as a play build,
     never a launch on the rig while Mike is playing (the relaunch of
     vi95 at 22:34), and never a "fix" that has not been A/B'd on the
     instrument that can see it (172: ares cannot rank rig speed).

## STATUS 2026-09-13 02:10 (builder)

fold 2 exact (check 0/0 with the corrected header, LOOP29 243) and
measured: wall 1.16 -> ~1.05 v/gen ares, single-vint 42 -> 49-55%; the
rig's frame rate did not move (its wall is windows per vint). Fold 1's
hole punch is built per pixel (C1PUNCH=1, c1p95b, LOOP29 244b), Mike's
eye pending. Order stands: fold 5 with the copy on the packet side, then
fold 3 / RELBANK on the rig with BOOTFLIPRATE + BOOTGATECHK.

---------------------------------------------------------------------
## BUILD A PASSED (Mike, 2026-09-13): the zombies rise behind the ground

`rom/night/c1p95c.32x` (md5 85116f58) = vi95's flags + `C1PUNCH=1`.
Card matched the rom (LOOP-DECOMPILE 107), rig frame rate at fr95e's,
Mike's eye: behind. **The fold-1 line is c1p95c.** Still to glance at
on the same rom, not a blocker: the player's legs through the grass
tufts (the per-pixel half of the split).

**Build B next: fold 2 on this line.** One change: `SETCOLS=1` on
c1p95c's flags (sc95 measured it on vi95 at ~1.05 v/gen from 1.16 and
exact in check mode; it has to be re-measured on the punch line as its
own card). Gate: the wall on ares and the check mode's 0/0; Mike's
question: does it look like c1p95c. Then Build C.

---------------------------------------------------------------------
## BUILD B CARD (builder, 2026-09-13 03:10) -- fold 2 on the fold-1 line

    rom        rom/night/bldB.32x   md5 493d4984
    base       c1p95c (md5 85116f58, the fold-1 line)
    change     ONE flag: SETCOLS=1 (-DSET_COLS on the SH-2 side)
    flags      vi75's line + MD_STATE (both CPUs) + C1_PUNCH + SET_COLS;
               .build_flags diff against vi75's set is exactly those four
               defines, so against c1p95c it is SET_COLS alone
    what       the maps drain's scan (bm_scan_rows, 2,464 cell reads per
               plane per generation) is replaced by one pass over the
               baked per-column set extents (sh_src/setcols_md.h, from
               tools/bake_setcols.py with the corrected unpacker, LOOP29
               243) whenever the state word says the level's tilemap is
               on screen and every page select is < 10; the live scan
               otherwise. The tail (bm_tail) is untouched. Nothing on
               the 68K changes.
    probes     pcB (+PHASECENSUS) for the wall and the drain split;
               scB (+PHASECENSUS +SETCOLSCHECK, compare in ROM) for the
               live-vs-baked check; frB (+BOOTFLIPRATE) for the rig.
    gates      ares wall (pcB) against c1p95c's line; check mode 0/0 in
               steady play; the picture gates equal to c1p95c; rig frame
               rate at c1p95c's; Mike: does it look like c1p95c.
    numbers    (appended below when the runs land)

    ares, play2, 4000 frames (pcB):
      wall 1.18 v/gen   single-vint 40%   ships 37.0/s
      echo 1.11  mtask 0.79  ship 0.64  flip 0.76
      maps drain: scan 0.107, tail 0.113 v/gen   (vi95's line: 0.279 / 0.135)
    picture gates (bldB vs vi95/c1p95c): title 0.273/0.273, demo 0.038/0.038,
      eye 0.487/0.480, title2 0.259/0.255, return 0.040/0.040 0.038/0.039,
      face plane 0.62/0.65, play 0.036-0.042 (=), late-coin play 0.036-0.042 (=)
    reading: the scan fell as on vi95 (0.28 -> 0.11) but the wall did
      not follow -- the SLAVE phase (echo) is 1.11 here against 0.95-1.02
      on vi95's line, i.e. the punch's sprite-loop cost is what this
      card's base carries. pcA (c1p95c + census) is being read to put
      the base wall on the card; the check mode and the rig follow.
    rig (unattended attract):
      frB   21 19 7 15 18 presented frames per 64 vints (c1p95c 16 16 6 21 18)
      attract black shares, first and second demo: bldB trees 0.00 fg
      0.00-0.01; c1p95c the same run: 0.00 / 0.00-0.02. (The 0.30-0.45
      frames are the face/eye transitions.) Note for the record: the
      fold-1 line's attract on the rig reads CLEAN here, where vi95's
      read 0.20 / 0.57 an hour earlier -- the punch changed nothing in
      the allocator, so the rig's black was phase; not re-derived tonight.
