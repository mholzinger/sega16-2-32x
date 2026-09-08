# HANDOFF — SESSION 3 (written 2026-09-01 ~14:10, branch native1)

Read this first; PALSTATIC.md, BOSSFIGHT.md, INTEGRATION.md are the
arc records. Everything below is paid for by measurement.

## 0. ROM STATE

- **rom/s16.32x = SHIPPING = 07d77855+ (sphere build: LAUNCHEARLY + orb pair fix (one-pass) + round-clear FM gate + bottom-band backstop conditional on FG cover; promoted 2026-09-02 14:40, Mike: "good enough to continue working from, very very close to parity"; fallbacks rom/s16_fix2.32x, rom/s16_early.32x = 6282408d, rom/s16_ship_883b580e.32x = v2.1)** (Mike
  passed and promoted twice in one night). Fallbacks:
  rom/s16_palstatic11.32x (v1.1), rom/s16_native.32x (e0549e86).
- **rom/s16_boss.32x = PROBE ARM = 41d6bfc5** = shipping + the
  whole BOSSFIGHT arc M1-M7 (per-scene MDSPR, set-agnostic keys,
  boss frames in sprbake, SCALEBAKE zoom ladder, height-tolerant
  lookup, slave-busy census). Boss VISUALS passed Mike's eye.
  Candidate for promotion after his next general play pass.
- Canonical line (zsh arrays only; probe arm adds nothing — the
  flags below ARE the v2.1+boss build):

      CANON=(MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1
             FMGATE=1 R60=1 CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40
             BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BLITSHIFT=24 SPRLATE=1
             PRHOLD=6 TILECLASS=1 TXTCLASS=1 MDSPR=1 ROWDEFER=1
             PALDELTA=1 NATIVE=1 PALSTATIC=1 PALGLOW=1)
      make "${CANON[@]}"

## 1. WHAT SHIPPED THIS SESSION (all gated, all committed)

- PALSTATIC v1.1: heal channel (COMM8 0xBAD2 full re-mark; wrong
  loads self-heal ~9 vints), confirmed detect (8 glow-free probes ×
  3 consecutive landings), transform demoted to anchor-only (its
  identity IS animation), unknown-state exit reload.
- PALSTATIC v2/v2.1: the glow was GAME code animating 18 words
  (logic-clocked, level-global). 68K masks blocks 4/5 only under
  SH-2 grant (COMM8 0xBAD3/4) + 3-sentinel program check (0x99,
  0xA1, 0xA6); SH-2 animator plays baked rules at arcade 60Hz;
  continuation-confirmed reseed. **Handler 70.3 -> ~57; game share
  73% -> ~80%.** Mike's ledgers: psw=heal=grants EXACTLY, always.
- BOSSFIGHT M1-M7 (probe arm): boss renders arcade-correct. The
  speed diagnosis CONCLUDED: **the fight runs at 17-30% slave
  utilization against a 1.7-2.3v wall — choreography-bound, not
  compute.** Every diet falsified (claims: pp3+zoom+line-budget
  dead; decode: 89%->~5% miss with wall unmoved). THE 60HZ LEVER =
  GENERATION PIPELINING (overlap gens / same-vint close).

## 2. NEXT QUEUE (ordered; each bounded)

1. ~~Fixed-SDRAM map audit~~ **DONE 2026-09-01 (BOSSFIGHT.md "MAP
   AUDIT", tools/sdram_audit.py; #13=FBCLEAR, #14=snap; free: 0x398E0
   96B, 0x28E38 72B, 0x28F60 32B; .ramtext headroom 48B)**, then
   ~~the GEN PHASE-SPLIT census~~ **DONE: PHASECENSUS=1, BOSSFIGHT.md
   "M8 DONE" — wall 1.19v = echo 1.05 / mtask 1.06, ship +0.54, flip
   +0.80; the period is 2 windows for a ~1.1v job**, then the arc
   itself: **D2 maps pointer walk (+7%), C1 cat-1 to plane A (+6% on
   top; rom/s16_c1.32x = shipping candidate, NEEDS MIKE'S EYE — see
   BOSSFIGHT.md "C1"). Boss datum still needs Mike's savestate under a
   PHASECENSUS=1 build (state_health.py prints the split).** Mike's
   pass 2026-09-01 late: C1 FAILED (pulled; design for a future C1 in
   BOSSFIGHT.md), rom/s16_d2.32x PASSED = the new shipping candidate
   (promote after one more pass). New cosmetics: boss-smoke palette
   flap (scene-detect, one witness), wolf missing frames in the boss
   smoke. Gravestone: savestate anatomy banked; blue moment still
   SOLVED (collision #15, PAL_SETGEN; rom/s16.32x = 6b418bd4 shipped).
   **THE BAR (Mike, 2026-09-01 night): 60 or best-to-60 or we don't
   ship. Cadence today: light 34/59/6% one/two/three-vint periods,
   heavy 9/88/3. Report progress as % single-vint frames (PHPER in
   nat_score.py). PACE30 exists as an instrument only.** Pipelining
   step 1 done: LAUNCHEARLY=1 (rom/s16_early.32x, exact, soaked 0
   wedges, 36% single-vint) — promote after Mike's eye. Next: CHASING
   (launch during the blit, band-ordered), C1 with MD-matched pens,
   maps drain off the close path. 2026-09-02 candidates on top of
   shipping: rom/s16_l2.32x = orb pair fix + round-clear FM gate + level-2
   foreign palette anchors (Mike's eye pending). Oddity: DIAG[27]
   "wedges" 0-2 per attract run vary by build (shipping 2, orb 0, l2 1)
   — a timing race at attract cuts, pre-existing, unexplained. Then the GEN PHASE-SPLIT census (~80B
   .ramtext + 16B audited scratch): split the ~1.4v per-gen wait
   into launch/landing/mtask/close-ship. That datum starts the
   PIPELINING arc. Alternative room: SBUFCANARY -> shrink sbuf
   margin to measured extent.
2. **Zoom sweep finish + rebake**: 45/116 SPR_SNAP snapshots in
   the old session scratchpad (gone after restart — REDO the sweep:
   headless dumps of sdram:0x28400:0x800 across attract 600-5400 +
   play 400-1880, feed tools/boss_census.py --all-banks --with-zoom
   style harvesting; the bake serves any zoomed art it knows).
   Bakes the orb/transform ladders. Flicker class stays live.
3. **Gravestone heal-on-reset** (Mike sees it on-again-off-again in
   the Zeus intro; fresh-boot renders CORRECT — session-accumulated;
   suspect sticky grp ownership): NEEDS his savestate taken WHILE
   BLUE. Diff grp_key/grp_kind/CRAM vs a fresh-boot baseline.
4. Cosmetics: atomic scene reveal (shadow NT 0xD000 + plane-base
   flip), cloud strip, red orb, grass blink.
5. **Audio merge** (Mike's second Claude thread, SOUND.md/sndtest):
   INTEGRATION.md is the contract. Blockers to resolve there: the
   ~155KB free cart split (samples vs future bakes), COMM6 lab acks
   must not survive integration, COMM14 handover.

## 3. LAWS PAID FOR THIS SESSION (do not re-learn)

- SH-2-side palette state must be 68K-differ-visible; every
  divergence needs a re-ship path (heal channel = the belt; it
  caught every wrong load all night).
- Probe words: never 0x0000/0xFFFF (fade/flash attractors), never
  glow words; scene detects need 3 consecutive landings; nomatch
  counts K-vints only (threshold 16).
- The transform flash SHARES the glow blocks: masks need per-vint
  VALUE sentinels, not block identity.
- The boss heads march their palette-SET number as the phase
  counter -> set-agnostic art keys (0xFF wildcard).
- Height/zoom prefix law: same (addr,d2,bank[,zoom]) = same row
  walk; bake tallest, serve any shorter. Taller-than-baked = miss.
- **Fixed-map comments carry their era** (collisions #13 0x3A300
  squatter, #14 K2F landing reaches ~0x39B68 over the "free"
  0x39800 gap). Audit the LIVE build before placement.
- .ramtext displaces .bss 1:1 into the 0x19000 region guard — the
  guard trips on CODE growth too.
- Slave FRT = phi/8 (TCR never set): 48208 ticks/vint, 4x the
  master's 12052. (Also: the FM-park bound is ~25 lines, not the
  ~100 its comment assumed — pre-existing, load-bearing, LEFT.)
- Instrument law still rules: cross-build deltas <1.5 lines are
  layout luck. ares runs are deterministic per build (bisect trick:
  same rom = same numbers, exactly).

## 4. RIG ADDITIONS THIS SESSION

- tools/nat_score.py (promoted from tmp; +psw/heal columns).
- tools/boss_census.py: savestates -> census rows (--all-banks,
  --with-zoom); tools/glow_bake.py: glow corpus -> tables.
- Counters: 0xFFA0D6 heals, D8 grants, DA art uploads, DC-E0
  upload state (68K WRAM). SDRAM scratch: 0x28C80 slave busy
  (phi/8), 0x28C88 miss_n, 0x28C90 text_grp, 0x28CA0 shadow_cur,
  0x28C00 cram_key/keygen, 0x28E28-2C mdspr state, 0x28E30-37 glow
  state. 0x28F5C psw. Full map: INTEGRATION.md (keep it updated —
  the audio thread reads it too).
- sprbake blob now in its own cart section .sprbake @0x340000
  (768KB window, 665KB used); rom pads to 4MB/0xFF; mdsprart
  repinned 0x2F9100. The "misses" counter now includes zoom-draw
  probes (cheap; not a regression signal by itself).
- Savestate reading: build hash check FIRST (bs2 tonight was a
  stale-build leftover — the save didn't take; Mike's slot labels
  can drift). PAL_SH at state offset 0x23B+0x27000 swapped;
  SPR_SNAP at 0x23B+0x28400.

## 5. OPEN ODDITIES (small, known)

- state_health relabels still pending (pre-NATIVE labels).
- boss_smoke scene has ONE palette witness; probes could flap in
  unsampled smoke animation (watch psw churn in boss states).
- bake_sprites' offline verify for ZOOMED frames checks my two
  transcriptions against each other (no independent oracle);
  Mike's eye is the true gate and has passed it once.
- The 68K GLOWMASK=1 probe flag is now inert without a grant
  (documented in the Makefile).


## 2026-09-02 evening — BLITCHASE banked at +2.2%

rom/s16_chase.32x = shipping line + BLITCHASE=1 (5a5eae73+ base). 1092
ships vs 1068 (play2 1900), fence timeouts 0, rejects/hdlr flat. Awaits
Mike's play pass: the thing to look for is a SEAM at row 136 (master
half below, slave half above) — any tear there is a fence bug. Record:
BOSSFIGHT.md "BLITCHASE". Next: item 2, C1 with MD-matched pens.


## 2026-09-02 night — CANDIDATE rom/s16_edge.32x (+20% over shipping)

Flags: CANON LAUNCHEARLY=1 BLITCHASE=1 PENMATCH=1 CAT1MD=1 EDGE42=1.
play2 1900: 1278 ships / 42.4fps (shipping 07d77855+: 1068 / 35.6).
Soak 26000: 15680 / 37.3fps, wedges 1. Awaits Mike's pass. Things to
look at: the scroll-in column at both screen edges (EDGE42), tile
colour on sprite rows vs the rows around them (PENMATCH: should be one
colour now), any black tile (C1's old failure), the seam at row 136
(BLITCHASE). Fallbacks: rom/s16_chase.32x (item 1 only, 1092),
rom/s16.32x (shipping). Record: BOSSFIGHT.md "C1 DONE RIGHT".
Next on the 60Hz route: the maps drain off the close path (item 3).


## 2026-09-03 — CANDIDATE rom/s16_edge3.32x (edge + black-cell fix)

Mike's pass on s16_edge found black cells and tile bleed: slot-cache
thrash pinned cut mode (BOSSFIGHT.md "MIKE'S PASS ON s16_edge").
edge3 = same flags, cat-1 tiles never evict hot ways + cut mode not
re-armed by dirtiness. play2 1276 ships; soak 16771 blits (edge
15680, shipping 13127); 0 black cells in 3225 sampled soak frames.
Watch: tile bleed (pre-existing hot-evict class, now on cat-1 cells).


## 2026-09-04 — CANDIDATE rom/s16_edge8.32x

edge3 + near-pen merge (sky band flat) + vblank-gated CRAM writes with
a WRAM deferral (story-panel DAC dots gone) + refcount fix. play2 1214,
soak 15388 (family level; +15% over shipping's 13127). Grass shimmer =
arcade behaviour (BOSSFIGHT.md). Pop-in = art latency, queued. Record:
BOSSFIGHT.md "MIKE'S PASS ON s16_edge3".


## 2026-09-04 — CANDIDATE rom/s16_edge9.32x (edge8 minus the near merge)

The merge broke fades (chevrons, load-in, cloud blocks); off by default,
NEARMERGE=1 to test. edge9 = edge3 + DAC-dot gate. play2 1220. Open:
Zeus black scale-in (pre-existing, FB pairs), MD-sprite art lag (zombie
dropout / red block), grass wave phase order vs arcade (needs video
side-by-side). Record: BOSSFIGHT.md "MIKE'S PASS ON s16_edge8".


## 2026-09-05 — CANDIDATE rom/s16_zeus2.32x (edge9 + Zeus)

Zeus = the game's one-in-three ghost flicker meeting a one-snapshot
pair memory. Held-pair window (4 gens) + quick claim at launch. No
black in the headless scale-in; play2 1227; soak 15274. Record:
BOSSFIGHT.md "ZEUS SOLVED".


## 2026-09-05 — CANDIDATE rom/s16_cand.32x (zeus2 + HSSHIP + quick-claim LRU fallback)

Grass "two layers" = MD plane scroll landing a frame before the FB on
2-vint generations; HSSHIP patches the packet with the shipping frame's
scroll (band skew 68% -> ~50% of motion pairs; the rest is the flip's
own variance). Quick claim now takes the stalest held pair when none
is free (the black hit-flash frame). play2 1217, soak 15399, Zeus
scale-in no black, wedges 0. Open from Mike's list: sprite-box pop-in
(frames 3239-3245, not classified from stills), grass tone.


## 2026-09-05 — CANDIDATE rom/s16_cand2.32x

zeus2 + quick-claim LRU fallback + glow scene gate (the level-1 glow no
longer plays in the transformation scene: red flames, ring per game).
HSSHIP off (no visual gain per Mike). play2 1254.


## 2026-09-05 — CANDIDATE rom/s16_cand3.32x = cand2 + HSSHIP (ship_now rule)

Mike's captures: late-scene band skew 67% -> 45% with the sync. Next: the flip census for the residual.


## 2026-09-05 — CANDIDATE rom/s16_cand6.32x = cand3 + scroll-sync census fixes

Scroll rides every packet (two header words), stub packet when nothing
was copied, in-place re-patch at ship, explicit ship flag. Census 59/63
coherent. play2 1230, soak 15379. Watch: cut-blanks/gen 0.67 (was 0.24).
Japanese build: milestone 1 in (TOOLKIT.md).
