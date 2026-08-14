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

(2026-08-13, Mike: colour fidelity is now LAST — "we're so close that
really it will be a matter of taste." Working order: jitter/DREQ →
audio → speed/window-ack → artifacts → §11 palette.)

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

## 2026-08-13 (part 4) — TICKS DEAD (staged DMA playback shipped);
## open regression: bottom band + dreq_incomplete 11.5%

**THE TICKS ARE GONE — Mike-confirmed on ares, BUILD 492522e8.** The
fix is the staged VDP playback (part 3's design): the receiver
appends every VRAM/CRAM write to WRAM 0xFFA400 (records: wlen,
ctrl_hi, ctrl_lo, data; header holds vsB/vsA/hscroll), and
md_stage_play flushes it at the top of the next vint, AFTER the
window post, inside vblank, via 68K->VDP DMA. Zero VDP writes
outside vblank. MAME-verified transport-correct before handoff.
Side effect, measured: worst handler total 230 -> 194 lines,
window/ack 185 -> 151 (the port writes left the tail) — real
progress on handoff item 2. Blit pipeline unchanged-healthy
(skips 1.0%, cadence 3.00, restores 0 past vblank).
All MD plane data lands one window late, uniformly (~17ms at 20Hz
ship cadence; not visible so far).

**OPEN REGRESSION, next session's first job:** the bottom third of
the screen is a mess (Mike: "hopeful this means progress") and
dreq_incomplete jumped 2.9% -> 11.5% of cycles with push_aborts
unchanged. bs9 audits: sbuf holds plausible band content, plane B
cells 0/400 stale in the bottom rows, plane decode structurally
fine — so the mess is AGAIN not in any RAM snapshot, or is a
freeze-instant compose artifact (sbuf showed sprites as sparse
1px dashes — possibly mid-compose). Working hypothesis: the DMA
halt (~6-12 lines between window post and ack-spin) shifted the
tail's phase enough to cut DREQ pushes short (the push has
deadline machinery around md_main.c:590-855 with its own history
of 5x incomplete swings — read it before touching). The bottom
band (R2, composed last from the freshest DREQ data) starves
first, which matches the mess's location.
  Leads, in order: (a) read the push deadline condition and where
  the 11.5% aborts trip; (b) consider moving md_stage_play AFTER
  the DREQ push (still vblank? push is in the tail, so likely not
  — measure); (c) shrink playback cost: coalesce consecutive-slot
  tile records into one DMA run.

## 2026-08-13 (part 4, cont.) — push_aborts was GARBAGE: slot
## collision #4; instruments rebuilt for the 11.5% hunt

Lead (a) closed at the instrument level before touching the deadline
machinery. **The "push_aborts unchanged" datum in the part-4 handoff
was a clobbered counter**: md_main's MD_BG packet receive stamped its
last-magic diag into 0xFFB0E0 — the DREQ push-abort counter — every
accepted window, BEFORE the same handler's push could increment it.
Every MD_BG push_aborts figure ever read (including LOOP 7g/8's
"aborts 0 while dreq_incomplete 14-47%") predates MD_BG or is void.
FOURTH slot collision this era. The 11.5% may yet be plain spin-budget
aborts that were invisible.

What the mechanics allow (read this before theorising further):
  - DIAG[17] counts at k1 only; prev_k=0 = the SPRITE push, always
    596 words. The 340-word TEXT short-push can NOT contribute.
  - Pushes follow accepted windows 1:1, so an incomplete sprite push
    is either a spin-budget abort (68K gave up mid-list) or a drain
    that genuinely stalled — nothing else writes that TCR.
  - Stage-buffer overflow ruled out by arithmetic: MD_BATCH=40 →
    worst tile packet 819 words (1638 bytes) at 0xFFA400, well short
    of the 0xFFB000 diag block.

Instruments (this commit; round-trip MDBGALL rom is BUILD 896f7654):
  - last-magic diag RELOCATED 0xFFB0E0 → 0xFFA020; 0xFFB0E0 is a true
    abort counter again (init 0, shipping + probe builds).
  - 0xFFA022 = SPIN HEADROOM WATERMARK: min polls left of the 2600
    budget across completed pushes (init 0xFFFF). Near 0 = budget
    marginal → the aborts are the mechanism; comfortable + aborts 0 =
    the drain itself stalls (DMAC0 starved by post-ack SDRAM traffic —
    the tail now starts ~34 lines earlier and overlaps the compose).
  - state_health.py prints both and flags pre-relocation states.
  - MAME baseline (3600f attract, MDBGALL): aborts 0, dreq_inc 0,
    wmark 2600 EXACT — the FIFO poll never once found it full on
    MAME. ares is the machine that will spend the budget.

First ares round-trip (bs9, BUILD 896f7654) — TWO findings:

  1. **TRAP, self-inflicted: `make MDBGALL=1` alone is NOT the part-4
     rom.** BQ_CHUNK lives behind its own flag; the rebuild dropped it
     and blit skips read 37.1% — the item-3 disease, un-fixed. The
     round-trip configuration is `make MDBGALL=1 BQCHUNK=1`. Write it
     that way every time.
  2. **The accidental A/B is a real datum: dreq_incomplete is
     BQCHUNK-correlated.** Without BQ_CHUNK (this bs9): dreq_inc 0.4%,
     push_aborts TRUE 0, spin headroom 2598/2600 — the 68K push has
     essentially the whole budget spare and the drain keeps up. With
     BQ_CHUNK (part-4's 492522e8): 11.5%. The suspect is BQ_CHUNK's
     full-poll-rate in-queue work (cache_fill 64-code quanta,
     build_maps 8-row slices) — SDRAM-heavy master traffic running
     exactly while DMAC0 cycle-steals the FIFO drain, where the old
     long single-shots left the bus alone between shots. Different
     play sessions, so correlation not yet causation.

NEXT: Mike round-trip on BUILD 1426c951 (MDBGALL+BQCHUNK — part-4
config with honest instruments), savestate at/after the bottom-band
mess, `state_health.py` on it. The watermark + true aborts decide:
aborts > 0 / headroom near 0 → the push is being cut (shrink/retime;
lead c coalesce; budget raise is LOOP 7b-scarred); aborts 0 with
headroom but dreq_inc high → the DRAIN stalls under BQ_CHUNK's bus
traffic → throttle the quanta while a drain is armed, or retime the
push.

RESULT (bs9, BUILD 1426c951, Mike): skips 0.4% (BQ_CHUNK restored,
jitter fix back), dreq_incomplete 8.8%, push_aborts TRUE 0, spin
headroom 2597/2600, deferrals 0 -> 44. **THE 68K IS EXONERATED**: it
delivered all 596 words with the whole budget spare, every push. The
failure is on the master side of the FIFO, correlated with BQ_CHUNK.
Two mechanisms left; the TCR residue at k1 separates them: ==256
exactly = the MD pushed the 340-word TEXT layout while the master
expected SPRITE (wskip/prev_k PHASE DESYNC — note deferrals moved
0 -> 44, and gate-rejects/idle-skips are the known desync seams);
1..8 = tail-drain starvation (DMAC0 never serviced the last FIFO
groups). DRQR probe added at 0x26028F80 ([0] ==256, [1] 1..8,
[2] other, [3] last, [4] max — first free scratch after PSRC;
audited the whole DIAG block first: slots 0-63 ALL claimed, no fifth
collision). state_health.py prints the split + a one-line verdict.
MAME smoke: boots, cadence 3.00, dreq_inc 0.
NEXT ROUND-TRIP: BUILD 489ea253, same recipe (jitter moment,
savestate, state_health). Also ask how the bottom band FEELS vs the 492522e8 pass.

Mike's play verdict on 896f7654 (same session as bs9): "Bottom
cleaned up! only issues currently blit, jitter and speed." So the
bottom-band mess TRACKS the dreq_incomplete number (0.4% here, 11.5%
on 492522e8) — supporting the R2-starves-first hypothesis — and his
blit/jitter/speed complaints are the un-fixed 37% skips from the
missing BQCHUNK flag. The 1426c951 round-trip now answers the whole
question at once: if BQ_CHUNK's return brings back both the skips fix
AND the bottom mess + high dreq_inc, the interaction is proven and
the fix is throttling BQ_CHUNK's quanta while a DREQ drain is armed.

## 2026-08-13 — MD-HARDWARE-SPRITE CENSUS (Mike's "lean into the
## hardware" question, measured against the arcade oracle)

New instruments: `tools/sprite_census.lua` (MAME arcade altbeast, live
object-RAM walk + ragged strip widths from the :sprites region) +
`tools/sprite_census.py`. Strip-walk trap worth keeping: the row end
marker is a word whose LAST-WALKED nibble is 0xF — mid-word F nibbles
are ordinary skipped pixels, and flipped sprites (d2 bit 8) walk
backward low-nibble-first. An any-F-ends-row reading collapses every
flipped sprite to ~1px (first census run was garbage this way).

Attract census, 540 samples (title + stage-1 demo + loop):

    records/frame        mean 4.7, max 18 (whole actors)
    unzoomed             86.2%   zoomed 13.8%   shadow 0% (attract)
    MD-equiv sprites     mean 7.4, max 26  vs 80/frame  — comfortable
    worst line MD-equiv  18     vs 20/line   — NO headroom
    worst line px        450    vs 320 fetch — OVER on 10% of samples
    sprite colour sets   max 8/frame
    priority             94.6% pp2, 5.4% pp3, zero pp0/pp1

Read: MD hardware sprites CANNOT replace the SH-2 sprite pass —
the 13.8% zoom residue keeps the compose alive regardless, the
per-line ceilings are at/over capacity already in attract (gameplay
is heavier), and 8 sprite colour sets would eat all four MD palettes
that the pivot's 21→8 tile merge already spends. Cross-chip ordering
is also structurally wrong: a zoomed (SH-2/FB) actor overlapping an
unzoomed (MD) actor always wins the pixel, whatever the S16 list
order said. One genuine gift in the data: pp0/pp1 sprites are ~absent
in attract, so sprite-vs-PLANE priority alone would be expressible.
Verdict: partial offload at best, palette-grind-class cost, cadence
win capped by the residue — ARCHITECTURE §4's "sprites, categorically"
now has load numbers behind it, not just format arguments. PARKED
behind jitter and audio; revisit only with a gameplay .inp census if
the cadence lanes (window/ack, §14) run dry.

## 2026-08-14 — THE PURPLE BAND IS A DEAD PLANE A ON ARES; DMA
## playback is the suspect, receive/staging exonerated from the state

Mike's stage-1 screenshot (purple repeating band over the walkway) +
bs9 (BUILD 489ea253) audited offline. Residue split from that state:
dreq_incomplete 0.9% this session, ALL tail-drain (residue 2 words,
never 256) — phase desync is DEAD as a theory; the transport was
near-clean while the band was on screen, so the band is NOT the DREQ.

The real finding, savestate-proven chain:
  1. VRAM base by mirror-anchor search (the old 80+48-zero fingerprint
     false-positives; anchor on a KNOWN mirror row instead — new
     recipe). bs9 base 0xD4366.
  2. NT A (0xC000): ALL 1120 window cells ZERO. NT B (0xE000): equals
     the FG (plane A) mirror half EXACTLY, 0/1120 — cross-checked
     against both halves; half identity proven by MD_BLANK_SLOT(1023)
     saturation. FG content is IN THE WRONG PLANE; BG content absent.
  3. MAME, same rom (tickdump): NT A written 1120/1120, NT B holds BG,
     margins 0x03FF as init wrote them. ARES-ONLY.
  4. The dead stage buffer in the state (records survive playback —
     only the header clears): a cell chunk staged ctrl 0x4C00_0083 →
     NT A rows 24-27, correctly tagged, correctly addressed, blank
     cells. So bit15 ARRIVES, the receive maps it right, the records
     are right — and those exact rows read zero in VRAM with their
     content 0x2000 higher. THE CORRUPTION IS AT DMA PLAYBACK ON ARES.
  On screen this is exactly Mike's artifact: plane A renders cell 0 =
  slot-0 art repeated, palette 0 — visible only where FG cat-0 should
  be opaque and cat-1 doesn't cover it: the walkway band. Likely the
  whole purple-blocks/drips family.

The screen's "correct" BG with NT B holding FG cells is unexplained —
possibly last-writer phase luck at freeze (both streams landing at
0xE000), possibly A13 redirection; do not build on either guess yet.

Probe added (md_stage_play, MD_BG builds): count cell records played
per plane (0xFFA024 NT A / 0xFFA026 NT B) + PORT READBACK of the
first NT-A cell just DMA'd (0xFFA028 last, 0xFFA02A mismatches).
MAME positive-verify: naplay==nbplay rates, readback real, rbmm=0.
On ares: rbmm≈naplay = the DMA'd NT-A write is lost/redirected at the
VDP (then A/B port-writes vs DMA for cells is the next build);
rbmm=0 = VRAM takes it and something wipes it later.

TRAP (new): BUILD hash is COMMIT-derived — a plain and an MDBGALL rom
of the same commit share a hash. The hash confirms the commit, not
the flags; confirm the flavor by look (pivot vs shipping renderer).

NEXT ROUND-TRIP: BUILD 18c12af3 MDBGALL+BQCHUNK, play into stage 1
until the purple band shows, savestate, state_health.py — read the
"cell records played" line.

RESULT (bs9, 18c12af3, deep stage-1 session): NT A=37072 records
played, immediate readback mismatches ZERO — and the state STILL
audits NT A all-zero with NT B == FG mirror. So the DMA'd write is
present immediately after playback and gone by freeze: either a
MID-FRAME WIPE by a non-shim writer (ares-only), or the immediate
readback is fooled (FIFO residue — though 37k matches over varied
rows argues real). Probe extended: WIPE RECHECK — re-read the same
cell at the NEXT vint's top, one game-frame later (0xFFA02C-34;
state_health prints "NT-A wipe recheck"). MAME verify: ~1/vint,
varied cells, 0 mismatches. Also noted from that hot session (14-line
margin, strobe 1.0%, deferrals 762): heavy-load window/ack is item-2
territory, parked.
NEXT: BUILD 124cec9c, same recipe. Recheck mismatches > 0 = mid-frame
wipe confirmed -> hunt the writer (game-side rebased access? vint
chain?); == 0 = the readback lies on ares -> port-write A/B build.

## 2026-08-14 (later) — RETRACTION + THE REAL ROOT + FIX SHIPPED

**RETRACTION: the dead-plane-A narrative was an audit-script bug.**
The wipe recheck came back 0/16884 (cells survive the frame), which
forced a re-audit of the auditor: band_audit.py validated a VRAM-base
candidate by requiring ZERO bytes in the plane-A margin — but the
part-1 margin fix deliberately writes 0x03FF there, so the CORRECT
base (0xD6366, same as part 1) failed validation and an alias 0x2000
lower was chosen; "NT A" then pointed at dead VRAM and "NT B" at NT A.
At the correct base: **both planes 0/1120 vs their mirrors, margins
exactly as init wrote them, MAME and ares AGREE.** Transport, staged
DMA, playback: fully healthy end to end. Everything in yesterday's
"FG-in-B / A13 flip" section is VOID. Mike's ruling stands recorded:
ares IS the hardware; the divergence was never real — both emulators
were right and the instrument was wrong. (Recipe fix: validate a VRAM
base by MARGIN CONTENT 0x03FF, not by zeros; the fingerprint recipe
in part 1 predates the margin fix and must not be reused as-is.)

**The purple band's real chain, each link measured in bs9:**
  - MD walkway rows: uniform cell 0x2128 -> slot 0x128 = solid pen-2
    grey filler, correct; staged CRAM lines 1-3 sane (greys/greens,
    no purple). The MD side is INNOCENT.
  - sbuf band rows 192-215: dense 32X FB content, pen groups 9 and 2
    — composed sprites/FG-cat-1, not silhouettes (fallbacks only
    32/5686 cycles).
  - dreq_incomplete 8.5%, residue 1-4 words, STUCK across frames:
    with less than a DREQ-burst left in the FIFO, DREQ never asserts
    and the tail never drains. landed 592-593 misses the sprite-
    snapshot gate (>=594) -> the compose keeps LAST FRAME'S sprite
    list on ~1 in 12 cycles -> R2 (bottom band, composed last) paints
    stage-1's PURPLE zombies at stale positions over the grey MD
    walkway. Location, colour, structure, counters all line up.

**FIX (md_main.c, unconditional): TAIL OVERPUSH** — after the 596th
word, push 4 dummy words; TCR reaches 0 on the real payload (TE sets,
landed=596 always) and the extras die at the next session's 68S 0->4
FIFO reset. MAME: dreq_inc 0, no session poisoning, transport
byte-identical. Gates: statics 2.44/3.37 EXACT, _end 0x06018da0
shipping / 0x06018ca8 probe, stamped normal.

VERIFY on the next ares pass: DRQR[1] (tail-drain residues) must
collapse ~to zero, dreq_incomplete with it, and the purple band
should die with the stale lists. If DRQR[1] survives, the DREQ
threshold model is wrong — re-derive the FIFO behaviour from srcref.

NEXT ROUND-TRIP: BUILD 51717680 (MDBGALL+BQCHUNK), stage-1 walkway,
savestate, state_health.py + how the band looks.

**CONFIRMED (bs9, 51717680, Mike's pass): dreq_incomplete 8.5% -> 
0.0%. ZERO. The overpush killed the tail-drain class outright** (the
DRQR split stayed silent — no incompletes to bin). Sprite lists now
land whole every cycle. Mike: "not fully clean but the most playable
rom we have had yet! in weeks!" The DREQ-threshold model (no assert
below one burst) is validated hardware fact — TOOLKIT-grade: any
S16 title's push must pad past TCR or strand its tail.

Still open in that state, ranked: strobe 1.3% of blit windows
(restores past vblank, worst 141 lines — item-2/window-ack land,
black-frame class); deferrals climbing (956; audit what DIAG[13]
actually counts before chasing); V-gate rejects 1.5%; skips 2.5%.
Mike's "not fully clean" — get the artifact list from him next pass,
then triage against the shortlist (sky tick-row is DEAD, margin strip
DEAD, purple band DEAD).

## 2026-08-14 — NEW ARTIFACT SHORTLIST (Mike's corpus pass, BUILD
## 51717680: "all in screen tearing")

1. **Scene-cut draw-in** (frames 34/250/335): splash + logo arrive in
   tiles over ~1s with rectangular holes — tile-upload burst latency;
   MD_BATCH=40/window can't absorb a scene cut's hundreds of new
   codes. Fix lane: burst/priority upload at scene cuts (game is
   fading anyway). MAME-reproducible.
2. **Bottom unset-tile garbage (513/514 + the Zeus shot) — ROOT
   FOUND, MAME-reproducible, palette-pack class.** The walkway rows
   are uniform cells (slot 0x128/0x12E/0x12F, palette line 3) whose
   pattern is a CORRECT solid-pen-3 filler — and MD line 3 pen 3 =
   0xE08 SATURATED PURPLE on both MAME and ares (td_f2400/4800/6000
   dumps). Not unset tiles, not sprites, not transport: the walkway's
   S16 colour set is packed onto a line/pen holding another set's
   purple. Same family as the mint tint (§11), but it reads as
   corruption, not taste — and it may be a targeted allocator bug
   (exclusive-pen violation) rather than precompute-lane work.
   TRACED + FIX SHIPPED (BUILD 7504a11e). Pen trace at f6000: MD line
   3 pen 3 shared by sets 74-80's S16 pixel 0 (BG colour 0 is OPAQUE,
   jts16_prio.v:87 — "empty" filler tiles render it), owner set 80.
   Exact-match sharing at claim time merges pixel-0 colours that
   momentarily coincide (fades); when the owner's live colour later
   diverges (purple 0x1C4), every co-owner's filler paints purple —
   ffc8d27's KNOWN COST at its far end. Caveat learned: f6000 is the
   SPLASH (all five sets' live colour-0 = 0x0D07 purple-blue is
   arcade-correct THERE); the parity demo anchor showed ours ~= arc
   in the bottom band on MAME except a 136px #0000F7 patch — same
   family, small; ares' claim ordering amplifies it into the band.
   FIX: thresholded drift tolerance (m_main.c drift loop): d^2 < 18
   tolerated as before (lockstep fades); d^2 >= 18 = CATASTROPHIC ->
   mdp_free_set, set re-assigns against LIVE colours at next
   note_tile, tiles re-ship within a rotation. Measured on MAME
   attract: catastrophic ~25/min, frees 143/100s, no churn storm,
   parity IDENTICAL to overpush baseline (statics 2.44/3.37 exact,
   dynamics unchanged to the pixel). DRQR[5]/[6] = small/catastrophic
   counters; state_health prints them ("pen drift" line).
   VERIFY on ares: the walkway band should now self-heal within ~a
   rotation of any divergence; watch for NEW churn artifacts (the old
   free+reassign disease) in stage 2's statue floor.

   **CONFIRMED DEAD (Mike's pass, bs9 BUILD 7504a11e): the purple
   band is cleared.** Drift split on ares: 443 small tolerated / 18
   catastrophic re-claims — the bound fires rarely and works. That
   state is also the healthiest yet: dreq 0.0%, window/ack 151 (67
   lines margin), skips 1.5%, rejects 0.7%.

## REMAINING, Mike's list 2026-08-14: slowness, tearing, missing HUD

  1. **Missing HUD/text info** (health bars, level text, score) —
     functional loss, triage FIRST: likely the text-layer path under
     the pivot (MD_BG_TEXT lane or the 32X text compose). Check
     MAME-side first (local iteration if it reproduces).
  2. **Slowness** — window/ack item 2 + the 20Hz cadence; the 151-line
     window/ack span is the lever (OpenLara idle-spin rule, staged
     work already removed the port writes).
  3. **Tearing** — 3-band pipeline composing adjacent bands from
     successive frames; wants the single-frame-flip / cadence
     architecture pass. Biggest lever, biggest risk; last.
3. **Band tearing** (514): sprite top/bottom halves from different
   cycles at band boundaries — the 3-band 20Hz pipeline composing
   bands from successive frames. Architecture-class (single-frame
   flip / cadence); park behind 1-2.

## GATES ON EVERY COMMIT (unchanged)

  - `tools/parity_run.sh <dir>`: title 2.44, eyehold 3.37 statics.
  - `grep ' _end$' rom/s16.lst` < 0x06019000.
  - `python3 tools/build_id.py show rom/s16.32x` — shipping stamped
    `normal`; probe builds NEVER handed to ares as the shipping rom.
  - Mike's ares play pass. His one-word verdicts ("slow") have
    out-diagnosed the counters twice this era; ask how it FEELS.
