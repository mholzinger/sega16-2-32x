# HANDOFF — NATIVE SESSION 2 (written 2026-09-01, branch native1)

Shipping rom: `rom/s16.32x` = **BUILD e0549e86** — the NATIVE
whole-frame build Mike play-passed ("BANK THIS WORK! This is 100% an
improvement! Speed, tearing, all of it"). Same bytes preserved at
`rom/s16_native.32x`. Old band-queue architecture at
`rom/s16_canon.32x` (A/B only). **PALSTATIC v1 was pulled from the
rom after Mike's eye caught it standing a wrong palette — see §3
before touching anything palette-side.**

Canonical NATIVE line (zsh: ARRAYS ONLY — `make $CANON` with an
unquoted scalar passes ONE bogus arg and builds a flag chimera):

    CANON=(MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1
           R60=1 CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40 BLITSKIP=1
           DIRTYROW=1 SPRBAKE=1 BLITSHIFT=24 SPRLATE=1 PRHOLD=6
           TILECLASS=1 TXTCLASS=1 MDSPR=1 ROWDEFER=1 PALDELTA=1
           NATIVE=1)
    make "${CANON[@]}"            # + PALSTATIC=1 for the pulled arm

## 0. WHERE THE PROJECT STANDS

NATIVE (NATIVE.md) is the shipping architecture: one generation =
one whole frame, shipped whole-screen-or-nothing, flipped only when
fresh. Band queue / chain / ROW_DEFER / cat1 deferral are compiled
out. Frozen bands are structurally impossible and Mike confirmed it
on his TV. He then played through LEVELS 2 AND 3 on level-1-trained
tables — the architecture held (one wedge in 30k vints, handler
stable). Five levels total is the endgame; the recipe per level is
corpus regeneration, not engineering.

Mike's standing asks, in his priority order:
  1. SPEED — both dials (see §2). Slowdown in gravestone+enemy
     pile-ups, and the "speed way up" afterward = the two clocks
     (game logic / display) snapping back out of phase.
  2. The scene-cut smear (his "blue-white gravestones" + "black
     silhouettes" = ONE class, convicted on capture frames
     5046/5217 vs oracle 10646->10647 one-frame cut).
  3. Cosmetics carried from canonical: cloud strip (parked
     cell-mode hscroll), red transform orb (rebase-layer sprite
     corruption), grass blink residual.

## 1. THE ORACLE (ORACLE.md, tools/oracle_grade.py)

`ref_arcade/` (gitignored, local): 17,528 frame-true 320x224 PNGs of
a full arcade level-1 run. Scene map + measured truths inside.
THE REFRAME: the arcade WALKS at ~30Hz-stepped (0.5px/frame scroll,
22% frozen pairs) — NATIVE's 32.6fps whole-frame already matches it.
The smoke scene is slow on the arcade too. **The only true-60Hz
arcade scene is the BOSS FIGHT (0-6% frozen pairs); ours runs it
~19-20fps. That is the entire display-rate gap.**
`tools/ref_dump.lua` re-captures; `oracle_grade.py cadence|scroll|
match` grades either side (anchor-match first — build timelines
shift, the identity law).

## 2. PERFORMANCE ROADMAP (the parity-with-MAME plan)

Two dials, measured:
  A. GAME-LOGIC RATE: game gets 73% of a 7.67MHz 68K vs the
     arcade's 100% of 10MHz. Worst vint: handler ate 254/262 lines.
     Handler mean 70.4; the fat term is the palette rotor+cmp at
     30-33 lines — and it is LIVE WORK: the glow palette genuinely
     changes ~10 words EVERY vint (the "91% redundant" census was
     per-write, not per-block). Streak backoff measured a MISS
     (noise floor; reverted, in LOOP26). THE REAL DIET: the glow is
     a deterministic palette CYCLE -> bake it, run it SH-2-side on
     its own clock, mark its blocks OUT of the 68K rotor entirely.
     Expect rotor ~30->~10, handler ~50, game share ~80%.
  B. DISPLAY RATE (the boss fight): generation wall 1.22v mean,
     ~2.3v in the boss scene vs the ~0.65v a same-vint close needs.
     Master row rebalance (BANDSHIFT sweep) measured DEAD 4 points
     (two CPUs on one art bus lose to one warm CPU; machinery kept,
     no-op at BANDSHIFT=36). The lever is P3 sprite-offload growth:
     claims are 16% of records, limited by ONE MD CRAM line, which
     the static-palette arc frees.
Both dials converge on the SAME arc: PALSTATIC v2 (glow cycle bake +
static sprite pairs + freed CRAM lines + more MDSPR classes).

INSTRUMENT LAW (banked): cross-build handler-mean deltas under ~1.5
lines are code-layout luck. A/B within one build or demand >2-line
effects.

## 3. PALSTATIC — PULLED; v1.1 REQUIREMENTS (do this arc next)

Design PALSTATIC.md; v1 corpus + bake are GOOD and banked
(discover/palscenes/*.palsh, tools/palscene_bake.py ->
sh_src/pal_scenes.h, cart .palscenes section in mars.ld). Three
level-1 scenes: normal / boss_smoke / transform (transform palette
harvested from the ATTRACT ~frame 1800; boss from Mike's states).
Scene distance matrix: fades jitter <=34 tile words, scenes >=62
apart; the SPRITE half of PAL_SH is per-moment actor state — scene
identity lives in words 0-1023 only.

v1 FAILED ON MIKE'S SCREEN, two flaws (LOOP26 conviction, his
savestate read live PAL_SH = the baked BOSS image during gameplay):
  1. 4-word probes match palettes a FADE passes through (boss =
     a desaturation = maximally fade-aliasable).
  2. An SH-2-side PAL_SH load is INVISIBLE to the 68K's PALDELTA
     shadow -> zero heal deltas -> a wrong load STANDS FOREVER.
LAW: never install SH-2-side palette state the 68K's differ cannot
see — every divergence needs a re-ship path.

v1.1 build order (belt FIRST):
  a. HEAL CHANNEL: new COMM8 code (BAD1-family, e.g. 0xBAD2) = 68K
     re-marks ALL pal blocks + sets pal_force = full force-raw
     re-ship over ~8 vints. The SH-2 posts it with EVERY scene
     load. With this alone, any wrong load self-heals.
  b. CONFIRMED DETECT: 8-word probes (bake change) + the same scene
     matched on 3 CONSECUTIVE landings before loading.
  c. RIG GATE: full attract run must show switches ONLY at the two
     cutscene boundaries (0x28F5C is the switch census; the attract
     traverses fades and both cut directions). Then Mike's
     transform pass.
The PALSTATIC=1 flag still builds; runtime code is in m_main.c
(detect+load in the R60 harvest, LUT rush in the poll branches).

## 4. RIG CHEATSHEET

  ARES=/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless
  battery:   scratchpad nat_score.py <rom> (1900f play2, prints
             ships/wall/bad1/rej/handler; gen-wall census 0x28F50)
  soak bots: discover/inputs/play_native2/3.csv — survive level 1
             indefinitely on NATIVE timing via banked continues;
             they never transform (orbs lost on death) — the boss
             scene comes only from Mike's play.
  savestates: Mike's bs1..bs5 read via state_health.py; PAL_SH at
             state offset 0x23B+0x27000 byte-swapped; his last set:
             bs1-3 boss scenes, bs4 level 2, bs5 level 3.
  captures:  screenshots/ = his latest level-1 run (12,130f, the
             PALSTATIC conviction frames 4429-4433 + transform
             chimera at 5046/5217); prior sets in screenshots_*/.
  counters (NATIVE): D28 ships, D29 flip-holds, D30 gen-overruns,
             D27 wedge belt+echo timeouts, 0x28F50/54/58 gen wall,
             0x28E20/24 MDSPR SAT/claims, 0x28F5C pscene switches.

## 5. OPEN ITEMS LEDGER

  - PALSTATIC v1.1 (§3) — next arc, both speed dials ride on its v2.
  - Boss-fight 60Hz (§2B) — after v2 frees CRAM lines.
  - Atomic scene reveal (art-mix chimera): shadow NT in free VRAM
    0xD000 + plane-base register flip. After v1.1.
  - Cosmetics: cloud strip, red orb, grass blink (pre-NATIVE, all
    still open).
  - state_health relabels: "deferrals by band" prints bq-drop
    counters (dead under NATIVE); D29/D30 meanings changed (flip
    holds / gen overruns); r60 "window/ack" label predates R60.
  - Level 2-5: corpus regeneration per round once level 1 is
    flawless (TILECLASS/palscenes/bake_mdspr all say "regenerate
    per round" in their headers).
  - mamecap/ + ref_arcade/ are gitignored LOCAL corpora — never
    `git add -A` sweeps them (the wip hook once did; history was
    re-banked and gc'd 8.1G->2.5G).

## 6. LAWS PAID FOR THIS ARC (do not re-learn)

  - Whole-frame generation beats every per-band gate; four band
    fixes failed Mike's eye before the deletion.
  - Two consecutive slave commands must never be bit-identical
    (the genbit; the wait-loop wedge cost 195 of 340 gens).
  - The landing wait stays OUTSIDE any gated block (bad1 doubles
    without it).
  - Two CPUs composing on one art bus lose to one warm CPU.
  - The glow palette is live animation, not redundant churn.
  - SH-2-side palette state must always be 68K-differ-visible.
  - The self-chain 2026-08-25 negative was era-bound (PALDELTA
    changed the bus economy); ledger negatives carry their era.
