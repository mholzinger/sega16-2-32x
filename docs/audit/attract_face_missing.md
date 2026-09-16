# THE ATTRACT FACE SCREEN DRAWS NO FACE (2026-09-15, ares, no rig)

**METHOD CORRECTED 2026-09-16 after re-reading START-HERE and
ARCHITECTURE.md. Three things in the first version were wrong about
documented architecture; the observation survives all three, but the
reasoning below the line is what should be reused, not the original.**

  1. ALIGNMENT. This file first aligned by a SPEED RATIO (our frame =
     arcade x 1.309, from the ships counter). The project's method is an
     ADDITIVE offset from the display-gate mailbox (0xFFB001 bit 5),
     bisected, and it already exists as tools/attract_parity.py. On this
     build it reports OFFSET = 55.
  2. CROP. ares screenshots are 1415x243 WITH OVERSCAN. The active area
     is crop 1280x224+65+19 -> 320x224. The first version computed pixel
     statistics on the uncropped frame. Redone with the crop the numbers
     barely move (arcade 31.2% vs 30.9%), but uncropped stats are not
     comparable to the corpus and must not be quoted.
  3. SINGLE BUFFERING. Under FBXPORT the master composes INTO THE BANK
     BEING DISPLAYED (START-HERE, "THE BAR"), so one screenshot can
     catch a partly-composed frame and a still frame is expected. A
     single missing element in a single shot is NOT evidence. What makes
     this finding stand is that it is zero at every frame across the
     whole screen, not that it is zero in one.

Also note tools/attract_parity.py's ladder tops out at 90 frames and the
face/eye rows never collapse (diffs 100-113), which is consistent with
our lag being far larger than 90 frames by that point in the timeline.
A FIXED offset only holds near the alignment point on a build this slow.

AND THE BAR IS NOT THE NUMBER THIS FILE FIRST QUOTED. "ships 3058/4000 =
47.8 fps" is a ships counter. The bar is tools/presented_fps.py MOTION,
and on this build it is 9.3 fps (any-change 25.7). START-HERE's opening
section exists because ranking on the wrong one of these already cost
two sessions.

---


Found offline on the line (`make line`, bldS-equivalent, 17 stamp bytes).

## Alignment

Do not diff two roms at the same frame number -- they run at different
speeds. Align on the game's own clock. Attract, no input, 4000 frames:

    ships 3058 / 4000 = 0.764 of arcade    ->  our frame = arcade x 1.309
    wall 0.91v (mean), max 5.38v

That ratio is confirmed against the game's own attract step (0xFFF031,
the address tools/arcade_pagesel.lua reads) at nine points, every one
inside its predicted window. It also settles which clock gates the 68K:
gated per GENERATION would predict 1.098x and put us AHEAD of the
arcade; gated per SHIPPED FRAME predicts 1.309x and fits. It is the
shipped frame.

## The defect

Classify each centre pixel as a FIELD colour (the screen's blue, red,
orange/yellow, or black) or not. The face is the only thing on that
screen that is not a field colour.

    arcade, face frame          non-field centre pixels  30.9%
    ours, frames 1560..1595     non-field centre pixels   0.0%

Zero, at every sampled frame across the whole screen, so it is not a
phase artefact. The background renders correctly -- blue and red fields
and the flame scrolls are all present and the right colours. The face
alone is missing.

Frames 1605-1640 are then FULLY BLACK (100% of sampled centre pixels),
a sustained blackout of at least 35 frames before the eye screen.

Reproduce:

    A=.../ares-headless
    $A --frames 1620 --screenshot 1585:/tmp/face.png rom/s16.32x
    # and the oracle, no coin:
    mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
      -nothrottle -window -resolution 160x120 -keyboardprovider none \
      -nomouse -nojoystick -bench 40 -autoboot_script tools/attract_steps.lua

tools/attract_steps.lua logs the arcade's step/round/progress timeline,
which is what the alignment above is built on.

## Not yet known

Whether the face is sprites or tiles on this screen, and therefore
whether this is the sprite path or the tile path. That is the next
question and it is answerable from the arcade with no rig.

## PACKING ANSWER, AND A CORRECTION TO MY OWN DIAGNOSIS (2026-09-16)

Arcade PAL_SH at frame 1060, quantised with mdpen_bake's quant():

    set 19: 140 180 1C0 1FF      a BLUE RAMP to white -- the zigzag field
    set 20: 140 1F 7             dark blue + red + orange
    set 21: 140 1F 7             identical to 20

Against scene 0's current pack (line 0 15/15, line 1 15/15, line 2
11/15, so FOUR free slots, all on line 2), and remembering the real
constraint is PER LINE -- every colour of a set must sit on one line:

    union of all three (6 new)   does not fit any line
    sets 20+21 together (3 new)  FITS on line 2
    set 19 alone (4 new)         FITS on line 2
    but 19 AND 20/21 together    6 new > 4 free, does NOT fit

So MDCHEV is HALF buildable with no fourth line: either the ramp or the
red/orange pair, not both.

**AND THE REFUSAL IS PROBABLY NOT WHAT KILLS THE FACE.** Those six
colours are blue, red, orange and white, and our render at frame 1585
already shows a blue field, a red field and orange scrolls -- correct
colours, present. What ours lacks against the arcade is the ZIGZAG
texture in the blue (set 19's ramp, rendering flat) and the FACE, whose
skin tones come from some other colour set entirely and are not in
19/20/21 at all.

So the sets-19/20/21 refusal plausibly explains the flat blue. It does
not explain the missing face, and I asserted that it did. The next
question is which set the face tiles carry, and whether THAT set is
packed -- not more work on 19/20/21.

## WHERE THE FACE IS LOST (2026-09-16) — narrowed by elimination

Everything upstream of the screen is CORRECT. Checked in order:

    tile art baked        page 10: 127/127 codes in sh_src/tiles.bin
                          page 11:  32/32   (1 blank, code 1281)
    art reaches MD VRAM   91 of 91 distinct tile masks present at f1585
    palette packs         mdpen_bake --also 20,21 fits (41 sets,
                          lines [15,15,14], one pen spare)
    palette matters?      NO -- built it, pixel-identical to bldS

So the art is baked, shipped and resident, and the colours are available.
The screen still renders flat.

THE REMAINING SUSPECT IS THE 32X FB LAYER COVERING THE MD PLANES. The FB
at f1585 is 67.3% pen 0 (transparent, priority set) but also 4.9% pen 18
= rgb(0,0,248) blue, 2.8% pen 23 = blue, 4.3% pen 128 = black — roughly a
third of the screen carrying FLAT blue and black. The MD planes below it
hold the correct detailed art with the correct palette. A flat FB fill
composited OVER a correct MD render is exactly the observed picture: right
colours, no zigzag, no face.

NEXT TEST, and it is one build: suppress the FB compose for this screen
(or run with the FB layer forced transparent) and see whether the face
and the zigzag appear from the MD planes underneath. If they do, the bug
is that the SH-2 is compositing a screen it should be leaving to the VDP,
and the fix is a scene/page gate, not a palette or a bake.

## THE FB IS INNOCENT, AND THE PALETTE HANDOVER IS THE BUG (2026-09-16)

**The FB theory is DEAD and the build was not needed.** The decompile
thread's arithmetic caught it first: 67.3% transparent cannot produce a
uniformly flat screen. That 67.3% was a histogram over raw 32X DRAM —
both banks plus the line table — and not a decoded framebuffer, so it
was not a measurement of anything.

Decoded properly (line table at DRAM+0, 256 word offsets, pen 0
transparent), bank 0 at f1585:

    opaque pixels 1,053 / 71,680 = 1.5%

and they sit in three contiguous blocks: row 1 cols 20-24, row 25 cols
12-15 and 26-27, row 26 cols 29-38. That is the score line, INSERT COIN
and the SEGA notice. **The FB draws text and nothing else on this
screen.** 98.5% of the picture is MD planes showing straight through.

## What is actually wrong: the sets never reach MD CRAM

Colours the screen needs, from the arcade at f1100:

    set 19    1FF 180 1C0 140          white + blue ramp (the zigzag)
    set 20/21 140 03F 037 02F 027 01F 007   blue + yellow->red ramp

Against our MD CRAM across eight frames of the screen:

    frame   set19 best line   set20/21 best line
     1560        L2 1/4            L1 1/7
     1575        L2 1/4            L1 7/7
     1585        L2 1/4            L1 4/7
     1600        L2 1/4            L1 5/7
     1615        L2 1/4            L3 0/7
     1630        L2 1/4            L3 0/7
     1645        L2 1/4            L3 0/7
     1660        L2 1/4            L3 0/7

    frames with BOTH sets fully present: 0 of 8

**Set 19 NEVER lands** — one of its four colours is present and that one
is white, which is on the line anyway. Sets 20/21 land complete in ONE
frame of eight. This is LOOP29 284/285's logged anomaly ("only 2 of 8
chevron frames show the game's own sets 20/21"), measured again with set
19 added, and it is upstream of the framebuffer.

It also explains the picture exactly: no zigzag because set 19's ramp
never arrives, and fields that render because 20/21 partly do.

The static bake is NOT the failing path — packing 20/21 into it changed
no pixel. The handover is. `glow_chev` (m_main.c:14620-14650, GLOW_PAGE)
already gates this screen exactly; do not write another gate.

## ISOLATED (2026-09-16): PAL_SH is correct, MD CRAM never gets it

Ran the LOST-PUSH detector's logic on transformation frames, rebuilt on
--dump because headless ares writes no .bs1 (shadow 0xFF6000, game
mirror 0xFF9000, PAL_SH at SDRAM 0x27000):

    f1575  LOST-PUSH 0   pending 0
    f1600  LOST-PUSH 0   pending 0
    f1630  LOST-PUSH 1   pending 0   -- word 0x036, the BLINK word
                                        glow_bake.py leaves unbaked

The belt is CLEAN. And the colours are actually there, at 8 words per
slot (the 14-colour / 28-byte structure, not 16):

    arcade  set 19 @f1100   7FFF 4B00 4C00 4D00 4E00 4F00 4900 4A00
    ours    game 0xFF9000   7FFF 4B00 4C00 4D00 4E00 4F00 4900 4A00
    ours    PAL_SH          7FFF 4E00 4F00 4900 4A00 4B00 4C00 4D00

Identical ring, phase-rotated, which is what a cycler should look like.

So: the game writes it, the queue delivers it, PAL_SH holds it, the tile
art is baked and resident in MD VRAM, the FB covers 1.5% of the screen
and only with text -- and sets 19/20/21 still never reach MD CRAM.

**The fault is isolated to the SH-2's MD pen path: PAL_SH -> MD CRAM
line assignment.** Nothing upstream of it is wrong.

Stride check, as asked: our readers are correct. m_main.c uses
PAL_SH[s * 8 + p] throughout (2509, 2576, 2731, 2895) and
tools/actor_palettes.py uses BASE 0x242A0 STRIDE 28. No 32-byte drift
found on our side.

Also recorded: the index-0/15 rule does not change this screen's
arithmetic -- set 19's lone matching white sits at MD CRAM index 14,
inside the written range, so it is another set's colour coinciding. The
conclusion is unchanged and stronger: nothing of set 19 lands.
