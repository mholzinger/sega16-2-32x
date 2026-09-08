# HANDOFF — R60 SESSION 4 (2026-08-30)

Shipping rom: `rom/s16.32x` BUILD dca1d1fa. Canonical line:

    make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1
         CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40 BLITSKIP=1 DIRTYROW=1
         SPRBAKE=1 BLITSHIFT=24 SPRLATE=1 PRHOLD=6 TILECLASS=1
         TXTCLASS=1 MDSPR=1 ROWDEFER=1

Mike's play pass is the acceptance gate. It out-graded the entire
instrument stack repeatedly this week; his frame-number tags and
red-box annotations localised in minutes what aggregate counters
missed for a month. Treat his tags as the highest-grade data the
project produces.

---

## 0. MIKE'S STANDING CHALLENGE — READ THIS FIRST

Verbatim: "I ABSOLUTELY refuse to think we can't match the arcade
frame for frame. We have faster beefier hardware with more color and
ram. The sega16 was just a straight sprite machine. We already have
heavier firepower — so you keep measuring the wrong set of metrics
for WHY and HOW we move sprites on screen."

He is right, and the numbers prove it two ways:

**The compute exists.** Two 23MHz SH-2s against a sprite chip that
fetches ~800 sprite pixels a line. The census says gameplay runs
4.7 records/frame mean, 18 max — tens of kilopixels a frame against
tens of MIPS. Raw drawing power was never the wall.

**What the arcade actually had that we don't is PARALLEL BUSES.**
The System 16 puts sprite ROM, tile ROM, palette and work RAM on
separate silicon with separate buses — the sprite chip reads its ROM
while the 68K runs and the tile chip reads its own. Our port funnels
EVERYTHING — sprite art reads from cart, compose writes, the blit,
the DREQ drain, the 68K's packet push, MD transport — through one
shared adapter bus, and then spends measured, stamped time
arbitrating it (the FM window, the landing wait, the ~0.65-line-per-
word push). Every "law" in the ledger — the scheduling ceiling, the
load-bearing idle, the FIFO loss floor — is a shared-bus symptom,
not a compute limit.

**So the right metrics are not transport counters.** They are:

1. **Game-68K share.** M1 measured the arcade binary fitting the MD
   68K at p99 72.9% of a frame — the game needs ~73% of its CPU to
   hold 60fps. We currently give it ~61% (the push tax). 61 < 73 is
   not a vibe, it is arithmetic: THE GAME LOGIC ITSELF misses frame
   budget at p99. Mike's "the game is slow" is this number. Target:
   >= 80%.
2. **Content generations per second.** The display flips at 60; the
   composed CONTENT advances ~30-40Hz bursty (compose-skip ~50%,
   35-53% frozen consecutive pairs in his corpora). Target: 60.
3. **Sprite pixels on parallel buses.** Every sprite the MD VDP
   renders exits the shared-bus economy entirely — the MD has its
   own VRAM bus and its own fetch engine. Currently ~1 record/frame
   (the zombie class). Target: the majority of per-frame records.
4. **Bus transits per sprite pixel.** Live compose reads cart art
   per pixel; SPRBAKE pre-decodes to runs; MD offload drops the
   transit to zero after a one-time upload. Count them, then drive
   them down.

The strategy that satisfies the challenge (each step measured
before/after on metrics 1-4):

- **A. Palette transport off the FIFO** (the push diet, §4): 68K
  share 61% -> ~80%. The game logic fits its own budget again —
  the visible "slow" should die here.
- **B. P3 scale-up — the MD VDP is the sprite machine we own.**
  One palette line and one mob class today; the arcade's own
  concurrency census says 4-7 distinct sprite palettes per instant
  against the MD's 4 lines — per-scene allocation is plausible, and
  streaming residency (2-4KB per animation frame through the
  existing 12-40 tile/vint transport) covers the player class the
  VRAM cannot hold statically. Majority of sprite pixels onto the
  MD's parallel bus; SH-2 compose keeps only zoomed/oversized
  actors; the chain fits a frame; metric 2 hits 60.
- **C. Bake coverage** (SPRBAKE already replay-gated): every baked
  record trades per-pixel cart reads for sequential SDRAM runs.
- The DIRECTFB lesson bounds all of this: the blit is the COHERENCE
  layer (69% band drops, invisible); do not delete it again without
  a recovery design. Speed comes from putting pixels on other buses,
  not from removing the layer that hides scheduling misses.

---

## 1. STATE — WHAT MIKE'S EYES HAVE CONFIRMED CLOSED

- Purple band (0 across every corpus since the pair-15 era)
- Inverted/wrong-palette sprites; black-silhouette RATE (0.2%
  steady-state; transient pair-starvation silhouettes still occur —
  open, his frame 3412)
- The month-old band stripes at rows 68-71/140-143 ("banding LOOKS
  to be solved") — they were the master's 4-row compose slices;
  BANDSHIFT=36 + RG2SHIFT=40 deleted them geometrically
- Bottom-ground void/pop-in (ROWDEFER re-armed; 40 -> 0 on rig)
- The zombie-under-gravestone dropout ("WOW that gravestone dropout
  is gone!") — two fixes: lockstep claims (SAT rebuilt at chain
  launch so MD and FB layers age together) + the R2 mid-band hazard
  gate (SYNC[10] bit 2 around the sprites-done/cat1-pending gap;
  the ATTEMPT-4 lesson: gate ONLY the short R2 gap — gating the
  chronic R0/R1 owed windows measured WORSE, 5 blinks/10 swings)
- Green HUD digits (TXTCLASS: HUD text set pinned to CRAM group 0 —
  the 32nd group, reserved-never-armed; entries 1-7 were virgin)
- Cold-boot black shadows (shadow_lut boots to identity)

## 2. OPEN, WITH MECHANISMS NAMED

- **Slowness** = the DREQ push (§4). Conviction is complete.
- **Tearing/judder** = content rate (metric 2) + the one-frame-late
  band family at rows 72/144. Same root as slowness (the FM span).
- **Sprite flicker** = three parts: FIFO word-loss bursts (~5-9% of
  packets, ares silent-drop; magic-tail machinery detects+skips —
  stale frames in bursts), transient pair-starvation silhouettes
  (frame 3412 class), and whatever Mike's next capture shows on the
  current build.
- HUD dropout (his frame 3991): NOT identified at full res — ask
  him what element he saw missing before chasing.
- Grass-blink residual on R0/R1 (the chronic owed windows) — needs
  the SCHEDULING fix (owed drain guaranteed before ship), not
  ship-side deferral (attempt 4's measured failure).

## 3. THE INSTRUMENT RULES (paid for in blood this week)

- **DIAG slots: NOTHING in 0-63 is free.** Collisions #9-#12 all
  hit this session-era. MDSPR counters live in scratch (0x28E20).
  state_health had TWO ghost blocks reading reused slots ("STROBE
  CONFIRMED" and "FS restore latch" — both retired 2026-08-29/30;
  both had triggered false investigations). Before trusting ANY
  state_health line, check the slot map against the source.
- The 0x28D80-0x28FFF "free" map comment at m_main.c:636 is FALSE
  (five tenants). md_dirty at 0x28EC0 is live — collision #12.
- **Step-sampled sweeps alias 1-frame events** (three separate
  misses). Consecutive-frame windows are the standard instrument
  for blinks/strobes; Mike's ~42-57fps capture also aliases and
  tears at the top rows (his HUD-digit "strobing" was proven
  capture-side: 0 changed px over 16 frame-exact frames).
- **The full-level rig**: discover/inputs/play_level1.csv fights to
  the graveyard/wolf/green-zombie scenes (play2.csv never left the
  first 30s — that gap silently blinded every deep-scene A/B for
  days). Standard battery: scratchpad score_build.py (1900f);
  standard sweeps: step-4 for rates, step-1 windows for blinks.
- **Laws need dates + preconditions.** "Do not exceed BANDSHIFT 32"
  calcified past its precondition (slave saturation) and hid the
  band-stripe fix for a month. When re-reading LOOP/REBUILD
  negatives, check whether the architecture they were measured
  under still exists.

## 4. THE NEXT ARC — PALETTE TRANSPORT (designed, unstarted)

Facts: handler mean ~100 lines V-stamped = the push; 151 words
mean; palette 107 of them (K mean 3.34 32-word blocks); per-word
cost ~0.65 lines (two 68K adapter accesses); the game needs >=73%
of the 68K (M1) and gets 61%.

Poisoned shortcuts (do not retry): K-cap below 8 (K=4 era = black
silhouettes + 60-frame miscolored load-in, Mike-graded), per-group
FIFO polling (trades directly into FIFO loss = his flicker).

The design to measure: game palette writes are ALREADY thunked
(FMGATE writers). Step 1 — COLLISION CENSUS: count palette writes
landing inside vs outside the FM span per frame class (fade vs
normal). If non-fade frames are collision-free, route palette
through FM-gated FB staging writes (thunk-side) + SH-2 in-window
apply, and drop pal blocks from the packet: push ~151 -> ~50 words,
handler ~100 -> ~45 lines, 68K share -> ~80%. If collisions are
chronic, fall back to: pal blocks ride the FIFO only on fade frames
(measured K distribution needed — histogram, not mean).

## 5. WHERE EVERYTHING IS WRITTEN

- REBUILD.md — the working log; every conviction/negative this
  session-era is appended with numbers. Newest entries: the blit-
  is-coherence postmortem, seam-kill, void fix, attempt-4 negative,
  R2 hazard gate, push conviction.
- P3.md — the MD-sprite offload design + execution log + honest
  bounds (one palette line; BG allocator measured 1/1/4 free pens —
  line steal evicts live colors; per-scene allocation is the path).
- The plan artifact (Mike-facing scoreboard):
  https://claude.ai/code/artifact/f3a4a7e3-0b98-4663-aace-6bba6a41fe1b
- tools/: text_census.lua + sprite_census.lua (arcade-oracle
  censuses), state_health.py (post-ghost-cleanup), frame_grade.py,
  ares_gate.py; scratchpad score_build.py battery.

The finish line Mike defined: level 1 + attract + level change =
a complete game, frame-for-frame against the arcade. The compute is
there. Put the pixels on the buses the machine actually has.
