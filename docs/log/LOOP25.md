# LOOP 25 — THE DRAW MUST MATCH: the palette pipeline under storms

Mike, closing LOOP24: "we have the speed cadence down, but we need
the screen draw to match." Cadence 2.18-2.21, flip tear extinct,
transport lossless — the TIMING is right; the CONTENT deviates.
LOOP24's close-out has the seam/sprite-transient items; THIS loop's
opening evidence (Z4 raw corpus, BUILD 7ecd150b) points the other
way: FOUR of Mike's six call-outs are ONE FAMILY — palette.

## MIKE'S SIX (Z4 raw frames), CLASSIFIED

1. BLACK SMOKE (f351-468, ~2s sustained): the transform smoke renders
   as a PERFECT BLACK SILHOUETTE — pixels present, colorset black.
   Its palette state never leaves black during the transform's
   palette animation. NOTE THE Z3 DELTA: with FLICKFUSE the same
   smoke drew GRAY — the fuse's held-copy draw masked whatever the
   raw path exposes. Single variable, resolve with the pal probe +
   a flick A/B.
2. TICK-ROW (f499, f533): a full-width row of black dashes at the
   tree/sky boundary — THE LOOP13 TICK-ROW GHOST (MD-plane, receiver
   row at the NT_WRAP seam). Never fully killed; the LOOP13 receiver
   tools (MDVERIFY family) are the probe.
3. INVERTED-COLOR FLASH (f508-509 = ONE 30Hz display frame): the
   whole TILE world (BG+FG+temple) renders in garbage palette while
   the PLAYER SPRITE is PERFECTLY colored — a tile-half CRAM
   generation tear during the scene-transition palette storm. May be
   PRE-EXISTING (this is the first raw-mode capture since Z; a
   1-frame flash at 30Hz hides from dedup'd captures and from eyes).
4. PURPLE BAND (f510): band-scoped stale palette — a band composed
   against an old cram_mirror generation (LOOP24's named item).
5. DISCOLORED GRASS (f521-530): palette aftermath — stale blocks
   post-transition, converging late.
6. = 2 + the split-blit seam at the same rows.

## THE ARITHMETIC SUSPECT

The pal channel ships at most PKT_PAL_KMAX=4 dirty 32-word blocks
per k2 push (128 words/cycle), and k2 pushes skip on aborts/rejects
(~8-10%). LOOP22 sized KMAX=4 from a steady-state census (mean 1.63,
max 7 dirty blocks/frame) — a SCENE-TRANSITION STORM that rewrites
palette wholesale was never in that measurement's tail. If a storm
dirties tens of blocks per vint, the channel runs 10+ cycles behind
and CRAM is a torn mix of generations for whole seconds — which is
exactly what frames 351-530 look like.

## MEASURE FIRST (the LOOP22 census, redone where it hurts)

1. PAL STORM PROBE: per-cycle {blocks dirtied, shipped, applied,
   max-backlog} + a tile-half/sprite-half split + a CRAM generation
   spread counter (how many distinct ship-generations are live in
   CRAM right now — >1 = torn frame). Run it across a transform +
   a scene transition, on headless ares. This sizes the fix:
   - KMAX 4 -> 8 (264-word k2 packet, burst-aligned family exists),
   - pal blocks on the k1 packet too (doubles the rate), or
   - a storm mode: full-region burst when backlog exceeds N.
2. TICK-ROW: LOOP13's receiver verification on the wrap row.
3. Band-palette pinning (the purple band): per-band cram generation.
4. The smoke black: pal probe + Z3-vs-Z4 flick A/B (single var).

Then the 60Hz arc (LOOP24 close-out items: seam age, ISR-span diet,
k1k2 at 60) — the draw must match FIRST; fidelity wins tradeoffs.

Gates: the pal probe's backlog -> ~0 across a transform on ares;
Mike's eyes on the smoke, the flash, and the grass; parity statics
unmoved in MAME; cadence/handler hold the LOOP24 scoreboard.


## PALSTORM MEASURED + FIRST FIX LANDED (2026-08-21 night)

`make ... PALSTORM=1` (probe rom rom/test/PS_palstorm.32x; counters
0xFFA060, decoded by state_health). Attract, headless ares, 5400f:

  BEFORE (KMAX=4): backlog max 64 — the WHOLE palette dirty at once,
  BOTH halves pegged 32/32; mean dirty rate 4.0/vint vs a 4-block
  channel = saturation, zero burst headroom; torn-exposure 428/2539
  pal vints = 16.9% — one in six palette cycles leaves CRAM a
  generation-mix. A full storm = ~16 cycles (~0.5s) of torn colour.
  LOOP22's steady-state census (mean 1.63, max 7) is formally dead.

  AFTER (K2F family widened: 8 id slots, KMAX=7 — the 3-bit tag
  ceiling; packet 44..236 words, arm 244, buffer fits at 0x394C0):
  torn-exposure 106/2574 = 4.1% (4x better), channel bursts 6.2
  blocks/vint mean, storms clear in ~9 cycles. Ares gate vs Y
  baseline: PASSED. MAME structure: skips 0, cycles 1559, clean.

REMAINING: 4.1% torn vints = storms still take ~9 cycles. If Mike's
eyes still catch the smoke/flash/grass family, build the STORM FLUSH:
backlog >= threshold -> the 68K copies the dirty half of the 0xFF9000
mirror into FB scratch at the k1-entry consume slot (FM=0, bounded,
once per storm) and the SH-2 applies it wholesale — one-cycle sync.

Ares roms: rom/test/AA_k2free_pal7.32x (play, no probe),
rom/test/PS_palstorm.32x (PALSTORM=1 — savestate a transform with it
and state_health prints the storm block). Watch: the smoke should
light up MUCH sooner (possibly still ~0.3s dark at onset), the
inverted flash should be rarer/shorter, grass recolours faster.


## THE WIDENING FAILED IN THE FIELD — TWICE — AND THE VERDICT IS THE
## CHANNEL, NOT THE SIZE (2026-08-21, late)

Mike on AA (KMAX 7, per-group): "borderline unplayable" — speed
accelerated/frames dropped, enemies skipping whole animation rounds,
inverted blanking, palette shifting. His state named it: misaligned
22-30 -> 101, residue pegged at the FULL 244 arm — wide pushes dying
wholesale to the FIFO drop race. Fix attempt 2 (restore the per-word
belt): handler 53 -> 80, incomplete 30.5% — the belt burns the spin
budget against the slow drain instead. BOTH failures share one root:
under K2FREE the push runs DURING the master's ISR/window span, where
the DMAC drains slowly; per-group RACES there, per-word STALLS there.
The pre-K2FREE 596-word era worked because pushes ran post-ack
against an idle master. THE FIFO CANNOT CARRY BULK PALETTE AT THIS
POINT IN THE FRAME.

REVERTED to the Z4 transport exactly (per-group + KMAX 4) and
verified BIT-IDENTICAL on the deterministic headless run (vints
3584, cycles 1687, handler 55.4, incomplete 18.7%, misaligned 62,
aborts 170 — every counter equal to the Z4 rom's same run).
Ares rom: rom/test/AB_k2free_z4transport.32x (= Z4 behavior; the
PALSTORM probe stays available behind its flag).

STANDING PROBLEM (unchanged, measured): palette storms dirty 64
blocks against a 4-block/cycle channel — 16.9% torn-exposure, the
black smoke / inverted flash / stale grass. THE ONE REMAINING
DESIGN: the STORM FLUSH — bulk palette through the FB, not the FIFO:
on backlog >= threshold the 68K copies the dirty half of the 0xFF9000
mirror into FB scratch during its k1-entry FM=0 slot (bounded, ~13
lines, once per storm phase) and the SH-2 applies it wholesale.
Needs: 2KB of FB scratch (candidates: time-share the A-packet block
— palette storms and MD-plane bursts rarely coincide... they DO at
scene cuts; find real space first), a flag word, and idempotent
apply. Design it fresh, gate it on a GAMEPLAY state, and do not
touch the packet family again.

LESSON FOR THE LOG: two field failures in one night because the only
gameplay gate is Mike's hands. Input playback in ares-headless is no
longer a nice-to-have — it is the missing gate. Ask the ares session
to prioritize it before the storm flush lands.
