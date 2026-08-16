# LOOP 15 — THE SPEED LANE: SHORTEN THE 68K WINDOW, THEN TEARING

Kickoff. Read `ARCHITECTURE.md`, then LOOP14 (PGSTICKY era: the
stale-truth root fix, the PGROTOR bank disease, CUT_BLANK, and the
methodology traps). Where they disagree, ARCHITECTURE wins.

## WHERE THIS STANDS (2026-08-16, Mike's ares passes + counters)

Mike's verdict on the PGSTICKY MDBGALL build: **"NEARLY flawless…
closer to presentation perfect. The only real issues are speed and
screen tearing."** Verdict rom now **rom/s16_mdbgall_v4.32x (BUILD
eac47533)** = MDBGALL bundle (PG_STICKY folded) + CUT_BLANK + the
WINSPAN meter. Gates: eyehold 8.25 dx=0 (the PGSTICKY heal, floor —
never regress it), shipping statics 2.44/3.37 dx=0 EXACT, smoke
cadence 3.01/skips 0/dreq 0/[7]=1. _end MDBGALL 0x18ff0 — **16
bytes under the region guard; the next SH2 feature diets first.**

Closed eras: transport (magic-tail), presentation 2.0 (strobe dead),
co-owner pens, stale tilemap truth (PG_STICKY: marks enter watch,
12-quiet drop, 24-cycle deep watch on broad marks; capture-wide
REQUIRES restore-wide — comment at the restore site).

OPEN VERDICT: CUT_BLANK cut-reveal question (v3cb/v4 is the first
rom where it is actually testable — the old cutblank rom predated
PGSTICKY and its garbled backgrounds voided that pass).

## THE SPEED ANALYSIS (measured, 2026-08-16)

Mike's v3cb state: worst handler 254 lines of a 262-line frame —
window/ack 214 + tail 40. **The master's ack-spin is the 68K's
window length; the lever is MD-side work, not SH2 pickup.**
(OpenLara structural rule: the CPU with the deadline idle-spins;
NOTES.md:2110.)

Per-stage receiver costs (tools/cut_profile.lua V-probes, MAME
shape — MAME can rank the 68K's work, not the SH2's):
  - **cell-chunk consume: ~52 lines median, EVERY window** — 280
    cells read from the FB packet + staged. THE fat slice.
  - tile batches: median 0 (empty in steady state), ~65 at cuts.
  - palette stage: ~6 lines every window.

## THE JOB, in order

1. **Get the ares truth for the window split.** v4 carries WINSPAN
   (0xFFA038 sum / 3C n / 3E max, printed by state_health as "MD
   consume span"). Mike plays v4, one state → if consume >> tail,
   job 2 is confirmed as the headline; also answers CUT_BLANK.
2. **Incremental-column nt protocol (the headline).** Today the nt
   builder re-stamps the whole visible 40x28 window every rotation
   and the 68K re-reads it — that is why scrolling costs 52
   lines/window forever. Native MD games scroll by writing ONLY the
   incoming column strip and letting plane hscroll wrap (64-cell
   plane vs the 40-cell view leaves 24 columns of slack). Design:
   coarse-scroll delta per window → ship only new columns (28
   cells/column) + a slow background heal walk (1 chunk per N
   windows, keeps the eviction/palette-line healing the rotation
   provides today). Both sides change; the mirror (md_dbg_nt) and
   bs9_audit must track the wrapped layout. Expected: consume 52 →
   ~10 lines steady state; scroll cost proportional to speed, not
   screen size.
3. **Cheap MD-window trims** (independent, measure each): skip the
   48-word palette block when CRAM didn't change that window (SH2
   knows; flag word in the header); skip cell chunks whose 280
   entries match the mirror (static scenes → near-zero windows).
4. **Deep-watch cost tuning** (Mike FELT the pgsticky cost): 24 →
   lower, or spread the k2 13-page capture. Floor: eyehold ~8.2.
5. **Then tearing**: compose all three bands from ONE latched
   snapshot per cycle (pres-2.0 already flips whole frames; the
   bands just sample the game at 3 successive windows). Cheaper
   than LOOP13 feared, but do it on the headroom jobs 2-3 buy.
Parked: poisoned-packet polling (TAILPROBE first), boot-preload of
the first screen's tiles (draw-in cosmetics), audio, §11 palette
(sky = 4 blues vs arcade 7 — taste-level per Mike, on file).

## 2026-08-16 — ARES WINSPAN CONFIRMS THE LEVER; WRAP PHASE A BUILT
## AND PIXEL-PROVEN

Mike's v4 state: **MD consume span mean=47.3 lines max=73
(n=10936)** of the 215-line window/ack — the MAME shape confirmed on
ares timing. Phase B target: mean -> ~10.

**NT_WRAP PHASE A (make NTWRAP=1, BUILD 74b06396,
rom/s16_mdbgall_wrapA.32x): display math proven, bandwidth-neutral.**
Cells land at wrapped 64x32 plane positions (per-row header
(prow<<8)|c0, prow anchored on the PRIMARY vy coarse — single VSRAM
value; alt/rowscroll rows fetch their own map cells as before, only
placement changed); receiver stages up to 2 spans per row; MD runs
cell-strip hscroll (reg 0x0B=0x02, entries 32 bytes apart at 0xFC00)
with FULL per-strip values; VSRAM carries full vy. Boot blank-fills
the WHOLE plane (margins no longer exist) and zeroes the hstable.
**Gate: pixel-IDENTICAL to the non-wrap build on all five parity
scenes (ImageChops bbox None on ours-vs-ours), smoke clean.** Fine-x
parallax per band is now EXACT where the old path applied one
full-screen value — none of the five anchors happened to catch a
fine-x divergence, hence identical rather than improved.

Notes banked:
  - Region guard: NT_WRAP excludes the always-compiled SKIP-RATE
    BARS block (k2 debug bars + CRAM-255 hijack, ~150 bytes). Those
    debug pixels are ours-only content inside the capture area of
    EVERY other flavor — whether shipping drops them too (statics
    would move DOWN) is a Mike gate-rebaseline call, parked.
  - md_dbg_base[2][28] at 0x3E780 records per-row placement for
    audits; bs9_audit does NOT yet understand wrapped layouts or the
    dead margin checks — update it before the first NT_WRAP ares
    state gets audited.
  - Same-source MDBGALL rebuilds are NOT bit-reproducible (1.3M
    byte diff, behaviorally equivalent within the eyehold blink
    wobble ±0.1%) — never cmp across rebuilds to prove flavor;
    cmp only against the SAVED handed-over copy.
  - MDBGALL eyehold parity wobbles 8.1-8.3% across builds (anchor
    lands mid-blink); treat ~8.2 as a band, not a constant.

NEXT (Phase B, the payoff): per-row coarse-x trackers ship only
INCOMING COLUMN cells + strip hscroll on scroll; full-chunk rotation
drops to 1-in-4 windows (heal walk) except during claim storms
(CUT_BLANK's detector marks those); WINSPAN on Mike's state is the
scoreboard: mean 47.3 -> target ~15.

## 2026-08-16 (later) — PHASE B SHIPPED: MIRROR-DIFFED ROWS, CONSUME
## MEAN 47 -> 18 LINES; two placement bugs paid for on the way

**rom/s16_mdbgall_wrapB.32x (BUILD 21b71894) = the speed rom.**
Design pivot from the kickoff: NO new packet scheduling — the chunk
rotation stays exactly as-is (its cadence already matches AB scroll
speeds), and rows became MIRROR-DIFFED: shift the view-space mirror
by the coarse step (or invalidate on vertical/teleport), walk the
row unchanged (allocator side effects identical), ship only the
changed span. Eviction/palette healing falls out free (changed
entries diff). Plus: an ALL-STRIPS hscroll delta tail every type-1
packet (scroll smoothness needs per-window hs — the pre-wrap path
shipped a full-screen value every window), and a LOSS BACKSTOP (one
rotating force-full row per chunk visit + each strip re-ships every
28 windows: a span that never lands — stale-bank re-read after a
V-gate reject — would otherwise diverge FOREVER; the old full-chunk
protocol self-healed by construction).

**MEASURED: WINSPAN mean 47.2 -> 17.8 lines (max 70, storms) on the
65s MAME attract**; ares expects the same shape (MAME 47.2 == ares
47.3 baseline). Gates: title parity PIXEL-EXACT (46619, the stable
constant); smoke cadence 3.01, dreq 0, [7]=1, drift [5]=2109/[6]=1;
region guard 0x18d68; shipping statics EXACT after plain make.

**GATE NOTE — eyehold under NT_WRAP+CUTBLANK is judged by diff
ANATOMY, not %:** the anchor+20 races the scene's draw-in and the
eye's blink phase (observed 8.14 / 13.35 / 40.04 across builds —
the 40 was CUT_BLANK's black rows mid-load + a wide-open blink).
PASS = diff confined to the eye interior + logo, NO bands/scatter;
late-scene frame_snaps confirm rows heal.

**TWO PLACEMENT BUGS, both cost a build each (slot collisions #6
and the stack red-zone):**
  - md_pkt moved .bss -> 0x3E860 put its palette words 192 bytes
    below the 0x3F000 master stack top: the STACK clobbered them —
    green monochrome title with CORRECT shapes (CRAM garbage). The
    master stack dips >=576 bytes; anything above ~0x3ED80 is
    red-zone. md_pkt now 0x3E780..0x3ED7F.
  - trackers at 0x3E380 sat exactly on mdp_s_used ([6] 26 -> 257
    from corrupted usage masks). THE FIXED MAP IS FULL — grep every
    define before parking ANYTHING; new small arrays go in .bss and
    audits take their address from the flavor lst (md_dbg_base
    0x50b0 / md_dbg_hs 0x5040 this build).

MIKE: play wrapB. Questions: (a) speed — transitions and gameplay
should feel lighter (the 68K window shrank ~30 lines mean; watch
window/ack + deferrals + rejects in the state), (b) any NEW
background wrongness (wrap placement, scroll seams at the 64-column
wrap boundary, per-band parallax — now exact per strip for the
first time), (c) the standing CUT_BLANK cut-reveal verdict.

## 2026-08-16 (night) — WRAPB ON ARES: CONSUME WIN LANDED (47->14.8)
## BUT EXPOSED TWO REGRESSIONS; BOTH FIXED IN wrapB2 (BUILD e0c03387)

Mike's wrapB state: **MD consume span mean 14.8** (from 47.3) — the
Phase B win is real on silicon. But: dreq misaligned **1914 = 17% of
windows** (was 250-450), and the residue split says ALL tail-drain —
wrapB's shorter/variable consume moved the 68K's FIFO push into the
DMAC's busy phase; every poisoned packet = one window of stale
sprites/text = jitter that ate the felt win. And the corpus shows
the Zeus load as a ~4s foreign-art field (frame_000620): under
mirror-diff, transport losses are PERMANENT until eviction (the old
wasteful protocol was accidentally self-healing), and lost-batch
slots aren't dirty so CUT_BLANK can't blank them.

**wrapB2 fixes (rom/s16_mdbgall_wrapB2.32x, BUILD e0c03387):**
  1. PUSH-TAIL PER-WORD POLLING (md_main, NT_WRAP): the per-group
     poll admits a 4-word burst into a 6/8 FIFO and ares drops the
     burst's tail words uncounted; the last 4 groups + the magic
     pads now poll per word (~2 lines). Targets the diagnosed
     tail-drain class directly. VERDICT METER: dreq misaligned per
     minute on Mike's next state — expect back to <=450-class.
  2. REJECT-LOSS HEALING (m_main, NT_WRAP): on a master-detected
     V-gate skip, re-mark the last tile batch's slots, invalidate
     the last chunk's mirror rows, re-ship all strip hscrolls —
     whatever that window carried may have been consumed from a
     stale bank on the MD's retry. Worst case a harmless
     double-ship per skip (~1.7% of vints).
  ALSO BUILT: rom/s16_mdbgall_wrapB2_verify.32x (same + MDVERIFY) —
  ONE Mike run makes state_health's MDVERIFY section count stale/
  skipped packet generations and settle the loss mechanism
  decisively. Diagnosis rom, receiver span doubled — never ship.

Gates wrapB2: title PIXEL-EXACT 46619; eyehold 8.06 (eye-only
anatomy); smoke cadence 3.01/dreq 0/[6]=0/[7]=1; WINSPAN 17.7
(win preserved); shipping statics EXACT; _end 0x18ee0.

**ARCHITECTURE ANSWERS (Mike's three questions, banked):**
  - "Best use of 32X tiles / larger native tilemaps?" The 32X has no
    tilemap hardware at all — its only display is the framebuffer;
    OUR compose IS the 32X-native path, and the shipping flavor
    already draws every layer that way with NO load-in (nothing
    ships — the SH2 reads tilemap truth directly). The load-in pain
    is exclusive to the MDBGALL transport (MD plane cells + tile
    art must cross the FB packet at 40 tiles/window). Bigger maps
    can't ship whole: the container is the 1536-byte dead FB block
    and the MD side is cell-granular by hardware.
  - "Are load-ins just timing?" Partly bandwidth-floor (0.5s for a
    ~800-code cut, measured at the transport floor in LOOP14),
    partly the LOSSES above stretching 0.5s into ~4s. wrapB2
    attacks the losses; the floor stands. The remaining lanes for
    the floor: boot/cut-time preload while faded (parked), or
    accept the blank reveal (CUT_BLANK) as the cover.
  - Game speed still ~50%: the game's 68K main loop is starved by
    the vint handler (worst 250 of 262 lines; mean much less). The
    consume win returns ~32 lines/window mean to the game (~12% of
    a frame) — real but not the 2x Mike needs. The remaining big
    slices are the DREQ push (~596 words, protocol-fixed) and the
    tail (40). A 2x needs an architecture step (slimmer DREQ
    payload, or fewer game-visible vints), not micro-trims — design
    next loop with TAILPROBE numbers first.

## 2026-08-16 (later night) — MDVERIFY VERDICT: FB CHANNEL LOSSLESS;
## THE 13% POISONED DREQ IS THE ONE LIVE DEFECT; PUSH GUARD SHIPPED

Mike's MDVERIFY state (wrapB2_verify): **packets=7079,
write-mismatches=0, STALE re-reads=0, seq jumps=0** — the SH2->MD FB
packet channel is LOSSLESS on ares. The stale-bank loss theory is
DEAD (reject-healing stays as free insurance). His wrapB2 verdicts:
loads tightened (not free), jitter slightly down (17% -> 13%
poisoned), gameplay "really closing in".

The 13% is the ONE live defect, and the mechanism is now cornered:
wrapB freed ~30 lines of 68K consume, the master's post-ack compose
launch moved ~30 lines earlier, and compose (SDRAM) now overlaps the
68K's DREQ push whose FIFO drain (the DMAC, also an SDRAM customer)
starves — poisoned 4% -> 13-17%. The per-word tail patch only
trimmed the edge because the drops are NOT tail-confined (the "ALL
tail-drain" residue read was 3 samples — over-read, mea culpa).

**PUSH GUARD (wrapB3, BUILD 5d0e625f, rom/s16_mdbgall_wrapB3.32x):**
the master idles 1380 FRT ticks (~30 lines) post-ack before the
compose launch — returning exactly the lines wrap freed, on the side
that was idle pre-wrap anyway. The 68K keeps its win for the game.
DIAL: the 1380 constant vs the ares "dreq misaligned" meter; target
the ~4% historical class. MAME cannot measure this (no FIFO loss).
Cost visible on MAME: flip skips 1 -> 3 per 65s (~0.25% — the pad
occasionally pushes a k2 late); acceptable, watch it on ares.

**MAME parity under NT_WRAP is now a PHASE LOTTERY for anchored
scenes** (title 65.04dx24 / 59.22dx0 attractors, eyehold 8.1 / 13.4
/ 40.3): every build's timing shifts which load/animation phase the
anchor+20 catches. Judge by ANATOMY (load-in/blink content vs
garble/bands) + the shipping statics + Mike's eye. The pixel-exact
gate era for MDBGALL anchors is over until anchors get load-aware.

MIKE (wrapB3): (a) jitter — state check: dreq misaligned should
drop toward ~4%/minute-class; if still high I dial the guard up,
(b) loads/foreign fields vs wrapB2, (c) gameplay feel — the guard
costs the master, not the 68K, so speed should hold or improve.

## 2026-08-16 (latest) — PUSH GUARD DEAD ON ARES ("jitter VERY
## high"); PER-WORD PUSH SHIPPED INSTEAD (wrapB4, BUILD 989432e4)

The 1380-tick master idle pad made jitter WORSE, not better: the
master had NO slack — compose was already consuming the freed spin,
and the pad drove k2 flips past the V-gate (dropped frames; MAME
foreshadowed it, skips 1 -> 3). REVERTED same-day. LESSON: the
master's freed spin was never idle headroom; both sides spent the
wrap dividend immediately.

**wrapB4 = the deterministic fix: per-word FIFO polling on the
ENTIRE 68K push** (FPUSH macro, NT_WRAP builds; ~13 lines of 68K
paid from the ~32 the wrap freed — the game still nets ~+19). By
construction a full FIFO is now always detected before every word:
the uncounted-drop race cannot occur. MAME gates: eyehold 8.38
(eye-only), flip skips back to 1, cadence 3.03, WINSPAN 14.5,
shipping statics EXACT. rom/s16_mdbgall_wrapB3.32x is DEAD —
discard; wrapB4 replaces it.

VERDICT METER (Mike's next state): dreq misaligned — per-word
polling should collapse it toward zero (not just the ~4% class:
the race itself is closed; residual poisons would indicate a
DIFFERENT loss mode worth knowing about).

## 2026-08-16 — WRAPB4 ARES VERDICT: **dreq misaligned 1914 -> 1.**
## Jitter "extremely high -> little"; baseline NEARLY PLAYABLE.
## The DREQ loss class is CLOSED. Canonical MDBGALL build line:
## `make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1` (BUILD 989432e4).

State: skips 0.3%, deferrals 192 (lowest ever), catastrophic drift
1, spin headroom 2600/2600 (the per-word polls never actually wait —
they exist to catch the one racing moment). Handler split moved:
window/ack 167 (was 205-215), tail 87 (the poll cost lives there);
total ~unchanged at 254 worst.

## THE SPEED ARITHMETIC (Mike's 10MHz question — the honest frame)

Yes — the deficit is structural. The arcade S16B 68000 runs 10MHz
with 100% of the CPU; the MD 68000 runs 7.67MHz (0.77x) and our
vint handler takes its share of every frame on top. Game CPU vs
arcade ~= 0.77 x (1 - handler share). The game paces itself per
vint: when its per-frame work overruns the CPU it has, it takes 2
frames per step — the "~50% speed" feel. Wrap already returned ~30
lines/window; the remaining levers, in order of leverage:
  1. DREQ payload/cadence redesign — the push (~596 words x 3
     windows/cycle) is 3x oversampled for a 20Hz compose; 2 windows
     per cycle returns ~1/3 of the handler to the game. Fidelity
     tradeoffs (text refresh cadence — LOOP8's negative — and
     sprite-landing freshness) need TAILPROBE-grade measurement
     first. THE candidate for the next big step.
  2. Continue shaving consume (16 -> ~10: palette-block
     skip-when-unchanged, storm-only costs).
  3. Accept: 0.77x is the ceiling with the game on the MD 68K. The
     only way past it is moving game logic off the 68K — a
     different project.

## DEAD ENDS — DO NOT RE-ATTEMPT (paid for in LOOP11/12)

  - IDLETOKEN/IDLEGRACE (Chaotix handshake): premise is a parked
    SH-2, ours is saturated — ares cadence 3.03 -> 7.48.
  - CMDINT (interrupt pickup): prize 8.8 points, ISR alone cost
    3.1; preemption moves latency to band-completion where the
    pipeline has NO slack. LOOP11:956 has the arithmetic.
  - Dirty/block-skipping blit: 31.8% transparent vs ~25% break-even.
  - PGROTOR (LOOP14): captures outside the settled-mark discipline
    read diverged banks into truth — backgrounds cycle.
  - Restore-narrow under PG_STICKY: capture-wide REQUIRES
    restore-wide (eyehold 8.18 -> 26.56).

## INSTRUMENTS

  - WINSPAN (always-on, MD_BG builds >= eac47533): state_health "MD
    consume span" line. Relative meter — rank builds, not absolute.
  - tools/cut_profile.lua — per-frame claims/sent/consume V-stages.
  - tools/nt_dump.lua / nt_audit.py / arc_dump.lua — truth-vs-arcade
    at state anchors (TRAP: anchored truth dumps measure load
    progress; use the parity harness for verdicts).
  - tools/magic_smoke.lua, parity_run.sh, bs9_audit.py, cut_snap.lua,
    frame_snap.lua — as LOOP14.
  - `make TAILPROBE=1` prices the 68K tail (NEVER SHIP).

## TRAPS (inherited + new)

  - **Probe roms are frozen at their build commit** — before handing
    one over, check which fixes postdate it (the cutblank-rom void).
  - Flavor by cmp, never by BUILD hash; lst must match the flavor.
  - Region guard: MDBGALL _end 0x18ff0. Free scraps: 0x28F4E-4F,
    0x28F68-7F, 0x28FA8-FF (0x28F40-4D = pg_quiet/pg_deep,
    0x28FA0-A7 = CUT_BLANK counters), fixed block 0x3E780-0x3EFFF.
  - MAME cannot rank SH2-side window/pickup work (SH2 ~3x fast);
    it CAN rank 68K work shape and correctness.
  - [5] drift counter is phase-sensitive; never a gate.
  - DRQR + fixed-scrap counters need explicit boot init on silicon.

## GATES ON EVERY COMMIT (unchanged)

  - tools/parity_run.sh: title 2.44, eyehold 3.37 statics EXACT.
  - MDBGALL eyehold 8.2-8.3 (the PGSTICKY heal) must not regress.
  - grep ' _end$' rom/s16.lst < 0x06019000.
  - Shipping stamped normal; probe builds never handed to ares.
  - Mike's ares play pass — ask how it FEELS.
