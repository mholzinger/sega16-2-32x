# BOSSFIGHT 60Hz — the arc, measured open (2026-09-01, post-PALSTATIC)

The only true-60Hz arcade scene (ORACLE: 0-6% frozen pairs) runs
~19-20fps on ours. Gen wall needs ~0.65v for same-vint close; boss
scene measured ~2.3v (handoff era). BANDSHIFT rebalance is DEAD
(measured, 4 points). The lever is MDSPR claim growth.

## Census (Mike's states, 2026-09-01 — the de-risk)

- Boss-scene sprite load: 12-18 records/frame, ALL pp==2, ZERO
  zoomed = 100% structurally claimable under the v1 rules.
- ONE set dominates each moment (12 of 17-18 records) but the set
  FLIPS between 0x23/0x24 across the fight (same art, phase
  palettes: flat 0x9EF silhouette / flat white / shaded ramp — the
  red-silhouette phases ARE palette animation on the head sets).
- MDSPR today claims ~ZERO in the boss: V1_SETS=(0x09,) zombies
  only; census CSVs contain no boss frames (bots never transform).
- Head keys, enumerated from SPR_SNAP of 8 states (synthesized
  census rows — the states carry the same w0-w7 the census uses):
  ~11 keys. Workhorse 0xFCAD/0xFCBD (hflip pair, pitch 0x011/0x111)
  at h=32/48; support 0xFDBD/0xFDCD h=16, 0xFBAE/0xFBB0 h=15,
  0xFBD1 h=16, 0xF8E6 h=24. Both sets need key rows (same art).
- Anchor claim rate with heads baked: 12/18 = ~67% of boss-scene
  compose records.

## The walls (both measured tonight)

1. MD CRAM: BG owns entries 16-63 (lines 1-3, live DMA per window);
   line 0 = sprites+backdrop. NO free line — a second concurrent
   sprite line means repacking BG into 2 lines (separate arc).
2. VRAM: zombie art ~5.4K words of the 6K-word 0x8000-0xB000
   budget. The head set does NOT fit beside it.

## The design that needs neither wall moved: PER-SCENE MDSPR

The zombies and the boss never share a screen. PALSTATIC's detect
already knows the scene SH-2-side. So:
- bake_mdspr emits PER-SCENE tables: {art blob, key table, anchor
  set}. normal -> zombies (as today); boss_smoke -> heads.
- Scene switch: SH-2 posts an art-upload request (BAD-family COMM8
  code, the heal channel pattern); the 68K DMAs the scene's art
  blob cart->VRAM 0x8000 during the scene-cut storm (one-time ~11KB,
  invisible beside the load storm). Anchor variable follows scene.
- Anchor within boss: sets 0x23/0x24 flip mid-fight. Claim gate is
  pal_equal against the LIVE anchor palette (ships every frame, so
  phase animation tracks free). Switch anchor set only on SUSTAINED
  majority (the banked per-frame-flap desync, mdspr_claim comment).
- Records with unknown keys stay SH-2 (graceful, as always).

## Gates when built

Battery (no regression, play2 has no boss), then the ONLY boss rig
is Mike: his states' MDSPR_CNT deltas (0x28E20/24) across a fight +
gen-wall census + his eye. Ask for a state saved mid-boss-fight.

## Open questions for the build session

- Subsprite/SAT budget for 12 heads (nclaim<=20, nsat<=32): h=32
  heads are 2-4 subs each — count from the bake, may need cap bump.
- 0xF8E6 h=54 record: taller than MD sprite max (32px) - 2 subs,
  or skip (1 record).
- Does compose-skip of 67% of boss records actually buy the ~1.65v
  the wall needs? Measure gen wall from Mike's next states FIRST
  with claims live.

## M1 BUILT + RIG-PASSED (2026-09-01, build 7c660b46)

Per-scene MDSPR is live in the probe arm (`rom/s16_boss.32x`):
- tools/boss_census.py -> discover/boss.csv (14 head keys from 8
  states); bake_mdspr.py emits per-scene {blob, key slice, anchor}
  (normal 6.9KB / boss 9.8KB art, both fit the VRAM window; art
  rects deduped across the 0x23/0x24 set pair).
- SH-2: scene follows pscene_cur; switch posts 0xBA50|scene +
  30-vint claim suspend; boss anchor DYNAMIC 0x23<->0x24 on
  sustained majority (15 passes of other>=3 while current==0).
- 68K: 0xBA50 consume arms a chunked cart->VRAM re-upload (512
  words/vint, ~11 vints, bank-switch inside the handler is safe).
- Layout surgery this needed: .mdsprart repinned 0x2F0000 ->
  0x2F9100 (the old low gap is CONSUMED — the ROM image ends
  0x2F79xx, 248B under .palscenes; the "96KB gap" comment was
  stale); cram_key/keygen relocated to fixed SDRAM 0x28C00 (+128B
  region-guard headroom, now 0x70 free).

Rig: battery ships 987 / wall 1.15v / hdlr 58.0 (all best-or-equal),
psw 2 = heal 2 = grants 2, uploads 0 in attract (correct — no scene
switch there), play claims 983 vs 765 on v2.1 (zombie offload
intact). Attract claims 0 on BOTH builds (attract actors never
matched play-corpus keys; pre-existing, documented in the bake).

NEXT: Mike plays `rom/s16_boss.32x` through a boss fight and saves a
state mid-fight. Read: uploads (0xFFA0DA, expect 1 at smoke entry),
MDSPR claims delta across the fight (0x28E24), gen-wall mean in the
boss span, scene/danchor bytes (0x28E28/2B). Then M2 = whatever the
wall numbers say.

## M2 (2026-09-01, build 7d762211) — the 0x22 lesson

Mike's mid-fight state (bs1, saved IN the smoke with 7 live records)
convicted M1's key table: the records were SET 0x22 — a third
palette phase the two-state census never sampled — so claims sat at
ZERO through the fight (SAT 0, danchor parked on 0x23).

Two facts unpacked from it:
1. Set 0x22 ALSO tags the BOSS BODY: bank-3 pieces up to 118px wide,
   15KB of art that blows both the VRAM window and (untested) the
   H40 per-line pixel budget. boss_census now filters to BANK 2 (the
   head/debris art); the body stays SH-2-composed. The wall moments
   are the head barrages, not the light body phase (8 records).
2. The anchor generalized to N-ARY leader election (per-set counts
   over key-matched records; switch on leader>=3 while anchor==0,
   held 5 passes — the banked flap bug was list-order oscillation
   with two sets concurrently live; a clean phase change follows
   fast since SAT + palette ship in the same consume).

Boss table: 17 keys (0x22/0x23/0x24, bank 2), art unchanged 9.8KB
(the phases share rects — the cache dedupes). Rig: battery ships
980 / wall 1.13v (best yet) / hdlr 57.2; attract psw2=heal2=grants2
uploads 0; play claims 850.

NEXT: Mike's boss pass on the rebuilt rom/s16_boss.32x — a state
saved DURING a head barrage should now show SAT>0 + claim-marked
records + the anchor tracking the phase; then the capture's boss
span answers the payoff question (v1.1 baseline: ~20fps delivered
vs ~22-24 normal-play, same-capture relative gap ~25%).

## M3 (2026-09-01, build 424986b4) — SET-AGNOSTIC KEYS (the real law)

Mike's three M2 states finished the conviction: mid-smoke records
read set 0x21; with 0x1F/0x20/0x22/0x23/0x24 from earlier states,
the pattern is unmistakable — **the heads' silhouette animation IS
the palette-set number marching through a contiguous range. The set
field is the phase counter.** No finite set list can claim this.

- Boss keys are now SET-AGNOSTIC (set 0xFF wildcard; match on art
  identity addr/pitch/bank/height only). The N-ary leader election
  adopts whatever set the live records carry and ships its pens —
  every phase claims, sampled or not.
- boss_census drops its set filter (bank 2 IS the class identity).
- Bake gained the GREEDY VRAM BUDGET: keys emit count-sorted until
  the window fills; overflow stays SH-2, loudly reported (one
  144-tall single-sighting dropped). Boss art 11.9KB / 14 rects.
- Guards for the unset-anchor state (wildcard scene before first
  election): count-only passes, palette block untouched, election
  indexes clamped.

Rig: battery 982 / 1.19v / 57.7, attract psw2=heal2=grants2
uploads 0, play claims 1041. rom/s16_boss.32x = 424986b4.

NEXT (same drill, third time pays for all): mid-barrage state ->
expect SAT>0, claim-marked records, danchor tracking the marching
set. Then the capture's boss span for the payoff number.

## M4 (2026-09-01, build c03cb7e8) — THE REAL WALL WAS SPRBAKE MISSES

Mike's four M3 states finished mapping the fight:
- The smoke-arena fight is BODY-DOMINANT: ~12 bank-3 pieces (the
  animating Neff), NOT the bank-2 heads. Heads fly in other moments;
  M3's set-agnostic claims stand for those.
- Body-on-MD-sprites is DEAD, measured: the body alone hits 304 of
  the H40 line budget's 320 sprite-pixels (13 of 20 sprites) on its
  worst scanline; 17 lines already >280. Hardware would drop pieces.
- THE WALL, named by the bake's own counters: **89% SPRBAKE MISS
  RATE through the fight** (bs2->bs3 delta: 7137 hits vs 60687
  misses). The boss frames were never in the bake corpus (bots
  never reach it), so nearly every body draw took the live
  strip-decode slow path.

M4 = the boss frames in the sprite bake: boss_census gained
--all-banks (36 frames, all boss actors) -> discover/boss_all.csv;
bake_sprites globs it; pixel-identity gate passed (88K rows, 0
mismatches). Blob 763KB -> relocated to its own cart section
.sprbake at 0x340000 (it sat in .rodata INSIDE the ROM image, which
had 248B of slack; the bake's "768KB free" budget always described
this window). Rom now pads to the full 4MB cart with 0xFF (the
build-stamp tail check needs it); cart-end guard moved to 0x400000.

Rig: battery 982 / 1.14v / 57.6; attract psw2=heal2=grants2,
sprbake 0.0% miss. rom/s16_boss.32x = c03cb7e8.

NEXT: Mike's boss pass. The state to save is mid-SMOKE-fight: the
sprbake delta there is the payoff number (was 89% miss). If the gen
wall drops toward ~0.65v, the 60Hz door opens.

## THE COMPLETE FIGHT ANATOMY (2026-09-01, Mike's bracketed states)

bs1 smoke entrance / bs2 head barrage / bs3 body phase / bs4 death:

- M4 SPRBAKE VERDICT: fight misses 89% -> 19-24%. The residual is
  structural, not corpus: the barrage.
- THE BARRAGE (bs2): ~16 records, ONE art (0x01D0 bank 3), pp==3,
  ZOOMED — zoom fields 66..990 = FIFTEEN quantized steps of x66.
  Unclaimable (pp3 + zoom) and unbakeable (zoom path) under current
  laws: pure SH-2 zoom-compose. It also runs at scene 0 (pre-smoke).
- THE BODY (bs3): bank-3 pp==2, now fully baked (the M4 win); claims
  excluded by the measured line budget. Wall there: 1.85v mean,
  22.4 gen/s displayed.
- DEATH (bs4): 2.53v spike, brief.
- MDSPR claims in this fight: structurally ~ZERO (nothing passes
  pp==2 + unzoomed + line-budget simultaneously). The M3 machinery
  still serves the head-MOB moments (bank-2 fcbd art, seen in the
  older states) and the zombies; it is correct, just not the boss
  lever it was hoped to be.

## THE NEXT LEVER: SCALEBAKE (designed, not built)

The barrage's 15-step zoom ladder of one art = 15 pre-scaled frames,
bakeable exactly like sprbake (the zoom compose becomes a plain
baked blit + scale-index lookup). This ALSO delivers Mike's
"scale stepping is a must-fix" for this actor with arcade-exact
per-step pixels. Cart: .sprbake has 4.9KB spare — the ladder needs
its own budget pass (maybe demote rarely-hit corpus frames).
Second lever candidate: height-tolerant sprbake lookup (clipped
draws serve from the full-height bake's row table) — kills the
19% residual class without new art.

Both wall numbers to beat: 1.85v fight mean vs the 0.65v same-vint
close; the arcade scales in silicon, we scale in software — 60Hz
may land as 30Hz-flawless here first.

## M5 (2026-09-01, build d0363f80) — HEIGHT-TOLERANT SPRBAKE

The 19% residual's non-zoom half is gone with NEGATIVE art cost:
height is out of the bake hash and match — same (addr,d2,bank) at
any height is the same per-row walk, so a shorter (bottom-clipped)
draw is a row-PREFIX of the tallest bake. Only the tallest variant
is emitted (149 shorter variants folded, blob 763KB -> 616KB — the
freed 147KB is SCALEBAKE's budget); the index carries the row count
and a TALLER-than-baked request stays a miss (the row_off-overrun /
tan-band lesson). The caller needed zero changes (it indexes rows
relative to its own top).

Rig: pixel identity 0 mismatches, battery 976/1.21v/57.6, attract
psw2=heal2=grants2, sprbake 0.0% miss in BOTH attract AND play
(play had residual misses before). Eyehold (the heaviest sprite
scene) visually intact. rom/s16_boss.32x = d0363f80.

Remaining boss-fight misses = the zoomed barrage only (SCALEBAKE's
target). Top-clipped draws (game adjusts addr) also remain — a
separate, smaller class.

## M6 = SCALEBAKE (2026-09-01, build c9e9394f) — BUILT + PRE-GATED

The barrage's 15-step zoom ladder is baked: zoomed frames render
offline through TWO INDEPENDENT transcriptions of the runtime's
sampler — the closed-form row map (the bake) vs the iterative
yacc/ZNIB loops (the verifier) — and agree pixel-for-pixel (93,416
row renders, 0 mismatches, all 15 frames). The run format is
unchanged, so the existing baked drawer serves scaled frames as-is;
the runtime delta is one lookup param (zoom in the hash + the index's
spare halfword) and the fast-path gate dropping its zoom==0 test.
Blob 665.5KB (fits; ~103KB cart still free for audio).

Counter semantics note: sprbake "misses" now include zoom draws
probing the bake (attract shows ~4.7K/5400f from the orb class —
one cheap probe each, handler unmoved). LEVEL-1 PATTERN ITEM: a
zoom-inclusive re-harvest of the level-1 corpora would bake the orb
and transform zoom ladders too; the flicker class stays live by
design (per-pixel presence gate).

Rig: battery 972/1.20v/57.3, attract psw2=heal2=grants2, eyehold
intact. rom/s16_boss.32x = c9e9394f.

GATE LEFT: Mike's barrage pass — the baked heads' first real
render, and the fight-wall re-measure (was 1.85v; the two big
levers, body bake + barrage bake, are now both in).

## M6 FIGHT VERDICT (2026-09-01, Mike's states on c9e9394f)

- Scaled heads SERVED: barrage-span miss 9% (was 89% pre-M4,
  19-24% at M4). bs1 = the barrage (7 live 0x01D0 heads), Mike's
  eye passed the pre-scaled art.
- **THE WALL DID NOT MOVE: 1.91v barrage / 1.82v body, ~23 gen/s**
  (was 1.85v). SPRITE DECODE WAS NOT THE BINDING CONSTRAINT — the
  roadmap's P3-offload assumption is falsified by measurement,
  completing the arc's chain: claims structurally dead (M1-M3),
  decode diet delivered but wall-neutral (M4-M6).
- HYPOTHESIS THAT FITS ALL NUMBERS: VINT QUANTIZATION. Generations
  close at vint boundaries; work just over 1.0v reads as a ~2v wall
  and NO diet shows wall movement until work crosses under the
  boundary. The bakes may have cut real work a lot while staying
  above the cliff.
- NEXT INSTRUMENT (before any more levers): a WORK-TIME census —
  FRT cycles actually spent composing per generation vs the wall
  time. That single number says the distance to the cliff and
  whether the next shave flips the boss to 30fps+. Readable from
  any state so Mike's natural play answers it.

Boss visual correctness: DONE (bakes verified + his eye). Boss
speed: gated on the work-time census.

## M7 (2026-09-01, build 92c5e711) — SLAVE BUSY CENSUS

One accumulator at the slave's command dispatch (scratch 0x28C80,
ticks at phi/8 = 48208/vint — the slave never set TCR; note the
FM-park spins inside compose count as busy, so the number is an
UPPER bound on true work). Boot-zeroed. Region diet to fit: miss_n/
text_grp/shadow_cur relocated to scratch 0x28C88-A0 (the .ramtext-
displaces-.bss mechanism named at last).

FIRST READING, normal play: **~36% slave utilization at 33fps** —
the slave idles two thirds even composing. If Mike's fight states
read similarly (busy-delta / vint-delta / 48208), the boss wall is
SERIALIZATION/QUANTIZATION, not compute — and the next lever is
pipeline shape (what blocks the generation from closing), not more
diets. Battery: 996 ships / 1.17v / 56.4 (best-yet numbers, treat
as cross-build noise per the instrument law).

## THE DIAGNOSIS (2026-09-01, Mike's census states — ARC CONCLUSION)

Slave utilization through the fight (busy-delta / vints / 48208):
  smoke entry   30%   wall 2.29v   19.4 gen/s   sprbake 0% miss
  heads fight   17%   wall 1.86v   23.3 gen/s   sprbake 11% miss
  post-death    29%   wall 1.70v   23.8 gen/s   sprbake 1% miss

**The boss fight is NOT compute-bound. The compose engine idles
70-83% while generations take ~2 vints of wall for ~0.3 vints of
work.** The wall is the generation choreography itself: launch ->
landing wait -> compose -> mtask tail -> vint-quantized close, with
the master/slave round trips between. Every diet lever is now
formally falsified (claims: structurally dead; decode: delivered
89->~5% miss, wall-neutral; compute: idle).

THE 60HZ LEVER, NAMED: GENERATION PIPELINING — overlap gen N+1's
work with gen N's ship/close, and/or close a generation the same
vint its work completes. This is NATIVE-scheduler structural
surgery (gen_open/gen_ready/launch chain), a fresh-session arc.
First step there: split the ~1.7v of per-generation WAIT into its
phases (launch latency / landing wait / mtask tail / flip hold —
the chain-phase census 0x28FF8 and VISRFLIP stats are the
instruments) and pipeline the longest one first.

Boss arc state: visual correctness DONE and shipped to the probe
rom; speed diagnosis DONE and named; the build is the next arc.

## M8 PULLED — the phase-split census hit slot-collision roulette

Two placements of the gen phase census + a spr_pair relocation both
regressed the battery hard; both convictions are MAP FACTS now:
- 0x3A300: an UNMAPPED SQUATTER (battery 947/1.39v, psw 0 — slot
  collision #13). The DIRECT_FB-era MDSPR comment claims it; the
  canonical build has a live tenant nobody documented.
- 0x39900-0x3998F: the "grep'd free 0x398E0-0x39FFF gap" comment is
  WIN_TWO-era truth. Under K2F the landing arm at 0x394C0 plus
  storm extents plausibly reaches ~0x39B68 — blocks placed there
  poisoned DREQ BOTH directions (ships 515, bad1 392 — collision
  #14).
LAW: the fixed-SDRAM map comments carry their era. Before ANY new
fixed placement, re-audit against the LIVE build (dump-diff a span
across a battery run, not a grep of comments).

Reverted to clean: rom/s16_boss.32x = 41d6bfc5 (everything through
M7's slave-busy census; no phase census, spr_pair in .bss). Battery
991/1.18v/56.5, psw1=heal1.

NEXT SESSION: the phase-split census wants ~80B of .ramtext + 16B
of AUDITED scratch. Do the audit first (the dump-diff method), or
buy .bss room the measured way (SBUFCANARY -> shrink sbuf margin).
The pipelining arc's first datum is still: split the ~1.4v of
per-gen wait into launch/landing/mtask/close-ship phases.

## MAP AUDIT DONE (2026-09-01, next session) — #13 and #14 were DECLARED tenants

Method (tools/sdram_audit.py): seven ares-headless runs of the probe
arm 41d6bfc5 — play2.csv at 1/300/900/1900 frames, attract at
1500/3000/5400 — each dumping SDRAM 0x19000-0x40000 at end of run
(p1900 reproduces the battery: 991 ships, 56.5 hdlr, cadence 1.037).
A byte is LIVE if nonzero in any dump or different between two. The
declared map is every 0x[02]60xxxxx literal in sh_src, ALL #ifdef
branches. Live 127,357B of 159,744B; the cache zone 0x29000-0x39000 is
sparse by nature (112 spans) and is not free anywhere.

- **Collision #13 SOLVED. The 0x3A300 "unmapped squatter" is FBCLEAR**
  (`[2][224]` words, 0x3A300-0x3A67F, m_main.c:351, `#ifdef BLIT_SKIP`
  — canon has BLITSKIP=1). The MDSPR_SAT comment "FBCLEAR's block" is
  the DIRECT_FB branch; the grep took the alias for the only tenant.
  Live in all 7 dumps as one span 0x3A180-0x3A768 (missq tail, FBCLEAR,
  ROWLIVE/bh, DRVC). Nothing between 0x39FC0 and 0x3A7DA is free:
  missq owns 0x3A000-0x3A2FF (2 x 192 words) even where it read quiet.
- **Collision #14 SOLVED. 0x39900-0x3998F overlaps `snap`** — the
  latched layer_regs[2] at 0x39940-0x399E7 (m_main.c:1395, `#ifdef
  WIN_TWO`; the Makefile turns WIN_TWO on under R60). A block there
  garbles the scroll/page latch every window, which is the "poisoned
  both directions" battery. The "K2F landing reaches ~0x39B68" story is
  NOT supported: SPR_LAND's live extent is exactly 0x39000-0x3974F in
  every dump, md_dbg 0x39800-0x398DF, and 0x398E0-0x3993F carried no
  byte in any of the 7 runs (nothing zeroes that gap, so residue would
  have shown).
- **Verified free, the whole list** (quiet x7, undeclared, outside
  every array extent): 0x398E0-0x3993F (96B), 0x28E38-0x28E7F (72B),
  0x28F60-0x28F7F (32B). Every other quiet gap is inside a declared
  block: SPR_SNAP (0x28545-0x287FF: list was short), DIAG slots
  0x282D0-0x2831A, RG_BASE 0x39750-0x397F3 (164B, dirty masks — quiet
  at run end), mdp_s_map/qc/vol sparse rows (0x3C521.., 0x3C918..,
  0x3E3FF..), pri_lut 0x3E5F0../0x3E6F0.., md_pkt tail 0x3ED40-0x3ED7F,
  and the 0x28F80-0x28FF0 flag-gated counters (DRQR, FMGATE, 3827/4074).
  The stack zone 0x3ED80-0x40000 reads live only because R60 paints
  sentinels there at boot (p1 shows it too).
- **.ramtext headroom is 48B, not 0x70**: rom/s16.lst (14:05:48, the
  41d6bfc5 link) has `_end` 0x06018FD0. The ~80B phase census does not
  fit the probe arm as-is. Room paths, measured-first: SBUFCANARY
  re-measure (the 8 bottom rows already went; margin columns were
  load-bearing), or evict a probe-only block from .ramtext/.bss.
- Note: `.build_flags` was rewritten at 14:27 with a non-canonical set
  (`-DNO_ROW_DEFER -DSHAD_CAP=255 -DNO_SELF_CHAIN`, no R60) — some
  `make` ran after the handoff. The next canonical build MUST pass the
  CANON line; and rom/s16.32x still stamps 883b580e (verified).
- LAW, sharpened: the audit is dump-diff PLUS an all-branch #define
  sweep. Either alone was fooled tonight — the grep by an #ifdef, the
  comments by their era. The tool does both in one run (~20s wall).

## M8 DONE (2026-09-01) — the phase-split census, first datum

`make CANON PHASECENSUS=1` -> rom/s16_phase.32x (ab54b5b3). Scratch
0x28E40-0x28E7F (the audited-free span); the arithmetic is ROM-resident
and the three close sites now share NAT_CLOSE_CHECK, so the flag-off
.ramtext is byte-identical to 41d6bfc5 bar the build hash and the
flag-on image is 40B SMALLER (_end 0x18FA8). Readers: nat_score.py
(headless dump), state_health.py (Mike's savestates — the boss fight
is only reachable that way).

Battery play2 (33fps, 987 gens / 1874 vints = 1.90v period), v/gen:

    launch -> slave echo   1.05  (max 2.26)
    launch -> mtask done   1.06  (max 5.38)
    close lag              0.02  (max 5.38)   wall 1.19
    close -> blit done     0.54  (max 4.33)   = wait for the next window + 0.24v blit
    blit  -> ISR flip      0.80  (max 4.82)   = wait for the next vint

Attract 3000f: wall 1.37 = echo 1.31 / mtask 1.01 / lag 0.02; ship
0.52, flip 0.77.

Reading: the wall sits JUST over one vint — both halves (slave chain
and master maps drain) land at ~1.05v — so the close misses window W1
by a hair and ships at W2: a 2-window period for a ~1.1v job. That is
the vint quantization M6 hypothesised, now measured. The blit is
window-bound because the window IS the SH-2's FB ownership slot (the
68K owns the FB in the gap for its rebased VRAM writes), so
close->ship cannot move out of the window; the lever is the compose
wall: get echo AND mtask under ~0.9v and most generations close before
W1 — the period halves without touching the blit. Slave utilization
(M7 census) says the slave is not compute-bound at 1.05v; what it
waits on inside the chain is the next question (see below).

### M8 datum 2 — the vint timeline (PHASECENSUS v3, battery play2 33fps)

Offsets into the vint (master FRT, means):

    0.00 window pickup     0.23 landing done     0.30 ack (FM drops)
    0.34 blit done         0.46 LAUNCH            1.00 next pickup
    launch -> slave picks the cmd up: 0.15v (SYNC[13] seen)

So a generation gets 0.54v of gap before W1, and the slave starts
composing 0.15v into it. Master gap budget per vint (DIAG sums):
maps drain D[11] 0.335v (= 0.63v PER GEN, the whole mtask), blit
D[5] 0.13, window D[8] 0.31, D[63] 0.08, D[45] 0.05, D[2] 0.05.
SH-2 PC profile (ares --profile, 1900f): slave = 47% compose
(compose_layer_regs 22.6, slave_concurrent_k 20.0, text 4.6), 8%
blit half, 4% text capture, 39% idle spin; master = m_main 57%
(one 512B bucket at 0x6004400 alone 19.5%), blit_half 12, cap_drain
8.6, flip_span 8.2 (ROM spin), visr 5.1, cram_paint 3.5.

Latency histograms (0.125v bins from 0.5v, launch-relative; W1 pickup
= 0.54v, W1's ack = 0.84v):
  echo : 242 29 35 37 | 219 139 68 | 221   (25% before W1; 36% right
                                             after W1's park; 22% 2+ windows)
  mtask: 208 16 22 110 | 327 186 29 | 92    (the maps drain spills past W1)

READING. Two things gate 60Hz and both are now numbers, not folklore:
1. The MASTER's maps drain (build_maps_chunk, 0.63v/gen) is on the
   generation's critical path (close waits for mtask) and does not fit
   the 0.54v gap; it finishes in the gap AFTER W1. Diet or de-path it.
2. The SLAVE's chain (0.68v busy + 0.15v pickup latency) also just
   misses W1 in most gens, then parks through W1's FM span (0.30v).
Everything after the close (ship 0.53, flip 0.78) is window/vint
quantization that follows from WHICH window the close lands in; it is
not separately reducible. The pipelining arc's order: maps-drain diet
or de-path -> slave pickup latency -> launch earlier than 0.46 (the
0.12v of post-blit, pre-launch work) -> then the FM park trade.

### E1 LANDPARK — NEGATIVE (2026-09-01)

`LANDPARK=1` (slave parks only announce->landing-done, SYNC[12]
cleared at landing-done): battery IDENTICAL to control — echo 1.06,
bins 243/29/35/37/220/135/67/226 vs 242/29/35/37/219/139/68/221,
ships 992 vs 990, rej/bad1/hdlr unchanged. The FM park is NOT what
holds the chain past W1. Flag kept (harmless, documented) for the
day the chain is short enough for the park to matter.

What holds it: the slave's COMPOSE. Profile (1900f, per gen):
compose_layer_regs 0.43v + slave_concurrent_k 0.38v + compose_text
0.09v = 0.89v of compose, + blit half 0.15v + text capture 0.08v
landing mid-chain = ~1.1v of slave work per generation. The M7 "0.68v
busy" census undercounts (it brackets the band calls only). The
slave IS compute-bound in normal play; the boss fight's 17-30% is a
different regime (Mike's savestate under PHASECENSUS will split it).

Structural floor under the current scheduler: period >= compose
(0.9) + blit (0.25) + landing/harvest (0.3) > 1v, quantized to 2v.
Levers left, in order: (1) compose diet — compose_layer_regs is the
biggest single block; (2) CHASING — launch gen N+1's compose while
gen N's blit is still reading sbuf, ordered top-down (the blit runs
~3.5x the compose's row rate); (3) the maps drain off the close path.

### D2 — maps drain pointer walk: +7% fps, exact (2026-09-01)

bm_scan_rows recomputed the page/row/column address per tile; the 44
columns of a row are consecutive words bar one page boundary. Pointer
walk (index formula proven offline, 20000 random cases): battery ships
990 -> 1060 (32.8 -> 35.5 fps), wall 1.18 -> 1.04v, mtask 1.03 -> 0.79v,
maps drain D[11] 0.335 -> 0.246 v/vint, .ramtext -16B (flag-off _end
0x18FF0). echo also 1.06 -> 0.97 (the slave stops waiting on the
master's blit-half/harvest timing less often). D2b (the same walk in
the FG column scan) bought +7 ships for 88B of .ramtext and broke the
flag-off region guard: REVERTED (fade101). Pixel gate: run WITHOUT
SPRTRUNC (Makefile:527) — the SPRTRUNC build's 69%/44% title/eyehold
diffs are the frozen-sprite class, not a regression.

### M8 datum 3 — the slave's chain, band by band (2026-09-01)

One-generation slave/master event traces (ring at 0x398E0, since
retired for the region guard; decoder tools/gen_trace.py now reads the
sums), battery play2, times in vints after the launch:

  gen 300 (fast): bands end +0.06/+0.12/+0.31 -> echo +0.32 -> flip
    +0.62 -> W1 pickup +0.69 -> blit-half +0.71..0.73 -> blit done
    +0.83. ONE-window period.
  gen 900: bands +0.14/+0.31, textcap +0.37..0.40, [flip +0.41, W1
    pickup +0.50, landing +0.55, ack +0.61], band2 end +1.02.
    Band 2 ran straight through the window: no park (NO_SELF_CHAIN
    compiles the FM park out — LANDPARK was a no-op by construction),
    only a 0.036v text capture.
  gen 600: band2 0.91v; slave idle 0.36v after the echo waiting for
    W2, blit-half +1.66..1.80, then idle to the next launch +2.04.

Per-band means over 1050 gens: band0 0.150v, band1 0.194v, BAND2
0.589v (rows 144-224 = 63% of the chain). The "0.15v launch->pickup
latency" of datum 2 is an ARTIFACT: the master's first poll after the
launch is 0.15v later (its window tail); the slave picks the command
up within 0.002v (gen 300/900 traces).

MD_BG_FG0 is in canon (Makefile:148): FG cat0 already lives on MD
plane A. compose_layer_regs' 0.43v/gen is therefore the FG CAT1 pass
ALONE — priority tiles over sprites, a full-screen sweep every
generation, ~222 tiles drawn per gen at 85% opaque pixels. Sprites are
97% pp=2, 3% pp=3, 0% pp<=1 (play census, 52k record-frames).

Pass split over 1046 gens (slave, all bands, v/gen): clear+sprites
0.386, FG CAT1 0.436, text ~0.11 (remainder). Chain 0.93v.

**THE LEVER, NAMED (C1 — "cat1 to plane A"):** FG cat0 already lives
on MD plane A (MD_BG_FG0). Emit the FG cat1 cells there too WITH THE
MD PRIORITY BIT (bit 15: plane-A high cells sit above MD sprites,
which is exactly S16 cat1-over-pp2), and restrict the framebuffer's
cat1 pass to the rows/strips where SH-2-composed sprites were drawn
this generation (ROWLIVE rows; then per-8-row-strip x extents, 56B at
0x398E0). Outside sprite rects the FB is 0 = MD-through = plane A's
own cat1 copy; inside, the FB cat1 tile covers the sprite exactly as
today. pp=3 sprites (3%) keep their per-pixel gate; MD-claimed sprites
are pp=2 only. Expected: cat1 0.44 -> ~0.1v/gen, chain ~0.6v = the
one-window threshold. The same staleness class as cat0 on the MD (one
generation of packet lag on scroll-in) — already accepted.

## C1 — CAT1 TO PLANE A (2026-09-01 evening): +6% fps, candidate rom/s16_c1.32x

`make CANON CAT1MD=1` (flag-off _end 0x18F30; the flag is a shipping
candidate, not a probe). Three steps, each measured on the battery:

  step 1  cat-1 cells emitted on plane A with the MD priority bit
          (were blanked); FB unchanged.        ships 1055 = control,
          hdlr +0.5 lines, md_tag claims +13%.  Pixel-neutral by
          construction (FB wins where opaque, both transparent in holes).
  step 2  FB cat-1 pass only on rows with SH-2 sprites (ROWLIVE);
          MD-claimed sprites pri 0.              ships 1202 (40.0fps),
          wall 0.83v, echo 0.77v.  BUT: a freshly scrolled-in cat-1
          column shows BLACK for a generation (MD packet lag) — frame
          1400 of play2, white in base and step 1.
  step 3  FB keeps the two edge column pairs (0-1, 39-40) and any
          tile-row whose MD cells are pending (CAT1_PEND[28] at 0x28F60,
          written by the walk: slot dirty or colour set unassigned).
          ships 1119 (37.1fps), wall 0.93v; the edge column is white
          again at 1400; 1700 clean.  Edge-only variant (no pending
          rule): see the line below this entry.

RAM: the master's stage-1 row compose is compiled out under the
all-slave split (NAT_ALL_SLAVE; docs/design/ORACLE.md measured BANDSHIFT dead) and
spr_pair moved to the slave stack floor — C1 fits both arms.

WHAT MIKE'S EYE MUST JUDGE (cannot be gated here):
- cat-1 tiles now render in MD 9-bit colour like every cat-0 tile
  (mdp_quant rounding), where the FB drew them in 15-bit. Consistent
  with the rest of the tile art; the pillars/grass/foreground stones
  are the places to look.
- pen exhaustion on the three plane lines: a cat-1 set with no line
  falls to the FB via CAT1_PEND, but a set that got a line with
  "nearest pen" approximations (mdp_claim_pen fallback) renders
  approximated on the MD. Same class as cat-0 today.
- The MAME parity statics are INVALID for NATIVE builds: both the base
  and every C1 gate capture are confetti (parity_gate_*/eyehold_ours
  .png) — MAME does not render the MD-plane scheme. The 2.44/3.37
  reference numbers are canonical-era. Fixed-frame ares diffs across
  builds are dominated by generation-boundary shifts (instrument law);
  only static frames (title 200/400) compare exactly, and they match.

Edge-only variant (pending rule off): ships 1099 — within layout
luck of step 3's 1119. So the 83 ships lost against step 2 are the
EDGE COLUMNS, not the pending rows: drawing columns 0-1/39-40 marks
the row live (RL_MARK) and the blit stops skipping it — the blit win
of step 2 evaporates on every row. The right fix is upstream: make the
plane walk emit ONE COLUMN BEYOND each screen edge (bm_scan_rows'
"-1..42" idea: cbrow/mrow 40 -> 42, hdr c0 one earlier; the 68K span
apply already wraps at 64), so the MD has the cell before it scrolls
in — that also retires the same one-generation stale column the cat-0
tiles have today. Then the FB edge fallback goes and step 2's 40fps
comes back. NEXT SESSION, first item after Mike's eye on rom/s16_c1.32x.

Soak: see the play_native3 line below (26k frames, wedges/skips).
Soak (rom/s16_c1.32x, play_native3.csv 26000f): ships 13665 (32.4fps
on that heavier mix), wall 1.02v, wedges 0, skips 0, rej 0.18%, bad1
154, hdlr 52.0.

### C1 — MIKE'S PASS: FAILED (2026-09-01 evening). Pulled from the candidate.

Three regressions, all consequences of one layer split across two
renderers: (1) BLACK TILES (frame_003369): a cat-1 tile that misses
the SH-2 art cache used to keep last frame's FB pixels ("stale beats
wrong"); now the row is cleared, the FB draws nothing, and the MD cell
there is blank -> black. (2) A SHADOW BAND FOLLOWING SCALED SPRITES
(frames 1769-1796, grass colour too): rows with SH-2 sprites are
FB-drawn in 15-bit colour, the rows around them MD-drawn in 9-bit
(mdp_quant + nearest-pen) — a horizontal stripe of different tile
colour rides with every sprite. (3) The scroll-in edge column (fixed
in step 3, at the cost of the blit-skip win).
The smoke effects (player rise, boss entrance, boss death) are perfect
on this build — that is the D2/census work, not C1.

C1 stays a flag (CAT1MD=1) for a future arc whose design must be:
FB cat-1 pixels use the MD's OWN pen colours (32X CRAM tile-group
entries quantised to the MD line's 9-bit pens, nearest-pen included),
cache misses fall back to the MD cell (draw nothing only when the MD
cell is known good), and the plane walk emits one column beyond each
edge. Candidate for Mike's next pass: rom/s16_d2.32x (D2 + the RAM
moves, no C1) — exact by construction, +7% on the battery.

## GRAVESTONE — first look with Mike's blue states (2026-09-01, evening)

rom/s16_c1.bs1-4 (build 01fbb9c2+ = C1 step 3). Method banked:
- SDRAM in an ares state at +0x23B (state_health's sd), word-swapped.
- VDP VRAM: find sh_src/md_sprart.bin bytes 0x40.. WORD-SWAPPED in the
  state; VRAM base = hit - 0x40 - 0x8000 (bs1: 0xD6366). VDP CRAM: the
  MD-format pens of line_c (bbb0ggg0rrr0) little-endian, first hit
  OUTSIDE the SDRAM block (bs1: 0x54D51-2-32).
- Name tables vs the SH-2 mirror (md_dbg_nt 0x3D200, rotate each row
  by md_dbg_base's hdr: col base hdr&63, VRAM row (hdr>>8)&31): ZERO
  mismatches on both planes in bs1. VDP CRAM lines 1-2 == line_c; line
  3 has 5 pens off by one level (sets 19/20 — a refresh in flight).
- Pen bookkeeping clean: no visible cell on an unassigned line; pen
  drift d<=3 (one level).
Finding: the gravestone's cells in these states are CAT-1 cells on
plane A (0xC310.., priority bit) — i.e. C1's MD rendering of the
gravestone with sets 85/86 on line 2 (fresh boot: line 1). So these
four states diagnose C1's gravestone (the 9-bit/shared-pen class),
NOT the pre-C1 bug the handoff queued. Mike: recapture WHILE BLUE on
rom/s16_d2.32x; the tools above then apply unchanged (tile_grp at
0x3E480, cram_key at 0x28C00 are the 32X-side suspects there).

### GRAVESTONE on rom/s16_d2.32x (Mike's bs1-4 of 3287234d, 2026-09-01 late)

Method extended (all offline, from the state + headless dumps):
- The game's palette RAM is FB staging +0x1F000 (md_main.c:65); in an
  ares state it is little-endian at 0xC910B for these states (find it
  by searching a distinctive 8-word set of the PAL_SH shadow outside
  the SDRAM block). ares-headless can dump "32X DRAM" (both FB banks)
  and "32X CRAM" directly, so any headless frame gives game palette +
  shadow + 32X CRAM + FB pixels.
- Diff game palette vs shadow: bs1 112 words (28 tile sets, ONE FADE
  STEP behind — the last vint carried pal=33 lines, a storm mid-flight),
  bs2 24 words (1 LSB), bs3/bs4 only the cycling sets 19-21 (the glow).
  The 32X group mirror == shadow for the gravestone sets (75/85/86 ->
  groups 4/5) in all four; MD lines/pens consistent, d<=3.
- Rendered both layers of bs1/bs3 from the state through the port's own
  paths (MD pens for plane tiles, FB group colours for cat-1): the tomb
  is grey in both. Headless D2 at frame 619 (intro, Zeus): the start
  gravestone is grey with moss = correct. So a fresh boot is right, the
  usual-spot states are right, and the blue moment is NOT in hand.
  Mike: save state WHILE the stone is blue (the intro, after a session);
  the diff scripts above then say in one run whether it is the shadow
  (lost push), the group map (tile_grp/cram_key), or the MD pens.
- Torn/poisoned landings DO post 0xBAD1 (m_main.c ~8079) and the 68K
  re-marks that push's pal ids (md_main.c ~1508) — the belt exists; a
  session-accumulated divergence would have to slip past it (e.g. a
  packet skipped WITHOUT landed>0, or a PALDELTA shadow lie).

### Mike's D2 pass (2026-09-01 late): PASSED, cosmetics noted
rom/s16_d2.32x: C1's regressions all gone; smoke effects (player rise,
boss entrance, boss death) feel right; a small slowdown on sprite-heavy
screens (expected at 35fps). Open cosmetics from this pass:
- BOSS-SMOKE FLASH (frames 6546/6558/6560): a palette flap between two
  scene tables — score/INSERT COIN text green vs blue-white, sky purple
  vs teal, frame to frame. The boss_smoke scene has ONE palette witness
  (HANDOFF-SESSION3 §5 oddity) and the smoke animation walks through
  unsampled palette states: PALSTATIC scene-detect flap. Fix in the
  PALSTATIC machinery: more witnesses for boss_smoke + a longer confirm
  hold while smoke is on screen (psw churn in a boss-smoke state is the
  instrument).
- WOLF MISSING FRAMES while our wolf moves during the boss smoke
  entrance: the sprbake zoom/frames corpus (the zoom sweep redo, queue
  item 2) or MDSPR claim suspension (30 vints after a scene cut) — a
  boss-smoke savestate under the census build splits it (misses counter
  vs mdspr_sus).

### BOSS-SMOKE FLAP FIX candidate: rom/s16_flap.32x (2026-09-01 late)
Arcade oracle: ref_013560 (teal sky, GREEN text, Neff standing) ->
ref_013700 (the whole world desaturated grey, smoke up). The smoke is a
FADE. Our two baked tables differ in 248 words (set 5 = the text set:
normal blue-white vs boss_smoke green; sets 74-101 = the desaturated
tint; 19-21 the glow phase), and the fade steps re-match one table's 8
probes then the other's, each held 3 landings -> whole-image swaps =
the flash (6546 purple sky/green text, 6558 teal sky/blue-white text).
Fix (m_main.c detect): boss_smoke is DETECT-ONLY (pscene_cur=1 for the
MD boss-art upload; no PAL_SH image load, no heal storm, no setgen
bump — the deltas carry the fade exactly, the transform precedent) and
leaving boss_smoke needs 30 consecutive landings (entering stays 3).
Battery and attract unchanged (psw 2). NOT verifiable on the rig (the
bots never reach the boss): Mike's boss pass decides. Not promoted.

## GRAVESTONE SOLVED — slot collision #15 (2026-09-01 night, Mike's s16_flap.bs1)

The state was saved WHILE BLUE, 7 seconds after boot (vints=420, the
intro, lightning on screen). Walked every layer of the stone's pixels:
- Plane A cells under the stone are the blank tile; plane B is wall
  art; the STONE IS SPRITE RECORD 0 (y 120-184, x 263, colour set 0,
  bank 3, pp 2). FB pixels under it index CRAM 34/51/... = pair 14.
- Game palette == PAL_SH shadow for set 0 (greys). spr_pair[set 0] =
  pair 14. cram_key groups 28/29 = 0x2000 (sprite set 0) — the memo
  says pair 14 holds set 0 at generation 0. cram_mirror pair 14 holds
  0x7d80 0x7fff 0x7c00 0x7000 ... — a BLUE-WHITE ramp, the intro
  lightning flash that the game writes into sprite palette 0 first.
- Why the memo never repainted: PAL_SETGEN sat at 0x28C00 for 384B
  (192 u16) — over cram_key (0x28C00), cram_keygen (0x28C40) AND
  MDSPR_SAT (0x28D00). Sprite set 0's generation = MDSPR_SAT[0],
  zeroed by mdspr_claim every generation, so cram_memo(pair 14, set 0)
  hit forever; the first paint (blue-white) stood until another set
  displaced the pair (the on-again-off-again). Tile sets 0-31's
  generations were the memo keys themselves. The MAP AUDIT listed
  both tenants and missed the overlap: the audit tool flags declared
  addresses, not extents — EXTENTS are the next audit feature.
Fix: PAL_SETGEN -> 0x3F880-0x3F9FF (slave stack floor, after spr_pair;
sentinel paint from 0x3FA00). rom/s16_gen.32x = D2 + flap fix + this.
Battery unchanged (1041 ships); attract REMAP paints 49937 -> 43827
(the memo lied both ways). Mike's eye on the intro decides.

## CADENCE A/B for Mike's "frame drops on the player" (2026-09-01 night)

Census gained a SHIP-PERIOD histogram (PHPER at 0x39920, bins 1/2/3/4+
vints; nat_score.py prints it). Shipping 6b418bd4 on the battery:
  periods 1v 34% / 2v 59% / 3v 6% / 4v+ 1%  (34.8fps)
i.e. every third frame or so arrives a vint early and the next a vint
late — the player's motion alternates 1- and 2-vint steps at random.
That IS the "small sense of frame drops"; the game logic runs at 60.
`make CANON PACE30=1` (rom/s16_pace30.32x) launches a generation only
every 2nd window:
  periods 1v 2% / 2v 92% / 3v 5% / 4v+ 1%   (29.0fps, steady)
Eye A/B: steady 30 vs irregular 35. Not promoted — Mike decides.
RETRACTED (same night, Mike's call): the 68K-clock comparison above
was asserted without a measurement and it is the wrong model — the
render is on the SH-2s, nothing races a beam, and the 68K only matters
if the game's own frame logic plus our handler overrun a vint. The
rig says they do not: V-gate rejects 0.4-0.5% in every state Mike has
sent. MEASURED instead — the heavy mix (play_native3, 26000f):
  periods 1v 9% / 2v 88% / 3v 3%   (~31fps, nearly steady)
vs the light battery's 34/59/6. So heavy screens are a flat 2-vint
cadence and light screens an irregular 1-or-2: the "general slowness"
is the rate dropping from ~35 to ~30 as scenes get heavy, and the
"frame drops" are the light scenes' irregular steps. One cause, the
compose wall vs the window; one fix, 60Hz (the pipelining arc).

## PIPELINING STEP 1 — LAUNCH EARLY (2026-09-01 night): candidate rom/s16_early.32x

The NATIVE launch is now one function (nat_window_launch) with two
call sites; `LAUNCHEARLY=1` calls it just before apply_cram instead of
after the ack. The launch needs the harvest, the reg latch, the page
copy and the pair claim — all done by then; CRAM paints, skip bars,
DREQ re-arm, publish and the ack do not feed it. Picture unchanged by
construction.
  launch offset 0.43 -> 0.38v; battery ships 1045 -> 1056/1068;
  single-vint frames 34% -> 36%; 3-vint 57 -> 26 (of ~1060);
  soak play_native3 26000f: ships 13665 -> 14060 (+2.9%), 0 wedges.
The one "wedge" seen on the first census build was a PROBE ARTIFACT:
the census's ROM-resident poll (nat_ph_check) accumulated a pickup sum
per poll; retiring it removed the wedge deterministically (the flag-off
arm never had it). Law: a probe that runs on every poll perturbs what
it measures — keep per-poll census work RAM-side and tiny.
Where the rest of the vint went (measured): landing 0.25, blit-done
0.34, launch 0.38. The next 0.1-0.3v is CHASING (launch while the blit
still reads sbuf, ordered by band), then the compose diets (cat-1 with
MD-matched pens, C1 done right), then the maps drain off the close.

Mike's pass on rom/s16_early.32x (2026-09-01 23:00): "a MUCH better
feeling build than the 30-frame build" — PROMOTED (6282408d). The
3-vint tail halving is what the hand feels; steady-30 was not it.
Open cosmetic from this pass: the level 1 -> level 2 transition screen
does not display the purple circle (the round-clear orb) — a sprite
that never lands in the FB: sprbake miss (zoom ladder, queue item 2)
or a pair/claim class; a savestate on that screen decides it.

## LEVEL-TRANSITION ORB (2026-09-01 late, Mike's s16_early.bs2): SOLVED, candidate rom/s16_orb.32x

State anatomy: exactly one live sprite record (the orb: set 0, bank 3,
addr 0x7EF, pitch 13, y 72-120, x 168), SPR_SNAP == SPR_LAND, not
MD-claimed, sprite ROM art present at that address. sbuf held 1884
nonzero pixels, ALL inside the orb's box, ALL pens 240-254 = pair 15,
the shadow ramp = the drawer's fallback for spr_pair == 0xFF. The
current parity's map (nat_par 1) had spr_pair[1][0] = 0xFF while
parity 0's had 13, pr_key[13] = set 0, every other pair free, no tile
squatters (grp_key only group 1), BM's last build (par 0) had sused[0].
So the orb ENTERED the snapshot at this generation's launch and was
absent the generation before: the maps are built one generation behind
from the snapshot, and the game shows this orb on alternate frames
(30Hz blink). Every visible frame therefore met a map built from an
absent frame -> ramp -> invisible, forever.
Fix (bm_tail): held pairs follow their sets into every map — after the
0xFF reset, `spr_pair[par][pr_key[q]] = q` for every owned pair; the
used-set loop re-assigns as before. PAIR_HOLD already keeps the pair
90 passes, the map just never said so for sets absent this snapshot.
Exact for everything that was already drawn. Battery 1073 ships, soak
26000f 13517 ships / 0 wedges / 0 skips. Mike's eye on the transition.
Tools banked here: FB banks in an ares state = the line table (first 32
bytes of a headless "32X DRAM" dump), word-swapped, bank 1 at +0x20000;
sbuf from the .lst symbol; both render through cram_mirror.

### Round-clear crystal ball (s16_orb.bs1): the BALL is missing, not the orb

The orb sprite now draws (the held-pair fix holds). What is missing is
the crystal BALL itself: arcade ref_016270 shows a purple field, the
ball as tiles, and the small glowing orb sprite inside it; ours shows
the orb on black. In the state the GAME'S OWN video RAM has nothing
else: tile pages 0-11 empty in FB staging (both banks), page 12 holds
level-1 residue (rows 16-28), text RAM empty, sprite list = the orb +
EOT, live page regs (WRAM text 0xE80..) = BG page 5 / FG page 0, and
page 5 is empty in staging. So the game's writes of the ball into tile
RAM never reached staging: an UNPATCHED WRITER (patch_game.py rebases
literal addresses; TILE_DIRTY_SITES is a curated list; the ball's
drawer is neither). Next: tools/tilewrite_probe.lua on the ARCADE in
Mike's MAME session logs every tile-RAM writer PC by page through the
round clear; the PC not in patch_report.txt / TILE_DIRTY_SITES is the
site to rebase + thunk.
Resolved from the state after all (no arcade run needed): the ball's
writers ARE patched (0x1A52C page 0, 0x1A54C/0x1A562 page 5, patch_report
487-489) — they were never FM-GATED. LOOP 23's gate list came from a
level-1 census; the round-clear drawer (0x1A52C-0x1A59C, caller 0x1A458)
and the round clear-all (0x1ACCA-0x1ACEA, caller 0x1A924) never ran in
it, so their FB-staging writes landed inside the SH-2's FM span and were
DROPPED: page 5 empty, ball gone. Fix: both entries added to
FMGATE_ENTRIES (+spans). rom/s16_ball.32x = shipping + orb fix + this.
Law: any scene the census never visited has ungated writers — the
tilewrite probe (now stop-hook-free) is the cheap way to find the rest.

## LEVEL-2 HEAD SCENE: the level-1 palette image was loading over it (2026-09-02, s16_ball.bs1)
Mike: "the yellow light seems static". State: PAL_SH shadow differs
from the game's palette in 991 tile/text words; the shadow's words
0x98-0xAF are level 1's baked glow blocks (7fff 4900 4a00 ..) while the
game holds its own animation (a401 a402 ac7c ..); glow_on=1, grants=3,
psw=4. The 'normal' scene's 8 probes were discriminated only against
boss_smoke and transform — nothing from level 2 was ever in the corpus
— so they matched the head scene and the runtime loaded level 1's
image over it (heal re-ships, probes match again, loop: REMAP 176k),
and the glow animator played level 1's yellow-light rules over the
game's own writes = the static light.
Fix: two FOREIGN anchors from the states' game palettes —
discover/palscenes/level2_head.palsh and roundclear.palsh — added to
palscene_bake.py ANCHORS (not loadable). Rebaked: 'normal' now probes
0x40/0x61/0x81/0xA0 too; offline, every level-1 witness still matches
and neither foreign scene does. rom/s16_l2.32x = ball + this. Law: a
loadable scene's probes are only as good as the corpus of scenes they
were discriminated against — every new level needs a witness.

### s16_l2.bs1: still level 1's image in the shadow — the HEAL was incomplete
With the new probes the head scene no longer matches, yet the state has
990 shadow words wrong, the glow ramp, glow_on, psw 5, and the 68K's
pal dirty bitmap shows only blocks 0-4 dirty. Sequence: at the level
boundary the game's palette RAM is still level 1's -> 'normal' loads
(legit) -> BAD2 re-marks all 64 force-raw -> the level-start storm
drops some pushes with landed==0 (no BAD1 re-mark for those) -> their
blocks keep the IMAGE while the 68K's shadow says "synced" -> the
probes keep matching the image inside PAL_SH, the scene never
un-detects, the game's level-2 words never arrive, the glow grant
stays. Fix: HEAL-COMPLETION BELT — the SH-2 records every block id
that lands after a load (64-bit mask at 0x28F60); after 40 palette
vints any missing block re-posts 0xBAD2 (COMM8 idle only). Attract:
all 64 seen, 0 reposts. rom/s16_heal.32x = l2 + this.

## SPHERE BOTTOM (s16_fix2.bs1, 2026-09-02): the bottom-band backstop — SOLVED, rom/s16_sphere.32x
State: the round-clear picture is ALL MD planes (BG page 5 full, FG page 0
rows 9-28 = the interior; sbuf empty; FB holds the orb's leftovers), and
the SHIPPED plane-B name table has screen tile-rows 24-27 BLANK while the
page holds 59 distinct tiles there = the ball's base. Cause: the PURPLE
BACKSTOP (2026-08-25) blanks the BG's rows 24-27 unconditionally ("the
FB-composed floor always covers it") — true in level 1, false on this
screen. Fix: blank a bottom-band BG cell only when the FG tilemap word at
the same screen cell is nonzero (jts16_prio.v: the FG covers it). Data:
level-1 play (audit dumps 900/1900, attract 3000) has 0 uncovered cells in
the band -> unchanged; frame 300 (pre-level) has an empty BG there ->
unchanged; the sphere gets its base back. Battery 1068, soak 0 wedges.
Mike's pass on the round clear decides; then promote.

**PROMOTED 2026-09-02 14:40: rom/s16.32x = 07d77855+ (the sphere build).**
Mike: "good enough to continue working from; not 100% parity but very
very close." The bar stays 60 (ship-bar-60hz): next is the chasing step
of the pipelining arc — launch generation N+1's compose while the blit of
N is still reading sbuf, band-ordered (the blit runs ~3.5x the compose's
row rate), worth 0.1-0.3v of gap per the vint timeline (landing 0.24,
blit-done 0.34, launch 0.38). Then C1 done right (FB cat-1 with the MD's
own pen colours), then the maps drain off the close path.


## BLITCHASE — launch during the blit (2026-09-02, pipelining arc step 2)

`make CANON LAUNCHEARLY=1 BLITCHASE=1`. The window used to run: landing
wait, blit (both halves), pre-ack work, launch. Now: landing wait, post
the slave's half (SYNC[4]), pre-ack work, LAUNCH, then the master's
half (rows 136-223). The launch may open a generation over a READY one
because a row fence orders the slave behind the blit: SYNC[14] = first
master row not yet shipped (136 at the post, y per row, 224 when done),
and the slave (a) drains SYNC[4] at compose entry so its own half ships
before any compose write, (b) waits on the fence before band 1 (ends at
144) and band 2 (224). Fence waits are bounded at ~0.5v and counted at
0x28C84.

Battery, play2 1900 frames:

| build | ships | fps | gen wall | rej | hdlr |
|---|---|---|---|---|---|
| shipping 07d77855+ | 1068 | 35.6 | 1.06v | 1.71% | 57.4 |
| chase, fence with TWO writers | 1031 | 34.4 | 1.42v | 1.87% | 56.9 |
| chase, master-only publisher | **1092** | **36.4** | 1.33v | 1.87% | 56.8 |

LAW (collision-shaped): blit_half is SHARED .ramtext and the slave runs
it for its own half — a "publish progress" store inside it has two
writers. The first arm measured 220 half-vint fence timeouts in 1031
generations from exactly that. Publisher = master only, told apart by
the stack pointer (master SP < 0x3F800).

READ THE WALL: +0.27v with the launch ~0.25v earlier. The compose makes
almost no progress while the master's half blits — the FB-write stall
holds the bus and the slave's SDRAM traffic waits behind it. Overlap
buys the pre-ack time and the slave's idle gap, not the blit itself.
The blit is BUS TIME, not CPU time; the levers that remain are fewer
bytes over the bus per frame (C1: cat1 off the slave and off sbuf; the
maps drain off the close path), not more overlap.

Region: _end = 0x06019000 exactly. The census arm does not fit under
BLITCHASE (+168B); its single-vint share is unmeasured, ships/1900 is
the number.


## C1 DONE RIGHT — PENMATCH + CAT1MD + EDGE42 (2026-09-02 evening): +17% over BLITCHASE

Candidate rom/s16_edge.32x = CANON LAUNCHEARLY BLITCHASE PENMATCH CAT1MD
EDGE42 (build on 5a5eae7+). The three pieces the C1 failure record
demanded, each measured on play2 1900:

| build | ships | fps | wall | hdlr | notes |
|---|---|---|---|---|---|
| chase (item 1) | 1092 | 36.4 | 1.33v | 56.8 | base |
| + PENMATCH | 1085 | 36.1 | 1.34v | 57.1 | neutral |
| + CAT1MD | 1125 | 37.4 | 1.17v | 57.5 | edge fallback still on |
| probe: fallback off (C1_NOEDGE) | 1273 | 42.2 | 1.05v | 58.0 | the win to unlock |
| + EDGE42 (fallback off) | **1278** | **42.4** | 1.04v | 58.8 | candidate |

Soak play_native3 26000: 15680 ships (37.3fps), wedges 1, skips 0, rej
1.24% (C1 step 3's soak: 13665 / 32.4fps / wedges 0). Attract 5400:
belt 0, timeouts 0.

PENMATCH (part 1, the colour band): apply_cram paints tile groups as
mdp_quant re-expanded to the nearest 32X level under ares's MD DAC
{0,52,87,116,144,172,206,255}, through a 32-entry channel table, memo
kept. Two dead ends measured on the way: (a) nearest-pen SUBSTITUTION
(the record's wording) churns — tile classes merge sets that hold
different MD lines, so alternate parities painted one group two ways
every window (4149 vs 876 CRAM writes per 600 frames); (b) the no-memo
shift/clamp version cost +300 master ticks per window and -3% ships:
the window end feeds the next landing, and light scenes sit on the
one-vint cliff. LAW: anything added to the window is paid in ships.

Cache-miss fallback (part 2): NOT built — tile_pixels reads the tile
straight from cart ROM on a miss (blank_tile is never returned), so
the record's black-tile mechanism cannot occur as written. The C1
black tiles need Mike's eye on this build; if they return, the
suspect is the MD side (a pending cell the FB pass did not cover).

EDGE42 (part 3, the scroll-in column): the walk runs cols -1..40 and
ships the two edge cells as an optional pair after the row's span (w1
bit 15; count/start fields untouched), applied by the 68K as two
direct VDP writes. No mirror for them (RAM): the pair goes out when
the row header moved, when either cell is art-pending, on the row
after a pending send (a 2x28-bit memory), and on the backstop window.
1.7 pairs per generation on play2. The FB cat-1 edge fallback is off.
Packet max grows 14 words (368-long copy: room).

RAM for it (region guard): mcont/scont boot code to ROM behind 12-byte
trampolines, three unreferenced marsdev helpers compiled out,
shadow_lut_chunk to ROM (gap work), sused_prev on 0x398E0, the fence
timeout counter dropped. _end 0x18F98. NEGATIVE: compose_layer_regs
looked like a latch by name and is the hot tile compose — never move
it.

Battery determinism: a rom with an inert probe added scored EXACTLY
the same ships as without (1060 = 1060). Differences of 20+ ships are
real; layout luck is not a thing on this rig.


## MIKE'S PASS ON s16_edge (2026-09-03): black cells + tile bleed -> rom/s16_edge3.32x

Two reports on the EDGE42 candidate: isolated black 8x8 cells in the
tree line that scroll with the background for ~200 frames (his frames
2726-2950, two cells at once), and a sky cell flashing another tile's
art for a frame ("bleed").

DIAGNOSIS, from a headless reproduction (play_native3 frame 5400, every
video memory dumped): the black cell is a plane-B cell holding the
BLANK slot while its tilemap word is a real tree tile whose VRAM slot
is tagged and clean. Blank shows the backdrop, and the backdrop is
black (CRAM[0]=0; the sky is real cells). The walk emits blank for a
cell only in CUT mode when the cell's slot is art-pending — so the cell
was landing on a freshly claimed slot at EVERY visit: its cache set (8
ways, 128 sets) was full of hot ways and the tile ping-ponged with
another live tile, each visit re-claiming a dirty slot. Cut mode was
pinned because its re-arm (>=24 dirty cells per chunk) is the steady
state under thrash. CAT1MD made it: cat-1 cells now claim slots, and
the working set overflowed hot sets. Counters (per generation):
cut-blanks 3.9 (Mike's edge state) vs 0.8 (shipping state); soak
0.65 (edge) vs 0.36 (shipping). Evictions run at 130-200/gen on EVERY
build — the slot cache is full and scrolling turnover is normal — but
only the edge build had cells pinned blank.

WRONG FIRST THEORY, kept for the record: "a set freed by the drift
check renders palette 0 (black)". Real mechanism, but not this bug —
the fix (last-line memory, 32B at 0x28820) stays because it is
correct and cheap. A SOFT PEN-LINE claim for cat-1 sets was also
tried: -12% ships in the soak (every pending row is an FB row).
Reverted.

FIX (rom/s16_edge3.32x, build 8cfec491+):
1. A cat-1 tile never evicts a HOT way (victim younger than 12
   windows): the slot stays blank and CAT1_PEND hands the cell to the
   FB. Cat-0/BG tiles keep the old rule (they have no fallback).
2. Cut mode: dirtiness EXTENDS a cut, never starts one; capped at 36
   extensions.
3. RAM for it: the unreferenced marsdev helpers (fast_memcpy,
   fast_wmemcpy, get_stack_pointer, CacheClearLine, cache_flush,
   CacheControl) compiled out; text_capture and slave_window_k run
   from ROM (small per-window loops; the I-cache holds them).

| build | play2 ships | soak blits | soak black-cell frames (3225 sampled) | cut-blanks/gen |
|---|---|---|---|---|
| shipping 07d77855+ | 1068 | 13127 | 0 | 0.36 |
| edge (Mike's) | 1278 | 15680 | 92 | 0.65 |
| edge3 | 1276 | 16771 | 0 | 0.24 |

Attract 5400: belt 0, timeouts 0.

TILE BLEED: a hot eviction (a visible cell's slot re-used) shows the
new art in the old cell until that cell's next visit — one generation.
That class is PRE-EXISTING (hot evicts ~1/gen on the shipping build
too); C1 put cat-1 cells on the plane so it now hits pillars and
gravestones that the FB used to draw. Rule 1 removes the cat-1 share
of it. The rest needs a slot allocator that knows which slots are on
screen (md_ref is per way, not per cell) — queued, not done.

The mid-event method, banked: reproduce in headless with the sweep
(screenshot every 8 frames, numpy scan for isolated black 8x8 blocks
with a clean ring), then dump "VDP VRAM", "VDP CRAM", "VDP VSRAM",
"32X DRAM", "32X CRAM" and sdram 0x19000-0x40000 at the frame, and
decode the cell on every layer. A savestate taken later is NOT the
event (bs1/bs2 both showed healthy cells).


## MIKE'S PASS ON s16_edge3 (2026-09-03): four reports -> rom/s16_edge8.32x

1. "Black MD dots" on the story panel (a row of single dark pixels every
   16 px on one line; the arcade frame that matches the panel has none).
   MECHANISM: the Mega Drive DAC dot — a CRAM write during active
   display paints a pixel of the written colour at the beam; ares
   models it (md/vdp/main.cpp, cram.bus.active). V-counter stamps at
   the 68K's CRAM writers proved it: on the story panel (a full screen
   of unique tiles) consume A ran from line 227 to ACTIVE line 17, so
   its 48-word palette DMA and the sprite palette landed on-screen.
   Every layer decoded dark blue at the black pixel — the dot is not in
   any memory, it is the beam.
   FIX: every CRAM write checks V. In vblank (V >= 0xE0): as before. In
   the picture: the palette block is copied to WRAM 0xFFA100 and DMA'd
   first thing at the next vint top; the sprite palette skips the frame
   (it re-ships every frame). Deferrals counted at 0xFFA162 (11 in the
   400-frame story run). NEGATIVE: hoisting the palette DMA ahead of the
   name-table DMAs (CRAM first) also killed the dots but cost 9% of
   ships in the soak — everything on the 68K path before the DREQ push
   moves the SH-2 launch onto the one-vint cliff. Reverted; the gate
   costs nothing in the common case.
2. "Sky tiles wrong palette" — a checkerboard band under the HUD. It is
   the sky's own dither: two colours 2/31 apart at 15 bits ((14,18,25)
   vs (12,17,24)), invisible on the arcade, that 9-bit quantisation
   lands one MD level apart ((4,5,6) vs (3,4,6)). FIX: NEAR MERGE — two
   pixels of a set whose raw channels are all within 2/31 share one
   pen (mdp_near, in assign and extend; the pen refcount is bumped at
   the merge or free_set under-counts it and releases a pen another set
   still holds). The band is flat now, like the arcade's, and the set
   spends fewer pens.
3. "Persistent grass shimmer": the arcade does it too — consecutive
   arcade frames differ on 7-8k grass pixels (palette animation, ref
   601/602, 1001/1002); ours differs on ~4k. Not a defect.
4. "Tile pop-in" at the scroll-in column: a cell whose slot art has not
   landed shows the slot's OLD art for a frame (the bleed class). A
   two-column pre-claim (cols -2..41) removed it but cost 9.5% in the
   soak (the walk grows and every window is on the cliff). Reverted;
   queued behind the on-screen-aware slot allocator.

| build | play2 ships | soak blits |
|---|---|---|
| edge3 (Mike's) | 1276 | 16771 (sweep run) |
| edge5 merge only | 1202 | 15721 |
| e6 reorder only | 1193 | 15190 |
| edge7 merge+reorder | 1244 | 15500 |
| **edge8 merge + V-gated CRAM** | 1214 | 15388 |

LAW (cliff chaos): the battery is deterministic, but a timing change of
a line or two flips generations across the one-vint cliff in either
direction; builds that differ only in timing spread +-6% on play2 and
+-5% on the soak (edge, edge2, edge4..8 all sit at 15.2-15.8k; edge3's
16771 is the outlier). Read differences under ~6% as noise unless the
mechanism is known; A/B the mechanism, not the number.


## MIKE'S PASS ON s16_edge8 (2026-09-04): the near merge is withdrawn -> rom/s16_edge9.32x

Seven reports. Four are the NEAR MERGE and it is off by default now
(`NEARMERGE=1` to test): the level load-in "wrong palette that never
finishes" (blue blocks where the cloud dither was), the transformation
background with the blue chevrons gone flat (the arcade shows two
blues), and the sky. Merging at assignment time is wrong for any scene
entered through a fade: every colour of a set is near black at that
moment, the pixels merge, and they stay merged when the fade lands.
A fade-safe version needs an unmerge when a merged pixel's own colour
drifts from the shared pen; until then the sky band keeps its checker
(one MD level apart; the arcade's two blues are 2/31 apart).

The other three:
- Zeus scale-in as a black silhouette: PRE-EXISTING — the edge3 sweep
  shows 30-44% black in the Zeus region over frames 648-680 too. Not
  in Mike's edge3 list; it is a sprite-pair paint at scale-in and gets
  its own investigation (FB CRAM pairs, not MD pens).
- Left zombie dropout / red block at the right edge: MD-sprite (mob
  class) art arriving late — the zombie shows partly, the newcomer
  shows filler red. Pre-existing class (mdspr_upload_pump pacing);
  unrelated to the CRAM gate, which touches only the palette.
- Grass "shimmer that never heals": the arcade animates the grass
  highlights every frame (palette WAVE); ours does too. Whether OURS
  marches the 16 phases in the arcade's order or flickers between two
  needs a side-by-side of capture.mov against the arcade capture —
  two stills cannot tell. Open.
- Frame drops in stride and black load-in cells at the bottom: cadence
  and the cut-mode cover, both known.

edge9 = edge8 with the merge off = edge3 + the vblank-gated CRAM
writes (the story-panel dots) + refcount and last-line fixes.
play2 1220 ships.


## 2026-09-04 late: edge9 capture pre-scan, and the drain cutoff probe

Pre-scan of Mike's edge9 capture (4509 frames) by the black-cell,
solid-red and large-dark detectors: black cells only at load-ins (53-91,
4233-4251 = scene cuts, the cut cover); Zeus dark over 169-195 (scale-
in) then coloured by 224; a solid red 16x16+ region at x~295 over 94
frames from 1049 = the zombie death splatter.

- SPLATTER: the ROM frame (bank 4, offset 0xB646 flipped, pitch 19, 37
  rows) decodes offline to an irregular 7-pen splash; ours draws it with
  an irregular left edge and solid red to the right. The arcade capture
  shows solid red 16x16 regions in the same fight too (55 sampled
  frames), so part of it is the art. PRE-EXISTING: shipping shows it 8
  sampled frames in the same window, edge3 47 — the family shows it
  longer. Needs Mike's frame-aligned arcade capture to say how much is
  ours. The Python port of the live decoder did NOT reproduce the frame
  (scattered noise) — my parameter mapping, not evidence.
- ZEUS: no reference in ref_arcade (attract footage, no Zeus). The
  silhouette is pair 15 (the fixed shadow pair) all 0x0842: Zeus is
  drawn as an opaque dark shape during the scale-in. Whether the arcade
  fades him from dark or draws a translucent shadow decides the fix.
  Waits for the arcade capture.
- DRAIN CUTOFF (NAT_DRAIN_CUT, default 11300 ticks): the maps drain is
  on the close path only through this cutoff (the drain overlaps the
  slave's compose in the gap polls). 12300: play2 1271, soak 15796;
  13000: play2 1261. Both inside cliff noise against edge9 (1220 /
  15388). Not the lever it looked like; the census must say where the
  drain ends before touching it again.


## ZEUS BLACK SCALE-IN — DIAGNOSED (2026-09-04), pre-existing, not fixed

Arcade (mamecap frames 2408-2500): Zeus is coloured with the magenta
ball from the first frame of the scale-in; no dark phase. Ours: an
opaque dark shape (FB indices 241-254 = the fixed shadow pair, all
0x0842) for ~30 frames, then coloured when zoom reaches 0. Shipping
shows it too (sweep frames 664-680).

MECHANISM: the master's snapshot of Zeus's record (sprite 1, set 3) is
`FFFF 0131 DFFF F0C9 F283 00E7` — words 0 and 2 are the landing
buffer's 0xFFFF sentinels (word 2 = 0xFFFF & ~0x2000), words 1,3,4,5
landed. The game's list in WRAM is intact (`6718 0131 0016 F0C9 F283
00C6`). Word 0 = 0xFFFF makes top >= bottom and word 2 carries the EOT
bit, so the set census stops at Zeus (sused = {0}), no pair is mapped
for set 3 (spr_pair[3] = 0xFF while pr_key[13] = 3 still holds the
pair), and the draw falls to the shadow pair. The magic-tail check
passes: the tail landed; two INTERIOR words of one record did not.
This is the ares DREQ FIFO word-loss class (dreq misaligned 1.8-2.5%
of cycles on every build), hitting the same two words of record 1 for
the whole scale-in because the push layout is constant then (2
records, regs, no rowscroll).

FIX DIRECTION (queued): a landed record whose word 0 or word 2 is the
sentinel is not a record — heal it from a second copy. The 68K's FB
copy of the list (0x85E000, the loop under FB_SPR_READ in md_main.c) is
that copy and costs ~1 line of 68K time; the master then patches the
sentinel words from it before the snapshot. Padding the push does not
help if the loss position is absolute in the FIFO stream.


## mamecap comparisons (2026-09-04): splatter, flash, grass

mamecap/ is a MAME level-1 recording (14034 frames, full screen). Used
frame-aligned against Mike's edge9 capture:
- SPLATTER / HIT FLASH: the arcade draws hit actors as SOLID one-colour
  silhouettes (player red then yellow, mamecap 3150/3158) and the
  zombie splat as a solid red blob. Ours draws the same class. NOT a
  bug — except that OUR flash sequence has a BLACK silhouette frame
  (Mike's 658; the arcade's is red/yellow): a one-frame set whose pair
  is not painted, the Zeus mechanism's little brother. Same fix family.
- GRASS: the arcade steps the grass by a fixed amount every ~6 frames
  (mean change 14, regular). In headless over frames 600-700 our MD
  CRAM never changes while the game animates sets 6, 19, 20, 21 every
  3-6 frames — our grass is STATIC there. Mike's "shimmer" frames
  657-662 are the player's hit flash (yellow/black/white) inside the
  grass band, not the grass. Two items: the black flash frame (pairs),
  and the grass not animating (the glow/value-paint path not reaching
  CRAM in that window — glow_on gating or the set's pens not on a
  line). Both open.


## ZEUS SOLVED (2026-09-05) -> rom/s16_zeus2.32x

The DREQ-loss theory was WRONG, and the record says so: the 68K's own
copies showed the same FFFF words. Stamping Zeus's record at part A
entry, after the game's IRQ (part B) and post-ack over three frames:
the game itself ALTERNATES the record between hidden (words 0/2 =
FFFF) and valid, frame by frame, in its main loop. mamecap 2440-2447:
the arcade draws Zeus ONE FRAME IN THREE during the scale-in — a
deliberate ghost flicker. Our snapshot is per generation (1-2 vints),
so it locks onto either phase for stretches, and the pair map is built
one generation behind: a visible generation whose set had no mapped
pair drew him through the dark shadow pair.

Two fixes, both in the map:
1. HELD-PAIR WINDOW: sused_prev is a 4-generation countdown instead of
   a 1-snapshot flag, so a flicker sprite's pair stays mapped across
   its off frames (bounded: an absent set stops repainting a shared
   pair after four — the Neff-black limit from the orb fix).
2. QUICK CLAIM at launch (ROM, <= 24 records): a set with no pair in
   THIS snapshot gets a held or free pair mapped before the slave is
   commanded, so its indices are right at compose; apply_cram paints
   it later in the same window (CRAM is read at scanout). Covers the
   first appearance of any set — Zeus's first frames and the black
   hit-flash frame (Mike's 658).

Headless 630-700 (2-frame samples; B black, Z coloured, . absent):
  edge9    ..B......B......  (black stretches)
  countdown ..B......B.Z.Z.Z..Z.....ZZZZZZZZ.ZZ.
  zeus2    ...Z....ZZZ.Z.Z.Z..Z.....ZZZZZZZZ.ZZ   (no black; 7 claims)
play2 1227, soak 15274, attract belt 0.

The look: Zeus now flickers coloured at our cadence, as the arcade
flickers at 60 Hz. A spatial checker (draw alternate pixels for a set
the game alternates) would read as the arcade's ghost on a CRT; queued
as cosmetics.

Also withdrawn this session: SPRHEAL (68K list shadow / master heal)
— built three ways, all measured; the shadow after the game's IRQ
read FFFF too, which is what exposed the alternation. Reverted, not
in the tree.


## GRASS "SHIMMER" — measured, not an animation (2026-09-05)

The arcade's grass does NOT animate: the 14-mean-abs step every ~6
frames in mamecap 2900-2960 is the screen scrolling one pixel (best
row shift -1, every row of the band). At our level start (vx0 = 0)
nothing moves and nothing should. The four sets the game animates at
the level start (6, 19, 20, 21) hold no MD line and are not the grass
(BG set 101, FG sets 85/86, all static in the game palette). The
change I first measured on ours (23 every ~4 frames) was the player's
hit flash inside the band. Per-row profiles of Mike's frames show no
sprite-aligned seam. What differs is TONE: ours mean RGB (83,153,41)
vs arcade (77,132,33), contrast 49/44/58 vs 37/37/49 — 9-bit rounding
((v<<1|hi)+2)>>2 plus the MD DAC's mid-level lift. If the shimmer
persists on zeus2, the next step is a frame range and a screen
location from Mike; the only lever named so far is a round-half-down
quantiser, which would dim everything.


## ART TAIL (ARTTAIL=1, opt-in) — built, measured, parked (2026-09-05)

Mechanism for the scroll-in pop-in: tile art shipped only in tile
chunks (phase 0 of the 9-window rotation, or 40 pending), so a new
column's slots could show their old art for up to 9 windows. Now a
cell chunk can carry up to 24 dirty-slot records after the dense
hscroll words (body ends at 688 where the palette block sits; header
bit 0x4000 = tail present; the 68K DMAs them after the hscroll DMAs).
The record emitter is one ROM function shared with the tile chunk
(md_emit_art), which freed RAM. First attempt's diag counter sat on
mdspr_up_left (0xFFA0DC) and corrupted the MD-sprite upload state —
-14% ships; moved to 0xFFA0F0.

Measured (tail2): play2 1181 (zeus2 1227), soak 15169 (15274) — noise
band; 926 of 3751 art records rode tails in the battery. The pop-in
itself could not be reproduced on the rig: the soak and a hold-right
input scroll the foreground at 0.25 px/frame, a column per 32 frames,
and no visible cell ever waited for art (mirror x dirty-bit census =
0 on both roms). Mike's pop-in frames (edge3 capture 6375-6379) came
at a full-column jump per frame. Parked as a flag until a fast-scroll
reproduction exists; zeus2 stays the candidate.


## GRASS IN TWO LAYERS = MD scroll a frame ahead of the FB (2026-09-05)

Mike's frames 492->493 and 2932->2936: wall, ledge and lower grass moved
-1 px, the upper grass 0; the arcade moves every band together. The
upper grass rows are priority foreground under the player, drawn by
the FB from the generation's latched scroll; the MD planes take the
packet built in the maps drain from the same latch but landing a vint
later, while the FB lands when the generation closes. On a 2-vint
generation the planes move a frame before the FB rows. Metric: per-band
horizontal shift between consecutive headless frames of a walk (frames
1130-1300 of a hold-right input): zeus2 skewed 30 of 44 motion pairs.

HSSHIP=1: the scroll words are computed at CLOSE into HS_CLOSED
(0x3F900, 58 words; the latch still belongs to the closing generation
since no launch can precede a close), promoted to HS_DISP (0x3F980) at
SHIP, and patched into the packet at copy time: the closed frame's
scroll when a closed generation waits in this window, the displayed
frame's otherwise. Skewed pairs 21 of 39 (hs8) / 18 of 38 (hs5,
promote-at-close). No ship cost (play2 1211-1218 vs 1227).

Dead ends measured: promote at ship with per-parity pending slots
(parity wrong under BLIT_CHASE; then the two buffers OVERLAPPED at
0x3F900/0x3F980 with a 232-byte pending array -> planes oscillating
+-3 px), and promote-at-ship without the copy-time rule (28/42). The
residual ~50% is the FB flip's own variance (VISRFLIP 65% in-ISR, 35%
body fallback) and the metric's coarseness; the next step is to align
the flip, not the packet.


## FLOATING HEAD: the level-1 glow was playing in the transformation scene (2026-09-05)

Mike's state s16_cand.bs1 at the head: glow_on = 1, PAL_SH wave words
0xA1-0xAD = 30DF..305F (our level-1 wave, orange/yellow) while the
game's own words are 100F (red); ring 0x99-0x9F rotating in both but
out of phase. mamecap 8430: flames RED, bottom band red; ours: yellow
flames and a yellow band, the ring's motion overridden. The bake is
level-1 graveyard animation; in any other scene it must not run.
FIX: the reseed only fires when pscene_cur == 0 (the normal scene),
and an animator found running elsewhere is turned off with the mask
grant withdrawn (0xBAD3), so the 68K ships the game's words. Level-1
ring verified rotating -2/tick at frames 1400-1403 with glow_on = 1.

HSSHIP is OFF in the candidate: Mike saw no visual improvement in the
two-layer grass (my metric halved the skew, the residual is the FB
flip's timing); flag kept for the flip-alignment work.

rom/s16_cand2.32x = zeus2 + quick-claim LRU fallback + glow scene gate.
play2 1254; Zeus scale-in no black. Open from Mike's pass: sky "wrong
palette" (no frame given), one orange bleed cell in the transformation
band (allocator class), sprite-box pop-in (3239-3245, unclassified).


## HSSHIP on Mike's captures (2026-09-05 evening)

Mike: the grass was fixed BEFORE the tombstones rise and the dual
movement returned after — i.e. once zombies push generations to two
vints. Band-skew metric on his own frames, late stretch 1500-2500:
zeus2 run 128 of 190 motion pairs skewed (67%), cand run (HSSHIP) 74
of 165 (45%). The copy-time rule now uses nat_ship_now (set at the
last-call close check, cleared after the master's blit half), which
says exactly whether this window ships; nat_gen_ready alone also
covered a generation that closed after the ship decision. Residual
skew needs the flip census (which vint each generation's FB flip and
MD scroll land on); the packet side is now deterministic.
rom/s16_cand3.32x = cand2 + HSSHIP(ship_now).


## SCROLL SYNC, THE CENSUS ROUND (2026-09-05 evening) -> rom/s16_cand6.32x

A ring census (HS_CENSUS: per generation, the vint of the scroll-
carrying packet copy and the vint of the FB flip) turned the two-layer
grass from a guess into three named holes, each fixed in turn:
1. The copy-time rule keyed on nat_shipped, which stays up across a
   body-fallback flip (36% of flips): the next window's copy took the
   NEXT closed generation's scroll a frame early. -> an explicit
   shipped-this-window flag consumed by the copy.
2. Windows where the ship followed the copy: the packet left with the
   displayed scroll. -> the ship re-patches the FB packet in place
   (the SH-2 owns the FB inside the window; the 68K reads it next vint).
3. Windows that copied NOTHING (K2_FREE publishes only a fresh packet;
   a 2-vint generation's second window has none) and windows whose
   packet was a TILE chunk, which carried no scroll words at all. ->
   scroll rides EVERY packet as two header words (the 68K runs full-
   screen hscroll, reg 0x0B = 00, so only the strip-0 entries of the
   56-word per-strip block ever took effect — those DMAs wrote a table
   the VDP ignored), and a shipping window that copied nothing posts a
   scroll-only stub packet (tile chunk, 0 records) into the consumed FB
   slot.
Census (flip vint minus packet vint, +1 = coherent): 40/61 -> 49/60 ->
52/63 -> 59/63, three of four sampled stretches perfect. A fixed 56-
word block at 632 was tried first: it forced the tile batch to 36 and
cut-blanks per generation rose 0.24 -> 0.55 (load-in cover longer);
withdrawn for the two header words. cand6: play2 1230, soak 15379,
attract clean, 1 load-in black-cell frame in the sweep; cut-blanks
0.67/gen (up from 0.24 on zeus2 — not batch-related, unexplained;
the sweep shows no visible cost). Retired: the per-strip hscroll DMAs
(56 words/vint of 68K time) and the packet sequence word.
