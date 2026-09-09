# START HERE — read this before anything else

Written 2026-09-09 after a session that lost the plot. If you are a new
session, this file replaces `HANDOFF-PIPELINE.md` as the entry point.
That file is still accurate about the transport but it let a session
spend a night optimising the wrong number.

## THE BAR. There is only one.

**60 frames per second REACHING THE SCREEN.** Two numbers say whether a
build has it, and a build needs BOTH:

    tools/presented_fps.py ROM    MOTION: substantial picture changes
                                  per 60 frames. THE PLAYER'S FRAME RATE.
    tools/gameplay_speed.py ROM   the 68K's logic keeping up with its
                                  own frame. A PRECONDITION, NOT THE BAR.

    accepted line today   logic 49.7%   MOTION 1.3 fps
    the bar               logic  100%   MOTION  60

**CORRECTED 2026-09-09 16:30 (LOOP29 113). The paragraph that stood here
said the logic rate was the only bar and explicitly ruled out "flip
rate... screen updates per second." That was wrong, it was written into
this file at 01:23, and an overnight loop then ranked four hours of
builds on it.** The accepted rom scores 49.7% logic while showing ZERO
changed pixels across 60 consecutive frames — its scene timer advanced
350 ticks over the same second. Mike, unprompted, the next morning: "I
haven't gotten a single build from you that has moved frames."

The two numbers diverge because the port is single-buffered under
FBXPORT: the master composes into the bank being displayed, so a long
compose shows as a still frame no matter how fast the 68K runs. Rank on
MOTION. Use the logic rate for what it measures — whether the 68K
finished its pass — and never as a substitute for looking at the screen.

LOOP28 93 measured this same divergence on 2026-09-08 and Mike's play
pass overruled the gate then too. It has now cost two sessions. If a
future document tells you one number is the whole bar, check it against
`presented_fps.py` before you believe it.

## THE SCOPE

The 68000 clock is not the loss. The game needs 2780 instructions per
vint and the budget covers it. **Our pipeline costs about 2882
instructions per vint — as much as the game itself.** That is the whole
gap between 49.7% and the bar. Everything else is a detail.

## THE ACCEPTED ROM

    make ship-us FBXPORT=1          # tag mister-keeper-20260908

FBXPORT is not optional: without it the port is a BLACK SCREEN on real
hardware, which ares does not show you. Every flag added since is
default-off.

## DEAD ENDS — do not re-open without new evidence

  - **Double buffering** (`FBXSTAGE`, `FBXBOTH`, `FLIPEDGEOFF`). Mike's
    hardware verdict: the single-buffered line does NOT tear, so it
    solves nothing, and it costs 15 points of game speed. He played it
    on ares and MiSTer: "painfully slow", both rigs agreeing.
  - **`PALSTREAK` / `PALBACKOFF`.** Were dead code until 2026-09-08 (the
    counter was never written). Fixed, swept 25 ways, no setting wins.
  - **`TEXTCAPMASTER`.** Needs `TEXTCAPFULL` or it renders confetti. Even
    correct it buys nothing on the shipping line.
  - **`PALNOCMP`, `CLAIMNEW`, `FBXTAIL`.** Re-measured twice each in
    2026-09-09; every negative holds.
  - **`BGBLANK0`.** A different bug's workaround. See the open bug below.

## HOW TO MEASURE WITHOUT FOOLING YOURSELF

Four rules, each of which was learned by breaking it:

  1. **`gameplay_speed.py` measures LOGIC.** It is deterministic for a
     given source — two clean builds of the same code agree exactly —
     but it swings **18 points on code layout alone**. So an A/B is
     valid only when the builds differ in the thing under test, and
     never trust a gap under ~6 points.
  2. **`anim_rate.py` counts CHANGE, not correctness.** A build
     rendering confetti scores three times the shipping line.
  3. **LOOK AT A FRAME.** Colour count, black fraction and edge density
     were all in range on a frame that was visibly broken. There is no
     cheap automatic substitute. `tools/attract_parity.py` against
     `ref_arcade` is the only oracle that judges pixels.
  4. **Divide before you claim.** Two wrong conclusions in one session
     came from reading a total as a rate. 136,411 writes over 3000
     frames is 45 a frame, and they are all in 0.3% of the frames.

## THE ONE OPEN BUG, LOCATED

The attract title screen never renders: the arcade shows a full-screen
eye, we show the demo's graveyard with the title text over it. Measured
at the same frame — our name table in SDRAM goes from 341 distinct
background slots to 4, correctly. **MD VRAM Plane A follows it (148 ->
2). Plane B does not (338 -> 207).** The background name table is
computed right and never reaches Plane B. The walk, the capture, the
SH-2 cache and the page selects are each measured innocent.

Next probe: instrument Plane B's upload against Plane A's at the frame
the scene changes. Both come from the same pass, so the difference
between the two upload paths is the entire defect.

## THE WORKING LOG

`docs/log/LOOP28.md` is this session. Entry 94 is marked WRONG in place
and corrected by 96; entry 104's conclusion is superseded by 106. Read
the corrections, not just the entries.
