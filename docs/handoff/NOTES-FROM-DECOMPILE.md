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
