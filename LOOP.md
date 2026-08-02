# The Parity Loop

Goal: `rom/s16.32x` renders like `mame altbeast` — sprites, caching,
layering, background — measured, not felt. The loop does not stop until
every scoreboard scene is under threshold and Mike's ares pass agrees.

## The objective function

`tools/parity_run.sh` captures both machines at SCENE-ANCHORED states
(game variables f031/f02A — never frame numbers; NVRAM cleared every
arcade run) and prints the scoreboard: percent mismatched pixels per
scene plus a magenta diff image per scene in `parity/`.

Threshold: a scene passes at **< 1.0%** (conversion-noise floor is ~0%,
sprite-position jitter from 1-frame anchor slop accounts for the rest).
Scenes: title, scream, eyehold, demo, demo2. Grow the list as rounds
2-5 come online (gameplay anchors: level/round variables, same method).

## One iteration

1. `tools/parity_run.sh` → scoreboard.
2. Pick the WORST scene. Open its `_diff.png`. Classify the mismatch:
   layer offset (scroll/geometry), wrong colors (maps/palette), missing
   art (cache/transport), sprite placement (list/decode), priority
   (mixing rules).
3. Diagnose with the kit, in order of cheapness:
   savestate parsing (TOOLKIT recipes) → scene-matched MAME probes →
   wpcatch → jtcores .v (hardware truth) → MAME segas16b_v.cpp →
   shipped-ROM archaeology (srcref/*.32x) → the MD Altered Beast port
   (a third same-game reference when 68K-side behavior is in doubt).
4. Fix ONE root cause. No fix without a named mechanism.
5. Gates, all mandatory before commit:
   - full regression (`tools/play_32x.lua`, telemetry sane, deferrals ~0)
   - `make PRESSURE=1` eye+demo scenes hold shape
   - scoreboard: target scene improves, NO other scene regresses
   - region guard passes, rom stamped `normal`
6. Commit with the mechanism in the message. Update the scoreboard
   history table below.

## Standing laws (violations caused every regression this week)

- The FB window is not an MD→SH2 data channel; COMM + DREQ only.
- The blit deadline is sacred: no maintenance work past dt=4000.
- Bands complete or defer; never ship partial layers.
- Frame numbers never align across builds/machines; anchor on state.
- MAME cannot see: FB arbitration drops, write-buffer stalls, host-load
  from idle polling. Ares-only symptoms need savestates, not theories.
- Every rom stamped; every savestate names its build.

## Roles

- Loop (Claude): iterations, autonomously, reporting the scoreboard
  delta each cycle. Runs via /loop or continuous turns.
- Mike: ares pass every ~5 iterations or at any milestone commit; ares
  remains the outer acceptance gate. Savestate slot 1 on any anomaly.

## Scoreboard history

| date | build | title | scream | eyehold | demo | demo2 | mean |
|------|-------|-------|--------|---------|------|-------|------|
| 07-30 | 54b227c | 71.5 | n/a | 51.4 | 47.8 | 22.4 | 48.3 | (instant anchors: latency-dominated)
| 07-30 | 2d0d52c | 2.75 | n/a | 3.32 | 48.3 | 22.7 | 19.3 | (stable anchors: statics=render truth)

### Iteration 4 probe verdict — cart-bus DEAD; the retry loop IS the load

SPROBE run (sprites off, ares): V-gate rejects 65.9% — unchanged.
The 64-67% band has now survived: three scheduler designs, the
copy_pages hold, and the heaviest cart-bus reader. The only mechanism
that self-stabilizes at a constant reject rate: THE GATE-REJECT RETRY
LOOP SATURATES THE 68K. Rejected posts retry every vint; the master's
pre-ack path includes slave_wait on the PREVIOUS concurrent compose
(ms on ares); the handler chain runs near frame-length, so every
entry is late, every post rejects, and the equilibrium re-forms no
matter what is shaved elsewhere — which is exactly the observed
invariance.

ITERATION 4 IMPLEMENTATION (next session):
(a) EARLY ACK: MD releases the game after FM-required work only
    (blit ~0.75ms + apply_cram ~0.7ms); latch, DREQ harvest/re-arm,
    and compose launches move post-ack (SDRAM/no-FM work).
(b) PREEMPTIBLE SLAVE COMPOSE: the slave polls for window commands
    inside its strip loop (it already calls slave_service_stream
    there); on a window signal it pauses compose, performs its blit
    duty, resumes. Master slave_wait shrinks from a full compose to
    <=1 strip.
Target: 68K per-window stall ~2ms; handler chain << frame; gate
rejects -> ~0; ares cadence vints/cycle -> 3. Verify with
state_health.py before/after — the invariant 64-67% band is the
falsifier either way.

### Iteration 3c closed / 4 opened — the ring works; the drag is deeper

Ring verdict (state_health on ares, build d6165d5d): dirty bitmap
drains to ZERO (the ring is correct and steady-state page traffic is
gone) — yet V-gate rejects held at 63.9% vs 67%. Across original /
split / ring scheduling the reject rate is INVARIANT: the 68K's
chronic lateness is not scheduling. ITERATION 4 HYPOTHESIS: cart-bus
contention — unpair keeps RV=0, the SH-2s read sprite art from cart
per composed pixel, on the bus the 68K fetches game code from; MAME
does not model the arbitration, ares does. A/B probe built:
rom/PROBE_sprites_off.32x (`make SPROBE=1`, stamped SPROBE) disables
sprite compose. If ares rejects collapse -> the fix arc is sprite-art
RESIDENCY (SDRAM working-set cache for sprite rows, like the tile
cache); if unchanged -> next probe strips the shim vint preamble.
tools/state_health.py = the one-line savestate health report.

### Iteration 3b — stall probe verdict: splitting is not shrinking

Dual-PC probe at the "deadlock": both CPUs healthy, composing
continuously — the pipeline starves because window posts stop passing
the V-gate ONCE k2/k0 lengthen (the split spread the 68K-blocked time
without reducing it; the spiral reproduced in MAME). LAW: the ares
cadence fix must REMOVE steady-state FM-hold work, not redistribute
it.

### Iteration 3c — THE plan: write-observer ring (1b, correctly scoped)

The ring needs NO bank tricks and does NOT touch the read-back sites:
thunked stores land in FB staging normally (game reads unaffected)
AND append their offset to an MD-RAM ring. Loads (RLE/block fills)
get entry/exit thunks: load_flag + dirty-page bitmap instead of
per-word logging. MD vint ships ring+bitmap in the DREQ push tail.
SH-2 k1: apply ring offsets (few in-window FB reads) and run page
copies ONLY when the bitmap demands (post-load, display already
held). copy_pages leaves the steady path -> k1 FM-hold ~2ms (blit
0.75 + apply_cram 0.7 + ring apply) -> handler fits the frame ->
20Hz cadence on ares. Store sites (enumerated, disassembled):
- ring-tier: 0xD84 fill helper (+ inline 32BC singles 0xD60-0xD7A),
  0x2AD4/2AE2/2AF0/2AFE vint corner words, 0x6836-cluster seam
  writers (stores in helpers past 0x6966), 0x1BA1C/0x1BA2C fills,
  0x1BA42/0x1BA4A words, 0x173C/40 + 0x1760/64 byte-pair loops
- load-tier (entry/exit brackets): RLE 0x16AE/0x16B2 pair, block
  blitter 0x258A, clears 0x36B0 and 0x1ACD8, 0xDA8 column blit,
  0x1A52E/0x1A54C bonus fills, scratch save/restore 0x1B760/0x1B7A4
  (bracket keeps its round-trip in one bank tenure — moot without
  alternation, kept for the eventual double-buffer)

### Iteration 3 — ares cadence spiral: DIAGNOSED, fix attempt REVERTED

ARES VERDICT (savestate d6ed14ac): speed pathology dead (blit skips
0.7%, DREQ pristine) BUT the render cadence is starved — 145 cycles
in 1317 vints (~7Hz effective): 67% of the MD's window posts fail the
V-counter gate with MID-FRAME entries (B0FE V=0x95). MECHANISM: the
k1 window blocks the 68K through latch + copy_pages(0,6) + apply_cram
+ blit (8-15ms on ares) — the vint handler overruns the frame, the
next vint fires late, the gate rejects, retry: a latency spiral.
FIX DIRECTION (attempted, deadlocked in MAME, reverted): split
copy_pages in thirds across windows + move the DREQ harvest post-ack.
Two ordering laws found and banked: (a) FM must outlive EVERY FB
reader on BOTH CPUs (slave page-copies after its done-signal froze
the pipeline; master acking before the slave's copy did too); (b)
even with both waits there is a residual interlock that stalls at the
scream tile-load — needs a dual-PC stall probe (master+slave PCs at
hang) before the next attempt. The k1-shortening remains THE ares
cadence fix; only its execution needs the probe first.

### Iteration 2 — 30Hz cadence retry: REVERTED (sharper negative result)

The original 30Hz negative predates the sprite fast-forward, so it was
retried with 9ms/cycle freed: STILL fails (skips 976, swait 2.1ms,
scoreboard collapses from deferral staleness). SHARPENED MECHANISM:
the binding constraint is not raw compute (per-frame work ~8ms fits a
33ms cycle on paper) but the PER-WINDOW SYNC BARRIERS — master waits
slave at every window entry, and 3 region-composes + 2 blits + stream
service serialize across only 2 window-gaps. 30Hz needs an async
slave scheduler (no per-window barriers), which is a real arc, not a
cadence flag. Motion-scene latency floor stands at 20Hz until then.
Iteration 3 candidates: demo-scene sprite-list timing alignment
(cheap latency win: harvest the DREQ list every vint instead of every
k1 — sprites currently lag up to 3 vints), then the statics' last 3%.

### Iteration 1a — atomic ship via bank alternation: REVERTED

Attempted: single flip per k==1 vblank entry, full-frame two-CPU blit
into the hidden bank, banks alternating per cycle. Two REAL findings
survived the revert:
1. The MD's fs_home hold and the per-window double-flip dance cost
   measurable time everywhere — with them gone the pipeline hit its
   best-ever health (mskips 9 vs 300+, skips 2 vs 25+). Reclaim these
   wins when 1b lands.
2. FATAL FLAW: `copy_pages` is a BLIND copy — with banks alternating,
   each bank holds only the tile writes from its own access cycles,
   and the blind copy overwrites the SDRAM shadow with partial truth
   (letter-soup tilemaps). Accumulation needs dirty-aware merge, which
   doesn't exist. Also found: the shim's windows-completed diag and
   the DREQ abort counter collided at 0xFFB0F2 (fixed in 1b's base).

### Iteration 1b — the real fix: tile write-log over DREQ (NEXT)

Evict the last FB tenant: patch the game's ~15-25 tile-store
instructions (enumerable from patch_report's 29 address-formation
sites) to thunks appending (offset,value) to an MD RAM ring; the
DREQ push ships ring entries after the sprite list (header word
distinguishes payloads); the SH-2 applies the log directly to the
SDRAM tilemap shadow. copy_pages retires (frees its window time),
the FB becomes pure display double-buffer, and 1a's flip discipline
(+ its measured wins) drops in cleanly. 68K read-watchpoint already
proved zero tile-staging readbacks (90s attract+demo) — the write
side is the whole contract.

Baseline note: frame-exact anchors reveal the dominant term is the
ROLLING SHIP (per-window flips display a composite of two frames; the
arcade ships whole frames). Iteration 1 = atomic ship: flip once per
cycle at the completion window, gated on all-slices-landed; staging
stays correct because the SDRAM tilemap shadow accumulates each
cycle's access-bank writes (late writes carry ≤2-cycle latency, no
loss). Scream anchor needs a 16px mask window (pan steps 12px/frame
and skips exact values).
