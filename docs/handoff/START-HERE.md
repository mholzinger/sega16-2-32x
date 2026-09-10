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
vint and the budget covers it.

**CORRECTED 2026-09-10 (HANDOFF-20260910 section 2b, LOOP29 117/123/124).
This section used to end "our pipeline costs 2882 instructions per vint,
that is the whole gap". It is not the gap.** Measured per generation —
and a GENERATION IS 3.6 VINTS, not 1:

    MASTER accounted work    0.44 vints/gen   the master is IDLE
    SLAVE compose            1.40 vints/gen   0.0 idle polls per vint
      of which cat1 tiles    0.67             = 48% of it
    MASTER maps drain        0.95 vints/gen

**The 68K is not the frame-rate constraint and has not been for some
time.** The SLAVE is the saturated processor. That retires a week of
shim/rotor/FM/transport work which moved the logic rate and never the
picture — and 39% of the frame budget is the slave PAINTING IN SOFTWARE
what the MD VDP would draw for free.

## THE ACCEPTED ROM

    make ship-us FBXPORT=1          # tag mister-keeper-20260908

FBXPORT is not optional: without it the port is a BLACK SCREEN on real
hardware, which ares does not show you. Every flag added since is
default-off.

## DEAD ENDS — do not re-open without new evidence

  - **Double buffering** (`FBXSTAGE`, `FBXBOTH`, `FLIPEDGEOFF`) — **NO
    LONGER A DEAD END, and this entry was wrong. Corrected 2026-09-10.**
    It was dead-ended for "costing 15 points of game speed", which is the
    LOGIC metric this file's own bar section now disowns. Ranked on
    MOTION it is 1.3 -> 16.7 fps, a 13x improvement, and Mike's MiSTer
    pass called it "an order of magnitude improvement... not enough
    frames to be playable" and later "more concurrent frames displayed
    but no speed improvement".
    It is a REAL TRADE, measured: `FBXSTAGE+FBXBOTH` costs 2.6 points and
    buys almost nothing (the vblank edge guard declines most flips);
    `FLIPEDGEOFF` buys all the motion and costs 12 points of game speed
    (47.1 -> 34.9). Frames OR speed, and nobody has got both.
    **Where those 12 points go is MEASURED (2026-09-10, LOOP29 136):
    with the guard off the FS write lands mid-scan, both ares and the
    FPGA defer the latch to vblank, and the master spins on that latch
    with FM held (~198 lines). The game's text writer waits on FM to
    the end of the frame and the next vint enters late; 33 of 33
    deferred flips did this.** BUILT AND MEASURED, LOOP29 137: the fix
    was not to release FM but to get the FS write INSIDE vblank with the
    guard on — stop capturing the two packet regions as page truth
    (23 lines of drain per flip), lift the packet before the flip on the
    ISR path, and move the 68K's packet blast out of the pre-post slot
    into the FM-gate spin. `make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1
    FBXISRLIFT=1 PGSKIPPKT=1 TEXTCAPMASTER=1 TEXTCAPFULL=1` =
    `rom/night/vi2.32x`: ~29 Hz flips, none deferred, logic 50.1%.
    vi.32x (LOOP29 137) drew black backgrounds on Mike's pass: the
    packet sat in tilemap page 0, the BACKGROUND page — LOOP29 138 moves
    it to page 12 and the attract title eye renders clean for the first
    time. **Awaiting Mike's play pass on vi2; not the accepted rom.**
    The build is `rom/night/dblfast_clean.32x`, on the MiSTer as
    `dblfast-20260910.32x`.
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

## THE LAST SESSION

`docs/handoff/HANDOFF-20260910.md` — the 2026-09-09/10 rendering session,
in full: the FBXPORT motion regression and how eight days of instruments
missed it, the frames-or-speed trade the double-buffered line makes, where
a generation's 3.6 vints actually go (the master is idle, the SLAVE is
saturated, cat1 tiles are 48% of it), the palette question closed by
exhaustive search, the five-item diagnostic refactor, and a full account
of four retracted claims with the failure pattern behind them. Read
section 4 before trusting any LOOP29 entry and section 7 before writing a
probe.

`docs/log/LOOP29.md` entries 107-135 is that session's log.
`docs/handoff/HANDOFF-DECOMPILE.md` briefs the parallel Ghidra thread;
`docs/log/LOOP-DECOMPILE.md` is its log.

## THE WORKING LOG

`docs/log/LOOP28.md` is this session. Entry 94 is marked WRONG in place
and corrected by 96; entry 104's conclusion is superseded by 106. Read
the corrections, not just the entries.
