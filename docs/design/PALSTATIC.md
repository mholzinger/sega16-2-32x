# PALSTATIC — per-scene static palettes + atomic scene cuts

2026-09-01. The arc the pivot ordered ("per-scene static palettes
first; do not extend the allocator/memo/queue"), now carrying three
convictions from Mike's level-1 pass + capture:

1. **Scene-cut smear** (frames 5046/5217 of the 0831 capture): the
   arcade cuts scene in ONE frame (oracle 10646->10647); ours smears
   ~10-20 frames of two-scene chimera — cutscene tiles in gameplay
   walls (Mike's "blue and white gravestones"), shadow-LUT fallback
   actors (his "black silhouettes"), text blanked. Both his tags,
   one class.
2. **REMAP volume**: ~240k remap paints over one session — the
   dynamic allocator recolors groups constantly; each is a chance to
   land mid-scan (the blink-strip family).
3. **The 60Hz limiter**: MD sprite offload claims 16% of records
   because ONE MD CRAM line exists for sprites — the dynamic BG pen
   allocator owns the rest. Static tables free the lines; claims
   scale; the boss-fight wall (2.3 vints, ~19fps vs the arcade's
   true-60 scene) comes down.

One design serves all three.

## Design

**Offline (the bake — extends palpack_tiles.py's proven shape):**
- Scene inventory from the corpora (attract, gameplay, transform
  cutscene, boss presentation/phases, death, round clear — plus
  per-round variants later). Scene = a stable palette epoch; the
  harvest corpus already visits all of them.
- Per scene: the FULL palette state — 32X CRAM 256 entries (tile
  groups + sprite pairs + text), MD CRAM lines, and the SHADOW LUT
  (computable offline from the palette: the LUT rebuild disappears
  from runtime entirely — silhouettes die by construction).
- Tables in cart ROM. Census says this fits easily: 11 fade-stable
  tile classes + 4-7 concurrent sprite sets per scene, whole-frame
  distinct colors ~92 vs 256 CRAM.

**Runtime:**
- Scene DETECT: the game's own scene state, shipped in the packet
  (one byte — the 68K reads the round/mode variables it already
  hosts; failing that, palette-content match against table keys, the
  TILECLASS precedent).
- Scene ENTER: one atomic apply inside a single flip span — full
  32X CRAM from the table, MD CRAM lines, LUT pointer swap. 1-2
  vints, not a trickle. Runtime palette writes within a scene become
  VALUE updates only (fades, ~10 words/frame — the census number);
  REMAP ceases to exist inside a scene.
- Scene-cut PICTURE atomicity (v2, after tables land): hold the flip
  + defer MD-plane publishes for the cut window so the old scene
  displays whole until the new scene's tiles are resident, then
  reveal. The MD side gets a shadow name table in the free VRAM at
  0xD000 + a plane-base register flip for a true one-frame reveal.
  A ~150ms clean hold at a cut beats 150ms of chimera — drop-beats-
  tear at scene scale. (v1 ships without this; the palette half
  alone kills the color chimera and the silhouettes.)
- Sprite sets: per-scene static pair assignment (the 8-pair zone
  becomes a table row, not an allocator arena). MD sprite palette
  lines multiply as dynamic tenants retire -> bake_mdspr classes
  beyond the zombies -> claims past 16% -> the boss-fight wall.

**Kill list (retired as tables land, in order):** the dynamic pair
allocator + stealing, cram_memo classification, the remap paint
path, shadow_lut_chunk (LUT becomes data), PAL32 dirty machinery's
remap half. VALUE/fade path survives unchanged.

**Gates:** battery (bad1/rejects/handler unchanged, REMAP counter ->
~0 in-scene), capture greps (blue-excess event scan at transform
entry/exit -> gone), the silhouette scan, then Mike's eye on the
transformation moment specifically.

**Fallback discipline:** scenes the tables don't know (untrained
rounds) ride the existing dynamic path unchanged — same graceful
degradation the level-2/3 play just demonstrated.

## v1.1 (2026-09-01) — SHIPPED TO PROBE ARM, awaiting Mike's pass

Rebuilt after the v1 pull, belt-first per the handoff order. Build
`rom/s16_palstatic11.32x` (1fe09543+); shipping rom untouched.

- **a. HEAL CHANNEL (the law's belt):** every SH-2 scene load posts
  0xBAD2 on COMM8; the 68K consume re-marks all 64 pal blocks +
  pal_force all-raw -> the storm clamp re-ships the full palette
  through the differ in ~9 vints. Any wrong load self-heals; no
  SH-2-side palette state is differ-invisible any more. 68K counter
  0xFFA0D6 = heals consumed (rig checks heal == psw). The handler is
  unconditional (every build carries the belt; only the SH-2 post is
  flagged).
- **b. CONFIRMED DETECT:** 8 probe pairs = 4 stable discriminators
  against EACH other anchor (a full match cannot be another baked
  image), block-spread (v1's 4 consecutive-index probes sat in one
  palette line that fades as a unit). Same scene must match on 3
  CONSECUTIVE palette landings before loading; any gap resets.
- **TRANSFORM DEMOTED to anchor-only (the measured surprise):**
  dense span sampling (48 frames at 2-frame stride, attract
  1736-1830) found ZERO stable transform-vs-normal discriminators —
  every distinguishing word IS the flash animation. No probe can
  hold the scene and a static load would fight it. Its span rides
  the delta pipeline; loadable = {normal, boss_smoke}.
- **UNKNOWN STATE (what the exit cut needed instead):** 32
  consecutive no-match landings (fades resolve in ~10) mark the live
  image foreign -> pscene_cur = 0xFF; the next 3-confirmed match
  loads atomically. So the transform-EXIT cut still gets the
  whole-image repaint. Boot starts unknown -> every run exercises
  one load + heal (the channel is never dormant untested).
- **Corpus:** +10 transform-span witnesses (att2_t*.palsh, harvested
  from clean NATIVE attract). Mike's bs2/bs3 cluster to NORMAL (the
  boss arena palette is the normal set — boss_smoke is only the
  intro); bs1 was a stale-build state (dcaca2f6, poison, excluded).
  boss_smoke still has ONE witness — its probes' stability is
  unproven against smoke-scene animation; if Mike's boss pass shows
  load churn (psw climbing during the fight), harvest witnesses from
  his states first.

**Rig gate PASSED (2026-09-01):** 5400f attract census psw 2 =
heal 2, exactly boot-load (<=300) + transform-exit reload
(1830-1900), zero false switches across every fade both directions;
final PAL_SH 8 words off the NATIVE baseline (glow jitter — healed);
wedges 1 = baseline; battery ships 979 / wall 1.22v / hdlr 70.3 =
baseline noise, psw 1 heal 1 (boot load).

**Remaining before v2:** Mike's transform + boss play pass on
`rom/s16_palstatic11.32x`.

## v2 GLOW BAKE (2026-09-01) — BUILT + RIG-PASSED, awaiting Mike

Build `rom/s16_palglow.32x` (d34f55d3+, flag PALGLOW=1). **Handler
mean 70.3 -> 57.3 (-13 lines), the largest single 68K diet banked.**
Everything else at baseline (ships 979, wall 1.22v, bad1 76).

The censuses that shaped it (discover/glow/, tools/glow_bake.py):
- 18 live glow words, blocks 1/4/5; scene- AND level-global (bs2/
  bs3/bs6 all mid-cycle). GAME-LOGIC-clocked: stutters + catch-up
  bursts at our 73% 68K. RAMP 0x99-0x9F rotate -2/tick; WAVE
  0xA1-A5=0xA9-AD, 6 states, dwells [4,2,2,2,2,4]; BLINK 0x36
  irregular (NOT baked - stays shipped, block 1 unmasked).
- The transform flash PLAYS ON THE SAME BLOCKS (its palette identity
  IS the glow system) - a blind mask swallowed the whole flash.

Design (every piece measured in, LOOP-style):
- 68K masks blocks 4/5 from the rotor ONLY while granted (COMM8
  0xBAD4 grant / 0xBAD3 revoke) AND a per-vint PROGRAM CHECK passes
  (two mirror sentinels inside the ambient value-sets; any foreign
  value ships that same vint). Frozen-glow deadlock structurally
  impossible: no grant -> full-fidelity shipping.
- SH-2 animator ticks the baked rules at 60Hz vint = the ARCADE's
  cadence (measured: phase step -2 EVERY frame, zero stutters - it
  is smoother than the 68K-shipped glow was). Pauses on any landed
  glow block; re-seeds both phases from live PAL_SH; reseed-fail =
  stays off, delta pipeline rules.
- Probe bake: glow words excluded globally (fixes the latent BOSS
  detect - its old probes sat on ring values); 0x0000/0xFFFF
  excluded as VALUES (flash/fade attractors - white probes matched
  mid-flash and reset the no-match counter).
- Unknown threshold 32 -> 16: nomatch counts K-vints only and the
  mask idles most vints (span topped at 29).

Rig PASSED: 5400f attract psw 2 = heal 2 = grants 2 (boot +
transform-exit; the full pause/reseed/regrant cycle), wedges 1,
transform span non-glow words EXACT truth, post-span state matches
truth (the white 0x20-0x2F line IS the game's real post-transform
state - the anchor was a different moment).

Residual (documented, heal-covered): 0xA6/0xAE (flash-only words in
masked blocks) can lag <=0.5s until the exit reload+heal re-ships
them. Sentinel values are level-1 constants in 68K code - the bake
should emit them per round (toolkit item).

**Gate left: Mike's pass on rom/s16_palglow.32x** - transform
sequence, boss fight, and the graveyard glow itself (now arcade-
smooth; his eye may notice it improved).

## v2.1 (2026-09-01) — Mike's "how it feels" pass, two heals

Mike's v2 verdict: intermission screens healed, framerate up (his
states measured game share 73% -> ~80%, handler 53.5-55.2). Two
feel-level symptoms, both fixed and rig-passed in
`rom/s16_palglow.32x` (883b580e):
- GLOW PAUSE AFTER FADES: pause 60 -> 8 vints + CONTINUATION-
  CONFIRMED reseed (the 3-landings law): grant only after live
  PAL_SH follows the ambient program for 3 consecutive vints (ramp
  step 0/-2/-4, wave 0..2). Recovery measured <=15 frames, was 60+.
  The continuation check also kills mid-flash regrant thrash.
- FLASH ARTIFACTS: the flash ran UNDER the mask (its ambient
  programs keep sentinels 0x99/0xA1 in-set) — its 0xA6/0xAE pulses
  were swallowed and the animator's wave phase stood in for the
  game's. THIRD SENTINEL 0xA6 (constant 0x100F in every scene AND
  every state census — the flash is its only writer) lifts the mask
  the vint a pulse lands. Span now plays 11-18 words off the
  transform anchor INCLUDING glow words = inside the truth build's
  own 6-18 jitter band; a6/ae pulses measured live on screen.
Ledger equality held in his real play: bs1 psw2=heal2=grants2, bs2
psw4=heal4=grants4 (bs3's 169-off palette = level-2/cutscene scene,
foreign to level-1 anchors by design).
