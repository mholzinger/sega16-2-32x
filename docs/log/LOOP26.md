# LOOP 26 — THE PALETTE-TRANSPORT ARC AND THE BLINK HUNT (2026-08-30)

One day, one arc, three convictions overturned by measurement. The
full numbers live in REBUILD.md (entries from "PALETTE-TRANSPORT
ARC, STEPS 1+2" through the CRAMFLIP field grade); this is the loop
digest.

## What shipped and held

- **PALDELTA** (R60 packet v3): the census read the truth first —
  palette writes land EVERY frame (glow streamer, 75% of writes)
  but only ~10 words/frame CHANGE; the 107-word payload was 10x
  block+redundancy amplification. Word-delta packet: push 145 -> 51
  words, word-exact on ares, tears heal force-raw.
- **The 68000-shaping lesson**: cutting 93 words bought 3 lines
  because the handler span was C-code cost, not words — 6 uint16-
  indexed block compares cost 55 LINES on a 7.67MHz 68000. Long-word
  post-increment walks + rotor byte-skip: handler 98.8 -> 72.2,
  game-68K share 61% -> ~72% (metric 1; target 80). The ship phase
  still runs ~310 cy/word vs ~70 raw — the mapped next slice.
- **RAMCODE r60_push** (principle, not speed — measured zero).
- **CRAMFLIP** (arc B half 1): remap CRAM paints deferred to the
  flip via an SPSC ring in the ISR. Rig-neutral-to-positive (best
  bq/D29 of the day); Mike-graded MARGINAL on the blink ("same,
  maybe slightly less frequent"). Stays flagged, unproven.

## What died, with numbers (NEGATIVE RESULTS)

1. **FM-gated FB palette staging** — census: 80% of game palette
   writes land inside the FM span. Conservation of pain, measured.
2. **The 0.65-lines/word push model as stated** — attribution
   error; the ship phase obeys it, the selection phase never did.
3. **Attempt 5, the R1 strip-phase gate** (R2's twin) — corpus
   2.33 vs 1.54 ungated: mid-action R1 SHOWS its deferred rows
   where ground-band R2 hides them. LAW COMPLETED: no ship-side
   deferral for R0/R1, either window. R2's tolerance is positional.
4. **Attempt A, the V-floor** (clamp the chain to the old phase) —
   swept 6100/5600/5300 ticks, ALL worse than both baseline and
   the disease (bad1 223-274, compose-skip to 58.8%): the old
   phase was a DISTRIBUTION (FIFO draining, early landings early);
   a clamp forces every window to the late edge and eats the
   jitter slack. A dead spin cannot reproduce a distribution.
5. **Two instrument classes**: launched sprites fire the seam-blink
   detector (width filter added, >50% columns); the "~8px vertical
   displacement" eyeball (cross-correlation: dy=0 — the blinks are
   COLOR events); capture.sh's silent 2:50 truncation (removed).

## The open class, precisely stated

Fight-scene band blinks at the gravestone rows: full-width COLOR
events, one captured frame, revert. Baseline 0.27/1000f, PALDELTA
0.94-1.54 (3-5x regression, real), CRAMFLIP 1.01 with a possibly
changed event mix (stones stable in the 1469 triptych; a sprite
silhouette flash + a sub-visible tint shift fired the detector).
Mike's anchor: frames 1690-1695 of the CRAMFLIP corpus. Candidates,
in order: stale ROW_DEFER rows displayed under the flip-paired
mapping (arc B half 2's case), the MD-plane pen path (mdp — the
CRAMFLIP queue covers only 32X CRAM), detector event-mix
conflation.

## Instruments added this loop (all live)

pal_fm_census.lua (MAME palette write tap), pal_check.py (PAL_SH ==
mirror equality gate), seam_stale.py (width-filtered blink rates),
the r60 push section stamps (0xFFA0B4..BE + ncmp/ndirty), the
chain-arrival census (0x28FF8/FFC), the CRAM remap/value census
(0x28FF0/F4), state_health ghost guards (MD_PAYOFF, the R60
window/ack caveat). DIAG collision #14 (cram_paint_spr vs the push
census on [52]) is KNOWN and unfixed — nrec means read inflated.

## Standing state

rom/s16.32x = 960d67b3 (PALDELTA + CRAMFLIP), Mike-played, healthy.
Metric 1 at ~72% (target 80, path mapped: asm compare ~15-20 lines,
ship loop ~2x). Boss smoke = the LOOP25 storm-flush arc, specimens
banked (his bs1-bs3). The autonomous session continuing from here
works the metric-1 finish under full rig gates.

## NATIVE — THE WHOLE-FRAME PIPELINE SHIPS (2026-08-31, branch native1)

Mike's verdict on the BQ coalescing build ("DID NOT EVEN MOVE THE
NEEDLE. Same stuck lower third") plus his directive ("gates should
be for an entire screen update") ended the band-machinery fix-loop.
NATIVE=1 (NATIVE.md is the design doc) deletes the band as a
schedulable unit:

  - ONE generation in flight = the whole frame. Launch latches ALL
    frame inputs at one point (regs, rowscroll, records, mdspr
    claim); the slave composes R0+R1+R2 from ONE self-chain command
    with cat1 AND the full text range inline — sbuf has exactly one
    writer per generation. Master tail = build_maps chunks only.
  - The blit ships ONLY closed generations (whole screen or
    nothing); the ISR flips ONLY freshly-blitted banks. Frozen
    bands, band tears, and generation-mixing are impossible by
    construction — there is no per-band ship path in the binary.
  - Compiled out: bq[8]/BQ_PUSH/strip executor, CHAIN_ADVANCE,
    ROW_DEFER + both hazard gates, cat1 deferral, AUTO-30. RAMCODE
    freed ~1.4KB (guard margin 112 B -> ~1.1KB).

Three bugs found by the battery on the way (each measured, not
guessed):

  1. IDENTICAL-CMD WEDGE: the whole-frame cmd was bit-identical
     every launch; the slave's post-echo wait (while SYNC[0]==cmd)
     read a missed 0->repost transition as its own command still
     standing. 195 of 340 generations wedged into the 8-window belt
     (D27). Fix: generation parity bit (cmd bit 3, unread by every
     consumer). Wedges -> ~1 per 13.8k frames.
  2. MASTER-TEXT RACE + TAIL SPILL: the master's text stages could
     land before the slave's row clears (the old bq spaced them by
     accident); and the maps drain sat behind the old dt<=8000
     heavy-slot deadline (built for the 4ms UNINTERRUPTIBLE
     build_maps) at one chunk per visit — a whole vint of critical
     path. Fix: text moved whole to the slave; maps chunks loop to
     dt<=11300. Gen wall 1.74 -> 1.22 vints.
  3. SCHEDULE QUANTIZATION: closes landing during the flip span /
     landing wait missed the window's blit slot by microseconds and
     waited a full vint. Fix: last-call close check directly before
     the blit gate. (Plus: the ISR's nat-hold decline suppresses the
     body-fallback flip_span — the double capture pass was 1190
     wasted in-FM calls per 1900f.)

NUMBERS (1900f play2 battery vs canonical 62004359): content
generations 979 = 32.6fps WHOLE-FRAME LOCKSTEP (canonical: ~50%
compose-skip, band-sliced); bad1 90 vs 202; rejects 1.55 vs 2.34%;
bq drops 0 (no queue); handler 70.4 vs 69.4. 13800f full-level run:
30.5fps mean, 1 wedge, 0 skips, handler 69.6. Per-band burst
9800-10040: NATIVE R1/R2 frozen-pair 0%/0%; canonical same window
R1 20% / R2 2% — independent band freezing visible even in an
ordinary graveyard scene.

LEDGER: the SELF-CHAIN 2026-08-25 negative did NOT reproduce — bad1
went DOWN with back-to-back slave compose. The negative was measured
under the 151-word push; PALDELTA (~51 words) + the hoisted landing
wait changed the bus economy it rested on. The negative stands FOR
ITS ERA; NATIVE.md records the differences.

GEN-WALL CENSUS at 0x28F50/54/58 (sum/count/max, PICKUP_SRC scrap).
The 60Hz path from here: the wall is 1.22 vints and the window is
~0.65 — the levers are master row rebalance (BANDSHIFT) and the P3
MD-sprite offload taking pixels off the slave. Timeline note: NATIVE
shifts window timing, so canonical-tuned input CSVs diverge
(identity law); play_level1.csv no longer reaches the boss.

roms: rom/s16.32x = NATIVE (BUILD e0549e86); canonical preserved at
rom/s16_canon.32x. Mike's play pass arbitrates — the boss smoke
lower third is the acceptance scene.

## MIKE'S PLAY PASS — NATIVE BANKED (2026-08-31, same day)

Verbatim: "BANK THIS WORK! This is 100% an improvement! Speed,
tearing, all of it. It's not perfect but brother - we are almost
there. ... THIS is the arc we needed."

First play-pass win of the arc after four band-machinery failures.
The whole-frame scheduler is now the SHIPPING architecture on this
branch. The verdict rom is BUILD e0549e86 (the pre-squash hash baked
into rom/s16.32x and rom/s16_native.32x — the squash below renames
the commit, not the bytes; keep those bytes as the passed artifact).

"Not perfect" — the open list going into the next arc, in Mike's
priority order (speed first, per the standing queue):
  1. 60Hz: gen wall 1.22 vints vs the ~0.65 a same-vint close needs.
     Levers: BANDSHIFT master row rebalance (the master's gap is
     ~0.5 vint of maps against the slave's 1.05+ of compose) and the
     P3 MD-sprite offload (every claimed record leaves the slave's
     wall entirely). Both compose ON TOP of NATIVE unchanged.
  2. The pre-existing cosmetics NATIVE inherited: displaced cloud
     strip (parked cell-mode hscroll / per-band fine-x), the red
     transform orb (rebase-layer sprite corruption), grass-blink
     residual class — all unchanged from canonical, all still open.
  3. Timeline retune: canonical-era input CSVs (play_level1) diverge
     on NATIVE timing; re-record against this build when the rig
     needs the boss scene again. Mike's arcade ref capture
     (tools/ref_dump.lua) gives the oracle frames to grade against.

## UNATTENDED SESSION ADDENDUM (2026-08-31 afternoon)

Mike's arcade reference captured and banked (ORACLE.md): 17,528
frame-true PNGs, coin-in through round clear. Graded — the walk is
already at arcade cadence; the boss fight is the true-60Hz scene and
the arc target. BANDSHIFT master-rebalance measured DEAD (4 points,
ORACLE.md table); machinery stays in-tree as a no-op. P3 claim rate
on NATIVE measured: 0.57 records/gen of ~3.6 (16%) — the one-CRAM-
line limit, unlocked by the static-palette arc.

Also: play_native2/3.csv — continues-grind bots that survive the
level INDEFINITELY on the NATIVE timeline (banked credits + blind
START presses). They never transform (orbs lost to deaths), so the
boss stays unreached on-rig; they are soak/heavy-scene assets. The
boss scene remains graded by Mike's captures.

Hygiene: the wip auto-commit hook swept mamecap/ + ref_arcade/
(1.2GB) into history; re-banked without them (4ccdc10, content
identical), gitignored both, git gc 8.1G -> 2.5G. And the zsh
unquoted-$VAR trap struck the Makefile flags — arrays only.

## PALSTATIC v1 SHIPS TO TEST (2026-08-31 night)

Mike's blue-white/silhouette conviction (scene-cut smear) -> v1 built
the same evening: per-scene PAL_SH images baked (3 level-1 scenes:
normal / boss_smoke / transform — the tile+text half only; the sprite
half is per-moment actor state, measured 217-vs-34 diff split) with
4-word detect probes. On detect: the whole tile+text palette loads in
ONE window + every tile/text generation bumps (apply_cram repaints
same-window) + an 8-gap shadow-LUT rush. Corpus: rig harvests + the
transform cutscene FOUND IN THE ATTRACT (~frame 1800) + boss palettes
extracted from Mike's savestates. Tables live in cart .palscenes
(mdsprart slot's free upper half; new mars.ld section + asserts).
Battery: zero regression (ships 976/wall 1.21/bad1 88). Attract A/B:
3 switches fire, correct scenes, no introduced damage — but the
attract entry FADES (game-side) so it cannot reproduce the hard-cut
smear; the LIVE transformation is the acceptance scene and only
Mike's play reaches it. v2 (atomic picture: shadow NT + plane-base
flip; sprite-pair static zone -> freed CRAM lines -> P3 claims) per
PALSTATIC.md. Note: in-scene REMAP churn unchanged by design (44k on
the attract run both arms) — that class is v2's allocator retirement.

## ROTOR BACKOFF: MEASURED MISS, BETTER TARGET FOUND (2026-08-31 late)

Streak backoff on chronic equal-compare palette blocks (visit 1-in-4
after 4 equal visits): handler mean 71.8 vs 70.4 baseline = inside
the +/-1.5-line cross-build noise floor. REVERTED. The finding that
matters: the glow streamer's blocks CHANGE nearly every vint (the
census's "91% redundant" is per-WRITE; per-block the ~10 changed
words/frame land across those same 6 blocks) — the rotor's 30-33
lines are LIVE delta work, not waste, and no skip scheme can cut it.
The real diet: the glow is a DETERMINISTIC PALETTE CYCLE — bake the
cycle, run it SH-2-side from its own clock, and mark its blocks out
of the 68K rotor entirely (rotor -> ~8-10 lines, handler ~50, game
share ~80%). Folded into PALSTATIC v2 alongside the static sprite
pairs. Instrument caveat banked: handler-mean deltas under ~1.5
lines across rebuilds are layout luck — A/B within one build or
demand >2-line effects.

## PALSTATIC v1 PULLED — MIKE'S EYE + SAVESTATE CONVICTION (2026-09-01)

Mike's play: standing gray/desat gameplay AFTER the wolf transform
(frames 4429-4433) + blue gravestones in the ATTRACT. His savestate
closed the case in one read: live PAL_SH = the baked BOSS image (11
words off) during normal gameplay, 4 switches fired. TWO design
flaws, both mine:
  1. FADE-TRANSIENT ALIASING: 4-word probes match scenes a fade
     passes THROUGH — and the boss scene is a desaturation, i.e.
     the most fade-aliasable target possible.
  2. UNHEALABLE BY CONSTRUCTION: the SH-2-side PAL_SH load is
     invisible to the 68K's PALDELTA shadow — game staging equals
     shadow, so NO deltas re-ship and a wrong load STANDS FOREVER.
     A 10-20 frame smear was traded for a permanent wrong palette.
rom/s16.32x REVERTED to the play-passed NATIVE build (e0549e86).
v1.1 requirements before this ships again (PALSTATIC.md updated):
  a. HEAL CHANNEL FIRST: a scene load must post a full re-mark +
     force-raw request to the 68K (BAD1-family COMM8 code) so ANY
     wrong load self-heals in ~8 vints — the belt precedes the
     mechanism, like ROW_DEFER's backstop preceded the gates.
  b. Confirmed detect: 8-word probes + the same scene matched on 3
     consecutive landings before a load.
  c. Rig gate that traverses fades: scripted attract pass must show
     ZERO switches outside the two cutscene boundaries.
LAW (palette-transport family): never install SH-2-side palette
state the 68K's differ cannot see — every divergence must have a
re-ship path, or one glitch becomes permanent.
