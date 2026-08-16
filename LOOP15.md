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
