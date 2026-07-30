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
| 07-30 | 54b227c | 71.5 | n/a | 51.4 | 47.8 | 22.4 | 48.3 |

Baseline note: frame-exact anchors reveal the dominant term is the
ROLLING SHIP (per-window flips display a composite of two frames; the
arcade ships whole frames). Iteration 1 = atomic ship: flip once per
cycle at the completion window, gated on all-slices-landed; staging
stays correct because the SDRAM tilemap shadow accumulates each
cycle's access-bank writes (late writes carry ≤2-cycle latency, no
loss). Scream anchor needs a 16px mask window (pan steps 12px/frame
and skips exact values).
