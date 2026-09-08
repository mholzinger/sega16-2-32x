# NATIVE — the whole-frame pipeline (branch native1)

2026-08-31. Mike's directive, verbatim: "Drawing the the screen should
have NO delay on topp, middle or bottom. Whatever you are trying to
fix is LEGACY. ... Thewy should be for an entire screen update. The
entire game is sprites and scaled sprites."

Four consecutive band-machinery fixes (R1 strip gate, CRAMFLIP x2, R2
defer bound, BQ coalescing) failed his eye on the frozen-lower-third
scene. The coalescing build collapsed bq-drops 716-1900 -> 207 with
every gate green and he reported ZERO visible change. The lesson is
structural: the defect class is not any one gate — it is that the unit
of scheduling (a band) is smaller than the unit the player perceives
(a frame). This flag deletes the band as a schedulable unit.

## What NATIVE=1 changes (SH-2 only; the MD shim is untouched)

ONE generation in flight, and the generation is the WHOLE FRAME:

1. **Launch**: at the k2 post-ack, if no generation is open and none
   is awaiting its blit: latch the frame inputs (layer regs +
   rowscroll via latch_layer_regs, sprite records SPR_LAND ->
   SPR_SNAP, mdspr claim), then ONE slave command — the self-chain
   form (CMD_TILE|0x40|genbit): the slave composes R0,R1,R2 back to
   back with cat1 AND THE FULL TEXT RANGE inline (NOCAT1DEFER=1 +
   the NATIVE range widening in slave_concurrent_k) — **sbuf has
   exactly one writer during a generation**, which deletes the
   master-text-vs-slave-clear race class outright. The master's
   tail is maps-ONLY (build_maps chunks looped per poll visit up to
   the dt<=11300 quiet zone — the old dt<=8000 heavy-slot deadline
   guarded the 4ms uninterruptible build_maps and put the chunked
   drain on the generation's critical path for a whole vint). No
   queue, no chain relaunch, no drops — there is nothing to drop.
   THE GENBIT (cmd bit 3, unread by every consumer) is load-bearing:
   the whole-frame cmd is otherwise bit-identical every launch, and
   the slave's post-echo wait loop (while SYNC[0]==cmd) reads a
   missed 0->repost transition as its own command still standing —
   the first battery wedged 195 of 340 generations exactly this way.
2. **Close**: generation closed = slave echo landed AND master tail
   done. Detected in the poll loop and at window pickup.
3. **Ship**: the blit (both halves, the existing SYNC[4] flow) runs
   ONLY on a window with a closed, unshipped generation — the whole
   screen or nothing. sbuf can never ship mid-compose because a new
   generation cannot launch while one awaits its blit.
4. **Flip**: the V-ISR flips ONLY a freshly-blitted bank
   (nat_shipped, consumed at flip_span — the DIRECT_FB stage-1 gate
   pattern, DIAG[29] counts holds). A declined flip holds the last
   WHOLE frame.

Cadence: 60Hz when compose closes inside a vint; whole-frame-coherent
30Hz when it does not. Never a band, never a frame assembled from two
game states. Every input of a displayed frame (records, scroll,
rowscroll, MD-plane scroll packet) is latched at ONE point.

Alignment of the MD planes with the FB: the launch latches regs, the
same window's gap-prep builds the MD scroll packet from that latch,
the next window publishes it, the 68K consumes it at the vint after —
which is the same vint the generation's flip lands. Planes and
sprites move together.

## What compiles out under NATIVE_FRAME

- The band queue (struct band bq[8], BQ_PUSH, the strip executor,
  drop_s0, phase machine) — the master poll branch shrinks to the
  mtask chunker + the existing maintenance slots.
- CHAIN_ADVANCE and the per-band relaunch (2 x ~163-line round trips
  per frame of pure latency, LOOP26 profiler).
- ROW_DEFER and both hazard gates (NO_ROW_DEFER forced): nothing
  ships mid-generation, so there is nothing to defer.
- The cat1 deferral (NOCAT1DEFER=1 forced): cat1's deferral existed
  to dodge the ship racing the compose; the ship no longer races.
- The AUTO-30 launch skip machinery (replaced by the generation
  state) and the post-ack slave_wait drain (the launch site clears
  SYNC[0] itself).

## Ledger interactions (read before re-judging old negatives)

- **SELF-CHAIN negative (2026-08-25, four park variants dead)**: that
  measured back-to-back slave compose TRAMPLING the DREQ landing
  under the 151-word push era. NATIVE re-enters that structure with
  the world changed: PALDELTA cut the push ~3x, the master's landing
  wait (192-tick stability, 4000 bound) still runs on every window
  (hoisted OUT of the gated blit block — the stage-2 A/B lesson),
  and the master still parks on the announce. bad1/rejects on the
  battery arbitrate; if they blow up, the next lever is a slave park
  keyed to SYNC[12] (already broadcast by the ISR) — but only on a
  measured verdict.
- **Complete-or-defer law**: preserved, promoted from band to frame.
- **Drop-beats-tear law**: preserved (flip declines, edge guard
  untouched).
- **Stale-beats-torn law**: preserved (a torn landing keeps last
  generation's SPR_SNAP: nat_spr_ok).

## Counters (state_health/ares_gate read these)

- DIAG[13] (bq drops): structurally 0 under NATIVE.
- DIAG[29]: NATIVE flip holds (was ROW_DEFER row-defers — ROW_DEFER
  is compiled out here, the slot is free per the 4346 comment).
- DIAG[30]: generation overran a vint (was AUTO-30 compose-skips —
  same meaning, frame-sized now).
- DIAG[27]: slave echo timeout OR the NATIVE wedge belt firing
  (8 consecutive overrun windows force a fresh launch).

## Measured (2026-08-31, BUILD e0549e86, headless-ares batteries)

1900f play2 battery, NATIVE vs the shipping canonical (62004359):

    metric                NATIVE      canonical
    content generations   979 = 32.6fps whole-frame, lockstep
                                      ~50% compose-skip, band-sliced
    bad1 (torn landings)  90          202
    rejects               1.55%       2.34%
    bq drops              0 (no queue exists)   207
    68K handler mean      70.4        69.4      (+1 line)
    skips / flip-late     0 / 0       0 / 0
    gen wall (launch->close) mean 1.22 vints, census at 0x28F50

13800f full-level run: 30.5 content fps mean, 1 wedge, 0 skips,
rejects 0.39%, handler 69.6 — parity with canonical. Per-band burst
analysis (9800-10040): NATIVE R1/R2 frozen-pair rate 0%/0% (lockstep
by construction); canonical same window read R1 20% / R2 2% —
independent band freezing, the Mike-visible disease, present even in
an ordinary graveyard scene.

The SELF-CHAIN 2026-08-25 negative did NOT reproduce: bad1 went DOWN
(202 -> 60-90) with the slave composing back-to-back — the PALDELTA
push diet (151 -> ~51 words) plus the hoisted landing wait changed
the bus economy the negative was measured under.

Known non-regressions carried over (verified same on canonical):
the displaced cloud strip upper-left (parked cell-mode hscroll, the
per-band fine-x gap), the red transform orb (rebase-layer sprite
corruption). Both pre-date NATIVE.

Timeline note: NATIVE changes window timing, so scripted inputs
diverge from canonical runs (the identity law). play_level1.csv no
longer reaches the boss on this build; per-band lockstep was gated
on the heavy graveyard scene instead. The boss-smoke scene is Mike's
play pass to judge — structurally, a frozen lower third now requires
the WHOLE display to freeze (there is no per-band ship path at all).

## Canonical NATIVE line

    make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 \
         R60=1 CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40 BLITSKIP=1 \
         DIRTYROW=1 SPRBAKE=1 BLITSHIFT=24 SPRLATE=1 PRHOLD=6 \
         TILECLASS=1 TXTCLASS=1 MDSPR=1 ROWDEFER=1 PALDELTA=1 \
         NATIVE=1

(identical to the shipping canonical line + NATIVE=1; NATIVE forces
NO_ROW_DEFER over ROWDEFER=1 by design.)
