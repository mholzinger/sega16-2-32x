# HOT PATH -- what consistently works for level one, and the conditions

Mike, 2026-09-21: *"while we are moving towards the fast delivery system
that is accurate (FDSTIA) we need to also keep in mind EVERY PATH that
worked. Like a hot path."* This file is that list. Add to it only from a
rig-verified build; say what was verified and how.

## The line and how it is built

    make line                    LINE_FLAGS in the Makefile, nothing else.
    rom/night/slim31.32x         THE LINE (2026-09-21 01:15). Rig: level-1
                                 background present on 2 of 3 launches
                                 (99% non-black, mean ~[120,160,150]);
                                 the third lost it. The race below.
    rom/s16.32x                  must equal the line at session end
                                 (differs at the stamp bytes 25f3/3fff).

Verification that a build satisfies level one, in order, cheapest first:

1. `.build_flags` carries every define you meant (the zsh trap: spell
   flags out, `make line X=1 Y=1`, never `make line $F`).
2. ares, 1200 frames of `discover/inputs/play_level1.csv`: scene timer
   0xFFF02A ~718 at frame 1200; screenshots at 600/1100 match the line.
   Ares is exact on data and blind to hardware timing.
3. Rig, attract demo, `tools/mister_push.sh` + 5 captures 25 s apart:
   at least two frames >95% non-black with the level's colour mix at
   37-50 s. Sky-only blue [~5,92,130] at ~61 s with no 99% frame = the
   background is missing. Launch THREE times and report pass/total: the
   verdict is a race whose odds the layout shifts (LESSONS 01:30).
4. Mike's play pass: background, transform, Zeus text, score table,
   frame feel.

## Paths that have worked on hardware (each with its evidence)

- **Boot**: slave SDRAM warm-up (MiSTer keeper), stack at 0xFF3FF0 clear
  of the thunk page (2026-09-19, MAME watchpoint), .tilesmd at 0x268000
  with the ld assert (2026-09-20).
- **Tiles**: both routes. The FB route (SH-2 packs 17-word records, 68K
  DMAs them from the FB) is the pre-slim line. The slim route (SH-2 ships
  2-word records for baked sets, 68K fetches cart art after the game's
  IRQ4 and DMAs it from WRAM; unbaked sets ride inline 17-word records)
  is the line since 2026-09-20. Flip rate in the attract demo: both
  ~28 per 64 vints (rig, BOOTFLIPRATE). 92% of gameplay tiles are baked
  (BAKECENSUS); the attract is not (round unpublished).
- **Text**: FM-gated writers with a TEXTCAPMASK row mark; the Zeus
  typewriter marks all rows (st.b) since slim30. Marking only its rows
  (bset #2) shipped in slim23/25 and those builds lost the background --
  see the open item below before reading that as cause.
- **cat-1 priority**: per-pixel masks from the bake for level pages,
  computed at run time for unbaked pages (the transformation's page 10,
  2026-09-20, Mike: transform fixed).
- **Palette**: BG palette rides the packet at +688, flagged on change;
  the first 16 publishes are forced since slim31 (the FB persists across
  a warm relaunch on the rig).
- **Sprites**: FB composite + MD offload of mob sprites (MDSPR); the
  offload was cleared as a transform suspect by MDSPROFF=1.

## What is NOT known to work, and why the list above must stay honest

The level-start background on the FPGA is a per-launch race whose odds
build layout shifts: a 1- or 3-word pad in one thunk passes, a 5-word
pad, one added gate site, or the bset mark fail almost always, the line
itself fails one launch in three, and ares never shows it (LESSONS
2026-09-20 evening and 2026-09-21 01:30). Until that step is found,
any verdict on a build that changes the 68K image is one layout's luck.
The rig readback (CRAMPROBE) on failing layouts: no palette-flagged
packet consumed in 60 s; forcing the SH-2's flag did not rescue them.

## Observations to keep

- Mike, 2026-09-21: no black tile pop-in on levels 2 and 3; level 1 has
  it. The bake covers rounds 0-4; level 1's five unbaked sets are
  38, 42, 46, 64, 65. Either the later rounds' bakes are complete and
  level 1's is the odd one, or the pop-in is a level-1 load shape.
  Measure before building.

## 2026-09-21 05:20 -- the framebuffer packet echo belt (FBXECHO=1)

Path: 68K stages the packet in WRAM -> blasts it into the draw bank at
FM=0 -> master lifts in its ISR before flipping. Failure: a lift that
overlaps a blast in progress leaves the finished packet in the displayed
bank, and the master's fill erases it (the level-start black
background: boot palette packet 2). Fix: the master echoes the sequence
it lifted in its packet header (word 5 bits 12-15); `fbx_echo_belt()` in
partb_hook re-blasts the kept packet two vints after an unechoed blast.
Conditions: FBXSTAGE + FBXPEND + FBXISRLIFT (the line), FM=0 at
partb_hook. Proof: ares palgate 8/8 layouts; rig pad-5 0/6 -> 3/3.

## 2026-09-21 09:30 -- round-driven MD table install on a round change

Path: 68K posts the game's round in the MD_STATE word (COMM14 [6:4]);
the SH-2 installs `pal_rounds_md.h`'s table for that round. Trigger:
display-on edge (existing) OR the published round differing from the
installed one while on screen (new). Conditions: MDROUND + MDSTATE +
MDSREFUSE (the line). Proof: ares level-2 demo background present at
the round-anchored frame; rig 3/3 (screenshots/rig_round1).
