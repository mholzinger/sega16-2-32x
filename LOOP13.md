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

## 2026-08-12 — THE 32% BLIT SKIPS ARE DEAD (item 3 closed)

Four Mike round-trips, one instrument rebuilt twice, one fix. ares
bs9 series, all BUILD-hash-verified.

**Root cause.** Handoff item 3 (ares skips 32%, MAME 0.3%) was the
band queue's two LONG SINGLE-SHOTS — `cache_fill` (adaptive 384-code
drain) and the `build_maps` terminator (~4ms uninterruptible) — both
behind the `dt<=8000` heavy gate, whose ~4000-tick budget holds on
MAME and not on ares (~3x slower SH-2). A shot started just inside
the gate straddled the vint raise by 5-37+ lines; the V-gate
(correctly) skipped those windows, and each skip shipped a
full-cycle-old band = the jitter Mike called "blitter".
SPAN_PROBE v3 attribution: 359 of 363 misses (98.9%) in stage 6,
zero in every striped phase, zero idle (the 68K always posted on
time). Striped phases with bounded quanta missed NOTHING — the
architecture was already correct everywhere else.

**Fix (`BQ_CHUNK`, m_main.c band queue).** Same total work, bounded
per-visit quanta, in-queue:
  - `cache_fill`: 64 codes/visit, stays in phase until the old
    budget (2 or 6 visits) is spent.
  - `build_maps` terminator: `build_maps_chunk` 8-row slices, one
    per poll visit, stays in terminator phase until the tail lands
    (~14 visits, a fraction of one window gap).
Measured (ares, Mike's run): skips 37% -> 0.4-0.8% of cycles,
pickups 98.2% at V=E1, cadence 3.00, restores past vblank 0.
Mike: "animations and frames are improved." Silhouettes/slabs from
the first cut: gone (corpus-verified).

**NEGATIVE RESULT — do not re-route the terminator through
maps_owed.** First cut set `maps_owed=1` and let the idle/
maintenance path drain it: 1-2 chunks/cycle against ~14 needed, so
maps landed cycles stale (black tile slabs) AND the shadow LUT
starved behind perpetually-owed maps in the maintenance slot
(every actor a black silhouette — 2026-08-12 corpus, frames 458/
1200/2400/3000). The chunks must run at full poll-visit rate INSIDE
the band queue.

**SPAN_PROBE v2/v3 (instrument, `make SPANPROBE=1` +
`tools/span_hist.py`).** LOOP 9's v1 slots [50..60] were RECLAIMED
by pivot-era counters (evictions, sprite/page/ISR work) — the v1
readout was self-contradictory (104.6% overruns, 2046 late restores
vs 11 late pickups). Audit DIAG slots before trusting any old probe.
v3: [42..49] bin every missed pickup by master poll-loop stage;
[34..41] pickup-V histogram unchanged. Also SUPERSEDED: LOOP 7a's
"every skip is on the LATE side, never wrapped" — under the pivot,
wrapped-late (V<DF, 25+ lines) was the dominant mode in one run.
Heartbeat fact: MD writes 0xD0|V before the post and every ack-spin
iteration (md_main.c:295,313), so "no heartbeat" cannot occur
mid-game; V<DF at pickup can only mean the NEXT frame's active scan.

**Z80 clipping, second root (md_start.s).** The reset-park holds the
Z80 AND the YM2612 — but NOT the PSG, which lives in the VDP. Any
channel keyed during the pre-park free-run (or the emulator's
power-on PSG state) sounded forever. Four attenuation-off latches
(0x9F/BF/DF/FF -> 0xC00011) after the park. Mike: "silent I think."
UNCONDITIONAL — lands in the shipping build too.

**Red flat-colored player (frames 1500-1560): possibly NOT a bug.**
After the second spirit ball the arcade flashes the player red until
the transform; at 20Hz we may sample mostly-red frames. Verify
against MAME before "fixing" — fidelity rule.

**Artifact shortlist, updated from the corpus pass** (replaces the
item-4 list): (1) sky tick-row — fixed row of dashes near the top,
every frame, both builds, worst by visibility; (2) right-edge
checkered strip; (3) stray sprite fragments at the top edge;
(4) purple flecks/drips, bottom corners. Mint-tinted BG remains §11
palette-precompute work.

**BQ_CHUNK graduation checklist** (it is a fidelity fix, candidate
for shipping): plain `make` gates (parity statics, region guard,
`normal` stamp), then Mike's play pass on the shipping rom with
BQCHUNK folded in. Until then it lives behind the flag.

**OpenLara/32X reference notes** (srcref mining, derive-don't-copy):
two measured negatives that match our traps — cache-as-RAM at
0xC0000000 SLOWER than cached SDRAM (their commits fedd2ed/380d137),
and VDP autofill REMOVED because the FEN spin blocked the master
(latency lost to "hardware acceleration", same trap as DREQ here).
Their structural rule: the CPU with a deadline must be idle-spinning
— timing-critical work lives on the CPU with a spare vblank,
results parked in latched mailboxes. Full report in NOTES.md.

## 2026-08-13 — TICK-ROW HUNT: transport exonerated, suspect is slot
## eviction vs the cell cursor

The sky tick-row (fixed dashes, ares-only) is MD-PLANE content.
Elimination chain, each step instrumented:

  1. sbuf zero on those rows; blit_half writes all 320 bytes of every
     row through the plain FB (m_main.c, cached-alias write loop) —
     zeros land, the 32X ships clean. NOT the 32X.
  2. MAME MD VRAM (emu.item dumps, tools/tickdump.lua, decoded
     offline) — clean plane, no ticks. NOT the shared logic, on
     MAME's timing.
  3. ares VRAM decoded STRAIGHT FROM THE SAVESTATE (plane fingerprint
     search: 80 cell bytes + 48 zero bytes at 128-byte stride finds
     the nametables; VRAM base = fingerprint - 0xC000; bs9's was
     0xD6366, little-endian words) — speckle band across the sky
     nametable rows + right-edge garbage strip. THE CORRUPTION IS
     REAL AND MD-SIDE ON ARES.
  4. MDVERIFY v2 (make MDVERIFY=1, all state in WRAM 0xFFA000):
     2688 packets, write-mismatches 0, STALE bank re-reads 0, seq
     jumps 0. Transport FULLY clean: VDP writes land, packets always
     fresh. (v1 tallied at 0xFFB0EA — collides with the palette-scan
     span max; v1's zero was void. Slot-collision trap, second time
     in two days: AUDIT THE SLOT before trusting any probe.)
  5. copy_pages/TILEMAP_U exonerated by the shipping build: the 32X
     composed layers render from the same copy and shipping skies
     are clean on ares.

REMAINING SUSPECT, with a supporting number: the pivot's code->slot
cell map. DIAG[50] (evictions — the value that corrupted SPAN v1's
readout) was 2046 in one bs9 run, against 1024 slots and ~1120
on-screen codes. Eviction reassigns a slot while the 280-cell chunk
cursor takes ~5 windows to re-walk the plane; cells touched since
their last rebuild show FOREIGN ART. Static sky tiles are the
least-re-stamped = the natural victims. MAME's demo scene showed no
speckle — possibly lighter code pressure at that anchor; unproven.

NEXT, in order:
  a. Measure the race directly: at packet build, count cells whose
     slot's md_tag no longer matches the code the cell was built for
     (a generation stamp per slot, checked per chunk). If >0 on
     ares, that IS the speckle; fix = invalidate/requeue cells on
     eviction, or reserve headroom (evict only into slots whose
     cells were rebuilt this pass).
  b. If (a) reads zero: MAME tickdump at a HIGH-pressure scene
     (Mike's play position, not the demo anchor) to test whether
     MAME speckles under the same code load.
  Sea also: right-edge strip may be the same mechanism at the
  chunk-cursor wrap column; check after (a).

The seq-freshness counters and the read-back verifier stay in
MDVERIFY; tools/tickdump.lua + the savestate VRAM decode recipe are
the era's new instruments.

## 2026-08-13 (later) — TICK-ROW, part 1: the margin garbage
## (PARTIAL — see part 2 below; the ticks-over-art survived this fix)

The dashes were VRAM BOOT GARBAGE in the nametable margin the packet
never writes, revealed 1-7px at a time by FINE SCROLL. The packet
ships 40 cols x 28 rows; fine hscroll (-(vx0&7)) reveals cols 40+
(the grey right-edge strip), fine vscroll reveals rows 28-31 at the
top (the sky tick-row). Proof: ares native capture (Tools > Capture
Screenshot — 1280x224 at exact 4x, origin (65,11), pixel-perfect
where window captures alias) at a zero-fine-scroll moment had ZERO
dash pixels on smooth sky rows; the tick scenes were nonzero-fine-
scroll scenes. bs1/bs9 offline audits had already cleared every RAM
layer (cells == md_dbg_nt mirror 1120/1120, patterns, transport).

FIX (md_bg_palette): margin cells -> reserved blank slot at init.
Slivers now show backdrop instead of garbage; arcade shows real art
there, so the 41-col/29-row packet is the fidelity follow-up.

The eviction-race suspect (yesterday's item a) is RETIRED as the
tick cause but the pressure is real: ~1120 codes vs 1024 slots,
DIAG[50] evictions high — keep it in mind for foreign-art reports.
DIAG[39] doubles as SPAN_PROBE's V=E3 bin AND the hot-evict counter
— THIRD slot collision this era; never read either while both
machineries are compiled in.

The savestate offline-audit kit this hunt built (VRAM base by plane
fingerprint, sbuf/md_dbg_nt/md_tag extraction, packet decode at
_md_pkt — symbol moves per build, always re-read rom/s16.lst) plus
tools/tickdump.lua are the reusable instruments.

## 2026-08-13 (part 2) — THE TICKS' TRUE ROOT: demand-bias starves
## the cell cursor; frozen cells + slot reassignment = foreign art

Mike's Zeus-cutscene screenshot showed the dashes OVERLAYING plane B
art — so not the plane-B margin (part 1 fixed a real but separate
artifact: the grey right-edge strip). Synced bs9 audit: sbuf clean at
the dash rows; plane A VRAM = a UNIFORM stale row (0x212B repeated,
tile fully transparent) while the md_dbg_nt mirror held varied fresh
cells. Plane A cells were GENERATIONS stale.

The freezer is the DEMAND BIAS: "when claims are outstanding, ship a
tile batch; the chunk cursor does not advance." Under sustained claim
pressure (cutscene bursts, heavy gameplay) md_pending never drains
below MD_BATCH, so cell chunks stop shipping ENTIRELY while eviction
keeps reassigning slots under the frozen cells — foreign art, i.e.
the dashes, anywhere on screen. (Yesterday's clean 1120/1120 plane-B
audit was a cursor-advancing moment; the eviction-race suspect was
the right mechanism with the wrong enabler.)

FIX: md_forced starvation bound — at most 2 consecutive demand-bias
tile batches, then a cell chunk ships regardless; reset on every cell
chunk. Worst-case cell staleness is now bounded (~24 windows for a
full 8-chunk replot at 1:2 mix) instead of INDEFINITE.

Verification recipe for the next state: mirror-vs-VRAM cell diff
(md_dbg_nt at 0x0603D200 vs decoded VRAM planes) must stay near zero
in cutscenes; the dashes must be gone from a nonzero-fine-scroll
scene.

## 2026-08-13 (part 3) — TICKS PROVEN: the receiver races the beam

The md_forced bound landed (cells 0/1120 stale on BOTH planes,
mirror == VRAM) and the ticks SURVIVED. The receiver's own V-stage
probes (0xFFB0B0-B6) close it: V entry 0x0B, after tiles 0x0D,
after cells 0x35 — the receiver writes VRAM across ACTIVE DISPLAY
LINES 11-53, the exact dash band. The VDP fetches nametable rows
while the receiver rewrites them; the transient wrong fetch recurs
every window = static-looking dashes. No RAM audit can see it, by
construction. MAME doesn't model the fetch granularity; ares does.
Parts 1-2 fixed real co-artifacts (margin garbage, frozen-cursor
foreign art) but the surviving ticks are THIS.

Root shared with handoff item 2: the window/ack span pushes the
receiver's VDP writes deep past vblank into the beam.

FIX DIRECTION (chosen): stage the packet's VDP traffic to the free
WRAM block during the window tail, play it back at the TOP of the
next vint INSIDE vblank via 68K->VDP DMA (~205 words/line: ~1050
words ≈ 6 lines — fits; a move.w loop at ~30 words/line ≈ 35 lines
would NOT leave margin). Benefits item 2 directly: the VDP writes
leave the window entirely. Alternatives considered: nametable
double-buffering (0x8000/0xA000 are free 0x2000-aligned bases —
tiles only occupy 1024 slots to 0x8000) costs either doubled writes
or flip-lag bookkeeping; window-shrink alone cannot get 42 lines of
writes inside a 38-line vblank.

## GATES ON EVERY COMMIT (unchanged)

  - `tools/parity_run.sh <dir>`: title 2.44, eyehold 3.37 statics.
  - `grep ' _end$' rom/s16.lst` < 0x06019000.
  - `python3 tools/build_id.py show rom/s16.32x` — shipping stamped
    `normal`; probe builds NEVER handed to ares as the shipping rom.
  - Mike's ares play pass. His one-word verdicts ("slow") have
    out-diagnosed the counters twice this era; ask how it FEELS.
