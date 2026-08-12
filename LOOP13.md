# LOOP 13 — POLISH THE TWO-PLANE PIVOT

Kickoff for a fresh session. Read this, then `ARCHITECTURE.md` §9-17
and `DESIGN-FLOW.md` (the decision map). `LOOP12.md` is this era's
history — the void hunt, the palette pack, the cadence fixes — with a
NEGATIVE RESULTS trail embedded; read it before re-trying anything.
**Where LOOP12 and ARCHITECTURE.md disagree, ARCHITECTURE wins.**

## WHERE THIS STANDS (all verified, Mike's ares runs + counters)

`make MDBGALL=1` = the two-plane pivot: MD Plane B carries the S16 BG,
Plane A carries FG cat-0 (transparent-pixel-0 pattern variants), the
32X composes sprites + FG cat-1 over them via the through bit. On ares:

    vints/cycle        3.00   (healthy; was 4.64)
    V-gate rejects     0.0%   (was 35.3%)
    window/ack worst   160    (was 209; shipping build ~96)
    blit skips         32%    (was 45.6%; MAME reads ~0.3%)
    tearing            dead   (the pivot's original prize)
    colours            arcade-class; sky stable (was two-state flip)
    audio              SILENT (Z80 parked in permanent reset)

Shipping build untouched throughout: parity 24.26, title 2.44,
eyehold 3.37, `_end 0x06018d38`, stamped `normal`.

## THE JOB, in order

1. **§11 offline palette precompute** (the standing lane; pure python
   until the last step). Walk the captured palette states (28 ares
   states + attract + Mike's stage-2 captures), emit per-scene
   set→line/pen tables with CYCLING-AWARE EXCLUSIVE PENS, ship as ROM
   tables + scene detector. Retires: the mint tint, the frozen-shimmer
   trade (drift-tolerance KNOWN COST in ffc8d27), most runtime
   fallbacks ([36] ~55/min). Design notes in the ffc8d27 and 7a95400
   commit messages.
2. **The remaining ~60 window/ack lines** (160 vs shipping ~96).
   What's still in-window beyond shipping: the publish copy (~0.2
   lines — innocent), apply_cram, copy_pages, blit. Measure with the
   TAIL_PROBE machinery before theorising; the delta may simply be
   MDBGALL's larger CRAM/paint work.
3. **Ares blit skips 32%** (MAME 0.3% — an ares-only saturation
   signal, sprite/FG-cat-1 staleness in skipped bands). Needs probe
   builds + Mike round-trips; park until 1-2 land.
4. Small visible artifacts: purple blocks (stage 1, by the player),
   wall speckles, bottom-left purple drips, and whatever Mike's next
   play pass flags.

## INSTRUMENTS (all in tools/ now — the scratchpad copies die with
## the session)

  - `tools/anchorshot.lua` — screenshot at the demo game-state anchor;
    timeline-drift-proof across builds.
  - `tools/voidmetric.py <png>` — one-line scene metric (dominant %,
    distinct colours). Healthy anchored scene ≈ 30%/40; void = 99.7%.
  - `tools/diag_grid.lua` — DIAG counter dump + snapshots every 600
    frames. Slots: [9] windows, [35] assigns, [36] fallbacks,
    [37] frees, [38] tolerated drifts, [39] HOT evictions,
    [50] evictions, [53] claims, [57] tiles shipped.
  - `tools/frame_profiler.py screenshots/` — raw ares capture: cadence
    (v1 caveat: whole-frame dup analysis reads 60fps because the
    banded blit changes every frame — v2 wants per-band), stalls,
    scene cuts, per-segment sky stability.
  - `tools/state_health.py rom/s16.bsN` — the ares counters readout;
    ALWAYS check the BUILD hash line against the rom played.
  - Receiver stage probes live at 68K 0xFFB0B0-B8 (V at entry/scroll/
    tiles/cells/CRAM); receiver diag at 0xFFB0E0-EC.
  - MD-side work RAM 0xFFA000 is free (4KB, documented).

## TRAPS — cost real time this era, do not re-learn

  - **Low-bandwidth rule: images are the context cost.** Verify with
    numeric instruments (voidmetric, counters, span probes); only pull
    a PNG into context when a number can't answer.
  - **The auto-commit hook only fires on Edit/Write tool calls** —
    python-heredoc patches bypass it; commit manually right after.
  - **Git timestamps are the only clock.** Sessions that "feel" like
    nights are usually under an hour. State durations from `git log`,
    never from feel. Same for Mike's context: no narrative-building.
  - **MAME's `videoram` space is fake for reads AND writes** (§8/§17);
    the ONLY honest window into MD VRAM is `emu.item` on
    `:gen_vdp` `0/m_vram`. MAME write-taps die silently when handlers
    reinstall — verify a positive before trusting any zero.
  - **Frame-indexed screenshots don't compare across builds** (window
    timing shifts the attract timeline). Anchor on game state.
  - **parity_cap.lua anchors are settle-tuned for the SHIPPING build**;
    probe-build parity numbers measure transition latency, not
    fidelity. Judge probe builds on ares or anchored shots.
  - **-mshort**: MD-side int is 16-bit; constant-only `<<16` VDP
    commands evaluate to ZERO. Cast `(uint32_t)` or die silently.
  - **68K DIVU in per-word loops** = scanlines of cost (the receiver's
    `% 40` was 129 lines/chunk). Check generated arithmetic in any
    68K hot loop.
  - **The Z80 park must stay poll-free** (reset held asserted; two
    bus-grant poll variants hung boot — grant never arrives here).

## DEAD ENDS THIS ERA — measurements in LOOP12/commits

  - Tear guard / snapshot-consume (736-word FB copy): halved the
    window rate; the tear it guarded against is impossible (receiver
    and packet rebuild strictly sequential). cae844e → retired 7a95400.
  - Sole-owner in-place recolour + volatile-set exclusive pens: never
    fired ([51]=0 both attempts) — drifted pixels always sit on shared
    pens. The working answer was drift-tolerance (ffc8d27).
  - Write-budget/DMA machinery for the receiver: never built — the
    140-line span was two DIVUs per cell (7b52357), and the cadence
    recovered to 3.00 without it.

## GATES ON EVERY COMMIT (unchanged)

  - `tools/parity_run.sh <dir>`: title 2.44, eyehold 3.37 statics.
  - `grep ' _end$' rom/s16.lst` < 0x06019000.
  - `python3 tools/build_id.py show rom/s16.32x` — shipping stamped
    `normal`; probe builds NEVER handed to ares as the shipping rom.
  - Mike's ares play pass. His one-word verdicts ("slow") have
    out-diagnosed the counters twice this era; ask how it FEELS.
