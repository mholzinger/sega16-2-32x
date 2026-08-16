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

## 2026-08-15 (late) — ITEM 2 MEASURED TO ITS FLOOR; CUT_BLANK BUILT
## AND GATED (the J-field becomes a clean reveal)

**The bandwidth half of draw-in is CLOSED as "already at the floor."**
tools/cut_profile.lua (per-frame DIAG[53]/[57] deltas + MD-side
consume counters, 70s MDBGALL attract): transport windows run at
60/s (one per MD vint — not the 20Hz render cadence). The title cut
at f32 claims 792 fresh codes and drains in 28 windows = 0.47s, and
28 is the conservation floor: ~8 discovery chunks (the 9-phase
rotation must WALK cells to find claims) + ~20 tile batches, one
packet per window. The packet container is FULL (8 hdr + 680 tile +
48 pal = 736 of 768 words; MD_BATCH can only reach 41). Reordering
(more forced batches vs more chunks) just trades discovery against
shipping — the sum is invariant. The only real bandwidth lever is a
second packet per window, and the retired tear-guard measured a
736-word 68K FB read costing HALF the window rate — that lane stays
closed unless Mike still wants faster after seeing CUT_BLANK.

**CUT_BLANK (make CUTBLANK=1, m_main.c nt walk) attacks the SYMPTOM:
the J-field was never missing bandwidth, it was stale art.** Cells
are restamped correctly at claim time but their slot still holds the
previous scene's tile until the upload lands. Now: an nt chunk that
claims >=80 slots in one window (only cuts do; pans claim <80,
LOOP13's trickle-starvation case never triggers) arms a 12-chunk
countdown; while armed, any cell whose slot is still dirty ships as
MD_BLANK_SLOT. Foreign art -> blank under the fade; the chunk
revisit after the art lands rewrites the real entry (post-drain the
rotation is pure-chunk, full heal in ~8 windows). The starvation
bound is untouched. Counters at the 0x28FA0 scrap ([0] blanked
cells, [1] arms), boot-zeroed explicitly (FM_TEST trap noted).

**Verified (BUILD ffa476df = MDBGALL+BQCHUNK+CUTBLANK,
rom/s16_mdbgall_cutblank.32x, stamped normal, _end 0x18f28):**
  - Mechanism: arm at f32, 680 cells blanked through the storm,
    blanking stops by f48; 4 arms / 2182 cells across 7s of attract
    (tools/cut_snap.lua prints the counters + snapshots the cut).
  - The f044 A/B is dramatic: baseline shows the scattered
    foreign-tile field across the whole midscreen; CUTBLANK shows
    landed art + clean blank + crisp INSERT COIN. The draw-in reads
    as a top-down reveal instead of garbage.
  - MDBGALL parity A/B vs s16_mdbgall_magic: eyehold/demo/demo2
    PIXEL-IDENTICAL (steady state untouched). Title 59.33 -> 64.03:
    the title reveal is progressive, claims trickle past the
    45-frame capture anchor, so late blanks are still healing at
    capture — the feature working, not a heal failure. Mike's call.
  - Shipping gate (BUILD bdd7b8fe, plain make): title 2.44 dx=0,
    eyehold 3.37 dx=0 EXACT. Region guard: ship 0x18b90 / cutblank
    0x18f28. magic_smoke on CUTBLANK: cadence 3.01, skips 1,
    dreq 0, [7]=1 boot-only, drift 3349/26 — matches baseline.

**FOR MIKE'S NEXT ROUND-TRIP (two verdicts now):** (a) the b1343ac9
co-owner pen items from THE JOB above, unchanged; (b) does
rom/s16_mdbgall_cutblank.32x make cuts read clean on ares — blank
reveal vs the old foreign-art field — and does anything NEW flicker
at scene starts (a storm that re-arms every chunk would blank-flash
cells; the >=80 threshold is the guard). If (b) reads well, fold
CUTBLANK into the MDBGALL bundle default.

## 2026-08-15 (night) — ITEM 3 ROOT FOUND ON MAME: THE "RESIDUE" IS
## STALE TILEMAP TRUTH, MDBGALL-ONLY; PGROTOR FIX BUILT AND GATED

Mike's b1343ac9 pass came back "too much garbage in background
tiles" (corpus frames 149-236 transition + 754 gameplay) with a
HEALTHY state (cadence 3.01, dreq 0, drift 255/8 — the co-owner slab
class IS dead: fence/temple clean in every gameplay frame; what
remains is a sky/cloud + top/bottom band class). The whole chain ran
offline, no ares round-trip needed:

  - **The eyehold MDBGALL static (30.72%) was already measuring it.**
    The eye scene's top 2 + bottom 5 cell rows show ranking-table
    trash for the entire scene, MAME-deterministic. sbuf render
    (tools/nt_dump.lua) shows the 32X itself composing a stage-1
    GRASS STRIP + glyph cells there — stale cat-1 (0xB3xx-0xB5xx)
    cells composed opaque.
  - **The truth is stale, not the compose.** At the same eyehold
    state anchor (tools/arc_dump.lua on mame altbeast vs anchored
    nt_dump on the 32x driver): MDBGALL SDRAM truth pages 0-4 hold
    6947 words the arcade CLEARED at the scene cut (full-width row
    blocks 4-8/27-31 = exactly the trash screen rows via vy0=32);
    pages 5-11 exact. **The SHIPPING flavor at the same anchor is
    0-word EXACT across all 12 pages** — the truth pipeline is
    perfect there; MD_BG's per-window work shifts capture timing so
    the game's streaming staging writes slip past pg_watch's
    one-quiet-capture drop and nothing ever re-marks the page.
    (This also retires the ranking-table ~30s mystery: nothing
    re-dirties the page until the NEXT big writer.)
  - **Fix: `make PGROTOR=1`** — background truth re-verify, 2 pages
    per k1 through the existing cap_drain (budget 3->5, ~0.1-0.3ms).
    Bounds any missed mark to <=0.65s instead of forever.
  - **Verified (BUILD eeab8e85, rom/s16_mdbgall_pgrotor.32x, _end
    0x18ef8, stamped normal):** stale pages 0-4 heal to 0 diffs at
    the anchor; eyehold MDBGALL 30.72% -> 13.35% and the bands are
    CLEAN in the capture — the remaining 13.35% is the eye caught
    mid-blink (display-latency class, same family as the shipping
    3.37% pupil diff). magic_smoke: cadence 3.01, skips 0, dreq 0,
    [7]=1 boot-only ([5] runs ~35% higher — the rotor rephases the
    drift check; [5] is documented phase-sensitive, not a gate).
    Shipping statics EXACT (2.44/3.37 dx=0, pixel-identical counts).
  - Instruments promoted: tools/nt_dump.lua (anchored SH2-side truth/
    mirror/sbuf/CRAM dump), tools/arc_dump.lua (arcade-side anchored
    truth), tools/nt_audit.py (replay the nt-builder against dumped
    truth), tools/frame_snap.lua, tools/cut_profile.lua,
    tools/cut_snap.lua.
  - TRAP RE-PAID ONCE: mid-session `make MDBGALL=1` left rom/s16.32x
    MDBGALL-flavored and a "shipping" measurement was really MDBGALL
    (caught by the contradiction it produced, then cmp). The flavor
    check is not optional.

**MIKE'S NEXT ROUND-TRIP, updated (three roms, one question each):**
  1. rom/s16_mdbgall_pgrotor.32x (BUILD eeab8e85) — is the
     background-tile garbage gone in gameplay and after cuts? This
     is the headline fix for what you reported.
  2. rom/s16_mdbgall_cutblank.32x (BUILD ffa476df) — do scene cuts
     read as a clean reveal (no foreign-art field)?
  3. If both hold, next build folds PGROTOR (+CUTBLANK if liked)
     into the MDBGALL bundle for a single verdict rom, and PGROTOR
     becomes a candidate for the shipping bundle too (cheap
     insurance; shipping truth measured clean without it).
  The b1343ac9 co-owner items (a)/(b) are ANSWERED by this corpus:
  slabs dead, no churn flicker reported.

## 2026-08-16 — PGROTOR DEAD ON ARES (backgrounds cycling); ROOT
## RE-FIXED AS PGSTICKY (sticky+deep watch), EYEHOLD 30.72 -> 8.18

**PGROTOR NEGATIVE RESULT (Mike, frames 3239-42 + savestate 9):**
sprites good, but "all background tiles are rotating on their own
accord" — backgrounds CYCLE between different scenes' art. Mechanism:
FB staging is PER-BANK, and only pages in the restore set
(cycle_dirt|pg_watch) have their two banks kept coherent. The rotor
captured pages OUTSIDE that set from whichever bank was mapped that
cycle — for pages whose banks had legitimately diverged (streams that
straddled flips unwatched), truth alternated bank A/bank B content
every cycle = the cycling. MAME never showed it (its banks happened
converged). Un-shippable by design, flag kept as archaeology. Its
ares state also priced the per-window cost: worst window/ack 183 ->
212 (margin 38 -> 7 lines), deferrals 2 -> 377.

**PGSTICKY (make PGSTICKY=1) is the root fix.** The original miss,
sharpened: thunks mark at POINTER-LOAD; the drain can capture before
the stream's stores arrive, see no change, and drop the page —
nothing re-marks. Fix, entirely inside the settled-mark discipline
(no unmarked-page captures, so the bank disease cannot occur by
construction):
  - every mark puts its pages into pg_watch, not just pg_pending;
  - watch drops only after 12 consecutive quiet captures (sticky=3
    caught the eye-scene CLEAR but missed the face draw into page 0
    — loaders write their last pages 5+ cycles after their one mark);
  - a BROAD mark (>=8 pages = the loaders/clear-alls that mark ALL)
    pins deep watch for 60 cycles (pg_deep) — scene loads run ~1s and
    the tail pages arrive seconds after the mark.
Steady-state cost ZERO (no rotor); transitions get ~60 cycles of
13-page k2 captures inside faded scenes.

**Verified (BUILD 652cb5fe, rom/s16_mdbgall_pgsticky.32x, _end
0x18fa0, stamped normal):** eyehold MDBGALL 30.72 -> 26.86 (sticky
12 alone) -> **8.18%** with deep watch — better than the rotor's
13.35, and the capture is visually PERFECT (face, open eye, INSERT
COIN, no bands; the 8.18 is animation phase). magic_smoke: cadence
3.01, skips 0, dreq 0, [7]=1 boot-only, drift 3083/25 ~= baseline.
Shipping statics EXACT (2.44/3.37 dx=0).

**METHODOLOGY TRAPS PAID (do not re-learn):** (1) anchored truth
dumps at the eyehold anchor measure LOAD PROGRESS, not staleness —
the anchor fires at scene entry and every build shifts run timing, so
sticky=12 "measured worse" than sticky=3 on a metric that was really
the dump moment moving; (2) the "arcade clear-class rows stay zero
all scene" assumption is false at +300 frames. The parity harness
(both sides state-anchored by the same terms) is the robust
instrument; use eyehold % + the png, not raw truth diffs.

**MIKE: play rom/s16_mdbgall_pgsticky.32x (BUILD 652cb5fe).**
Questions: (a) background garbage in gameplay/after cuts — gone?
(b) any background CYCLING like pgrotor — must be none; (c) feel —
pgrotor's cost class (jitter) should be absent, cadence unchanged.
CUTBLANK verdict still open from last round. PGROTOR rom is DEAD —
discard it.

## 2026-08-16 (later) — MIKE'S VERDICT ON 652cb5fe: **"NEARLY
## FLAWLESS."** PG_STICKY FOLDED INTO THE MDBGALL BUNDLE (deep=24);
## speed + band tearing are now the front of the queue.

Ares pass, PGSTICKY build: presentation nearly perfect; residuals =
a few cloud cells in the stage-1 sky loading oddly (boxed in Mike's
frame_001980; small, transient-loading class) and the two standing
items: SPEED (slow) and band TEARING. State 652cb5fe.bs9: drift
1404/0 (catastrophic ZERO), skips 0 — but the deep-watch cost showed:
V-gate rejects 0.2 -> 3.1%, [31]=129, cadence 3.10.

Cost work, both measured on the eyehold gate:
  - **NEGATIVE RESULT: restore-narrow (pg_hot) is WRONG.** Restoring
    only changed pages (8.18 -> 26.56) — a watched page skipped at
    the flip leaves the new bank's staging stale and the NEXT k2
    capture reads that bank into truth: the bank disease at k2.
    **Capture-wide REQUIRES restore-wide.** Reverted; comment at the
    restore site.
  - pg_deep 60 -> 24 cycles: eyehold 8.21% (unchanged from 8.18);
    the k2 exposure per scene load drops 3s -> 1.2s. Ares rejects
    should land well under the 3.1% measured at deep=60.

**BUNDLE: `make MDBGALL=1` now carries PG_STICKY** (Makefile note at
the fold). Verdict rom rom/s16_mdbgall_v2.32x = MDBGALL+BQCHUNK,
BUILD 631b4875, _end 0x18fa0, stamped normal. Gates: eyehold 8.21
dx=0, statics EXACT 2.44/3.37, smoke cadence 3.01/skips 0/dreq 0/
[7]=1.

QUEUE NOW (Mike's list): 1. speed (window/ack ~205 worst, the
parked OpenLara idle-spin lever, + rejects tuning above), 2. band
tearing (3-band pipeline composing adjacent bands from successive
frames — the architecture pass, biggest lever/risk), 3. cloud-cell
load transients (cosmetic, corpus frame_001980), 4. audio (parked).
CUTBLANK verdict STILL OPEN (cuts read clean?) — fold decision waits
on it.

NOTE srcref/cannonball is stock desktop Cannonball (no 32X code);
the real 32X OutRun vibe-port is srcref/cannonball-outrun-32x
(haroldo-ok). Mined 2026-08-15: nothing new for us — its 60Hz is
span-fill rendering with no audio/DREQ/slave-SH2, its 32X idioms are
d32xr's (already mined, NOTES.md:402+). Keep for its MAME bring-up
notes only.

## GATES ON EVERY COMMIT (unchanged)

  - `tools/parity_run.sh <dir>`: title 2.44, eyehold 3.37 statics.
  - `grep ' _end$' rom/s16.lst` < 0x06019000.
  - `python3 tools/build_id.py show rom/s16.32x` — shipping stamped
    `normal`; probe builds NEVER handed to ares as the shipping rom.
  - Mike's ares play pass. Ask how it FEELS — his one-word verdicts
    have out-diagnosed the counters repeatedly.
