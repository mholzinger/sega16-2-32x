# HANDOFF — session 8 kickoff (written 2026-09-07 ~03:00, after session 7)

Session 7 did task 0 (shipped), built and measured BOTH write levers
from HANDOFF-SESSION7 to their floor, and re-measured what the frame
rate is actually bound by. Read section 3 before touching the blit
again: the write chase is closed on this line, with numbers.

Every number below is ares-headless, level-1 gameplay via
discover/inputs/play_level1.csv, frames 1500-4100 (2600 vints), read
with tools/gameplay_speed.py. "Speed" = game frames per vint (100 = 60
game-frames/s). The game's own IRQ4 frame-miss counter (WRAM 0xFFF144)
is printed too and equals 100 - speed exactly: every miss is a vint the
68K's pass had not finished by.

## 0. Roms on disk (commit 5578f78+, `make ship`)

| rom | what |
|---|---|
| rom/s16.32x | US ship line = SESSION 7 line + task 0. 50.4% (CAT1MD shipped and REVERTED 2026-09-07, see section 4) |
| rom/s16_cat1md_0907.32x | the same line + CAT1MD=1, failed Mike's play pass (shimmer, transform palette). 50.5% |
| rom/s16_altbeastj.32x | JP, same line; boots and plays (NTSC-J checked) |
| rom/s16_nocat1.32x | pre-session-6 base, untouched |

Tree clean after the session's last commit. Ship flags unchanged
(SHIP_US); ROWGEN / BLITHASH / probes are OFF by default.

## 1. TASK 0 — DONE and shipped

- `.ramtext` (the hot RAMCODE, ~23KB) moved out of the low region to
  SDRAM 0x31000-0x38000 (mars.ld). The MARS header copy carries .data
  only; `mcont` in mars_start.s copies .ramtext from ROM BEFORE it
  posts M_OK, and the 68K releases both SH-2s (0xACED) only after M_OK
  and S_OK, so the slave cannot reach s_main (RAMCODE) early. The
  manual-reset path (main_reset) copies it too.
- Render tile cache halved: 512 slots (CSETS 64 x 8 ways, 0x29000-
  0x31000). The MD VRAM slot map keeps 1024 (NSETS) through its own
  MD_SET fold — the two were coupled through CACHE_SET; that coupling
  would have halved MD residency silently.
- Result: .bss end 0x18f40 -> 0x13530 = 23KB free under the 0x19000
  guard; .ramtext slot has 0x1600 spare; 0x38000-0x39000 is probe
  scrap (RG_COUNT at 0x38E00, ROWSHIP census at 0x38000, RSHIP at
  0x38F00).
- Verified: gameplay 49.8 -> 50.4%, cadence 1.025, rejects 1.0%,
  skips 0, attract parity in family (OFFSET 51 vs 54, same rows), JP
  boots. Pixel-neutral by construction (a cache miss reads the same
  art from ROM).

First cut did NOT boot: the copy was in main_reset, which only runs
on the manual-reset vector. Cold start goes mstart -> mcont. Both
paths now copy.

## 2. THE LEVERS, measured (all vs task 0 = 50.4% walk / 49.3% idle)

| build | walk | idle | note |
|---|---|---|---|
| task 0 (ship) | 50.4 | 49.3 | rows shipped 61/vint (top 21.5, bottom 39.6) |
| CAT1MD=1 | 50.5 | — | rows 55/vint; +1.1 vs pre-task-0 base. LOOK = Mike's call, see §4 |
| BLITHASH=1 row hash | 47.4 | — | skipped 35 rows/vint, SLOWER (rotl5 = 5 SH-2 rotates) |
| BLITHASH=1 group hash | 49.3 | — | skipped 384 group WRITES/vint, still slower |
| ROWGEN=1 v2 | 50.0 | 49.7 | skips 26-31 rows/vint, master half 78.7 -> 66.0 lines/ship, PROVEN exact (0 mismatches / 120K skipped rows read back) |
| BLITSHIFT 40 / 56 / 72 | 50.1 / 47.1 / 44.8 | | 24 is the optimum |
| XDEF=MHALF_PROBE (master half never ships) | 52.4 | | calibration, picture wrong |
| XDEF=SHALF_PROBE (slave half never ships) | 49.0 | | calibration |
| NOBLIT=1 (no rows at all) | 60.1 | | calibration; misses 39.9% |
| PALROTOROFF=1 (68K ships no palette blocks after vint 900) | 50.0 | | 68K handler 62 -> 45.7 lines, speed UNCHANGED |
| MDCONSUMEOFF=1 (no MD-plane/SAT/art DMAs after vint 900) | 50.2 | | 68K handler 62 -> 51.1 lines, speed UNCHANGED |
| both 68K probes + ROWGEN=1 | 56.2 | 54.4 | THE FIRST CROSSING with correct SH-2 pixels; see §7 ladder |

Laws these buy (SESSION6 3c's "0.42 points per master line" is dead):

- THE BLIT IS LOAD-BOUND. Removing 384 group stores per vint (group
  hash) changes nothing; only rows that are never READ save time
  (ROWGEN), and a shipped row costs ~0.9 lines (57us) whichever half.
- THE GAIN IS THRESHOLD-SHAPED. The 68K's frame protocol (clear
  0xFFF01C, spin until IRQ4 sets it) makes every frame 1 or 2 vints.
  The 68K's frame is ~310 lines (pass ~212 + IRQ4 ~35 + handler ~62)
  against 262. Cuts below the threshold buy nothing until the total
  crosses it for SOME frames: -16 lines (rotor off) = 0 points,
  -13 lines of master blit (ROWGEN) = 0 points, the whole master half
  = +2, no blit at all = +9.7 (40% of frames still miss). To ship 60Hz
  the 68K frame must lose ~80-100 lines, not 15.
- THE 68K HANDLER IS A FIXED ~62 LINES ON EVERY BUILD (NOBLIT
  included): it is not waiting on the blit. Decoded from the PUSH
  AUTOPSY stamps (WRAM 0xFFA0B4..BE, HV counter, 8 samples f3000-
  3007): handler entry at vblank line ~17-21; palette rotor+compares
  21-24 lines; block selection 8-9; regs ship 3; rowscroll+pal ship
  1-7; tail ~12. Skipping the rotor (PALROTOROFF) takes the handler to
  45.7 lines — the palette-delta selection costs the 68K ~16 lines
  every vint.
- Where NOBLIT's +9.7 comes from: NOT the handler (unchanged at 61).
  It is the FM=1 span: while the SH-2 blits, the game's own staging
  writes spin in the FM-gate thunks inside its pass. Shorter blit =
  shorter spins in the pass, which is where the 262-line threshold
  is fought.

## 3. What is in the tree from session 7 (all OFF unless named)

- ROWGEN=1 (m_main.c, "ROWGEN v2"): write-knowledge row skip for the
  SHIP only. Compose untouched (every row cleared and recomposed;
  sbuf is always the truth). Per bank an accumulated dirty mask,
  master-written only; D_g = spans(snapshot g) | spans(g-1) | text
  rows whose 64-word hash changed | scroll strips that moved | new
  scene; ALL rows when the FG layer regs, an FG-visible page's
  content (cap_page compare), the pen-map signature (tile_grp/
  text_grp for the parity) or a cut moved. At ship start the mask is
  published (RG_SHIPM, uncached) before SYNC[4]; deferred rows are
  re-marked per CPU/bank. ROWGENVERIFY=1 reads every skipped row
  back from the FB: 0 mismatches over 120K rows, walk and idle.
  Pitfalls found on the way (each cost a build): spans(g-1) is
  required (a vacated row is dirty only in the previous snapshot);
  the launch-path build must be RAMCODE with word-mask spans (a ROM
  per-row loop cost 5 points); anything added to text_capture (the
  pre-flip path) trips the flip edge guard DIAG[44] and moves ISR
  flips into the body (1485 -> 38); the text hashes now ride in
  rowgen_build after the launch's cache_purge. Speed-neutral on this
  line for the threshold reason above; it becomes worth its 13 lines
  only once the 68K frame is near 262.
- BLITHASH=1 (+BLITHASHVERIFY): per-bank content skip per group.
  Negative result, kept as the record.
- Probes: ROWSHIP=1 + tools/rowship_hist.py (per-row ship count and
  identical-content count, per bank), WAITPROBE=1 (master half /
  wait-for-slave / post-to-start per ship), XDEF=MHALF_PROBE /
  SHALF_PROBE, PALROTOROFF=1 (68K side; from-boot skip deadlocks: the
  SH-2 boot needs the first palette blocks), SHIPBLITSHIFT=N (the
  ship line's split, overridable through `make ship-us`).
- Rig: tools/gameplay_speed.py (speed + miss counter + any SDRAM
  block as u32 deltas: `--extra addr:len`), tools/gameplay_align.py
  (screenshot two roms at the same GAME frame — note gameplay
  diverges between roms because inputs are keyed to vints; the
  attract is the deterministic pixel check), discover/inputs/
  idle_level1.csv (standing-still gameplay).
- Stale probes found: SHIMNOPUSH=1 and WINSPLIT=1 do not work on the
  NATIVE line (no boot / zero counters). Do not read numbers from them.

## 4. CAT1MD — the A/B for Mike's eye

screenshots/session7_cat1md/: attract frames 600/800/1000/1250 for
task 0 (`task0_attract_f*`) and CAT1MD (`cat1md_attract_f*`, captured
4 vints later = its attract OFFSET 55 vs 51), the side-by-side
ground-band crops `AB_ground_f*_top-task0_bottom-cat1md.png`, and two
gameplay frames each. Pixel diffs between the two are dominated by
the demo sprites being a vint apart; the ground band (grass/fence,
rows 150-224) is the thing to judge — under CAT1MD it is drawn by the
MD plane at 3 bits/channel next to FB sprites at 5. To my eye at 1x
they match; the seam Mike declined before is a fidelity call only
his pass can make. Speed prize if accepted: +1.1 points today, more
once the frame is near the threshold (it removes ~8 rows/vint from
the master half).

**MIKE'S CALL, 2026-09-07: CAT1MD colour translation ACCEPTED "for now".**
Judged on the 4:3 copies (`*_43.png`) of the attract A/B crops and the
gameplay pairs; top (task 0) and CAT1MD read the same to his eye.
Pixel evidence agreed: the A/B composites had the bottom half pasted 3px
low, which made a naive diff read 59% different; aligned, the grass and
fence rows are pixel-identical at all four anchors and the residual is
the sprites one vint apart. CAT1MD may join the ship line; the seam
question is closed until a later step restricts the FB cat1 pass.

**SHIPPED 2026-09-07 ("ship CAT1MD"):** SHIP_US now carries CAT1MD=1
(Makefile). Re-measured on the same rig the same day, 2600 vints,
frames 1500-4100: CAT1MD 50.5% (1286 misses) vs pre-flag control
50.4% (1289 misses) — speed-neutral at today's threshold, exactly as
section 3 predicts (the ~8 master-half rows only pay once the 68K
frame is near the boundary). Region guard _end 0x06013530. The
"dot moved backwards before .bss" linker warning is structural
(mars.ld places .bss below .ramtext since task 0), not a flag effect.

**PLAY PASS FAILED, REVERTED (2026-09-07 ~15:45):** Mike played the
CAT1MD ship rom (rom/s16.bs1 saved, BUILD 559b9dd5, vints/cycle 1.02):
"the grass feels shimmery, and the bottom layer of the screen seems to
have a different palette that shows through the beast transition
phase." TWO defects, attributed from his capture (screenshots_0907_1546
/ now screenshots/, frames 5724-5920) against the arcade oracle and the
09-05 base capture:
  (a) SHIMMER = CAT1MD, C1 step 2 (m_main.c ~3041: the FB cat-1 pass
      draws only over ROWLIVE rows, plane A's 3-bit copy shows
      elsewhere) — the renderer boundary moves with the sprites every
      frame. A still frame cannot show it; the attract A/B crops were
      pixel-identical for that reason.
  (b) TRANSFORMATION-EXIT PALETTE = the BASE line's hard-cut load lag,
      NOT CAT1MD. Arcade (ref_arcade 10822->10823): ONE-frame hard cut,
      no blank, wall at its final colour on the first frame. Ours
      (CAT1MD rom): 2 frames of garbage (red FB residue + black
      unresident MD cells) then ~16 frames with the wall band at teal
      [10,132,100] instead of [135,175,151], correct at +24. The 09-05
      base capture (screenshots_0905_2312, 4772-4792, pre-display-gate)
      shows ~20 frames of garbage at the same cut. The display gate
      cannot help: the arcade does not blank here. This is the PALSTATIC
      v1.1 / hard-cut scene class (LOOP9, docs/design/PALSTATIC.md), still open.
      Frames for Mike's eye: screenshots/transform_exit/*_43.png. SHIP_US is back to CAT1MD OFF; both roms
rebuilt; rom/s16_cat1md_0907.32x + .bs1 kept. Lesson for the kit:
C1 needs the single-renderer design (docs/design/BOSSFIGHT.md "future arc") or
it does not ship — no more screenshot-only acceptance for it.

## 4b. "SKY FEELS INCOMPLETE ON A RANDOM BOOT" — DIAGNOSED FROM MIKE'S TWO STATES (2026-09-07 ~16:30)

States: rom/s16_initialized_sky.bs1 (good, level, vint 1321) and
rom/s16_uninitialized_sky.bs1 (bad, INTRO "rise from your grave" scene,
vint 499), both BUILD 559b9dd5 (the post-revert ship line). Full-frame
reconstructions from the states (MD planes + shown 32X bank) and the
arcade intro (ref_arcade 3880) are in screenshots/sky_states/
composite_{init,uninit}_43.png and arcade_intro_f3880_43.png. The
ares .bs1 block offsets used are in the memory note ares-state-layout.

TWO ALLOCATOR-ORDER DEFECTS, both boot-random, neither transport:

 (1) FLAT SKY = the sky set (0x5C, tiles 0x1700-0x170A, 5 distinct MD
     shades). Good boot: it arrived while line 2 had free pens and got
     all 8 pixels EXACT. Bad boot: it arrived last, line 2 was full,
     and 5 of 8 pixels took the nearest-colour fallback (mdp_claim_pen,
     DIAG[36]) onto pens 4 and 14 -> five shades collapse to two -> the
     flat sky. (tools/state_frame.py --report reproduces the runtime's
     quantisation and its DIAG[36] counts; an earlier reading here that
     said "all fallback on both boots" used the wrong rounding.) Name tables, tags, MD CRAM vs the SH-2 tables: all
     consistent. The pack is first-come and the sky arrives after the
     line is full.
 (2) MISSING CLOUDS = the cloud tiles (set 0x4E, codes 0x139C-E,
     0x13B7-8) are not in the VRAM slot map in the bad state (1 free
     slot of 1024 in BOTH states: the title's cycling backdrop saturates
     it and LRU eviction decides who survives the cut). Cat-0 cells
     have no FB fallback, so an unresident cat-0 cell is simply absent.

Both are the case the native pivot named: per-scene STATIC assignment
(the sky set and the scene's BG sets get exact pens first; the scene's
tile set pre-claims slots at the cut) instead of first-come + LRU.
Not a CAT1MD effect, not a DREQ/lost-push effect. No code changed.
MIKE CONFIRMED (16:45): composite_uninit_43.png "is what I saw".

## 4c. STATIC-SCENE ARC — STARTED, FIRST BUILD GATED (2026-09-07 evening)

Mike approved docs/design/STATIC-SCENE.md ("looks good") and said go.
Built and measured on the ship line + MDSTATIC=1 (rom/s16_mdstatic.32x):

- tools/mdpen_bake.py -> sh_src/pal_scenes_md.h (gitignored, generated
  like pal_scenes.h; `make tables` regenerates from
  discover/palscenes/palharv_*.txt + *.bs1). From the two sky states:
  normal = 37 sets / 31 colours -> exact lines 15/15/6, 9 pens spare.
  The level-1 harvest (tools/palharvest_tiles_ares.py, play2 400-4000
  step 20) was still running at write time; re-bake with it.
- Runtime (m_main.c, MD_STATIC): mds_install() at the PALSTATIC scene
  load (tables wholesale, refcounts/owners rebuilt, every md_tag
  invalidated), mds_pin[] honoured by mdp_free_set and the eviction
  victim loop, pins dropped on the unknown-scene reset, mds_flush()
  at display-off inside disp_gate. Counters mds_ctr[4] in .bss (the
  0x28F40 "free" scrap is pg_quiet — stale comment, measured).
- tools/mdstatic_gate.py = gates 1-3 headless: dumps SDRAM/VRAM/VSRAM/
  CRAM/DRAM at frames N (+k jitter on every input) and runs
  tools/state_frame.py's report. RESULT (frames 560 intro / 1321 level,
  k=0..3):
    control rom/s16.32x : tables 4 DISTINCT outcomes per frame, DIAG[36]
                          4-18 -> the random-boot bug reproduces headless.
    MDSTATIC            : tables IDENTICAL across k (sha 8f9bdfd00d94),
                          DIAG[36] 12-13 (dynamic sets the 2-state bake
                          missed; the harvest closes that), no visible
                          stale cells (the ~600 "stale" are the 64x32
                          plane's off-window cells, never rewritten by
                          design). display-gate hold unchanged (59-65).
- Gate 4 speed: 49.9% vs control 50.4% = noise.
HARVEST BAKE LANDED (same evening): 178 harvest samples + the 2 states
-> normal = 39 sets / 36 colours -> exact lines 15/15/11, 4 pens spare.
rom/s16_mdstatic.32x rebuilt (_end 0x060135C0). Six-boot gate at
frames 560/1321/3000: DIAG[36] fallbacks = 0 on EVERY boot (control:
4-18); the baked sets' tables identical across k=0..4; k=5 met two
sets the harvest never saw (0x29, 0x2A) and gave them exact pens from
the spare line — the dynamic remainder working, counted separately by
the gate now. Visible stale cells 0. Display-gate hold 62-65 vs control
59-65. Speed 49.3% vs control 50.4% (control re-run 50.4 again): NOT a
68K cost — handler mean 63.1 (control) vs 63.4 (MDSTATIC) lines/vint
over the same 2600-vint window, slot claims 2562 vs 2622, CRAM writes
1900 vs 1133, vints/cycle 1.01 both. The 2-state bake of the same code
read 49.9, so the ~1-point spread is the scripted play diverging
between roms (inputs keyed to vints; section 4's caveat), not the flag.
Corpus for `make tables`: discover/palscenes/palharv_level1.txt + the
two .bs1 (gitignored like the .palsh dumps).
MIKE'S PASS (rom/s16_mdstatic.bs1, ~19:00): "stage 1 presentation is
now nearly perfect" — the sky fix holds. REGRESSION: "the splash
screens for the arcade broken, wrong palettes, the logo never fully
loads for the title." Reproduced headless (tools/mdstatic_gate.py
--png at frames 250/350): the PALSTATIC 'normal' detect fires DURING
THE TITLE (pscene_sw=1 by frame 250 on the control too — the game
preloads the level palette words the 8 probe pairs sit on), so the
MD install ran at the title and pinned 39 level sets there; the
title's own sets (0x25-0x2E) starved and the shared sets 0x72-0x7E
kept level remaps under title colours (4/4 fallback each), the drift
check's re-assign refused by the pins 130-221 times. Measured tile-
half distance to the 'normal' anchor: title 141 words, level 12; the
bake's TOL is 40. FIX: the install now requires that distance <= 40,
measured before the image copy overwrites PAL_SH (mds_ctr[4] counts
refusals). The rule is the bake's own clustering rule, so runtime and
bake agree by construction. Rebuild + title/level gates + speed
running at write time.
TWO MORE TURNS TO GET THE INSTALL SITE RIGHT (all headless, same
evening; every step in the gate logs):
  - distance rule alone: title fixed (attract scorecard == control),
    but the level never installed — PALSTATIC's detect fires once per
    scene and had already fired (and been refused) at the title.
  - MD-side installed-scene state (mds_scene_cur) + a retry: first
    inside the display-on hold, which re-fired at the TITLE because
    PAL_SH == the just-loaded image until the heal restores the real
    palette (~9 vints) -> mds_loadgap = 32 vints after any image load.
  - the retry then never fired at the level: the "same tables, just
    track" branch also ran while the load gap was open. Fixed: track
    only when something is installed.
  - the install is now SELECTIVE (a set whose dynamic remap already
    equals the table keeps its slots) and the retry runs every 4th
    vint whenever tables are pending, so a late install re-converts
    only the changed sets.
FINAL (build _end 0x060135C8): title refused (attract scorecard ==
control: boot card 25 by lag 2), level installed FROM THE HOLD at
the coin->level cut (the level palette is within 8-17 words of the
anchor for the whole hold), fallbacks 0 on 2 of 3 boot timings and 3
on the third (dynamic remainder), speed 49.7 vs control 50.4 (the
scripted-play spread). rom/s16_mdstatic.32x is the candidate.
SHIPPED (2026-09-07 18:32, Mike: "looking better... ship the fixes,
anything we've done we can undo"): SHIP_US carries MDSTATIC=1;
`make ship` rebuilt rom/s16.32x and rom/s16_altbeastj.32x (stamp
cf83ac94+, _end 0x060135C8). Pre-arc control kept as
rom/s16_pre_cat1md.32x (559b9dd5).
NEXT (Mike's order): (1) his pass on the ship rom; (2) the WRITE-TAX
CENSUS — count the game's per-frame hardware writes by region on
ares-headless and price each path (dirty-bit thunk ~50 extra cycles,
FB-window sprite writes + FM stalls, text-RAM handshake spins); if
the tax inside the "212-line pass" is 50-100 lines it is the 60Hz gap
itself, with mechanical levers (batch thunks, sprite list to WRAM +
SH-2 pull, hold FB writes outside the SH-2 span); (3) the Ghidra map
of the frame + opcode census for the static-recompile decision.

## 5. THE NEXT LEVER (recommendation), and why it is 68K-side

The 68K frame must lose ~80-100 lines. The SH-2 side has ~13 (ROWGEN,
built) + ~8 (CAT1MD, Mike's call) of master-half lines to give, and
those only matter once the 68K side has moved. The 68K side has:

a. The palette rotor (16 lines/vint, measured): move the palette
   compare to the SH-2. The game's palette writes are 68K WRAM
   (0xFF9000 mirror -> palette RAM); if the palette RAM lands in the
   FB the way FBTEXT landed text RAM (patch_game remap), the SH-2 can
   compare in place (cap_page already does copy-and-compare for tile
   pages) and the 68K ships no palette blocks at all. Prerequisite:
   the SH-2 boot needs the first palette (PALROTOROFF from boot
   deadlocks) — keep the DREQ palette path for boot/scene loads or
   read it from the FB from the first vint.
b. The FM=1 span (the blit) — NOBLIT is worth 9.7: every row off the
   FB shortens the game's thunk spins. ROWGEN + CAT1MD + player on
   VDP are the tools; they now have a measured value only in
   combination with (a).
c. The mark thunks (PAL/TILE dirty marks, +20 points in SESSION6's
   ladder once the blit was zero). cap_page compares content already;
   the tile marks may be removable outright. Not measured this
   session.
d. IRQ4 + the pass itself (~247 lines at 7.67MHz) are the game's own;
   nothing to take there.

ORDER (from the §7 ladder, each step measured in this rig):
1. The 68K handler diet, REAL versions of the two probes, ~27 lines:
   - the MD-plane/SAT/art upload (11 lines): fewer or cheaper
     FB-sourced DMAs per vint (batch the strips, skip unchanged
     name-table rows, art tails only on scene loads — the consume
     code at md_main.c md_consume/mdspr_consume/mdspr_upload_pump).
   - the palette rotor (16 lines): the compare moves to the SH-2
     (FB-resident palette RAM like FBTEXT, or the 68K ships only the
     dirty bitmap and the SH-2 reads the blocks from the FB).
   Alone these show 0 points — do not stop there; the miss counter
   will still read ~50%. Verify them by the handler mean (62 -> ~36)
   and pixel truth (attract parity unchanged).
2. Turn ROWGEN=1 on. It is built, proven exact, and worth +5.8 points
   on top of step 1 (56.2%), 0 without it.
3. Then every FB row band off the FB is worth points: CAT1MD (Mike's
   eye), enemies on the VDP (second CRAM line), text on the MD plane.
   The ceiling with the FM span at its floor is ~80% (consume off +
   rotor off + NOBLIT); SESSION6's all-stripped floor was 88%.
Measure every step with tools/gameplay_speed.py (speed AND the miss
counter) in walk and idle, and keep tools/attract_parity.py as the
pixel gate.

## 7. THE 68K FRAME ANATOMY (PCSAMP=1 + the push-autopsy stamps)

PCSAMP=1 (H-int at lines 55/111/167 logs the interrupted 68K PC, ring
0xFFA200; 8 dumps x 64 = 512 samples per build, decoder in the
session scratch, trivial to rewrite: PC classes = FM-gate thunks
0xFFBCF4-0xFFBE80, game idle loop 0x903980-0x903990, game code
0x900000+, WRAM else, ROM else; MASK THE PC TO 24 BITS). Ship line:
game pass 46.5%, game idle 31.6%, FM-gate thunk spin 21.5% of
samples; NOBLIT: 48.2 / 38.7 / 12.5%. The "line 55" sample lands at
V=39-71 on every frame: the vint handler (shim 62 + the game's IRQ4
35, RTE-trampolined) holds the H-int off until line ~58 of the NEXT
frame. Then the game's pass needs ~212 lines and the next vint is 165
lines away: it overshoots by ~47 lines — that is the 2-vint frame,
and it is the target: THE 68K FRAME MUST LOSE ~50 LINES.

Per-vint budget (vblank line 0 = the vint), from the stamps:
  0 -> 17-21   asm prologue + MD-plane DMA playback (BG on the VDP)
  -> 41-45     palette rotor + compares (21-24 lines)
  -> 50-54     palette block selection (8-9)
  -> 53-56     regs ship (3)
  -> 54-63     rowscroll + pal ship (1-7)
  -> 62-75     records ship + ack wait (~12)
  -> ~97       the game's IRQ4 (35, its own)
  -> +212      the game's pass, with the FM-gate spins inside it

Finer stamps (PALSTAMP=1, 0xFFA0C0..CA) split the "rotor 21-24":
the rotor LOOP is 4 lines and the selection 9; the other ~14 lines
are a STALL between two adjacent statements (post-glow stamp at
vblank line 24 -> rotor-start stamp reads HV = 0x0000, line 38): the
68K is held at its first VDP access and released exactly when
vblank ends. The only candidates are the 68K->VDP DMAs issued right
before the push whose SOURCE is the 32X framebuffer (md_consume A/B,
mdspr_consume, mdspr_upload_pump). With the 17-21 lines before the
push entry, the MD-plane/sprite upload is the largest non-game item
in the 68K vint (~30 lines) — larger than the palette rotor (16).
Not yet measured in isolation (my stamp attempts around the
consumes collided with existing diag words at 0xFFA0CC..F2; pick
addresses from a fresh audit): a probe that skips the consumes
(plane freezes) and reads the handler mean would size the prize in
one run.

MDCONSUMEOFF=1 (no MD-plane/SAT/art DMAs after vint 900; planes
freeze): 68K handler 62 -> 51.1 lines. So the MD upload is ~11 lines
of the handler, the palette rotor ~16, and both together take the
handler to 36.1 lines. NEITHER moves the game alone.

THE LADDER, walk / idle (68K handler lines in brackets):
  ship (task 0)                                50.4 / 49.3  [62]
  rotor off                                    50.0 / 50.1  [46]
  consume off                                  50.2 /  -    [51]
  consume off + rotor off                      50.4 / 51.4  [36]
  consume off + rotor off + ROWGEN             56.2 / 54.4  [35]  <- FIRST CROSSING WITH CORRECT PIXELS
  consume off + rotor off + ROWGEN + CAT1MD    51.5 / 51.7  [36]  (CAT1MD cells never reach a frozen plane; not a clean test)
  rotor off + ROWGEN                           51.2 / 50.6  [45]
  rotor off + NOBLIT                           73.2 / 64.5  [44]
  consume off + NOBLIT                         83.5 / 65.9  [47]
  consume off + rotor off + NOBLIT             80.4 / 80.8  [33]
  NOBLIT alone                                 60.1 /  -    [61]
Two cuts worth 0 alone are worth +6 to +23 together: the 68K frame
sits ~47 lines over the 262 threshold, the handler diet (-27 lines)
brings it to the edge, and from there every FM-span line counts —
ROWGEN's 13 master lines are worth +5.8 points THERE and 0 on the
ship line. The order of work follows directly (§5).

FBSPR=1 (sprites read from the FB in place) is NOT a free push cut on
this line: 68K handler 62 -> 99.7 lines, speed 49.2, attract parity
moves (demo 2: 52 vs 43). Dropped.

Rig pitfall that cost me three runs: `make ship-us "A=1 B=1"` passes
ONE variable named A with value "1 B=1" — B silently never applies.
Pass flags as separate words and check `.build_flags` (the run
scripts above count the expected -D's).

## 6. Do NOT repeat (measured this session)

- Row-level or group-level content hashing in the blit: slower or
  neutral (load-bound).
- Moving the blit split (BLITSHIFT): 24 is optimal.
- Widening the master/slave halves' skip in isolation: MHALF +2,
  SHALF -1.4; only the FM span as a whole moves the game.
- Reading speed off 68K-handler lines: the handler is fixed ~62 and
  independent of the blit.
- SHIMNOPUSH / WINSPLIT probes on the NATIVE line: dead.
- ROWGEN v1 (CUR|PREV, chain-head build): stale sprite blocks; v2
  replaced it.
