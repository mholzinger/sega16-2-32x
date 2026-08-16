# LOOP 14 — MDBGALL BACKGROUNDS: HEAL THE PENS, SPEED THE CUTS

Kickoff for a fresh session. Read this, then `ARCHITECTURE.md` and
`LOOP13.md` (the transport era: presentation 2.0, the magic-tail DREQ
gate, the co-owner pen fix — with the NEGATIVE RESULTS and five slot
collisions embedded). **Where LOOP13 and ARCHITECTURE.md disagree,
ARCHITECTURE wins.**

## WHERE THIS STANDS (verified 2026-08-15, Mike's ares states +
## corpus + offline audits)

Current build both flavors: **BUILD b1343ac9** (`rom/s16_ship_magic.32x`
shipping, `rom/s16_mdbgall_magic.32x` MDBGALL+BQCHUNK; same hash —
flavor by look/cmp, per the trap). Gates at handoff: title 2.44 dx=0,
eyehold 3.37 dx=0 EXACT; `_end` ship 0x18b90 / MDBGALL 0x18e98;
stamped normal. **Mike has NOT yet played b1343ac9 — that round-trip
is item 1.**

Proven-good, ares-verified (state d8358681, tools/bs9_audit.py):
  - **Transport is CLOSED.** The magic-tail gate catches ares' FIFO
    word-loss (drops a 68K write racing a full FIFO without counting
    it; the overpush used to backfill the length and land packets
    displaced -2). Poisoned packets (4-9% of windows on ares) are
    skipped whole; lost dirty-marks recovered exactly via the COMM10
    one-window latch. dreq_incomplete 0.0%, rejects 0.2-0.4%,
    cadence 3.01, strobe 0/10448, NT readback 0. TEXT_U reg block at
    canonical offsets. Sprites: Mike — "sprites look complete."
  - **Presentation 2.0 holds**: no tearing, no strobe class, flip
    skips ~1%.
  - **Both MD planes byte-match their mirrors (0/1120 + 0/1120),
    margins blanked both planes (0/672 + 0/672).**

## THE JOB, in order

1. **Mike's play pass on b1343ac9 (MDBGALL flavor).** It carries the
   CO-OWNER PEN FIX: the yellow-slab/purple-stripe/black-cell fields
   that covered his 2:50 corpus were co-owner sets painting the
   OWNER'S diverged live colour — a class the old drift check could
   not see by construction (it compared bookkeeping colours, and the
   live CRAM refresh guarantees the screen shows owner-live). Fixed:
   co-owners compare against owner-live, same d^2>=18 free+re-claim.
   VERDICTS TO COLLECT: (a) do slab fields clear within ~a second;
   (b) does anything FLICKER in fades (churn storm = the opposite
   failure; the drift-reassign saturating counter is the guard);
   (c) state_health "pen drift" line — [5]/[6] have NEW SEMANTICS
   from b1343ac9 (MAME 65s attract baseline 3397/26; old numbers are
   not comparable).
2. **Scene-cut draw-in** (corpus frames 182-231): tilemap shapes
   arrive instantly, art takes ~0.85s (1120 fresh codes at
   MD_BATCH=40/window ≈ 28 windows of uploads + cell restamps),
   showing the previous scene's slot art meanwhile. Fix lane: burst/
   priority upload at cuts — the game fades there, and the freed
   pres-2.0 budget (no flip pairs) has headroom nobody has re-measured.
   MAME-reproducible; iterate without ares round-trips.
3. **Post-cut residue persistence** (the ranking-table cells that sat
   in the rightmost columns ~30s): LOOP13's open question — truth
   heals in ~2 cycles, cell chunks re-walk every ~5 windows, so why
   do stale md_tag claims survive ~30s? Suspects on record:
   pg_watch's k2-only recapture (mid-stream for long streams),
   md_pending saturation ordering, allocator memory. A savestate
   TAKEN WHILE RESIDUE IS VISIBLE is the missing evidence — ask Mike
   to save the moment he sees garbage, then bs9_audit it.
4. **Loss-rate reduction (optional, feel-driven):** each poisoned
   packet = one window of stale sprites/text (invisible at 0.2-1%
   window impact, but 4-9% packet-loss rate). If Mike still feels
   jitter, per-word FIFO full-polling costs ~+10-16 lines of 68K
   tail — MEASURE with TAIL_PROBE first (spin headroom 2597/2600
   says polls almost never wait, so the cost is pure MMIO reads).
5. Parked, unchanged order: window/ack (~202 worst), audio (Z80
   parked), §11 palette precompute LAST (but note the co-owner fix
   may retire much of the visible-corruption half of §11's motivation).

## INSTRUMENTS (tools/, session-proof)

  - `tools/bs9_audit.py <bs9> [--snap A] [--sbuf A --png out]` — the
    one-stop state audit: VRAM base ANCHORED ON MIRROR CONTENT with
    two-row verification (the margin fingerprint aliased +0x2000
    twice — never reuse it), plane-vs-mirror diffs, margins, TEXT_U
    map, staged VDP records, snap regs, sbuf render. --snap/--sbuf
    addresses come from the FLAVOR-MATCHED lst (`grep ' _snap$'
    rom/s16.lst`).
  - `tools/magic_smoke.lua` — 65s MAME MDBGALL smoke; prints cadence,
    flip skips, dreq_inc, DRQR[0..7]. Baseline b1343ac9: cadence
    3.02, skips ~1, dreq 0, [5]=3397 [6]=26 [7]=1 (boot-only; growth
    in [7] on MAME = regression).
  - `tools/state_health.py` — now prints "dreq misaligned" (DRQR[7],
    the honest loss meter) and the pen-drift split.
  - `tools/parity_run.sh <dir>` — statics 2.44 / 3.37 are the gate;
    dynamics carry dx=+8..24 pres-2.0 display-latency offsets at high
    % (documented, not a regression).
  - Capture corpus recipe: `find ./screenshots -mindepth 1 -delete;
    ./capture.sh raw` (~57fps). Scan by size; frame N ≈ N/57 s.

## TRAPS — paid for in LOOP13/14, do not re-learn

  - **BUILD hash is commit-derived; flavors share it.** Confirm flavor
    by look or `cmp` against the saved flavor copies; `make` after an
    MDBGALL build may NOT relink (check the lst's `_end`).
  - **Flavor symbol addresses differ** (`_sbuf`, `_snap`); grep the
    lst that matches the binary audited.
  - **VRAM-base fingerprinting by margin content is DEAD** — it
    aliased +0x2000 twice. bs9_audit's mirror-content anchor is the
    recipe. Mirror[0] = plane B (0xE000), mirror[1] = plane A (0xC000).
  - **[5]/[6] pen-drift counters are phase-sensitive AND changed
    meaning at b1343ac9** — never a fidelity gate, never compared
    across builds.
  - **The region guard is one static away.** Fixed-block free space:
    0x3E780..0x3EFFF (after tile_grp 0x3E480 + pri_lut 0x3E580, under
    master stack top 0x3F000); scraps at 0x28F40-4F, 0x28F68-7F,
    0x28FA0-FF. DRQR block ends 0x28FA0.
  - **DRQR[0..7] boot init sits inside `#ifdef FM_TEST`** — fine on
    emulators (zeroed SDRAM), garbage on real silicon. Fix before any
    hardware bring-up.
  - **The auto-commit hook fires on Edit/Write tool calls only**;
    python-heredoc patches bypass it — commit manually right after.
  - **Scratchpad instruments die with the session** — promote to
    tools/ before handing off (this file's instruments already are).

## GATES ON EVERY COMMIT (unchanged)

  - `tools/parity_run.sh <dir>`: title 2.44, eyehold 3.37 statics.
  - `grep ' _end$' rom/s16.lst` < 0x06019000.
  - `python3 tools/build_id.py show rom/s16.32x` — shipping stamped
    `normal`; probe builds NEVER handed to ares as the shipping rom.
  - Mike's ares play pass. Ask how it FEELS — his one-word verdicts
    have out-diagnosed the counters repeatedly.
