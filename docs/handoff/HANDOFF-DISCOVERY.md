# HANDOFF — DISCOVERY-TIMING arc (loop engineering doc)

2026-09-07, late. Mike: "I want to look into the gates' timing. We have a
couple of invaluable resources right now that might tell us everything
we need." This is the handoff for that arc and the protocol the loop
runs by. The loop log is `docs/log/LOOP27.md`.

## The question

The port runs Sega's 68000 program on a 7.67 MHz part that the arcade
ran at 10 MHz, and the game repeats every other frame (HANDOFF-SESSION8
section 3: 68K frame ~310 lines against 262). The standing story is
"the clock gap plus our 62-line shim". Mike's hunch, and the reason for
this arc: the arcade program never used 10 MHz of instruction time; it
was paced by the board's bus (wait states per region, the video chip
arbitrating tile RAM) and by its frame protocol. If that is right, then
part of the 212-line "game pass" we measure on our side is not arcade
work at all but OUR taxes on the game's hardware writes (dirty-bit
thunks, framebuffer-window writes, FM stalls), and those are removable
without touching Sega's logic. The arc exists to MEASURE that split and
name every timing gate in the program with a number next to it.

## Resources on disk (verified 2026-09-07)

1. **Ghidra 11.2.1** — `/Users/mikeholzinger/src/kyocera-2235/ghidra_11.2.1_PUBLIC/`.
   Headless works; language `68000:BE:32:default`; loader base 0x0
   (the arcade map). Driver: `tools/ghidra_run.sh` (import once, then
   `census` / `script NAME.py` against the project). The project lives
   OUTSIDE the repo (scratch dir) because it holds the analysed Sega
   binary. Scripts are Jython 2 (ASCII only, `getScriptArgs()` for
   arguments; stock scripts that call `askFile` fail headless).
   `tools/ghidra/timing_census.py` is the first script: functions with
   call graph, every backward branch with the memory it touches, every
   instruction on a System 16B hardware range, the vectors. Output
   `docs/audit/timing_census.json` (gitignored: it names Sega's code).
2. **The operator's manual** — `docs/arcade/AlteredBeast.man.pdf`, 22
   scanned pages (no text layer; `pdftoppm` renders, no OCR installed).
   It is the KIT INSTALLATION manual: cabinet, harness pinouts, coin
   door, DIP switch tables (advertise sound, players, lives, difficulty),
   the TEST MODE menu (ROM test, Fix RAM, Scroll RAM, Color RAM, Scratch
   RAM, input, sound, CRT, DIP, bookkeeping), monitor 15.75 kHz / 60 Hz.
   No CPU or bus timing. Its value here: the DIP table and the self-test
   are an oracle we can run on our port (the ROM/RAM tests exercise every
   region the patcher rebased), and the harness pinout is the kit's I/O
   truth.
3. **jtcores** — `srcref/jtcores/cores/s16b/hdl/` (GPL: derive, cite,
   never copy). Facts already read for this arc:
   - 68000 clock = 50.3496 MHz x 29/146 = **10.001 MHz**
     (`jts16b_mapper.v` `FNUM=29, FDEN=146` feeding
     `jtframe_68kdtack_cen`; `jts16_cen.v` header for the master clock).
   - **Wait states are PROGRAMMED PER REGION by the game itself**: the
     mapper's size register for each of the 8 regions carries the DTACK
     cycle count in bits [3:2] (`jts16b_mapper.v:284-292`,
     `dtack_cyc = mmr[0x10+2n][3:2]`, fed to the DTACK generator as
     `wait2`/`wait3`). From the boot table NOTES.md decoded
     (`02 00 02 08 00 3f 00 ff 04 44 0d 40 00 84 00 c4`): program ROM 0,
     second ROM 0, tile bank 0, work RAM 0, **sprite RAM 1**, **tile +
     text RAM 3**, palette 0, I/O 0. So on the board every tile/text RAM
     access was the slowest thing the CPU did, and sprite RAM was slower
     than work RAM. On our port those regions are work-RAM mirrors and
     framebuffer staging: a different cost structure in BOTH directions.
   - The DTACK module itself (`jtframe_68kdtack_cen.v`) is in the
     jtframe submodule, which is NOT checked out under `srcref/jtcores/
     modules/jtframe` (only `fx68k`, `jt12`, ... are). Fetching it is a
     loop item; until then "dtack_cyc = 3" is a count of extra DTACK
     cycles in the module's units, not yet converted to 68K clocks.
   - `srcref/jtcores/modules/fx68k/` — the fx68k cycle-accurate
     microcode 68000 the arcade core runs on.
4. **MegaDrive_MiSTer** — `srcref/MegaDrive_MiSTer/rtl/nuked-md/68k.v`:
   Nuked-MD's `m68kcpu`, a die-shot-derived netlist-level 68000 (GPL,
   6299 lines, with `68k_ucode.txt` / `68k_ncode.txt`), plus
   `md_board.v`. This is instruction-cycle truth for BOTH machines' CPU.
   No simulator is installed (verilator/iverilog/yosys absent); an
   instruction-cycle table can instead come from the 68000 user manual
   numbers, cross-checked in MAME's 68K (cycle-approximate) where a
   whole-frame count is wanted.
5. **Already in the repo**: `roms/altbeast/prog68k.asm` (objdump
   listing, 94k lines), `tools/fmgate_derive.py` (write/read censuses of
   the game's FB-window sites from wpcatch, the SPANS/ENTRIES the FMGATE
   thunks gate), `tools/harvested_handlers.txt` and NOTES.md's UNPAIR
   section (every dispatch table and computed jump), the frame protocol
   (frame flag 0xFFF01C, IRQ4 handler 0x2AAC, MCU mailboxes 0xFFF0C0-C7,
   the text-RAM handshake at 0x410002), and the MAME lua taps
   (`tools/pal_tap.lua` per-PC write census on any range).

## What the first census says (docs/audit/timing_census.json)

- 234 functions, 272 backward branches. Hardware-range instructions
  (addresses normalised to 24 bits; the program uses sign-extended
  short absolutes for work RAM): text RAM 25 writes / 2 reads, tile RAM
  23 writes, palette 22 writes, sprite RAM 5 writes, I/O 13 reads /
  2 writes, MCU mailboxes 4 reads / 4 writes, frame flag 2 writes /
  1 read, the IRQ4 miss counter 0xFFF144 1 write (cleared at boot,
  incremented in the handler); ~1400 work-RAM references.
- Vectors: reset PC 0x400; level-4 autovector 0x404 (the IRQ4
  trampoline to 0x2AAC, NOTES.md); every other level 0x40C (rte stub).
- The IRQ4 handler at 0x2AAC: re-entry guard on 0xFFF01E, then `tst.b
  0xFFF01C` — flag still set means the previous frame has not finished:
  `addq.w #1,0xFFF144` and skip the update (THE miss counter
  gameplay_speed.py reads); otherwise set the flag and run the frame.
- 36 tight loops touch hardware. Nearly all are `dbf` block copies/fills
  into tile, text or palette RAM (scene loads, string writers such as
  0x159E-0x15B2). The GENUINE WAITS the static pass finds are three:
    - 0x397E-0x3988 (`FUN_0000397e`): `clr.b 0xFFF01C; tst.b; beq` inside
      a `dbf` — wait N vblanks. The main loop's vsync wait: idle on the
      arcade, and on ours the place where a missed frame is paid.
    - 0x2D82-0x2D88 and 0x2DA8-0x2DAE: `btst #2` / `btst #7` on the MCU
      mailbox 0xFFF0C2 with `bne` back, each followed by a `pea`/`rte`
      into 0x1AFDE or 0x400 — RESET/RESTART paths waiting on MCU bits,
      with a bare 127-count `dbf` delay loop at 0x2D8C between. Not
      per-frame; they matter only for how fast our shim answers the
      mailbox at boot and after a game-over restart.
    - 0x3ABE-0x3AC4 (`FUN_00003aae`): a `dbf` loop READING text RAM
      0x410000 — the text-RAM side of the MCU screen-sync handshake
      (NOTES.md: the MCU polls 0x410002); the read path was rebased to
      work RAM, so its cost is ours to check, not the board's.
  0x3028 (earlier mis-read as an I/O poll) is a coin-counter countdown
  that pulses 0xC40001; not a spin.
- Static counts are sites, not time. The dynamic census (question 1)
  converts them to lines per frame.

## Results so far (2026-09-07, LOOP27 entries 1-3)

- MAME cannot run any current line past the boot (68K parks in the shim
  window wait; FB-window writes invisible to its taps). The dynamic
  census lives on ares-debug, which now has 68K read and write range
  hooks (`--count-writes / --count-reads lo:hi:name`,
  `--count-writes-out CSV`; driver `tools/write_census_ares.py`).
- Write tax: ~90 game writes per frame in steady play, ~2 thunk
  entries; under 1 line. NOT the gap.
- THE GAP, decomposed per game frame (2 vints): game work ~250 lines,
  shim 2 x 63, FM-gate spin ~83 lines (the text-RAM writer's gate at
  game site 0x3AC8 waiting for the SH-2 to release the framebuffer),
  frame-flag slack ~65 lines. 250 lines = 73% of an arcade frame at
  10 MHz: the arcade idled the rest.
- Levers that reach the 262-line threshold on paper: remove the FM
  stall (text writer must not need the framebuffer while the SH-2 owns
  it) AND cut the shim to ~12 lines (palette compare off the 68K,
  HANDOFF-SESSION8 section 5). Neither touches Sega's logic.

## The loop (how every iteration runs)

Each iteration answers ONE question and ends with a number in
`docs/log/LOOP27.md`, positive or negative. Order inside an iteration:

1. **Discover (static).** A Ghidra script (`tools/ghidra/*.py`, run by
   `tools/ghidra_run.sh script`) that lists the sites relevant to the
   question, with function, callers, and the memory they touch. Cite
   arcade addresses. Never paste decompiled code into the repo; the
   scripts emit facts (addresses, counts, operands).
2. **Measure (dynamic).** ares-headless is deterministic under
   `--input`: dump counters or memory at chosen frames
   (`tools/mdstatic_gate.py` is the pattern). For per-PC counts use
   MAME's 68K-side lua taps (honest for the MD 68K) with the
   `-window -resolution 160x120 -keyboardprovider none -bench` flags
   from CLAUDE.md. The speed number is `tools/gameplay_speed.py`
   (game frames per vint, 2600-vint window; control 50.4 on the ship
   line).
3. **Hypothesise.** State the expected saving in LINES of a 262-line
   frame before building anything. Below ~50 lines total the frame
   protocol pays nothing (the threshold law, HANDOFF-SESSION8 3): say so
   and either combine levers or stop.
4. **Probe.** A Makefile flag, never a ship-line change. Measure speed
   AND fidelity: `tools/mdstatic_gate.py` (pens/slots/gate), the attract
   scorecard `tools/attract_parity.py` (arcade-graded title/demo), and
   `tools/state_frame.py` on any state Mike sends.
5. **Record.** LOOP27.md entry: question, sites, numbers, verdict. A
   negative result is recorded with the number that killed it, like every
   LOOP file before.

Standing rules apply: accuracy before speed; Sega's logic is never
rewritten, only gated, rebased or relieved of our taxes; derive from
the RTL, cite the `.v` line; measure before arguing.

## The questions, in order

1. **WRITE-TAX CENSUS.** DONE (LOOP27 2-3): negligible; the tax is the FM spin. Per frame, how many writes does the game make to
   each rebased region (tile RAM, text RAM, sprite RAM, palette, I/O,
   mailboxes), and what does each cost on OUR path (thunk: jsr + OR +
   displaced write + rts vs the original 16-cycle write; FB-window write
   incl. the FM stall; work-RAM mirror)? Output: tax in lines per frame,
   split by region. Method: MAME lua write census by PC over one level-1
   second (the `pal_tap.lua` shape, ranges from the census), costs from
   the 68000 cycle table plus the measured FM stall (session 7's NOBLIT
   +9.7 points prices the stall family).
2. **SPIN CENSUS.** DONE (LOOP27 3): gate #22 (text writer 0x3AC8) 75%, gate 0x372E 25%. Which of the 36 loops actually spin at runtime, how
   many iterations per frame, and on what: the text-RAM MCU handshake
   (0x410002), the I/O poll at 0x3028, the FMGATE thunks' own spins,
   the frame-flag spin. Method: MAME PC-hit counters on the loop
   branch addresses; ares for the FM-stall side. Output: lines per frame
   spent waiting on US versus waiting the way the arcade waited.
3. **THE FRAME BUDGET, DECOMPOSED.** DONE on our side (LOOP27 3: 250 / 126 / 83 / 65). The arcade-side DTACK conversion still wants jtframe's module. Instruction-cycle cost of the
   game's pass with no stalls (MAME 68K cycle count between IRQ4 entry
   and the frame-flag set, thunks and stalls subtracted) versus the same
   pass on the arcade's model (10 MHz with the per-region DTACK cycles
   from the mapper table). This is the number that ends the "10 vs 7.67"
   argument: the arcade's EFFECTIVE budget per frame, and ours.
4. **THE GATES.** DONE for the text writers (LOOP27 4): TXTWRAM=1 stages the credit line + health bar in WRAM, 49.7 -> 66.4% walk, 48.5 -> 57.3% idle; remaining spin = the upload dispatcher 0x3716 in heavy frames (window length + handler diet).  For each stall found in 1-2: can it be moved out of the
   game's pass (batch the thunks, sprite list to work RAM with an SH-2
   pull, hold FB writes outside the SH-2 span, answer the MCU handshake
   earlier), one flag per lever, measured on the speed ladder.
5. **RECOMPILE DECISION.** Only if 1-4 leave the frame above the
   threshold: the opcode/addressing-mode census for a static 68K -> SH-2
   translator (HANDOFF-SESSION8 section 5's note), sized from the census
   this arc produced.

## Gates on the arc

- Every lever ships behind a flag until Mike's play pass; the ship line
  is untouched by this arc until then.
- `tools/gameplay_speed.py` before/after, same 2600-vint window, control
  re-run the same day (the scripted play drifts ~1 point between roms).
- Fidelity: attract scorecard equal to control rows; `mdstatic_gate`
  fallbacks 0 at the level; `state_frame --report` on Mike's states.
- Region guard (`_end` under 0x06019000) and stamp `normal` as always.

## Where things are

- Loop log: `docs/log/LOOP27.md` (this arc's entries, newest last).
- Scripts: `tools/ghidra_run.sh`, `tools/ghidra/timing_census.py`.
- Census output: `docs/audit/timing_census.json` (local, gitignored).
- Ghidra project: `$GHIDRA_PROJ` (default under the session scratch dir;
  re-import with `tools/ghidra_run.sh import` on a fresh machine, ~1 min).
- Prior art to read first: HANDOFF-SESSION8 sections 3 and 5 (the
  threshold law and the 68K-side lever list), NOTES.md UNPAIR and the
  MCU section, `tools/fmgate_derive.py`'s header.

## KICKOFF FOR THE NEXT SESSION (written 2026-09-07 night, after question 4)

**MiSTer FPGA HARDWARE (2026-09-08, LOOP27 entry 10) — READ FIRST NOW.**
The port BOOTS AND RUNS on a real 32X FPGA core (srcref/S32X_MiSTer),
first time ever. A colour-beacon bisection (BOOT_* Makefile flags) proved
boot, both SH-2s in SDRAM, DREQ landings (word 20 byte-identical to ares),
flips, FB writes, MD-plane DMA, timer rate, V-ISR, compose/blit content,
and the MD text plane ALL work on hardware. TWO takeaways:
  1. SHIPPABLE FIX: the slave SH-2 hangs jumping into SDRAM unless an
     SDRAM stub runs first (ares never needed it). Fold the warm-up into
     mars_start.s for every build.
  2. BLOCKER: the 32X framebuffer LAYER does not reach the screen while
     the pipeline runs. Gate + mode register RULED OUT (BOOT_GATEOFF and
     BOOT_MDMODE both still black). Suspect = blit target-bank parity vs
     the displayed bank under the per-vint flip = the entry-9 flip
     question from the hardware side. Next probe: read the DISPLAYED bank
     right after a real flip, blit-parity-aware (the no-flip probe was
     unsound). This supersedes the item-3 text-in-packet work: getting a
     picture on hardware comes first.


**MiSTer WILL NOT BOOT (2026-09-07 ~23:40):** backrooms (32x-builder,
1.4 MB, same marsdev header template) boots on Mike's MiSTer; ours does
not. Header/vectors/region identical. Two structural differences:
ours is a 4 MB cart (bank 3 of the 0x900000 window holds the game
image) and the BIOS-copied stub is 0x464 bytes at ROM 0x2468D4 (past
2 MB) after which the master copies .ramtext from ROM 0x246EA8 itself.
Probe: `make ship-us BOOTBEACON=1` -> rom/s16_beacon.32x (boots on
ares): backdrop BLUE = 68K waiting for M_OK (green tint 1-4 = the
master's progress on COMM12: started / bss / ramtext copied / past
handshake), GREEN = M_OK seen, YELLOW = S_OK seen, WHITE = into main.
The 68K's M_OK/S_OK waits are unbounded.

**THE 60 Hz FREEZE (LOOP27 entry 9, ~23:15): FLIPS.** With tears gone the
32X layer still holds 10-15 frames: the framebuffer flips on 17% of vints
(ship line 41% = every game frame at 30 Hz) because the K2FREE edge guard
declines flips that miss the 38-line vblank, and at one frame per vint
the post (~27 lines) + pre-flip span (~51) miss it. Neither CPU is loaded
(slave 17%). Levers a/b/c in entry 9; Mike decides (or PACE30 = clean
30 Hz display while the 68K runs at 60).

**ITEM 2 CLOSED (LOOP27 entry 8, ~22:30): ARMGATE=1** — tears 172 -> 1 on
the wolf script (headless DOES tear; entry 7's zero was a trace-column
error). Cost: 2.5% of vints ship no packet (announce serviced late).
rom/s16_txtwram_opt1.32x now = TXTWRAM + option 1 + ARMGATE; Mike plays
it on the DEBUG build (`~/bin/aresrom rom/s16_txtwram_opt1.32x`).

**TEAR FOUND AND FIXED (LOOP27 entry 7, ~22:00):** Mike's census state
showed 88 of 97 tears as landings LONGER than the tag — pushes into an
unarmed DMA after the V-ISR's stale bail skipped the announce. ARMGATE=1
(arm at ack on the SH-2, push only after the arm echo on the 68K) is in
rom/s16_txtwram_opt1.32x with option 1; awaiting Mike's pass.

**SECOND PASS (LOOP27 entry 7):** Mike's pass on the option-1 rom still
shows frozen sprite lists (player x held 6-18 frames, then jumps) with
134 torn landings (5.7%) in his GUI session. Headless, beam-stamped DREQ
and COMM traces on BOTH builds and BOTH scripts show ZERO tears and zero
FIFO drops; the extra DIAG[17] was deferred windows (121 per 2600 vints
on opt1 = dropped frames, the window-length lever). The 32X core is the
same in upstream v148 and the fork. Open: which ares app Mike runs, and
a state saved right after a freeze. TXTWRAM fix landed: the health-bar
copy now selects P1/P2 (phantom orbs). rom/s16_txtwram_opt1.32x rebuilt.

**OPTION 1 BUILT (LOOP27 entry 6 results):** `rom/s16_txtwram_opt1.32x`
= TXTWRAM + LATESTEAL0 + LATEKEEP + DRAWADOPT: late-claim failures 67 -> 0,
shadow-ramp draws 893 -> 114 (ship line 19), attract/mdstatic gates
equal. Residual = a harvest/compose race (claim from SPR_LAND before the
SPR_SNAP copy; not built). Next after Mike's pass: that refactor, then
entry 5 b/c (torn landings, text in the packet).

**ITEM 1 RESULT (2026-09-07 late night, LOOP27 entry 6):** the sprite
ramp draws are FAILED late pair claims — capacity (5 tile-class pairs +
~8 live sets of 14) — not torn landings, not slave cache, not parity.
Fix options 1-4 in entry 6 are Mike's design call; SPRMDFREE=1 is built
and slightly positive. ALSO: the cadence swings 49-74% between builds
that differ by census code alone — the window length is on a knife
edge; A/B sprite work through SPRLATE counters, never speed.

**PLAY-PASS UPDATE (2026-09-07 ~20:30): TXTWRAM FAILED Mike's ares pass** —
sprites drawn as red silhouettes / missing, and the post-coin card's
palette (the latter is pre-existing: his own ship-line capture shows the
identical card). LOOP27 entry 5 has the measured diagnosis: the first
60 Hz session shows the SH-2 sprite pair map missing sets 2.5x more and
drawing them in the shadow ramp 13x more (SPRLATE[3] 19 -> 247), torn
landings 2x, and the shim's FB text copy delays the post (ISR flips
40% -> 21%). Item 3 below is superseded by entry 5's a/b/c: fix the
SH-2 side for one-cycle-per-vint first, move the text footprints into
the packet, then re-run the pass. The 68K side of question 4 is done.


Read LOOP27.md entry 4 first; it corrects entry 3's numbers. Then:

1. State of the trees: this repo still has the uncommitted edits listed
   below (docs move etc.) plus tonight's: `tools/frame_timeline.py`
   (new), TXTWRAM in `tools/patch_game.py` / `tools/game_altbeast.py`
   (`TXT_WRAM_WRITERS`) / `Makefile` / `md_src/md_main.c`, LOOP27 entry
   4. `~/src/ares-debug` (uncommitted, built into
   build_macos/headless-ui/Release/ares-headless): the read/write hooks
   PLUS `--trace-access lo:hi:name:fa:fb` + `--trace-access-out`
   (beam-stamped 68K accesses), `vdpBeam()` in system.cpp, and v,h
   columns on `--trace-comm`. Mike commits when he asks.
   `rom/s16.32x` is the ship line (restored); `rom/s16_txtwram.32x` is
   the probe for Mike's pass. `.build_flags` still says TXT_WRAM — the
   next `make` rebuilds everything, as designed.
2. THE PICTURE (LOOP27 4): per vint the shim runs to line 67-87, the
   SH-2 window is posted at ~20 and acks at 90-250, the game's pass
   starts 2 lines after IRQ4 returns. The spin is whichever FB writer the
   pass reaches first while the window is up. The text writers at the
   pass top are gone (TXTWRAM); what remains is the upload dispatcher
   (gate #13, 0x3716) at pass +105..127 in HEAVY frames (game work ~185
   lines), where the window acks at 198-249. Light frames (~147 lines)
   are one vint now.
3. NEXT LEVERS, in order, each measured with gameplay_speed (walk AND
   idle) + frame_timeline + write_census --reads + attract + mdstatic:
   a. The handler diet (HANDOFF-SESSION8 5a): shim 74 -> ~50 moves the
      heavy pass start from ~100 to ~75; the dispatcher then lands at
      ~180-200 — right at the ack. Measure, do not assume.
   b. The window length: the SH-2 span on a new-frame vint is 170-230
      lines. ROWGEN=1 is built and exact; NOBLIT prices the ceiling.
      Every line off the ack moves the dispatcher out of the spin.
   c. The dispatcher family itself (#13 0x3716, #16-19, the loop heads):
      they write tile pages and text through the FB. A WRAM staging like
      TXTWRAM does not fit (4KB pages); the SH-2-side FM handshake
      (SH-2 raises FM only when it needs the FB, 68K publishes span
      busy via a COMM word + TRAP-hooked span exits) is the general fix
      and a real protocol change — only after (a) and (b) are measured.
4. Open items from LOOP27 4: fmgate_derive fall-through entries (audit
   the 8 remaining spans); the 0x1200..0x168E copy-loop callers that hit
   #20 mid-window (1 in 48 frames); the 40-word health-bar copy.

Commands:
    python3 tools/frame_timeline.py ROM --a 2400 --b 2412 --out DIR
    python3 tools/gameplay_speed.py ROM --out DIR [--input discover/inputs/idle_level1.csv]
    python3 tools/write_census_ares.py ROM --reads --out DIR
    python3 tools/mdstatic_gate.py ROM --frame 350 --frame 1321 --out DIR
    python3 tools/attract_parity.py ROM --out DIR
    make ship-us TXTWRAM=1        # the probe line

## PREVIOUS KICKOFF (2026-09-07 late), kept for the tree-state notes

 (written 2026-09-07 late, context handed off)

Start here, in this order:

1. Read LOOP27.md entries 1-3 (the decomposition) and HANDOFF-SESSION8
   sections 3 and 5 (threshold law, handler levers). Do not re-derive.
2. State of the trees: this repo has uncommitted edits from three
   sessions (the docs/ move, README/Makefile path rewrites, my
   ARCHITECTURE section 0, .gitignore, tools/). `~/src/ares-debug` has
   the read/write hook UNCOMMITTED in cpu.hpp, cpu.cpp, bus.cpp,
   system.cpp, headless-ui/main.cpp (plus Mike's HANDOFF-ANSWERS.md).
   Mike commits when he asks; do not commit unprompted. The built
   binary at build_macos/headless-ui/Release/ares-headless already
   carries the hooks.
3. QUESTION 4, the text-writer gate (game site 0x3AC8, gate thunk
   #22 at 0xFFBE4C; the FMGATE machinery is tools/fmgate_derive.py +
   patch_game.py "LOOP 23 FMGATE"). Measure, one flag each, on
   `tools/gameplay_speed.py` (control 50.4 on the ship line) AND
   `tools/write_census_ares.py ROM --reads` (the FM-spin count must
   fall) AND the fidelity gates (attract scorecard, mdstatic_gate):
     a. FBTEXT=0: text RAM back in a WRAM mirror, shipped by the shim.
        The gate on 0x3AC8 becomes unnecessary (no FB access); the shim
        handler grows by the text push (LOOP-era number to re-measure).
     b. WRAM text staging with the copy done inside the shim's own
        window: text writes never touch the FB from game context.
     c. Move the SH-2's FM span later in the vint so the text writer
        (early in the main loop) runs before the window: the census's
        per-vint fetch pattern tells where in the pass the gate is hit.
   Hypothesis to write down before building: (a)/(b) remove ~83 lines
   of spin on vint B but do NOT change the 2-vint cadence alone
   (313 > 262); the win shows as vint-B slack growing and as points
   only once the handler diet lands. Expect ~0 speed points in
   isolation — record it as such, it is not a negative.
4. Then the handler diet (HANDOFF-SESSION8 5a: palette compare off the
   68K). Both together are the 60 Hz case on paper.
5. Only if 3-4 leave the frame above threshold: question 5, the
   recompile census (tools/ghidra/timing_census.py already lists the
   opcode-bearing instructions; add an opcode histogram script).

Commands:
    python3 tools/write_census_ares.py rom/s16.32x --reads --out DIR
    python3 tools/gameplay_speed.py ROM --out DIR
    python3 tools/mdstatic_gate.py ROM --frame 350 --frame 1321 --out DIR
    python3 tools/attract_parity.py ROM --out DIR
    tools/ghidra_run.sh import   # once per machine; census/script after
