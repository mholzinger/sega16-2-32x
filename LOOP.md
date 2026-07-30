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
