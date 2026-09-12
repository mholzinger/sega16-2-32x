# HANDOFF — the decompile thread, session 2

Written 2026-09-11 at the end of session 1. Working log is
`docs/log/LOOP-DECOMPILE.md`, 71 entries, newest last. Read entries 50
and 71 before anything else: they are about HOW to work here, and they
cost the most to learn.

---------------------------------------------------------------------
## THE STATE: the disassembly is done. Naming is not.

    instructions      19102 of the reference's 19137          99.82%
    above the 0x1EF1E code ceiling                                 0
    functions bounded                                            560
      correctly bounded (terminator or fall-through)              98%
    classified                                                   560
      read to their return and named                              45
      classified by SIGNATURE — hypotheses, marked as such        515
    rom data attributed to a purpose                             ~71%

Rebuild the analysed project with `tools/ghidra/rebuild.sh`. **Import
once, seed once, stop.** Re-running Ghidra's analysis over seeded code
DESTROYS it (entry 18): 534 functions down to 450.

---------------------------------------------------------------------
## WHAT IS VERIFIED, AND THE RULE THAT DECIDES IT

Entry 50 audited all 27 load-bearing claims by HOW each was established.
The split was perfect:

    read the CONSUMING instruction, or measured on a running frame   21   all stand
    plausibility filter / partial read / absence / bad comparison      6   all wrong

**So the test for any claim here is one question: which instruction
consumes this value?** A claim that can name it was verified when it was
made. A claim that cannot is a hypothesis however reasonable it reads.
That is a provenance test; it needs no subject knowledge to apply.

The four situations that produce a wrong claim, all seen tonight:

  1. **A number matching a constant you already hold.** The frame table's
     words straddle 4096 and the world bias is 4096. The match is
     generated before any check runs (entry 49).
  2. **An unmarked boundary.** Tables do not declare their length, so any
     stopping rule is a guess. Mine merged two real dispatch tables into
     one false table of 20 valid pointers (entry 47).
  3. **An absence.** "Ghidra has not disassembled it" is not "it is not
     code" (entry 37). "There is no dirty site" is not "it is missed" —
     check whether the KNOWN-GOOD case is also absent (entry 43).
  4. **The first coherent reading.** Two instructions of a 124-byte
     function cohered into a name and I stopped (entry 35).

---------------------------------------------------------------------
## THE MAP, as far as it goes

**Object system** (28, 29, 32, 34, 38, 45). 64 slots x 128 bytes at
0xFFC000, dispatched at 0x398E through the routine pointer at $02.
Slot 0 is player 1, slot 8 player 2; slots 48-61 are the collision group.

    $00 status (bit7 active)      $2C flags (bit4 depth-sorted)
    $02 routine pointer           $2E bit7 horizontal flip
    $08 sprite slot -> pool       $2F priority band 0/1/2
    $0A/$0B palette slot/index    $30-$3A box A
    $0C long X 16.16              $3C/$3E CLAIM LOCKS (TAS)
    $10 long Y 16.16              $40/$44 saved X/Y
    $14/$16/$18 X vel/accel/limit $48/$49 from the animation frame
    $1A/$1C/$1E Y vel/accel/limit $4E zoom level (0-31)
    $21 anim frame index          $50-$5A box B
    $22 anim frame TIMER          $5C-$66 box C
    $23 actor mode (~9 states)    $78 word sentinel, only ever -1
    $24 anim script pointer

**Frame handshake** (22, 67, 68). The main loop clears 0xFFF01C and spins
at 0x3982; IRQ4 increments it at 0x2AC6. The clear DISCARDS a release
that arrived during work. 0xFFF144 counts overruns — **and the game
CLEARS it at 0x930**, which is why every 0.0% the port ever read was
meaningless. True rate is 50% (entry 68, `MISSKEEP=1`).

**Sprites** (13, 21, 48). One writer, the loop at 0x2B16: a 256-byte
order list at 0xFFEC80 indexes a 128-record pool at 0xFFF800, copying 12
of each 16 bytes because the hardware owns words 6 and 7. The order list
is **banded**: `band*64 + slot`, so walk order is draw order. Frame table
at 0x255E0 (6 bytes: a 16-bit OFFSET from that base, plus a long).
Zoom ladder at 0x20000, 32-byte rows.

**Tiles** (10, 11, 31, 56, 59). Two RLE passes, high bytes then low, so a
per-write conversion never sees a whole word — intercept at the loop
heads 0x16BE/0x16DE. **Cat-1 is bit 15, static per scene**, verified
20480/20480 against live tile RAM. Live page selects: FOREGROUND page 0,
BACKGROUND page 5 — measured, and the OPPOSITE of what the two code sites
say, because both are service-mode.

**Per-scene data** (46, 66). 17 tables indexed by 0xFFF142. Pointer
tables have exactly five valid entries and a garbage sixth — a reliable
shape. Named: scene descriptor 0x1CE2, floor geometry 0xDEC4, sky
palettes 0x32AE and 0x4050, music 0x1858, actor palettes 0x73DA/0x92EA/
0x173A0, level scripts 0x17E24.

---------------------------------------------------------------------
## TOOLS

    tools/ghidra/rebuild.sh        import + seed to the analysed state
    tools/ghidra/seed_harvest.py   dispatch tables + routine pointers.
                                   PASS --code OR IT HARVESTS PHANTOMS
                                   (2156 of 2435 sites are phantoms)
    tools/ghidra/func_profile.py   every function: fields, callees, regions
    tools/ghidra/classify.py       -> docs/audit/function_map.md
    tools/ghidra/bound_audit.py    bounding defects, measured correctly
    tools/hazard_census.py         arcade-behaviour dependencies
    tools/palette_demand.py        the sprite palette table
    tools/bake_cat1map.py          per-scene cat-1 bitmaps

Probe flags, all `make ship-us FLAG=1`:

    MISSKEEP    stop the game wiping its own miss counter  (BUILDER'S)
    SCENESEL=N  force every round to load scene N          (mine)
    CD_N=N      the same thing with no build at all: the lua writes the
                round->scene table into the cart region  (tools/cram_dump_scene.lua)
    MDSPRPROBE  blank the sprite copy to price it          (mine)
    FRAMEDONE   frame-complete signal                      (BUILDER'S)

---------------------------------------------------------------------
## OPEN

  1. ~~Scene 3 will not load under SCENESEL.~~ **CLOSED** (LOOP-DECOMPILE
     72): a stale rom, not a game fact. Scene 3 loads on the arcade and
     on ours, its pack is 8 palettes in two lines, and the dumps are in
     `discover/cram/scene3_*.bin`. Scene selection no longer needs a
     build: `CD_N=<scene> tools/cram_dump_scene.lua`.
  2. **The eleven bounding defects are diagnosed, not applied**
     (LOOP-DECOMPILE 73, `docs/audit/bound_repairs.md`): six functions to
     extend, five entries to delete because they are pointer tables and
     anim scripts, not code. Applying it means opening the analysed
     project — do it with the rest of a Ghidra pass, not on its own.
     `tools/bound_ref.py` re-runs the audit with no Ghidra at all.
  3. **~29% of rom data unattributed.** The two largest blocks are
     identified (sprite frame table, zoom tables) but not exhausted.
  4. **The dependency census** (entry 37) ran on two instruction classes.
     It is the piece that makes the NEXT TITLE cheap and it is the least
     advanced.
  5. **515 functions classified by signature, not read.** 150 are leaf
     helpers and are the least valuable thing left.
  6. I owe the builder a redo of my framebuffer bank figure against
     page 12.

---------------------------------------------------------------------
## THE TRAP THAT COST THE MOST TONIGHT

**Four times I built something the builder had already built from my own
finding.** MISSKEEP, FRAMEDONE, the overdraw-as-message-queue idea, and a
rewrite of `bake_tilecram.py` that mangled a working tool. Each time the
sequence was: I publish a finding, they act on it within the hour, I then
build it myself and the two collide.

**Before acting on any finding of yours, `git log` and read what they did
with it.** They move fast and they cite your entry numbers, so the
connection is easy to find. This is not politeness; my duplicate broke
their build twice.

Second: **a build that fails still leaves the previous rom in place.**
`cp rom/s16.32x` after a failed make copies a stale image, and it will
measure beautifully and mean nothing. Verify the artefact — run it and
read a value you know should have changed — not the build log.

Third: **a suspiciously stable number is uninitialised memory until
proven otherwise.** My frame-timing probe gave an identical beam position
across twelve frames. It was never running; the counter I added on
suspicion read zero.
