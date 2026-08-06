# LOOP 10 — the purple hue, then the compose overrun

Kickoff doc for a FRESH session. Self-contained: read this, then LOOP.md
(negatives list, especially 20-24) and LOOP9.md's two PATH sections.
**Do not read LOOP9.md top-down as a plan** — its opening frames the arc
around the blit being the bottleneck and ranks fixes off MAME numbers.
Both premises died in LOOP 9. The corrections are further down that file.

MIKE'S PRIORITY ORDER for the port: **framerate, screen tearing, sprites,
sound.** LOOP 9 moved 1 and 2 and did not finish them. Sound is untouched
and last.

## Where LOOP 9 left it (commit 47e992d)

The game is playable and Mike's verdict on the landing build was "lots of
flicker, some screen tear, but very playable" — better than the build
before it. That is the first thing the smoothness arc has won.

    ares, per window, vblank = 38 lines
                blit     wait    total   in-compose pickups
      k=0     27.08ln  7.00ln  34.08ln       35.2%
      k=1     30.05ln  0.20ln  30.25ln        0.0%
      k=2     27.09ln  0.33ln  27.42ln        2.4%

    restore past vblank  8.3% of windows (heavy scene), worst 67 lines
    vints/cycle 3.24    V-gate rejects 7.3%    blit skips 28.3%
    worst handler  total=149  window/ack=104  tail=45   (frame=262)

## THE TWO NUMBERS THAT DEFINE THIS ARC

**0.744 lines per blitted row, in every window, every session, light or
heavy.** The blit is a fixed 6.76 MB/s hardware floor. It is not the
bottleneck, it is not load-dependent, and nothing instruction-side moves
it — DMA measured 1.77x SLOWER. Stop optimising it.

**7.00 lines of wait in one window out of three.** That is the 80-row
band's compose failing to finish inside its one window gap. It scales
with sprite count. It is the largest recoverable term left and it is
PATH 1 below.

## PATH 2 FIRST — the purple sprite hue (correctness)

Mike, playing ares: **a purple hue on sprites, reduced but not gone**
under LOOP 9's change, plus grass that glitters. LOOP 8 recorded this as
a MAME-PRESSURE-only artifact that "does NOT reproduce on ares". That
line is wrong — PRESSURE was an early warning read as noise.

Mechanism understood, fix already designed: the palette lands at k0/k2
but `apply_cram` paints CRAM only at k1, so a shipped pair reaches CRAM
up to a full cycle late and the per-set memo holds the stale paint until
the next generation bump. Disabling the memo restores the colour and
costs PRESSURE demo2 40.30 -> 28.47, which is why the memo exists.

THE FIX: paint the shipped sets into CRAM in the window the pair LANDS.
It is already pre-ack there, so it is legal. Second, smaller lever from
LOOP 9's Mission 1 item 2: LOOP 8 made PAL_SETGEN bump 32 sets whenever
a region pair ships where the old path bumped the 1-2 that changed —
bump only the sets whose words actually differ.

WHY THIS ONE FIRST: it is correctness, and the standing directive is
accuracy before speed. It is also small, where PATH 1 will churn the same
code the palette timing rides on — do it second and you measure the
palette through a moving floor.

apply_cram is 0.673ms/cycle on ares (2.02x its MAME reading), so this may
pay in the window too, but that is a bonus, not the case for it.

## PATH 1 SECOND — the 80-row compose overruns its window gap

The compose launched post-ack at window k runs during k+1 and is drained
at k+1's post-ack: ONE window gap. Bands are 72/72/80 because 224 rows is
28 tile rows and 28 does not divide by three. The 72s fit; the 80 does
not, so 35.2% of the overlapping window's mailbox pickups land inside a
running compose and the master sits on SYNC[2] for 7.00 lines.

Two candidates, neither costed:
  a. **Split the big compose across two window gaps** — needs a resumable
     compose or a second outstanding slot (SYNC[0]/SYNC[1] carries one
     command). This is the structural piece of work.
  b. **Even the COMPOSE bands without moving the SHIP bands.** The two are
     independent — compose_layer takes arbitrary row ranges, and only the
     ship set must be composed by pickup. 74/75/75 compose against
     unchanged 72/72/80 ship has never been tried.

DO NOT REACH FOR MORE MAILBOX-POLL POINTS. That is iteration 7f: it
bounded pickup latency, hit its target metric, and lost the arc on sprite
artifacts. Polling more often cannot shorten work that has not been done.

MEASURE IT ON SPRITES. Mike: flicker is constant when the level-1
gravestones rise. Sprites are what the compose spends its time on, so a
heavy-sprite scene is the input — the title screen will show nothing.

## Gates (every commit)

1. Boots + full attract, NO hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build. **Judge the STATICS**
   (title, eyehold) — they are render truth. Motion scenes are phase-noisy
   and ares overrules them. Reference, commit 47e992d: title 2.44,
   eyehold 3.37, scream 47.14, demo 51.47, demo2 21.85, TOTAL 25.26.
3. Region guard (`grep ' _end$' rom/s16.lst`, limit 0x06019000; currently
   0x06018c48, 952 bytes headroom), rom stamped `normal`, `make PRESSURE=1`
   builds and runs.
4. **PLAYABILITY PRESERVED — Mike's ares pass is the acceptance gate**, and
   it has overruled the metrics twice.

## Falsifier

PATH 2: the purple hue is gone on Mike's ares pass, with PRESSURE demo2
no worse than 28.5 and the statics unmoved.

PATH 1: `tools/wait_split.py` — the 7.00-line wait under 2 lines and
in-compose pickups under 5% in a heavy-sprite scene, with worst-window
total under 30 lines. Cross-check black-frame rate with
`./capture.sh raw` (NOT dedup — see below) from the SAME run as the
savestate.

## HARD-WON LESSONS (do not re-learn these)

- **MAME DOES NOT MODEL FRAMEBUFFER WRITE COST.** Cached and uncached FB
  aliases read byte-identical there; ares charges 5x. Per-term ares/MAME:
  blit 5.05x, copy_pages 8.92x, apply_cram 2.02x, TOTALwin 3.80x — every
  term understated by a DIFFERENT factor, so MAME cannot even RANK them.
  Iterate on MAME for CORRECTNESS; take every timing verdict from ares.
  Budget for the round trip. (TOOLKIT.md has the null test to re-run this
  on any future title.)
- **NEVER LEAVE A PROBE OR A LOSING CANDIDATE AT `rom/s16.32x`.** A build
  with the deliberately-slow uncached FB path sat at the default rom path
  for 21 minutes and Mike played it; an hour of analysis went into
  explaining flicker that the diagnostic had caused. Probe builds get
  their own `rom/PROBE_*.32x` filename, always.
- **A CONFIRMED MECHANISM IS NOT A LICENCE TO SHIP THE FIRST FIX.** 7f and
  LOOP 9's blit-thirds rebalance both hit their target metric and lost the
  play pass. Four builds in LOOP 9 passed every mechanical gate and lost.
- **AN A/B IS ONLY AN A/B IF THE CONTENT MATCHES.** Single-session ares
  numbers vary ~14x by content. Two savestates read 0.2% vs 10.1% on the
  same metric and it was a light scene against a heavy one, not a result.
- **SAVESTATE COUNTERS ARE CUMULATIVE FROM BOOT; a capture is a slice.**
  0/655 overruns alongside 12.4% black frames is not a contradiction if
  the state was saved before the footage. Save the state at the END of the
  run you captured.
- **`./capture.sh raw` for any per-frame RATE.** Dedup deletes frames that
  match their neighbour, but an overrun frame differs from its neighbour
  by definition, so the survivors are enriched for exactly what you are
  counting. LOOP 7i's phantom "3.3x second cause" was this.
- **STATE THE INVARIANT, THEN MEASURE IT.** The code asserted for nine
  iterations that the blit set and the outstanding compose were "DISJOINT
  by pipeline construction". They were the same band, in all three
  windows, the whole time.
- **"BUSY" IS NOT "COLLIDED".** LOOP 9 diagnosed a data dependency from a
  probe that only measured whether the slave was inside its compose. The
  fix worked and the diagnosis was wrong. Check what a counter counts.

## Probes available (all `make FLAG=1`, all NEVER SHIP)

    WINSPLIT   blit-only vs post-blit waits, rows blitted   win_probe.lua
    SPANPROBE  pickup-V + blit-span histograms, per-k       span_hist.py
    WAITSPLIT  per-window blit vs SYNC[2] wait              wait_split.py
    PICKUPSRC  slave answers mailbox in-compose vs idle     wait_split.py
    ROWSTALE   rows identical to one cycle ago              win_probe.lua
    FMTEST     do SH-2 FB writes land with FM=0             (inline python)
    TAILPROBE  MD tail split + means                        win_probe.lua

Counters live in DIAG (0x28000, 64 slots, 0-61 used) and the 28D00-28FFF
hole. Put new ones where a SAVESTATE reader can find them — ares is the
target that matters and it cannot be scripted.

## Ruled out — do not re-propose (LOOP.md 20-24 has the numbers)

- DMAC channel 1 for the blit: 1.77x slower on ares, 14% of rows dropped.
- Dirty-row blit: 13-17% skippable during scroll against a ~25% break-even.
- Evening the blit thirds: seams, reverted on the play pass.
- Composing straight into the FB / shadow bank: FM=0 writes land only
  85.8% of the time, which is worse than a flat no.
- Late window pickup: contributes 0 of the overruns.
- Striping the cat1 pass (7f), splitting past thirds, skip-on-predict,
  atomic ship.

## Not in this arc (open, ranked)

1. **SPRITES** (Mike's #3) — next correctness arc after smoothness.
   `tools/objdiff_32x.lua` differs object slots against the arcade driver.
2. **blit skips 28.3% of cycles** — each is a stale third. Re-measure
   after PATH 1 before treating it as its own arc.
3. **SOUND** (Mike's #4). No Z80, no PWM. Untouched, and last.
