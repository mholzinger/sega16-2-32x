# LOOP 27 — DISCOVERY-TIMING (Ghidra + RTL + manual)

Protocol and resources: `docs/handoff/HANDOFF-DISCOVERY.md`. One entry
per iteration; every entry ends with a number and a verdict. Negative
results stay.

## 0. Setup and first census (2026-09-07)

- Ghidra 11.2.1 headless imports and auto-analyses `prog68k.bin`
  (68000:BE:32:default, base 0) in ~1 min; `tools/ghidra_run.sh census`
  runs `tools/ghidra/timing_census.py` against the saved project in
  ~20 s. Jython 2: keep scripts ASCII, take arguments from
  `getScriptArgs()`; the stock `ExportFunctionInfoScript` fails headless
  (askFile).
- Census (24-bit-normalised, second run): 234 functions, 272 backward
  branches; hardware-range instructions: text RAM 25w/2r, tile RAM 23w,
  palette 22w, sprite RAM 5w, I/O 13r/2w, MCU mailboxes 4r/4w, frame
  flag 2w/1r, miss counter 0xFFF144 1w; ~1400 work-RAM refs. Genuine
  waits found statically: the vsync wait 0x397E-0x3988 (clr/tst/beq on
  the frame flag inside a dbf), two MCU-mailbox bit polls at
  0x2D82-0x2D88 and 0x2DA8-0x2DAE (btst 0xFFF0C2 / bne) that sit in
  reset/restart paths (pea/rte to 0x1AFDE and 0x400, a 127-count dbf
  delay between) — boot-time only — and a text-RAM read loop at
  0x3ABE-0x3AC4 (the screen-sync handshake). Everything
  else in the 36 hardware-touching tight loops is a block copy/fill.
  0x3028 is a coin-counter countdown, not a poll (first read wrong).
  First run missed all work-RAM refs: short absolute operands arrive
  sign-extended from Ghidra; mask to 24 bits.
- RTL facts banked (jtcores, cited in the handoff): 68000 at
  50.3496 MHz x 29/146 = 10.001 MHz; DTACK wait cycles programmed per
  region by the game's own mapper table (`jts16b_mapper.v:284-292`):
  tile+text RAM 3, sprite RAM 1, everything else 0. The DTACK module
  (`jtframe_68kdtack_cen.v`) is in the jtframe submodule, not checked
  out — fetch before converting to 68K clocks.
- Manual: 22-page kit installation manual (DIP tables, test-mode menu,
  harness pinout, 15.75 kHz/60 Hz monitor). No CPU timing. Its test
  mode (ROM / Fix RAM / Scroll RAM / Color RAM / Scratch RAM tests) is a
  self-test oracle to run on the port.
- MiSTer: Nuked-MD `m68kcpu` netlist 68000 + `md_board.v` present; no
  HDL simulator installed.

Verdict: tooling ready. Next entry = question 1, the write-tax census.

## 1. Write-tax census — MAME is dead for it; moving to the ares fork (2026-09-07 late)

- `tools/write_census.lua` (68K write taps on every rebased region +
  fetch taps on the thunk blocks, coin+start at 600/800, per-500-frame
  bins, PC-at-exit probe). On BOTH the ship rom and the pre-arc control
  every write stops after the first 500 frames: at frame 2000 the 68K
  PC is in shim RAM (0xFF0DF6 / 0xFF0A26) with adapter reg 0xA15100 =
  0x8080 — the handler's window wait, FM held by the SH-2 side that
  never answers in MAME (CLAUDE.md (c): no R60 packet lands there).
  irq4 misses 332 of ~353 game frames. So MAME cannot carry a dynamic
  census on any current line; the "68K still reads true" note covers
  RAM contents, not progress. NEGATIVE, banked.
- Also: MAME's 68K taps on the 32X framebuffer window (0x840000-0x85FFFF)
  saw ZERO writes even during boot/title, when tile RAM (rebased there)
  is certainly written — the shared-RAM device bypasses the 68K-space
  taps. A second reason MAME cannot do this census.
- What the first 500 frames (boot + title) did show, per frame: palette
  ~24 writes, sprite RAM ~11, text RAM (WRAM half) ~10, I/O ~2; pal
  thunk block fetched 7151 words, tile thunk block 334 words. Sites, not
  gameplay; kept only as a sanity scale.
- Next: a 68K write-range counter in ares-debug (`--count-writes
  lo:hi:name`, per frame + top PCs), same plumbing as the COMM/flip
  hooks, then the census over play_level1 frames 1500-4100.

## 2. Write-tax census on ares — the game's steady-state writes are FEW (2026-09-07 late)

Tooling: ares-debug now has a 68K bus write hook (`--count-writes
lo:hi:name`, repeatable, `--count-writes-out CSV`; per-frame counts +
top writer PCs; `tools/write_census_ares.py` drives it). Ship rom,
play_level1, window frames 1500-4100 (level-1 gameplay), per frame:

    region          per-frame  peak   note
    tileram_fb          2.0      4    steady state; the 153k total is the
                                     scene-load fill at 0x36C0 (98k writes)
    textram_fb         23.0     62    HUD/score writers (0x4D96, 0x3AC2)
    textram_wram        3.0      6    scroll regs/page selects (IRQ4 0x2ADA..)
    spriteram (WRAM)   45.7    134    the IRQ4 sprite upload (0x2B3A-0x2B3E)
    palette (WRAM)     14.5     53    fade/glow writer 0x30FC-0x3102
    io_bank             5.0      6    mailboxes
    tile thunk entries ~0.0           (the 1.0/frame is the shim's own clear)
    pal thunk entries  ~1.6           (0xFFBA1C; the rest of pal_dirty32 is shim)
    fbwin_any          27.0     64    tile + text through the 32X window
    adapter           165.1    262    116/frame is the SHIM's COMM/FM traffic
                                     (0xFF0962), not the game

So in steady gameplay the game makes ~90 hardware writes per frame,
~27 of them through the framebuffer window, and enters a dirty thunk
about twice a frame. Even at +54 cycles per thunked write (jsr/ori/rts)
the thunk tax is < 1 line; the window writes are priced by the clocked
run (entry 3). The big write bursts (98k tile writes, 33k text writes)
are the scene loads inside the display-off blank, where nothing is
gated. VERDICT so far: the write tax cannot be the 50-100 lines the
hypothesis needed; if the 212-line pass hides a stall it is on the
READ side (the FM-gate spin reads 0xA15100) or it is real work.
Next: price the window writes (clocks), then count reads of 0xA15100
by PC — the gate thunks' spin — the same hook mirrored on CPU::read.

## 3. THE FRAME, DECOMPOSED: the tax is the FM-gate spin, ~83 lines every other vint (2026-09-07 late)

Tooling: ares-debug also got a 68K READ hook (`--count-reads`, same
shape; `tools/write_census_ares.py --reads`). Pricing by ares' own bus
clock was tried and is a NEGATIVE: ares' 68000 charges every access the
same fixed clocks (the M68000 core times bus cycles outside CPU::read/
write; the 32X window adds nothing, bus.acquired() never waits here), so
"clk/write" reads 0 for every region — there is no write tax in ares'
model at all. Spins are priced from the 68000 instruction table instead.

Reads per frame, window 1500-4100 (mean / p50 / p90 / max):

    0xA15100 (FM reg)         773 /  770 / 1553 / 3236
    0xFFF01C (frame flag)     918 /    4 / 2733 / 5067
    gate-thunk code fetches  3938 / 3919 / 7824 / 15803
    game code fetches (hi)  11488 /11079 /15485 / 20751

THE PATTERN IS A STRICT TWO-VINT ALTERNATION (frames 2000-2023):
    vint A: FM reads ~8,   flag reads 4,    game fetches ~14.6k  -> the pass runs flat out, misses
    vint B: FM reads ~1550, flag reads ~1450, game fetches ~7.7k -> pass finishes, then TWO spins
So per game frame (2 vints = 524 lines), pricing tst.w (abs).l + bmi.s
taken = 26 clocks, tst.b (abs).w + beq.s taken = 22 clocks, 487 clocks
per line:
    FM-gate spin      ~1550 x 26 = 40.3k clocks =  83 lines   (waste: the game waits for the SH-2's window)
    frame-flag spin   ~1450 x 22 = 31.9k clocks =  65 lines   (slack: the game finished early on vint B)
    shim handler       2 x ~63               = 126 lines
    game work          524 - 83 - 65 - 126   = 250 lines   (22.3k fetched words = 5.5 clocks/word, plausible)

WHO SPINS: gate #22 at 0xFFBE4C, game site 0x3AC8 (`lea text,%a1` in
FUN_00003aae, the text-RAM writer called from 8 main-loop sites) —
2.68M iterations over the run, 75% of all FM spins; second, the gate
at game site 0x372E (`btst #0,flag`, the main-loop upload dispatcher
0x3728-0x3742) — 0.67M. The rest (< 30k each): 0x3AB4/0x3ABE (the text
clear loop), 0x4DA0, 0x5700, 0x36B4, 0x3852, 0x16E8. The game hits the
text writer early in its main loop, the SH-2 still owns the FB, and the
gate holds it for the whole window (isr span mean 50.7 lines +
pickup) — that is the 83.

WHAT THIS SAYS ABOUT THE ARGUMENT:
  - Mike's hunch was right in kind and wrong in mechanism: the game's
    212-line "pass" is NOT arcade work + a write tax (writes cost ~0
    here; ~90/frame in steady play); it is ~250 lines of real work per
    game frame PLUS an 83-line wait on our framebuffer ownership.
  - 250 lines at 7.67 MHz = ~192 at 10 MHz = 73% of an arcade frame:
    the arcade's 68000 idled ~27% per frame (its vsync wait), which is
    exactly the slack the port spent.
  - Reaching 60 Hz needs work + shim <= 262 with the stall gone:
    250 + shim. The shim is 63 today; the stall removal alone leaves
    313 -> still 2 vints. Stall removal + shim <= 12 lines = 262: the
    threshold, on paper. Both levers are ours; neither touches Sega's
    code beyond the gates.
  - The FM stall is CHARGED ONLY on vint B; vint A carries no spin. So
    the stall is not "why frames miss" (vint A misses on work alone); it
    is why vint B cannot start the NEXT frame early. Removing it turns
    the fixed 2-vint cadence into ~1.2-vint work — which, by the
    threshold law, still rounds to 2 until the handler diet lands.

Next (question 4): the text writer's gate. Options measured next:
(a) FBTEXT off (text RAM in WRAM, shipped by the shim — costs handler
lines, kills the text gate), (b) a WRAM text staging + copy in the
shim's own window, (c) move the SH-2's FM span so the text writer runs
before it (the game's main-loop order is fixed; the SH-2's window is
ours to place). Each is one flag on the speed ladder + the read census.

## 4. QUESTION 4 — the frame TIMELINE, and the text writer's gate (2026-09-07 night)

Tooling: ares-debug got `--trace-access lo:hi:name:fa:fb` (every 68K
read/write in a range during frames [fa,fb), stamped with the VDP beam)
and beam columns on `--trace-comm`; `tools/frame_timeline.py` prints
per vint, in LINES from vblank start: the shim's FM raise, the FM drop
(exact when a gate spins), the game IRQ4 entry, its miss count, every
gate thunk's entry line (thunk # from fmgate_tab.h), the idle-loop
entry, and the level-loop top (fetch of 0x97C). Ship rom, play_level1,
five 12-frame windows across 1600-3611. What it says, and what it
CORRECTS in entry 3:

  - The shim is 67-87 lines at the vint (IRQ4 entry line), not 63: the
    raise is at line 16-21, the game's IRQ4 gets in at 67-87 and runs
    ~25 (set-flag path) or ~3 (miss path). Entry 3 priced 126/game
    frame; it is ~150-160.
  - The game's pass starts 2 lines after IRQ4 returns (loop top 93-108)
    and the CREDIT-LINE writer (0x3AAE, gate #22) is its SECOND
    statement: entry 2 lines after the loop top, every frame. The
    upload dispatcher (0x3716, gate #13) is reached 140-190 lines of
    work later; the HUD writer (0x4D88, #23) ~116 lines in.
  - The FM span on a spin vint ends at 170-240 (mostly 180-195). So
    the gate that spins is whichever FB writer the pass reaches first
    while FM is still up: #22 when the pass (re)starts inside the span
    (frames 2400+, 3000+, 3600+: 75-107 lines), #13 when #22 slipped
    through before the raise took (2000-2015: 81-89 lines). Entry 3's
    "75% / 25%" split is this, not two independent stalls.
  - TWO REGIMES of game work per frame (loop top -> idle, minus spin,
    minus the intervening vint's shim+IRQ4):
      light (2400-2411, 3000-3011):   ~147 lines  (idle ~105-125)
      heavy (2000-2015, 1600+, 3600+): ~183-190   (idle ~55-65)
    Entry 3's "250" was the light regime's work PLUS the mis-priced
    shim; the arcade-idle inference (27%) stands in spirit, the number
    does not.

THE THRESHOLD, re-done per regime (shim + IRQ4 + pass <= 262):
      light:  74 + 25 + 147 = 246  -> FITS ONE VINT if nothing spins
      heavy:  76 + 26 + 185 = 287  -> 2 vints regardless (handler diet: -25)
So the text writer's spin is not "vint-B slack" (entry 3, the handoff's
prior): in the light regime it is THE thing holding the frame at 2
vints, and it will hold EVERY frame at 60 Hz, because a 60 Hz window
is the long window (FM 20 -> ~190) and the writer sits at pass line 2
(vint line ~100). The dispatcher's writes at pass line ~140 land after
the drop once the frame fits; #23 (HUD) at ~116 lands at ~215, also
after.

HYPOTHESIS (written before the build):
  CREDWRAM=1 — the credit writer's text base rebased to the WRAM text
  mirror, its gate a mark, the shim copying its 10-word footprint at
  FM=0 before the raise (+0.4 handler lines on dirty vints):
    - FM spin at #22 -> 0; #13 spins remain in the heavy regime (same
      length, they are the drop time minus the pass position);
    - light-regime frames go to ONE vint; heavy stay at two. The speed
      number rises by the light regime's share of frames (unknown;
      guess 5-25 points), the IRQ4 miss rate falls by the same;
    - textram_fb writes -10/frame, textram_wram +10; fbwin_any -10;
    - attract scorecard unchanged (the credit line is in the title and
      demo captures; a missing/late line would move title/eyehold),
      mdstatic_gate unchanged.
  (a) FBTEXT=0 (all text via the packet): NOT built. It moves the whole
  text region through the FIFO (256 words/vint rotating = ~8-12 handler
  lines, glyphs up to 8 vints late) — it spends the scarce resource
  (handler lines) to buy back a spin that costs the frame nothing once
  the writer is off the FB, and it undoes LOOP 20's parity win.
  (b) WRAM staging for ALL text writers: the general form of CREDWRAM;
  needs per-writer footprints (#0, #9, #15, #23 + the loop heads' a1/d0
  ranges) or a 33-line full copy. Only worth deriving if another writer
  shows up at the top of the pass after CREDWRAM.
  (c) moving the window: dead under R60 — the window is pinned to the
  post at line ~20 (flip inside vblank, LOOP 23 v2) and lasts to ~190;
  the only "move" is shortening it (ROWGEN/NOBLIT class), which is the
  SH-2-side lever HANDOFF-SESSION8 5b already lists.

RESULTS (same night; flag TXTWRAM=1, table TXT_WRAM_WRITERS in
tools/game_altbeast.py; rom parked as rom/s16_txtwram.32x):

  step 1, credit writer only (CREDWRAM, since folded into TXTWRAM):
    speed 49.7 -> 49.7 walk. The spin MOVED to the next FB writer at
    the top of the pass, the HEALTH-BAR writer FUN_4d54 (gate #23 at
    0x4D88, entered ~25 lines after the loop top, 59-107-line spins).
    The hypothesis's "#23 at pass line ~116" was measured behind the
    credit spin and was wrong by ~90 lines: pass positions must be read
    on a build where nothing earlier spins.
  step 2, credit + health bar (TXTWRAM):
                         control    TXTWRAM
    walk  (play_level1)   49.7%      66.4%   IRQ4 misses 50.3 -> 33.6%
    idle  (idle_level1)   48.5%      57.3%   misses 51.5 -> 42.7%
    FM reads/frame         773        408    thunk-fetch lines 1.08 -> 0.59
    textram_fb writes       23         47    (the shim's copies: 50 words
                                             per dirty vint, ~2 lines)
    textram_wram            3          21    (the two writers, 0x4D96 +
                                             0x3AC2, now in the mirror)
    timeline 3600-3611: EVERY vint is a one-vint frame (no misses,
    dispatcher #13 at 220-232, idle 20-30 lines); 2400-2409 likewise
    (#13 at 239-255, 5-15 lines to spare); 2000-2011 and 3000-3011
    (heavy) alternate 1-vint frames with misses whose #13 spins 30-139
    lines against SH-2 windows that ack at 198-249.
  Remaining spin = the upload dispatcher (#13 at 0x3716) in heavy
  frames: reached at pass +105..127 while the long window is still up.
  The SH-2 window (post..ack) runs 90-250 lines; on 1-vint frames it
  ends 94-231. That is now the lever: window length (ROWGEN/NOBLIT
  class, HANDOFF-SESSION8 5b) and the handler diet (74 -> ~50) both
  move heavy frames under the threshold; the text spin no longer does.

  Fidelity: attract scorecard identical to control (same collapse
  columns, values within alignment noise); HUD renders (credit line,
  score, lives, health orbs) at f3000 on both. mdstatic_gate MD lines
  identical, BUT at the same GAME frame (ctrl f1458/f2363 vs TXTWRAM
  f1321/f2000) TXTWRAM shows nearest-colour fallbacks DIAG[36]=2 and set
  assigns 45 vs 0/35, and the display-gate hold at the boot card is 43
  vints vs 23. Both counters are SH-2 pen-packer/pipeline state, not
  text; under investigation below (frame sweep) — a speed-exposed
  pipeline effect (more distinct frames per vint) or a staging bug.

  FIDELITY VERDICT on the counters: DIAG[36]'s low bit is the R60 A/B
  packet parity (m_main.c r60_pkt_flip), so ctrl reads 0/1 and TXTWRAM
  2/3 = ONE real nearest-colour fallback, and the 10 extra set assigns
  all land between vints 350 and 500 — the attract's cut into the demo
  scene, where the coinless game runs at the new cadence. The extra
  sets are freed again by 700 (the set tables at 700 differ only by four
  line-1 sets ctrl still holds), the MD lines are identical at 1321 and
  2000, and the attract scorecard (aligned on the game's own timeline)
  is equal. This is the allocator's documented order sensitivity
  (first-come pens + LRU slots, HANDOFF-SESSION8 4b) seen through a
  different vint cadence, not a text-staging defect — the per-scene
  static assignment arc is its fix. Both frame-500 renders are mid-load
  and neither shows the credit line; the HUD is complete on both at
  f3000. VERDICT: TXTWRAM passes every headless gate; Mike's play pass
  (rom/s16_txtwram.32x) is the acceptance.

  OPEN ITEMS found on the way (not today's question):
  - fmgate_derive gap class: span (0x4D80,0x4D98) had its first
    iteration ENTER by fall-through (0x4D7E bcc not taken -> 0x4D80),
    ungated; health < 16 would have written the bar at FM=1 on
    hardware. Moot under TXTWRAM (span dropped), but the derivation
    only scans branches — audit the other 8 spans for fall-through
    entries.
  - 1 of 48 sampled frames (2403): a MAIN-loop caller of the glyph copy
    loop 0x3A9A with an FB destination (the 0x1200..0x168E family) hit
    gate #20 mid-window and spun 158 lines. Identify the caller with a
    fetch trace when it matters.
  - The health-bar copy is 40 words every vint (the bar is redrawn each
    frame); P1-only games need 16. Trim if the handler diet wants it.

## 5. TXTWRAM PLAY PASS — FAILED on ares; the 60 Hz cadence exposes the SH-2 side (2026-09-07 ~20:00)

Mike's pass (rom/s16_txtwram.32x, capture screenshots/ 1660 frames,
state rom/s16_txtwram.bs1): vints/cycle 1.05 — the first 60 Hz session
— with two faults: (1) "entering a coin changes the screen palette for
the splash" (frame 529), (2) "game logic runs but sprites are not
making it to the screen" (frames 1195-1659).

DIAGNOSIS, each fact measured:
  (2) SPRITES. Frames 1597/1600 show an actor drawn as a FLAT RED
  silhouette that persists 30+ frames: a sprite whose colour set has
  no CRAM pair, drawn in the shadow ramp (m_main.c SPRLATE[3]). Headless
  on the same 2600 vints (gameplay_speed --extra 0x3A7D8:16):
      SPRLATE [0] map misses  [2] claimed  [3] ramp draws
      control        10            10            19
      TXTWRAM        25            25           247
  Every miss is claimed late, yet the set draws in the ramp ~10x per
  miss; the persistent case is the per-parity map (spr_pair[par]) not
  being rebuilt when a torn landing drops the R2 terminator (the
  "black Zeus" note at the late claim). Torn/incomplete landings rise
  with the cadence: DIAG[17] 94 -> 152 of ~3950 cycles headless (2.4 ->
  3.9%), 79 of 1531 (5.2%) in Mike's session, and the lost-push belt
  re-marks after each. NOT a text-staging fault: the sprite records and
  palette blocks ride the packet exactly as before; what changed is
  that every vint is now a full cycle.
  (1) SPLASH PALETTE. Mike's frame 529 (grey-green relief, "1 PLAYER
  START ONLY / CREDIT 1") is pixel-identical to frame 385 of his OWN
  previous capture on the ship line (screenshots_0907_2002) — the card
  the port shows after a coin has looked like this before TXTWRAM. The
  arcade (MAME altbeast, coin at frame 300, scratch oracle_coin.lua)
  shows that card as a blue monochrome relief with "2 CREDITS 1 PLAY":
  a pre-existing palette difference on the post-coin card, its own
  item, not this flag's regression. Headless counters agree: at vints
  320-440 (coin at 300 on the splash) both roms read 35 set assigns and
  0 fallbacks.
  (3) A REAL TXTWRAM COST found on the way: the shim's footprint copy
  (50 FB word writes) sits BEFORE the announce/post, and with the
  larger per-vint consumes at 60 Hz the post moved from line ~20 to
  ~27 (timeline). ISR flips fell from 40% to 21% of vints (DIAG[58]
  1624 -> 875, body-fallback [56] 1635 -> 2915, flip-edge guard [44]
  3073 -> 5506, no-post bails [60] 149 -> 289). The text copy belongs
  AFTER the post — i.e. in the R60 packet (a 26-50-word text-patch
  section applied to TEXT_U after the capture, before the restore),
  not in the FB path at all. Not built tonight.

VERDICT: TXTWRAM does what it claims (the 68K frame fits one vint in
the light regime; text lands: credit line, health bar, HUD all render;
attribute bytes verified against ship-line states). What it exposes is
that the SH-2 pipeline was never run at one cycle per vint: the sprite
pair map and the DREQ landing both degrade there. The flag stays a
probe. The next work is on the SH-2 side, in this order:
  a. sprite pair map at 60 Hz: rebuild spr_pair[par] for BOTH parities
     on a torn landing (or share one map), and make the late claim
     effective for the draw that follows it — measure SPRLATE[3] back
     to the control's 19;
  b. torn landings: why 2x at the cadence (push vs window overlap;
     the F103 restore-done gate);
  c. move the text footprints into the packet (post no longer delayed).
Then Mike's pass again.

## 6. ITEM 1 (sprite pairs at 60 Hz) — measured, mechanism pinned, fix is a design call (2026-09-07 late night)

Mike: "1, then the other two". The hypothesis of entry 5 (rebuild the
per-parity map on a torn landing) was tested and is WRONG; here is what
the counters say instead. Rig: lean SPRLATE slots [4..9] at 0x3A7E8..
(census code in compose_sprites and the late claim, ship-safe, left in),
`gameplay_speed.py ROM --extra 0x3A7D8:40`, TXTWRAM builds unless named.

  - Every shadow-ramp draw is on the SLAVE ([4] == [3] in every run).
  - The slave never purges its cache and the pair map is a cached alias
    (0x3F800): SLVPAIR=1 (uncached read in the compose) — NO change in
    ramp draws. Not the mechanism (flag kept, harmless).
  - Draws do not happen during bm_tail's map rebuild (0 of 266) nor
    before the cycle's late claim (1 of 266): they happen AFTER a claim
    that FAILED. "Nothing stealable" [1] = 31-67 per run; ramp draws =
    ~8 records x failures.
  - Capacity census at each failure (14 pairs): ~8 owned by sets live in
    the snapshot, 5 blocked by static tile-class groups, ~1 held by an
    absent owner too young to steal, ~1 held by a set live ONLY through
    MD-VDP-drawn records.
  - The other parity's map holds the pair for 11% of ramp draws (a
    small parity/latency term, not the story).
  - SPRMDFREE=1 (sets live only through MD-drawn records release their
    pair, build_maps + late claim): failures 33 -> 18 in one A/B; the
    late-claim scan had to skip MD-claimed records too (first cut
    claimed pairs for them: 739 pointless claims).
  - Demand per late-claim scan: live 32X sets mean 5.0-5.5 on both
    cadences; MD-claimed records per snapshot 2.1 (ship line) vs
    0.85-1.7 (TXTWRAM runs) — the MD sprite path takes FEWER records at
    the faster cadence, pushing sets back onto the 32X pairs.

WHY 60 Hz AND NOT 30: build_maps builds each parity's map one cycle
ahead from the previous snapshot and can re-budget the tile groups
(`bound`); at two vints per game frame the "previous" snapshot IS the
frame being drawn, so the map is complete and the late claim rarely
runs (10 misses / 2600 vints). At one vint per frame every new set
hits the late claim, which cannot touch tile-group pairs, and with
5 + 8 of 14 pairs taken it fails; a failed set draws with base 15 —
the shadow ramp — until build_maps catches up, and build_maps' own
over-budget path shares pair 15 too (the persistent red silhouette).

THE CADENCE IS ON A KNIFE EDGE: eight builds differing only by census
code in the SH-2 window measured 49.0 / 52.2 / 52.5 / 52.9 / 57.4 /
62.1 / 63.7 / 64.5 / 66.4 / 73.7 % on the same script. The dispatcher
spin (#13) lands within a few lines of the SH-2 ack, so a few
instructions in the window flip whole stretches between one and two
vints. Any sprite-budget A/B must be read through SPRLATE counters, not
speed; and the window length is the real lever (entry 5 b).

RESULTS of option 1 (Mike: "go with 1 for sure"), the same rig, all on
TXTWRAM; ramp draws = SPRLATE[3], failures = [1] over 2600 vints:

    build                                 failures  ramp draws  speed
    TXTWRAM (census only)                  31-67     266-893    52-57
    + LATESTEAL0 (age-0 steal, 9+ live)      0-3     247-380    62-66
    + LATEKEEP (rebuild keeps age-0 pairs)     0        133      52
    + DRAWADOPT (draw asks pr_key)             0        114      72
    ship line, for scale                       0         19      49

  LATESTEAL0: the late claim never fails once it may take a pair whose
  owner left THIS snapshot (the census's one slack pair) — build_maps'
  own demand rule. LATEKEEP: the chunked rebuild completing after a
  claim was measured at 0 of 266 draws, so this is belt, not fix; kept
  (harmless, principled). DRAWADOPT: the draw consults the ownership
  truth (pr_key, uncached) when the per-parity map says none — it
  removed the 32% of ramp draws whose pair sat in the other parity's
  map. RESIDUAL 114: sets with NO owner at draw time although every
  claim succeeded — the slave is still composing frame N when the
  harvest refreshes SPR_SNAP (the snap latch yields after 2 skips) and
  reads frame N+1's new records before the master's claim loop reaches
  them. Fix (not built): claim from SPR_LAND BEFORE the copy into
  SPR_SNAP (factor the late-claim block into a function taking the
  list base and the target parity). Fidelity on the four-flag build:
  attract scorecard identical, mdstatic 35/45 as TXTWRAM with the set
  table now matching the ship line's rows; f3000 renders clean.
  Rom for Mike's pass: rom/s16_txtwram_opt1.32x (TXTWRAM LATESTEAL0
  LATEKEEP DRAWADOPT). Flags SLVPAIR / SPRMDFREE exist, measured,
  not in that rom.

FIX OPTIONS (Mike's call — this is palette-accuracy design):
  1. Budget: let the late claim re-run build_maps' `bound` logic (take a
     tile-group pair when sprite demand exceeds the sprite reserve), or
     raise the sprite reserve at the static-class table.
  2. Demand: SPRMDFREE=1 (built, measured, small but positive), plus
     keeping the MD sprite path's record count up at 60 Hz (why it
     drops from 2.1 to ~1 per snapshot is unmeasured).
  3. Fallback: an over-budget set should share a REAL pair (nearest by
     colour, or the last claimed) instead of pair 15 — wrong hue beats a
     red silhouette or a missing actor.
  4. Latency: build the map from the CURRENT snapshot before the launch
     when the cycle has slack (costs window lines; measure).

## 7. ITEM 2 (torn landings) — Mike's second pass, and what headless can and cannot see (2026-09-07 ~21:30)

Mike's pass on rom/s16_txtwram_opt1.32x: "exact same diagnosis: lost
invisible frames" (frames 807/808/813). Read from his capture: the
player's sprite holds one x for 6-18 captured frames while everything
else updates, then jumps (790-807 at x=415, 808 at 591, ...); 1229 of
1314 capture frames unique. That is a FROZEN SPRITE LIST with a live
background — the harvest's "stale beats torn" path (SPR_SNAP kept when
a landing tears; regs and tiles ride other paths). His counters: torn
landings 134 of 2344 cycles (5.7%), echoes 138, re-marks 138.
Also new and mine: two phantom health orbs at bottom right = the
player-2 bar re-planted from the WRAM mirror by the TXTWRAM copy
(the game's FB-path clear never touches the mirror). FIXED: the
health-bar writer's footprint now selects P1 or P2 by the byte the
writer tests (0xFFF109); rom/s16_txtwram_opt1.32x rebuilt with it.

HEADLESS (fork, recompiler), beam-stamped DREQ + COMM traces:
    script / build         windows  posts  FIFO drops  BAD1 (tears)
    play_level1 ctrl        2595    2595      0            0
    play_level1 opt1        2479    2479      0            0
    play_wolf   ctrl        5270    5270      0            0
    play_wolf   opt1        5254    5254      0            0
  Every window's push is fully read by the DMAC, pushes span 16-18
  lines with no gap over 2 lines, no window ends before the next
  starts. DIAG[17]'s 94 -> 152 was NO-PUSH vints (deferred windows:
  opt1 posts 121 fewer over 2600 vints — the SH-2 window overrunning
  the vint, each one a dropped frame), not tears.
  So ares-headless cannot reproduce the tear at all, on the light or
  the heavy script; Mike's GUI session tears 5.7%. The GUI/headless
  difference is the next measurement (interpreter vs recompiler,
  version), not another shim change.

  Interpreter A/B: the fork's `--interpreter` path does not boot this
  rom (illegal-instruction spam, 0 cycles) — no data. Mike's GUI is
  either /Applications/ares.app (upstream v148, 2026-05-30) or the
  fork's desktop-ui (upstream base 2026-08-21, same 32X core: zero
  m32x/sh2 commits between v148 and that base). So it is not the
  version: the tear needs the GUI's runtime (his inputs, real-time
  pacing, audio) — the next measurement is GUI-side: a state saved
  right AFTER a freeze, read for the DRQR residue and the belt, and
  which ares app he launches.

  GUI vs headless, narrowed (2026-09-07 ~21:45): the FORK's desktop-ui
  also tears (80 of 1083 cycles, 7.4%), so it is not the app version.
  Same firmware (mia's built-in Mega 32X boot ROMs) in both frontends;
  the MD core runs with entropy None in both; the GUI sets no option
  headless does not (Recompiler, TMSS). One hard difference in Mike's
  state: the harvest's bounded drain-wait census DIAG[43] is ZERO in
  the GUI (headless 200-500 ticks per run) — the GUI's tears never
  enter the near-miss path: landed reads < 26 or >= 924, or the tag
  check fails. Instrumented: DRQR[0..4] (free under R60) now classify
  the landed value at every tear ([0] <26, [1] 26..923, [2] >=924, [3]
  last landed, [4] last tag). rom/s16_txtwram_opt1.32x carries it;
  read Mike's next state with a DRQR dump (SDRAM 0x28F80, u32 BE).

  THE TEAR, NAMED (Mike's census state, 2026-09-07 ~21:55): DRQR
  [0]=0 [1]=88 [2]=9 of 97 tears; last landed 248 against tag 116. The
  landing is LONGER than the packet: the 68K pushed into a DMA that was
  never re-armed for that vint, so the words continued the PREVIOUS
  transfer (TCR residual, DAR past the old packet) and the tail sat
  132 words off. The path: the V-ISR finds the window still live at
  vblank and bails "stale" BEFORE consuming the 68K's announce; the
  window acks a few lines later; the 68K's gate sees COMM0 clear,
  announces, posts and pushes — nobody armed. Headless never sees it
  because its overrunning windows overrun by 100+ lines (the 68K
  defers), the GUI's by a few. FIX (ARMGATE=1, both CPUs): the master
  consumes a pending announce and arms AT THE ACK (window closed,
  nothing in flight, before the post can happen); the 68K pushes only
  after the 0xA001 arm echo (bounded; no echo = no packet this vint,
  counted at 0xFFB0CE — a stale frame instead of a torn landing plus a
  belt storm). Built into rom/s16_txtwram_opt1.32x with option 1.

## 8. ITEM 2 CLOSED: the tear is an UNARMED PUSH, fixed by ARMGATE (2026-09-07 ~22:30)

CORRECTION to entry 7: the COMM trace indexes registers by WORD (COMM8
= column 4, COMM4 = column 2); my headless tear counts read column 8
and found nothing. Recounted, headless tears on every build:
    level1 ship 95 / 3996   level1 opt1 137 / 3879
    wolf   ship 173 / 5270  wolf   opt1 172 / 5254   (2.4-3.5%)
The GUI-vs-headless hunt of entry 7 was chasing a filter bug; the
firmware/entropy/version facts stand but were never the question.

MECHANISM (from Mike's census state + the beam-stamped traces): when a
window overruns the vint, the V-ISR finds COMM0 live at line 0, bails
"stale" and never consumes the 68K's announce; the window acks at
line ~20; the 68K (out of its belt) announces at ~23 and posts at
~58 — nobody armed the DMA. The push continues the previous
transfer: DAR past the old packet, TCR residual, landed > tag
(Mike: 248 vs 116, 88 of 97 tears), tail displaced, harvest tears,
belt re-marks, next pushes swell, more tears: the frozen sprite list.

FIX LADDER (ARMGATE=1, wolf script, 5400 frames; tears = BAD1 echoes):
    step                                                   tears  skipped
    opt1 baseline                                            172      0
    arm at the ACK + 68K waits for the echo                  138      3
    + 68K clears COMM4 at the ANNOUNCE (a stale echo had
      passed for a fresh one; the pre-post clear wiped the
      real one -> 100% skips on the first cut)               101      3
    + arm at the ANNOUNCE from the master's idle poll loop
      (the ack-site arm serviced announces mid-window: every
      remaining tear was one of those), echo wait 1.5 lines      1    130
The 130 skips are announces the body could not service in time (it
was not in its idle loop: 97 echoes 40-400 lines late) — those vints
ship no packet, a stale frame, instead of a torn landing plus a belt
storm. Next refinement: service the announce inside the master's
busy waits too, or let the ISR's stale bail arm when the previous
landing is already harvested.
Level1 speed on this build 75.9%, misses 24.1%, ramp draws 247
(pair claims: 46 misses / 14 failures on this run). Rom:
rom/s16_txtwram_opt1.32x = TXTWRAM LATESTEAL0 LATEKEEP DRAWADOPT
ARMGATE (+ the P2 health-bar fix, + the tear census in DRQR[0..4]).

  MIKE'S PASS on the ARMGATE rom (debug build, ~22:45): torn landings
  0, misaligned 0, incomplete 0 over 1734 cycles at 1.06 vints/cycle
  (his two earlier sessions: 134 and 80 tears). Capture: 1640 of 1793
  frames unique, 54.9 updates/s; the player-x holds are 1-5 frames
  where he walks (the earlier 6-18-frame freezes are gone from the
  histogram's walking stretches). Eye verdict pending.

## 9. THE FREEZE, NAMED: at one cycle per vint the FRAMEBUFFER FLIPS 17% OF VINTS (2026-09-07 ~23:15)

Mike's eye after ARMGATE: tears gone (0 torn), "sprites still freeze
constantly" (frames 1401-1454). Read: the zombie moves every 2 frames,
the player's x holds 10-15 frames then jumps; frame 1432 shows the
player drawn (kick pose) — it is a HOLD, not a vanish. Headless
reproduces it (per-frame screenshot bursts, level1 3000-3120):
player-x holds 10-15 frames on the ARMGATE build, 2 frames on the ship
line. The MD plane scrolls every vint (regs ride the 68K consume), so
the capture looks alive while the 32X layer stands still.

COUNTED from the COMM traces (COMM4 echoes: F102 = flipped, F1FF =
declined):
    build / script        posts   flips   declined   flips per post
    ship  level1           3996    1639     3810        41%  (= one per game frame at 30 Hz)
    opt1  level1           3879     651     6065        17%
    ARMGATE wolf           5242     917     8237        17%
The 32X frame changes only at a flip: at one game frame per vint the
display refreshes every ~6 vints = ~10 Hz. The decline is the K2FREE
EDGE GUARD (m_main.c ~5531): a flip must land within 1650 FRT ticks
(~36 lines) of the vblank ISR entry or ares latches it mid-scan (the
visible tear), so the body echoes F1FF and drops the frame. The ISR
itself flips only when the post lands inside its wait: 13-20% of
cycles on the 60 Hz builds (the post moved from ~20 to ~27 lines: the
TXTWRAM copy + bigger per-vint consumes), 40% on the ship line. Both
CPUs are idle enough (slave busy 17% on both builds; generation
overrun DIAG[30] ~55% on both) — this is PROTOCOL, not load: the
pre-flip span (text capture, cap_drain ALL, restore) plus the post
latency exceed the 38-line vblank once every vint is a new frame.

This is entry 5's "window length" lever, now with its real face: the
flip has to reach vblank every vint. Levers, in order of cost:
  a. the post earlier: the 68K's pre-post path (consumes + the TXTWRAM
     copy) — move the text footprints into the packet (item 3) and
     the consumes after the post where FM allows;
  b. the pre-flip span shorter: cap_drain(13) drains ALL dirty pages
     before the flip ("correctness") — bound it per vint and carry
     the rest (the pages restore anyway); the slave already captures;
  c. the flip protocol itself: flip FIRST at the vint (the previous
     generation is closed by then on 55% of cycles), restore after —
     the hardware defers an out-of-vblank flip to the next vblank
     without tearing; ares' immediate latch is the reason for the
     guard, so this needs the FPGA/hardware answer before trusting it.
Mike's decision: which of a/b/c, or accept a clean 30 Hz display cap
(PACE30 exists: launch every 2 vints) while the 68K runs at 60.

## 10. FIRST BOOT ON REAL 32X HARDWARE (MiSTer FPGA) — blocker isolated to the 32X FB layer (2026-09-08 early)

Mike put the port on a MiSTer running the S32X_MiSTer FPGA core (source
now in srcref/S32X_MiSTer). It did not boot; backrooms (32x-builder)
does. A backdrop/CRAM colour-beacon bisection (dozens of one-flag probe
roms, `BOOT_*` in the Makefile; the 68K paints CRAM 0 through the MD
palette which FM never gates, the SH-2 paints 32X CRAM) walked the whole
boot and frame pipeline. EVERYTHING below now works on real hardware,
proven one probe at a time:

  - Boot: BIOS -> 68K cart code (blue at _start), all 68K init stages,
    read of the game image through bank 3 of the 0x900000 window (>2 MB
    cart, a 4 MB ROM), the master's whole SDRAM init to the 0x600D post.
  - THE SLAVE NEEDED A WARM-UP ares never did: jumping into SDRAM at
    _s_main hung (blue, cache on or off). Running an SDRAM-resident STUB
    first (copied to 0x3FA00, called, returned) unwedged it and the
    slave then sustained SDRAM execution and reached s_main. This is a
    REAL fix to fold into every build (mars_start.s), not a probe. The
    fork/ares boots without it; hardware does not.
  - DREQ: packets land, and the landed word 20 (display word) reads
    0x6000 on BOTH the MiSTer and ares — byte-identical. Transport is
    correct on hardware. The tear census (entry 7-8) is an ares-and-
    hardware reality, not emulator-only.
  - Flips latch (a boot fliptest showed the 2nd-drawn bank), 68K FB
    writes land, MD-plane VDP DMA from the FB works, the FRT tick rate
    is the calibrated ~12k/vint (not 4x), the master's V-ISR fires,
    render windows run to the ack, compose/blit report content present.
  - The MD TEXT PLANE displays: forcing the 32X mode on and freezing the
    pipeline showed "INSERT COIN" / the SEGA title (MD text RAM), on a
    coloured backdrop.

THE BLOCKER: the 32X FRAMEBUFFER LAYER never reaches the screen while
the pipeline runs. Ruled OUT as the cause: the display gate and the
bitmap mode register — `BOOT_GATEOFF` (gate disabled, SH-2 forces mode
256 every window, never blanks) is STILL BLACK, and `BOOT_MDMODE` (68K
asserts the mode every vint) is STILL BLACK. So it is not disp_gate and
not a lost mode write. With mode forced and the pipeline FROZEN after a
flip, only the MD text plane shows; the composed 32X background does
not. Prime suspect (unproven): the blit's target-bank parity vs the
displayed bank under the every-vint flip — an ares/hardware flip-timing
difference, the SAME question the 60 Hz flip work (entry 9) reached from
the speed side. A no-flip "is the FB black" probe was UNSOUND (catches a
mid-compose bank; also a CRAM bit-order slip) — the next probe must read
the DISPLAYED bank right after a real flip, blit-parity-aware.

Roms parked (all BOOT_* probes, none shippable): s16_beacon*,
s16_shstage*, s16_fliptest, s16_gateoff, s16_mdmode, s16_word*, etc.
The ship line and s16_txtwram_opt1 are untouched by this arc.

### 10b. RECOVERED FROM THE SESSION TRANSCRIPT (2026-09-08 ~03:10)

Mike kept the session transcript (/tmp/loglog.txt, 1076 lines) and it
carries the probe ladder that entry 10's summary compressed away. Entry
10 preserved the LIST of capabilities proven; it dropped the SEQUENCE,
and the sequence contains the sharpest fact of the night. Recorded here
verbatim in substance, because it changes the suspect order.

THE PROBE LADDER, in order, with the MiSTer's answer:

    s16_beacon_halt   blue          BIOS hands us the 68K
    s16_beacon        black         dies between first instruction and M_OK
    s16_beacon_stage  blue          ALL 68K init stages pass
    s16_shstage       (ladder)      slave never entered our start code
    s16_pktchk        GREEN         windows complete AND packets land whole
    s16_isrchk        GREEN         the master's V-interrupt FIRES on the FPGA
    s16_fliptest      GREEN BAR     FB writes, both banks, and the flip all work
    s16_frtchk        GREEN         FRT rate is the calibrated ~12k/vint
    s16_flipchk       GREEN         the master's ISR DID flip in-game
    s16_content       GREEN         composed frame has pixels AND plane has tiles
    s16_gatechk2      GREEN         both display switches say ON
    s16_ramp          black         grey ramp forced into both palettes: still black
    s16_modechk       (grey)        ambiguous; the 68K-paints-verdict method hit its limit
    s16_68kdraw       RED BAR       <-- THE MILESTONE. See below.
    s16_gateint       RED           master never sees the game's display-on flag
    s16_words         0x6000        landed word 20 identical MiSTer vs ares
    s16_gateoff       black         gate fully disabled: STILL BLACK
    s16_mdmode        black         68K asserts mode every vint: STILL BLACK

**THE MILESTONE ENTRY 10 LOST: `rom/s16_68kdraw.32x` PUT A FRAME ON THE
SCREEN.** Mike's words: "pulses last color held is black. THEN we see a
68K frame!!!" That rom has the 68K itself write a bar into the
framebuffer at rows 8-15, colour it by the 32X mode register it read,
force the 32X display on, and request a flip FROM THE 68K SIDE. The bar
came up **RED** — meaning the mode register read 0, the display gate had
the 32X output switched off — and the moment the 68K forced mode 1 the
title's text layer appeared behind it.

So the 32X framebuffer layer DID reach the screen on this core, once,
when the 68K wrote it and flipped it. Entry 10's blocker sentence ("the
32X FRAMEBUFFER LAYER never reaches the screen") is too strong: it never
reaches the screen WHEN THE SH-2 PIPELINE DRIVES IT.

**AND THAT SITS IN DIRECT CONTRADICTION WITH `s16_gateoff` AND
`s16_mdmode`, BOTH BLACK.** Forcing the mode on through the gate path is
black; the 68K forcing the mode on while ALSO writing the FB and
flipping is visible. The discriminator is therefore NOT the mode
register and NOT the gate — entry 10 got that right — but it is also not
merely "which bank is displayed". It is WHO writes and WHO flips. That
is a narrower target than entry 10's parked suspect and nobody has
tested it.

TWO MORE RECOVERED FACTS:

  - `s16_gateint` came back RED: the master never sees the game's
    display-on flag, while the 68K reads it set and landings count whole.
  - `s16_words` then showed landed word 20 = 0x6000 on BOTH machines.
    NOTE 0x6000 HAS NO BIT 15. If bit 15 were the display-on flag, ares
    would not see it either — yet ares works. So either the flag is not
    bit 15 of word 20, or the gateint probe read the wrong thing. The
    session concluded "transport is not the fault" from the byte-identity
    and moved on; the gateint RED was never reconciled. **This is an
    open loose end, not a closed one.**

  - Mike also spotted, and the session confirmed, that ROWS 0-7 ARE OFF
    HIS DISPLAY. Every in-game bar drawn there was invisible by
    construction. Any probe that draws a marker must use rows 8+.

  - ROM Storage (Auto/SDRAM/DDR3) was tested by Mike on the beacon rom:
    identical every way. Genuinely ruled out.

WHY IT WAS LOST: the auto-commit hook never fired for this session's
source edits (last commit touching mars_start.s is 09-07 02:33 — the
whole arc left five docs-only commits), .claude/prompt.log logged exactly
one line for the night, and entry 10 was written as an end-of-session
summary rather than as the work happened. Nothing deleted it; nothing
ever captured it.

## 11. MiSTer boot — CLOSED by Mike (2026-09-08)

Mike called the loop closed. State at close, for a clean restart if ever
re-opened (memory: mister-boot-closed):
  - `rom/s16_words.32x` BOOTS THE SLAVE on hardware: stripes render at
    vint 120 in all three ROM-storage modes (Auto/SDRAM/DDR3), landed
    word 20 = 0x6000, ares-identical. So the known-good bisection probe
    still boots.
  - The permanent slave warm-up attempts do NOT: `rom/s16_bootfix` /
    `rom/s16_bootbeacon` (heartbeat SDRAM stub + cache off, folded into
    mars_start.s) leave the slave hung (beacon RED), both storage modes.
    A register-only countdown prime also failed. So the warm-up as
    written is NOT the fix; the real fix is a DIFF between the words
    build and the bootfix build, not a new hunt.
  - ROM Storage (SDRAM vs DDR3 vs Auto) RULED OUT: identical every way.
  - The mars_start.s warm-up code is left in (permanent, non-probe) but
    unproven; a future session may revert/rework it.
Do not chase this again unless Mike re-opens it. The 32X-native 60 Hz
work (entries 1-9) and the ship line are untouched by this arc.

## NEGATIVE RESULTS
- Entry 11: the permanent slave warm-up (heartbeat stub + cache off, mars_start.s) does NOT fix the MiSTer slave hang, though the older gated stub in s16_words did boot the slave; ROM Storage mode is irrelevant. CLOSED — diff the two builds if re-opened, do not re-hunt.
- Entry 10: on the MiSTer FPGA the display gate and 32X mode register are NOT the black-screen blocker (BOOT_GATEOFF/BOOT_MDMODE still black); it is the 32X framebuffer layer reaching the displayed bank. The one-flag colour-beacon method bottoms out at whole-bank questions — the next probe needs blit-parity-aware readback of the displayed bank.
- The slave SH-2 hangs jumping into SDRAM on hardware unless an SDRAM stub runs first; ares never needed it (entry 10) — fold the warm-up into mars_start.s.
- Entry 9: the 60 Hz freeze is not the slave (17% busy), not the pair map, not the tear: the framebuffer flips 17% of vints because the edge guard declines flips that miss vblank.
- Entry 8: "headless never tears" (entry 7) was a trace-column error (COMM index is per word). Headless tears 2.4-3.5% on every build.
- Arming at the ACK site (ARMGATE v1) is wrong: it services an announce whose post already happened; arm at the announce, from the idle loop.
- Entry 7: ares-headless (fork, recompiler) produces zero torn landings on either build on the light or the heavy script; the fork's interpreter does not boot the rom; the 32X core is identical between v148 and the fork. Mike's 5.7% tears are a GUI-runtime difference, unreproduced.
- Entry 6 option 1: the chunked map rebuild never completes after the late claim (0 of 266) — LATEKEEP is belt; pair 15 stays off the late claim's list (2026-08-27 verdict stands).
- Entry 6: uncached slave reads of the pair map (SLVPAIR) change nothing; the ramp draws are FAILED late claims (capacity), not cache staleness, not the parity map, not torn landings.
- Speed as an A/B metric for SH-2-window changes: builds differing by census code alone span 49-74%; use the counters (entry 6).
- TXTWRAM on ares (entry 5): the first 60 Hz session draws sprite sets in the shadow ramp 13x more often (SPRLATE[3] 19 -> 247) and tears 2x more landings; the flag is right, the SH-2 side is not 60 Hz-ready.
- The shim's FB text copy before the post costs ISR flips (40% -> 21%); text must ride the packet, not the FB (entry 5).
- CREDWRAM alone (credit writer off the FB): 0 speed points — the spin moved to the health-bar writer 25 lines later in the pass (entry 4). A top-of-pass FB writer must be removed as a FAMILY.
- Pricing pass positions from a spinning build: the "#23 at +116" number was the spin's end, not the writer's position (entry 4).

- MAME cannot run the dynamic write census on the ship line: the 68K parks in the shim window wait (FM 0x8080) after ~500 frames, and its taps do not see 32X framebuffer-window writes at all (entry 1).
- ares' 68000 has no per-access bus cost to measure: every access is the same fixed clocks, so a "write tax" cannot exist in its model and the hook's clock column reads 0 (entry 3). Spins are priced from the 68000 instruction table.
- The write-tax hypothesis itself: ~90 game writes per frame in steady play, thunk entries ~2/frame — under 1 line. The tax is the FM-gate SPIN, not writes (entry 3).

## 12. DEFERRED FLIP: BUILT, MEASURED, BLOCKED BY FM (2026-09-08 ~02:35)

Built `FLIPDEFER=1` (`-DFLIP_DEFER`, m_main.c): the K2FREE edge guard
arms `flip_deferred` instead of dropping the frame, and the next
vblank's V-ISR commits the flip at its top — the software form of what
`srcref/S32X_MiSTer rtl/32X/VDP.sv` does in silicon (`FS <= FBCR.FS`
only when `VBLK`).

MEASURED, same rig, same window, same input script (2600 vints,
gameplay_speed level 1), both roms from THIS tree so the only variable
is the flag:

    opt1 control (FLIPDEFER off)   52.6%   misses 47.4%
    FLIPDEFER=1                     3.4%   misses  5.9%

3.4% = the game is not advancing. Two attempts:

1. First cut returned from the ISR after the commit. Theory: that skips
   the k1 announce servicing (COMM6 0xB101 -> dreq_rearm + 0xA001 echo),
   which ARMGATE made load-bearing — no echo, no push. Fixed it to fall
   through and arm/echo as usual, skipping only the second flip.
2. Result IDENTICAL to the digit (89 game-frames, 153 misses). Not the
   announce.

**ROOT CAUSE, and it kills the design as specified: flip_span() REQUIRES
FM=1 and there is no FM=1 at the top of vblank.** Its own header says so
(m_main.c ~5389: "Requires FM=1 and V inside vblank at call — the body's
V-gate or the 68K's own entry gate (it raises FM before posting) provide
that at the two sites") and ~5765 says "seen post guarantees FM=1". At
the ISR top no post has been seen, so FM=0, and FM=0 FB reads return
garbage (FM_TEST: 1507 mismatch vs 176 match, m_main.c ~3696). The
deferred commit therefore captures garbage into truth and restore_pages
writes it into the fresh bank. The game wedges on its own read-backs.
The patch is correct; the site is impossible.

NEGATIVE RESULT: **do not re-try "commit the flip at the top of vblank"
without first solving FM.** The handoff's framing — "replace the decline
with a deferred latch" — is right about the hardware and silent about
this constraint.

THE REAL SPLIT, for whoever takes it next: the FBCTL write itself is a
register poke and needs no FB access, so it alone is safe at FM=0. What
is NOT safe there is the rest of flip_span's tail — the pre-flip capture
and the post-flip restore_pages, both FB traffic. So the question to
answer BEFORE building attempt 3 is:

  (a) do FM=0 FB WRITES land? FM_TEST already has the counters for it
      ([0] FM=0 writes that landed, [1] attempts, [2] the FM=1 control,
      m_main.c ~4435) but the convicted result is about READS. If writes
      land, restore_pages can run at the deferred vblank.
  (b) truth freshness: capturing last vint misses the game's writes
      during the intervening vint. Those live in the bank that the
      deferred flip is about to put ON SCREEN, and the new draw bank
      would not have them — the staging skew the k2 comments call the
      bank disease. A deferred flip needs an answer for that gap, not
      just a place to put the write.

Option (a) is one probe build away and it is the cheap half. Nothing
here touches the ship line: FLIPDEFER defaults off, and the flag is
documented in the Makefile above the MISTERBOOT block.

## 13. MiSTer RE-OPENED by Mike: the words-vs-bootfix diff, DONE (2026-09-08 ~02:50)

Entry 11 parked the restart as "diff the two builds". Done, on the
BINARIES — the auto-commit hook did not capture that session's
mars_start.s edits (last commit touching it is 09-07 02:33), so the two
intermediate source states are gone from git and only the roms remain.

Method: locate the slave cache-control write (`e1 11 20 10` / `e1 10 20
10`) in each rom, extract a 4-BYTE-ALIGNED window around it, disassemble
raw. (A misaligned window decodes the PC-relative literal loads wrong
and invents register-load bugs — the first pass did exactly that.)

    s16_words.32x   slave boot @ 0x43556
    s16_bootfix.32x slave boot @ 0x4335a

**THE HANDOFF'S PREMISE IS WRONG. Both roms carry the SAME permanent
warm-up stub**, not "old gated stub vs permanent". Both: cache OFF
(`mov #16`), copy 12 words to 0x0603FA00, r4=COMM6 0x20004026,
r6=COMM14 0x2000402E, r7=0xB007, `jsr`, then `jmp` to _s_main. Identical
instruction for instruction; only the link address of _s_main differs
(0x06036964 vs 0x060368FC), which is expected between flag sets.

**THE ONLY DIFFERENCE IN THE SLAVE BOOT PATH: s16_words carries
BOOT_SHSTAGE and s16_bootfix does not.** The words build writes three
stage markers to COMM10 (0x2000402A) — `mov #1/#2/#3,r5; mov.w r5,@r4` —
interleaved through the boot, including one immediately before the stub
copy. bootfix writes none.

So the boots/hangs difference is not the stub and not the cache setting.
It is either (a) the extra COMM writes acting as bus settling before the
SDRAM jump, or (b) something else BOOT_SHSTAGE drags in (it also installs
the magenta exception handler, so a slave exception HALTS visibly under
words and runs off into the weeds without it — i.e. words may not be
"booting" so much as "failing visibly and differently").

ONE-VARIABLE PAIR BUILT for the next hardware session:

    rom/s16_fpga_stage.32x     MISTERBOOT=1 BOOTSHSTAGE=1
    rom/s16_fpga_nostage.32x   MISTERBOOT=1

Same tree, same flags, one bit apart. If stage boots the slave and
nostage does not, (a) is confirmed and the fix is a deliberate settling
delay, not a stub. If BOTH boot, the warm-up is fine and entry 11's
failure was something else in that build. If NEITHER boots, the warm-up
is genuinely not the fix and the stage writes were incidental.

NOTE: the slave warm-up and slave-cache-off are now behind `MISTERBOOT=1`
(default OFF, ship line clean). Any FPGA rom must be built with it.

## 14. A/B WRITER PROBE built for the FPGA (2026-09-08 ~03:15)

From entry 10b: the 68K's FB write reached the screen on the MiSTer
(s16_68kdraw), every SH-2-driven frame did not, and gateoff/mdmode were
both black. The untested axis is WHO WRITES. `rom/s16_abdraw.32x`
(`BOOTABDRAW=1 MISTERBOOT=1 BOOTSHSTAGE=1`) asks the master's half
directly: MAGENTA bar into FB **rows 8-15**, written as the LAST FB
write of the window with FM still the master's, so compose cannot paint
over it. Fixed CRAM index 1, not bank-coded — this asks "does the
master's write show", not "which bank shows"; the bank question needs a
sound readback and this is not it.

**THIS IS BOOT_FBBAR WITH ONE THING FIXED.** That probe drew at rows
0-7, and rows 0-7 are OFF MIKE'S DISPLAY — established the same night,
AFTER fbbar had already been read as "no bar". Its result was
uninterpretable, not negative. Same write, eight rows down.

ARES REFERENCE (rom verified before hand-off, unlike the fbpix/no-flip
probes): magenta bar across the top, game running underneath.

READ KEY on the MiSTer, one screenshot a few seconds into the game:
  - **magenta bar, black below** -> the master's FB writes DO reach the
    display. The black is then the composed CONTENT, not the write path
    or the bank, and the hunt moves to compose/blit output on this core.
  - **no bar at all** -> the master's FB writes do NOT reach the display
    while the 68K's do. That is the split named, and it is a bus/FM/
    ownership question, not a bank-parity one.
  - **bar AND game** -> it works on hardware and something else changed.

NEGATIVE RESULT, same session: the 68K half of this probe (paint GREEN
into rows 16-23 at vint top, FM=0, after forcing the 32X mode on) BLACKS
THE SCREEN ON ARES where the game otherwise runs. Bisected with
BOOTABDRAWM / BOOTABDRAWK: master half alone = bar over a live game;
68K half alone = black. CUT before hand-off rather than shipped — an
unsound probe costs a hardware round trip and teaches nothing, which is
how s16_fbpix and the no-flip probes were wasted. The 68K positive
control already exists and already passed on hardware: s16_68kdraw. What
that write actually lands on is unknown and is its own question.

## 15. FPGA: NO BAR. The master's in-game FB write never reaches the
## display (2026-09-08 ~03:25)

`rom/s16_abdraw.32x` on the MiSTer: **no bar at all**, with the game's
pipeline running behind it. So the master's magenta bar — written as the
last FB write of the window, FM still the master's, at rows 8-15 which
are ON screen — never appears.

THE THREE-WAY CONTRAST, which is the whole finding:

    s16_fliptest   master writes FB at BOOT, flips        -> GREEN BAR SHOWS
    s16_68kdraw    68K writes FB IN-GAME, flips           -> BAR SHOWS
    s16_abdraw     master writes FB IN-GAME, pipeline runs -> NO BAR

The master CAN write the framebuffer and have it displayed — at boot.
The 68K CAN, in-game. The master in-game cannot. It is not a general
"the 32X FB layer never reaches the screen" (entry 10's wording), and it
is not the display gate or the mode register (both ruled out).

**CORRECTION TO THE READ KEY I GAVE MIKE.** I wrote that a no-bar result
means "a bus/FM/ownership question, not bank parity". That was too
strong and I withdraw it. No-bar is equally consistent with:

    A. the master's in-game FB write does not land at all; or
    B. it lands in memory the display never shows — i.e. the master
       always draws into a bank that never becomes the displayed one.

B is exactly the bank/flip question, and it is NOT ruled out: the probe
that was built to answer it (BOOT_FBBAR, two-bank colour-coded) drew at
rows 0-7, off Mike's display, so it has never actually been answered.
s16_flipchk's green says our own counter recorded a flip; it does not
say the DISPLAYED bank changed.

NEXT PROBE, built and ares-verified: `rom/s16_abread.32x`
(`BOOTABREAD=1 MISTERBOOT=1 BOOTSHSTAGE=1`). The master still paints its
bar; the 68K then reads those framebuffer bytes itself at FM=0 and
paints the verdict on the **Mega Drive backdrop** — the one reporter FM
cannot gate and that shows even with the 32X output off, which is the
lesson every failed one-colour 32X verdict in this arc taught.

    GREEN backdrop -> the 68K FINDS the master's bar in the FB. The
      write landed. Cause A is dead and the fault is on the display
      side: the bank the master draws into is never shown. That makes
      it the same flip question as arc A, from the hardware side.
    RED backdrop -> the 68K does NOT find it. Either the write is lost
      in-game, or master and 68K address different banks; combined with
      abdraw's no-bar, the master's writes are landing where neither the
      68K nor the display can see them.

Honest limit, stated on the probe itself: this cannot separate "lost"
from "a bank the 68K cannot see". It kills one of the two causes
cleanly, which is what a probe should do.

ARES REFERENCE (verified before hand-off): green backdrop, magenta bar,
game running.

## 16. FPGA: GREEN — the master's write LANDS. And abdraw was flawed.
## (2026-09-08 ~03:30)

`rom/s16_abread.32x` on the MiSTer: **GREEN backdrop**. The 68K reads
the master's magenta bar straight out of the framebuffer at FM=0 and
finds it. **The master's in-game FB write lands.** Cause A (the write is
lost on this core) is DEAD. The fault is on the display side.

**AND A FLAW IN ENTRY 15's PROBE, which I have to withdraw.**
`s16_abdraw.32x` was built WITHOUT forcing the 32X display mode. But
`s16_68kdraw` reported its bar RED — meaning the mode register read 0,
i.e. the display gate had the 32X output OFF in-game — and 68kdraw only
showed its bar BECAUSE it forced mode 1 itself. So abdraw's "no bar" is
fully explained by the 32X output being off, and says NOTHING about
whether the master's write displays. Entry 15's three-way contrast is
not sound as stated; the 68kdraw/abdraw comparison had two variables.

`rom/s16_abdraw_on.32x` fixes it: master's bar PLUS `BOOTGATEOFF`, which
disables the gate and forces mode 256 every window. One variable now.
  - **bar visible** -> the master's in-game FB writes reach the display
    once the 32X output is actually on. The black is then the display
    GATE holding the output off, which is our policy code and bounded by
    design — and gateoff being "black" earlier only ever meant "no
    composed content visible", never "no master write visible".
  - **no bar, mode provably forced on** -> the master's writes land in
    memory (abread proved it) but the 32X layer still does not reach the
    display. That is a core-level question and the RTL mux is next.

ARES REFERENCE (verified): magenta bar, game running.

WHAT srcref/32X240pTestSuite RULED OUT (Mike's pointer, read tonight):
  - **Per-bank line table.** The suite's Hw32xInit flips, writes the
    line table, clears, flips, writes it again, clears — both banks get
    a line table. `sh_src/mars.c:38-95` does exactly the same, plus the
    overscan belt for entries 224-255. Not our bug.
  - **32X layer priority.** The suite passes MARS_VDP_PRIO_32X (0x0080)
    into every DISPMODE write. We set it too: `m_main.c:4488` builds
    `base = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X` and
    every gate write is `base | mode`. (`mars.c:40` omits it at boot,
    but m_main's first mode write replaces that.) Not our bug — which
    matters, because "MD plane has priority, 32X layer is behind it"
    would have explained the MD-text-only symptom exactly.
  - Address arithmetic confirmed against the suite: MODE_256 is 320
    bytes = 160 words per line, line table entry i = i*160 + 0x100 word
    offset, so row 8 pixels start at byte 0x200 + 8*320. Our probe
    addresses are right.

## 17. FPGA: abdraw_on = NO BAR with the mode forced on (2026-09-08 ~03:33)

`rom/s16_abdraw_on.32x` (master's bar + BOOTGATEOFF, gate disabled and
mode 256 forced every window): **no bar, screen held on green** (green =
a BOOT_SHSTAGE milestone colour, not a verdict).

So, on hardware, with all three now established:
  - the master's in-game FB write LANDS (abread green, entry 16);
  - the 32X output mode is FORCED ON (gateoff);
  - the bar still does not appear.

REMAINING VARIABLE: which bank. The pre-flip bar goes into the bank
being drawn; if the display never comes to show that bank, it is
invisible however correct the write is. `s16_flipchk`'s green only ever
said OUR COUNTER recorded a flip — our readback loop saw FS change in
the register. It never said the DISPLAYED bank followed.

`rom/s16_abboth.32x` (`BOOTABBOTH=1 BOOTGATEOFF=1 MISTERBOOT=1`) removes
that variable: the bar is written TWICE per vint, once before the flip
and once immediately after the latch wait, so BOTH banks carry it. This
is the boot-time fliptest — which WORKED on hardware — moved into the
live pipeline.
  - bar visible -> bank/flip was the whole thing, and it is the same
    flip question as arc A.
  - still no bar -> the master's in-game FB writes do not reach the
    display in ANY bank, with the write proven to land and the mode
    proven on. Banks and flips both eliminated; the RTL video mux is
    next and it is a short read.

ARES REFERENCE (verified): magenta bar, game running.

MIKE, MID-ROUND: "I WAS WRONG! THE GENESIS IS WRITING FRAMES!" —
unresolved at time of writing which reading that is (the earlier no-bar
report being withdrawn, or the observation that what reaches the screen
on hardware is the MD plane). Asked; do not build on it until answered.

## 18. THE BLACK SCREEN IS THE 32X PALETTE: CRAM WRITES ARE DROPPED
## OUTSIDE HBLANK/VBLANK ON HARDWARE (2026-09-08 ~03:40)

Mike's MiSTer screenshot of `s16_abdraw_on.32x`
(screenshots/20260908_073324-s16_abdraw_on.png) shows **the level 1
graveyard — pillars, tombstones, the wolf statue — in TWO COLOURS over
black: magenta and a pale green.** Magenta is 0x7C1F, green is 0x03E0:
those are CRAM entries **1 and 2**, the only two entries the abdraw
probe writes, and it rewrites them EVERY WINDOW.

So the 32X framebuffer layer reaches the display on this core, and it
always did. The composed frame was there all along. **Everything drew
with an EMPTY 32X palette — black on black — and the two entries our
probe hammered 60 times a second are the only colour on screen.**

(And the "no bar" reading was the probe defeating itself: CRAM 1 =
magenta made every index-1 pixel magenta, so a magenta bar at rows 8-15
sat invisible on a magenta sky. Withdraw entry 17's no-bar reasoning
entirely.)

**THE RTL SAYS WHY.** srcref/S32X_MiSTer/rtl/32X/VDP.sv:

    line 170:  else if (!PAL_CS_N && (...) && ACK_N && PEN) begin
    line 403:  if (H_CNT == 9'h157+3-1 || VBLK || !MODE[0]) PEN <= 1;
    line 405:  else if (H_CNT == 9'h017-1)                  PEN <= 0;

Palette access requires **PEN**, and PEN is asserted only during VBLANK,
during HBLANK (H_CNT 0x159..0x016), or when the display mode is off.
During active scan PEN is 0 and **the palette write is silently
dropped**. It does not error, it does not stall — it vanishes.

Our `cram_paint` (m_main.c:2770) has NO such gate. It writes whenever it
is called, and it is called from inside the render window, which the
frame timeline puts at roughly lines 20-190 — active display. On
hardware nearly every palette write we make is thrown away. ares accepts
them all, which is why this never showed until silicon.

This also explains the whole shape of the arc: MD text visible (MD CRAM
is the 68K's and unaffected), 32X layer "black" (empty palette),
gate/mode/flips/banks/transport all innocent and all correctly cleared
one by one.

**THE FIX**, and there is a proven idiom for it in
srcref/32X240pTestSuite: `pri_vbi_handler` (src/hw_32x.c:99) applies the
whole 256-entry palette **inside the master's V-blank handler**, after
checking FM. Two candidate shapes:
  a. move the palette apply into vblank, the suite's way; or
  b. gate each CRAM burst on PEN — status register 0x2000410A, and per
     the RTL's `4'hA: DO <= {VBLK,HBLK,PEN,11'h000,FEN,FS}` that is
     **bit 13** (bit 15 VBLK, bit 14 HBLK, bit 1 FEN, bit 0 FS) — and
     write only while it is set.
(a) is proven on hardware by a shipping program and is the one to build
first. (b) is what the port's own comments always claimed it did.

NOTE on Mike's "VERY VERY VERY VERY SLOW": expected for THIS ROM, not a
signal about the fix. BOOTGATEOFF forces the mode register every window
and the probe rewrites two CRAM entries every window on top of the
normal pipeline. Do not read a cadence number off a probe build.

## 19. PALVBL BUILT: the palette apply moved into vblank (2026-09-08 ~03:40)

`PALVBL=1` (`-DPAL_VBLANK`). `cram_set` no longer writes CRAM: it updates
the existing `cram_mirror` and marks the entry in a 256-bit dirty map;
`cram_flush_vbl()` drains every dirty entry immediately after the flip
write in flip_span — which the K2FREE edge guard already pins INSIDE
vblank, so PEN is asserted and hardware accepts the stores. FM is still
the master's there, the same precondition the capture and bar writes use.
This is the shape srcref/32X240pTestSuite ships (pri_vbi_handler applies
the whole palette in the master's V-blank handler).

ARES, 2600 vints, level-1 script:
  - renders correctly, full colour, no visible change from the control
  - speed 49.7%, misses 50.3% — IDENTICAL to the ship-line control
    measured earlier tonight (49.7% / 50.3%). The deferral costs nothing
    on the emulator; it only changes WHEN the stores happen.

Roms:
  rom/s16_palvbl.32x       PALVBL=1 MISTERBOOT=1 BOOTSHSTAGE=1  <- MiSTer
  rom/s16_palvbl_ares.32x  PALVBL=1                             <- ares ref

READ ON THE MiSTer: if the 32X layer comes up in the GAME'S OWN COLOURS
instead of black (or instead of the two-colour magenta/green of the
abdraw probe), entry 18's diagnosis is confirmed and the black-screen
blocker is closed.

CAVEAT worth stating before the run: this flushes at the FLIP. Vints that
DECLINE a flip (the edge guard, ~83% of them at 60 Hz per entry 9, fewer
on the 30 Hz ship line) do not flush, so palette updates on those vints
wait for the next landed flip. If hardware shows correct-but-laggy
colour, that is this, and the answer is a second flush point inside
vblank that does not depend on the flip landing.

## 20. PALVBL on the MiSTer: BLACK — because the rom had no way to be
## seen. My build error, not the fix (2026-09-08 ~03:45)

`rom/s16_palvbl.32x` and `rom/s16_palvbl_ares.32x` both came back a
1034-byte pure-black PNG on the MiSTer.

**THE ROMS COULD NOT HAVE SHOWN ANYTHING.** Neither carried
`BOOTGATEOFF`. `s16_68kdraw` established that the display gate leaves the
32X output in mode 0 in-game on this core, and the ONLY hardware runs
that ever showed 32X pixels — 68kdraw, abdraw_on — were the ones that
forced the mode on. A palette fix cannot be seen through a display that
is switched off. Black was the guaranteed result whatever the palette
did. I shipped an unobservable experiment.

(`s16_palvbl_ares.32x` additionally has no MISTERBOOT, so its slave never
gets the warm-up either — doubly black, and it was never meant for
hardware.)

`rom/s16_palvbl_on.32x` = `PALVBL=1 BOOTGATEOFF=1 MISTERBOOT=1
BOOTSHSTAGE=1`. Same conditions that produced the two-colour graveyard
from abdraw_on, so the comparison is now one variable:

    s16_abdraw_on   gate off, NO palette fix  -> graveyard in 2 colours
    s16_palvbl_on   gate off, palette fix     -> ?

  - **full colour** -> entry 18 confirmed, black-screen blocker closed.
  - **still two colours / black** -> the vblank flush is not reaching
    CRAM either, and the next question is whether the flip (and so the
    flush) lands at all on this core, which is arc A's question again.

ARES REFERENCE (verified): full colour, game running, HUD and sprites
correct.

STILL OPEN, and now clearly the NEXT blocker after the palette: the
display gate holds the 32X output OFF in-game on hardware. Every
picture we have seen on the MiSTer needed BOOTGATEOFF to force it on.
`s16_gatechk2` came back GREEN (both switches say display ON) while
`s16_68kdraw` read the mode register as 0 — those two have never been
reconciled and one of them is measuring the wrong thing.

## 21. THE TWO MiSTer SCREENSHOTS ARE BYTE-IDENTICAL. Nothing read off
## them since entry 18 is trustworthy (2026-09-08 ~03:47)

    screenshots/20260908_073324-s16_abdraw_on.png   md5 72f2caf6...
    screenshots/20260908_074530-s16_palvbl_on.png   md5 72f2caf6...

Same md5. Different roms (md5 920b052c vs 8340858d), and the palvbl_on
rom does NOT contain the abdraw CRAM writes — one fewer 0x7C1F in the
image. A live game cannot produce two pixel-identical frames from two
different builds. So ONE of these is true and I do not know which:

  a. the MiSTer was still running the earlier code when the second shot
     was taken, and the "palvbl result" is a picture of abdraw_on; or
  b. both roms genuinely freeze at the same deterministic frame, in
     which case the magenta/green CANNOT be our probe's CRAM 1/2 (that
     code is not in palvbl_on) and **entry 18's diagnosis is unfounded**
     — the two colours would be coming from somewhere I have not
     identified, and the palette story would be built on a coincidence.

Entry 18 is therefore NOT established. It is a hypothesis with a
plausible mechanism (the RTL's PEN gate is real and our cram_paint is
genuinely unguarded — that part stands on the source and the RTL, not on
the screenshot) but the evidence I claimed for it does not hold.

`rom/s16_palvbl_tag.32x` settles (a) vs (b) before anything else is
read: `BOOTTAGBLUE=1` has the 68K paint the MEGA DRIVE backdrop BLUE
every vint. MD CRAM — neither FM nor the 32X display gate can touch it.
  - **blue border/backdrop present** -> this rom is running, the frames
    really are identical, and (b) holds: entry 18 is wrong and the
    magenta has another source.
  - **no blue, same magenta picture** -> (a) holds: the machine was
    running stale code and every hardware reading since the abdraw_on
    shot describes that older rom.

ARES REFERENCE (verified): blue border around the full-colour game.

Nothing further should be concluded, built on, or written into the
handoff until this comes back.

## 22. THE TAG SETTLES IT: stale code, and PALVBL made it WORSE
## (2026-09-08 ~03:55)

`rom/s16_palvbl_tag.32x` on the MiSTer: **FULL BLUE SCREEN**, repeatedly,
four identical captures. Blue is the BOOT_TAGBLUE MD backdrop, so:

1. **The tag rom IS running.** Entry 21's fork resolves to **(a)**: the
   earlier `s16_palvbl_on` capture was STALE CODE — a picture of
   abdraw_on. Every reading taken between the abdraw_on shot and now
   describes the older rom, and my "palvbl still shows two colours"
   reasoning was about a rom that was not running.
2. **Entry 18's evidence therefore stands.** The magenta/green graveyard
   WAS abdraw_on's CRAM 1/2, hammered every window — the only two
   entries with colour. The palette diagnosis is back on its feet.
3. **And PALVBL made it strictly worse: nothing but the MD backdrop.**
   No 32X layer at all, not even the two colours.

WHY PALVBL FAILED, and it is a design error I should have caught when I
wrote the caveat in entry 19: the flush rides the FLIP, because the flip
is the only point in our frame that is both inside vblank AND FM=1. On
hardware the flip rarely lands (arc A's blocker, from the other side), so
the flush almost never ran and CRAM stayed empty — hence a screen that is
purely MD backdrop. The old unguarded code at least wrote constantly
mid-window and got lucky whenever a store happened to fall in an hblank;
that luck is exactly why only the hammered entries had colour.

The test suite's idiom (apply the whole palette in the master's VBI
handler) assumes THE MASTER OWNS FM OUTRIGHT. It does; we do not — our
68K holds FM for most of the frame. Copying the shape without that
precondition is what broke it.

**PALPEN=1 is the fix that fits our architecture.** HBLANK comes round
every scanline and PEN is asserted through it, and the render window
already holds FM — so PEN is the only thing left to wait for. cram_set
spins for PEN (bit 13 of MARS_VDP_FBCTL; reg 0xA returns
{VBLK,HBLK,PEN,11'h0,FEN,FS}, VDP.sv:165) before each store, bounded to
~1200 FRT ticks (about a scanline) so it can never wedge the pipeline on
a machine that does not report PEN.

ARES: renders correctly, full colour. Speed **49.6%** vs the ship-line
control's 49.7% — the PEN spin costs nothing measurable there, though
ares may simply always report PEN; the hardware cost is unknown and is
the thing to watch.

Roms:
  rom/s16_palpen.32x       PALPEN + BOOTGATEOFF + MISTERBOOT +
                           BOOTSHSTAGE + BOOTTAGBLUE   <- MiSTer
  rom/s16_palpen_ares.32x  PALPEN alone                <- ares ref

READ: blue border proves the rom is live. Inside it —
  - **full colour game** -> the palette was the blocker and it is fixed.
  - **two colours / black** -> PEN is not the gate, or our stores still
    miss it, and the hbank timing needs measuring rather than assuming.

## 23. PALPEN v1 WAS TOO SLOW ON HARDWARE: uniform green = an EMPTY
## framebuffer, not a palette (2026-09-08 ~04:00)

`rom/s16_palpen.32x` on the MiSTer: **uniform green**, with a one-frame
BLUE flash when Mike pressed Y to coin up.

Reading it: green is BOOT_SHSTAGE's stage colour sitting in 32X CRAM 0,
and it fills the screen because **the framebuffer is all index 0** — no
composed content at all. The 32X layer is ON (gateoff) and covering the
MD backdrop; the blue flash on the coin press is our BOOT_TAGBLUE MD
backdrop showing through for the one frame the 32X blanks. So the rom is
live and the layer works; there is simply nothing in the framebuffer.

**CAUSE: my per-entry PEN spin starved the render window.** v1 waited up
to ~1200 FRT ticks (about a scanline) BEFORE EVERY CHANGED ENTRY. On a
scene cut hundreds of entries change, so that is hundreds of scanlines
of spinning inside a window that has ~170 lines total. Compose and blit
never ran. ares never showed it because ares reports PEN asserted, so
the spin never spun — a perfect emulator-lenient blind spot, the same
class as the original bug.

**v2, built: BURST + BUDGET.** cram_set only marks a dirty bit;
`cram_flush_pen()` runs once per window (FM still ours, before the flip,
so it does NOT depend on the flip landing — which is what killed
PAL_VBLANK). It waits for PEN once, then writes entries back to back for
as long as PEN holds, and re-waits only when it drops. A hard ~3000-tick
per-window budget stops it dead and leaves the rest dirty for the next
window: the palette may lag a frame, the pipeline may never stall.
DIAG[19] counts stores, DIAG[20] counts budget cut-offs.

ARES: renders correctly, full colour. Speed 49.0% vs the ship line's
49.7% — 0.7 points, and ares does not exercise the wait at all, so the
hardware cost is still unmeasured. DIAG[20] on a hardware run is the
number to read if colour comes back but lags.

Roms:
  rom/s16_palpen2.32x       PALPEN v2 + GATEOFF + MISTERBOOT +
                            BOOTSHSTAGE + TAGBLUE   <- MiSTer
  rom/s16_palpen2_ares.32x  PALPEN v2 alone         <- ares ref

READ: blue border = live rom. Inside it, full colour = the palette was
the blocker and it is fixed. Still flat green = the FB is still empty and
the flush is still eating the window, and the budget needs cutting hard.

## 24. PALPEN: three self-inflicted bugs, found by counters not screens
## (2026-09-08 ~04:15)

Mike's 04:06 MiSTer capture of `s16_palpen` (v1) shows the graveyard
geometry in pale green ON BLACK — so the framebuffer HAS content and a
few palette entries have colour, most are black. Partial, not fixed.

(Also: 20260908_080613-s16_palpen.png is md5 72f2caf6 — BYTE-IDENTICAL to
the abdraw_on capture again. The MiSTer screenshot path has now produced
a stale frame twice. Treat any single capture as suspect unless a live
marker like BOOT_TAGBLUE is in it.)

Then I stopped reading screens and read counters. Three bugs, all mine:

1. **The wait was 26 scanlines, not one.** The FRT runs ~12000 ticks per
   vint over 262 lines = **~46 ticks a scanline**. v1/v2 waited up to
   1200 ticks per entry — 26 lines each, hundreds of them on a scene
   cut, inside a ~170-line window. That is what emptied the framebuffer
   and gave Mike the flat green. hblank is ~17% of a line, so 60 ticks
   is enough to catch one. I should have done this arithmetic before
   writing the first version, not after the third.
2. **"ares never exercises the wait" (entry 23) was wrong.** DIAG says
   ares models PEN and defers constantly. That claim would have kept
   this loop bouncing off hardware for every iteration; it is testable
   locally after all.
3. **Both flush call sites landed at the same place.** My second patch
   anchored to the same line as the first, so both calls sat six lines
   apart AFTER the flip and the in-window drain was never built. Every
   PALPEN measurement before this one was PAL_VBLANK again with extra
   steps — i.e. the thing I said PALPEN existed to avoid.

Fixed: one drain at the END of the render window (FM ours, flip-
independent, beside the BOOT_FBBAR site) and one after the flip.

ARES, 2600 vints:
    stores 2080 (was 1377), budget cuts 25148, PEN-low give-ups 57
    speed 50.5% vs the ship line's 49.7%
Stores up 51% with the drain in the right place, and no speed cost.
25148 budget cuts still says most flushes end early — the palette lags,
it does not fail. DIAG[20] is the number to watch on hardware.

Roms:
  rom/s16_palpen5.32x       PALPEN + GATEOFF + MISTERBOOT + BOOTSHSTAGE
                            + TAGBLUE   <- MiSTer
  rom/s16_palpen5_ares.32x  PALPEN alone

Ship rom rebuilt clean, _end = 0x060135c8, stamp c79131d3+.

## 25. THE MAGENTA IS NOT OURS: CRAM residue test (2026-09-08 ~04:20)

`s16_palpen5` on the MiSTer (after coin + start; flat green before that,
i.e. an empty FB until the game runs) shows the graveyard with a MAGENTA
sky and PALE GREEN stones — and its capture is md5 b84d0c08, NOT the
72f2caf6 of the abdraw shot, so this is a genuinely new frame, not the
stale-capture problem again.

**But s16_palpen5 contains no CRAM 1/2 writes at all.** The magenta and
green cannot be coming from this rom. Two readings:

  a. the MiSTer core KEEPS 32X CRAM across rom loads, and we are looking
     at RESIDUE from the abdraw runs — meaning nothing this rom writes
     reaches CRAM, which strengthens the palette diagnosis considerably;
  b. those two colours have a source I have never identified, in which
     case entry 18's evidence (that they were our hammered CRAM 1/2)
     was a coincidence and the diagnosis rests on the RTL alone.

`rom/s16_paltest.32x` decides it with DIFFERENT COLOURS: hammer CRAM 1 =
YELLOW (0x03FF) and CRAM 2 = RED (0x001F) every window, direct stores
that bypass cram_set and PALPEN entirely, exactly the way abdraw wrote.
  - **yellow/red appears** -> hammered writes DO land, the magenta was
    residue, CRAM is otherwise empty, (a) holds.
  - **still magenta/green** -> nothing of ours reaches CRAM even when
    hammered, the abdraw picture was never ours, (b) holds and entry 18
    needs rebuilding from the RTL alone.

ARES REFERENCE (verified): normal full-colour game inside the blue tag
border, with the HUD text yellow/red instead of blue — the hammered
entries are visibly in use, so a colour change on hardware is readable.

NOTE for whoever picks this up: capture hygiene on this rig is poor. Two
of the MiSTer screenshots this session were byte-identical to an earlier
rom's frame. Every probe from here on should carry BOOTTAGBLUE or an
equivalent live marker so a stale frame is self-evident.

## 26. PALTEST: still magenta/green. My probes cannot separate the
## hypotheses, and I am stopping the rom cycle (2026-09-08 ~04:25)

`rom/s16_paltest.32x` hammers CRAM 1 = YELLOW and CRAM 2 = RED every
window, direct stores at the same site abdraw used. The MiSTer still
shows MAGENTA sky and PALE GREEN stones. Not yellow. Not red.

So the hammered write did NOT take — yet the picture is in exactly the
two colours abdraw hammered. Both cannot be true of the same code path
unless something else is in play, and I have run out of probes that can
tell these apart:

  - the colours are residue in CRAM that new writes cannot overwrite;
  - the colours are not CRAM at all and I have mis-modelled the display;
  - the capture is stale again (the blue tag CANNOT be seen in a 320x224
    MiSTer capture — the MD backdrop only shows where the 32X layer does
    not cover, and here it covers everything, so this session's freshness
    marker does not work for full-screen 32X content).

WHAT IS SOLID, and does not depend on any screenshot:
  1. **PEN gates 32X CRAM writes** to hblank/vblank/display-off
     (VDP.sv:170 with :403/:405). **Our cram_paint has no such guard.**
     That is a real defect in our code, read off the RTL and our source.
  2. **The FRT is ~46 ticks per scanline** (12000/vint over 262 lines).
     Every timing constant in this area should be checked against that.
  3. **ares DOES model PEN** — DIAG counters show constant deferrals. So
     this class of bug is testable locally and does NOT need a hardware
     round trip per iteration. Entry 23's claim to the contrary is dead.
  4. **PALPEN v5 costs nothing on ares**: 50.5% vs the ship line's 49.7%,
     stores up 51% with the drain in the right place.
  5. **The capture rig produces stale frames** (two byte-identical to an
     earlier rom's) and the blue-border marker does not survive
     full-screen 32X content. Any future hardware probe needs a marker
     INSIDE the 32X layer, not behind it.

THE NEXT PROBE, unbuilt, and it is the right one: **have the master READ
CRAM BACK.** Write a known value, read 0x20004200 back, compare against
cram_mirror, and publish the verdict — the same readback shape that made
s16_abread decisive for the framebuffer. That answers "do our palette
writes land on this core" directly instead of inferring it from colours
on a screen, which is what every probe since entry 18 has been doing and
is why they keep failing to separate hypotheses.

I am not sending another rom tonight. Mike has flashed ~10 and three of
tonight's failures were mine (unobservable rom, a wait 26x too long,
two call sites at the same address).

## 27. THE ANSWER, FROM DIFFING THE TWO IMPLEMENTATIONS: a 32X palette
## write STALLS the SH-2, it does not vanish (2026-09-08 ~04:35)

Mike: "you have the full source code for ares. and the fpga." He is
right, and it took thirty seconds of reading to get what fifteen probe
roms could not.

**ares** (ares/md/m32x/io-internal.cpp:353-362, and io-external.cpp
:387-394 identically):

    if(address >= 0x4200 && address <= 0x43ff) {
      if(!vdp.framebufferAccess) return;          // dropped if FM not ours
      while(vdp.paletteEngaged()) {               // STALL until PEN
        if(shm.active()) { shm.internalStep(1); shm.syncAll(true); }
        ...
      }
      ...write...
    }

with (vdp.cpp:114)

    auto M32X::VDP::paletteEngaged() -> bool {
      return !vblank && !hblank && latch.mode.bit(0);
    }

**FPGA** (srcref/S32X_MiSTer/rtl/32X/VDP.sv:170-175):

    end else if (!PAL_CS_N && (...) && ACK_N && PEN) begin
        PAL_ACCESS <= ~PAL_ACCESS;
        if (PAL_ACCESS) begin
            DO <= PAL_IO_Q;
            ACK_N <= 0;            // ACK asserted ONLY here
        end
    end

While PEN is low the RTL never asserts ACK_N, so the SH-2's bus cycle is
not acknowledged and **the CPU is held**. Both implementations AGREE:
outside hblank/vblank a 32X palette write BLOCKS THE SH-2 until the next
hblank. Neither drops it.

**SO ENTRY 18 IS WRONG AND I WITHDRAW IT.** The writes were never
vanishing. Every palette write our master makes during active display
STALLS IT until the next hblank — and we make hundreds per window, from
inside the render window, which is active scan by construction. The
window is consumed by stalls, compose and blit never finish, the
framebuffer stays empty, and the screen shows a flat CRAM 0. **That is
Mike's flat green.** The two-colour graveyard is the partial case: enough
early entries land to colour something, never enough window left to draw.

One root cause, several symptoms — and it plausibly also explains why
the flip rarely lands on hardware (arc A's blocker from the other side):
a window eaten by palette stalls does not reach its flip.

**AND PALPEN IS COUNTERPRODUCTIVE.** I added an explicit software wait
for PEN on top of a bus that already stalls for it. Pure overhead, on
both machines. Do not ship it; it is not a fix, it is a second copy of
the problem. (It is also why v1 starved the window so spectacularly:
1200-tick software wait PLUS the hardware stall.)

Second fact from the same read, worth keeping: **ares DROPS a palette
write when FM is not ours** (`if(!vdp.framebufferAccess) return;`). So
palette stores must happen with FM held — the window does hold it, so
this is not currently a bug, but any "move the palette out of the window"
fix must keep FM.

**WHAT THE FIX HAS TO BE:** put the palette writes where PEN is already
asserted, so no stall can occur — vblank — with FM held, at EVERY vint
and not conditional on the flip landing (which is what made PAL_VBLANK
fail: it hung the drain off the flip). The shape the 240p suite uses
(whole palette in the master's VBI handler) is exactly this; it works
there because the suite's master owns FM outright. Ours must claim FM
for a bounded vblank slot, drain the palette, release. That is a real
design change, not a flag, and it is the next task.

Measured cost of getting this wrong, for scale: this is what fifteen
hardware probe rounds and ~10 of Mike's flashes were spent on.

## 28. THE FIX, BUILT: palette drains at the TOP of flip_span — in
## vblank, with FM, before every decline (2026-09-08 ~04:40)

Three changes, all following from entry 27's implementation diff:

1. **The software PEN wait is GONE.** A palette write outside
   hblank/vblank already stalls the SH-2 on both implementations; waiting
   for PEN in software was a second copy of that stall and is what
   starved the window in v1-v2. cram_flush_pen now CHECKS PEN and SKIPS
   — never spins.
2. **The in-window drain is REMOVED.** The render window is active scan
   by construction, so every CRAM store there stalls until the next
   hblank. Hundreds of them is the window gone and an empty framebuffer:
   Mike's flat green.
3. **The drain moved to the TOP of flip_span**, before the edge guard and
   before the DIRECT_FB / NATIVE_FRAME gates. That point is inside vblank
   (the caller has seen the 68K's post, which guarantees FM; the edge
   guard keeps it inside the 38-line vblank) AND it runs even when the
   flip is DECLINED. PAL_VBLANK drained after the flip write, so a
   declined flip drained nothing — and on hardware most flips decline,
   which is why CRAM stayed empty and the screen fell back to the MD
   backdrop (entry 22). The palette must not depend on the flip landing.

ARES, 2600 vints, level-1 script:

    DIAG[19] CRAM stores        2060   (badly-placed version: 1377)
    DIAG[21] PEN-low skips         0   <-- every drain found PEN asserted
    speed                      49.0%   (ship line 49.7%)

**Zero PEN-low skips is the number that matters.** It says every drain
now happens inside a PEN window, so not one palette store stalls the
SH-2. That is the whole defect, closed, on the emulator.

Renders correctly on ares, full colour.

Rom for the MiSTer: `rom/s16_palvb.32x` = PALPEN + BOOTGATEOFF +
MISTERBOOT + BOOTSHSTAGE + BOOTPALTEST. PALTEST is kept deliberately: it
hammers CRAM 1 yellow / CRAM 2 red, so the screen also answers the entry
25 residue question in the same run.
  - **game in its own colours** -> fixed.
  - **yellow sky / red stones** -> the drain is not landing but hammered
    direct stores do; back to the CRAM readback probe (HANDOFF-PALETTE).
  - **magenta/green again** -> neither lands; the readback probe is
    mandatory before anything else.

## 29. WHICH LAYER IS ACTUALLY ON SCREEN? (2026-09-08 ~04:45)

`s16_palvb` on the MiSTer: STILL magenta sky / green stones. The vblank
drain did not change it, and BOOTPALTEST's hammered direct stores of
yellow and red into 32X CRAM 1/2 did not change it either. Two completely
different ways of writing the 32X palette, no effect.

That convicts the assumption every probe since entry 18 has rested on:
**that the picture is the 32X layer at all.**

**THE SHIP LINE IS MDBGALL.** SHIP_COMMON carries it, so the background
and FG cat-0 are drawn by the MEGA DRIVE VDP out of tiles DMA'd from the
32X framebuffer — and they take **MD CRAM** colours, which the 68K owns
and which no amount of 32X palette work can touch. The graveyard's
temple and tombstones are exactly that class of content. If they are MD
plane pixels, then magenta/green is a wrong MD palette and every 32X
CRAM probe tonight was aimed at the wrong layer.

`rom/s16_mdpal.32x` (`BOOTMDPAL=1` + PALPEN + GATEOFF + MISTERBOOT +
BOOTSHSTAGE): the 68K paints the WHOLE MD palette RED every vint.
  - **picture turns red** -> it is the MD PLANE. The 32X palette arc has
    been aimed at the wrong layer; the real question is why MD CRAM is
    wrong on hardware, and that is the 68K's upload from FB staging
    (0x85F000 -> MD CRAM), not cram_paint.
  - **stays magenta/green** -> it really is the 32X layer, MD CRAM is not
    involved, and the CRAM readback probe in HANDOFF-PALETTE is next.

ARES REFERENCE (verified): background goes fully RED — temple, sky and
stone — while sprites, grass and text keep their own colours. So the
discriminator is sharp: on ares the red covers exactly the MD-plane
content and nothing else.

## 30. RED. IT IS THE MD PLANE — the whole palette arc was aimed at the
## wrong layer (2026-09-08 ~04:50)

`rom/s16_mdpal.32x` on the MiSTer: **the screen goes RED.** The 68K
painting MD CRAM turns the entire picture red.

So, established on hardware:

1. **What we have been looking at all night is the MEGA DRIVE PLANE**,
   coloured by MD CRAM. The graveyard, the magenta sky, the green stones
   — all MD plane, all MD palette.
2. **The magenta/green was a wrong MD CRAM.** It had nothing to do with
   32X CRAM, which is why neither the vblank drain nor hammered direct
   32X stores could move it.
3. **The 32X layer contributes NOTHING on hardware while the pipeline
   runs.** On ares the same rom leaves sprites, grass and text in their
   own colours — those are 32X-composed — and reddens only the MD-plane
   background. On the MiSTer EVERYTHING went red, so there is no 32X
   contribution on screen at all.

**ENTRY 10 WAS RIGHT AND MY ENTRY 16 "CORRECTION" WAS WRONG.** "The 32X
framebuffer LAYER never reaches the screen while the pipeline runs" is
exactly the finding. I withdrew it on the strength of s16_68kdraw's bar;
that bar needs re-explaining, but it does not overturn this.

**THE 32X PALETTE ARC (entries 18, 22, 23, 24, 26, 27, 28) WAS AIMED AT
THE WRONG LAYER** and none of it can have fixed the black screen. What
survives from it, and is worth keeping on its own merits:

  - The PEN stall is REAL and our cram_paint genuinely lacked any guard
    (entry 27's ares-vs-RTL diff). Hundreds of stalling stores per window
    inside active scan is a genuine defect in our code and a genuine
    waste of the window, whatever it does to this bug. PALPEN as it now
    stands (drain at the top of flip_span, in vblank, zero PEN-low skips
    on ares, no measurable speed cost) is a real improvement worth
    keeping — it is just not the black-screen fix.
  - The FRT is ~46 ticks a scanline. Still true, still needed.

**THE ACTUAL QUESTION, restated:** on hardware the MD plane draws (tiles
arrive, geometry is correct) but its palette is wrong, and the 32X layer
is absent. The MD palette reaches MD CRAM by the 68K's upload out of FB
staging (0x85F000), i.e. **a Mega Drive VDP DMA whose source is the 32X
framebuffer**. That transport was flagged as a suspect early in the arc
and never tested ("does the core serve VDP DMA from the framebuffer?").
It now has two symptoms pointing at it: wrong MD palette AND, if the 32X
layer's absence is the same story, a background that only exists because
the MD plane is drawing it.

**NEXT PROBE:** have the 68K upload the MD palette DIRECTLY from a table
in its own RAM, bypassing the FB-sourced DMA entirely.
  - **colours become correct** -> the FB->CRAM DMA is the fault, and that
    is a transport bug on this core with a shippable workaround.
  - **still wrong** -> the palette data itself never reaches FB staging,
    and the hunt moves back up the pipeline to who writes 0x85F000.

## 31. PROBE: bypass the FB-sourced palette DMA (2026-09-08 ~04:52)

Entry 30 put the wrong palette on the MD plane, and the MD background
palette reaches CRAM by **a Mega Drive VDP DMA whose SOURCE IS THE 32X
FRAMEBUFFER** — md_main.c ~750, `src = (sc + 688) >> 1` fed to VDP regs
0x93..0x97 with a CRAM destination. "Does this core serve VDP DMA from
the framebuffer?" has been an untested suspect since the first hours of
the arc.

There is already a second path beside it: when the beam is out of vblank
the same 48 words are copied to WRAM (0xFFA100) and DMA'd from there at
the next vint top (consumed at md_main.c:2054). **That path never uses
the FB as a DMA source.**

`BOOTPALWRAM=1` forces it always (`if (0)` in place of the vblank test).
`rom/s16_palwram.32x` = PALPEN + BOOTPALWRAM + BOOTGATEOFF + MISTERBOOT +
BOOTSHSTAGE.

  - **colours come right on hardware** -> the FB-sourced VDP DMA is the
    fault on this core. That is a transport bug with an immediate,
    shippable workaround: stage every palette through WRAM. It would
    also implicate every OTHER FB-sourced DMA we do, which is how the
    whole MD plane is fed — and that would explain the 32X layer's
    absence as the same story.
  - **still wrong** -> the palette data never reaches FB staging in the
    first place, and the hunt moves up the pipeline to whoever writes
    offset 688.

ARES REFERENCE (verified): renders correctly, full colour, identical to
the control — the WRAM path is already exercised on ares whenever the
beam leaves vblank, so forcing it costs nothing there and proves nothing
there either. This probe only says something on hardware.

## 32. PALWRAM: no change. So it is the DATA, not the transport
## (2026-09-08 ~04:55)

`rom/s16_palwram.32x` on the MiSTer: **still magenta/green.** Forcing the
MD background palette through the WRAM-staged path — which never uses the
32X framebuffer as a DMA source — changed nothing.

That is a real elimination. The two paths read the framebuffer by
COMPLETELY DIFFERENT MEANS: the normal one by Mega Drive VDP DMA with an
FB source address, the forced one by ordinary 68K reads into WRAM
followed by a DMA from WRAM. **Both produce the same wrong colours.** A
transport fault would have to break both, in the same way, which is far
less likely than the simpler answer: **the palette DATA in the packet is
wrong.** "Does this core serve VDP DMA from the framebuffer" is answered
— it is not the fault here.

And the geometry from the SAME packet (name tables, earlier words) is
CORRECT on hardware. So the packet is good early and bad deep, at least
by the time the 68K reads offset 688.

`rom/s16_palpeek.32x` (`BOOTPALPEEK=1`) stops inferring and reads it. The
68K peeks all 48 words the master wrote at offset 688 and paints the MD
backdrop:
  - **RED**    = all 48 words ZERO. The master never wrote the palette
                 block, or the 68K cannot read that region on this core.
  - **YELLOW** = all 48 identical and non-zero: a fill pattern, not a
                 palette.
  - **GREEN**  = varied non-zero, a plausible palette. Then the data is
                 there and the fault is downstream of the read.

ARES REFERENCE (verified): GREEN border, game in full colour — the data
is a plausible palette on the emulator, as expected.

This is the third time tonight that reading a value beat looking at
pixels (s16_abread for the framebuffer, BOOTTAGBLUE for rom identity).
Every probe that inferred from colour has cost a round trip and settled
nothing.

## 33. THIRD STALE CAPTURE, and a reporter that is actually visible
## (2026-09-08 ~04:58)

The palpeek capture came back md5 **72f2caf6** — byte-identical to the
abdraw_on frame for the THIRD time, now under a third rom name. That
frame is not palpeek's output and nothing can be read from it.

Two rig lessons, both learned the hard way tonight:

1. **A verdict on MD CRAM 0 is invisible.** The MD plane covers the
   screen, and the backdrop only shows where nothing is drawn. This is
   the third reporter killed by it (BOOTTAGBLUE's border, the gateint
   backdrop, this).
2. **Painting the verdict at the consume site is useless too** — the real
   48-word palette upload runs immediately after and overwrites entries
   16-63, which is exactly what the plane draws with. ares showed it: the
   flood left only the border green.

FIXED, and verified on ares: the peek STASHES its verdict at 0xFFA168 and
**the flood happens at VINT TOP**, the site BOOTMDPAL proved covers the
whole screen on hardware. ares now shows the entire MD plane in the
verdict colour while 32X-composed content (sprites, grass, text) keeps
its own — unmistakable.

`rom/s16_palpeek3.32x`. Read the FLAT COLOUR filling the screen:
  - **GREEN**  = the 48 palette words at offset 688 are varied non-zero,
                 a plausible palette. Data is there; fault is downstream.
  - **RED**    = all 48 are ZERO. The master never wrote the block, or
                 the 68K cannot read it on this core.
  - **YELLOW** = all 48 identical non-zero: a fill pattern, not a palette.
  - **the graveyard again, unchanged** = ANOTHER STALE CAPTURE. Reload
    and re-shoot; the live rom cannot show that picture.

A flat screen-filling colour is now also the freshness marker: a stale
frame shows the graveyard, a live one cannot.

## 34. PALPEEK: GREEN on hardware — the palette data IS there
## (2026-09-08 ~05:18)

`rom/s16_palpeek3.32x` on the MiSTer: **flat GREEN filling the screen**,
fresh capture (new md5, and the flood itself proves liveness — a stale
frame shows the graveyard, which this does not).

So the 48 palette words the master wrote at packet offset 688 are
**varied and non-zero** on hardware, and the 68K reads them fine. That
kills two more candidates at once:
  - the master DOES write the palette block on this core;
  - the 68K CAN read that region of the framebuffer on this core.

Combined with entry 32 (both upload paths give the same wrong colours),
the data is present, readable, and gets uploaded — and the screen is
still magenta. **So the words themselves are plausible but WRONG.** The
question is no longer "does the palette arrive" but "is the palette the
master builds the right one", which points upstream of the 68K entirely:
the master's source palette arrives over DREQ from the 68K, and partial
DREQ landings are a documented hazard on this port (CLAUDE.md).

`rom/s16_palshow.32x` (`BOOTPALSHOW=1`) shows the VALUE instead of a
verdict: it floods the screen with the first non-zero word of the block.
  - **floods MAGENTA** -> the packet carries the wrong colour, the fault
    is in what the master built, i.e. upstream in the palette it
    received. The 68K side is exonerated completely.
  - **floods the grey-green ares shows** -> the words are right and
    something between reading them and CRAM mangles them.

ARES REFERENCE (verified): the first non-zero word is **0x0686** (MD BGR
R3 G4 B3, a muted grey-green) and the screen floods exactly that.

Note on probe hygiene, learned in the same round: the first cut of this
used sc[688+1] and ares read 0x0000 there — CRAM 17 is legitimately black
on that scene, so it would have flooded nothing and cost a round trip for
no reason. Taking the first NON-ZERO word is robust without knowing which
pen the scene uses. Check a probe's value on ares BEFORE sending it.

## 35. THE PALETTE WORD ITSELF DIFFERS BETWEEN ARES AND HARDWARE
## (2026-09-08 ~05:20)

`rom/s16_palshow.32x` floods the screen with the first non-zero word of
the packet's palette block. Measured, not eyeballed:

    ares    first non-zero word = 0x0686   = MD BGR  R3 G4 B3
    MiSTer  flood colour RGB(206,255,206)  ~ MD BGR  R6 G7 B6   (85% of
            the frame; measured with a colour histogram over the capture)

**It does not flood magenta.** So the packet is NOT carrying the magenta
we have been chasing — that came from some other entry. And it does not
flood ares' value either: **the same word reads differently on the two
machines**, and on hardware each channel is roughly DOUBLE ares'.

Channel-wise doubling is what a one-bit left shift does. Two candidate
mechanisms, and this probe cannot separate them:
  a. the master's colour conversion produces a different value on
     hardware — unlikely, the SH-2 code is identical; or
  b. **the master's SOURCE palette differs**, because the palette it
     receives over DREQ from the 68K lands differently — a one-word
     alignment slip in the landed packet would give values that are
     wrong but entirely plausible, which is exactly the symptom.
     Partial/misaligned DREQ landings are a documented hazard on this
     port (CLAUDE.md, tools/drq_probe.py).

(b) is the strong candidate and it connects to a known open issue rather
than inventing a new one.

**NEXT PROBE, unbuilt:** put a KNOWN PATTERN through the path. Have the
master write a ramp (0x0000, 0x0001, 0x0002 ...) into offset 688 instead
of the palette, have the 68K read it back and flood GREEN on an exact
match, RED on any mismatch, and YELLOW if it matches but SHIFTED by one
or more words. That distinguishes "the transport corrupts" from "the
transport shifts" from "the source is wrong", in one run.

STATE AT THIS POINT — what hardware has now established:
  - the master writes the palette block           (entry 34, GREEN)
  - the 68K can read it                           (entry 34)
  - both upload paths behave identically          (entry 32)
  - the visible picture is the MD plane           (entry 30, RED)
  - the words themselves differ from ares         (this entry)
The fault is upstream of everything the 68K does, in what reaches the
master. That is a much smaller box than the arc started with.

## 36. KNOWN-RAMP PROBE, built (2026-09-08 ~05:26)

`BOOTPALRAMP=1`: the master writes **0x0100+i** into the 48 palette words
(m_main.c ~12490, in place of the quantised colour) and the 68K checks
what actually arrived, flooding the screen with the verdict:

    GREEN (0x00E0)          exact match, all 48 words
    GREEN + red level k     the ramp is present but SHIFTED by k words;
                            k is encoded in the red channel so the shift
                            amount is readable off the screen
    RED   (0x000E)          neither: corrupted, not merely displaced

This separates the three candidates from entry 35 in ONE run:
  - **exact** -> the transport is clean, and the fault is the SOURCE
    palette the master builds from (PAL_SH, delivered over DREQ). The
    hunt moves to what the 68K pushes and what lands in PAL_SH.
  - **shifted** -> a landing alignment slip, the documented partial-DREQ
    hazard, and we get the exact word count off the screen.
  - **corrupt** -> the packet region itself is unreliable on this core,
    which contradicts entry 34's GREEN and would need re-testing.

ARES REFERENCE (verified): verdict word 0x00E0, screen floods GREEN —
exact match, as expected on the machine where the picture is correct.

Rom: `rom/s16_palramp.32x` (PALPEN + BOOTPALRAMP + BOOTGATEOFF +
MISTERBOOT + BOOTSHSTAGE).

BUILD NOTE: the first cut of this patch put the `#endif` closing the
BOOT_PALRAMP `#else` BEFORE the block's closing brace, which broke the
consume function 200 lines away with an "expected identifier before }"
at md_main.c:1077. Fixed by swapping the two lines. Preprocessor edits
into deeply nested C need the brace checked, not assumed.

## 37. RAMP: EXACT on hardware. Transport clean, source palette is the
## fault. And a cadence probe, because we never measured one
## (2026-09-08 ~05:30)

`rom/s16_palramp.32x` on the MiSTer floods **RGB(0,255,0) across 86% of
the frame** — measured with a histogram, not eyeballed. That is MD
nibbles R0 G7 B0 = **0x00E0 exactly**, the GREEN verdict, with NO red
tint, i.e. **no shift**.

So the master's known ramp arrives at the 68K byte-exact on hardware.

**THE PALETTE TRANSPORT IS CLEAN.** Master writes the block, 68K reads
it, values match exactly, both upload paths behave the same. Everything
from the master's pen downward is now proven good on silicon. **The
fault is the SOURCE the master builds from — PAL_SH — which arrives over
DREQ from the 68K.** That is upstream of every probe this arc has run,
and it is the documented partial-DREQ-landing hazard's territory.

Next in that direction: the same known-pattern trick one stage upstream —
68K pushes a known ramp in the palette region of the DREQ packet, master
compares what landed in PAL_SH and reports through the flood reporter
that now works.

### But first, Mike's point, which outranks it

"nothing ever shows actual moving streaming frames." He is right and it
is a real gap: every hardware probe in this arc has been a STATIC verdict
colour, and the only cadence datum on hardware is his "VERY VERY VERY
VERY SLOW". **The frame rate on hardware has never been measured.** If
the vint chain is starved on silicon, every palette and framebuffer
question so far is downstream of a much larger problem and the ordering
of this whole hunt is wrong.

`rom/s16_motion.32x` (`BOOTMOTION=1`) measures it with a wristwatch: the
MD palette steps through 8 colours every 8 vints, so ONE FULL CYCLE IS
64 VINTS = ~1 second at 60 Hz. Nothing to instrument, nothing to
screenshot.
    ~1 cycle/second   -> 60 Hz
    ~1 per 2 seconds  -> 30 Hz, the ship-line cadence
    much slower/stuck -> the vint chain is starved and THAT is the bug

ARES REFERENCE (verified by sampling four vints 8 apart): the colour
steps green -> cyan -> blue -> magenta on schedule, one step per 8 vints.

## 38. CADENCE MEASURED ON HARDWARE: ~60 Hz VINTS. (And my first read
## of it was wrong.) (2026-09-08 ~05:35)

`rom/s16_motion.32x` steps the MD palette through 8 colours every 8
vints; one full cycle is 64 vints, ~1 second at 60 Hz.

**MIKE, WATCHING THE SCREEN: "1 cycle a second."**

So the 68K's vint handler is entered ~64 times a second. **The vint chain
runs at full speed on hardware.** It is not starved.

**MY FIRST READING OF THIS WAS WRONG AND I WITHDRAW IT.** I read the
colours off his captures, ordered them by the MiSTer's filename
timestamps, saw consecutive wheel indices one second apart, and concluded
one STEP per second = 8 Hz. That inference is aliased: the filenames have
one-second resolution, and a wheel cycling at very nearly 1 Hz sampled
once a second produces exactly that slow phase-walk. A person watching
the screen is the correct instrument for a 1 Hz signal; second-resolution
filenames are not. I wrote "the MiSTer runs at 8 Hz, this reframes the
entire arc" and it was an artefact of my own sampling.

WHAT THE CORRECT MEASUREMENT MEANS:

  - The 68K's vint handler runs every vint on hardware. Interrupt
    delivery, the handler, and the MD-side path are all keeping up.
  - So "VERY VERY VERY VERY SLOW" is NOT the vint rate. It is the GAME's
    own frame advance — the scene timer — which is the known threshold
    law (speed = 100 - IRQ4 miss rate, LOOP27 and the frame-threshold-law
    memory). The game's pass is not finishing inside its vint, so it
    misses IRQ4s and advances a fraction as often as the vints arrive.
    On ares that costs us ~50%; on hardware it is evidently far worse.
  - The palette findings stand unchanged: transport clean (entry 37),
    source PAL_SH the suspect.

**THE MEASUREMENT WE STILL DO NOT HAVE** is the game-frame rate on
hardware, as distinct from the vint rate. The same wheel gives it for
free: step the wheel on the GAME's frame counter (WRAM 0xFFF02A, one tick
per game frame) instead of on vints. Cycle time then reads the game rate
directly against a clock, and the ratio against this run is the hardware
miss rate — the number the whole 60 Hz arc is about, never once measured
on silicon.

**WHAT TO DO NEXT, in order:**

1. **Measure the GAME-frame rate on hardware** with the same wheel driven
   off the scene timer (WRAM 0xFFF02A) instead of the vint count. That
   gives the hardware miss rate directly, against a wristwatch. It is the
   number the entire 60 Hz arc rests on and it has never been taken on
   silicon.
2. **Then find where the game's pass overruns on hardware but not on
   ares.** Candidates, in rough order of suspicion:
   a. **68K reads of the 32X framebuffer.** The consume reads hundreds of
      words out of the FB every vint (`sc[...]`, the palette block, the
      cell records). On this core an FB read is arbitrated against the
      SH-2s and the FIFO; ares charges almost nothing for it. This is the
      single biggest volume of hardware-arbitrated traffic we do.
   b. **Cart reads through the 0x900000 banked window** — the game image
      lives in bank 3, above 2 MB, and every game-code fetch goes through
      it.
   c. **The DREQ push** and its FIFO handshake.
3. **Bisect it the way the boot was bisected**: roms that skip one
   consume stage at a time, each read off the wheel. Screenshot-free,
   one number per variant.
4. Only then return to the palette (entry 37: transport clean, source
   PAL_SH suspect).

The wheel is the right instrument and should lead every future hardware
session — but READ IT OFF THE SCREEN WITH A CLOCK, not off capture
timestamps.

## 39. GAME-FRAME WHEEL: the hardware speed number, readable with a
## wristwatch (2026-09-08 ~05:38)

`rom/s16_motiong.32x` (`BOOTMOTIONGAME=1`). Same colour wheel, driven by
the GAME's scene timer (WRAM 0xFFF02A, one tick per game frame) instead
of the vint count. One step per 8 game frames, one full cycle = 64 game
frames.

    ~1 cycle / second   -> 60 game-frames/s   FULL SPEED
    ~2 seconds / cycle  -> 30 Hz              the ship-line cadence
    ~4 seconds / cycle  -> 15 Hz
    ~8 seconds / cycle  ->  7.5 Hz

Entry 38 measured the VINT wheel at 1 cycle/second on the same hardware.
So **the ratio of the two cycle times is the hardware miss rate** — 2s
means we lose half the vints, 4s three quarters. That is the number the
entire 60 Hz arc is built on and it has never been measured on silicon;
every figure in LOOP27 up to here is an ares figure.

ARES: verified the wheel advances off the game timer (sampled at six
vints 200 apart, the colour walks). ares' own level-1 rate is ~50%, so
ares should read ~2 seconds per cycle — which also makes this a
calibration: if Mike's MiSTer reads much slower than 2s, hardware is
losing frames ares does not, and the gap is the thing to hunt.

READ IT WITH A CLOCK, off the screen. Not off capture timestamps — that
is exactly the aliasing that produced the withdrawn 8 Hz claim in entry
38.

## 40. HARDWARE GAME RATE: ~3 FRAMES PER SECOND (2026-09-08 ~05:45)

`rom/s16_motiong.32x` on the MiSTer, colours read by histogram, ordered
by capture timestamp:

    094406 yellow (idx 1)
    094409 yellow (idx 1)   +3s, NO step
    094411 green  (idx 2)   +2s, one step
    094413 cyan   (idx 3)   +2s, one step
    094416 blue   (idx 4)   +3s, one step

**One wheel step every ~2-3 seconds.** A step is 8 game frames, so the
game is advancing at roughly **3 game-frames per second**. A full cycle
(64 game frames) takes ~20 seconds.

THIS SAMPLING IS SOUND, unlike entry 38's. There we sampled once per
second a signal stepping ~8x/second and got an aliased phase-walk. Here
we sample every 2-3 seconds a signal stepping every 2-3 seconds and we
SEE A REPEAT (094406 and 094409 both yellow) — oversampling relative to
the step rate, which is what makes the reading trustworthy. Mike's
wristwatch reading should still be the tie-breaker if it disagrees.

**PUT AGAINST ENTRY 38'S VINT WHEEL (1 cycle/second = ~60 vints/s):**

    vints arriving      ~60 / second
    game frames         ~3  / second
    hardware miss rate  ~95%

On ares the same build runs ~50%. **Hardware is losing roughly ten times
more frames than the emulator.** That is Mike's "VERY VERY VERY VERY
SLOW", quantified, and it is the top of the tree — every palette,
framebuffer and flip question in this arc has been asked about a machine
completing one game frame in twenty vints.

**FIRST BISECT, built: `rom/s16_motiong_noconsume.32x`**
(`MDCONSUMEOFF=1` on top of the game wheel). The 68K's packet consume —
hundreds of reads out of the 32X framebuffer every vint, the single
biggest volume of hardware-arbitrated traffic the port does, and the
thing ares charges almost nothing for — is compiled out.
  - **wheel speeds up sharply** -> the FB consume is the cost. That is a
    concrete, attackable target: fewer/wider reads, or move the data.
  - **wheel unchanged** -> the cost is elsewhere; next suspects are cart
    reads through the 0x900000 bank-3 window and the DREQ push.
The picture will be wrong with the consume off. That does not matter:
read the wheel, nothing else.

## 41. CONSUME-OFF BISECT WAS UNSOUND; a non-perturbing span probe
## instead (2026-09-08 ~05:52)

`rom/s16_motiong_noconsume.32x` (MDCONSUMEOFF) gave an unusable reading:
the wheel stepped roughly as before (~2s) but the sequence went
BACKWARDS in places (blue->cyan, red->darkred) and blacked out twice.
With the consume off the game never receives its packets, so the scene
timer that drives the wheel is not advancing normally — **the probe
changed the game's behaviour, not just its cost.** Same class of error as
the unobservable PALVBL rom: a bisect whose control arm does not run the
thing being measured. Do not re-run it.

`rom/s16_span.32x` (`BOOTSPAN=1`) measures the cost directly and
perturbs nothing. It reads a stamp the shim ALREADY records — the MD
V-counter at consume end, WRAM 0xFFA176 — and floods the palette with
which part of the frame that lands in. Vblank starts at V=0xE0.

    GREEN   V >= 0xE0   consume finished inside vblank   (healthy)
    YELLOW  V <  0x20   ~32 lines into the visible frame
    ORANGE  V <  0x60   ~96 lines in
    RED     otherwise   deep into the picture; the vint is lost

ARES REFERENCE (four samples, 300 vints apart): **GREEN every time** —
the consume finishes inside vblank on the emulator.

So on hardware:
  - **GREEN**  -> the consume is fine and the cost is elsewhere in the
                 handler; next stage stamps are 0xFFB0B2 (after scroll)
                 and 0xFFB0B6 (after cells), same technique.
  - **YELLOW/ORANGE/RED** -> the 68K's framebuffer consume alone is
                 eating the frame on silicon, which names the target: it
                 is hundreds of 68K reads out of the 32X framebuffer,
                 arbitrated against both SH-2s and the FIFO, and ares
                 charges almost nothing for them.

BUILD NOTE: the first cut of this probe accumulated a wrap counter and
latched WHITE permanently — ares read WHITE at all four samples, which is
how it was caught BEFORE it reached hardware. Checking the probe against
ares first is now the only thing standing between a bad idea and another
of Mike's flashes.

## 42. TARGET NAMED: the 68K's FB consume eats the frame on silicon
## (2026-09-08 ~05:57)

`rom/s16_span.32x` on the MiSTer: **RED, all three captures** (100%, 83%,
86% of frame).

    ares    GREEN  — consume finishes inside vblank, every sample
    MiSTer  RED    — consume finishes DEEP IN THE VISIBLE PICTURE

Same code, same stamp (WRAM 0xFFA176, V at consume end), no perturbation
of the game either way. **The 68K's packet consume is eating the frame on
real hardware and costs almost nothing on the emulator.**

That closes the chain of tonight's measurements:

    vints arrive               ~60/s          (entry 38, Mike's watch)
    game frames advance        ~3/s           (entry 40)
    hardware miss rate         ~95%           vs ~50% on ares
    where the time goes        the FB consume (this entry)

**WHY THIS IS SLOW ON SILICON AND NOT ON ARES:** the consume is hundreds
of 68K reads out of the 32X framebuffer every vint. On hardware each one
is arbitrated against both SH-2s and the DREQ FIFO; ares charges
essentially nothing. This is the single biggest volume of
hardware-arbitrated traffic the port does, and it was written and tuned
entirely against the emulator.

**FIX DIRECTION — get the 68K out of the framebuffer.** The port already
proves both halves of this work on hardware:
  - a Mega Drive VDP DMA sourced FROM THE FRAMEBUFFER works (entry 32:
    the palette path used it and the data arrived correctly);
  - the cell records currently go FB -> 68K reads -> WRAM -> DMA, which
    pays the 68K read cost twice over.
So: DMA the cell records straight from the framebuffer to VRAM the way
the palette block already can, and the 68K stops touching the FB at all.
That is a real design change with hardware evidence behind every step,
not a probe.

NEXT MEASUREMENT (one rom): the same bucket on the EARLIER stamps —
0xFFB0B2 (V after the scroll stage) and 0xFFB0B6 (V after the cells) —
to see how much of the span is the cell records specifically. That sizes
the prize before the work is done.

## 43. WHAT WORKING 32X CODE DOES: the 68K NEVER READS THE FRAMEBUFFER
## (2026-09-08 ~06:05, at Mike's insistence: "read any samples you can")

He was right to stop me. Two independent working 32X codebases in
srcref/, and both answer the question the probes were circling.

**1. 32X240pTestSuite — the 68K is a COMMAND SERVER.**
`src_md/crt0.s:255` is the whole 68K main loop: poll COMM0, dispatch on
a command word, do the work, clear COMM0. ~28 commands. Where bulk data
is involved it comes from CART ROM, not the framebuffer:

    handle_planeBitmap:
        move.l  0xA1512C,d1       ; pointer from COMM12
        andi.l  #0x0FFFFF,d1      ; ...masked to a ROM address
        move.l  d1,a0
        bsr     set_rom_bank      ; ...banked in
        jsr     set_planeBitmap   ; ...and read from ROM

`set_ntable` writes ONE pattern name per command, taken from COMM2. The
only 68K access to the framebuffer in the entire suite is
`src_md/diagnostics.c:844`, a SIXTEEN WORD diagnostic that saves and
restores what it touches.

**2. cannonball-outrun-32x — a shipping game engine. Its 68K side is 122
LINES.** `src/m68k/md_main.c` initialises, does the M_OK/S_OK handshake,
then loops reading the controllers into COMM12/14. **It never touches
the framebuffer at all.**

**AND OUR PORT HAS THE 68K READ HUNDREDS OF FRAMEBUFFER WORDS EVERY
VINT** — the packet consume — and drives the MD VDP from what it reads.
No shipping 32X program does this. The platform's own idiom is: the
framebuffer belongs to the SH-2s; the 68K gets a command word and a ROM
pointer.

Entry 42 measured the cost of the divergence on silicon: our consume
finishes DEEP IN THE VISIBLE PICTURE (RED) where ares finishes inside
vblank (GREEN). ~3 game-frames/second against ~60 vints.

**THE FIX, now evidence-backed rather than guessed:**

Get the 68K out of the framebuffer. Every MD-plane record it currently
READS should instead be moved by **VDP DMA sourced from the framebuffer**
— which entry 32 already proved works correctly on this hardware, since
the palette block travels that way. The 68K's per-record cost drops from
~17 framebuffer reads to ~5 VDP register writes, and the reads that
remain are the VDP's, not the 68K's.

The staged path is the worst of both worlds and should go first: it reads
the FB with the 68K into WRAM and THEN DMAs from WRAM, paying the read
cost in full before saving nothing.

SIZE THE PRIZE FIRST (one rom, same technique as entry 41): bucket the
EARLIER stamps — 0xFFB0B2 (V after the scroll stage) and 0xFFB0B6 (V
after the cells) — to see how much of the span is the cell records
specifically. That says whether converting the cells alone gets us there.

## 44. OVERNIGHT LOOP, iteration 1: rig autonomy + a self-correction to
## the span probe (2026-09-08 ~06:22)

**RIG.** Full MiSTer access established and PROVEN:
  - deploy: `scp rom/X.32x root@mister.office.local:/media/fat/games/S32X/probe.32x`
  - launch: `curl -X POST http://mister.office.local:8182/api/games/launch
    -d '{"path":"/media/fat/games/S32X/probe.32x"}'` — verified, /tmp/ACTIVEGAME
    flips and /tmp/remote.log records "game started: Sega32X/probe.32x".
  - **capture: NOT WORKING right now.** `echo screenshot > /dev/MiSTer_cmd`
    returns rc=0 and produces no file; the Remote API's POST /api/screenshots
    answers with every field empty; a filesystem-wide search for any .png newer
    than 10 minutes finds nothing. /dev/fb0 exists (960x540x32) and READS fine
    but carries only the OSD — captured it, 99% black with a little grey, no
    game video. Suspect the display is asleep with Mike. Retry each iteration.
  So: deploy and launch are autonomous, READING THE RESULT is not.

**CHALLENGE TO MY OWN ENTRY 42, and it was a real flaw.** The span probe
bucketed the consume's END POSITION (V at 0xFFA176). That conflates the
consume's own cost with everything that ran before it in the vint — a RED
could mean "the consume is slow" OR "the consume starts late because
something earlier is slow". It cannot distinguish them, and I reported it
as if it named the consume.

Both stamps exist: **0xFFB0B0 is V at consume ENTRY**, 0xFFA176 is V at
consume END. `rom/s16_span2.32x` buckets the DIFFERENCE:

    GREEN  < 8 lines     YELLOW < 24     ORANGE < 64     RED >= 64

ARES (three samples, 400 vints apart): **GREEN every time** — the consume
costs under 8 scanlines on the emulator.

Caveat now recorded on the probe itself: both stamps are written only
inside `if (live[0] == 0xB6B6)` (md_main.c:547), so this measures
CONSUMING vints only. That is the right population for the question, but
it means a stale stamp is possible on a vint with no packet.

Deployed to the MiSTer and launched; waiting on a capture path.

**ENTRY 42 STANDS AS EVIDENCE THAT SOMETHING IS SLOW, NOT THAT THE
CONSUME IS.** Until s16_span2 reads on hardware, "the FB consume eats the
frame" is a hypothesis with suggestive evidence, not a measurement.

## 45. COLOUR CENSUS: hardware has NEVER shown more than 9 colours.
## New hypothesis: DMA-to-CRAM (2026-09-08 ~06:57)

Pulled every screenshot the MiSTer holds (46 files) and counted distinct
colours per frame. This is the first time the hardware output has been
measured rather than looked at.

    MiSTer, best frame ever produced      9 distinct colours
    MiSTer, typical                       1-2 (a flood colour and black)
    ares, SAME flood rom (palpeek3)      76
    ares, no flood (palvbl_on)           92
    ares, the new paldirect rom         119

**Hardware renders with at most 9 colours where ares renders 76+ from
identical code.** Almost every palette entry is black on silicon. That is
not a subtle wrong-value problem, which is how I have been framing it
since entry 18 — it is most of the palette never arriving.

**AND THE CENSUS SPLITS THE PATHS FOR ME:**

  - the probe FLOODS work — 64 CRAM entries written ONE AT A TIME to the
    VDP data port, and they cover the screen every single time. So
    **direct 68K CRAM writes land on this core.**
  - the geometry is correct — temple, tombstones, grass all in the right
    shapes. Those name tables arrive by **DMA to VRAM**, which therefore
    works.
  - the palette upload is the one path that is neither: **a VDP DMA whose
    DESTINATION IS CRAM.**

**HYPOTHESIS: DMA-to-CRAM does not land (or lands truncated) on this
core, while DMA-to-VRAM and direct CRAM writes both do.** It explains
every symptom at once — correct geometry, near-empty palette, working
floods — and it explains why entry 32's WRAM-vs-FB source test changed
nothing: BOTH paths end in a DMA to CRAM, so swapping the SOURCE was
never going to matter. I tested the wrong half of that transfer.

`rom/s16_paldirect.32x` (`BOOTPALDIRECT=1`) is the test AND a candidate
fix: write the 48 palette words with DIRECT stores to the VDP data port
instead of a DMA. 48 word writes is cheap — the flood already does 64
every vint and costs nothing.

ARES: renders correctly, 119 distinct colours, full-colour title scene.
DEPLOYED AND LAUNCHED on the MiSTer (ACTIVEGAME confirms) — it is the rom
currently running, so ONE screenshot answers it.
  - **full colour on hardware** -> DMA-to-CRAM is the bug, and this is
    already the fix.
  - **still ~9 colours** -> the palette data is not reaching the 68K in
    the first place, and entry 34's GREEN verdict needs re-examining
    (it only proved the words are non-zero and varied, not correct).

Capture still not firing (retried twice this iteration).

## 46. THE RTL REFUTES ENTRY 45; and CRAM READBACK IS A DEAD END
## (2026-09-08 ~07:34)

**ENTRY 45'S HYPOTHESIS IS DEAD, killed by the RTL in two minutes.**
srcref/S32X_MiSTer/rtl/GEN/vdp.sv:

    :728  CRAM_WE = ((IO_WE && FIFO_CODE == 4'b0011) ||
                     (DMA_FILL_WE && DMA_FILL_CODE == 4'b0011))
                    && SLOT == ST_EXT && SLOT_CE;
    :765  VRAM_WE = ((IO_WE && FIFO_CODE == 4'b0001) || ... )
                    && SLOT == ST_EXT && SLOT_CE;

**CRAM and VRAM writes go through IDENTICAL gating** — same FIFO, same
external-slot requirement. There is no mechanism by which DMA-to-CRAM
fails while DMA-to-VRAM succeeds. "DMA-to-CRAM does not land on this
core" was a good story that the source refutes. Withdrawn.

(What entry 45's colour census DOES establish stands: hardware has never
shown more than 9 distinct colours against ares' 76-119 on identical
roms. That measurement is solid; only my explanation of it was wrong.)

**CRAM READBACK: DEAD END, one iteration spent.** The plan was to have
the 68K read CRAM back and count matches — "read a value, do not infer
from pixels", the rule that has worked all night. It does not work here,
and ares caught every failure before hardware:

  1. First cut read 0/48 on ares, where the picture is CORRECT. The MD
     CRAM READ command was wrong: CD=0b001000 needs second word 0x0002,
     I wrote 0x0020 which is CD5:2=0 = VRAM READ. It was reading VRAM and
     returning a constant 0x200F.
  2. Fixed encoding -> 5/48. Second flaw: I graded CRAM against THIS
     vint's packet, but CRAM holds the PREVIOUS upload, and under NT_WRAP
     the palette only uploads when it CHANGED. Mismatches were expected
     and meaningless.
  3. Graded against a WRAM shadow of what was actually last sent -> 3/48.
     Still wrong on a machine whose picture is correct.

An instrument that disagrees with a known-good reference is not an
instrument. Something else about MD CRAM reads is not being honoured
(a discarded first word, a blanking requirement like the write slots, or
the read auto-increment). **NOT SHIPPING IT.** Two lessons kept: the
command encoding above, and grade against a shadow of what was sent.

**THE REAL LESSON OF THIS ITERATION** is about method, not the bug:
every probe I have built tonight that INFERRED from a colour has cost a
round trip and settled nothing, and now the first one that tried to read
a value hit a VDP corner that took three attempts to even calibrate. The
cheapest reliable instrument in this whole session has been the ares
headless dump (`--dump wram:...`), which is exact, free, and needs
nobody. Hardware should be asked only questions ares CANNOT answer.

NEXT ITERATION: stop instrument-building. Go to priority 2 — the fix
that stands on entry 43's evidence regardless of which palette theory is
right: get the 68K out of the framebuffer.

## 47. ENTRY 43 IS WRONG ABOUT OUR OWN CODE, and the fix I proposed is
## ALREADY IMPLEMENTED (2026-09-08 ~08:10)

I told Mike, as the closing finding of the night: "our port has the 68K
read hundreds of framebuffer words every vint" and "the fix is to move
MD-plane records to VDP DMA sourced from the framebuffer". **Both halves
are false for the shipping build.** I read the legacy path and reported
it as the shipping one.

The cell-record copy loop I cited — `sp[2 + k] = e[k]`, md_main.c:988 —
is inside the **`#else` of `#if defined(R60) && defined(NT_WRAP)`**. It
is the LEGACY staged consume. SHIP_COMMON carries `R60=1 NTWRAP=1`, so
the shipping build takes the OTHER branch, md_main.c:636 and :655, which
**already DMAs everything straight out of the framebuffer**:

    uint32_t src = ((uint32_t)e) >> 1;        /* FB address */
    ...VDP DMA regs 0x93..0x97...
    *vdp_ctrl_wide = ((0x4000 | (a & 0x3FFF)) << 16) | (... | 0x80);

On the shipping path the 68K reads, per vint:
  - `sc[0..7]`                          ~8 words
  - 2 header words per row x 7 rows      14 words
  - `e[0]` per art-tail record           a few
  - EDGE42's 2 words per row when set
i.e. **roughly 20-40 words, not hundreds.** Everything bulk is DMA'd.

MEASURED on ares (ship rom, existing counters 0xFFA024/0xFFA026 over
1100 vints): **70.8 cells per vint moved BY DMA** — 35.5 plane A, 35.4
plane B. Those are words the 68K never touches.

**SO THE ARCHITECTURE ALREADY FOLLOWS THE PLATFORM IDIOM far more
closely than entry 43 claimed**, and "get the 68K out of the framebuffer"
is not available as a fix because it is already done.

**WHAT THIS DOES TO ENTRY 42.** The hardware span probe (consume ends
deep in the visible picture, RED, where ares ends in vblank, GREEN)
stands as a measurement. But its explanation cannot be bulk 68K reads —
there aren't any. What the consume actually spends its time on is
**VDP DMAs whose SOURCE IS THE 32X FRAMEBUFFER**, roughly 71 cells plus
tile records per vint, split across many short spans each costing six
register writes plus the transfer.

That is a much better-supported suspect, and it is still something no
shipping 32X program does: the 240p suite DMAs bulk data from CART ROM
(entry 43's `handle_planeBitmap`), and Cannonball has no MD plane at all.
An MD VDP DMA reading the 32X framebuffer has to arbitrate against both
SH-2s for every word; ares charges nothing for that arbitration.

**REVISED FIX DIRECTION** (unbuilt, and NOT to be handed over as fact
until measured):
  a. fewer, longer DMAs — coalesce spans so the per-DMA register cost
     and setup is amortised over more words;
  b. or move the MD plane's source out of the 32X framebuffer entirely,
     which is what both reference programs do.
Sizing (a) needs the span-count distribution, which the existing cell
counters do not give; that is one small ares-side census, no hardware.

METHOD NOTE: this correction came from reading our own source properly,
which I should have done before telling Mike what our architecture was.
Entry 43 was written from a grep hit without checking which #ifdef arm
it lived in — the same mistake class as the misplaced #endif in entry 36.

## 48. DMA CENSUS: the volume is SMALL, and ares cannot see the cost
## (2026-09-08 ~08:45)

Entry 47 moved the suspect to the FB-sourced VDP DMAs. Counted them on
ares (ship rom + DMACENSUS, one run, 2600 vints, all counters read
together — differencing two runs gave nonsense first time):

    tile-record DMAs   14731   = 5.7/vint, 16 words each = ~91 words/vint
    span DMAs            301   = 0.12/vint (the NT_WRAP span path barely
                                 runs in this scene; typ==0 dominates)
    cells A+B          55124   (16-bit counters, wrapped — lower bound)

**COUNTER COLLISION, recorded so nobody trusts the other two numbers:**
the 32-bit counters I placed at 0xFFA242 and 0xFFA248 return garbage
(1720717728, and 9463502 against an expected 235696), so those WRAM
addresses are already in use by something else. Only 0xFFA246 (tile DMA
count) is trustworthy — 16-bit, unwrapped, and consistent. WRAM slot
allocation in md_main.c needs a proper audit before any future probe
picks addresses; I picked blind.

**THE HONEST CONCLUSION OF THIS ITERATION: ~5.7 DMAs per vint moving ~91
words is NOT obviously frame-eating**, any more than 20-40 68K header
reads were. Every VOLUME measurement I can take on ares says the
workload is modest. If hardware takes 20x longer at this workload, the
cost is PER-ACCESS ARBITRATION — the 32X framebuffer answering the MD
VDP while both SH-2s contend for it — and **ares models none of it, so
no ares measurement can find it.**

That is the boundary of what this loop can do. I have now proposed three
mechanisms tonight (68K FB reads, DMA-to-CRAM, FB-sourced DMA volume) and
killed two of them with source evidence and weakened the third with a
count. Generating a fourth without hardware would be inventing, not
debugging.

**SO: the loop stops guessing and prepares the hardware session.**
`rom/s16_span2.32x` is deployed and launched as probe.32x — the single
most valuable unread measurement, the consume's DURATION in scanlines
(entry 44), ares GREEN <8 lines. One screenshot answers it.

    GREEN <8 / YELLOW <24 / ORANGE <64 / RED >=64 lines

If that reads RED on hardware, the consume genuinely is the frame, at a
workload ares completes in under 8 lines, and per-access arbitration is
proven by elimination. If it reads GREEN, the consume is innocent and the
time is going somewhere else in the handler entirely — and entry 42's
end-position RED was measuring something that ran BEFORE the consume.

## 49. THE MiSTer MAIN BINARY IS NOT RUNNING — and that invalidates my
## "deploy and launch are autonomous" claim (2026-09-08 ~09:30)

Chasing the capture failure properly instead of accepting it, I listed
the MiSTer's userspace processes:

    1 init, 534 resync, 546 syslogd, 550 klogd, 559 udevd, 599 dbus,
    680 bluetoothd, 694 ntpd, 737 wpa_supplicant, 742 proftpd,
    805 /tmp/remote.sh, 812 gpm, 813/815 agetty

**There is no `MiSTer` process.** The main binary is dead. Consequences,
all of which I had wrong:

  1. **`/dev/MiSTer_cmd` has no reader.** Writes to it go nowhere — and a
     blocking write to a readerless FIFO is exactly why several of my
     ssh commands hung with no output earlier tonight.
  2. **No screenshots are possible.** Not a sleeping display, which is
     what I assumed for four iterations. The component that takes them
     is gone.
  3. **MY LAUNCHES DID NOT NECESSARILY HAPPEN.** `/tmp/ACTIVEGAME`
     changing is **mrext's own bookkeeping** (remote.sh, still running),
     not proof that the core loaded the rom. The FPGA holds whatever core
     was configured when main was last alive.

**SO WITHDRAW THIS, WHICH I TOLD MIKE TWICE:** "deploy and launch are
autonomous, verified". Deploy (scp) is real. **Launch is not verified**,
and `rom/s16_span2.32x` and `rom/s16_paldirect.32x` are probably NOT what
his machine is running. What it IS running is most likely `s16_span`, the
rom he loaded by hand at 09:56 — the last launch that happened while main
was demonstrably alive.

TIMELINE from /tmp/remote.log:
    08:22:32  core started: S32X          (main alive)
    09:21-09:57  Mike's own rom loads and screenshots (main alive)
    10:13:26  game started: Sega32X/probe.32x   <- my first launch, logged
    (nothing after; my later launches produced no log line)

So main was alive at 10:13 and died somewhere after. **I cannot rule out
that I killed it**: my first attempts wrote malformed `load_core` paths
and an `.mgl` with a bad relative path into the command FIFO, repeatedly,
in the minutes before. That is the honest reading and Mike should have it.

**NOT RESTARTING IT.** Bringing MiSTer main back would change what is on
his screen while he sleeps, and the current state may itself be worth him
seeing. It is one command on his side when he wakes.

**METHOD FAILURE WORTH KEEPING:** I verified the launch by reading a file
that a DIFFERENT process writes, and called that verification. The whole
session's discipline has been "read a value, not a colour" — this was the
same error in a new place: reading a value that does not mean what I
assumed. The correct check would have been the screenshot filename, which
derives from the loaded rom, or the remote.log line — and remote.log was
sitting there the whole time showing my later launches were absent.

## 50. CONSOLIDATED: docs/handoff/HANDOFF-HARDWARE.md (2026-09-08 ~10:10)

The night produced more corrections than conclusions — five withdrawn
claims across entries 27, 43, 44, 45, 46, 47, 49 — and LOOP27 now reads
as a sequence of reversals that would take an hour to reconstruct. Wrote
`docs/handoff/HANDOFF-HARDWARE.md`: what is actually known on hardware
(8 items), what I withdrew and must not be rebuilt on (5 items), the open
question stated precisely, the one probe worth running first, and the rig
notes earned the hard way. It supersedes the state sections of
HANDOFF-MISTER.md and HANDOFF-PALETTE.md, both of which contain
withdrawn claims.

Ship rom verified clean this iteration: `.build_flags` carries NO probe
defines (checked for BOOT_*, PAL_PEN, PAL_VBLANK, FLIP_DEFER, DMA_CENSUS,
MISTER_BOOT, SLV_CACHE_OFF — none present), `_end = 0x060135c8`, stamp
c79131d3+.

THE SHAPE OF THE NIGHT, for whoever reads this next: every mechanism I
proposed was killed by evidence I could have gathered first — the RTL
killed two, our own source killed one, an ares dump killed one, and a
process listing killed one. The probes that worked all had the same
shape: READ A VALUE (the ramp, the colour census, the process list). The
probes that failed all had the same shape: INFER FROM A PICTURE. That is
the transferable lesson from this arc, and it cost about fifteen hardware
round trips to learn.

## 51. STAGE-ATTRIBUTION PROBE built; both hardware probes staged
## (2026-09-08 ~11:00)

`BOOTSTAGEMAX=1` answers the question AFTER s16_span2, so the eventual
hardware session is not one bit. The shipping consume already stamps the
MD V-counter at four points (md_main.c 545/627/652/747, and 1074):

    0xFFB0B0 entry -> 0xFFB0B2 after scroll -> 0xFFB0B4 after the
    tile/span DMAs -> 0xFFB0B6 after cells -> 0xFFA176 end

It buckets the four deltas, finds the largest and floods the palette with
a colour naming that stage — no new instrumentation, just arithmetic on
stamps that already exist:

    WHITE   nothing slow (total < 8 lines): the consume is innocent
    RED     entry -> scroll
    GREEN   scroll -> tile/span DMAs      <- the FB-sourced DMAs
    BLUE    spans -> cells
    YELLOW  cells -> end                  <- includes the palette

GREEN would confirm entries 47/48's FB-sourced-DMA suspicion directly.
YELLOW would put it back on the palette upload. WHITE would say the
consume is not where the frame goes at all.

ARES (three samples, 500 vints apart): **WHITE every time** — total under
8 lines, consistent with s16_span2's GREEN. Both probes agree on the
emulator, which is the baseline they need.

STAGED ON THE MiSTer SD CARD, numbered so the order is obvious:
    /media/fat/games/S32X/1_span2.32x      is the consume the frame?
    /media/fat/games/S32X/2_stagemax.32x   which stage of it?
(scp works with MiSTer main down; the files simply wait.)

## 52. LOOP CLOSING (2026-09-08 ~11:05)

Ran ~4.7 hours, eight iterations. It is stopping because it has reached
the end of what it can do without hardware, not because the work is
finished — and inventing a ninth mechanism to test would be the exact
failure this arc has been about.

WHAT THE LOOP PRODUCED:
  - five withdrawn claims, each killed by evidence available all along:
    the RTL (2), our own source (1), an ares dump (1), a process list (1)
  - one genuinely new hardware measurement: the colour census, 9 colours
    on silicon against 76-119 on ares (45)
  - the discovery that MiSTer main is dead, which invalidated my own
    "autonomous launch" claim and explains four iterations of failed
    captures (49)
  - a consolidated, corrected handoff (HANDOFF-HARDWARE.md) that
    supersedes the state sections of two earlier ones
  - two staged hardware probes with ares baselines

WHAT IT DID NOT PRODUCE: a fix, or a confirmed mechanism for the ~95%
hardware miss rate. That needs the rig back.

## 53. HARDWARE, RIG BACK: the consume is NOT the bug, and neither is
## what runs before it (2026-09-08 ~11:56)

Mike rebooted; MiSTer main is up (pid 529) and the full chain is now
autonomous — scp, launch via the API, VERIFY VIA /tmp/remote.log (not
ACTIVEGAME), screenshot over ssh, scp back, measure by histogram. No
human in the loop.

**PROBE 1 — `1_span2.32x`, the consume's DURATION:**

    hardware   YELLOW <24 lines (3 of 4 captures), GREEN <8 (1 of 4)
    ares       under 8 lines

Mike's eye: "flashing yellow screen, intermittent green patterns."
So the consume takes **8-24 scanlines on hardware** against <8 on ares —
maybe 2-3x slower, and **nowhere near frame-eating out of 262 lines.**

**THE FB-SOURCED-DMA SUSPICION (entries 42, 47, 48) IS DEAD.** The
consume, which is where all the FB-sourced DMAs live, costs at most 24
lines. It cannot be the ~95% miss rate.

**PROBE 2 — `3_preconsume.32x`, the gap from handler entry to consume
entry:** entry 42 measured the consume ENDING deep in the visible
picture, so if it only runs 24 lines it must START late. Measured:

    hardware   GREEN <16 lines, all 4 captures, 100% of frame
    ares       gap 4-7 lines (read from WRAM: entry V=224, consume V=228-231)

**The consume starts promptly too.** So the frame is not being eaten
before it either.

**WHICH LEAVES: everything AFTER the consume.** The 68K handler's tail —
raise FM, post the window, push the DREQ packet, and the flip-hold echo —
is now the only place left for ~200 lines to go. That is also where the
68K waits on the MASTER, which makes the SH-2 side the suspect rather
than the 68K side.

PROBE CALIBRATION LESSON, learned twice now: **on ares my MD-palette
floods are HIDDEN BEHIND the working 32X layer**, so a screenshot there
reads BOOT_SHSTAGE's 32X backdrop, not my verdict. That is why the
preconsume probe "read RED on ares" while its own WRAM values said
GREEN. On hardware the flood is visible precisely because the 32X layer
contributes nothing (entry 30). **Verify flood probes on ares BY DUMPING
THE WRAM VALUES, never by screenshot.**

RIG NOTE from Mike: ssh root can `reboot` the MiSTer at any time. Four
iterations of this loop sat blocked on a dead MiSTer main that one
command would have fixed.

## 54-55. FOUND IT: THE DREQ PUSH IS THE FRAME (2026-09-08 ~12:15)

Three probes, fully autonomous, no human in the loop.

**BOOTTAIL** — of the four handler-tail stages, which is biggest?
Stamps the shipping code already writes: consume-end 0xFFA176 -> post
0xFFA0A0 -> pre-push 0xFFA0AA -> post-push 0xFFA0AC -> hold-exit
0xFFA09E.

    hardware   BLUE, 4 of 4 captures, 100% of frame  = THE DREQ PUSH
    ares (WRAM) gaps [3, 0, 47, 0] lines, total 50   = also the push

**BOOTPUSHLEN** — how long is that push?

    ares       44-47 scanlines   (read from WRAM, three frames)
    hardware   ORANGE = 96-160 scanlines, 4 of 4 captures

**So the 68K's DREQ push into the 32X FIFO takes 96-160 scanlines on
real hardware against 44-47 on ares — up to a THIRD OF THE ENTIRE FRAME,
in one stage, and 2-3.5x the emulator's figure.**

Put the whole handler together, hardware vs ares:

    stage                  ares      hardware
    entry -> consume        4-7       <16
    the consume             <8        8-24
    consume-end -> post      3         (small)
    THE DREQ PUSH          44-47     96-160
    push -> hold exit        0         (small)
    ---------------------------------------------
    handler total          ~62       ~120-200 of 262 lines

That is the ~95% miss rate, and it is ONE STAGE.

**EVERY OTHER SUSPECT FROM THIS ARC IS NOW ELIMINATED BY MEASUREMENT**,
not by argument: the palette (37, 46), the FB-sourced DMAs and the whole
consume (53), what runs before the consume (53), DMA-to-CRAM (46), 68K
FB reads (47). The frame was never in any of them.

**WHY THE PUSH IS SLOW, and what to do about it** — not yet measured, so
stated as candidates in order:
  a. **The FIFO fills and the 68K stalls.** The 68K writes words into the
     32X DREQ FIFO faster than the master's DMAC drains them; every full
     FIFO is a stalled write. On ares the drain is effectively free. The
     RTL's FIFO_FULL path (S32X_MiSTer rtl/32X/VDP.sv:181, the DRAM_CS_N
     branch) is where to read next.
  b. **The master is not draining promptly** — it arms the DMA at the
     announce and the 68K pushes after the 0xA001 echo (ARMGATE), so a
     late or slow drain shows up entirely as push time on the 68K side.
  c. **The push is simply bigger than it needs to be.** One R60 packet
     per vint; if a large fraction of it is unchanged between vints, the
     push is paying for data the master already has.

(a) and (b) are the same measurement from two ends and can be told apart
by counting FIFO-full stalls on the 68K side versus drain completion on
the master side. (c) is a design question with a real answer available:
the packet's own change flags.

NEXT: measure the push's *shape* — is it a steady rate (bandwidth-bound)
or long stalls (FIFO-full)? A stamp every N words during the push, and
the distribution says which.

## 56-59. THE COST IS THE 68K's WRITE INTO THE 32X DREQ FIFO
## (2026-09-08 ~12:25)

Chain of hardware measurements, all autonomous, ares baselines read from
WRAM (never from a screenshot — see the calibration note in 53).

**56. WHICH PART OF THE PUSH?** r60_push already carries a PUSH AUTOPSY
with five stamps. Bucketing the biggest:

    ares (WRAM)  total 48 lines: rotor+cmp 21, selection 9, REGS 2-3,
                 pal 1, records 15
    hardware     BLUE, 4 of 4 = **the REGS stage**, which on ares is the
                 SMALLEST at 2-3 lines

The regs stage is `ship 20 words into the FIFO`. Nothing else.

**57. IS THE FIFO FULL?** `R60G()` spins while `*ctrl < 0` (FIFO full),
decrementing `spin` from 2600, before every word.

    ares       residual 2600  = never full
    hardware   residual 2600  = **never full either**, 4 of 4

So the 68K is NOT waiting for the master to drain. My candidate (a) from
entry 55 is dead, and so is candidate (b) — a slow master drain would
show as FIFO-full and it does not.

**58-59. POLL OR WRITE?** Per word the code does two 32X accesses: a READ
of *ctrl and a WRITE to fifo[0]. Dropping the per-word poll is sound here
precisely because the FIFO is provably never full. Bucketed the regs
stage in lines, with and without:

    ares                        2-3 lines
    hardware, poll kept         ORANGE 32-80 lines
    hardware, poll removed      RED    >=80 lines

**Removing the poll did not help.** So the cost is the FIFO WRITES, not
the poll reads.

CONFOUND, recorded honestly: Mike coined up and started a game during
both captures, so the two runs are under gameplay load (the right load)
but not identically matched scenes. The direction is solid — removing the
poll did not reduce the stage — but "nopoll is WORSE" is not established
and should not be quoted.

**THE FINDING: a 68K word write into the 32X DREQ FIFO costs roughly 2-4
scanlines on this hardware — on the order of 1000-2000 68K cycles each —
against ~30 cycles on ares.** Twenty of them is a third of the frame.
That is the 95% miss rate, and it is one instruction in a loop.

**WHAT IT IS NOT** (all eliminated by measurement this session): the
palette, the FB-sourced DMAs, the whole consume, anything before the
consume, DMA-to-CRAM, 68K FB reads, FIFO-full stalling, the master's
drain rate.

**NEXT QUESTION, and it decides the fix:** is it the DREQ FIFO register
specifically, or is EVERY 68K access to the 32X side this slow? Time 20
writes to a harmless 32X register (a COMM port) against 20 FIFO writes in
the same build.
  - COMM writes equally slow -> the whole 68K<->32X interface is the
    problem, and the fix is to move data across some other way entirely.
  - COMM writes fast -> the DREQ FIFO path specifically is slow, and the
    fix is bounded: fewer, wider, or differently-timed FIFO writes.

## 60. IT IS NOT THE DREQ PATH — IT IS EVERY 68K->32X REGISTER ACCESS
## (2026-09-08 ~12:40)

Timed 20 writes to a harmless 32X register (COMM2, 0xA15122, which the
68K already writes every vint) in the same build, same place, same load,
same buckets as the regs stage:

    hardware   20 FIFO writes   ORANGE 32-80 lines  (RED >=80 poll-free)
    hardware   20 COMM2 writes  ORANGE 32-80 lines
    ares       20 FIFO writes   3 lines
    ares       20 COMM2 writes  19 lines  (see caveat)

**A plain COMM register write is as slow as a DREQ FIFO write on
hardware.** So the DREQ path is not special: **every 68K access to the
32X side costs roughly 2-4 scanlines — on the order of 1000-2000 68K
cycles.** With ~50-145 words in a packet, that is the whole frame.

CAVEAT ON THE ARES BASELINE, stated rather than hidden: the ares COMM2
stamps (V 13 -> 32) are inconsistent with the regs stamps (V 14 -> 17)
given the COMM2 block sits BEFORE the regs ship in r60_push — the two
cannot both be same-vint. One of those stamp pairs is not measuring what
its placement implies, and I did not chase it further. **The HARDWARE
numbers do not depend on it**: both land in the same ORANGE bucket by
direct capture, which is the comparison that matters.

**WHY THIS REFRAMES THE FIX.** r60_push's own comment records that the
PAL_DELTA arm cut the packet from 145 to 52 words and **the push span did
not move** — measured on ares. Of course it did not: ares is not
per-access bound, so halving the word count changes almost nothing there.
**On hardware, where cost is per access, cutting words is the whole
lever.** That prior negative result was an ares artifact and should not
have discouraged packet-size work.

DIRECTION, in order of expected payoff:
  a. **Send fewer words per vint.** PAL_DELTA is already on the ship line
     (52 words); the question is what is left and whether more can be
     made conditional. Every word removed is ~2-4 scanlines back.
  b. **Push less often** — not every vint.
  c. **Cross the boundary a different way.** Both reference programs
     avoid heavy 68K<->32X register traffic entirely (entry 43): the 240p
     suite sends a command word and a ROM pointer; Cannonball's 68K is a
     pad reader.

NEXT MEASUREMENT: the actual packet word count on hardware
(0xFFA0A4 accumulates `tw`, 0xFFA0A8 counts pushes) — words/push tells us
directly how many scanlines the push is costing and how much (a) can win.

## 61-62. PACKET SIZE IS NOT THE LEVER; THE FIRST WORDS ARE
## (2026-09-08 ~12:42)

**61. Cut the packet on hardware and the game did not speed up.**
`BOOTPUSHCUT` ships the 20 reg words and abandons the rest (~52 -> 20
words), paired with the game-frame wheel. Sampled every 2s for 24s,
against a matched full-packet control, both in attract:

    full packet   wheel advanced ~7 steps per 2s sample
    cut packet    wheel advanced ~6 steps per 2s sample

**No gain.** Entry 60's "words are the whole lever on hardware" is
WITHDRAWN — it was my inference, and hardware says no. (r60_push's own
comment recorded the same negative on ares; I argued it was an ares
artifact. It was not.)

(Note: these runs are in ATTRACT, ~7 wheel steps/2s = far faster than the
~3 game-frames/s measured under play in entry 40. Attract is the light
case; the A/B is still valid because both arms are attract.)

**62. So where inside the regs stage?** Between the selection stamp and
the regs-shipped stamp sit exactly three things: the length-register
write, the DREQ enable (`*ctrl = 4`), and the 20 words. Stamped after the
enable to split setup from words:

    hardware   BLUE, 4 of 4 = **the 20 WORD WRITES dominate**, not setup

**PUT 61 AND 62 TOGETHER AND THE STORY CHANGES SHAPE.** The first ~20
words into the FIFO are expensive; the ~32 words after them are nearly
free (cutting them saved nothing). So this is NOT a per-word cost — it is
a **burst of stalling at the START of the push that later words do not
pay.**

The natural mechanism: **contention with the master.** The 68K pushes
immediately after ARMGATE's 0xA001 echo, but *armed* is not *idle* — the
master is still finishing its window work, and the 32X side stalls the
68K's writes until it frees up. Once the master settles, the rest of the
packet streams cheaply.

**THAT MAKES THE FIX TIMING, NOT VOLUME** — and timing is far cheaper to
change than the packet format.

NEXT PROBE: insert a deliberate delay between the echo and the first word
and re-measure the regs stage. If the stage shrinks, contention at push
start is confirmed and the lever is *when* the 68K pushes, not *how much*.
If it does not shrink, the stall is intrinsic to the first FIFO writes and
the DMAC warm-up is the suspect.

## 63-65. THE ANSWER: THE DREQ FIFO IS 24x SLOWER THAN THE FRAMEBUFFER
## (2026-09-08 ~12:55)

Built a value instrument (64) — MD CRAM is 9 bits, exactly enough to
carry a 0-255 scanline count as a colour (R = d&7, G = (d>>3)&7,
B = (d>>6)&3), flooded across the screen and decoded from the capture
histogram. **No more buckets: one screenshot, one exact number.** Every
figure below is a direct hardware reading.

    68K -> 32X DREQ FIFO   20 words   **48 scanlines**
    68K -> 32X FRAMEBUFFER 20 words   ** 2 scanlines**

**24x.** Same CPU, same 20 words, same vint, same build — only the
destination differs.

Supporting numbers, all exact, all hardware:

    whole push, ~52-word packet        99 scanlines
    whole push, cut to 20 words        63 scanlines   (so ~1.1 lines per
                                                       marginal word)
    regs stage with an 8-line delay    55-58  (baseline 48: the delay
                                       added its own 8 lines and dodged
                                       nothing — NOT contention)

**THE PUSH IS 99 OF A 262-LINE FRAME.** Routed through the framebuffer
instead, the same payload should cost ~5-10 lines. That is ~90 scanlines
returned to the 68K every vint, against a handler currently measured at
~120-200 lines on hardware.

**THIS ALSO RETIRES THE REST OF THE ARC.** Eliminated by measurement, not
argument: the palette, the FB-sourced DMAs, the whole consume, everything
before the consume, DMA-to-CRAM, 68K framebuffer READS (which are fast —
2 lines per 20, proven here), FIFO-full stalling, the master's drain
rate, packet size as the primary lever, and push timing/contention.

**THE FIX: stop pushing the packet through the DREQ FIFO. Write it into
the framebuffer and have the master read it there.**

Why this is credible rather than hopeful:
  - the 68K's FB writes are proven to LAND on this hardware (s16_68kdraw,
    LOOP27 10) and are proven CHEAP here (2 lines per 20 words);
  - the master already consumes packets from the FB in the other
    direction, with a magic-word handshake (0xB6B6) that is exactly the
    synchronisation this needs;
  - both reference programs avoid heavy 68K<->32X register traffic
    entirely (entry 43), which is what the FIFO is.

WHAT THE WORK ACTUALLY INVOLVES:
  1. 68K writes the packet into an FB buffer instead of `fifo[0]`, with a
     magic/sequence word written LAST so a torn packet is detectable —
     the same discipline md_consume already uses.
  2. Master polls that buffer instead of taking a DREQ landing; its
     existing landing-validation logic mostly transfers.
  3. FM ownership: the 68K writes the FB at FM=0, the master reads at
     FM=1. The window protocol already sequences exactly that, so the
     packet buffer must live where a bank flip cannot move it under the
     reader (the staging discipline the port already has for tiles).
  4. DREQ then carries nothing, or only the small control words.

RISK, stated plainly: this is the port's central transport and the change
touches the ARMGATE echo, the lost-push belt, the torn-landing census and
the flip hold. It is a real piece of work, not a flag. But it is the first
thing tonight with a 24x hardware measurement behind it.

## 66. FB TRANSPORT: the saving is proven, the PLACEMENT is the work
## (2026-09-08 ~13:00)

Started building the FB transport and hit the first real design problem,
which is worth recording precisely because it is the whole remaining job.

**The saving is not in doubt** (65): 20 words cost 48 scanlines through
the DREQ FIFO and 2 through the framebuffer, measured on hardware in the
same build.

**The problem is WHERE the packet lives.** Probed four candidate FB
regions above the visible area (0x12000, 0x14000, 0x18000, 0x1C000) by
having the master stamp magic words and the 68K read them back a vint
later. **Mask = 0: not one survived.**

Two readings, and the second is the likely one:
  a. all four are actively written (the FB above the visible rows carries
     FBCLEAR, staging and sbuf), or
  b. **BANK PARITY** — the master writes the DRAW bank, a flip happens
     between its window and the 68K's next vint, and the 68K reads the
     OTHER bank. The sentinel is intact, just not where the reader looked.

(b) fits what we already know: s16_abread proved the 68K CAN read what
the master wrote when both touch the same bank within a window.

**SO THE REAL WORK, stated honestly, is not "write to the FB instead":**

  1. **Find or make ~300 bytes of FB that survive a flip.** The
     documented 2KB hole at 0x1E800 is FULL — md_pkt B (1472B) + palette
     (64B at 0x1EDC0) + SAT (512B at 0x1EE00) = exactly 2KB. Either
     locate genuinely free space above 0x11FC0 (where md_pkt A ends) with
     a bank-aware sentinel probe, or accept writing the packet to BOTH
     banks (cheap — 2 lines per 20 words means even double-writing beats
     the FIFO by 12x).
  2. **Sequence it against the flip and FM.** The 68K writes at FM=0, the
     master reads at FM=1. The window protocol already orders exactly
     that, but the buffer must not move under the reader — the same
     staging discipline the port already applies to tiles.
  3. **Torn-packet detection.** Write a magic/sequence word LAST, exactly
     as md_consume already does with 0xB6B6. The existing landing
     validation, the lost-push belt and the torn-landing census all key
     off DREQ semantics and would need repointing.
  4. **ARMGATE, the echo and the flip hold** are all built around the
     DREQ handshake and would be simplified or removed.

**WRITING TO BOTH BANKS IS PROBABLY THE ANSWER TO (1)** and it makes the
whole thing tractable: no free-region hunt, no bank-parity reasoning, and
at 2 lines per 20 words the doubled cost is still ~4 lines against the
FIFO's 48.

STOPPING THE BUILD HERE. This is the port's central transport; the
measurement that justifies it is solid, but rewiring ARMGATE, the belt
and the tear census at the end of a 12-hour session is how good findings
get turned into broken ship lines. The next session starts with a
bank-aware sentinel probe and item 1.

---------------------------------------------------------------------

## 67. THE FB TRANSPORT, MEASURED: 48 LINES -> 1, AND IT LANDS

`BOOTFBXFER=1` / `BOOTFBXT=1`, 2026-09-08, on Mike's MiSTer. Item 1 is
answered, and the answer is better than the both-banks shortcut entry 66
proposed: there is nothing to double-write.

**The probe.** The 68K writes the same short test packet into the FB
TWICE per vint, in the two places the transport could sit:

    A  0x852000  before the post, at FM=0   (pre-flip)
    B  0x852040  inside r60_push, at FM=1   (post-flip)

word[0] is a per-vint sequence, the rest a fixed pattern. The MASTER
reads both regions in its window body and reports, per region, a
saturating count of consecutive windows in which the sequence advanced
by EXACTLY ONE. That is the whole design: constant test values cannot
tell a fresh write from last window's, and a naive readback would have
called a one-window-stale region a pass. The verdict rides COMM8 and is
flooded through the value instrument.

**Hardware:  runA = 7 (saturated), runB = 0, content-ok = 0.**

  - The FM=0 write LANDS, and the master reads it FRESH — same window,
    no flip in between — for every window it was watched.
  - The FM=1 write does not arrive at all. Not stale: absent.

**Cost: 20 words into the FB at FM=0 = 1 SCANLINE.** The same 20 words
through the DREQ FIFO are 48. Not 24x — **~48x**, with no free-region
hunt, no bank parity, no double write.

### WHAT THIS RETRACTS FROM ENTRY 65 AND HANDOFF-DREQ

**The 68K cannot touch the 32X framebuffer at FM=1 on this hardware.**
Writes are dropped, reads return nothing usable. Two of last night's
results were taken through that dead path and are void:

  - **"20 writes to the FRAMEBUFFER = 2 lines" (entry 65) timed writes
    that never landed.** The number is real; it is the cost of a
    discarded write. The live route costs 1 line for the same 20 words,
    so the conclusion survives — but it was luck, not method.
  - **"FB free-region sentinel mask: 0 of 4 survived" (entry 66) is not
    evidence about those regions.** The master stamped them, but the
    68K's READBACK sat in r60_push at FM=1. It was reading through the
    dead path. Bank parity was never demonstrated; the sentinels may
    have been sitting there untouched the whole time.

### TWO RIG LESSONS THAT COST SIX ROUND TRIPS

  - **d=0 IS BLACK AND SO IS A BLANKED SCREEN.** Four captures came back
    all-black and were read as "the flood never ran". Every value the
    instrument can carry must be biased away from 0 (this probe sets
    bit 7 always), or a zero result and a dead machine are the same
    picture. Entry 66's "0 of 4" is exactly this trap.
  - **A MiSTer rom needs `MISTERBOOT=1`.** Six black captures in a row
    were nothing but a missing flag: without the slave SDRAM warm-up the
    build boots on ares and is black on hardware. Every probe rom for
    the MiSTer is `make ship-us BOOT<X>=1 MISTERBOOT=1`. The control that
    caught it: the same flag build from the current tree was black while
    the night's rom of the same name painted, which pointed at the tree
    and not the code — the roms turned out to be the same build shifted
    560 bytes, and the flag was the only real difference.
  - Do not park a value on COMM8. v1 of this probe left 0xBBxx on the
    channel permanently; COMM8 never reads 0 again, the master's pended
    posts stop, and the screen goes black. The verdict now rides the
    channel like every other message and the 68K clears it.

### THE JOB NOW

Item 1 is closed. The transport is: **68K writes the packet into the FB
at FM=0, before the post; the master copies FB -> SPR_LAND in its window
body and the whole existing harvest runs unchanged.** Items 2-4 (flip/FM
sequencing, the magic-word tear check, repointing ARMGATE and the belt)
stand as written, minus every bank-parity concern.

---------------------------------------------------------------------

## 69. THE WARM-UP IS THE WHOLE BOOT FIX (2026-09-08 14:10)

`MISTERBOOT=1` had always bundled two changes — the slave SDRAM warm-up
stub in mars_start.s and the slave cache-off — and nothing had ever
separated them. Four roms from one tree, one flag apart, on the MiSTer:

    warm-up   cache-off   result
    no        no          BLACK   (1 colour on screen)
    no        yes         BLACK   (1 colour)
    YES       no          BOOTS   (18 colours, 64 game frames/64 vints)
    yes       yes         BOOTS   (62 colours, 64/64)

The warm-up is the entire fix; the cache-off does nothing and had been
carrying half the credit. On ares the ship line's counters are IDENTICAL
with and without the warm-up (vints, packets, tile batches, chunks,
rejects, every column, frames 200 and 400), so it costs nothing there.

**Now default on** (`NOSLVWARM=1` removes it). Two comments said the
opposite — the Makefile's "the DEFAULT BUILD CARRIES NEITHER / neither
fixed the MiSTer hang" and the same claim in mars_start.s — and SIX
black captures were spent this session rediscovering that they were
wrong. Both corrected at the source. Entry 10 had it right the first
time: "a REAL fix to fold into every build".

New flags: `MISTERWARM=1` (warm-up alone), `MISTERCACHE=1` (cache-off
alone); `MISTERBOOT=1` still means both.

## 70. MIKE'S CALL: KEEPER (2026-09-08 14:17)

`make ship-us FBXPORT=1` — the FB transport, the warm-up by default, no
probe flags, no palette floods. Deployed as
`/media/fat/games/S32X/KEEPER.32x`, confirmed running from BOTH
`/tmp/remote.log` ("game started: Sega32X/KEEPER.32x") and
`/var/log/ACTIVEGAME`.

Mike, watching it: **"AH THIS ONE! THIS IS THE ONE! Bank it!"**

Headless captures of that session, three shots 4s apart: 97-118 distinct
colours, 54-61% of pixels changing between consecutive captures. The 32X
layer is on screen and moving on real hardware.

Tagged `mister-keeper-20260908`. Reproducible: rebuilding from the clean
tag differs from the accepted binary by 36 bytes, all of them the build
stamp (git hash + timestamp at 0x2489D8 and 0x3FFFD4).

CAUTION carried forward: the rom Mike looked at ten minutes earlier
(`H4_both`) STROBED, and that was the instrument, not the port —
`BOOTGAMERATE=1` floods all 64 MD palette entries from inside the push
every vint, fighting the game's own palette writes. Never hand him a
`BOOT_VALUE` build as something to judge; those exist to be photographed
by a script, not watched.

---------------------------------------------------------------------

## 71. THE INSTRUMENT WAS THE BUG: SH-2 COUNTERS ABOVE DIAG[63] ARE LOST

2026-09-08, the flip census. Every SH-2-side number quoted in this
session's second half came from writes that never landed.

**Symptom.** Two counters incremented on the SAME STATEMENT read 808 and
1. Adjacent lines disagreed by three orders of magnitude.

**Calibration** at `m_main` entry, a site that must execute exactly once:

    DIAG[62]   0x260280F8    1     write sticks
    DIAG[64]   0x26028100    0     WRITE LOST
    DIAG[84]   0x26028150    0     WRITE LOST
    0x2602FF00               1     write sticks
    0x26037000               1     write sticks
    0x2603FF00              66     sticks, but the address is in use

The usable DIAG block ends around slot 63. Slots past it silently
discard writes and read back residue — plausible small numbers, which is
what makes it dangerous. A zero-in-the-baseline scan does NOT prove a
slot is free: a slot that discards writes reads zero too.

**What this invalidated, and the correction:**

  - **"FB transport delivery is 0.2%" — WRONG. It is 100%.** With a
    calibrated census block: 819 publishes, 819 taken, 0 bad length, 0
    stale. The alarm was a lost counter.
  - `DIAG[56]` ("body-fallback flips") is a real counter — it is below
    the cliff — and it was the only reason the contradiction showed up
    at all.

**Rule going forward:** every census build carries `CEN[10]`, incremented
once at `m_main` entry. If it does not read exactly 1, no other slot in
that run means anything. The census block is `CEN` at 0x2602FF00
(m_main.c), not DIAG.

## 72. THE FLIP IS NOT THE LIMITER (2026-09-08)

With the census fixed, the measurement entry 9 called for, 900 frames,
ares, same tree, one flag apart:

    build          vints  ISR flips  body flips  declines  refresh
    DREQ FIFO       875      357        338        668     79%  47.7 Hz
    FB transport    881        7        814        825     93%  55.9 Hz

The declines and the flips sum to more than the vint count because a
vint DECLINES in the V-ISR (past the vblank edge) and then FLIPS in the
body. The frame is not dropped; it is flipped late. Entry 9's "flips on
17% of vints = ~10 Hz" does not reproduce.

**So the framebuffer is refreshing at ~56 Hz and the observed ~9 fps is
the GAME's frame advance, not the display's.** FLIPDEFER is not the next
lever, and the plan built on entry 9 is withdrawn.

The limiter is upstream and already named in docs/design/SILICON.md: the
game enters and leaves its IRQ4 handler every vint (`fmgate_ret` reads
64/64 on both transports) while its own frame advance inside that
handler does not keep up. That is where the next measurement goes.

Also settled, since the census could finally see it: the packet lift
works at BOTH positions — before the flip and after it — at 100%
delivery each (`FBXLATE=1` is the A/B). The flip does not move the
packet out from under the reader, so the pre-flip move was unnecessary.
It is kept because it is free and it removes the question.

## 73. FBXPORT REGRESSES THE FLIP 27 Hz -> 1.2 Hz (2026-09-08)

Entry 72's "93% refresh" was wrong. It counted code REACHING a flip
site. The only thing that changes the displayed framebuffer is the FS
write in flip_span(), and flip_span() declines internally — the same
K2FREE edge guard, evaluated again — on almost every call.

Counter at the FS write itself, corroborated by ares's own flip trace
(`--trace-flip`, which logs FS write requests and actual bank changes):

    build       vints  reached flip site  FS WRITES  emulator trace   refresh
    baseline     497         392             222     228 wr / 226 fl  27.3 Hz
    FBXPORT      502         472               6      12 wr /  10 fl   1.2 Hz

**The FB transport made the display refresh 20x WORSE.** The mechanism
is consistent with the change: the push moved AHEAD of the post (it has
to — the 68K cannot reach the framebuffer at FM=1), so the post arrives
~57 scanlines later, every flip attempt lands past the vblank edge, and
both the ISR and the body fallback decline.

So: entry 9 was right that the flip is the limiter, entry 72's
withdrawal of that was wrong, and FLIPDEFER (entry 12) is exactly the
lever — a late flip must commit at the next vblank instead of being
dropped. The FPGA RTL does this in silicon (VDP.sv latches FS only when
VBLK); ares latches immediately, which is why the guard exists at all.

MEASUREMENT RULE from this entry: count the HARDWARE EFFECT, not the
code path. "Reached the flip site" and "wrote FS" differ by 78x here.
Where the emulator can log the effect itself (--trace-flip, --trace-dreq,
--trace-comm), prefer that over any counter in our own source.

## 74. THE CENSUS BLOCK NEEDED THE SAME CALIBRATION (2026-09-08)

CEN[0] was verified to hold a write and the rest of the block assumed.
CEN[12] then reported 470 hits inside a block compiled to `if (0)`.
Calibrated properly — bump every slot once at m_main entry, a site that
runs exactly once, and require each to read exactly 1:

    usable   0 1 2 3 4 5 7 8 10 13 17 18 19 20 21 22 23
    ALIASED  6 9 11 12 14 15 16      (read 76, 76, 150, 112, 35, 289, 150)

Slots 9/11/12/14/15/16 were the entire flip census, so the flip numbers
in entry 72 and the first version of 73 were garbage. `CENCAL=1` runs
this calibration. **Calibrate every slot, not the block.**

The `--trace-flip` figures were never affected — they come from the
emulator, not from our memory — which is why they were the only numbers
that stayed consistent all session.

Second trap, same shape: `.build_flags` does not capture every flag
(FLIP_EDGE_OFF and FBX_TAIL reach MDCCFLAGS/SHCCFLAGS and the compile
line, but not the stamp). Objects depend on the stamp, so a flag build
can silently reuse the previous build's objects — two roms differing by
2 bytes of timestamp. `touch sh_src/m_main.c md_src/md_main.c` before
every flag A/B until that is fixed.

## 75. WHY FBXPORT KILLS THE FLIP: THE POST LEAVES VBLANK

Trustworthy channel only (ares --trace-flip + 68K WRAM counters):

    build                 vints  V at post  flips   refresh   delivery  tears
    baseline (DREQ)        877      240      346    23.7 Hz     n/a       54
    DREQ, no landing wait  871      n/a      440    30.3 Hz     n/a      208
    FBXPORT (pre-post)     882       35       12     0.8 Hz   821/821      0
    FBXPORT (tail push)    882       35       12     0.8 Hz   821/821      0

**V at post is the mechanism.** The baseline posts at V=240, inside
vblank, where the ISR can still flip. FBXPORT posts at V=35 — line 35 of
active display — and every flip attempt then misses the window.

Ruled out as the cause:
  - the vblank edge guard (FLIPEDGEOFF=1 changes nothing under FBXPORT),
  - FLIPDEFER (no effect under FBXPORT; on the DREQ build it drops the
    game to 69 vints in 520 frames, reproducing entry 12),
  - the landing wait (removing it on the DREQ build RAISES flips to
    30.3 Hz, so it is not what delays a post),
  - the push's position: moving it to the vint tail (FBXTAIL=1, after
    the master's ack where FM is already down) left V at post at 35.

So the post is late for a reason that is NOT the 68K's push placement,
and that is the next thing to find. The FB transport itself is sound:
100% delivery, zero tears, against the DREQ route's 54.

NOLANDWAIT is a real but not free lever on the shipping line: +28%
refresh (23.7 -> 30.3 Hz) for 4x the torn landings (54 -> 208) and lower
tile throughput (355 -> 262). Not shippable as-is; worth revisiting if
the tear feedback can absorb it.

## 76. THE REAL GATE, AND THE TWO BOTTLENECKS ARE INDEPENDENT

Mike, on a day of measurements that did not move: "we had a working
model 4 days ago that was well over 30 frames per second, so you are
measuring the wrong gates". Correct. Everything in entries 71-75 was
measured on ATTRACT MODE with no input, and on the FLIP RATE. The
project's speed gate is `tools/gameplay_speed.py`: game-frames per vint
over the level-1 input script, 100% = 60 game-frames/s. It also already
carried the counter entry 72 went looking for — WRAM 0xFFF144, vints in
which the game's pass had not finished.

    build                              speed    fps   frame-misses
    ship line (today)                  48.7%     29     51.3%
    opt1 flags on today's tree         60.7%     36     39.3%
    opt1 rom as built 2026-09-07       64.9%     39     35.1%
    FBXPORT alone                      50.3%     30     49.7%
    **opt1 flags + FBXPORT             82.3%     49     17.7%**

Confirmed: 86.9% on a different sample window (2000..3600), and a fresh
build from clean objects reproduces byte-identically apart from the
3-byte stamp.

**THE TWO BOTTLENECKS ARE INDEPENDENT AND NEITHER SHOWS ALONE.** FBXPORT
by itself buys 1.6 points, which is why entry 75 read it as a failure.
The sprite-pair fix by itself buys 12. Together they buy 34. The DREQ
push and the late-claim failures were each hiding the other: with the
claim failing, the frame is spent on shadow-ramp draws no matter how
fast the packet crosses; with the packet crossing slowly, fixing the
claim leaves the 68K waiting anyway.

Flags: TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1 FBXPORT=1.

BUILD-SYSTEM HAZARD that cost several false results here: `.build_flags`
does not capture every flag and objects depend on it, so a flag A/B can
silently reuse the previous build's objects — three roms this session
differed from their control by 2-4 bytes of timestamp. `rm -f sh_src/*.o
md_src/*.o` before every A/B, and CHECK the roms differ before believing
a comparison. Also: `make -n | grep "md_main.c"` matches the rule's echo
line, not the compiler invocation; grep for `m68k-elf-gcc.*md_main\.c`.

## 77. CLAIMNEW: THE LATE CLAIM WAS SCANNING THE SNAPSHOT

Entry 6 left a residual of 114 shadow-ramp draws with a diagnosed cause:
the compose reads records that arrived after the claim loop last looked.
The fix it proposed was to claim from SPR_LAND before the copy into
SPR_SNAP. The same effect, one line: **have the claim scan FB_SPR (the
live list) instead of SPR_SNAP (the snapshot).**

The two hold identical content whenever the snap refresh happened. They
differ exactly when the snap LATCH SKIPPED a refresh — a compose chain
mid-flight, which is the case entry 6 diagnosed — and there the claim
was scanning last frame's records and never claiming a pair for a set
that had just arrived. That set draws with base 15: the shadow ramp.

    metric                    FBXPORT+opt1   +CLAIMNEW
    shadow-ramp draws [3]         704            57
    claim failures    [1]          28             0
    nothing-stealable [0]          83             0
    speed                        82.3%         82.0%

12x fewer ramp draws, no claim failures at all, and no speed cost. Also
below entry 6's best (114) on a build running 20 points faster, which
means more sprite churn per second, not less.

Flags: TXTWRAM LATESTEAL0 LATEKEEP DRAWADOPT FBXPORT CLAIMNEW.

## 78. THE ARCADE 68K IS BUS-BOUND, NOT COMPUTE-BOUND (2026-09-08)

Mike: "are reads and writes being posted by the CPU at the same rate on
the genesis/32x as they are on the arcade?" and "do we have timings
coded into the game, or is it gated on cycles alone?" Both answered from
the binary and from MAME's own instruction trace — no sampling, no
inference.

**HOW THE GAME KEEPS TIME.** It is EVENT-gated, not cycle-gated:

    2ac6:  addqb #1,0xfffff01c    ; IRQ4 (vblank) increments a flag
    397e:  clrb  0xfffff01c       ; main code clears it
    3982:  tstb  0xfffff01c       ; and spins
    3986:  beqs  0x3982           ;   until vblank sets it
    3988:  dbf   %d0,0x397e       ; repeated d0+1 times = wait N frames

Delays are counted in VBLANK EVENTS. A scan of the whole program finds
exactly ONE pure cycle-delay loop:

    2d8a:  moveq #127,%d0
    2d8c:  dbf %d0,0x2d8c         ; ~1280 cycles, no memory access

128 us on the arcade, 167 us here — 30.4% longer, the clock ratio, and
the only place in the program where that matters directly. The other 101
short dbf loops are memory-move loops, sensitive to ACCESS RATE rather
than to the clock.

**WHAT THE TRACE SAYS** (gameplay, level-1 timeline, 7 frames):

    instructions per frame   3681 3743 3665 3702 3667 3716 3664
    mean                     3691
    idle-loop instructions   54 of 29594 = 0.18%
    CPU at every vblank      0x3986 — the wait loop, 8 of 8

So the game DOES finish its frame and wait — but it executes only ~8
idle instructions per frame (about four iterations, ~88 cycles). It
finishes with essentially nothing to spare.

**THE CONSEQUENCE, AND IT OVERTURNS THE CLOCK ARGUMENT.** 166,667
cycles/frame over 3,691 instructions is **45.2 cycles per instruction**.
No 68000 instruction mix averages 45 cycles. The arcade's CPU is
spending most of its frame STALLED ON THE BUS — System 16B video RAM
writes carry wait states — not computing.

That means the 76.7% clock ratio is NOT the ceiling, because we do not
pay the arcade's bus stalls: the game's video writes land in our own RAM
and packet staging, not on an S16 video bus. Consistent with the
measurement: the port runs at 82.0% of 60 fps, which is ABOVE the clock
ratio and would be impossible if we executed the same instructions at
the same per-instruction cost.

Arithmetic for the ceiling: our budget is 127,841 cycles/frame, so the
game's ~3,691 instructions fit inside one vint only if our average cost
is at or below **34.6 cycles per instruction**. At 82.0% we are at about
42, so ~7 cycles per instruction of headroom is what the remaining work
has to find — and that is a bus/access-rate question, not a clock one.

**UNVERIFIED, and it matters:** this assumes our port executes the same
instruction COUNT per game frame as the arcade. The port patches the
game's video accesses and gates some writers, so the count may differ.
Measuring it needs a 68K instruction count on our side, which ares does
not currently provide (its --profile is SH-2 only).

Tools: tools/arcade_trace.lua (MAME instruction trace, gameplay-driven)
and tools/arcade_trace.py (the analysis). PC sampling and memory taps
were both tried and both failed — every MAME hook fires at a fixed phase
where the game is always in its wait loop, and taps did not see work RAM
in this driver. Do not re-try either.

## 79. OUR SHIM COSTS AS MUCH 68K TIME AS THE GAME ITSELF

Entry 78 left one term unpriced: our own instruction count per frame.
MAME traces our rom's 68K exactly as it traced the arcade's, so it is
measurable. Per vint, 7 vints of the level-1 timeline:

    total 68K instructions      5335
      the game's own code       2780   52.1%
      our shim / RAMCODE        2882   47.9%

    arcade, for comparison      3691   per frame, all of it game

The shim is not overhead at the margin. It is roughly a second copy of
the game's own workload, on a CPU with 77% of the arcade's clock.

Not a spin artifact: the top ten shim PCs are 4.2% of shim instructions
and the hottest is a compare loop, not a wait, so the count is not
inflated by timing-dependent spinning in MAME.

Where the shim's time goes, by 256-byte block, instructions per vint:

    FF0A00   306   10.6%
    FF0E00   264    9.2%
    FF0700   258    9.0%
    FF0D00   240    8.4%   4-byte compare loop = the PAL_DELTA pre-pass
    FF0C00   183    6.4%
    FF0100   182    6.3%
    FF1200   175    6.1%
    FF1500   118    4.1%

No single hot spot: the cost is spread across the whole shim, so there
is no one loop to delete. FF0D00 is the palette compare pre-pass, which
r60_push's own comment measures at ~65 lines/vint and which the
frame-threshold note had already predicted as the next lever —
an independent measurement landing on the same place.

METHOD CAVEAT: MAME's 32X models neither SH-2 timing nor the FB stall,
so cycle costs from it mean nothing. Instruction counts are code-path
facts and survive that; the spin check above is what makes them safe to
use here.

## 80. THE PIPELINE, SIZED: THE GAME WRITES 85 WORDS A FRAME

Mike's architecture, in his words: "The game code only knows it's feeding
data through a pipeline provided by the Sega 16 arcade architecture. All
we should be doing is rebaking the bitmap data so that it's the
compatible colour palette and having the dual SH-2 chips organize and
feed the data to the 68K, wholesale."

Sized with tools/write_census_ares.py on the 82% build, per frame over
frames 1500-4100 of the level-1 timeline:

    what the GAME writes            what OUR SHIM does
      sprite RAM      47.6            adapter accesses   125.0
      text RAM        21.8            shim instructions  2882 per vint
      palette         14.8            hottest site       r60_ship_words
      I/O + bank       5.6                               (389,804 accesses)
      -----------------------
      TOTAL          ~85 words

**We move ~150 words a frame, and burn 2882 instructions deciding which
ones, to convey ~85 words of actual change.** About 34 shim instructions
per game write.

The game's writes are ALREADY INTERCEPTED — the patcher rebases every
one of them and the thunks mark dirty bits. So the pipeline does not
need to discover anything: a write-through at the thunk, forwarding each
write into the FB staging as it happens, is O(writes) and deletes the
rotor, the compare, the shadow, the dirty bitmap, the selection and the
packing. That is the architecture change, and 85 words/frame is why it
is affordable.

### 80a. NEGATIVE: PALNOCMP (ship raw instead of comparing)

Reasoned from the FB transport's cheap words: a redundant 32-word block
costs ~1.6 scanlines to ship and ~5 to prove redundant, so ship it.

    baseline (compare)        82.0%
    PALNOCMP (ship raw)       74.1%     REVERTED

Wrong, and the measurement says why: the compare's fast pre-scan exits
at the first difference, and an equal block then ships ZERO words. The
raw path pays a 16-long shadow copy AND 32 shipped words. Discovery is
cheaper than delivery here — the opposite of the DREQ case, because the
unit of delivery is a whole block while the unit of discovery is one
compare that usually stops early.

The prize is in NOT VISITING, not in cheaper comparing:

    PALROTOR_OFF (no visits, colours freeze)   86.1%    the ceiling
    baseline                                   82.0%

PAL_STREAK_N / PAL_BACKOFF_M are now build-tunable (PALSTREAK=,
PALBACKOFF=) to chase that 4.1 points by visiting less often, but the
real answer is write-through, which removes the visit entirely.
