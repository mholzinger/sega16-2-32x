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
