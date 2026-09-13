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
    -> the line since 2026-09-12 20:15 is bldB (wall 1.18, 40%); see the
       Build B block below and START-HERE's heading
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
    base wall (pcA = c1p95c + PHASECENSUS, same run):
      wall 1.29 v/gen   single-vint 31%   ships 34.8/s
      echo 1.18  mtask 1.10  ship 0.62  flip 0.77   scan 0.291 tail 0.131
    so on this line:  Build B  1.29 -> 1.18 v/gen, 31% -> 40% single-vint,
      scan 0.291 -> 0.107, tail unchanged.  (On vi95's line fold 2 read
      1.16 -> 1.05; the punch itself costs 0.13 v/gen of slave time in
      ares -- Build A's price, 1.16 -> 1.29 -- and that is where a
      Build C could look: the 1:1 sprite paths still test per pixel.)
    legs glance (c1p95c on the rig, 233345, the attract demo): both
      fighters' legs are drawn down into the grass with the tufts over
      them -- through, not cut at the grass line.
    check mode on this line (scB = bldB + SETCOLSCHECK, compare in ROM;
      the census could not fit .ramtext beside it): f1000-4000, 3,010
      planes, presence 0, level 0.
    CARD COMPLETE 03:50. bldB is on the rig. Mike's question: does it
      look like c1p95c.

## BUILD B PASSED (Mike, 2026-09-13 ~04:00): "Behind the grass. Stellar
## lockdown on the progress." THE LINE IS bldB.

`rom/night/bldB.32x` (md5 493d4984) = c1p95c + SETCOLS=1: folds 1, 2 and
4 on one rom, ares 1.18 v/gen / 40% single-vint on its own base of
1.29 / 31%, check mode 0/0, rig frame rate at the line's. Build C:
per the census order, fold 5 with the text copy on the packet side
(NOTES 25/26 open with the decompile thread), then fold 3 / RELBANK on
the rig with BOOTFLIPRATE + BOOTGATECHK. A side card, whenever a slave
lever is wanted: the punch's 1:1 sprite paths per cell (0.13 v/gen of
ares slave time; the rig's wall is windows per vint, so it is not the
rig's number).

---------------------------------------------------------------------
## BUILD B PASSED (Mike, 2026-09-13): "behind the grass, stellar"

`rom/night/bldB.32x` (md5 493d4984) = c1p95c + `SETCOLS=1`. Verified in
the roms: the set-extent table present in bldB and absent in c1p95c,
the 68000 side identical but for the build stamp. Ares wall 1.29 ->
1.18, single-vint 31 -> 40%, check mode 0/0 on 3,010 planes, rig frame
rate inside c1p95c's spread, Mike's eye: passes. **The line is bldB.**

    wall on the line   1.18 v/gen     the threshold   1.00

**Next card, before fold 5: the punch's own price.** The base moved
from vi95's 1.16 to c1p95c's 1.29 because the 1:1 and zoomed sprite
plot paths still test the hole class per PIXEL; the baked-run path
already tests per CELL (244c) and cost the rig nothing. Same change on
the other paths: one flag, one card, expected ~0.13 off the wall, which
puts the line at ~1.05 by the card's own arithmetic. Gate: wall on
ares, picture = bldB, rig frame rate. Then fold 5 on the packet side,
then RELBANK.

**Also worth one look after that:** the remaining scan reads 0.107
v/gen for 44 columns of extent compares; that is a third of the cell
walk it replaced and should be nearer zero.

**Landing protocol, one rule added:** a rig black share is a fact only
across three launches. vi95's demo read a fifth black at one launch and
clean at the next on the same rom, with nothing in the allocator
changed between (Build B's card). One capture is a phase, not a
defect -- the same rule the log already applies to ares samples under
3,000 cycles.

---------------------------------------------------------------------
## LANDING PROTOCOL, RULE ADDED (decompile thread via Mike, 2026-09-13 04:15)

A rig black share counts only across THREE launches. The same vi95 rom
read a fifth black at one launch and clean at the next with nothing
changed in between (LOOP29 231 vs Build B's card), so a single capture
is a phase, not a defect.

## BUILD C CARD (builder, 2026-09-13 04:20) -- the punch's own price

    rom        rom/night/bldC.32x            (md5 below)
    base       bldB (md5 493d4984, the line)
    change     ONE flag: C1PCELL=1 (-DC1_PCELL): the 1:1 (NIB/NIB_NC) and
               zoomed (ZNIB/ZNIB_G) sprite paths fetch the hole class and
               the art row once per cell crossed (a per-row cache) instead
               of per pixel; the baked-run path already did (244c). No
               picture change by construction: the same class, the same
               art byte, tested once per cell instead of once per pixel.
    expected   ~0.13 v/gen off the ares wall (Build A's price, 1.16 -> 1.29
               on vi95's line), i.e. the line near 1.05.
    gates      ares wall (pcC vs pcB 1.18); picture equal to bldB; rig
               frame rate (frC vs frB 21 19 7 15 18); rig black shares
               across three launches.
    numbers    (appended when the runs land)
    rom        rom/night/bldC.32x   md5 543a5e7e   (.ramtext 27,884 B)
    flags diff against bldB: -DC1_PCELL alone.
    ares, play2, 4000 frames (pcC vs pcB):
      wall 1.21 v/gen (B 1.18)   single-vint 37% (40%)   ships 36.2/s (37.0)
      echo 1.14 (1.11)  mtask 0.76 (0.79)  ship 0.64  flip 0.76
    picture: title/demo/eye/return/play equal to B to 0.004 (frames not
      byte-identical: SH-2 timing differs); face plane 0.58 (0.62);
      LATE-COIN PLAY 0.083 0.081 0.089 against B's 0.036 0.042 0.042.
    rig: frC 22 18 19 7 21 (frB 21 19 7 15 18); attract black shares
      over THREE launches all 0.00-0.01.
    VERDICT: FAILS. The wall did not move (1.21 vs 1.18, inside noise
      but the wrong sign) and a credited game coined during the eye
      reads twice B's black. The punch's price is not in the per-pixel
      class test of the 1:1 paths; the per-cell call did not pay for
      itself. bldB stays the line and is back on the rig. Where the
      0.13 is: to be measured, not guessed -- the candidates are the
      baked-run path's punched loop (every pp < 3 sprite pixel now runs
      the cell loop instead of the tight run copy) and the master's
      mask writes in the name-table pass.

## BUILD D CARD (builder, 2026-09-13 05:50) -- the mask table

    rom        rom/night/bldD.32x            (md5 below)
    base       bldB (md5 493d4984, the line)
    change     ONE flag: C1MASKTAB=1 (-DC1_MASKTAB). The scene's class-2
               tile opacity masks (tools/bake_cat1mask.py: 81/139/192/14/
               130 tiles a scene, <= 1,536 B) are copied to SDRAM at
               mds_install; the name-table pass stores a mask INDEX per
               class-2 cell (binary search of the raw 13-bit code, in
               ROM); the slave's punched loops read one SDRAM byte per
               cell row and test a bit, instead of eight cart bytes.
               Class 0/1 handling unchanged. Nothing added to .ramtext.
    why        the ablation (247): noplot 1.03 / nomask 1.16 / line 1.18
               -- the art reads inside the punched loop hold 0.13.
    gates      ares wall (pcD) against pcB 1.18, expected ~1.05; picture
               equal to bldB incl. the late-coin game; rig frame rate
               (frD vs frB 21 19 7 15 18); black shares over three
               launches; Mike: does it look like bldB.
    numbers    (appended when the runs land)
    second effect of the flag, for the reviewer: under C1_MASKTAB (as
      under C1_PCELL) bm_scan_baked is fetched from ROM instead of the
      RAM-code slot -- the slot is full (28,472 of 28,672 B on the line,
      LOOP29 246) and the mask branch in the plot expansions needed the
      room. That scan is bound by its cart table reads anyway (573 ticks
      a plane); the wall number carries both effects.
    third effect, same flag: c1_hit (the class test of the 1:1 and
      zoomed paths) is one out-of-line RAMCODE function under C1_MASKTAB
      instead of 28 inline copies; .ramtext 0x6A10 = 27,152 B (the line
      28,472). Build C measured that call at noise.
    numbers (pcD, ares play2 4000): wall 1.22 v/gen (B 1.18), single-vint
      37% (40%), echo 1.14 (1.11), mtask 0.77 (0.79). Picture: title,
      demos, eye, title2 equal to B; the rest and the rig below.
    VERDICT so far: no gain. The art reads were not the price; LOOP29
      248 re-reads the ablation: the punched loop's SHAPE holds 0.13.

## BUILD E CARD (builder, 2026-09-13 06:40) -- the tight copy for class-0 runs

    rom        rom/night/bldE.32x            (md5 below)
    base       bldB (md5 493d4984, the line)
    change     ONE flag: C1FAST=1 (-DC1_FAST). A punched sprite run first
               reads the class byte of each cell it covers (2-9 loads); if
               every cell is class 0 it takes the ORIGINAL tight copy
               loop; only runs that touch a class-1/2 cell walk cells.
               No picture change by construction (class-0 cells were
               drawn whole either way). No mask table, no other change.
    why        the ablation re-read (248): noplot 1.03 vs nomask 1.16 --
               the per-cell walk itself on every punched run is the price;
               cat-1 is 11% of level 1's cells, so most runs cross none.
    gates      ares wall (pcE) against pcB 1.18, expected ~1.05; picture
               equal to bldB incl. the late-coin game; rig frame rate
               (frE vs frB 21 19 7 15 18); black shares over three
               launches; Mike: does it look like bldB.
    numbers    (appended when the runs land)
    Build D, rest of the card: play 0.037 0.036 0.042 0.043 and late-coin
      0.036 0.042 0.042 (= B); rig: [20, 17, 11, 10, 14]   (frB 21 19 7 15 18)
      attract black shares over three launches all 0.00-0.01 (one 0.05
      frame at a transition).
    VERDICT: FAILS on the wall (1.22 vs 1.18, no gain); correct picture,
      no rig cost. Not the line. bldB stays.
    Build E numbers (ares): wall 1.17 v/gen (B 1.18; nat_score's own
      wall, census-free -- the census rom could not fit .ramtext with
      the pre-scan in the run loop), ships 36.5/s; picture equal to B
      (title 0.273, demo 0.038, eye 0.479/0.487, return 0.040/0.038,
      face 0.58/0.62, play 0.037-0.042, late-coin 0.036-0.042). Rig below.
    VERDICT so far: no gain on the wall. The loop's shape was not the
      price either; LOOP29 249 names the last thing D and E left alone,
      the UNCACHED mask reads, and Build F tests it.

## BUILD F CARD (builder, 2026-09-13 07:20) -- the mask read through the cache

    rom        rom/night/bldF.32x            (md5 below)
    base       bldB (md5 493d4984, the line)
    change     ONE flag: C1CACHED=1 (-DC1_CACHED). The slave reads the
               punch's cell mask (cat1scr) and cell codes (cat1code)
               through its cache instead of the 0x2000_0000 uncached
               alias. No code path changes; the same bytes are read.
               Coherence: the slave purges its cache at every window
               start, the master's writes are write-through, and the
               mask is one generation behind the compose by design.
    why        D (art rows -> SDRAM) and E (tight copy for class-0 runs)
               both left the wall at 1.17-1.22 while noplot reads 1.03;
               the uncached read per cell (baked path) and per PIXEL
               (1:1/zoomed paths) is what noplot also removed.
    gates      ares wall against B 1.18 (expected ~1.05 if this is it);
               picture equal to B incl. late-coin; rig frame rate; three
               launches; Mike: does it look like bldB.
    numbers    (appended when the runs land)
    Build F numbers (ares): pcF wall 1.21 (B 1.18), echo 1.13 (1.11),
      mtask 0.76; bldF census-free 1.18; picture equal to B except the
      aligned return 0.074/0.064 against B's 0.040/0.038 (the same-
      round-return class re-rolled by the SH-2 timing change, 227).
    VERDICT: FAILS -- no gain; the uncached reads were not the price.
    Build E, rig: frE 20 20 15 14 15 (B 21 19 7 15 18); attract black
      shares over three launches 0.00-0.01. VERDICT: no gain, no harm;
      not the line.

## BUILD G CARD (builder, 2026-09-13 07:45) -- the tight copy, with the scan cached

    rom        rom/night/bldG.32x            (md5 below)
    base       bldB (md5 493d4984, the line)
    change     TWO flags that are one change: C1FAST=1 (a punched run
               whose cells are all class 0 takes the original tight
               copy; only runs touching a class-1/2 cell walk cells) and
               C1CACHED=1 (the per-run class scan and the cell walk read
               the mask through the slave's cache instead of the
               uncached alias). Build E was the first without the
               second and its scan cost what it saved; Build F the
               second without the first and had nothing to save.
    why        the generation trace (LOOP29 249b): the punch costs the
               slave's compose 0.065 v/gen, 0.059 of it present with no
               mask at all -- the per-cell loop control around every
               run. 99% of level-1 records take the baked-run path.
    gates      census-free wall against bldB's own (nat_score, same
               run); picture equal to bldB incl. late-coin; rig frame
               rate; three launches; Mike: does it look like bldB.
    numbers    (appended when the runs land)
    Build G numbers (ares, census-free, same run): wall 1.16 (bldB 1.18),
      ships 36.6/s (37.0); picture equal to B (title 0.273, demo 0.038,
      eye 0.494/0.487, return 0.040/0.038, face 0.58/0.62, play
      0.036-0.041, late-coin 0.036-0.042).
    VERDICT so far: 0.02, inside the noise. Whether the fast path even
      fires is the next number (250: runs walking cells vs runs taking
      the tight copy, in the census rom).
    Build F, rig: frF 18 22 8 13 23 (B 21 19 7 15 18); attract black
      shares over three launches 0.00-0.01. Closed: no gain, no harm.
      bldB is back on the rig.
    Build G, census rom (pcG): wall 1.18, echo 1.11 (= the line);
      compose pass sum 0.388 (line 0.389, noplot 0.324); the fast path
      fires on 67% of punched runs (790,990 tight copies / 387,576 cell
      walks in steady play). VERDICT: no gain; closed. The punch's cost
      is not any path that executes -- LOOP29 250's C1RTOFF ablation
      decides between codegen and execution before another card is cut.

## PUNCH-PRICE SERIES CLOSED (builder, 2026-09-13): the walk executing is the price

Cards C-G all closed (C fails to link, D worse, E/F/G no gain). The
C1RTOFF ablation (LOOP29 251) puts the punch's codegen at 0.013 compose
and its execution at 0.052 compose / 0.12 wall; no single part of the
walk holds it. Note 35 hands the decompile thread the numbers and two
designs (per-row hole skip; repaint-over instead of punch); the
builder's recommendation is to bank the punch at its price and cut
fold 5 next. THE LINE stays bldB (md5 493d4984), on the rig.

---------------------------------------------------------------------
## AFTER THE PUNCH-PRICE SERIES (2026-09-13): NEXT CARDS, PICKED BY THE DECOMPILE THREAD (NOTES 36)

    line    bldB   wall 1.18   40% single-vint
    punch off (ablation, 251)     1.03      <- the 0.12 is half of what is left that anyone knows how to cut

  Card H  the STAMP (NOTES 36): compose unpunched with the original
          loops; stamp transparent (0) over the hole cells from the
          master's class map and Build D's SDRAM masks; draw pp=3
          sprites after. Expected 1.18 -> ~1.06-1.08. Gate: pixel diff
          vs bldB near zero (the pp=3-over-pp=2-in-a-hole case only),
          Mike's eye, rig frame rate.
  Card I  the scan memo (NOTES 33.2): reuse each plane's scan while its
          (pq, tx, row range) is unchanged. Expected ~0.08. Gate: check
          mode 0/0, wall.
  Then    fold 5 on the packet side (NOTES 25/26/27), then RELBANK on
          the rig -- the 68000 side, which pays once the wall is under
          1.00.

---------------------------------------------------------------------
## CARD H (builder, 2026-09-13 01:30): the STAMP -- rom/night/bldH.32x

    rom      rom/night/bldH.32x   md5 ca80af61   ON THE RIG (three launches)
    base     bldB (493d4984), THE LINE
    change   C1STAMP=1: the slave's sprite loops compile unpunched
             (.ramtext 0x6F38 -> 0x6394); after the pp<3 pass each band
             writes MD-through (0) over the hole cells under the drawn
             records' rectangles (class 1 whole, class 2 by the
             cat1mask.h opacity bit); the pp=3 records draw after,
             only when the list has one. The master's name-table pass
             also emits each cell row's hole cells as bits (cat1hb).
             Implies C1MASKTAB. Six cuts to get here: LOOP29 252.
    flags    the line's + C1STAMP=1

    ares wall (nat_score, 4000 frames)    bldH 1.14   bldB 1.18 (same run, same day)
    slave compose sum (v/gen)             bldH 0.368  bldB 0.389  punch off 0.324
    single-vint share                     40% (pcH bins 951/1417/22/9; pcB 971/1408/24/11)
    picture gates (ares)                  title 0.273 demo 0.038 eye 0.487 title2 0.255
             return 0.040/0.038 face blue-upper 0.58 (bldB 0.62) play black
             0.037 0.036 0.042 0.043 -- all bldB's; late-coin play black
             0.085 0.082 0.089 vs bldB 0.036 0.042 0.042 -- the same-round
             return's PHASE re-roll (227), not the stamp: shifting the coin
             by frames, f3400/f3800 black share --
                 coin -20  bldB 0.044/0.042  bldH 0.044/0.042
                 coin -10  bldB 0.036/0.042  bldH 0.036/0.042
                 coin   0  bldB 0.036/0.042  bldH 0.085/0.082
                 coin +10  bldB 0.086/0.084  bldH 0.036/0.041
                 coin +20  bldB 0.036/0.040  bldH 0.036/0.038
             Each rom has one coin frame that lands the tree band black;
             they differ by ten frames. (The rig's three launches saw no
             level black on bldH.)
    check mode                            not touched by this card (fold 2 unchanged)
    picture, aligned on the attract demo  bldH vs bldB f1000/f1200: 527/533 px,
             all in the grass rows 20-22: dark specks bldB draws and the
             ARCADE does not (mame altbeast attract ref_000704, same scroll
             and zombies, captured headless). bldH's grass = the arcade's.
             A frame-N diff of two roms is NOT a gate (32k px at play
             f1000: the roms run at different speeds); the aligned demo is.
    rig frame rate (frH, presented/64 vints)   16 19 18 15 21   (bldB 21 19 7 15 18)
    rig black shares, three launches (18/24/30/50/56 s)
             level shots 0.00-0.02 on all three; the 30 s slot read the
             title (0.29) twice and a full-black frame once (launch 2,
             the demo->title fade; the level shots of that launch 0.00).
    Mike's eye   owed: legs through the grass tufts, zombies behind the
             ground, and the grass line clean of specks -- "does it look
             like bldB", minus bldB's specks.

    Expected by NOTES 36: 1.06-1.08. Measured: 1.14 (from 1.18). The
    stamp's own price is 0.044 v/gen on the compose sum (0.368 vs 0.324
    with the punch off): 0.015 FB writes, the rest the per-band cover
    and class reads. What the five earlier cuts cost, and why, is in
    LOOP29 252 (the FB write floor, the SH-2's write-through stores).
