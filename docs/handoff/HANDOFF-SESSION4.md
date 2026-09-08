# HANDOFF — session 4 (2026-09-04/05), written at Mike's request

Read docs/handoff/HANDOFF-SESSION3.md for the rig, the laws and the shipping line.
This file is the delta and the honest state of the grass.

## Roms on disk (all built from this tree, canonical flags + LAUNCHEARLY
## BLITCHASE PENMATCH EDGE42 unless noted)

| rom | what | play2 ships | Mike's verdict |
|---|---|---|---|
| rom/s16.32x = 07d77855+ | SHIPPING (sphere build, pre-C1) | 1068 | "very very close"; grass one renderer |
| rom/s16_zeus2.32x | + CAT1MD, hot-way rule, cut re-arm, DAC-dot gate, Zeus pair fixes | 1227 | "you fixed Zeus"; grass shimmer |
| rom/s16_cand3.32x | zeus2 + glow scene gate + quick-claim LRU + HSSHIP v1 | 1209 | grass "no real fix" |
| rom/s16_cand6.32x | cand3 + scroll-sync census fixes (below) | 1230 | untested |
| rom/s16_nocat1.32x | cand6 WITHOUT CAT1MD | 1087 | untested — the grass-correct baseline |

## THE GRASS, plainly

Mike is right that the grass was perfect before. It was perfect on the
shipping build because the priority-foreground grass had ONE renderer:
the FB drew every cat-1 cell. CAT1MD (the C1 step, 2026-09-03) put cat-1
cells on plane A except on sprite rows, where the FB still draws them.
From that day the grass under the player and the grass beside it were
drawn by two engines whose scroll lands on different vints, and Mike
reported "two layers moving independently" on every build since.

Everything in this session's scroll-sync work (HSSHIP) narrowed that
seam and did NOT close it. Mike's verdict on cand3 was "no real fix",
and he is the gate. cand6 measures better on the census (59 of 63
generations coherent) but has not been seen by him.

The clean fix is CAT1MD OFF: rom/s16_nocat1.32x, 1087 ships (cand6:
1230; shipping: 1068). C1's speed gain is what the seam costs.
Decision for Mike: grass-correct at 1087, or the seam at 1230. My
recommendation: ship nocat1 as the base and treat C1 as a queued
speed lever that needs a single-renderer design (all cat-1 on plane A
including sprite rows, with the sprite priority solved on the MD side)
before it returns.

## What the scroll-sync census established (kept, useful regardless)

HS_CENSUS (XDEF=HS_CENSUS) rings, per generation, the vint the scroll-
carrying packet was copied and the vint the FB flipped; +1 = coherent.
Three holes were real and are fixed under HSSHIP=1:
1. the copy keyed on nat_shipped, which stays up across body-fallback
   flips -> explicit shipped-this-window flag;
2. ship after copy -> the ship re-patches the FB packet in place;
3. tile chunks carried no scroll and K2_FREE windows without a fresh
   packet copied nothing -> scroll rides EVERY packet as two header
   words sc[3]/sc[7] (the VDP runs full-screen hscroll, reg 0x0B=00, so
   the 56-word per-strip DMAs were writing a table it ignored — retired),
   and a shipping window that copied nothing posts a scroll-only stub.
Census 40/61 -> 59/63. Cost: none on play2. Watch: cut-blanks/gen
0.67 on cand6 vs 0.24 on zeus2, unexplained, no visible cost in the
sweep.

## Fixed this session and confirmed by Mike

- Zeus black scale-in: the game flickers him one frame in three; the
  pair map ran a generation behind. 4-gen held-pair window + quick
  claim at launch (+ LRU fallback when no pair is free — the black
  hit-flash frame). Mike: "you fixed zeus!!!!"
- Story-panel black dots and the dither band: MD DAC dots from CRAM
  writes outside vblank -> vblank-gated palette DMA with WRAM deferral,
  sprite palette skips the frame.
- Floating head / transformation: the level-1 glow animator was
  playing in the transformation scene (yellow flames, static band) ->
  scene gate, mask handed back to the 68K elsewhere.
- Black cells (edge builds): slot-cache thrash pinning cut mode ->
  cat-1 tiles never evict a hot way; dirtiness extends a cut, never
  starts one.

## Withdrawn, with the measurement

- Near-pen merge (sky checker): wrong for fades (chevrons flat, cloud
  blocks). NEARMERGE=1 to test; needs unmerge-on-drift.
- Soft pen-line claim: -12% soak.
- Two-column edge pre-claim: -9.5% soak.
- SPRHEAL (68K list shadow): the diagnosis it served was wrong.
- Fixed 56-word scroll block at 632: forced tile batch 36, cut-blanks
  doubled.

## Open

- Sprite-box pop-in (Mike's frames 3239-3245, cand run): unclassified.
- Tile bleed / scroll-in pop-in: art latency and hot evicts; ARTTAIL=1
  is built (art rides every cell chunk) but unproven — the rig cannot
  scroll at walking speed (0.25 px/frame on every input tried).
- Grass tone: ours brighter (9-bit rounding + MD DAC); the arcade's
  grass does not animate (scroll only).
- Sky "wrong palette": no frame number yet.
- cut-blanks/gen 0.67 on cand6.

## Japanese build (Mike's ask, TOOLKIT.md has the plan)

Milestone 1 DONE: `make GAME=altbeastj` -> rom/s16_altbeastj.32x from
roms/altbeastj. Graphics are the US bytes in a different split (SH-2
remaps under GAME_ALTBEASTJ); MCU uses the same 14 RAM addresses; the
program differs in 48% of its words, IRQ4 at 0x2ACA (derived into
md_src/game_irq.h). The build stops at the patcher's first US site
check (expect(0x16CC)). Milestone 2 = derive the tables into
tools/game_altbeastj.py (rebase_scan, fmgate_derive on a MAME altbeastj
census, TAS/dirty idiom scans); milestone 3 = boot in MAME, Mike's pass.

## Laws added

- Timing-only build differences spread +-6% on the battery (cliff
  chaos): A/B mechanisms with a census, not ship counts.
- MD CRAM writes outside vblank paint DAC dots; never reorder the 68K
  vint path to fix it (anything before the DREQ push costs ships).
- A savestate taken after an event shows healthy tables; reproduce in
  headless and dump AT the frame.
- Mike's pass is the gate; a census win that he cannot see is not a
  fix.

## Mike's pass on nocat1 (2026-09-05 evening) — ACCEPTED BASE

Mike: level-1 presentation in rom/s16_nocat1.32x is "nearly identical to
MAME ... you NAILED IT". Decision made: nocat1 (CAT1MD OFF) is the base;
C1 returns only as a single-renderer design. Open after the pass:
(1) the round-clear golden beam with Neff's head does not move;
(2) slowness / frame drops / stutter.

### The beam, measured (ref_arcade 15480-15720 vs screenshots_0905_1720 10000-10230)

- ARCADE: the beam (x 232-281) alternates EVERY FRAME between a solid
  pale-yellow column and a 1px checker of the same yellow over the sky
  (rows 0-89 change 2250/2250/4500/4500 px in a 4-frame cycle, the whole
  scene long). A 60Hz flicker-translucency. The colour 0x9EF fills sprite
  palette set 0x64 (words 0x640-0x64E, all pens) in bs_head.palsh and in
  no tile word: the beam is SPRITES with a flat palette; the two phases
  are two sprite frames.
- OURS (cand3 corpus, same scene): both phases render correctly, but the
  phase changes only when the ship cadence slips (solid held 3-5 frames,
  bursts), then LOCKS on the checker for 200+ frames = "non-moving".
  A 2-vint cadence samples the sprite list at one parity forever.
- So: not palette, not the glow animator, not a dropped writer. It is the
  60Hz gap made visible by a 60Hz effect (ship bar: 60 or best-to-60).
  Fix classes: (a) 60Hz shipping in this scene (static planes, one head
  sprite, the beam) — the docs/design/BOSSFIGHT.md arc; (b) a deliberate alternate-
  parity sample (3-vint gens) = 20Hz flicker, NOT shimmer — rejected on
  sight; (c) a compositor blend of the two phases = a method, not the
  arcade's frames. Mike's call; (a) is the bar.
- PRECEDENT: docs/design/BOSSFIGHT.md "ZEUS SOLVED" — the arcade draws Zeus one
  frame in three and our per-generation snapshot locked a phase; the
  fix there was the pair map, and a spatial checker for game-alternated
  sets was queued as cosmetics. The beam is that queue item's first
  real customer.
- The Neff ENTRANCE (~13450) likely uses the same effect ("Neff-pillar
  presentation", m_main.c:4262) — check the same way before touching it.

## Two-title build (2026-09-05 late, Mike: "parity builds, a Japanese rom
## and a US rom as two build outputs")

`make ship` -> rom/s16.32x (US, the accepted nocat1 line) + rom/s16_altbeastj.32x
(JP, same line). SHIP_US/SHIP_JP live in the Makefile; the shipping
line is no longer folklore in a zsh array.

- patch_game.py is GAME-AGNOSTIC: all 29 program-specific tables moved to
  tools/game_altbeast.py (US outputs byte-identical, checked on every
  generated file and the rom minus its 25-byte stamp). The abs.w jmp
  thunk target reaches md_main.c through game_irq.h.
- tools/game_align.py + tools/game_derive.py DERIVED tools/game_altbeastj.py:
  listing alignment by instruction shape (98.8% one-to-one, shift +0x18 in
  the code, -0x190 near the packed assets), every entry verified against
  the JP bytes, the two scannable idiom lists re-scanned after the scanner
  reproduced the US hand list exactly. Report = the module docstring.
  Two entries are flagged "verify in MAME" (REBASE_TABLES 0x6DA8 entries
  in an unaligned regional block; data-exclude ranges whose bytes differ).
- gen_sprites.py: JP pairs are 64KB (block = 2*len). SH-2: GAME_SPR_BANK /
  GAME_TILE_REMAP now also fold the sprbake lookup, the MDSPR key and the
  MD-plane residency key.
- JP status: boots; 68K side tracks the JP arcade frame-for-frame
  (state byte, layer regs, text/tile census at 600/1200/2400). The JP set
  sits 600 frames in its WARNING screen before the title — that is the
  arcade's timeline too.
- **MAME cannot pixel-judge ANY current build**: the accepted nocat1 rom
  is confetti in MAME's 32X, as is every parity_* capture since 08-30
  (docs/design/BOSSFIGHT.md said so on 09-01; CLAUDE.md now says so too). The
  "kit-baseline" flag subset (canon minus the census-derived tables) is
  confetti on the US as well, so it demonstrated nothing; the JP ships on
  the full line. The JP pixel gate is Mike's ares pass. If he wants a
  headless pixel gate back, the work is a MAME-renderable flag line, not
  a JP question.
- mame/altbeastj.zip packed from roms/altbeastj (local oracle, verified
  by -verifyroms). tools/parity_run.sh takes [game] [rom].

### JP input fix (Mike: "input isn't being accepted", 2026-09-05 late)

The board is the same; the MCU program is not. The 68K reads coins and
the screen-sync/sound mailboxes from work RAM the i8751 posts to, and
the two MCUs use different addresses AND a different order: US 317-0078
busy 0xFFF0C0 / coins 0xFFF0C2 / sound 0xFFF0C4; JP 317-0077 busy
0xFFF0D2 / coins 0xFFF0D0 / sound 0xFFF0D4. The JP program never touches
the 0xFFF0Cx bytes at all (listing census), so the shim's MCU replica
was posting coins nobody read. The I/O ports (joystick, service) are
identical in both programs — every 0xC4xxxx reference maps one-to-one.
Fix: three per-game keys MCU_BUSY/MCU_COINS/MCU_SND (tools/game_*.py)
reach md_main.c through game_irq.h; game_derive.py finds them by a local
shape search around each reference site (the differing RAM operand is
part of the instruction shape, so those sites never align). MAME: the
coin press now drops both roms from the eye scene to the title at the
same frame. Gate: Mike's pad on ares.

### JP black boss = a LOST PALETTE PUSH (shared class), and the belt for it

rom/s16_altbeastj.bs1 (Mike's screenshot: the boss a black silhouette,
the orb inpainted): the game's palette mirror has the boss's set 33
(words 0x610-0x61F, block 48), the 68K's PALDELTA shadow (0xFF6000) has
the same words = "shipped", PAL_SH on the SH-2 has ZEROS. Torn landings
254, BAD1 echoes consumed 243: ~11 pushes lost their blocks for the
session. Two holes: (1) a push that lands ZERO words posted no echo
("the echo needs landed>0"); (2) a BAD1 written straight into COMM8
while the previous one was unconsumed collapsed two tears into one
re-mark of ONE id list. Nothing JP-specific — the JP rom simply lost
the boss's one-shot palette push. Not the MDSPR pair classes (those
were MD sprites; this is an FB sprite with a zero PAL_SH block).
Belt (both CPUs, both roms): the SH-2 keeps r60_arm_seen from the 68K's
0xB101 announce and treats an announced zero landing as a tear; echoes
pend (bad1_post) until COMM8 is free instead of overwriting; the 68K
keeps its last TWO push id lists (0xFFA0C0 and 0xFFA044) and re-marks
both on an echo. DRQR[11] counts echoes raised. tools/state_health.py
prints LOST-PUSH words (shadow==game, PAL_SH stale): 18 on the JP state,
0 on cand3. GATE: Mike's ares session + a state — LOST-PUSH must be 0.
MAME cannot test it: under R60 every landing reads torn there (3897 of
3900 vints, SPRFULL or not) — which is also the whole reason MAME shows
confetti for these builds: no packet has landed in MAME since R60.

### Cold-start blue-white gravestones (Mike, 2026-09-05 late) = the same class at boot

Cold-only means uninitialised state. The 68K clears all 64KB of WRAM at
boot (md_start.s) and m_boot_init initialises the allocator tables, but
several fixed-address SDRAM blocks outside .bss were never written at
boot: PAL_SH, TEXT_U, SPR_SNAP, mdp_s_last, sused_prev, HSC_RING. On a
reset they inherit the previous run's values (sane); on power-on they
are random. The boot palette storm ships all 64 blocks raw, and a storm
push lost with landed==0 (the hole the lost-push belt now closes) left
PAL_SH's RANDOM words standing under a shadow that said "shipped": a
random ramp on whatever tile set that block held = the gravestones.
Fix: m_boot_init zeroes those blocks (residue is black, never a ramp;
the scene probes have no zero entries so nothing false-detects) AND the
belt re-marks the lost storm push. Gate: Mike's cold starts.

### Boot screen / transitions (Mike: "slow and littered with tiles") — DISPLAY GATE

Measured (screenshots_0905_1720 + ref_arcade): the arcade's two boot
cuts are FOUR BLACK FRAMES then the whole scene (frames 16-19 -> 20;
461-464 -> 465). It hides every tilemap load behind its video-enable
bit: port 0xC40001 bit 5 (MAME segas16b misc_io_w set_display_enable(
data & 0x20); jts16_main.v:247 video_en). The game writes 0xFFF018 to
that port — 0x80 during a load, 0xA0 after — on both machines at the
same frames (io18 census). Our mailbox kept the byte and nothing read
bit 5, so ours showed the load: the title card filling in three bands
over 40 frames, and the title->demo cut as 40 frames of the old scene
recoloured, black holes and garbage cells while pages and art streamed.
Fix (both roms): the 68K mirrors bit 5 into MD VDP reg 1 (0x8154/
0x8114) and into bit 15 of the packet's dirty-bitmap word; the master
sets the 32X mode to OFF while the bit is off and keeps it off after
display-on until no page copy and no art slot is pending for 2 vints
(cap 60), then restores MODE_256. While blanked the page budget opens
to 13 and the art batch to 40 — the blank is spent loading. Census in
state_health (DISPLAY GATE line): held-after-display-on is OUR lag per
cut; the arcade's is 1. That number is the speed work that remains.
Gate: Mike's eye on the boot and the first two cuts. MAME shows it
blanked forever (no packet lands there, so display-on never arrives).

### Cold-boot states s16.bs1 / s16.bs2 (2026-09-05 23:xx) — what they said

- s16.bs1 ("environment tiles in a wrong palette", capture screenshots_*):
  LOST-PUSH=104 words = colour sets 114/115 (0x390-0x39F). PAL_SH holds
  a BLUE ramp there — the TITLE CARD's — while the game holds the
  gravestone greys. So "blue-white gravestones" and "wrong-palette
  environment tiles" are ONE class: a lost push leaves the previous
  scene's colours standing on that set. 81 tears, all echoed; the loss
  was a push that landed NOTHING (no tear to echo).
  Belt v3 (this commit): a 2-bit push sequence rides packet word 20
  bits 13-14; the master echoes BAD1 on a gap, judged only from packets
  that landed (race-free), and the 68K re-marks its last two id lists.
- s16.bs2 (cold, "blue gravestones", no capture): LOST-PUSH=0 and the
  pen lines for sets 114/115 quantise to the right greys at save time —
  the state does not show the fault; either it had healed by the save
  or the blue was elsewhere. Next time: save the state WHILE it is
  blue, and capture.
- DISPLAY GATE census on bs1: 4 blanks, 37 vints held per cut. The
  capture still shows litter where the arcade keeps its display ON
  (the SEGA logo build-up, the title card's cells): that is the ART-
  LATENCY class (a cell reaches the plane before its art/palette), not
  the gate. rom/s16_arttail.32x (ARTTAIL=1, art rides every cell chunk)
  is built for Mike's eye against rom/s16.32x.

### s16.bs1/bs2 round 3 (build 0140ad1f): stale ART, not palette — two self-inflicted faults fixed

Mike's screenshot: the intro gravestone (an FB sprite, set 0, unclaimed)
shows the TITLE CARD's checker texture: the MD plane cell under it is
wearing stale VRAM art, i.e. a tile-art upload was dropped. LOST-PUSH=0
in both states. Tells in the states: 68K consume span max 94 lines
(was 48-49 before the gate) and 113 echoes for 64 tears.
1. DISPLAY GATE opened the art batch to 40 while blanked; the 68K's
   in-window consume overran and records were dropped. Batch back to
   MD_BATCH. (The page-copy budget of 13 stays: SH-2-side copies.)
2. Belt v3 echoed on sq != prev+1, which also fires when the same
   landing is harvested again (49 false echoes = raw re-ship storms).
   Gap = +2/+3 mod 4 only.
state_health now prints BELT: torn / echoes / re-marks.

### Transitions: where the remaining litter comes from, and the lever (2026-09-05 late)

After the gate, the litter left is where the arcade keeps its display
ON: the SEGA-logo build-up and the ALTERED BEAST logo scroll-in
(ref_arcade 200->235 whole; ours cell by cell, screenshots_0905_2312
233-465), and the title card's cells. It is UPLOAD RATE. MD-plane tile
art crosses SH-2 -> FB staging -> 68K -> VDP DMA, and that consume must
finish BEFORE the 68K's window post (the SH-2's V-gate accepts the post
only at V=DF..E2, ~4 lines into vblank) because the FB is unreadable at
FM=1 afterwards. DMA from the FB reads at ~85 words/line, so the batch
is capped at MD_BATCH=12 tiles/vint (40 measured 92 lines: it spilled
into active display at ~16 words/line — the purple-band cluster, and
today's stale gravestone slots). A cut that needs ~500 distinct tiles
= ~40 vints. That IS the held count the gate prints.
The 68K then sits idle in its ack-wait for 55-70 lines/vint. The MDSPR
blob already proves the escape: chunked CART->VRAM DMA at ~64 words/
line runs fine on ares in that idle (512-word chunks, ~8 lines). A
cart source has no FM constraint. So:
PLAN (a day): PRE-BAKED MD TILES. 44 of 128 colour sets already have
a fade-stable static class (tile_classes.h, 11 classes = fixed pen
lines). Bake every tile used by a static-class set as MD 4bpp with its
class pens into the cart (tools/ — same census as palpack_tiles.py);
the SH-2 emits (slot, cart offset) 2-word records instead of 34-word
art for those; the 68K DMAs them cart->VRAM INSIDE the ack-wait, up to
~150 tiles/vint. Dynamic-class tiles keep the FB path. Expected: cuts
of static scenes in <5 vints; scroll pop-in for static classes gone;
the 12-tile FB batch left to the dynamic minority. Pixel-neutral by
construction (same pens, same art). Not started.
rom/s16_arttail.32x (art rides every cell chunk) is a same-rate
ORDERING experiment for Mike's eye: it can remove garbage cells (cell
before art) but not the delay.

### Intermittent blue gravestone/statues = STALE CRAM PAINT (rom/s16_arttail.bs1)

Zero lost words; the FB draws the sprites (set 0 unclaimed) with pens of
pair 0, and pair 0's CRAM (cram_mirror slots 0-1) holds red/white/blue
= the intro lightning flash the game writes into sprite palette 0
FIRST; PAL_SH set 0 holds the greys; PAL_SETGEN[128] = 0 (the set's
generation never moved) while cram_keygen[0] = 2 and slot 1 carries
key 0x1006 gen 182. The memo (key + generation) hit over a stale paint
— the "first paint stood" class of docs/design/BOSSFIGHT.md, third occurrence. Why
the generation never bumped for set 0 is NOT resolved (the landing
walk bumps on any changed word, and PAL_SH was zeroed at boot).
Belt: apply_cram forgets one slot's generation per window (cram_rot),
so any stale paint repaints within 32 windows. Same rom line for JP.
ARTTAIL: Mike: "isn't doing what you think" — same litter; dropped.
