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
| 08-02 | e37885b | 41.0 | 90.8 | 3.3 | 48.4 | 20.2 | 40.7 | (iter4 BASELINE; scream now measured; MAME rig has drifted — ares is the gate)
| 08-02 | 59cdebf | 41.0 | 90.8 | 3.3 | 48.3 | 20.2 | 40.7 | (iter4 LANDED: MAME-neutral by construction — latency-only change, ares measures the win)

### Iteration 5 — BAND CRACKED: the per-vint HANDLER TAIL overruns the frame

The 64-67% V-gate reject band is NOT a window-stall latency spiral. It
is the shim's per-vint TAIL — the work AFTER the render window that runs
on EVERY vint, gate-rejected or not: the DREQ sprite-list push, the
512-word palette scan, and the text/palette COMM stream. Four iterations
shortened the WINDOW (runs on the ~35% accepted vints); the band never
moved because the tail (100% of vints) was untouched.

MEASURED (tail-span probe, md_main 0xFFB0FE entry / 0xFFB0F4 packed
max: high=whole tail, low=stream; state_health decodes). ares verdict
59cdebf: rejects 65.5%, vints/cycle 8.69, and one accepted sample at
V=0xE0 — so NOT a calibration shift (on-time vints exist), it is
intermittent overrun. MAME floor (build 26d4148, 3x faster SH-2, gate
rejects ~0%): whole-tail = 231 of 262 scanlines, split stream 120 /
DREQ+scan ~111. The handler eats 88% of the frame EVEN ON MAME; it fits
only because 231 < 262. On ares (slower) the tail crosses 262 -> the
next H-int fires while the handler still runs -> late -> reject, every
frame it overruns. That IS the band, and the ~7Hz crawl.

ROOT: too much slow 68K<->32X bus traffic per vint. The palette scan is
fast local RAM; the cost is (a) ~320+ COMM register writes + ack-spins
in the stream (the MD waits on the slave, which is busy composing) and
(b) the 516-word DREQ FIFO push gated on DMA drain. Both ~110 lines.

ITERATION 5 FIX (design fork, next):
- STREAM: stop re-sending the 16 priority batches (layer regs +
  rowscroll) every vint when unchanged; static scenes then send ~0.
  Motion scenes still pay — pair with moving the stream to a single
  bulk DREQ transfer instead of per-word acked COMM.
- DREQ: push the sprite list every OTHER vint (1-frame lag already
  tolerated) to halve its cost, or fold text/palette into the same DMA.
Target: whole-tail << 262 on ares with margin -> rejects collapse,
vints/cycle -> 3. Falsifier: state_health tail span + reject %.
Diagnostic build 26d4148+ carries the probe (F4 repurposed from the
retired fs/torn tracer); it is NOT a fix — the band will still read
~65% until the tail is cut.

### Iteration 4 IMPLEMENTATION LANDED — preempt-blit + early ack

Shipped two of the plan's mechanisms; the third (copy_pages off the
FM-hold) proved impossible without a new arc (below).

(b) PREEMPT-BLIT MAILBOX (the core win). New SYNC[4]/[5] slots: the
master posts the per-window blit in SYNC[4] instead of first draining
the slave's whole concurrent compose (the old pre-blit
`slave_wait(tile_cmd)` — the named retry-loop-saturation cause). The
slave services SYNC[4] between compose strips (slave_service_stream),
so blit pickup latency is <=1 strip (~0.4ms) instead of a full
compose. SAFE BY CONSTRUCTION: the rows Wk blits and the region the
slave is still composing are always DISJOINT (W1 blits 0-112 while
R2/144-224 composes; W2 blits 112-224 while R0/0-72 composes) — single
sbuf, no tear.

(a) EARLY ACK, correctly scoped. The compose LAUNCH, the previous-
compose DRAIN (slave_wait, now post-ack, off the 68K path), and the
band enqueue moved after the COMM0 ack. copy_pages, the DREQ
harvest+re-arm, apply_cram, and the blit stay PRE-ACK.

DEAD END found the hard way (MAME deadlock, both CPUs frozen at
scene 0x0C): moving copy_pages / DREQ re-arm post-ack is UNSAFE.
copy_pages READS the game's FB staging (0x24012000) — legal only at
FM=1 (pre-ack). And the DMA re-arm MUST precede the game's own DREQ
sprite-list push, or the 68K blocks forever on a full FIFO write with
no armed drain (measured: 68K stuck at the fifo store `fifo[0]=s[1]`,
vint entries frozen). Both stay pre-ack. copy_pages (~2ms) is now the
pre-ack FLOOR; retiring it needs the full write-LOG ring (iter 1b:
thunks ship (offset,value), SH-2 applies to the shadow directly, no FB
read) — a separate arc, NOT this iteration.

ARES FALSIFIER (Mike, next pass): state_health.py before/after on
build `iter4`. Predicted: V-gate rejects fall from the invariant
64-67% band toward ~0; vints/cycle -> 3. If the band holds, the
preempt latency (<=1 strip during heavy sprite strips) is itself the
new binding constraint -> next probe shrinks slave strips or splits
the blit off the compose CPU. MAME gates all pass (scoreboard
identical; attract runs title->scream->eyehold->demo; PRESSURE holds
shape) but MAME cannot see the FM-hold latency — the 64-67% band is
the only verdict.

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
