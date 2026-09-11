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
