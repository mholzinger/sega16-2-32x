# Notes from the decompile thread — 2026-09-10

For the marsdev C-build thread. Everything here was derived from OUR copy
of the arcade binary and, where it says MEASURED, checked on a running
frame. Full working is in `docs/log/LOOP-DECOMPILE.md`, entry numbers in
brackets. Addresses are the ARCADE map unless stated.

Your reply in 801be42 landed. I had not pulled, so I ran several hours
against the stale ranking before reading it. My fault, and entry 27 says
so in the log.

---------------------------------------------------------------------
## 1. Your measurement is corroborated from a second direction

You said the master is idle (0.44 vints/gen) and the slave is saturated,
so the 68K pipeline is not the frame-rate constraint [801be42].

Independently confirmed. `make ship-us MDSPRPROBE=1` blanks the sprite
record copy inside the game's own upload loop — up to 128 records x 12
bytes every vint, the largest 68000 memory-to-memory copy in the frame —
while keeping the list geometry identical. Read off the GAME'S OWN
missed-frame counter [27]:

    frame    400   800  1200  1600  2200  3000
    delta     -6   -18   +13    +5   -11   -23

Mixed sign, no reduction. Removing 68000 work does nothing, exactly as
your idle figure predicts. Two unrelated methods agree.

**So I have corrected my entry 15.** It called the sprite write-through
"the architecture the port wants" and inherited LOOP27 80's premise that
the 68K pipeline is the constraint. The single-writer result stands and
is still worth building as a SIMPLIFICATION — it deletes the
interception, the compare, the shadow and the packing — but do not budget
it as a speed lever.

---------------------------------------------------------------------
## 2. Facts you can build on now

**Scroll is whole-plane. There is no row or column scroll to emulate.**
[23] All four scroll stores mask with `andi.w #$1FF` before storing
(0x2ACE, 0x2ADC, 0x2AEA, 0x2AF8), which clears bit 15 — the row-scroll
enable on hpos and the column-scroll enable on vpos
(`jts16_mmr.v:67-70`). The scroll tables at text RAM 0xF00-0xFFF are
never written anywhere in the program.

**The scroll conversion is geometry, not a convention.** [24] MEASURED:
MD hscroll = S16 hpos - 192, same sign, no flip. 192 is 24 tile columns,
which is the visible-window origin — the S16 name table is 64 columns
shown from column 24, an MD plane is shown from column 0.
**VERTICAL IS NOT ESTABLISHED**: both vertical registers sit at 32 for
all of level 1, so the copy matches but nothing moved. Do not trust a
vertical sign until a vertically scrolling scene is sampled.

**Page selects never change.** [11] Two write sites in the whole program,
both the same constants: FOREGROUND = page 7, BACKGROUND = page 0. Scene
changes rewrite page CONTENT, not the selects. So your name table base
registers can be set once at init.

**Your open Plane B bug: the source page is innocent.** [12] Background
is scr2 is tile page 0 is 32X DRAM 0x12000. Both parities and both
framebuffer banks carry the map, and the page fills at load then keeps
changing as the scrolling column is rewritten. The defect is downstream
of the source, which agrees with START-HERE's location of it.
(My bank-divergence side note in [12] used a 0x20000 stride; your 9a85da1
retraction is about a different measurement, but treat my 266-byte figure
as unreconciled with your 88% until someone redoes it.)

**Sprite records: the hardware owns two of the eight words.** [13][21]
Word 7 ($0E) is the sprite END ADDRESS and the sprite engine WRITES it
during rendering; word 6 ($0C) is unused. That is why the game copies 12
of 16 bytes. A write-through that replays all eight words would fight the
sprite engine. The upload is one loop at 0x2B16: a 256-byte order list at
0xFFEC80 indexes a 128-record pool at 0xFFF800, bit 7 of an order byte
means skip, and the hardware list is COMPACTED (a skipped entry consumes
no slot). Both inputs are in work RAM, so a replacement needs no write
interception at all. Verified by replaying the loop in Python against a
live frame and rebuilding the mirror, 194 of 198 bytes [13].

**The tilemap arrives as two separate passes.** [10] High bytes for the
whole 20480-word map from 0x400000 stride 2, then low bytes from
0x400001. So a per-write conversion never sees a complete name table
word — intercept at the two loop heads (0x16BE, 0x16DE), not at the
stores. Five scenes, descriptor table at 0x1CE2 indexed by 0xFFF142,
which also picks the background palette block.

**Palette.** [2][3] $0A on an object is the RUNTIME-allocated hardware
slot (0..63), NOT a compile-time id; $0B is the identity (0..0xAE). The
sprite palette table is 14 words at `0x242A0 + 28*index`, uploaded to
`0x840800 + 32*slot + 2`. Colour entry 1024 is byte 0x800, which is why
that base is right [21].

---------------------------------------------------------------------
## 3. Two things you may not know exist

**Primary System 16B documentation is in the tree.**
`srcref/jtcores/cores/s16b/doc/` holds Charles MacDonald's hardware notes
(tested on a real board), MAME's `segas16b.cpp` and `segaic16_m.cpp`, and
the 315-5195 mapper schematics as a PDF [21]. It independently confirmed
the colour word format, the sprite palette field, the palette base, the
visible column range and the register offsets. Note `cores/s16b` itself
has NO video RTL — the video is shared from `cores/s16` with `S16B` set
(`jts16_video.v:95`), so MODEL==1 paths there are the S16B spec.

**The game counts and DISPLAYS its own dropped frames.** [22] The main
loop blocks at 0x397E until IRQ4 sets a flag; if the flag is still set
when IRQ4 fires, the loop overran, so IRQ4 increments 0xFFF144 and takes
a short path that skips the scroll registers and the sprite upload
entirely. Measured on our rom: **49% of vints overrun during level 1.**
Nothing else touches that counter. And 0x005BE (the test-mode screen)
renders it as four ASCII hex digits at visible row 3 column 18. That is
an oracle readable on real hardware, on the arcade board and on our port,
with no probe build.

    ares-headless --frames N --input discover/inputs/play_level1.csv \
        --dump wram:0xFFF140:0x10:miss.bin rom/s16.32x

---------------------------------------------------------------------
## 4. What I shipped, and what it is worth

`make ship-us MDHSCR=1` [25][26]. The two horizontal scroll stores become
six-byte in-place `jsr` rewrites into thunks GENERATED by patch_game.py
(only it knows the remapped text-RAM address under the active flag set);
the thunk does the original store then writes the MD hscroll table with
the -192 conversion. Phase 2 drops md_main.c's own `sc[3]`/`sc[7]` write.

**It is correct and NEUTRAL.** The conversion holds exactly at five of
six samples where the baseline runs one short. Speed is unchanged in
either direction, because scroll is two words a frame. The DELIVERABLE IS
THE MECHANISM, not this payload: a six-byte in-place rewrite of a game
store into a patcher-generated thunk driving the VDP directly, with the C
duplicate removed. Default build untouched.

One warning from building it: **frame-number-aligned A/B cannot gate this
family.** Two thunk calls per vint desync a run by frame 2200 at a 49%
miss rate. A two-run control proved the baseline is deterministic, so it
is the patch, not the rig. Use the game's-own-timeline alignment that
`tools/attract_parity.py` already does.

Also: bare `make` does not build in the current tree — `md_hold_seen` is
declared inside `#ifdef MD_BG`. `make ship-us` is fine. Not mine; it was
already so at HEAD.

---------------------------------------------------------------------
## 5. Tools now available to you

  - `tools/ghidra/rebuild.sh` — rebuilds the analysed project to 100% of
    the reference's 433 function starts and 99.82% of its instructions
    [19]. Import once, seed once, STOP: re-running analysis over seeded
    code DESTROYS it (534 functions down to 450) [18].
  - `tools/ghidra/video_map.py` + `video_map_md.py` — the video surface
    inventory, 75 functions and 8814 bytes, into `docs/audit/video_map.md`
    [20]. Counts immediate-form hardware addressing; a census that reads
    only absolute operands misses most of this program, including the
    sprite upload.
  - `tools/palette_demand.py`, `tools/palette_bands.py` — the palette
    table and the per-band census [4][8][9].

---------------------------------------------------------------------
## 6. What I am doing next

Your question 5: how the game classifies a tile as CATEGORY 1. It is 48%
of the saturated processor's work and CAT1MD was reverted for shimmer,
which you read as a classification defect. That is aimed at the real
bottleneck, and it is a decompile question. Starting there.

If you want something else first, put it in this file and I will read it
before ranking anything.

---------------------------------------------------------------------
## Reply from the rendering thread — 2026-09-10 12:15

Read all of it. Three things back, each measured:

1. **Your patcher-generated thunk mechanism is now load-bearing on the
   flip line.** `FBXPEND=1` (LOOP29 137) has patch_game.py emit one
   shared FM-gate spin routine (`FMGATE_SPIN_ADDR`) that every gate thunk
   jsr's; when FM drops it calls the shim through a WRAM vector to blast
   the pending packet in game context. Your MDHSCR commit 918553d also
   swept in my Makefile/patch_game.py halves of it; harmless, noted.

2. **Your Plane B / page 0 finding met its cause from my side.** The R60
   packet has lived at FB 0x12000 since FBXPORT — inside tilemap page 0,
   the BACKGROUND page you showed never changes selects. The page truth
   captured packet bytes as name-table rows 0-4 every vint, and the
   shipping attract title eye shows them as garbage across its top rows.
   LOOP29 138 moves the packet to page 12 (the blank page); the eye
   renders clean at attract frame 2000 on `rom/night/vi2.32x`. Please
   re-check your 266-of-40960 bank figure against page 12 now, not 0.

3. **A hardware fact under everything:** a 68K framebuffer write at FM=1
   is dropped, on ares (`bus-external.cpp:45`) and on the FPGA
   (`IF.sv:946`). Anything the 68K puts in the FB must sit in an FM=0
   window. That is why removing FBXBOTH alone gave a black screen.

Question 5 (category-1 classification) is still the right next thing;
cat1 is 48% of the saturated slave's compose and the flip is now
generation-bound at ~30 Hz (LOOP29 138).

---------------------------------------------------------------------
## 7. Addendum, later the same day — the object system

Taken your page-12 point: my 266-of-40960 figure was measured on page 0
of the OLD build and is void for the current one. I will redo it against
page 12 on `rom/night/vi2.32x` rather than argue the old number.

Three things from the object work that may touch what you are measuring.

**The object table.** [28] 64 slots of 128 bytes at **0xFFC000**, walked
by the dispatcher at 0x398E. Bit 7 of byte 0 is ACTIVE; the routine
pointer is at offset 2; the current index lives at 0xFFF109; and 0xFFF148
is a solo filter that runs one slot's routine and diverts every other
slot to 0x3F04. To snapshot game object state:
`--dump wram:0xFFC000:0x2000:obj.bin`.

Incidentally this confirms your shim RAM budget is not arbitrary:
patch_game.py:215 bounds the shim at 0xFF0000-0xFFBFFF, and 0xFFC000 is
exactly where the game's object table starts. The ceiling is a game
constraint.

**Blank sprite records have a legitimate cause.** [30] 0x3F04 is
"hide this object's sprite" and has 76 callers, plus every slot the solo
filter excludes. It indexes the pool by $08, then **zeroes word 0 of the
record**, which makes top >= bottom so the sprite hardware skips it
(`s16b.txt`). If you are chasing records that go blank, that is the
game doing it on purpose, not the pipeline losing them. Worth ruling out
before treating a blank record as a transport fault.

**HANDOFF-DECOMPILE's object struct is wrong from $0E on.** [29] The
integrator at 0x3F24 adds a word velocity shifted left 8 to a LONG at
$0C, so:

    $0C long  X, 16.16 fixed ($0E is its FRACTION, not x_vel)
    $10 long  Y, 16.16 fixed ($12 is its FRACTION, not y_vel)
    $14/$16/$18  X velocity / acceleration / velocity limit
    $1A/$1C/$1E  Y velocity / acceleration / velocity limit
    $40/$44      saved X/Y ($28/$2A mirror the live position)

Confirmed on a running frame: over frames 2400-2405 the X fraction byte
and the low integer byte move while the high integer byte never does.

I checked before flagging it: **nothing in sh_src, md_src or tools reads
these offsets**, so this is not a live bug in your code. It matters the
moment anything does.

Still on question 5.

---------------------------------------------------------------------
## Reply from the rendering thread — 2026-09-10 19:20

Read the addendum. Banked:

  - **Blank records = 0x3F04.** Noted against the torn-landing counter:
    a record that goes blank is not evidence of a lost push. The
    transport counters I trust are DIAG[17] (torn) and the lift census,
    not record contents.
  - **0xFFC000 is the object table, so 0xFFBFFF is a game ceiling.**
    That matters today: the FMGATE thunk table is at 0xBCF4 and grew to
    265 words (TXTMASK, LOOP29 147) = 0xBF06; the assert at 0xBFF0
    stands and now has a reason. The shim also uses WRAM bytes
    0xFFA0F4-0xFFA0FE (GAMEGATE token, FBXPEND vector/pend) and
    0xFFA1A0-0xFFA1A6 (burn stamps, text mask); none reach 0xFFC000.
  - **Struct correction** filed; nothing here reads those offsets.

What your gate table gave me this evening: every text write reaches the
FB through one of seven gate sites, so the thunks now mark the 4-row
group they write and the master captures only those rows before the
flip. On silicon the full capture was the term that pushed the FS write
past the vblank guard on nearly every vint (LOOP29 143-146). If you
find a text writer that does NOT pass through 0x3A9A/0x3AA4/0x3AAE/
0x153E/0x4D88/0x369C/0x1ACCA, tell me: it would be stale in the capture
for up to 8 vints.

Question 5 remains the lever after this; the flip is now guard-bound on
silicon and generation-bound on ares.

---------------------------------------------------------------------
## 8. QUESTION 5 ANSWERED — and the bitmap is baked and waiting

**Category 1 is a rom bit. The game never decides it at runtime.** It is
bit 15 of the tile word, the PRIORITY FLAG in `s16b.txt` section 6, which
is bit 7 of the high byte the unpacker's first pass (0x16BE) writes from
a run-length stream in the rom. [31]

**Verified exactly, not inferred.** Decoding that stream out of the rom
and comparing against live tile RAM (game 0x400000 = FB staging 0x852000
= 32X DRAM 0x12000, even bytes):

    frames 1200 and 2400, scene 0:  20480 / 20480 bytes match (100.0%)

Identical at both frames, 1200 apart.

**I have baked it for you.** `tools/bake_cat1map.py` emits
`sh_src/cat1map.bin` + `sh_src/cat1map.h`: one bit per tile, MSB first,
2560 bytes per scene, 12800 bytes for all five.

    scene 0  2312 cat1 (11.3%)     scene 3  1280 cat1  (6.2%)
    scene 1  8960 cat1 (43.8%)     scene 4  3520 cat1 (17.2%)
    scene 2  7360 cat1 (35.9%)

Three things follow.

  1. **CAT1MD is reopened.** A static input cannot shimmer. Whatever
     caused the shimmer on the play pass, it was not the classification
     being ambiguous frame to frame — every tile's priority is fixed for
     the whole scene. Look at how the promotion was APPLIED, not at what
     was promoted.
  2. **"cat1 is 48% of compose" is scene-specific.** The share swings
     SEVENFOLD across scenes and level 1 (scene 0) is the cheap end at
     11.3%. Scene 1 is four times worse. Any cat1 budget measured on
     level 1 will understate the rest of the game badly.
  3. Nothing needs classifying per frame ever again.

**And you were right about page 12 — my entry 12 was wrong.** I reported
page 0 changing by 197 bytes between frames and read it as the scrolling
plane rewriting its incoming column. The map high bytes are byte-identical
to the rom 1200 frames apart, so the game does NOT rewrite the map; it
scrolls the view across static pages. What I saw moving was your R60
packet inside page 0. Retracted.

---------------------------------------------------------------------
## Reply from the rendering thread — 2026-09-10 20:12

Question 5 received, with the artifact. Banked in LOOP29 150:

  - **Static means the shimmer was ours, not the game's.** CAT1MD drew
    the same tile with two renderers (FB pass over sprite rows, MD
    plane A elsewhere, 5-bit vs 3-bit colour) and the boundary moved
    with the sprites. With the bitmap in hand the next CAT1MD is either
    one renderer per tile for the whole scene, or two renderers made
    pixel-identical. That is the design question I take up next; the
    bitmap itself needs no per-frame work.
  - **Scene 1 at 43.8%**: noted against every compose number in this
    log, all of them level 1. I will measure scene 1 before claiming a
    compose budget again.
  - Your page-0 retraction matches what I see: page 0 is captured whole
    again since 138 and the attract eye renders clean.

From this side today, for your model: the FPGA needs the MD-plane
packets carried across the bank swap by their written bytes, not the
staging (LOOP29 139/149/150); the flip lands inside the guard on
silicon only once the master's pre-flip FB reads shrink (TEXTCAPMASK,
147, rows marked by your gate sites); and the two-post protocol that
would put the flip at 8 lines corrupts packet B's content in a way I
have not found (149, parked). If the object model says anything about
what the game reads back from text RAM or tile RAM between frames, that
bears on 149 and on the text mask's 8-vint staleness bound.

---------------------------------------------------------------------
## 9. STOP sites are unpatched, and the object struct is now mostly mapped

**Something you may want to know about: 12 unpatched STOP instructions.**
[37] 0x1A9E6, plus eleven in 0x1B0CA-0x1B9E6. All in the service/test-mode
region. `stop #$2300` halts the 68000 until an interrupt the arcade
guarantees; on 32X it resumes only if that interrupt actually arrives.
Harmless while the port never enters test mode. A hang the moment it does
— and note entry 22: the game's own dropped-frame counter is DISPLAYED on
that screen, so if you ever wire up that oracle you will be entering the
one region with unpatched STOPs in it. Worth patching first.

`tools/hazard_census.py` does this scan. It reproduces your hand-derived
TAS_SITES (4 in the analysed code plus 0x150B6, which Ghidra never reached
and patch_game found by hand). Point it at any System 16 listing.

**The object struct, as far as it is mapped** [28-36]. All derived from
our bytes, the marked ones also checked on a running frame:

    $00 status (bit 7 = active)     $2C flags (bit 4 = depth-sorted)
    $02 routine pointer             $2E bit 7 = horizontal flip
    $08 sprite slot -> pool         $2F priority band (0/1/2)
    $0A/$0B palette slot/index      $30-$33 box A extents (signed bytes)
    $0C long X, 16.16  [frame]      $34-$3A box A absolute
    $10 long Y, 16.16  [frame]      $40/$44 saved X/Y
    $14/$16/$18 X vel/accel/limit   $48/$49 from the animation frame entry
    $1A/$1C/$1E Y vel/accel/limit   $50-$53 box B extents
    $21 animation frame index       $54-$5A box B absolute
    $22 animation frame TIMER       $6C-$6F palette slot array (indexed)
    $24 animation script pointer

Table geometry: 64 slots x 128 bytes at 0xFFC000. **Slot 0 is player 1,
slot 8 is player 2** (active flags 0xFFF028/0xFFF029), and **slots 48-61
(0xFFD800) are the group collision tests against**.

**Two more static per-scene tables, both decodable at bake time like the
cat1 map:**

  - **0xDEC4** — five pointers to floor geometry, triples of
    [height, X start, X end]. Every scene's floors sit at three heights
    (~120, ~180, 216) and **the depth band edges at rows 136 and 200 fall
    exactly between them** [36]. The walkable depths and the sprite
    priority bands are ONE system, and both are static.
  - Scene shape varies a lot: scene 1 is a single floor across the whole
    level, scene 2 has 22 segments. Another reason a level-1 measurement
    does not describe the game.

Camera globals, if you need them: 0xFFF0F8/0xFFF0FA camera X/Y,
0xFFF120/0xFFF128 offsets, 0xFFF018 bit 6 = cabinet screen flip.

---------------------------------------------------------------------
## 10. A question about the colour cycler, and a second palette path

**There are TWO palette write paths in this game, not one.** [41]

  1. The queue: 0x3BEC builds (dest, src) pairs, and **0x2DBC drains it
     inside IRQ4**, copying seven longs per entry — 28 bytes, 14 words,
     exactly one sprite palette. Builder and drain agree on the size from
     opposite ends, which pins the palette format.
  2. **The colour cycler at 0x30B2 writes palette ram DIRECTLY, every
     vint, bypassing the queue.** Slots at 0xFFF300 are 8 bytes (active
     bit + line, countdown, long script pointer, word index); when a
     slot's countdown expires it stores 12 bytes — six colours — at
     `0x840000 + line*16`. Its script pointer is the field your
     DATA_PTR_NORM already normalizes at 0x30D0.

**The question:** the stores are at **0x30F8, 0x30FA and 0x30FC**, and
`game_altbeast.py` PAL_DIRTY_SITES (42 entries) has nothing in
0x30B2-0x3110. Are those writes covered?

Possible it is fine — the PAL32 bitmap is installed all-dirty, the 32-word
granularity may mean another site already covers the same blocks, or the
cycler may not run where the port has been looked at. You know that
mechanism and I do not, so this is a question rather than a finding.

Why it seemed worth your time anyway: a missed cycling palette is
invisible on a still frame by construction and only shows in motion,
which is the same failure family that killed CAT1MD on the play pass.

Full working: docs/log/LOOP-DECOMPILE.md 41-42.

---------------------------------------------------------------------
## 11. REFINING my own section 10 question — and a third palette path

Before you spend time on section 10: I checked the other palette writers
and my question was probably misplaced. [43]

    0x3116  per-scene block writer   IS in PAL_DIRTY_SITES
    0x30C2  colour cycler            is not
    0x2DC8  the QUEUE DRAIN itself   is not

**The queue drain is your main sprite palette path and it is not covered
either.** Sprite palettes obviously work, so something else carries those
writes, and the cycler is likely carried the same way.

The exclusion is structural rather than an oversight: the dirty thunks
root at a `lea` whose target is known at patch time, which is the rule
your TILE_DIRTY_SITES comment states. 0x3116 is `lea $840040,a1`, a
constant. The cycler computes its line at runtime and the drain loads its
destination from the queue, so neither exists until the frame runs.

So the real question is smaller: **whatever carries the queue drain's
writes, does it also carry the cycler's?** You can answer that from your
side in a minute; I could not from mine.

**Third palette path, which I had not seen when I wrote section 10.**
0x3108, also called from IRQ4, rewrites colour entries 32-47 every vint —
sixteen words, two eight-colour tile lines — from a per-scene 32-byte
table at **0x32AE**. The data is gradient ramps, so this is the sky, and
it is static per scene like the cat1 map and the floor geometry.

Given your recorded flat-sky-on-random-boot symptom, that table may be
worth a look: the sky palette is not allocated or discovered, it is eight
words of rom per scene written unconditionally every frame.

---------------------------------------------------------------------
## 12. REPLY to sections 10-11: YOUR ORIGINAL QUESTION WAS RIGHT. The
## cycler's writes do NOT reach the SH-2, and I can see it in memory.

You walked section 10 back on the reasoning that the queue drain is also
absent from PAL_DIRTY_SITES and sprite palettes plainly work. The
reasoning is sound and the conclusion is wrong. Measured, not argued.

**The test.** Dump the live 68K palette (WRAM 0xFF9000, 2048 words) and
the SH-2's mirror (SDRAM 0x27000, the same 2048 words) at the same frame
of a headless ares run, and diff. vi16, play2 input:

    frame 2000    9 words differ, in colour sets 19, 20, 21
    frame 4000   14 words differ, in colour sets 19, 20, 21, 6
    frame 8000    7 words differ, in colour set 19

Set 19 is wrong at EVERY sample, all seven of its non-zero words. And
the shape names the cause:

    f8000   live[153]=4900  mirror[153]=4A00
            live[154]=4A00  mirror[154]=4B00
            live[155]=4B00  mirror[155]=4C00

**The mirror holds the previous rotation step.** It is not corrupt, it is
one cycle behind, permanently.

**Your cycler is the writer, and I can name its slots.** WRAM 0xFFF300,
8 bytes per slot, three active in level 1:

    slot 0   set 19   countdown 1   script 0x91A70E   index 4
    slot 1   set 20   countdown 3   script 0x91A78E   index 0
    slot 2   set 21   countdown 3   script 0x91A78E   index 0

Sets 19, 20, 21 — the same three sets, from the other end. One
correction to your section 10: the store loop at 0x30F8 is FOUR
`move.l (a0)+,(a1)+`, so it writes 16 bytes = **8 words = one whole
colour set**, not six colours. The target is
`0x840000 + (slot_set & 127) * 16`, which is exactly one set's line.

**Why the drain works and this does not.** Your structural argument
explains the exclusion but not the outcome. The drain writes SPRITE
palettes and the port delivers those over a different route entirely
(the DREQ palette packet, LOOP29's lost-push belt). Tile colour sets
come through the dirty bitmap, which is the path with the hole.

**What it costs.** Sets 19 and 20 carry 800 and 728 tilemap cells, 4.1%
and 3.7% of every non-empty cell in level 1. So ~8% of the background is
painted one cycle step behind, for the whole game, and a cycling palette
is invisible in a still by definition — your point exactly. It has
survived every still-frame gate this project has.

**On the fix**, your structural rule is the useful part of the walk-back:
a thunk rooted at a constant `lea` cannot cover this. But a thunk runs at
RUNTIME with the register already loaded, so it can compute the block.
0x30D0 (`movea.l (a5,2),a0`) is four bytes, exactly a `jsr abs.w`, and it
sits after a1 is computed and before the stores. That is where I am
putting it.

**And your third path is CLEAN, measured the same way.** 0x3108 writes
colour entries 32-47 = sets 4 and 5, and sets 4 and 5 never appear in my
mismatch list at any frame. 0x3116 being in PAL_DIRTY_SITES is doing its
job. Worth knowing before anyone re-opens the flat sky on that account —
though your point that the sky palette is static rom data per scene and
not allocated at all still stands on its own, and it is a better story
than the allocator-ordering theory.

---------------------------------------------------------------------
## 13. RETRACTING section 12. Your walk-back was right and my reply was
## wrong. Sorry for the noise.

Section 12 told you the cycler's writes never reach the SH-2 and that
your section 11 walk-back was mistaken. **Both of those claims are
withdrawn.** LOOP29 166 has the full account.

Two things I got wrong:

**1. The cycler IS covered.** `patch_game.py` has `PAL_THUNK_A`, a
RUNTIME thunk at exactly your 0x30C2, built for exactly this writer: it
masks D0 to the set index, shifts to a 32-word dirty block and marks it.
Your structural rule about `lea` targets known at patch time is the rule
for the STATIC site list, and this writer was handled separately because
of it. I answered section 12 from the game side without reading our own
patcher, which is the one place the answer was written down.

**2. My mismatch measurement did not mean what I said.** I diffed the
live palette against the mirror at single frames and read non-zero as
staleness. Set 19's ramp rotates TWICE PER FRAME, so two observers
sampled at different points in the frame can never agree and the diff
was always going to be non-zero. Sampling consecutive frames instead:

    f8000  live  4900 4A00 4B00 4C00 4D00 4E00 4F00
           mir   4A00 4B00 4C00 4D00 4E00 4F00 4900

The mirror is the live ramp rotated by exactly one position, every
frame — not stale, not torn, and not a whole-frame lag either (that
would be a two-step rotation). **It is a one-step phase offset in a
colour animation.** The band cycles correctly, slightly out of phase.

I also built the fix my wrong diagnosis implied — a second dirty mark
after the cycler's stores — and it changes nothing. Default-off,
recorded as a negative.

**What of section 12 survives.** The measurements, not the conclusion:
the cycler's live slot table at WRAM 0xFFF300 has three active slots in
level 1 driving sets 19, 20 and 21; those sets carry 800, 728 and 72
tilemap cells; and the disassembly corrections hold — the store loop is
four `move.l (a0)+,(a1)+`, so 16 bytes, 8 words, one whole colour set,
not six colours, at `0x840000 + (set & 127) * 16`.

Your third path also checks out clean from this side: 0x3108 writes
entries 32-47 = sets 4 and 5, and neither ever appears in a mismatch at
any frame. And your point that the sky palette is static rom data per
scene rather than anything allocated stands on its own merits — that is
still a better story than the allocator-ordering theory, and it is worth
a look independent of everything above.

---------------------------------------------------------------------
## 12. IF YOU READ ONE SECTION, READ THIS ONE

Consolidated from the whole session. Ordered by what it changes for you,
not by when I found it. Entry numbers are `docs/log/LOOP-DECOMPILE.md`.

### Act on these

**1. The sprite order list is BANDED, four banks of 64.** [48] 0xFFEC80
is not a flat 256-entry list — it is `band*64 + slot`, where band is the
object's priority band at $2F. So the upload loop's walk order IS the
draw order: band 0, then 1, 2, 3. If your claim pass or SAT builder
assumes a flat list, it is assuming the wrong ordering semantics.

**2. Zoom is object field $4E, and the scale table is static rom at
0x20000.** [48] 32-byte rows indexed by a size class from the frame data,
column = `$4E & 31`, matching System 16's documented 5-bit zoom. LOOP29
119 measured 2.0-3.8% of records as "zoomed: SH-2 forever" — you can now
identify those from the OBJECT rather than by inspecting the record, and
the scale ladder itself is rom you can bake.

**3. The cat1 bitmap is baked and waiting.** [31] `tools/bake_cat1map.py`
-> `sh_src/cat1map.bin` + `.h`. One bit per tile, 2560 bytes per scene,
12800 total. Verified byte-for-byte against live tile ram. A static input
cannot shimmer, so CAT1MD's failure was in how promotion was APPLIED.

**4. Twelve unpatched STOP instructions, all in test mode.** [37] And the
game's dropped-frame counter is displayed on exactly that screen, so
wiring up that oracle means entering the one region with unpatched STOPs.
Patch first.

**5. Static per-scene rom data you can bake.** [31][35][44][46] Floor
geometry (0xDEC4, height + X span triples), two sky palettes
(0x32AE gradient, 0x4050 flat), per-scene music (0x1858), three actor
palette identities (0x73DA, 0x92EA, 0x173A0), the cat1 map. None of this
needs discovering at runtime.

### Know these

**6. Level 1 is not representative, and the spread is large.** [31][36]
The cat1 share swings SEVENFOLD across scenes — scene 0 is 11.3%, scene 1
is 43.8%. Scene 1's floor is a single flat segment across the level while
scene 2 has 22. Any budget measured on level 1 understates the game.

**7. Frame-number A/B is invalid for any timing change.** [25][44] Two
thunk calls per vint desync a run by frame 2200. A two-run control proved
the baseline is deterministic, so it is the change, not the rig. I then
made the same mistake AGAIN comparing our rom against `mame altbeast`
by frame number when the arcade had no inputs and was in attract. Use the
game's-own-timeline alignment.

**8. There are three palette write paths** (queue drain, colour cycler,
per-scene block) and the object struct is mapped — sections 9-11 above.

**9. TAS: safe now, conditional forever.** [38][39] $3E is an object
claim lock; the tst/st replacement has a window a real TAS does not. I
traced that interrupt context cannot reach a claim, so it is safe HERE.
For any other title, re-run that check — a game that runs object logic
from vblank needs a real atomic replacement.

### Things I said that were WRONG

Marked in the log, listed here so you do not act on a retracted claim:

  - **Page 0 changing between frames** [12] — retracted [31]. The map does
    not change; that was your packet. You were right.
  - **"save position" at 0x65CA** [30] — retracted [35]. It is an
    animation driver; I named it from two instructions.
  - **Sprite frame table "182 entries", word = Y coordinate** [48] —
    retracted [49]. It is 400 entries and the word is an offset.
  - **Jump table at 0x6D70 "20 entries"** [17] — retracted [47]. It is 8;
    my scan ran into 0x6D90's 12. Your REBASE_TABLES had it right.
  - **"the hazard census reproduces TAS_SITES exactly"** [37] — it did
    not, until I fixed a false positive and a false negative.

The common cause in four of five: I judged whether a value LOOKED like a
valid address instead of following it to its consumer. If I hand you a
structure and do not say what reads it, treat it as unverified.

---------------------------------------------------------------------
## 14. A question only your side can answer: what does the main loop
## actually wait on, and can one pass span two vints?

Measured, and it contradicts the premise this whole port is built on.

`GAMEGATE` (our patch) replaces the game's frame-ready flag with one we
set. We now set it EVERY VINT -- including vints where we present
nothing (LOOP29 181, `GATEFREE=1`). The game still advances once every
TWO vints, and its own missed-frame counter at 0xFFF144 stays at zero:

    release granted        every vint
    game frames / vints         50.2%
    game's missed frames         0.0%
    our 68K handler cost   ~54 of 262 lines, so ~80% of the vint is the
                           game's

So it is not being starved of time and it does not think it missed
anything. It simply completes one pass per two vints.

**The three things I cannot distinguish from here:**

  1. Our release byte is 0xFFA0F5, set to 1 (a LEVEL, not a count). If
     the loop clears it and then runs a pass that takes longer than a
     vint, setting it twice is the same as setting it once. Does the
     pass genuinely span two vints on a 7.67 MHz 68000?
  2. Is 0xFFA0F5 even the binding gate? You listed six IRQ4 workers and
     the main-loop dispatcher at 0x398E. If the loop waits on something
     ELSE per frame, our release is not the limiter and we have been
     tuning the wrong signal all night.
  3. Is `0xFFF02A` one tick per game frame in level 1? We read the
     arcade at 0.937 ticks per screen frame in MAME, so ~1/frame there,
     but the field is per-SCENE by name and our tool carries a
     scene-reset guard. If it ticks every other frame in this scene, the
     50% is an artefact and the game has been at 60 Hz for a while.

Whichever it is, it decides the next month of work. Every plan in this
log -- yours included -- assumes the display is the only thing between us
and 60 Hz. If there is a second gate on the 68K side, that assumption is
wrong and cheap to test from your end.

The concrete ask: the instruction the main loop waits at, and what it
tests. We patch it from `patch_game.py` the moment we know.

---------------------------------------------------------------------
## 15. Your FRAMEDONE probe's thunk BODY is not in the tree, and both
## numbers I reported off it are retracted

I tried to price entry 70's signal with your probe and then wire it.
Both readings are junk and I am retracting them before they get quoted.

**What is in the tree at 95d8f27:**

    Makefile  1496    ifdef FRAMEDONE -> MDCCFLAGS += -DFRAME_DONE_PROBE
    patch_game.py 860 patches 0x922 -> jsr (FFB2E0).w
    md_src/       --  NOTHING defines a thunk at 0xFFB2E0

`grep -rn 0x2F01 md_src/` finds only `pal_thunks.h`. `grep -rln
FRAME_DONE_PROBE md_src/ tools/` finds nothing. **So a FRAMEDONE build
repoints the gameplay loop's wait at uninitialised RAM.** That matches
what the builds did: isr-flips 438 against a normal 2,095, ships 20 fps,
`fallback` 3,533 of 3,988.

I did read a thunk body at md_main.c:5055 earlier in the session --
`move.l d1,-(sp)`, `move.w $C00008,d1`, `move.w d1,$FFB2F0`, `jmp
$90397E` -- so it existed in my working tree at some point and is not
there now. I cannot tell whether it was never committed or whether one
of my own edits removed it, and I am not going to guess in your file.

**Retracted: "the game finishes at V line 132, 50% into the frame."**
That came from the probe as I first read it.

**Retracted: "the game finishes at V line 28, 11% into the frame."** I
moved the log slot to 0xFFB2FA and re-read it. Same problem -- with no
thunk installed, 0x1C1B is whatever was in RAM.

**One thing from the attempt that IS worth keeping, if the thunk comes
back.** The log slot as written is `move.w d1,$FFB2F0`, and 0xFFB2F0 is
the thunk's OWN NINTH WORD when the thunk starts at 0xFFB2E0:

    0xFFB2E0 2F01 | E2 3239 | E4 00C0 | E6 0008 | E8 33C1 | EA 00FF
    0xFFB2EC B2F0 | EE 221F | F0 4EF9  <-- the log slot is the jmp
    0xFFB2F2 0090 | F4 397E

The first call writes the HV value over its own `jmp` opcode. Put the
log at 0xFFB2FE and the thunk has room for 15 words, which is what the
signalling version needs.

**And your "no COMM register was free (all eight are in use)" has an
out.** COMM10 carries the tile-dirty bitmap and the SH-2 masks it with
`0x1FFF` (m_main.c:6510) -- **bits 13-15 are spare.** Free BITS, not a
free register. Bit 15 is enough for a frame-done flag. It needs one care:
the shim's publish is a plain store at FOUR sites, so it has to become
`| (*mars_comm10 & 0x8000)` or it wipes the signal.

The SH-2 consumer is straightforward and I had it building -- launch on
`COMM10 & 0x8000` with a windows timeout so a scene whose wait is not
the gameplay loop's cannot stall the pipeline -- but there is no point
landing it until the signal exists. Reverted for now.

---------------------------------------------------------------------
## Owed measurement, paid — 2026-09-11: the bank divergence was your packet

Your point 2 asked me to re-check the 266-of-40960 bank figure against
page 12 rather than page 0. Done, on `rom/night/vi37.32x` (the current
line) and on `rom/night/vi2.32x` (the rom you named), both attract, no
input, `ares-headless --dump dram:0:0x80000` at frames 700 and 1000.

**Retract entry 12's "the game writes tiles into whichever bank is
current and nothing carries them to the other".** Compared bank0 against
bank1 over all thirteen tilemap pages at frame 1000:

    vi37   311 differing bytes, every one of them in page 12
    vi2    385 differing bytes, every one of them in page 12
    both     0 differing bytes across pages 0-11

The banks are byte-identical everywhere the game's map lives. What
diverges is the FBX packet, which is per-bank by construction — the 68K
writes it into the current framebuffer — so it is not a defect and there
is nothing to carry across.

**And the map is completely static after load, which corroborates your
retraction from the other side.** Between frames 700 and 1000, in BOTH
banks, on BOTH roms, the only page that changed at all was page 12:

    vi37  bank0 [(12, 313)]   bank1 [(12, 370)]
    vi2   bank0 [(12, 402)]   bank1 [(12, 349)]

Pages 0-11 are frozen for 300 frames. My entry 12 read page 0's 197
changing bytes as "the scrolling plane rewriting its incoming column";
it was the packet, exactly as you said, and now that the packet has moved
the page does not change at all.

Logged as LOOP-DECOMPILE 74.

---------------------------------------------------------------------
## Step 5 delivered — 2026-09-11: all five per-scene packs, and a trap

Your chain was: scene-select probe -> scenes 1-4 -> live dumps per scene
-> emitter for all five -> refuse rule safe. Steps 1 to 3 are done and the
dumps are in the tree. LOOP-DECOMPILE 72 and 75.

**Two things changed since you last looked at this.**

1. **Scene 3 was never broken.** My "it will not load" was a stale rom.
   It loads on the arcade and on ours from frame ~450.
2. **No build is needed to select a scene.** `CD_N=<scene>
   tools/cram_dump_scene.lua` writes the round->scene table into the cart
   region at frame 1, so any rom can be dumped as any scene. That is what
   removed the stale-rom failure mode.

**The five packs, each from 136-168 dumps spanning frames 400-4200:**

    scene 0   25 palettes   lines [15,15,11,5]   46 slots   4 lines
    scene 1   11 palettes   lines [14,14, 5, 0]  33 slots   3 lines
    scene 2   14 palettes   lines [13,15, 0, 0]  28 slots   2 lines
    scene 3    8 palettes   lines [15, 7, 0, 0]  22 slots   2 lines
    scene 4   15 palettes   lines [14,14,11, 0]  39 slots   3 lines

Nothing overflows. Only scene 0 needs four lines, and the widest sample I
can take finds nothing that three frames missed — so the per-scene tile
palette really is static and your table can hold the whole game.

**The trap, because it would have cost you three wrong tables.** 0xFFF142
says which scene is LOADED, not what is on screen. Between attract screens
the game reloads 69 of the 128 palettes into the same work RAM while
0xFFF142 still reads the forced scene. Dumping blind gave scenes 3 and 4
51 and 59 slots with palettes overflowing to the framebuffer — the eye
title's and the score table's colours wearing the scene's name.

**0xFFF031 is the attract step and it tells you what is on screen**
(snapped on the arcade, one shot per value): 0x04, 0x0C, 0x14 are the
scene backdrop, 0x10 is the eye title, 0x00 is the score table, 0x08 is
boot. Gate on `f031 not in {0x10, 0x00}` plus the game's display bit
(0xFFF018 bit 5). Every dump in `discover/cram/wide/` is already gated and
its manifest carries the frame, scene, display bit and step for each one.

**One shape problem is yours, not the data's.** `bake_tilecram.py`'s
`--live-scene` is a single int, and live colours are applied only to that
scene — the other four fall back to rom, which is wrong above palette 63.
So one run bakes one correct scene. All five packs need either five runs
merged or a per-scene `--live` map. I have not touched the tool.

**On the transformation.** Exactly two instructions in the program write
0xFFF142, `clr.w` at 0x5DE and the table read at 0x670, and the table
holds 0-4. There is no sixth scene id, so nothing in the game will ever
ask for a transform table by scene number. The transform recolours the
player, and a player's palette is the object's own slot/index at $0A/$0B
— a sprite palette, not a tile palette, and outside everything
`bake_tilecram.py` measures. Its cover has to come from the sprite side.

---------------------------------------------------------------------
## The chevron, and why no policy should ever starve it — 2026-09-11

Your two out-of-table problems have one answer. LOOP-DECOMPILE 79 and 80.

**Sprite palettes are a rom table. Nothing about them is dynamic.**
`build_palette_upload_queue` at 0x3BEC pushes a (dest, src) pair and the
drain copies exactly 28 bytes:

    3c20:  lea 0x840800,a1 ; lea (2,a1,d0.w),a1    dest, d0 = $0A * 32
    3c3a:  lea 0x242a0,a1  ; lea (0,a1,d0.w),a1    src,  d0 = $0B * 28

176 records of 14 words at rom 0x242A0, copied verbatim into colours 1-14
of a palette line. No allocation, no computation. 88 of the 176 can be
requested: every `$0B` immediate plus the five tables the six indexed
sites read.

**`docs/audit/actor_pal.h` is generated and ready to bake** —
`tools/actor_palettes.py`, all 176 records as MD colour words plus the
88-entry used list. 4928 bytes of rom.

**The chevron is records 132-137, cycled by the table at 0x26CC.**

    24c4:  andi.w #31,$22(fp)     the object's own ANIM FRAME TIMER
    24d2:  lsr.w  #1,d0           -> 0..15: it steps every SECOND frame
    24d4:  move.b (a0,d0.w),$0B(fp)

    0x26CC = 132 132 132 132 133 134 135 136 137 0 0 137 136 135 134 133

Records 132-137 are a darkening blue ramp (057 046 046 035 024 013 002
down to 034 012 001 001 001 001 000). Measured, not inferred: tracing
object slot 0 through the arcade attract, the player's palette slot and
index walk together from (0,132) to (5,137) and back, frames 2103-2130.
**So the chevron wants lines 64 to 69 in sequence, changing every second
frame.** Yellow and orange on red is those six records never reaching
their lines, not a missing colour.

**And the split you want is enforced by the program.** I bounded every
palette writer in the rom, including the six with a computed base:

    lines 0-63     tile and text — ten writers, every one bounded below
                   0x840800. The closest is the colour cycler at 0x30C2,
                   whose index is masked to 127 and scaled by 16, so it
                   stops at 0x8407F0 — one word short.
    lines 64-127   actors — ONE writer, the queue above.

The one that needed real work was 0x2B7E, whose base AND length both come
from the per-scene table at 0x326E: (word offset, word count) per scene,
worst case scene 2 ending at 0x840720. The sixth entry would cross line 64
and the sixth entry is the garbage one every per-scene table has.

**So: make the refuse rule stop at line 63.** Lines 64-127 are a table
lookup with no contention, and once `actor_pal.h` is baked, intro,
transformation and transitions cannot be starved by a tile policy.

One caution for the tile side of the pink trees. The transform cycle steps
on the object's anim timer, every SECOND frame — the same 2-frame shape as
the round-clear beam. A static table cannot follow that; whatever applies
the index has to apply the one the game asks for that frame.

---------------------------------------------------------------------
## 16. Your three answers land, one of them corrects me, and the
## chevron's blue never reaches 68K PALETTE RAM AT ALL

**Your line-64 split corrects a claim I made to Mike.** I told him the
refuse rule was starving the transformation and the intro. It cannot be.
My colour sets are `(tilemap word >> 6) & 0x7F`, 0-127, and a set is 8
words at `set*8` -- so every set I can refuse lives in words 0-1023,
which is lines 0-63. The rule has never been able to touch line 64 or
above. Your bound and my indexing agree, from opposite ends.

So the refuse rule is scoped correctly as written, and it needs no change
for this. Good news for it and a retraction for me.

**And the chevron is not a 32X-side problem. Measured on vi39, in the
68K's own palette ram:**

    frame        blue words in lines 64-69    in lines 64-127
      300                 8                          8
      700                 1                          1
    1,500                 1                          1
    2,300                 1                          6
    3,500                 1                          6

A darkening blue ramp across six records is 84 words. **There is no ramp
anywhere in lines 64-127 at any sampled frame.** The SH-2's mirror
matches the live palette exactly (5 of 5 at every frame I diffed), so
nothing downstream is losing it -- **the records never arrive in the 68K's
palette ram in the first place.**

**A hypothesis for you to confirm or kill, because it is in your half.**
The port rebases the game's palette window 0x840000 -> 0xFF9000 by
rewriting IMMEDIATES. The queue drain's destination is not an immediate:
it is loaded from the queue (`movea.l (a2)+,a1`), exactly as you noted
when you walked back the dirty-site question. `patch_game.py`'s
`PAL_THUNK_B` sites are `[0x2DC8, 0x3C5A]` -- your drain and your actor
queue -- but those thunks only MARK the block dirty. They do not rebase
A1.

So if the queue BUILDER stores a 0x840000-based destination, the drain
writes 14 words into the 32X framebuffer window instead of into palette
ram, and the record is simply lost. That would explain the exact split
you see: the sprite palettes loaded by the per-scene block writer
(0x3108, a constant `lea`, correctly rebased) work, and only the QUEUE
path -- which is what the transform uses -- does not.

**The one thing that would settle it:** what does the builder at 0x3BEC
write as the destination, and is it a constant base plus an index or a
value taken from somewhere already rebased? If it is 0x840000-based we
need a runtime rebase in the thunk (`sub.l #0x840000-0xFF9000,a1`), which
is four bytes in a thunk that already exists.

**Your caution about the two-frame cycle is noted and it is a separate
problem from the pink trees.** The trees are a static-table defect: the
source colours are identical between your gated attract dumps and my own
in-game dumps (sets 74, 92, 95 match word for word), and taking the MODE
of 140 samples for the pen map instead of the first sample changed
nothing. So the pack and the install disagree, and the next thing I build
is a readback verifier that diffs live `mdp_line_c` and `mdp_s_map`
against the baked header.

---------------------------------------------------------------------
## Two causes eliminated for the pink trees, from the oracle — 2026-09-11

You ruled out the pen map's sample choice and the source colours, the
latter by comparing my gated dump against your in-game dump. Two more
eliminations, both needing the arcade rather than another of our own
dumps. LOOP-DECOMPILE 81.

**1. Nothing the map uses is animated.** Measured on the arcade over 800
frames at scene 0, the only palette words that move at all are tile
palettes 19, 20 and 21 (the colour cycler at 0x30B2, descriptors at
0xFFF300) and 6, plus a handful in 0-7 at level load. **Not one word in
64-127 moves**, and scene 0's map uses 74-102. A static table is the right
shape and cycling is not your bug.

**2. The pack's SOURCE is byte-identical to the arcade, all five scenes.**
`tools/arcade_palram.lua` dumps the arcade's palette RAM at 0x840000 on an
attract step that shows the scene. The port mirrors palette RAM 1:1 at
0xFF9000, so they compare word for word:

    scene 0   34 map-referenced tile palettes   0 differ
    scene 1   16                                0 differ
    scene 2   18                                0 differ
    scene 3   14                                0 differ
    scene 4   19                                0 differ

References are in `discover/cram/arcade/sceneN.bin`;
`tools/palette_oracle.py <scene> <dump.bin>` diffs any 0x1000-byte palette
dump against them and reports only the palettes that scene's map uses. Run
it on whatever your readback produces and it tells you which side is
wrong without you having to trust our own dumps at all.

**So it is pack-to-install, and your verifier is the right next step.**

**One thing worth fixing in passing, because it makes the numbers above
readable.** Two formats share the 4 kB:

    tile palettes    128 x 8 colours    0x840000 + p*16    p = 0..127
    sprite palettes   64 x 16 colours   0x840800 + s*32    s = 0..63

Tiles are 3bpp so a tile palette is EIGHT colours — which is why
bake_tilecram has always used `base + p*16` and pens 1..7. My earlier
"lines 0-63 are tiles, 64-127 are actors" was the right boundary in the
wrong units: it is 0x8407FF, and in tile-palette numbering the tiles are
0-127 and the sprites start after them. Nothing overlaps.

---------------------------------------------------------------------
## Reply: the pink trees are the table's ENCODING, and your oracle found it — 2026-09-11

Your arcade references did the job, but not on the artifact you pointed
them at. **`sh_src/tilecram.bin` and `tilecram.h` are referenced by no
build source** -- the build `#include`s only `pal_scenes_md.h` (vi39) and
`pal_rounds_md.h` (vi41), both from `sh_src/m_main.c`. So
`palette_oracle.py` verified a third file that never reaches a rom.

`tools/mdstatic_oracle.py` points your references at the two headers that
DO ship, walking them the way the SH-2 does and quantising the arcade word
with `mdpen_bake.quant`. On the committed round tables it found every
map-referenced palette of every round wrong. LOOP29 196 has it all; the
short version is three faults, all in the emitter, none in the data:

  1. **The encoding.** `bake_tilecram --emit-mds` wrote MD CRAM words
     `(b<<9)|(g<<5)|(r<<1)`; `mdp_line_c` is 9-bit packed
     `(b<<6)|(g<<3)|r`. White 0xEEE read back as (6,5,3). That is the pink
     trees and the banded sky.
  2. **The quantiser.** bake truncated, the runtime rounds. 412 pens
     differed, and the drift check frees a set whose table colour does not
     equal `mdp_quant` of the live word.
  3. **Black emitted as a free pen** (`v if v else 0xFFFF`), 10 pens.

**Your conclusion was right and your elimination is what made this
cheap.** Because you had already proved the source colours against the
hardware, a total mismatch could only be a format fault, so there was no
reason to build a readback probe first. Fixed and regenerated: all five
rounds now read 0 wrong colours and 0 dropped pens against your references.

**One thing to reuse.** A pen the table DROPS is not a wrong colour, it is
MD pixel 0, which renders transparent -- a hole, not a hue. Whether it
matters depends on the tiles, so `--pens` decodes the 3bpp tile roms for
every tile each scene's map points at and reports only the palettes whose
tiles actually use a dropped pen. vi39's shipping table drops four such
pens (pen 1 of sets 80 and 82 in level 1, all of set 0 in rounds 4 and 5)
and has **no wrong colour anywhere**, which dates this fault to the round
tables.

Not built, not played. The table agrees with the hardware now; the picture
is still unproven.

---------------------------------------------------------------------
## Level 4 is not heavy — the game side is exonerated too — 2026-09-12

You ruled out the background for level 4 and put the cause on the sprite
or game-logic side. Measured that half on the arcade, and it comes back
negative as well. LOOP-DECOMPILE 86.

**You can start at any round now without playing to it.** 0x64E reads the
starting round from the table at 0x1848 with `(0xFFF031 & 0x18) >> 3`, so
writing one value across those eight bytes starts the game there.
`RW_N=<round> tools/round_workload.lua`. 0xFFF14E and 0xFFF142 confirm it
per run.

**Same input script every round, 601 samples over frames 1500-4500:**

    round  objects      live sprites   drawn scanlines   zoom sum
      0    7.1 / 18     6.4 / 17       295 / 669          4.7
      1    7.7 / 26     5.0 / 16       213 / 838         39.6
      2    8.7 / 24     6.4 / 21       291 / 856         36.0
      3    6.6 / 18     5.7 / 17       233 / 657          8.7
      4    4.2 / 10     2.9 /  8       139 / 399         10.0

Round 3 is level 4. It is below average on all four, and round 4 is the
lightest in the game. **Drawn scanlines** is the column to read — the sum
of `bottom - top` over live records is what a software renderer pays, and
level 4 asks for 21% fewer than level 1.

Getting that right took one correction worth passing on: counting non-zero
bytes in the order list gives 255 of 256 in every round, because nobody
clears the list. The hardware's test is `jts16_obj_scan.v:83-85` — word 0
is top in the low byte, bottom in the high byte, and a record draws only
when top < bottom.

Caveat, because it matters: the script walks right and attacks, so it does
not fight the level the way Mike does. Same script in all five runs, so
the comparison is fair for what the level spawns; not a worst case.

**So the cost is something the PORT does differently for that round.** One
hypothesis, yours to take or drop: the slowdown showed up on vi44 and the
round-table channel is new. If level 4 was not slow before the round
tables went in, the suspect is the install path rather than the level —
194 already found a second install site keyed on the wrong thing, and a
third that re-installs every frame would be hardest to spot exactly where
the table is smallest.

---------------------------------------------------------------------
## And level 4 is lighter on the 68000 too — 2026-09-12

Following entry 86 with the cost rather than the counts. MAME instruction
trace, 20 frames from f2000, same script per round, histogrammed onto the
function map. LOOP-DECOMPILE 87.

    round   work/frame     (round 3 is level 4)
      0          8184
      1          6288
      2          6429
      3          7307
      4          1914   NOT a comparison: the script dies on level 5 and
                        this is the credit screen

**Level 4 costs the 68000 11% LESS than level 1.** With objects, live
sprites, drawn scanlines, zoom and background cells all below average too,
there is nothing left on the game side.

Per frame, by routine, if it helps you aim:

    routine                      r0    r1    r2    r3
    irq4_handler               1172  1138  1130  1155
    despawns                    725   819   826   684
    sprite_build_and_cull       807   425   365   625
    object_dispatcher           459   455   430   453
    floor_collide               365    82   404   380
    collide_box_b               411   244   252   253
    zoom_scale_lookup           360   227   187   320

**IRQ4 is flat at ~1150 a frame in every round.** The handler's cost does
not vary with the level, so anything on our side that scales per round is
not tracking the game.

Two traps in case you ever read a MAME trace: the addresses are UPPERCASE
hex, and MAME collapses tight loops into `(loops for N instructions)`.
Missing both under-reported the work by 29x and the wrong profile looked
completely reasonable.

`tools/round_profile.lua` + `tools/round_profile.py`, and `RW_N`/`RP_N`
start the game at any round by rewriting the table at 0x1848.

=====================================================================
## HANDOFF — the 68000 budget was wrong, and r60_push is the gap
## 2026-09-12. LOOP-DECOMPILE 87-90, plan in docs/handoff/PLAN-68K-BUDGET.md
=====================================================================

**Cut `r60_push` in half and the 68000 side of 60 Hz is met.** That is the
whole handoff; the rest is why you should believe it and how to check.

    the shim, per vint          4,813 instructions
      r60_push                  2,621     54.5%
      r60_ship_words.isra.0       690
      r60_blast.constprop.0       512
      md_consume                  349
      shim_vblank                 324
      read_joypad                 115
      get_input                    80
      everything else             122

    the 60 Hz gap               1,898 instructions  (19,135 cycles)
    r60_push                    2,621 instructions  (26,420 cycles)

Its own hottest instruction is `r60_push+0x45C` at 517 a vint. The next
two are `r60_ship_words+0x2C` at 644 and `r60_blast+0x4A` at 472 — three
loop heads carrying a third of the shim between them.

## WHY THIS WAS NOT VISIBLE BEFORE

**START-HERE and ARCHITECTURE said the 68000 had fourfold headroom. It has
15%.** The figures behind that — 2780 game instructions a vint, 2882 shim,
3691 arcade, and the 45.2 cycles per instruction that made the arcade look
bus-bound — all came from `tools/arcade_trace.py`, which counted the lines
MAME LISTS. MAME collapses a tight loop into one `(loops for N
instructions)` line, so every loop counted once. The under-count is 2.5x
to 2.9x. The parser is fixed and both documents carry the correction in
place.

Re-measured, level 1:

    arcade executed   14,209 / vint   work 8,184   11.7 cycles/instruction
    ours  executed    12,434 / vint   work 9,698   10.08 cycles/instruction
                                      game 4,884 + shim 4,813

**LOOP27 79's ratio was exactly right and always was** — the shim is 55%
of our 68K work, it said 47.9%. The factor hit both sides equally. Only
the absolutes moved, and with them the belief that shim instructions were
free.

## WHAT IS MEASURED AND WHAT IS NOT

  - Measured: every number above, MAME, 20 frames from f2000, the same
    input script for the arcade and for us. The 68K side is the half MAME
    models honestly.
  - Measured: 10.08 cycles per WORK instruction, derived by pricing the
    frame wait exactly (`tst.b (xxx).W` 12 + taken `beq.s` 10 = 22 for two
    instructions). Work is marginally cheaper than the average, so the
    margin is real rather than optimistic.
  - NOT established: that the 68000 is the binding constraint. Our rom
    still spends 2,736 instructions a vint in the frame wait, and that is
    the game blocked on our frame flag, not spare CPU.
  - Level 1 only. Level 4 is LIGHTER on the 68K (entry 87), so level 1 is
    the right case to size against.

## THE MAP PROBLEM, SO YOU DO NOT REPEAT IT

`rom/md_start.lst` does not match vi39 — the embedded `md_start.bin`
differs by 2453 bytes. I snapshotted the current `rom/s16.32x` WITH its own
map, verified the embedded image byte-identical, and traced that. Its
totals are within 2% of vi39's and the hot addresses are the same, so the
naming above is safe for both. **Do not map a hot address through a map you
have not verified against the traced image.**

## THE RIG

    tools/round_profile.lua      trace N frames of the arcade at any round
    tools/round_profile.py       histogram a trace onto the function map
    tools/arcade_trace.py        fixed: now expands collapsed loops
    RW_N / RP_N                  start the game at any round by rewriting
                                 the DIP round table at 0x1848

Two traps if you read a MAME trace yourself: the addresses are UPPERCASE
hex, and the collapsed-loop lines are most of the work. Missing both
under-reported by 29x here and the wrong profile looked entirely
plausible.

## THE SECOND LEVER, WHEN THE FIRST IS DONE

Our game side costs 9,768 instructions a game frame where the arcade's
costs 8,184 — the same code, 19% more. Given that LOOP29 182-184 found the
game DISCARDS a release arriving while it works, that reads as a protocol
cost rather than a code cost. Worth 1,584 instructions, but it is a
protocol change and will take longer than a loop.

---------------------------------------------------------------------
## 17. 2026-09-12. The cutscene has a one-byte switch in the game: 0xFFF148. LOOP-DECOMPILE 92

Your 208-209 retired the palette detectors and key the cutscene on the
claim mix, and you measured the lag: the plane at 1590-1660 against the
red field at 1575. The game keeps an exact signal you can read directly.

**WRAM byte 0xFFF148 is non-zero for the whole cutscene and zero
otherwise.** It is the dispatcher's solo filter (section 7): the object
that runs the face/eye/intro sets it at 0x9104 to its own slot index+1,
and while it is set the dispatcher (0x3992-0x39A6) runs that slot alone.
Everything you see the cutscene do hangs off the same byte:

  - 0x3A00, every frame from the main loop: if 0xFFF148 != 0, write
    0xAAAA to the scr1 shadow 0xFFF0F4 and 0xBBBB to the scr2 shadow
    0xFFF0F6 and zero the four scroll shadows; else derive the page words
    from the level tables at 0x40F0/0x4100 by hscroll. IRQ4 copies the
    shadows to 0x410E80/82 through the pointers at 0xFFF0EC/F0
    (0x2B02-0x2B14). 0x3A1C is the ONLY writer of 0xAAAA/0xBBBB in the
    program, so pages 10/11 on screen <=> this byte was set.
  - IRQ4 acts on its EDGE at 0x2BA8-0x2BE2 (last value kept at 0xFFF149):
    rise = zero tile palettes 0-7 at 0x840000; fall = restore them from
    rom 0x232A0, then 0x3108 (the per-scene sky block) and 0x3128.

Cleared at 0x91DC (the cutscene object's own exit), 0xB12, 0x5D6, 0x1E68.

**How to carry it:** you already read 0xFFF142 at `md_src/md_main.c:2384`
and put it in COMM10 bits 13-15 for MD_ROUND. `*(volatile uint8_t*)
0xFFF148 != 0` is one more bit from the same read, and it moves before
0x3A00 rewrites the page shadows, so it is a frame ahead of the page
words and has no detector latency at all. Round values 5-7 in those three
bits are unused if you would rather encode "cutscene" as a round than
spend another bit. Your call; the byte is the fact.

**What the cutscene pages contain, so the ship batch can be sized rather
than tuned:** both pages are laid on EVERY scene load by the two writers
after the unpacker (0x174E page 10, 0x170A page 11 — LOOP-DECOMPILE 10),
so they are resident long before the byte rises; the cutscene uploads no
tiles. Page 10 (foreground, priority set) is 800 cells of tiles
0x500-0x57E: 728 in palette 20, 72 in palette 21. Page 11 (background) is
800 cells of tiles 0x4C0-0x4DF, a 8x4 pattern repeated, all palette 19.
Palettes 20 and 21 are always identical — one script, two slots in
lockstep.

**The animation, exact (the cycler 0x30B2 ticks once per vint):**

    palette 19     7 steps, hold 1 -> period 7 vints. Colour 0 white
                   (0x7FFF), colours 1-7 the blue ramp 0x4900..0x4F00
                   rotating one place per vint.
    palettes 20/21 6 steps, holds 4,2,2,2,2,4 -> period 16 vints.
                   Colours 1-7 red (0x100F); a yellow head (0x305F ..
                   0x30DF) enters at colour 1 and walks to colour 5, then
                   the ramp restarts all-red.

So the plane cycles seven colours at 60 Hz and the flames re-tint five
colours on a 16-vint cycle. The cycler writes the whole 8-colour line
(four longs, 0x30F8-0x30FE; LOOP-DECOMPILE 42's "six colours" was wrong).
The scripts are rom: 0x1A70E (line 19) and 0x1A78E (lines 20/21), 18-byte
entries of a hold word and eight colours, count word first.

---------------------------------------------------------------------
## 18. 2026-09-12. R60TIGHT=1: r60_push 2,512 -> 1,676 a vint, packet unchanged. LOOP-DECOMPILE 97

Mike asked this thread to go ahead on the 68000 lever. What landed:

  - `R60TIGHT=1` (Makefile; md_main.c `r60_ne_longs`): the palette
    pre-scan and the rowscroll compare as cmpm.l/dbne, the 17-byte belt
    copy as longs. Off by default. Same packet by construction and by
    `R60TIGHTCHECK=1`: 55,000 cross-checked scans, 0 disagreements.
  - MAME, level 1, per vint: r60_push 2,512 -> 1,676; shim 4,559 ->
    3,590; the 68K's frame wait 3,366 -> 4,055.
  - ares-headless coined path: 947 -> 957 flips per 1600 frames. Noise.
    The freed 68000 time is idle, which is what entry 88's caveat
    predicted for this window.
  - `rom/night/r60tight1.32x` (md5 see LOOP-DECOMPILE 102) = your vi59 recipe + the flag,
    built from HEAD 1ed642b (vi62b's line, MDBATCHOFF 24) + this change. Not pushed; your call.

Two things you will want to know:

  1. **`make ship-us` alone is not the night rom.** It leaves GAMEGATE
     off and regenerates fmgate_tab.h without the gate thunks; that rom
     read 603 flips where vi59 reads 947. The recipe that reproduces
     vi59 token-for-token is in LOOP-DECOMPILE 97. If it lives in a
     Makefile target nobody has to reconstruct it from .build_flags.
  2. **The rowscroll compare found no change in 9,000+ vints** of attract
     (through the level-2 demo) and level 1. Either the effect is rarer
     than the every-vint compare assumes, or the mirror the compare reads
     is not where the game's writes land. 180 instructions a vint either
     way; not chased.

The rest of r60_push is the rotor (~430) and the changed-block mask
walk (~330). Both change the packet if done wrong; neither is a copy.

---------------------------------------------------------------------
## 19. 2026-09-12. The invisible platform: your round tables were baked from page 0 only. LOOP-DECOMPILE 98

Mike's r60tight1 shots (083715): the player stands on a ledge drawn as
background. The arcade draws a grey masonry ledge on the FG plane there.

  - The cells are FG sets 82, 87, 88, 89, 90, 91 — 1,226 cells of level
    1's tilemap, the ramps and ledges on pages 1-4. None of the six is
    in `mdr_s_line[0]`, so MDS_REFUSE draws them as backdrop.
  - Cause: `bake_tilecram.py:105` `for c0 in range(64)` — the viewport
    sweep covers one page. Each round's table only knows page 0/5.
  - Every round is under-covered somewhere; the per-round lists are in
    LOOP-DECOMPILE 98 and `tools/scene_sets.py` prints them from the rom
    against the current header in one run. Round 1's BG misses set 1 on
    1,111 cells; round 4's FG misses 109 on 112.
  - Whole-level demand by set count is close to page 0's (19 vs 15 FG
    sets in the worst window of round 0). Whether the COLOURS still pack
    into the lines is for your bake to say once it is fed all five
    pages per plane.

Not touched: the bake, the header, the refuse rule. Rom facts and a
reader only.

---------------------------------------------------------------------
## 20. 2026-09-12. Step 2's residue is zero: tile RAM is static in play. LOOP-DECOMPILE 99

Every tile-RAM writer in the program, read to its caller: scene load,
round clear, the attract intro steps, boot/service. **Nothing writes
tile RAM during play**; the only in-play accessor is the ground test,
which reads. So `build_maps_chunk`'s input is constant between events,
and an MD name-table image per page per scene is bakeable from the rom
plus your set->line table. The events that change pages are each rom
data indexed by one WRAM byte (0xFFF142, 0xFFF031, 0xFFF14A, 0xFFF148),
all readable from the shim. Details and addresses in the entry.

---------------------------------------------------------------------
## 21. 2026-09-12. The maps scan's static half, baked: `sh_src/setcols_md.h`. LOOP-DECOMPILE 101

`bm_scan_rows` answers "which sets, at which cat bits, are in the
viewport" from 2,464 cells per plane per generation. Tile RAM is static
in play (99), so `tools/bake_setcols.py` answers it from the rom: per
scene, page, column -> (set|cat<<7, first row, last row). 9-17 KB a scene,
exact against your window formula on 4,000 random windows including
both wraps and any quadrant assignment. Drop-in shape:

    for each of the 44 columns: pg = pq[qy + (cx>>6 & 1)]
      for e in setcol_ent[scene][setcol_idx[scene][pg][cx&63] ..
                                 setcol_idx[scene][pg][(cx&63)+1]):
        if e.first <= hi_row(qy) && e.last >= lo_row(qy): present(e)

col_lvl/amb_col follow from the cat bit as in your loop. `tcount` is
only tested against zero downstream, so presence is enough. Not wired
in; the header is emitted and the tool regenerates it. The tail is
untouched and the scan/tail split of the 0.44 v/gen is yours to
measure before counting the saving.

## 22. 2026-09-12 (builder -> decompile). Fold 4 built as far as the game's bytes allow; one variable would finish it. LOOP29 232-233

Measured first (LOOP29 232): ares wall vi70 1.48 v/gen (18% single-vint),
vi75 1.11 (44%); on the FPGA the presented rate is ~15 and ~20 fps
(`BOOTFLIPRATE=1`, FS bank changes per 64 vints, read off the rig
unattended). Ares ranks, the rig measures; the bar is 3x away on hardware.

Fold 4 is in as `MDSTATE=1` (vi87, LOOP29 233): IRQ4's top posts COMM14 =
E | seq | cut | round every vint; the SH-2 takes the round from it and
forces the round off screen while `cut` is set. What it could NOT do is
delete the claim-mix flag, because of this dump (ares, attract):

    title 100-300 / 3000-3300     0xFFF142 = 0   0xFFF148 = 0
    face 1500-1580                          0              1
    eye 1700-1900                           0              0
    level-2 demo 3600+                      1              0

Your note 17 says the object that runs "the face/eye/intro" sets 0xFFF148;
in the attract the eye reads 0. And the title reads exactly like level 1
while it draws logo sets 37-46 and texture set 11 over level-2's cave --
so "round 0, not cut" cannot mean "level 1's table may refuse", and the
claim mix stays as the ON decision.

**Asked:** is there one byte (or a page-word signature) that says "the
level's tilemap is what is on screen" -- something the attract's title
and the eye set and the level does not, or vice versa? The attract-mode
phase, the object slot running the title, the scr1/scr2 shadow values
(0xFFF0F4/F6) at the title and at the eye. With that, the claim-mix flag
and its timing dependence go, which is the rest of fold 4.

Also void, for the record: 231's "FS never changed mid-consume" read the
DREQ destination register (0xA1510A); FS lives in 0xA1518A. The transport
proof stands on VRAM-equals-source and the SH-2 read-backs.

---------------------------------------------------------------------
## 23. 2026-09-12 (decompile -> builder). The byte you asked for is 0xFFF031, with 0xFFF026 bit 0; the page words cannot do it. LOOP-DECOMPILE 103

**The signal.** "The level's tilemap is on screen" is

    credited play:   0xFFF026 bit 0 = 1  (set at game start 0x1E62,
                     cleared at 0x2CEE/0x2D3C) AND 0xFFF148 = 0
    attract:         0xFFF031 & 0x1C in { 0x04, 0x0C, 0x14 }

0xFFF031 bits 2-4 are the attract step; the dispatcher at 0x1ECA jumps
through the table at 0x26DC:

    step  f031   routine   what is on screen (LOOP-DECOMPILE 75, snapped)
     0    0x00   0x1EF2    the high-score table   (upload 0x2580)
     1    0x04   0x1ED4    DEMO: the level's tilemap, player standing
     2    0x08   0x1F80    the intro pictures      (uploads 0x2564/0x2572)
     3    0x0C   0x1ED4    DEMO with the LOGO over it
     4    0x10   0x20A0    the EYE                 (upload 0x2552)
     5    0x14   0x1ED4    DEMO again
     6,7  0x18/1C 0x2282   (10-byte tail: restarts the cycle)

Carry the three bits in the state word next to round and cut. Then:
steps 1 and 5 are the round's table exactly; step 3 is the round's
table PLUS the logo's sets (37-46 and texture 11 in your dump) -- an
allowed-extra list keyed on step 3, not a detector; steps 0, 2 and 4
are picture screens whose sets belong to no round, and refuse nothing.

**Why the page words cannot be the signature.** The eye and intro
objects select pages through their own copies of the level's page
tables -- 0x2714/0x2724 are word-for-word 0x40F0/0x4100 (the object
routine at 0x2384-0x2470 writes 0xFFF0F4/F6 from them by hscroll). So
0xFFF0F4/F6 read 0x0000/0x5555 at the eye exactly as in level 1: the
eye's picture is UPLOADED INTO the level pages (0x2552's eight blocks,
all inside pages 0-7) and displayed through the same selects. The page
words say which pages; the step says what was last written into them.

**Correction to note 17.** 0xFFF148 is set by exactly one object, the
transformation (the face, constructor 0x90F4, LOOP-DECOMPILE 94). The
eye is attract step 4 and the intro is step 2; neither touches
0xFFF148, which is what your dump shows. LOOP29 208's "all three
switch pages" holds because all three show level pages through the
same tables; only the face switches to 10/11.

**Two more bytes worth carrying, both already read by the game:**
0xFFF018 bit 5 is the display-enable the game mirrors to the I/O port
(75 gated its dumps on it; loads are blanked); 0xFFF142 stays at the
LOADED round through the eye and the title (75's trap), so in the
attract "round" alone never says what is on screen -- the step does.

Not established: the exact writer that draws the logo during step 3
(the tiles are sets 37-46; the writer is one of the attract-step
uploads or the 0xC30 framed picture, not read this session), and the
round-clear rewrite of pages 0/5 in credited play (0x1A52C, once,
keyed on the 0x1A406 sequence) -- treat it as a tilemap write, which
it is.

---------------------------------------------------------------------
## 24. 2026-09-12. Fold 3's census: nothing of ours runs in the game's idle; the FM-gate spins are what RELBANK adds to the pass. LOOP-DECOMPILE 104

Every transport piece is in IRQ4 (build, post, consumes, tail blast) or
in the shared gate spin (the late blast). Under RELBANK two measured
things change: 4.9% of the game's pass is inside gated spans, which is
the nopost rise 183 saw (5.6%); and the game enters a gated writer 5
times a vint (three spans, four call sites, level 1), each a wait on
your window that GAMEGATE hid inside the idle and RELBANK adds to the
pass -- 8-120 lines each on hardware (your 88).

So fold 5 comes BEFORE fold 3: route those three writers (0x3A9A-0x3AFC,
0x35CC-0x3950, 0x4D80-0x4D98) through the WRAM mirror and the pass has
no waits in it; then RELBANK, on the rig, with BOOTGATECHK=1 painting
the post's verdict. TXTWRAM's old failure was measured under GAMEGATE,
where it could not show a gain.

Trap for your traces too: MAME prints .w WRAM targets as 8 hex digits;
a 6-digit match drops every thunk. `arcade_trace.py` / `round_profile.py`
need `{6,8}`.

## 24. 2026-09-12 (builder -> decompile). Note 23 wired; two readings that do not match it. LOOP29 234

The state word now carries play (0xFFF026 bit 0) and the step (0xFFF031
bits 2-4). Decoded from the 68K's own posts across the attract (ares,
vi88, trace on COMM14):

    frame   23-1106   play=1 step=1     (the SEGA / blue-wave screen, then the demo)
    frame 1106-2248   play=1 step=3     (demo with logo, the face at 2130-2240 with cut=1)
    frame 2248-2829   play=1 step=4     (eye)
    frame 2829+       play=1 step=5

So (a) 0xFFF026 bit 0 is 1 throughout the attract, from frame 23 -- the
demo is the game started with scripted input, so "credited play" needs a
different discriminator (a credit count? the input source?); (b) step 1
at boot covers the SEGA logo / blue wave screen for ~600 frames before
the level's tilemap is on screen. "play or step 1/3/5 = on" refused
everything on those screens (0.96 black at 300 and 1000).

Wired as of vi89: the word decides OFF where it is certain (cut; steps
0, 2, 4) and leaves ON to the claim mix. To retire the claim mix I need
the byte that separates credited play from the demo, and what step 1
reads while the SEGA screen is up (a sub-step? the 0x1ED4 routine's own
phase?).

## 25. 2026-09-12 (builder -> decompile). Fold 5: what the third writer needs from you. LOOP29 236

Taking your order (fold 5 before fold 3). `TXTWRAM=1` already mirrors
two of your three: the credit line (0x3AAE through the loop heads
0x3A9A/0x3AA4) and the health bar (0x4D54, span 0x4D80-0x4D98 dropped).
It never covered 0x35CC-0x3950 -- entered at 0x369C (`moveal #text,%a0`,
your 17-caller alt entry). To route it through the mirror the shim has
to copy exactly the footprint it wrote, at FM=0 before the raise, so I
need for that routine:

  - the entry points that write TEXT (0x369C only? 0x36B0/0x36C4 are the
    tile/sprite-ram variants and stay gated);
  - where the destination offset and the length come from (registers or
    the a5 record at 0x3706 `lea 8(%a5),%a0`), so a mark thunk can record
    (offset, words) per call the way the credit line records 0xFFF024;
  - which callers fire per vint in level 1 (you counted five gate spins
    a vint over four sites): the score, the timer, the orbs?

Measured meanwhile (ares, play frames 900-999): the writes into FB text
staging by pc and offset, so your list can be checked against what the
port actually sees. Also: tw75 (vi75 + TXTWRAM) is built and going
through the gates; its wall against vi75's 1.11 is the first number.

## 26. 2026-09-12 (builder -> decompile). TXTWRAM as it stands halves the rig's frame rate; fold 5 needs a different transport. LOOP29 237

Measured with the rig's frame-rate probe (presented frames per 64
vints, attract demos):

    fr75    (vi75)              21 19  7 22 16
    frtw75  (vi75 + TXTWRAM)     9  1 20 11  9

Ares reads the same pair 1.11 -> 1.16 v/gen (the shim's copies, 6
lines a vint). On hardware the copies -- each dirty footprint written
into FB text staging at FM=0 BEFORE the raise, at 0.05 lines a word --
push the post late enough to lose windows. So the mirror is right and
the copy is wrong: it has to ride the packet/DREQ side (the SH-2 reads
the mirror's footprint from the FB dead block the way it reads
everything else) or land after the post. Your expected gain (the five
gate spins, 8-120 lines each) is real only net of that. I will shape
the copy before measuring RELBANK; note 25's questions stand.

---------------------------------------------------------------------
## 27. 2026-09-12 (decompile -> builder). Notes 24 and 25 answered: bit 0 was inverted in note 23; the SEGA card is a step, not a sub-phase; the third writer's footprints. LOOP-DECOMPILE 105

**Credited play versus the demo: 0xFFF026 bit 0, and note 23 had it
BACKWARDS.** The input routine at 0x1366 is the definition:

    13C0  btst #0,$FFF026 ; beq 13F8      bit CLEAR -> read the joysticks
    13C8  ... table 0x1834[step] ...      bit SET   -> the tape: record if
    13E4  tst.b $FFF15E                     0xFFF15E, else PLAY BACK
    13F2  move.b (a0)+,d0 ; d1 ; d5         three bytes a frame

So bit 0 = 1 is ATTRACT (tape inputs, which is why your dump reads 1
from frame 23 on) and bit 0 = 0 is a credited game (the start press
clears it at 0x2CEE/0x2D3C; the demo start sets it at 0x1E62). Your
"play" bit is the demo bit; invert its meaning and it is exact.

**The SEGA card is attract step 2, not a phase of step 1.** Arcade,
MAME, no input, bytes read every 150 frames with snapshots:

    frame    f031  step   f028/f029   f148   on screen
    60-300   0x08   2       00 / 00     0     ALTERED BEAST / SEGA card
    450-1000 0x0C   3       01 / 01     0     demo with the logo (f1150: face, f148=1)
    1300     0x10   4       01 / 01     0     the eye
    1500-2000 0x14  5       01 / 01     0     demo
    2300-2600 0x00  0       01 / 21     0     high-score table
    3000     0x04   1       01 / 01     0     demo, round 1 (f142=1, f14e=1)

What separates a demo from a card is **0xFFF028 / 0xFFF029 bit 0, the
player objects active**: 0 on the SEGA card, 1 in every demo. If your
port reads step 1 under the SEGA card (the 68K boot path sets
0xFFF031 = 4 at 0x1E4E before the dispatcher has run), bit 0 of
0xFFF028|0xFFF029 still reads 0 there. So the rule with no claim mix:

    on  <=>  0xFFF148 == 0
             AND ( 0xFFF026 bit 0 == 0                       -- credited game
                   OR ( (0xFFF028 | 0xFFF029) & 1             -- a demo running
                        AND step in {1, 3, 5} ) )            -- not the cards
    (step 0, the high-score table, has f028 = 1 and needs the step test)

**Note 25, the 0x35CC-0x3950 span.** Its entries, read:

    0x369C   CLEAR all of text RAM (1024 longs at 0x410000). 17 callers,
             scene/screen changes. Mirror: clear the mirror, mark all.
    0x36B0   clear tile RAM; 0x36C4 clear sprite RAM + the order list --
             not text, leave them gated.
    0x3716   the per-frame HUD entry (gameplay loop 0xA5A, 0.5 a vint in
             level 1): for each player with 0xFFF028/029 bit 0, 0x374A:
             add the frame's points (abcd at 0x3778) and, if changed,
             0x37D0: EIGHT text words at a2 = the player record's score
             pointer ([0xFFE008] / [0xFFE088], the field at +8), digits
             as low bytes. If the score passed the high score (0xFFF010),
             0x37D0 again with a2 = 0x4100D2 (8 words).
    0x380A   attract only (bit 0 set): a 12 x 6 block of consecutive
             codes from 0xAD30 at 0x41024C, stride 128. The card's art.
    0x3858   credited only: per player, a0 = [record+8] + 128:
             0x38AA writes 7 words of 0xA000 at a0 and a0+126 (two rows),
             then 0x38C0-0x394E the beast/lives icons: 2+2 words from the
             table 0x4024, four fixed codes 0xAC7C-0xAC7F, and up to 3
             digit words via 0x392A -- all at a0.. and a0+124.. (two rows,
             at most 8 words each).
    0x3838   the per-scene palette block, not text (covered by PAL32).

So the footprints a mark thunk has to record: (a2, 8) at 0x37D0's two
call sites; (a0, 7) x 2 rows at 0x38AA; (a0, <=8) x 2 rows for
0x38C0-0x394E; the 0x369C clear; and 0x380A's block in attract. Per
vint in credited level 1 that is at most 16-24 words from 0x37D0 plus
the icon rows when lives change.

**Note 26, taken.** The gate spins' gain (five a vint, 8-120 lines each
on hardware) is net of whatever the mirror copy costs; you have the
right shape -- the copy rides the packet side, the master reads the
mirror's footprint from the dead block like everything else.

---------------------------------------------------------------------
## 28. 2026-09-12 (decompile -> builder). vi90's black level is note 23's inverted bit; note 27 is the discriminator 238 waits for

LOOP29 238 has it right: a game coined during a picture step keeps that
step in 0xFFF031 for the whole credited game (nothing advances the
dispatcher once the tape is off), so "step 0/2/4 = OFF" held the round
off screen in play and every set went dynamic -- Mike's black temple,
statues and trees with the sky and grass surviving (215851/220012/
220031). The root is mine: note 23 read 0xFFF026 bit 0 as "credited"
and it is "attract" (note 27, the input routine at 0x13C0).

So the discriminator 238 says does not exist yet, exists:

    credited game  <=>  0xFFF026 bit 0 == 0
    in a credited game 0xFFF031 is stale and must not be consulted
    in the attract (bit 0 == 1) the step and 0xFFF028/029 bit 0 decide

    on = !cut && ( !attract_bit || ( (f028|f029)&1 && step in {1,3,5} ) )

The state word already carries bit 0 as "play"; read it inverted (or
post it inverted -- one `^ 1` in md_main.c's OR) and the picture-step
OFF rule is safe again, because it can only fire in the attract.

---------------------------------------------------------------------
## 29. 2026-09-12 (decompile -> builder). 239's shim-owned credited flag is not needed: the game's bit is exact, measured on the arcade through a coin and a start

239 built a shim flag from the coin/start inputs and suspects an idle
joypad read sets it on the rig. The game already keeps the flag, and
it is the byte 239 still reads the other way round. MAME arcade, coin
at 600/800, start at 1000, no other input:

    frame   f026   f028  f029   f031   credits   meaning
     500    01     01    01     0x0C     3       attract demo (tape)
     900    01     01    01     0x08     5       SEGA card, coins in
    1010    80     01    00     0x08     3       start pressed: bit 7 = loading
    1100    00     01    00     0x08     3       credited game, player 1 only
    2400    00     01    00     0x08     3       ... for the whole game

**0xFFF026 bit 0 is 0 throughout a credited game and 1 throughout the
attract.** The start handler clears it (0x2CEE one player, 0x2D3C two,
right after deducting the credits at 0xFFF000); the demo start sets it
(0x1E62); the input routine reads the joysticks only when it is clear
(0x13C0). Nothing else writes it. So:

    credited  <=>  0xFFF026 bit 0 == 0      (no joypad, no timing, no rig hazard)

and the step byte reads 2 for the whole credited game here as in your
ares decode -- stale, never to be consulted while credited. The
attract's own demos read f028 = f029 = 1; a one-player credited game
reads f028 = 1, f029 = 0.

Post the bit as it is and read it inverted, or post `~f026 & 1`. Your
md_state_on then has every case from bytes the game maintains:

    cut                          -> OFF
    credited (bit 0 clear)       -> claim mix, or ON if you trust the table
    attract, step 0/2/4          -> OFF   (safe again: cannot fire in play)
    attract, step 1/3/5 with (f028|f029)&1  -> ON
    attract, step 1 with f028 = f029 = 0    -> the SEGA card: OFF

Note 28's "stale from the coin" was imprecise: the byte moves at game
start (0x1E4E writes 4, then the boot path leaves it at 2) and then
holds. Same conclusion.

---------------------------------------------------------------------
## 30. 2026-09-12 (decompile -> builder). vi94 is "incredibly slow" on the rig: the three changes since vi75, and the A/B that names the one

Mike on vi94: incredibly slow. Between vi75 (43ef418) and vi94 (2851244)
exactly three things are compiled into the rom (the TILE_VERIFY,
BOOTTILEVER and BOOTFLIPRATE code is flag-only and absent from vi94 --
checked in the binaries). Speed on the rig has NOT been measured on any
build since vi75: vi91-vi94's rig numbers are black shares. So the
slowness is unattributed, and the way back is one probe run per change:

    knob                    what it is                        how to A/B
    MDSTATE=1               the state word on COMM14, round   build vi94's
                            from it, cut -> OFF, steps 0/2/4  line without
                            -> OFF, 3/5 -> ON, logo exempt    MDSTATE
    236 C1_SOFT = 0         under C1_NOFB a cat-1 tile now    needs a source
    (unconditional under    EVICTS instead of leaving the     guard: restore
     C1_NOFB)               slot blank                        the vi75 define
    239 shim_credited       coin/start sets, the falling edge  covered by the
                            of 0xFFF026 bit 0 clears           MDSTATE A/B

Measure each with `BOOTFLIPRATE=1` on the rig (presented frames per 64
vints, unattended in the attract: fr75 read 21 19 7 22 16). Mike's
"slow" is in credited play, so one run of each with a start press is
the number that matters; the attract run is the free first cut.

Where I would look first, from the code alone (no measurement, so a
ranking only):

  1. **236 on a rig that ships 7 tiles a vint.** Cat-1 tiles evicting
     hot ways is more tile traffic per window on exactly the machine
     whose ship rate is a third of ares' (your 231/237). Every eviction
     is a re-ship, every re-ship is a window's worth of conversion, and
     fewer windows per vint IS the rig's frame rate. Ares cannot show
     it: its consumer is two orders faster (172).
  2. **The credited flag's falling edge.** The game clears 0xFFF026
     bit 0 at the start press; your clear fires on that edge one vint
     after the set. A held button re-sets it; a short one does not, and
     then credited play reads step 2 -> OFF -> the level refused: vi90's
     shape, and vi90 was also "slow everything". Note 29's byte has no
     edge: credited <=> 0xFFF026 bit 0 == 0.
  3. **COMM14 traffic** is a word per vint and one suppressed answer;
     no reader of the 0xB1xx answer is left on the 68K side but the boot
     hold, which now accepts E. Least likely.

The recovery line, if the A/B says what I expect: vi75's flags + MDSTATE
with (a) the credited bit posted as `~0xFFF026 & 1` (note 29) and
(b) C1_SOFT restored to vi75's rule under C1_NOFB, the slot-pressure
blanks handled by the re-ship knob you already named (237's rate) rather
than by eviction. That keeps the two parts of fold 4 that were free --
the round out of the dirty mask, the transformation forced off -- and
drops the two that cost.

And the plan: fold 2 (the maps scan from `setcols_md.h`, NOTES 21) is
the wall lever and has not been touched since it was handed over. The
rig's black tiles are a rate knob by your own measurement (237); they
do not move the wall. When vi94's slowness is attributed, fold 2 is the
next build that changes a vint number.

---------------------------------------------------------------------
## 31. 2026-09-13. Fold 1's hole punch as a lookup: `sh_src/cat1hole.bin`, two bits a cell. LOOP-DECOMPILE 106

Entry 65 said the suppress wants two bits per cell, not one; here they
are, baked from the rom like cat1map (`tools/bake_cat1hole.py`):

    0 no hole (not cat-1, or cat-1 on a blank tile)
    1 suppress the whole cell (the cat-1 tile is fully opaque)
    2 consult the art (transparent pixels in the tile)

    scene   cat-1 cells   blank   suppress-all   per-pixel
      0        2,312       163       1,439          710   (31%)
      1        8,960     6,108       1,303        1,549
      2        7,360     4,128       1,923        1,309
      3        1,280        32         800          448
      4        3,520         9       1,148        2,363

Every cat-1 cell in all five rounds sits on a FOREGROUND page (0-4);
the background pages carry none, so entry 59's "all cat-1 is FG cat-1"
holds for the whole game and the sprite test is: FG cell under this
sprite pixel -> CAT1HOLE_GET(map, cell) -> 0 draw, 1 skip, 2 test the
tile's pixel. `cat1hole.h` has the accessor and the per-scene counts.
Geometry is cat1map's; the art index convention is bake_cat1vis's
(tiles.bin by the 13-bit index).
The .bin is gitignored like cat1map.bin: run `tools/bake_cat1hole.py`
once (needs sh_src/tiles.bin) and it lands beside the header.

## 27. 2026-09-13 (builder -> decompile). Fold 2 wired and measured; fold 4 settled on your bytes. LOOP29 240-242

Your setcols_md.h is in as `SETCOLS=1`, eligible when the state word
says the level's tilemap is on screen (credited, or a demo step with
f028|f029) and every page select is < 10; the live scan otherwise.
Ares, play2, 4,000 frames:

    pc95 (census only)   wall 1.16  single-vint 42%   scan 0.279  tail 0.135
    sc95 (+SETCOLS)      wall 1.03  single-vint 55%   scan 0.113  tail 0.113

The scan's 0.279 is what you priced ("up to 0.29"); the wall moved
0.13 of it, the rest was overlapped by the slave. mtask 1.04 -> 0.78.
Check mode (live vs baked, set by set) is being read now. The rig's
frame rate for sc95 needs the rig.

Fold 4, final shape (vi95): credited = f026.0 clear, step from f031,
demo bit f028|f029 -- exactly note 29; vi94's slowness was the shim's
own credited edge (note 30's second suspect), vi95 measures fr75's
frame rate with the state word in (fr95e 19 15 22 21 14).

Fold 5 next needs the copy on the packet side (note 26); note 25's
questions on the 0x369C routine stand. The hole punch for fold 1 is
built as `C1PUNCH=1` (a 40x28 cat-1 cell mask from the name-table
pass; sprite pixels of pp < 3 skip those cells) and is unmeasured.

## 28. 2026-09-13 (builder -> decompile). The shared unpacker is off by one on zero runs; the fold-2 header was column-shifted. LOOP29 243

Check mode (live scan vs baked, set by set, in ares) disagreed on 6.4
sets a plane in steady play while the two formulas agreed offline on
the ROM unpack. The live tile RAM dump (TILEMAP_C at play f2000) against
the unpack: 10,550 of 20,480 words differ, in runs shifted by one
column. The low-byte pass's zero escape is `n + 1` zeros, like the
high-byte runs, not `n if n else 1`:

    zero run = n (n if n else 1)    10,550 words differ from live
    zero run = n + 1                     0

Fixed in tools/bake_setcols.py and tools/scene_sets.py (your LOOP-
DECOMPILE 10 format note and any tool that copied it carry the same
line; bake_cat1map.py's own decoder matched live byte for byte, so it
already has it right). The header is regenerated and re-verified
against the LIVE dump; check mode re-run. Your "4,000 random windows,
0 mismatches" was both sides sharing the bug.

Addendum to 28: with the corrected header, check mode reads 0/0 over
3,046 planes of steady play (was 6.43/1.13). Fold 2 exact; wall ~1.05.

---------------------------------------------------------------------
## 32. 2026-09-13. Why the baked scan may still read 0.107: its table lives in the cartridge. For the look after the punch card, not a card

Build B's census: scan 0.291 -> 0.107 v/gen. The lookup is ~600
extent entries per plane, which should be well under that. Two things
in the tree say where the rest goes:

  1. `sh_src/mars.ld` puts `.rodata` inside `.text` at 0x02000000 -- the
     cart window. `setcol_idx`/`setcol_ent` are `static const`, so
     every entry the scan reads is a cart access, three byte loads an
     entry, on the bus the 68000 fetches the game from (the game runs
     from the cart, rebased; the shim moved its own hot code to WRAM for
     exactly this contention, md_main.c "RAMCODE"). The old cell walk
     read TILEMAP_C from SDRAM.
  2. The scan runs once per plane and alt set (checked: `nrows = 0`,
     `BM->row = 0xFF` after one call), so it is not redundant work.

So the number to test is placement, not the algorithm: copy the
current scene's table (9-17 KB, NOTES 21) into SDRAM at `mds_install`
and point the scan at the copy, and pack an entry as one u32 (set|cat,
first, last) so it is one load. Expected: the scan toward the
arithmetic (tens of microseconds), and one less SH-2 reader on the cart
bus during the 68000's pass. If SDRAM is too tight for 17 KB, the
scene's FG pages alone (the level's five) are what the scan touches in
play; the BG pages could stay in the cart at half the cost.

## 29b. 2026-09-13 (builder -> decompile). The remaining scan is ROM reads, not arithmetic. LOOP29 246

Your "0.107 should be nearer zero": in steady play the baked scan runs
exactly 2.00 chunks a generation (one per plane) at 573 FRT ticks a
chunk. ~500 extent compares a plane cannot cost that on an SH-2; the
tables (setcol_idx/setcol_ent) are const in cart ROM and every column
costs two index loads plus three bytes an entry over the cart bus the
68K shares. The cure is the round's ~17 KB of tables copied to SDRAM at
mds_install; SDRAM under the region guard is ~14.7 KB, so it needs a
home. If you know a static block that size that is free after boot
(the old tile-cache half became .ramtext), say so.

---------------------------------------------------------------------
## 33. 2026-09-13 (decompile -> builder). 29b answered: two homes for the scan's table, and a cheaper option that needs ~1 KB

**Sizes first**, from the header (3 bytes an entry, 1,300 B of index):

    scene   FG pages 0-4   BG pages 5-9   whole + idx
      0        6,303          8,133         15,736 B
      1        2,568          4,911          8,779
      2        2,742          3,834          7,876
      3        3,237          5,760         10,297
      4        3,369          5,466         10,135

The scan reads both planes every generation, so it is the whole table
that wants to be near. Under the region guard you have 14.7 KB: scenes
1-4 fit whole; scene 0 does not, by 1 KB. Two ways round it:

  1. **Page 12's truth slot is dead.** TILEMAP_U holds 13 pages
     (0x19000-0x26000); page 12 is the blank page the game never writes
     and the truth machinery skips it as a whole (LOOP29 138). Its 4 KB
     at 0x25000-0x26000 is a static block that is free after boot. Not
     contiguous with the guard region, but 4 KB is enough for scene 0's
     index plus 900 entries -- or put the index (1.3 KB) there for
     every scene and the entries under the guard, and scene 0 fits.
  2. **Memoize by scroll cell instead of copying.** The scan's inputs
     per plane are (pq[4], tx, the two row ranges); they change only
     when the scroll crosses a cell, every 4-8 frames at play speed.
     Keep the last inputs and the plane's 128-entry result (tcount as
     presence, col_lvl, amb_col: ~400 B a plane, .bss) and reuse it
     while the inputs match; merge the two planes into the live state
     as the tail expects. The scan then costs its 573 ticks on one
     generation in several and a 128-entry copy on the rest, from the
     cart or not. Check mode already proves exactness for free (the
     memo either equals the fresh scan or it does not). This is the
     one I would build: no home needed, and it removes the cart reads
     from most generations rather than making them faster.

Either way the read that stays -- the art rows for class-2 cells in
the punch -- is also cart-resident (altbeast_tiles), and the same
memo shape applies there if it ever shows in a census.

---------------------------------------------------------------------
## 34. 2026-09-13 (decompile -> builder). Build C's failure read from the code: the 0.13 is probably the class-2 ART ROWS read from the cart inside the slave's punched loop. Sizes for the fix, if your stamps agree

Your stamp pair is the right next step; this is the prediction to test
it against, from the code you shipped in c1p95c:

  - 244c's baked-run path, for a class-2 cell, "walks the art (one ROM
    row per cell)": `altbeast_tiles` is linked in `.text` at 0x02125AF0,
    the cart window. A 32-pixel-tall sprite crossing 4-5 cells a row is
    ~150 cell-rows; at level 1's class-2 share that is ~50 art rows of
    8 cart bytes per sprite per generation, times the live sprites --
    thousands of cart byte reads on the slave's hot loop, on the bus the
    68000 fetches the game from. Same cost class as the scan's 573
    ticks (29b), and it lives in the slave phase, which is where your
    1.11 -> 1.14 and vi95 -> c1p95c's 1.16 -> 1.29 both sit.
  - the master's FG name-table pass reads the hole class from
    `cat1hole` (`.incbin`, cart) for 1,120 screen cells a generation.
    Rows are contiguous so this one caches well; smaller.

**If the slave's loop holds it, the fix is data placement, not code,
and it is small.** The slave does not need the tile: it needs one BIT
per pixel, "opaque or not", for the class-2 tiles the scene actually
uses. Distinct class-2 tiles per scene (corrected unpacker, FG pages):

    scene   class-2 cells   distinct tiles   8-byte masks   hole map/scene
      0          543              81            648 B          5,120 B
      1        1,659             139          1,112
      2        1,432             192          1,536
      3          448              14            112
      4        2,294             130          1,040

So at `mds_install` copy the scene's masks (<= 1.5 KB) and, if the
master's reads matter, its 5,120-byte hole map into SDRAM; the NT pass
then writes a mask INDEX per class-2 screen cell instead of the tile
code, and the punched loop reads one SDRAM byte per row instead of
eight cart bytes. Both fit under the guard with room (14.7 KB). The
RAM-code budget is untouched: the loop gets shorter, not longer.

**Correction to note 31's table.** Those counts came from my bake
before 243's unpacker fix; the header in the tree was regenerated by
the Makefile with the fixed tool (verified byte-identical, LOOP-
DECOMPILE 107). Corrected FG counts: scene 0 blank 170 / whole-cell
1,599 / per-pixel 543; 1: 5,984 / 1,317 / 1,659; 2: 4,272 / 1,656 /
1,432; 3: 32 / 800 / 448; 4: 16 / 1,210 / 2,294. All cat-1 is still FG.

---------------------------------------------------------------------
## 34b. 2026-09-13. Note 34's prediction is FALSIFIED by Build D (LOOP29 248)

Build D put the class-2 opacity masks in SDRAM exactly as note 34
sized them and read 1.22 against bldB's 1.18: no gain. So the punch's
0.13 is not the cart art rows. 247's subtraction puts it in the slave's
punched loop itself, 248/249 narrow it to the loop's shape and the
UNCACHED reads of the cell mask (cat1scr/cat1code through the
0x20000000 alias, per cell), which Build F tests by reading them
through the cache. Note 34 stays as written, with this on top; its
sizes are still right if masks are ever wanted for another reason.

## 35. 2026-09-13 (builder -> decompile). The punch-price series is closed: the price is the walk executing, and four shaves could not move it. LOOP29 247-251

**Ask:** the next card. Note 34 asked for the stamp pair and said
"either way it is one build, one card"; the stamps say neither of
note 34's two candidates holds the 0.13, so the card it sized is not
the card to cut. Here is what was measured, then two options and a
recommendation.

**What the stamps read (slave compose sum, clear+sprites, v/gen, and
the ares wall on the line's flags, 4000 frames):**

    line bldB                       0.389   wall 1.18   40% single-vint
    C1NOMASK (master mask off)      0.383
    C1NOPLOT (slave punch off)      0.324   wall 1.03
    no punch at all                 0.327
    C1RTOFF (punch code present,    0.337   wall 1.06
             never executed)
    Build D, SDRAM mask table       0.422   wall 1.22   FAIL (worse)
    Build E, per-run pre-scan       0.388   wall 1.17   no gain
    Build F, cached class reads     0.394   wall 1.21   no gain
    Build G, E+F                    0.388   wall 1.16   no gain

So: the master's name-table pass is not the price (nomask 0.383). The
art rows are not the price (D). The uncached class reads are not the
price (F). The loop's codegen with the punch present is 0.013 (RTOFF
vs noplot). The punched walk RUNNING is 0.052 compose, 0.12 on the
wall, and it runs on 67% of the pp<3 runs (G's counters: those are
the runs that cross at least one class-1/2 cell; the other 33% take
the tight copy under E and that bought nothing). SPRBK says 99% of
records take the baked-run path, so the 1:1/zoomed per-pixel paths
are not where it lives either. 0.052 v/gen is ~20k SH-2 cycles a
generation spread over every run that crosses a hole cell: tens of
cycles a run, no hot instruction.

**Two designs that could still cut it, neither a flag on bldB:**

1. Skip whole rows. The master's pass knows which of the 28 cell rows
   hold any class-1/2 cell; a 28-byte row summary lets the slave
   skip the class read entirely on rows without holes. E's per-run
   pre-scan is the same idea at run granularity and it paid for the
   scan itself; a per-row byte is one load per sprite row instead of
   one per cell. Expected: some fraction of the 0.052, bounded by how
   many sprite rows fall on hole-free cell rows in level 1 (the
   ground band is where the holes are and where the sprites walk, so
   I would not promise more than a third).
2. Repaint instead of punch. Compose sprites unpunched, then paint
   the class-1/2 cells' FG art over the composed band (opaque pixels
   only). ~120 cat-1 cells on screen, 64 pixel tests each, once per
   generation, independent of sprite count. Needs the tile's pens in
   the FB palette (the sprite lines 64-127 are what the FB carries
   today; the tile lines 0-63 are the MD's), so it is a fold-1
   renderer change with a palette question attached, not a card.

**Recommendation:** bank the punch at its price and cut fold 5 next.
The 0.12 is a fifth of the gap to 1.0 and the four shaves show it is
not a cheap fifth; fold 5 is on the plan's critical path and has an
open transport question (notes 25/26: the 0x369C writer's footprint;
the copy must ride r60_blast, TXTWRAM as written halved the rig frame
rate). Option 1 is the only punch card I would cut later, and only
with a per-row hole census from the master's pass first so its ceiling
is a number before it is a build.

**The line is unchanged:** rom/night/bldB.32x, md5 493d4984, on the
rig, = rom/s16.32x. Every probe rom above (pcB, pcB_rtoff, bldC-G)
stays in rom/night/ for re-measurement.

**A trap for anyone freeing RAM code (LOOP29 250b):** a static
function given a ROM placement is still inlined into its RAMCODE
caller by LTO; it has to be `noinline` to leave .ramtext. The C/D
card text that said bm_scan_baked was fetched from ROM described the
intent; the binary did not do it until 728c5ff.

---------------------------------------------------------------------
## 36. 2026-09-13 (decompile -> builder). The pick: the STAMP -- compose unpunched, stamp through-pixels on the hole cells, draw pp=3 sprites after. Then the scan memo. Then fold 5.

**Why not bank it.** The line is 1.18 and the threshold is 1.00, and
nothing is paid until the wall is under it (168). After the punch's
0.12 the known remaining cuts are the scan's cart reads (0.095, note
33's memo) and nothing else the censuses have found; 1.18 - 0.12 -
0.08 is the threshold. Fold 5 and RELBANK are the 68000 side and pay
only once the wall has crossed. So the 0.12 is not "a fifth of the
gap", it is half of what is left that anyone knows how to cut.

**Why not option 1.** Build E was the per-run form of the row skip and
bought nothing; the holes are in the ground band and so are the
sprites' feet. A per-row byte saves the class read on rows that never
cost anything.

**Option 2, in the form that has no palette question.** The punch
exists to make a sprite pixel TRANSPARENT where a foreground cat-1
pixel is opaque, so the MD's plane A shows (175: cram[0] = 0x8000,
an unwritten pixel shows the MD). The repaint does not need the tile's
colours at all -- it needs to write that transparent value where the
cat-1 pixel is opaque, over whatever the sprites left. So:

    1. compose every sprite with the ORIGINAL tight loops, no class
       test anywhere (the RAM-code slot gets vi95's bytes back);
    2. for each hole cell on screen (the master's cat1scr, ~270 in
       level 1's strip: class 1 = fill the cell with 0, class 2 = write
       0 where the mask bit is set, from Build D's SDRAM mask table,
       which exists behind C1MASKTAB and was exact);
    3. then draw the pp = 3 sprites, unpunched, on top.

Cost, bounded rather than spread: step 2 is at most ~270 cells x 64
pixels = 17 KB of FB writes a generation, a quarter of a screen pass,
~7 lines, ~0.03 v/gen -- and it is the SAME every generation, no
per-run branch in the sprite loops, which is where G's counters put
the 0.052. Restricting the stamp to cells under sprites (mark each
SPRITE's cell rectangle, one store per record, not per run) brings it
to the ~30-100 cells the sprites actually cover, ~0.01. Expected on
the wall: 1.18 -> ~1.06-1.08 by 251's own subtraction (punch off =
1.03, plus the stamp).

**Correctness, and the one case it is not exact.** Measured on the
arcade, MAME, rounds 0-4 forced by the DIP table, 67 samples of the
live sprite records each (word 0 low byte = top, high byte = bottom;
word 4 bits 7-6 = pp):

    round   pp=2 records   pp=3 records
      0        388             5
      1        268             8
      2        336             5
      3        320            23
      4        197             1

So pp = 3 sprites exist in every round (entry 59 sampled one scene and
saw none). Step 3 draws them after the stamp, which is their priority
against cat-1 (1<<3 > 4). The only deviation from the arcade is a
pp = 3 sprite overlapping a pp = 2 sprite INSIDE a hole cell where
list order put the pp = 2 one in front: the arcade shows the tile, the
stamp shows the pp = 3 sprite. Gate: a pixel diff of the stamp build
against bldB (the punch is exact) over the play script; the count of
differing pixels is that case and nothing else, and it should be near
zero. Then Mike's eye: legs through the grass, zombies behind, as
before.

**Then, before fold 5:** the scan memo (note 33, item 2), ~0.08. If
both land the line is at the threshold and fold 5 / RELBANK are what
cross it on the 68000 side.

## 37. 2026-09-13 (builder -> decompile). Card H, the stamp: 1.18 -> 1.14, picture = bldB's minus a defect the arcade never had; note 36 expected 1.06-1.08. LOOP29 252-253

**Ask:** (1) look at the card and say whether 1.14 banks it or whether
you want the per-band cost chased (below); (2) the game's tile bank
request (0xFFF095) per round, from the bytes -- the mask baker needs
it for any scene whose class-2 codes carry bit 0x1000 (level 1 has
none; I set rounds 1-4 to round 0's measured 1). Card I (the scan
memo) is next either way; it waits only on Mike's eye for bldH.

**What was built.** Note 36's shape exactly: sprite loops unpunched,
the hole cells under the drawn records written MD-through after the
pp<3 pass, pp=3 records after that. rom/night/bldH.32x, flags = the
line's + C1STAMP=1, on the rig.

**What it cost, and why six cuts (LOOP29 252).** Slave compose sum,
v/gen; the line 0.389, punch off 0.324:

    H1 every hole cell of every row a sprite touched     0.472
    H2 per-run/pixel cover marks (byte map)              0.437
    H3 + pp=3 pass only when a pp=3 record exists        0.434
    H4 cover = record rectangles, two words a cell row   0.385-0.390
       (H4 with the FB writes removed                    0.375)
    H5 master emits hole bits per cell row; the slave
       reads classes only for hole & cover cells         0.368
    H6 = H5 with the cover per call (both CPUs compose)  0.368

Your 0.03 estimate for step 2 assumed FB writes at SDRAM prices.
They are not: H1's 17 KB of zero-over-zero cost 0.148 -- the 32X FB
write floor, ~0.15 us a byte. Restricting to covered cells was the
obvious fix and the SH-2's write-through byte stores for the cover
marks (H2) cost more than they saved; the record rectangle (H4) has
no per-run work at all. What is left above the floor, 0.044, is
0.015 of FB writes and ~0.03 of per-BAND work: the compose is called
per strip, and each call pays the cover clear, the record scan's
rectangles, and two uncached longwords a cell row. That per-band
cost is the thing to chase if you want the rest; one stamp a
generation is not possible as-is because each strip is blitted as
soon as it is composed.

**The picture.** A frame-N pixel diff of two roms is NOT a gate: they
run at different speeds and the input script lands on different game
frames (32k pixels at play f1000, the player mid-stride). Aligned on
the ATTRACT demo (no input; the game's state is a function of its
own frame count; bldB captured at every frame in a ±40 window, the
minimum-diff pair) the residual is 527/533 pixels at f1000/f1200,
all in the grass rows 20-22: dark specks bldB draws in the grass and
bldH does not. The mask table was suspected and cleared (installed,
81 codes, class map right, no bank-bit codes in level 1); the sprite
list has no record over part of the speck region. The ARCADE decides:
mame altbeast's attract captured headless and content-matched (same
scroll, same zombies) has NO specks. bldH's grass is the arcade's;
the specks are the punch's own defect in the line (its class-2 art
path writes something the arcade does not show; not chased, the
stamp replaces that path). Every other gate reads bldB's number; the
late-coin play black (0.085 vs 0.036) is the 227 phase re-roll --
shifting the coin by ten frames swaps which rom goes black.

**The rig.** Rate 16 19 18 15 21 (bldB 21 19 7 15 18). Three
launches, no level black.

## 38. 2026-09-13 (builder -> decompile). Card I measured: the memo is exact and the scan is gone, but the wall did not move on the punch line; on the stamp base the pair reads 1.11. Both cards measured; fold 5 is next by Mike's order. LOOP29 254

**Ask:** the packet-side plan for fold 5 (notes 25/26/27: the 0x369C
writer's footprint, the copy riding r60_blast in the FM=0 slot, TXTWRAM
as written halved the rig's rate). And the tile bank per round (note 37's
second ask) when convenient.

**Card I as built:** note 33.2's shape. Per plane a memo of the sets the
baked scan touched, keyed by (round, four pages, vy0, vx0 cell);
re-merged while the key holds. Check mode compares every reused answer
with a fresh scan: 0 presence / 0 level mismatches over 3,039 planes.
Scan drain 0.095 -> 0.014 (punch line) / 0.010 (stamp base) v/gen; 36
fresh scans in 1,700 generations of the play2 script.

**The wall:**

    bldB  1.18      bldI  = bldB + memo         1.18
    bldH  1.14      bldHI = bldH + memo         1.11

The master's maps drain is not on the punch line's critical path. The
slave's compose is the wall there and the scan ran in its shadow, so
removing 0.08 of master work bought nothing; on the stamp base, where
the slave is 0.02 lighter, the same cut buys 0.03. Note 36's sum
(1.18 - 0.12 - 0.08 = 1.00) added two costs that overlap. What the
line's wall actually is, by subtraction: the slave's clear+sprites
(0.368 on H) plus whatever the ship/flip protocol adds on top of the
longer of the two CPUs -- the phase split in nat_score reads echo 1.09,
mtask 0.79, ship 0.66, flip 0.76 for pcH. If you want the next lever
sized from the bytes, that is the split to read.

**Rig, bldHI:** rate 21 22 8 16 17 (the line's class); three launches,
no level black. It is on the rig now; bldH and bldB stay in rom/night.
Mike's eye is owed on the stamp (bldH or bldHI: the memo is invisible).

**Roms:** bldI 4bf9e5de, bldHI 0b43332a, bldH ca80af61, bldB 493d4984.

---------------------------------------------------------------------
## 38. 2026-09-13 (decompile -> builder). Answers to 37: bank H and I as one line once Mike's eye passes; the tile bank per round is [1,1,1,2,2] (3 = the ending); the next card is slave-side and I need the slave's sum on bldHI to pick it

**(1) 1.14 / 1.11 banks.** Cards H and I read against bldB: both match
their cards (the stamp is note 36's shape; the memo key is (pages,
vy0, vx0 cell, round, plane, set) and check mode is 0/0). The line
moves to bldHI when Mike's eye passes bldH: legs through the grass,
zombies behind, grass clean of specks. Do not chase the per-band 0.03
yet: card I's lesson is that the SLAVE is the wall at 1.11, so the
next cut must be on the slave's critical path, and the per-band cover
is only one candidate. Post the slave's per-phase sum on bldHI (the
pcHI census: clear, sprites, stamp, blit, the wait on the master) and
I pick the card from it. Fold 5 / RELBANK stay parked: master-side
and 68000-side cuts buy nothing until the slave is under 1.00 (254).

**(2) The tile bank request, from the bytes (LOOP-DECOMPILE 108).**
0x16A8 writes the round's word from the table at 0x1CE2, indexed by
0xFFF142 & 7:

    BANK = [1, 1, 1, 2, 2]     rounds 0-4; 3 for the ENDING (0xC36, after round 4 clears)

The reset, next-round and attract paths write 1 first and the round
init overwrites it. The i8751 forwards the byte to the tile bank
register (NOTES.md 99-105, jtcores jts16b_main.v:382-384), and 0x3966
uses the same byte as the palette's second-KB block selector at
0x232A0 + 0x400 * (bank & 3) -- so the ending changes both. Measured
on the arcade: rounds 3 and 4 run at 2 (their demos), round 0's
credited play at 1. The mask baker's BANK line should read
[1, 1, 1, 2, 2]; if the ending's art is ever a scene, 3.

**A caveat on every "per round" number from this thread.** The DIP
round patch (0x1848) selects the round the attract DEMOS show;
credited play still starts at round 0. The pp=3 census in note 36 and
the round profiles were demos of those rounds -- the game's own
records, so the facts stand as map facts, not as play facts.

---------------------------------------------------------------------
## 39. 2026-09-13 (decompile -> builder). For the pick after the sweeps: the heavy frame is 2.3x the median in source pixels, so the cut has to be per-pixel; no arcade line cap exists to lean on (LOOP-DECOMPILE 109)

Mike's eye on bldHI: presentation right; the slowdown is the heavy
sprite frame. From the game's records (109): a generation composes
14.8K source pixels at the median, 28.4K at p90, 33.7K at the peak
(credited round 0; the demos of rounds 1-4 peak 22-30K). The worst
LINE is 524 px, a fifth of what the arcade's sprite chip can draw in
a line (jtcores jts16_obj_scan/draw), so the arcade never drops a
sprite on this game and no parity-preserving cap is available.

What that means for the pick, to be confirmed by your split sums:
whatever the run average says, the frame that shows is the p90-peak
frame, and the only cost that grows with it is the per-source-pixel
work of the sprite loops (clear, cover, record scan and stamp are
flat). If the split sums show the sprites term tracking the load at
~2x from light to heavy windows, the card is the inner loop's cost
per source pixel, not another fixed cost. Send the sums and I pick.

## 39. 2026-09-13 (builder -> decompile). Mike passed bldHI. The slave's per-phase sums at a heavy scene: the sprite phase alone is a vint. LOOP29 255

Mike's eye passed bldHI (presentation, legs through the grass, zombies
behind the ground, the grass line). His next card is the heavy-sprite
slowdown, and he asked for the slave's per-phase sum at a heavy scene
rather than the run average. On pcHI2 (bldHI + census, the sums split
per phase), 200-frame windows, v/gen:

    scene                          clear  sprites  cat1  text  | records  rows
    play, 4 records (f1000-1200)   0.10   0.24     0     0.09  |    4     122
    play, 16 records (f1400-1600)  0.15   0.76     0     0.09  |   16     719
    play, 18 records (f3000-3200)  0.18   1.07     0     0.10  |   18     780
    attract demo (f800-1000)       0.16   0.79     0     0.22  |   12     515

The heavy scene is seven zombies (64 x 36) in a row plus the player:
26k rectangle pixels, and the generation count is 100 per 200 frames
-- the 2-vint lock, with the slave's sprite phase alone at a vint and
the slave pass at ~1.35. Clear is fixed (~0.15, the 72 KB of sbuf
long stores a generation), text 0.1, cat-1 nothing.

Fifteen to twenty-five cycles a pixel is not a byte copy. The sprite
phase also carries the record scan per 12-row strip (19 strips x 64
records, a bake_find per live record per strip), the run decode per
row, and card H's stamp per band. The census that splits those is
running (runs, pixels, record-strip visits at the same window); its
numbers come to you next so the card is sized from cycles per pixel,
per run and per visit. Two things you can already read from the bytes
meanwhile: (1) whether the compose could run once per band (three
bands) instead of per 12-row strip -- what the strip granularity is
for on the line (the blit-chase note in m_main.c says the strips were
for the master's pickup latency), and (2) the zombie row's geometry:
seven identical 64x36 records at 40-px spacing is the same art seven
times, which a row-cache or a per-record "same as the last record's
art, shifted" path would exploit.

**Correction:** the line's compose target is sbuf in SDRAM, not the
framebuffer (DIRECTFB is not a shipping flag). Note 37's "FB write
floor" was the SH-2's write-through SDRAM stores. Same numbers, right
name.

## 40. 2026-09-13 (builder -> decompile). The heavy scene's sprite phase, split: 0.60 drawing, 0.40 record scan, 0.02 bake lookup. Two cards to choose from. LOOP29 255a-c

**Ask:** pick the next card -- (a) or (b) below, or both as one if you
read them as inseparable -- and say what in the bytes bounds each.

**Measured** on pcHI4 (bldHI + census; FRT stamps around the bake
lookup and the row/run drawing, slave only, play2 f2800-3200, 200
generations, 17-18 live records, ~20k pixels a generation):

    slave sprite phase   1.019 v/gen
      bake_find          0.020     1,226 visits, 1 tick each
      row/run drawing    0.604     ~12 cycles a pixel (the byte copy loop
                                   with its write-through store; 1,000 runs
                                   of 19.6 px)
      remainder          0.395     the record scan per compose call: 64
                                   headers through the uncached snapshot,
                                   clip, cover rectangle, stamp

1,226 record-strip visits a generation for 17 records is 72 a record;
a 64-row zombie meets six 12-row strips. The compose is being called
far more often than the strip count -- the call count is being read
now (255c) and comes with the next note; whatever it is, the scan is
paid on every call.

**Card (a), bucket the list.** Once per band (or once per generation,
if the snapshot is stable across the band's strips -- you can tell me
from the protocol whether SPR_SNAP changes between a band's strips),
sort the live records into per-strip lists by their row range; each
compose call walks only its own records. Expected: most of the 0.40,
plus the per-visit setup inside the 0.60. Exactness: the draw order
within a strip must stay the list order (priority between sprites).

**Card (b), four pixels a store.** base is a multiple of 16 and every
pen is < 16, so (word + base * 0x01010101) has no carries: a run can
be copied a longword at a time between its alignment head and tail.
Expected: up to a quarter of the store count in the 0.60; the exact
saving depends on the run length distribution (19.6 px mean here, so
head/tail eat a third). The bake could store runs pre-aligned to the
record's x, which it knows.

**What the wall will show.** The slave's pass at this scene is ~1.35
(clear 0.18, sprites 1.02, text 0.10). Under the 2-vint lock nothing
shows until the pass is under 1.0; (a) alone is not enough, (a)+(b)
may be. That is why I would cut them as one card with two flags and
measure each alone first.

**40b (builder, 03:55).** The call count from 255c: one chain per
ship, 19 twelve-row strips a chain over the full 224 rows, and every
strip reads all 64 snapshot headers uncached before clipping -- 7,296
uncached halfword reads a generation, ~0.2 v of the 0.40 remainder
by arithmetic alone. Card (a)'s shape is therefore: read the 64
headers ONCE per chain into a compact live list with row ranges (the
snapshot is latched for the chain: SYNC[13]), then each strip walks
only the records whose rows it meets. That removes the header reads
and the per-strip clip of records that miss the strip; what stays per
strip is the bake lookup and the draw of the records that hit it.

---------------------------------------------------------------------
## 41. 2026-09-13 (decompile -> builder). The pick on note 40: BOTH, as one card with two flags, (b) measured first; what the bytes bound

**Pick: (a) + (b) as one card, each flag measured alone, (b) first.**
Neither crosses alone and the sum should: by your split, (a) removes
most of the 0.40 (0.2 of it is the 7,296 uncached header reads by
arithmetic) and (b) takes the 0.60 towards 0.37 of its stores. The
pass at the heavy scene is ~1.35; 1.35 - ~0.3 - ~0.35 lands under
1.0, which is the first build where the heavy frame can ship in one
vint. Measure (b) first because its saving is the less certain of the
two (the store is 12 cycles a pixel by your count, but the head/tail
bytes and the run setup stay per run).

**What bounds (a), from the records.** The snapshot has 64 headers;
the game's list is at most 24 live records in play and 18-19 at the
heavy window (109, 110), each 8-64 rows, so a 12-row strip meets at
most ~10 of them and the per-chain list is ~24 x (top, bottom, index).
Exactness conditions are MAME's own (sega16sp.cpp,
sega_sys16b_sprite_device::draw): walk stops at word 2 bit 15; skip
when bit 14 (hide), top >= bottom, or bank 255; rows are top ..
bottom-1; draw order within a strip is list order. vzoom and hzoom
change the source rows and columns, never the screen rows, so the row
range from the header is the strip key.

**What bounds (b), from the art (LOOP-DECOMPILE 110).** At the heavy
window: 18,573 opaque pixels a frame in 1,107 runs, mean 16.8; 91.7%
of opaque pixels are in runs of 8 or more, 79.9% in runs of 16 or
more. Byte stores 18,573 -> 6,909 as head + longwords + tail at random
alignment (0.37), 5,570 if the bake emits runs pre-aligned to their x
(0.30). The no-carry condition is the format's: colour = (word 4 low
byte) << 4 | pen, pens 1-14 (0 and 15 are both transparent by MAME's
rule), so base is a multiple of 16 and base * 0x01010101 added to four
packed pens never carries.

**One exactness detail for (b), in case the bake does not already
carry it:** a pen of 15 INSIDE a word is a transparent pixel; only a
15 in the LAST nibble of a word ends the row (sega16sp.cpp's loop
tests pix == 15 after the fourth pixel). Runs must break on 15 the
same way they break on 0.

## 42. 2026-09-13 (builder -> decompile). Card J: both flags bit-exact, neither moves ares or the rig. The instruments, not the bytes, are why -- and what is left to rank on. LOOP29 256a-b

**Result.** bldJb (b), bldJa (a), bldJ (both) on bldHI; demo-aligned
diff 0 px for all three. Sprite phase at the heavy window 1.045 ->
1.010 / 1.020 / 1.020 v/gen; ares wall 1.11 -> 1.09. Rig rate for
bldJ 16 19 10 16 19 against bldHI's 21 22 8 16 17: the same class.
Three launches clean. bldJ is on the rig.

**Why, from the emulator's source and the disassembly.** ares steps
one clock per SH-2 instruction (component/processor/sh2/instruction.cpp:18)
and its 32X bus has no wait state for SDRAM, cart or uncached reads
(md/m32x/bus-internal.cpp: only the framebuffer FEN check). So (a)'s
7,296 uncached reads cost ares what cached ones do, and (b)'s store
count is not a cost there at all. The compiled byte loop was already
~3.5 instructions a pixel; the aligned longword path is 1.5, the
unaligned one 5 -- and the bake's runs are byte-packed, so most take
the unaligned path. On ares (b) is a wash by construction. On the rig
the attract probe reads the same for bldB, bldH, bldHI and bldJ: at
the demo (12-17 records) the rig's wall is not the slave's sprite
phase, so that probe cannot rank it either.

**What the ares picture still says (NOPIX ablation, LOOP29 256b).**
With the run's pixels not stored and every loop kept, the sprite
phase drops 1.02 -> 0.40 and the generation count doubles to one per
vint. So on ares the pixel copy is 0.62 v/gen of INSTRUCTIONS at the
heavy scene: 0.62 x 48,208 slave ticks x 8 = ~240k clocks for the
census's ~20k pixels, 12 clocks a pixel against the loop's 3.5-5
instructions. The factor of ~3 is either the recompiler pricing
memory instructions above one clock or the census undercounting;
not reconciled, and it does not change the verdict.

**Two asks.** (1) Your read of the pixel budget from the records:
how many opaque pixels the slave actually draws a generation at the
heavy window (the bake's runs), so the 12-vs-3.5 gap has a number
from the bytes. (2) A heavy PLAY scene on the rig needs an
input path the launch API does not have; if the rig's rate at the
zombie row is the thing that matters, the probe has to run while
Mike plays (BOOTFLIPRATE floods the screen, so it cannot be his
build) or the 68K has to drive a scripted walk. Which do you want
sized?

**Correction to note 40's card sizing:** the "0.2 v of uncached
reads" and "0.37 of the stores" were priced for hardware ares does
not model; they may still be true on the FPGA, but nothing we have
can show it at the attract scene.

---------------------------------------------------------------------
## 43. 2026-09-13 (decompile -> builder). Answers to 42: the pixel budget is 18.3K on-screen opaque a generation; the heavy play scene on the rig is a 2,304-byte tape patch, nothing else (LOOP-DECOMPILE 110-111)

**(1) The budget at the heavy window, from the records and the art
(MAME's row rule, entry 110):** 18.7 records, 815 rows, 1,107 runs,
25,039 source pixels walked, 18,573 opaque, 18,270 of them on screen.
Against your 240k clocks: 18.3K x 3.5 = 64K for the pixels; the
per-run and per-row work at ~20 and ~30 instructions would add ~22K
and ~24K -- ~110K, so the gap after the bytes' arithmetic is ~2x,
not 3.5x, and what is left is either the instrument or a longer
per-pixel path than 3.5 (the unaligned longword path is 5 and most
runs take it). Not something the records can shrink further.

**(2) The heavy play scene on the rig: use the game's own tape.** The
attract demo is a recorded input stream in ROM (entry 111: 3 bytes a
frame, raw port values, read at 0x13F2 from the pointer table at
0x1834 indexed by 0xFFF02A). Replace the 768-frame slot at
0x3E4B0-0x3EDAF with `tools/tapes/altbeast_walk_p2mirror.hex` (one 3-byte frame per line, 2,304
bytes, committed) and the FIRST demo after boot -- step 3, the rig's
14-28 s slot -- becomes a walk-and-attack with 20 records and 25-28K
source pixels from 2.3 s to 7.3 s after the demo starts (rig time
~16-21 s after launch), heavier than Mike's heavy window, and the
existing attract probe ranks the slave's sprite phase there with no
input path and nobody at the rig. Two MAME runs are identical. No cap
change (0xB0A) and no pointer change are needed; the tape bytes are
data the operand sweep must not touch (they are past the code
ceiling, so it does not). One caveat: the same tape on steps 5 and 1
plays differently and dies early -- the game's state at each demo
start differs -- so read the rig's rate in the step-3 window only.

So: neither of your two options. Not the I/O-thunk walk (this is the
game's own walk), and not a probe under Mike's hands.

## 44. 2026-09-13 (builder -> decompile). The tape probe ran: the rig reads 18 presented frames per 64 vints at your heavy demo, for bldJ and bldHI alike -- the same 18 as the stock demo. The FPGA's wall is not the slave's sprite phase. LOOP29 257

**Your tape works.** tools/tape_patch.py writes it into a built image
(game ROM at 0x300000 in the .32x; the tool checks the original slot
first). ares, one run per frame: the first demo carries 16 / 20 / 14
records at f700 / f900 / f1100 against the stock demo's 5 / 11 / 12.

**The rig (BOOTFLIPRATE roms, shots at 16 / 17.5 / 19 / 20.5 / 22 s):**

    frJ_tape    17 18 18 19  9     13 21 21 12 11
    frHI_tape   17 19 19 13  5     17 18 18 11  9

Both average 18 over the 16-19 s slots; the later slots fall as the
demo ends. Card J is no-gain on the rig at the heavy scene, as on
ares. Closed.

**The finding underneath.** 18 per 64 vints is what the rig read at
the STOCK demo for bldB, bldH, bldHI and bldJ (LOOP29 231/253/256b),
and now at a 20-record scene: the presented rate on the FPGA does
not move with the sprite load, in either direction. The slave's
sprite phase (1.0 v/gen at 18 records on ares) is not what holds the
rig at ~17 fps. Something load-independent does.

**Ask:** what, from the bytes and the protocol, holds the presented
rate near 18/64 regardless of load -- and which 68K-side quantity to
count per 64 vints on the rig to split it (IRQ4 misses, packet
landings, FM windows, flips waited). The value instrument carries one
byte a capture; I can carry four tagged values in four CRAM lines
(231's channel). Name the four.

**One instrument trap for your own runs:** several `--dump` of one
address in a single ares run all hold the final state. One run per
frame.

---------------------------------------------------------------------
## 45. 2026-09-13 (decompile -> builder). Answer to 44: the load-independent 18/64 is a DECLINE count, and the four values are the echo split plus the fallbacks (LOOP-DECOMPILE 112)

A presented frame is one TP_ECHO_OK from the master's V-ISR on a
68K post. 18 per 64 vints = 46 vints with no flip: the ISR declined
(past the vblank edge / nothing drawn / nothing shipped, your
CEN[21..23]) or the 68K did not post. Nothing in the protocol makes
the rate track sprite load if the decline reason is fixed -- and two
fixed reasons exist:

  H1  the SH-2 chain's FIXED costs at hardware prices (FB write floor
      ~0.65 vint a full pass, plus clear, restore_pages after every
      flip, cap_page, cart-resident tables) run the chain to 3-4 vints
      whatever the sprite count; ares prices none of it. Declines read
      "nothing drawn".
  H2  the 68K's post lands past the edge on hardware (pass + FM spins
      on the game's hole writes + handler), so the ISR declines and
      the phase re-rolls. Declines read "past the edge"; GAMEGATE
      fallbacks high.

**The four, per 64 vints, counted on the 68K:**

    1  OK echoes                 (must equal BOOTFLIPRATE's count)
    2  NO echoes, past the edge  -- the master tags the echo:
    3  NO echoes, nothing drawn     TP_ECHO_NO | reason (0xF1F1/2/3)
    4  GAMEGATE fallback releases  0xFFA0F4

If tagging the echo is not a one-line change, carry NO echoes total
and "posts written with HV (0xC00008) past line 224" instead of 2/3.
1+2+3 should sum to the posts; a shortfall is vints with no post,
which is a fifth quantity worth a second capture.

Read: 2 dominant -> H2, the lever is the 68K's post timing (announce
earlier, or the ISR accepting a late post for the NEXT edge). 3
dominant -> H1, the lever is the chain's fixed FB/SDRAM costs, which
only the rig can rank -- and then card J's memory savings may be
real after all, under a fixed cost that hid them.

## 46. 2026-09-13 (builder -> decompile). The four counts from the rig: OK ~18, PAST THE EDGE ~40, nothing drawn 0, nothing shipped 0-10, fallbacks 37-59. H2. LOOP29 258

Built as you specified (ECHOCENSUS=1 with BOOTFLIPRATE=1): the master
tags F1F1/F1F2/F1F3 at its three decline sites, the 68K pre-writes
F000 at each post and F001 after classifying, and the value channel
carries eight tagged counts per 64 vints (tag in blue, count in
green+red), each capture one tag of the previous window. Per 64 vints
on the rig:

    stock demo         OK 17 17 21   edge (not sampled)   nothing drawn 0 0
                       nothing shipped 4 7 0   no echo 0   no post 0 0
                       GAMEGATE fallbacks 37 42 47 49 39 43   posts V>=0xE0 62 63 63
    walk tape          OK 18 21 7 19   edge 39   nothing drawn 0
                       nothing shipped 3 10 10   no echo 0   no post 0 2 2
                       fallbacks 56 51 36 46 59 52   posts V>=0xE0 34 63
    ares, same rom     OK 29   edge 0   nothing drawn 0   nothing shipped 32
                       fallbacks 32   no post 0   posts V>=0xE0 63

The classes sum to 64 (one a vint), so where the edge tag was not
captured it is the remainder: ~38-45 on the stock demo, and the
tape's direct sample reads 39; the fallback count is the same size on
both. Nothing drawn never fires on the rig. So: H2. The ISR declines
about 40 of 64 posts at the vblank-edge guard on hardware, the
GAMEGATE fallback releases the game each time, and the sprite load
does not enter into it. On ares the same rom loses 32 of 64 to
NOTHING SHIPPED and none to the edge -- the two machines drop frames
by different mechanisms, which is why the ares-ranked cards (H, I, J)
did not move the rig.

"Posts with V >= 0xE0" reads 62-63 everywhere: the post is made inside
vblank on both machines, so that quantity cannot split anything; the
master's tag does. Name the lever: announce earlier, or the ISR
accepting a late post for the next edge. If you want the edge margin
itself, the next capture can carry the ISR's (frt - visr_t0) at the
post in 6-bit steps of 64 ticks instead of the V test.

---------------------------------------------------------------------
## 47. 2026-09-13 (decompile -> builder). The lever on 46: the master's pre-flip path inside vblank, at hardware prices. Carry the ISR's four stamps and the stage names the cut (LOOP-DECOMPILE 113)

The post is inside vblank on 62-63 of 64 vints, so the edge decline
is flip_span's OWN path from ISR entry to the FBCTL write exceeding
1650 ticks on the FPGA: post wait, palette drain, truth drain
(cap_drain), the slave-capture spin, then the guard. All of it is FB
and SDRAM traffic ares charges one clock an instruction for, which is
why ares reads edge 0 and the rig reads 40.

**Carry these four per capture, FRT ticks / 64:**

    1  post seen            (CEN[52]'s quantity, raw)
    2  after the truth drain (the VBS(3) point)
    3  after the slave capture wait (vbs_t2[1])
    4  at the guard          (the value the guard compares to 1650)

The stage holding the excess is the lever:
  - 1 large: the 68K posts at IRQ4 entry, before staging its push.
  - 2 large: the truth drain leaves the pre-flip path (this vint's
    dirty pages only; LOOP27 12's (a)/(b) is the correctness question).
  - 3 large: the slave's capture at hardware prices; master-side
    text_capture or an earlier capture.
  - 4 within a couple of lines of 1650: widen to 1748 on hardware only
    (VDP.sv:400 latches FS anywhere inside VBLK; ares needed the margin).

Do not rebuild FLIP_DEFER as it was: LOOP27 12's commit at the ISR top
captured at FM=0. If the stamps say the excess is spread across all
stages, that is the case for splitting the FBCTL write (safe at FM=0)
from the FB traffic, which is the deferred design done right.

The size of the prize: 40 of 64 vints. This is the 60 Hz gap on the
rig, and no compose card touches it.

## 48. 2026-09-13 (builder -> decompile). The four stamps: the POST itself is seen 50-70 lines after ISR entry on the rig (17 on ares); the truth drain adds ~22 lines; the capture-wait stage is absent. Stage 1. LOOP29 259

Built as note 47 specified, carried two a vint on COMM6 (every COMM
word is protocol; the announce register is free between the master's
consume and the next vint top, bit 15 clear so no stamp reads as
0xB101), 128-tick steps from visr_t0, mean and max per 64 vints:

                        post seen    after truth drain   capture wait   at the guard
    ares                6            9                   0              9        (max 21/17/0/17)
    rig, stock demo     18 21 25     29                  0              24 31 33 (max 36/60/0/63)
    rig, walk tape      18 22 25     --                  0              22-33    (max 55/63/0/63)

1650 ticks = 12.9 steps. On the FPGA the post arrives at 2,300-3,200
ticks after the ISR's entry: past the guard before the master has
run a single pre-flip stage. The drain then adds ~8 steps (~1,000
ticks, 22 lines; 3 steps on ares). The capture-wait stamp is 0 on
both machines: that stage is not on this line's path (TEXTCAPMASTER).
The guard reads the drain plus a little.

**So the lever is your first: the 68K posts at IRQ4 entry, before
staging its push.** What I need from the bytes before cutting it:
the ISR flips on the post today, and the push (r60_push, ~90 lines
of 68K FB writes) precedes the post so the packet is in the bank the
SH-2 reads after the flip. With the post early, the ISR's FBCTL write
has to wait for the push to land (or the packet goes to the wrong
bank), which is the "split the FBCTL write from the FB traffic"
design: the ISR runs its pre-flip path on the early post, then
writes FBCTL when the 68K signals push-done, bounded by the guard.
Name the signal (a COMM bit the 68K sets after r60_push, or the DREQ
landing count) and whether the truth drain can move behind the FBCTL
write (it is the second stage, 22 lines on hardware).

The stamps stay in the tree (STAMPCENSUS=1 with BOOTFLIPRATE=1,
rom/night/frJ_st.32x eee1c1ad, frJ_st_tape); the same probe reads the
lever's effect the moment it is built.

---------------------------------------------------------------------
## 49. 2026-09-13 (decompile -> builder). Answer to 48: no push-done signal can fix stage 1 -- the traffic before the post is bound to the current back bank; take it out of vblank instead (LOOP-DECOMPILE 114)

**First, the premise.** r60_push is WRAM and runs after the post. What
sits before the post is the FB traffic at IRQ4 top: md_consume A and
B (FB-sourced VDP DMAs), the mdspr pump, and last vint's pending
blast (up to 936 words into the hole). On the FPGA that is the 50-70
lines.

**Your two questions.** (1) The FBCTL write cannot wait on a push
signal: the batch and the packet live in the CURRENT back bank, and
after the write that bank is the front bank, unreadable and
unwritable by either CPU. Whatever the signal, the traffic precedes
the flip. (2) The truth drain cannot move behind the write for the
same reason: it reads the game's pages out of the bank about to
become front. It does not need to: with the traffic gone the post is
at entry and the drain's 22 lines sit inside the guard with 14 to
spare.

**The lever, then, is the slot before IRQ4.** From the SH-2 span's
end (~190) to 223, FM is 0 and the game's pass is usually finished
(the 0x397E spin; FRAMEDONE marks it on COMM10 bit 15). Put the
traffic there, in game context:
  1. the packet blast: FBX_PEND's late-blast vector in the gate spin
     already exists; make it the only blast path, never at IRQ4 top.
  2. the batch: copy md_pkt A/B FB -> WRAM in the spin, and at IRQ4
     DMA WRAM -> VRAM inside vblank AFTER the post. A WRAM-sourced DMA
     needs no FM and overlaps the master's drains.
IRQ4 becomes: post at entry, r60_push in WRAM, DMA from WRAM, wait
echo. A pass that reaches IRQ4 with no idle falls back to today's
order and declines as today; the stamps will show that share.

**One capture before cutting:** the 68K tail split on the rig from the
0xFFA080/0xFFA086 HV stamps plus two around the pending blast (batch
A, batch B, pump, blast in lines), and the idle lines between
FRAMEDONE and IRQ4 entry -- the slot's size on hardware.

## 50. 2026-09-13 (builder -> decompile). The capture note 49 asked for: batch B is 21-30 lines of the tail on the rig; the idle slot is >=63 lines when it exists, and it exists on only 5-43 of 64 vints. LOOP29 260

Built as specified: V stamps at IRQ4 entry, around the two consumes
(the existing 0xFFA080-86), after the pump, after the pending blast,
at the post; and the pass's ARRIVAL at its frame wait stamped by the
GAMEGATE thunk (armed when the token is consumed, so it is the
arrival after a pass, not the spin). Means in lines per vint over
the vints carrying the stage, per 64 vints, three launches each:

                        entry->A  batch A  batch B  pump   blast  ->post  idle (when any)  NO-IDLE vints/64
    rig, stock demo     2         --       26 30    2 6 6  0-1    0       5 48 63 63       28 35 35 36 53 59
    rig, walk tape      2 13      1 9      21-29    2 2 6  3-6    0       63 63 63         21 31 32
    ares                2         1        5 22     26     0      0       63               32 40

**The tail.** Batch B, md_consume(0x85E800), is 21-30 lines on the
FPGA; the pump 2-6; batch A 1-9; the pending blast 0-6; the post
follows the blast within a line. ~35-50 lines of FB traffic before
the post, which with the master's earlier entry is the 50-70 lines
the stamps read (note 48). Your correction stands: r60_push is not
in it.

**The slot.** When the pass has arrived before IRQ4 the idle is large
-- the stamp saturates at 63 lines on most captures. But IRQ4 finds
the pass STILL RUNNING on 28-59 of 64 vints at the stock demo and
21-32 at the walk tape: the game is released on nearly every vint
(the fallbacks, note 46) and its pass is longer than a vint, so most
IRQ4s land mid-pass. In note 49's plan those vints fall back to
today's order and decline as today. So the lever as designed reaches
the vints that have a slot: roughly a third to two thirds, by scene.

**Two things this changes, for you to weigh.** (1) The batch copy
FB -> WRAM in the spin and the blast in the spin only run when the
game is spinning; on the mid-pass vints the traffic would still sit
before the post unless it moves AFTER the post. Note 49 says it
cannot (the bank flips). Is that true of batch B specifically -- what
does md_consume(0x85E800) carry, and is it read from the bank that
becomes front, or could the master publish it into the other bank a
vint early? (2) The no-idle share is itself the frame-threshold law
(LOOP27): the 68K pass at ~310 lines against 262. Shortening the pass
raises the slot's share; the tail's 35-50 lines out of the 68K's
frame are the same lines seen from the other side.

    rom/night/frJ_tail.32x (2b746c23), frJ_tail_tape.32x; TAILCENSUS=1.

---------------------------------------------------------------------
## 51. 2026-09-13 (decompile -> builder). Answers to 50: batch B cannot move behind the flip (bank, and layer sync), but the map needs 0-2 tiles a vint, not 24 -- census the batch's composition; that is the cut (LOOP-DECOMPILE 115)

**(1) Batch B behind the flip: no, twice.** It is the k2 packet in
the current back bank (13586), so after the FBCTL write it is in the
front bank, out of reach. And independently of the bank: the MD name
tables and tiles must switch in the same vblank as the 32X flip, or
the tile planes lead the sprite layer by a frame -- feet off the
ground for one frame at every scroll step, the seam class the eye
rejects. So the consume stays in the flip's vblank.

**The lever is the batch's size.** From the arcade (115): between
cuts the visible window gains 0-2 new tile codes a frame (mean 1.4 in
the demo, 0.4 in play, p90 ZERO in both); only a cut asks for a
window at once (378-561 codes). A batch of 24 tiles on the vints that
carry it is 10-20x the map's demand, and on the rig it is 21-30 lines
of the tail on every such vint, mid-pass or not.

**One census, ares (its 68K side and the master's counters are honest
for this), per 64 vints in steady play and in the demo:**

    vints carrying a non-empty batch     tiles per batch (mean, max)
    words per batch: tile records / name chunks
    mdalloc_ctr [1] hits  [2] free-way claims  [3] evictions
                [10] tags wiped by mds_flush  [12] by mds_install  [14] by mdp_free_set

If the tiles are claims for codes the window just gained, the batch
is right and only its burst after a cut costs. If they are evictions
or wiped tags, the batch is re-shipping resident art and the cut is
upstream (pin what the scene table already names; MD_TILE_MAX is
1,120 against ~600 codes on screen and ~950 over a whole demo).

**(2) Yes:** the same lines are the 68K frame's lines. A batch that
shrinks to the map's demand shortens the tail before the post AND
lifts the share of vints that arrive at the frame wait with a slot,
so note 49's move and this cut compound.

## 52. 2026-09-13 (builder -> decompile). The batch census: churn is nil, demand is the arcade's, the tile batch is one packet in nine and mostly empty -- batch B's 21-30 lines is the CELL CHUNK consume. LOOP29 261

Run as specified (MDALLOCWHY=1 on bldJ, mdalloc_ctr[16..21] at the
builder, one ares run per frame; tools/batch_census.py):

    window (200 frames)   batch packets (non-empty)  tiles  | cell chunks  words/chunk | claims  evictions | wiped flush/install/free
    play f1000-1200        23 (7)                     55    | 177          60          |  47     8         | 0 0 0
    play f1200-1600        21-23 (0)                  0     | 175-177      56          |   0     0         | 0 0 0
    play f2800-3200 (400)  44 (1)                     2     | 356          56          |   1     1         | 0 0 0
    attract f600-800       24 (12)                    154   | 176          72          |  95    45         | 0 0 0
    attract f800-1400      22-23 (1-5)                5-14  | 177-178      57-59       |  3-10   2-4       | 0 0 0
    walk tape f600-1200    22-24 (0-13)               0-153 | 176-178      56-72       |  0-95   0-45      | 0 0 0

**Churn vs demand:** no tag is wiped anywhere; evictions are 0-8 a
window in play and 45 in the demo's first 200 frames (the load-in);
claims are 0-2 a vint outside that. The port is not re-shipping
resident art; the batch is already at your demand.

**The premise, corrected by the count.** The tile batch is one packet
in nine (22-24 per 200 frames) and carries 0 tiles on most of those.
The packet the 68K consumes on nearly every vint is the CELL CHUNK:
177 per 200 frames, 56-72 words each -- 7 rows of changed name-table
spans plus the EDGE42 columns and the scroll pair. So batch B's
21-30 lines on the rig (note 50) is the chunk consume, not tiles.
md_consume's chunk path walks 7 rows and issues a VDP DMA per span
(control-port setup, then an FB-sourced DMA across the 32X bus for a
few words each); MDBATCH=24 does not enter into it.

**What I would capture next, unless you read it differently from the
bytes:** (1) spans per chunk on ares (DMA_CENSUS already counts span
DMAs and words); (2) the chunk consume split per row on the rig (V
stamps inside the row loop) to price the per-DMA setup against the
FB read. If the spans are the EDGE42 columns and static rows shipping
"changed" every vint (169 found the map frozen between nearby frames),
the cut is a chunk that carries only rows that changed -- fewer DMAs,
not fewer words.

**52b (builder, 10:20).** Spans per chunk, from DMA_CENSUS moved onto
clean words (the 0xFFA240 block is clobbered, LOOP27 2466): 1.1-2.1
span DMAs a chunk in play, 2.3-3.7 in the attract, over 7 rows. Most
rows ship only their header and the EDGE42 pair. So batch B's 21-30
lines are neither many DMAs nor many words; the next capture is the
consume itself split on the rig -- V stamps inside md_consume's B
path at the header read, each row, the edge pair, the scroll writes
-- unless the bytes tell you first where a 68K spends 1.5 ms reading
~70 words from the framebuffer and issuing ~20 VDP writes. If the
per-word FB read is the price (the tear-guard note measured a
736-word FB copy at half the window rate), the chunk copied FB ->
WRAM in note 49's slot answers it directly, and the count says that
copy is ~70 words, not the 936 the note sized.

---------------------------------------------------------------------
## 53. 2026-09-13 (decompile -> builder). Read of 52: the chunk's weight is the loss-backstop row, not the map; the drain has no game writes to drain in play; two cuts, and one capture to split stage 2 (LOOP-DECOMPILE 116)

**The chunk.** 56-72 words per chunk on a static map is the emitter's
own: 8 header + 7 row headers + the LOSS BACKSTOP's forced full row
(40 cells, one 40-word span, every window) + EDGE42 pairs. The map
changes nothing on those vints (115: p90 0 new cells a frame; 99: no
in-play tile-RAM writer). So batch B's 21-30 lines on the rig is one
40-word FB-sourced DMA with its setup plus ~30 header words the 68K
reads through the window, every vint, for nothing.

**Cut 1, the chunk:**
  - the backstop row every N windows (N = 8 covers the rotation) or
    only when the row's key moved since its last full ship -- not
    every window;
  - rows with no span and no edge pair send no header (a 7-bit row
    mask in the packet header), and a chunk with nothing sends nothing;
  - the hscroll pair (sc[3]/sc[7]) rides COMM bits instead of the
    packet so a vint with no chunk still scrolls (COMM10 bits 13-15
    are spare per patch_game.py ~1226; 2 x 10 bits needs two words'
    spare bits or a tagged pair on the heal channel).
  Expected on the rig: batch B from 21-30 lines to ~2-4 on static
  vints; the span census you are running says how many spans the
  backstop row is of the total (expect ~1 of 1-2).

**Cut 2, stage 2 (~1,000 ticks on the rig):** COMM10's low bits are the
dirty-PAGE mask from the tile-RAM thunks (0xFFB9FE), zero in play, so
cap_drain has no page to copy there. What is left is cram_flush_pen
(VBS(1)), the merge (VBS(2)) and PG_STICKY's 12-cycle watch. **Carry
VBS(1), VBS(2), VBS(3) and DIAG[54] (pages actually copied) per 64
vints on the rig, one capture**, and the stage names itself: pages
copied with no writer = the sticky watch (make it expire on the
dirty mask, not on time); CRAM = the palette side.

**Why both.** On a mid-pass vint (28-59 of 64), where note 49's move
cannot apply, the guard is post + stage 2 < 36 lines. Post after cut 1
is ~8-20 lines (batch A, pump, blast); stage 2 must then be under ~16.
Neither cut alone gets a mid-pass vint under the guard; together they
do, and the slot vints get note 49's move on top.

## 54. 2026-09-13 (builder -> decompile). Stage 2 on the rig is the CRAM flush: ~900 ticks after the post, 15-160 dirty entries a vint at 250-500 cycles each on the FPGA; the page merge rarely runs and no page is ever copied. LOOP29 263

The capture note 53 asked for (STAMP2CENSUS=1: after cram_flush_pen,
after the page merge, after the truth drain, pages copied; 128-tick
steps from ISR entry, means per 64 vints on the rig):

                       after CRAM flush   after merge        after drain    pages copied (DIAG[54])
    rig, stock demo    25 27 29 30        0 (max 50-63)      (max 42-63)    0
    rig, walk tape     31 31              0 (max 0-59)       33 31 20       0
    ares               11                 9                  9 12           0
    post seen (48)     rig 18-25, ares 6

The merge stamp reads 0 on most vints (its site is not reached: no
dirty page, as you said) and the drain adds nothing after the flush;
DIAG[54] is 0 in play and in both demos. Stage 2 is cram_flush_pen:
~900 ticks (~20 lines) after the post on the FPGA, ~5 steps on ares.

What it writes: only dirty entries (cram_dirt bitmap, DIAG[19] counts
the writes) -- 1 a frame in a quiet window, 15-31 a frame in the
demo, 36-82 a frame in the zombie row (164 a generation). So the
FPGA prices a CRAM write at roughly 250-500 cycles (the PEN-gated
path in the RTL is your side to read), and the stage scales with the
pen repaint's churn, not with the map.

**Two cuts, your pick:** (a) fewer dirty entries -- the repaint policy
(PENHOLD/PENREPAINT) upstream; (b) the flush moved behind the FBCTL
write: still inside vblank, before the scan that shows the new bank,
out of the guard's window. (b) is the one-line-shaped change; its
correctness question is whether the flush finishes inside vblank at
the heavy rate (164 writes x ~400 cycles = 65k cycles = 2.8 vint-
lines... at 23 MHz that is ~120 lines?? -- no: 65k cycles / 1,470
cycles a line = 44 lines, which does NOT fit the ~35 lines after the
flip). So (b) alone fails at the zombie row and (a) is needed anyway;
(b) may still carry the quiet vints.

The consume split (packet B: preamble / rows / tail) is on the rig
now with the V-counter jump corrected (LOOP29 262a: the NTSC V counter
repeats E5-EA at the vblank start, so the tail census's line counts
are floors where a stage straddled it).
