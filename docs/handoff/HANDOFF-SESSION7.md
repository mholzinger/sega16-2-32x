# HANDOFF — session 7 kickoff (written 2026-09-07, after session 6)

Read docs/handoff/HANDOFF-SESSION6.md sections 3a-3l for the full record. This file
is the state you start from and the ONE job: cut the framebuffer writes,
which is the frame-rate wall.

## 0. Roms on disk (commit 8c3c01f, `make ship`)

| rom | what |
|---|---|
| rom/s16.32x | US, accepted sprite line (player on VDP, gravestones on FB) |
| rom/s16_altbeastj.32x | JP, same line |
| rom/s16_nocat1.32x | the pre-session-6 accepted base, for A/B |

Tree clean. `make ship` builds both. Player answer for Mike: MOST player
poses render on the MD sprite hardware, not all (8 baked of ~49; drops
to software on unbaked poses). No colour loss because the player moves
over the background — the 3-bit seam only bites STATIC sets abutting FB
geometry (the gravestone lesson, 3j).

## 1. THE WALL, measured (SESSION6 3c/3k)

The game runs at ~50% of 60Hz in gameplay. Half the frame is the
MASTER'S WINDOW (64 lines: blit 37, flip+capture+restore 17, apply_cram
10), and the 68K waits on it because FM is exclusive. The blit is the
lever: it ships 37-42 framebuffer rows/frame (idle 34, walking 50).

Two hard facts:
- The blit's ONLY skip rule is "skip fully-transparent rows." An opaque
  row identical to last frame RE-SHIPS every frame.
- Most of the cost is SCROLL: the FB holds sprites+cat1+text, which
  scroll with the camera, so their rows genuinely change every frame
  (walking = +16 rows/frame of pure scroll). Row-skipping can't skip
  genuinely-changed pixels. The arcade/MD scroll planes+sprites in
  hardware for free; we re-rasterize. THIS is why the Genesis wins.

## 2. THE JOB, and it is now unblocked-by-one-thing

Both write-reduction levers are blocked on the SAME thing: SDRAM region
space. That is task 0.

TASK 0 — FREE SDRAM REGION SPACE (unblocks everything below).
  The guard: SH-2 .bss end must stay < 0x06019000. Ship is at 0x18f40
  = only 0xC0 (192 bytes) free. Every write lever needs more:
    - CAT1MD needs +0x248 of .bss DATA (moving code to ROM does NOT
      help — verified 3l; the growth is data, not .ramtext).
    - content-skip needs 1792B of scrap.
  SDRAM map (0x06000000 base): .text/.data/.ramtext/.bss up to 0x19000;
  then TILEMAP_C 0x19000-0x26000 (0xD000, 13 tile pages, page 12 blank);
  TEXT 0x26000; PAL_SH 0x27000; CACHE_C 0x29000-0x39000 (0x10000, the
  tile cache, 1024x64B); SPR_LAND 0x39000+; missq/md_dbg/cache_tag
  0x3A000-0x3C400; scattered scrap to 0x3FC00 (stack top). The free
  gaps above 0x28000 are all <=0x1000 and scattered — no clean 1792B
  block (that is why content-skip stalled).
  OPTIONS to make room, in order of cleanliness:
    a. CACHE_C is 64KB (1024 slots). Measured live cache use is far
       below 1024 (round-1 uses a few hundred). Shrink it to 512 slots
       (0x8000) -> frees 32KB at 0x31000. Verify miss rate doesn't
       climb (tile_rate / the cache_fill counters). This single change
       unblocks BOTH levers with room to spare.
    b. Move a shadow (TEXT_U/PAL_SH are 4KB each) if 32X CRAM/dumps
       allow — riskier.
  Do (a): shrink CACHE_C, confirm no miss-rate regression, then the
  region has 32KB and the guard stops fighting every build.

Then pick the lever (both are ~half-day AFTER task 0):

LEVER A — cat1 foreground onto the MD plane (CAT1MD=1). BIGGEST cut:
  takes the scrolling FOREGROUND off the FB entirely. Cost is the LOOK
  (the "two grass renderers" seam Mike declined once). FIRST build the
  A/B (it now fails to link only for lack of region — task 0 fixes
  that) and put a gameplay capture of CAT1MD on vs off in front of Mike.
  His eye decides; if the current renderer closed the gap, this is the
  frame win.

LEVER B — per-bank content-skip in blit_half. Safe, bounded: hash each
  sbuf row (80 longs), store per (bank,row) = 2x224 u32 = 1792B (from
  task 0's freed space); skip the FB write when this bank already holds
  that content. Idle 34 -> ~10 rows; nothing while scrolling. Collision-
  safe 32-bit hash; VERIFY with a per-frame pixel diff vs the ship rom
  (a wrongly-skipped changed row = stale band). blit_half copy loop is
  at m_main.c ~4662; the transparent-skip it augments is the
  `was==0x3FF && !ROWLIVE` line ~4651.

RECOMMENDATION: task 0 (shrink CACHE_C) first — it is a prerequisite
for both and low-risk. Then LEVER A's A/B capture for Mike's eye, since
that is where the big frames are and only his look can green-light it.
Fall back to LEVER B if he rejects the CAT1MD look.

## 3. Do NOT repeat (SESSION6 3l, measured dead)

- ROW_GEN flag flip: deadlocks (written for the pre-R60 slave chain;
  rowgen_build never reached under NATIVE, RG_CUR stays 0, flip gate
  declines forever).
- Widening player VDP poses 8->13: builds clean, 48.9->48.7% (no gain;
  the blit is row-granular, the player shares rows). Sprite offload only
  pays when a WHOLE ROW BAND leaves the FB.
- MD sprite offload of a STATIC set abutting FB geometry (gravestones):
  colour seam (MD 3-bit vs FB 5-bit at the ledge). 3j.
- SH-2 DMAC as a faster FB path: 69us/row vs the blit's 47-58. 3c.
- FMLATE/POSTLATE reorderings: no speed change. 3c.

## 4. The rig (no Mike needed)

- gameplay headless: `ares-headless --input
  discover/inputs/play_level1.csv` (coin=Y start=START; level 1 by
  ~f1400). Speed = scene timer 0xFFF02A per N vints (100% = 60
  game-frames/s). ares-headless at ~/src/ares-debug/build_macos/
  headless-ui/Release/ares-headless.
- attract: tools/attract_parity.py rom/s16.32x (arcade no-coin corpus).
- gates: tools/ares_gate.py report on sdram+wram dumps (cadence,
  rejects, skips, flip-late). Ship must stay cadence ~1.04, rejects
  <2.5%, skips 0.
- MEASURE BEFORE SHIPPING. Three of session 6's ideas died on
  measurement AFTER looking obvious. Confirm rows-saved AND pixel-clean
  before committing any blit change.

## 5. Laws earned session 6

- Area moved off the FB does not convert to frames; ROW BANDS do (the
  blit is row-granular).
- A static sprite set abutting FB geometry seams at the 3-bit/5-bit
  palette boundary; moving objects don't (they're over the BG).
- The write cost is scroll-driven and largely irreducible on the FB
  path; the fix is hardware offload, not cleverer skipping.
- Every SDRAM scrap slot is suspect: 0x28FF4 is state_health's,
  0x39xxx is inside SPR_LAND's 936-word R60 arm. Verify free before
  using; prefer freeing a real block (task 0).
