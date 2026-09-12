# Plan: put the whole tile layer on the VDP

For the builder thread, 2026-09-11. Working in
`docs/log/LOOP-DECOMPILE.md` 56-62; entry numbers in brackets.

**The claim.** Both System 16 tile planes can be rendered by the Mega
Drive VDP instead of composed on the SH-2. Not just the cat1 strip — the
tile half of slave compose, entirely. Neither the priority model nor the
colour budget prevents it, and both of those were the stated reasons it
could not be done.

**Why this is worth your time.** Slave compose is 1.40 of a 1.44-vint
generation, split roughly 0.73 sprites and 0.67 cat1 tiles. Moving tiles
to hardware is the larger half and it is the half with no colour problem.
On its own it should put the generation under the one-vint quantum, which
is the thing 22% single-vint frames is waiting on.

---------------------------------------------------------------------
## What is verified, and how

**1. The priority mapping is EXACT for level 1.** [59]

The MD VDP resolves in this order
(`srcref/S32X_MiSTer/rtl/GEN/vdp.sv:1772-1782`):

    sprite HIGH > plane A HIGH > plane B HIGH > sprite LOW > plane A LOW > plane B LOW

System 16 level 1 needs, and gets:

    S16                      level      MD                      rank
    FG cat1 (the strip)        4        plane A, priority SET     2
    sprites (all pp=2)         -        sprite, priority clear    4
    FG cat0                    2        plane A, priority clear   5
    background                 1        plane B, priority clear   6

Cat1 beats sprites; cat0 loses to them; background is behind everything.
Exactly System 16's own order.

This rests on every live sprite being pp=2. Measured: 83 live records
across five frames of level 1, **100% pp=2, none pp=3**. A pp=3 sprite
would beat a cat1 tile on System 16 and cannot on this mapping, so its
absence is what makes the mapping exact rather than approximate.

**2. The colour budget fits, with a line to spare.** [61][62]

MD CRAM is 3 bits per gun against System 16's 5. The port already
quantises to it and ARCHITECTURE.md:838 already measured that loss at max
2 in 0-31 and called the images indistinguishable. In that space, scene
0's worst-case viewport — 25 distinct tile palettes over all horizontal
scroll positions, both planes — is **43 distinct colours**, and packs
into four lines at [15, 15, 11, 5]. 46 of 60 slots. The fourth line is
nearly free.

Measured against LIVE palette ram, union across six frames, so every
colour-cycler state observed is included.

**3. The colour cycler does not interfere.** [62] It cycles four tile
palettes (6, 19, 20, 21). Scene 0's viewport uses 72-103. No overlap.

---------------------------------------------------------------------
## What is NOT verified — read this before scoping

  - **Level 1 only.** The pp=2 uniformity, the colour fit and the cycler
    check are all scene 0. A later scene with a pp=3 sprite has a real
    boundary case; a later scene may not pack.
  - **Scenes 1-4's numbers in entries 60-61 are WRONG** [62]. I read
    palettes above 63 from a rom block that only holds 0-63. Those
    scenes need re-measuring from a live dump, which needs a playthrough
    that reaches them.
  - **`tools/bake_tilecram.py` has that same defect.** It reads colours
    from rom. Correct for palettes 0-63, wrong above. **Do not bake from
    it until it takes a live CRAM dump.** Its packer is sound; its input
    is not.
  - **Vertical scroll assumed fixed.** Level 1 pins it at 32 [24]. Not
    checked elsewhere.

---------------------------------------------------------------------
## The steps

Each one has a test that can fail, and the order is cheapest-kill-first.

**Step 1 — confirm the mapping without building anything.**
Set plane A tiles' priority bit from `sh_src/cat1map.bin`, put the
background on plane B, leave sprites low priority, and let the existing
software compose keep running underneath. Compare the two pictures.
If the VDP's output matches what compose produces for the tile layers,
the mapping is right and nothing has been committed.
*Kills the plan if:* the layering differs anywhere a sprite meets a cat1
tile.

**Step 2 — fix the baker's input, then bake level 1.**
Give `bake_tilecram.py` a live CRAM dump instead of rom. Bake scene 0
only. It emits four CRAM lines and, for each System 16 tile palette, its
line and the slot each of its seven pens maps to. **That pen map is the
rebake**: tile pixel values get rewritten so a tile indexes its assigned
line directly.
*Kills the plan if:* scene 0 stops packing into four lines once the
colours are read correctly. (It does pack — 43 colours, [15,15,11,5] —
but re-derive it rather than trust my run.)

**Step 3 — turn off tile compose for scene 0 and measure.**
The number to read is **percentage of single-vint frames**, not fps and
not "30 Hz". Current is 22% on vi26. If the tile half genuinely leaves
compose, the generation should fall from 1.44 toward 0.77 and that
percentage should move a long way.
*Kills the plan if:* the percentage does not move. That would mean tile
compose was not the cost it is believed to be, and the 0.67 figure needs
re-measuring before anything else is built on it.

**Step 4 — Mike's play pass.**
Still frames cannot judge this. The failure mode for a priority change is
temporal, which is what killed CAT1MD, and the whole point of the pp=2
measurement is that there is no boundary case to flicker. The play pass
is what confirms that.

**Step 5 — only then, the other four scenes.**
Each needs its own live palette dump, its own pack, and its own sprite
priority census. Scene 4 is the tight one: 16 palettes, and it needed a
best-fit with restarts rather than first-fit even on the optimistic
numbers.

---------------------------------------------------------------------
## Artifacts

    sh_src/cat1map.bin     which tiles are cat1 = which get plane A's
                           priority bit. Verified against live tile ram,
                           20480/20480 [31].
    sh_src/cat1vis.bin     VOID. Built on the page assignment corrected
                           in [59]; ignore it.
    tools/bake_tilecram.py the CRAM packer. Sound packer, wrong input
                           (reads rom, needs a live dump).

---------------------------------------------------------------------
## One thing I got wrong that matters to you

I reported earlier that cat1 tiles were in the background plane, from
page-select writes I found in the code. Both of those writes are in
service mode. Measured live, the foreground is page 0 and the background
is page 5 — the opposite way round [59]. If you had built against my
earlier note you would have promoted the wrong plane.

Six of my claims tonight were wrong and are marked in the log. The ones
that survived were all derived by reading the instruction that consumes a
value, or by measuring a running frame. Where this plan states a number,
it says which.

---------------------------------------------------------------------
## Addendum after vi27 — four things

**1. The exhaustive list you asked for, with a pack. Take it as input,
not as truth — re-derive it in your emitter.**

Scene 0, worst case over all 64 horizontal scroll positions, both planes,
LIVE palette ram, union across six frames so every cycler state seen is
covered. 25 palettes, 43 distinct MD colours:

    72 73 74 75 76 77 78 79 80 81 83 84 85 86 87 92 93 95 96 97 99 100 101 102 103

A verified four-line pack:

    LINE 0  15 colours  palettes 72 73 74 75 76 77 78 79 80 81 83 84 100
    LINE 1  15 colours  palettes 87 101 102 103
    LINE 2  11 colours  palettes 85 86 92 93
    LINE 3   5 colours  palettes 95 96 97 99

    line 0: 0000 0664 0246 0466 0686 0468 0688 08A8 0AA8 06AA 08CA 0ACA 08CC 08EC 0CEC 0EEE
    line 1: 0000 0006 0026 0266 0466 0028 0248 0488 0688 024A 046A 06AA 08AA 046C 0ACC 0CEE
    line 2: 0000 0062 0A62 0C64 0084 0C84 00A4 0C86 00A6 00C6 0C88 00C8 0000 0000 0000 0000
    line 3: 0000 0240 0460 0680 08A0 0AC0 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000

Note **no 2-line or 3-line packing exists** for these 25 — tiles need all
four. My earlier "one or two lines spare" was from bad palette data and
is corrected in LOOP-DECOMPILE 64.

**2. Your fourth-line result is not a surprise, it is the diagnosis.**
9,845 destroyed against ~2,000 is a thrashing allocator given more to
thrash. A static per-scene assignment has nothing to thrash BY
CONSTRUCTION — the palettes are fixed rom data, the viewport list is
computable ahead of time, and the pack is a bake-time bin-packing. The
allocator's job in the cat-1 path is not to be improved. It is to not
exist.

**3. The hole punch is cheaper than you costed it** (LOOP-DECOMPILE 65).
Four scenes of five have NO partial cat-1 row, so the test there is one
screen-row compare with no bitmap lookup; scene 0 needs the bitmap for
exactly one row (24). And per cell, many need no hole at all: a cat-1
cell whose tile is BLANK has nothing to show through — 1204 of scene 1's
1792 — and a fully opaque one can be suppressed whole. 68% of scene 0's
cells and 82% of scene 1's resolve per-cell. What the sprite loop wants
is two bits per cell (skip / suppress-all / consult-pixels), not one.

**4. On the 42/57 judder, one lever nobody is using.** The game already
knows when it did not advance. On an overrun it increments 0xFFF144 and
takes a short path at 0x2C06 that writes NO scroll registers and NO
sprite upload (LOOP-DECOMPILE 22). On those vints the composed output is
IDENTICAL to the last one. At 42% single-vint the game should be missing
roughly 58% of its own vints, and every one of those is a generation
spent recomposing an unchanged frame. Detecting it costs a byte compare.
It will not make a slow generation fast — but it makes the frames either
side of it free, which is exactly where a bimodal distribution hurts.

**On your three bugs.** A flag filtered before make added it; a four-line
table into a three-line declaration; a pen map built in colour order
instead of pixel order. Same shape as six of mine tonight, and the same
shape as the one that matters most: **a mechanism that looks applied and
is not.** The countermeasure that has worked for me is to make the
mechanism ASSERT IT FIRED and print the count — `patch_game.py` already
does this everywhere (`assert hrom[off:off+4] == want`), which is why no
rebasing bug has ever survived a build. Every new emitter should print
what it emitted and fail loudly on a count it did not expect.

---------------------------------------------------------------------
## Addendum 2 — per-scene tile CRAM, measured (LOOP-DECOMPILE 66)

**A correction first: I told you there are no spare CRAM lines. That is
true for SCENE 0 AND ONLY SCENE 0** — which happens to be the scene you
are working on, and the worst case in the game.

    scene   palettes   MD colours   lines needed   SPARE
      0        25          43            4           0
      1        11          28            3           1
      2        14          27            2           2
      4        15          33            3           1
      3         -           -            -           not reached

Scene 4 is also far kinder than I first said: 33 colours in 3 lines,
not the 77-in-6 from my bad rom read. That figure is retired twice now.

**How these were reached.** No input script in `discover/inputs` gets
past scene 0 — I checked four of them at 6000 frames. `make ship-us
SCENESEL=N` rewrites the eight-byte round->scene table at 0x1CDA to
all-N so every round loads scene N. One byte per entry, in place,
asserted against the expected table first. Tile and palette measurement
only: the ACTORS are still the round's, so it is NOT valid for a sprite
priority census.

**Scene 3 does not take.** Its rom carries the all-3 table, verified by
byte search in the image, and 0xFFF142 still reads 0 at frames 900,
1500, 2200 and 3000. Patch present, scene does not load. I have no
explanation and am not offering one; scene 3 stays unmeasured.

**The packs, ready to consume.** Same terms as the scene 0 pack above:
worst 40x28 viewport over all 64 scroll positions, both planes, live
palette ram, union across three frames.

    SCENE 1 — 11 palettes, 28 MD colours, 3 lines
      palettes: [64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 75]
      LINE 0  14 colours  palettes [64, 65, 66]
      LINE 1  14 colours  palettes [69, 70, 71, 72, 73, 75]
      LINE 2   5 colours  palettes [67, 68]
        line 0: 0000 0006 0066 00A6 0248 0468 0488 00C8 068A 06AA 048C 08AC 06AE 0CCE 0EEE 0000
        line 1: 0000 0664 0246 0466 0686 0468 0688 08A8 0AA8 06AA 0ACA 08CC 08EC 0CEC 0EEE 0000
        line 2: 0000 0688 08AA 0ACA 0CCA 0EEC 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000
    
    SCENE 2 — 14 palettes, 27 MD colours, 2 lines
      palettes: [1, 100, 102, 103, 104, 106, 107, 108, 109, 110, 111, 112, 113, 115]
      LINE 0  13 colours  palettes [1, 102, 103, 104, 106, 107, 108]
      LINE 1  15 colours  palettes [100, 109, 110, 111, 112, 113, 115]
        line 0: 0000 0004 0006 0026 0028 0248 024A 046A 046C 048C 000E 06AE 00CE 0EEE 0000 0000
        line 1: 0000 0000 0242 0244 0464 0664 0466 0686 0688 08A8 08AA 0AAA 08CA 0ACC 0CEC 0EEE
    
    SCENE 4 — 15 palettes, 33 MD colours, 3 lines
      palettes: [3, 96, 97, 98, 99, 100, 102, 103, 104, 105, 106, 107, 108, 110, 111]
      LINE 0  14 colours  palettes [3, 102, 103]
      LINE 1  14 colours  palettes [104, 106, 107, 108, 110, 111]
      LINE 2  11 colours  palettes [96, 97, 98, 99, 100, 105]
        line 0: 0000 0004 0206 0046 0A66 0408 040A 026A 060C 028C 048C 068C 086E 0A8E 0ACE 0000
        line 1: 0000 0000 0026 0028 0448 0468 004A 066A 068A 006C 088C 08AC 008E 00AE 0EEE 0000
        line 2: 0000 0040 0060 0206 0408 060A 068A 060C 080C 08AC 080E 0EEE 0000 0000 0000 0000
    

**One more process note, because it is your recurring shape.** One of
the four probe builds hit a transient link error and the copy step left
a STALE rom in place — the file named scene3 was scene 2's image.
Running each rom and reading 0xFFF142 caught it. The build log did not.
The accidental upside: that stale rom re-measured scene 0 and reproduced
25 palettes / 43 colours / [15,15,11,5] exactly, which is an unplanned
independent repeat of the number your whole pack rests on.

---------------------------------------------------------------------
## Addendum 3 — the live palette dumps, staged in the repo

LOOP29 188 item 1 says the emitter needs my exhaustive viewport list
rather than a sampled harvest. The blocker was never the list — it was
that **no input script reaches past scene 0**, so there were no live
palette dumps for the other scenes, and `bake_tilecram.py` falls back to
rom for any scene it has no dump for. Rom is wrong above palette 63, and
every viewport uses 64-127.

`make ship-us SCENESEL=N` (LOOP-DECOMPILE 66) solves that. The dumps are
now in the tree so you do not have to rerun it:

    discover/cram/scene0_a.bin  b  c      frames 2400 / 2404 / 2408
    discover/cram/scene1_a.bin  b  c      frames 1500 / 1504 / 1508
    discover/cram/scene2_a.bin  b  c
    discover/cram/scene4_a.bin  b  c

Each is WRAM 0xFF9000, 0x1000 bytes. Three frames apiece so the colour
cycler's states are covered by the union.

**Your own tool, run per scene:**

    python3 tools/bake_tilecram.py --live-scene 1 \
        --live discover/cram/scene1_a.bin discover/cram/scene1_b.bin \
               discover/cram/scene1_c.bin

    scene 0   25 palettes   lines [15,15,11,5]   46 slots
    scene 1   11 palettes   lines [14,14, 5,0]   33 slots
    scene 2   14 palettes   lines [13,15, 0,0]   28 slots
    scene 4   15 palettes   lines [14,14,11,0]   39 slots
    scene 3   NOT AVAILABLE

**Only scene 0 needs all four lines.** Scene 2 needs two. That matters for
LOOP29 176: the fourth line made the thrash 15x worse, and on three of
four scenes the fourth line does not need to exist at all.

**Scene 3 is missing and I could not get it.** Its SCENESEL rom carries
the all-3 table — verified by byte search in the image — and 0xFFF142
still reads 0 at frames 900, 1500, 2200 and 3000. Patch present, scene
does not load, no explanation offered.

**One correction to my addendum 2.** Those numbers came from my own packer
on the same dumps and differ slightly from your tool's (scene 2: I said
two lines at [13,15], your tool agrees; scene 4: I said [14,14,11], your
tool agrees; scene 1: I said [14,14,5], agrees). Where they differ, use
yours — it is the one whose output the emitter consumes.

**And an apology for the noise.** I rewrote `bake_tilecram.py` tonight to
take live dumps, not realising you had already done exactly that in
39ccafb and fixed the pen-order bug in e82284b. I reverted my changes;
the file in the tree is yours. That is the fourth time tonight I built
something you had already built from my own finding. The dumps above are
the part only I could produce.
