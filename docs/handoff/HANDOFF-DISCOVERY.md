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

- 234 functions, 272 backward branches, 92 static instructions on
  hardware ranges: text RAM 25 writes / 2 reads, tile RAM 23 writes,
  palette 22 writes, sprite RAM 5 writes, I/O 13 reads / 2 writes.
- Vectors: reset PC 0x400; level-4 autovector 0x404 (the IRQ4
  trampoline, NOTES.md); every other level 0x40C (an rte stub).
- 36 tight loops (span <= 40 bytes) whose body touches hardware. Most
  are block fills/copies (`dbf` with a 2-8 byte body writing tile, text
  or palette RAM): the level's tilemap and palette loads. Two are the
  polls: 0x3028 (`bne` over an I/O read, in the coin/service path) and
  the text-RAM handshake family around 0x1500-0x15B2.
- Static counts are sites, not time. The dynamic census (loop item 1)
  is what converts them to lines per frame.

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

1. **WRITE-TAX CENSUS.** Per frame, how many writes does the game make to
   each rebased region (tile RAM, text RAM, sprite RAM, palette, I/O,
   mailboxes), and what does each cost on OUR path (thunk: jsr + OR +
   displaced write + rts vs the original 16-cycle write; FB-window write
   incl. the FM stall; work-RAM mirror)? Output: tax in lines per frame,
   split by region. Method: MAME lua write census by PC over one level-1
   second (the `pal_tap.lua` shape, ranges from the census), costs from
   the 68000 cycle table plus the measured FM stall (session 7's NOBLIT
   +9.7 points prices the stall family).
2. **SPIN CENSUS.** Which of the 36 loops actually spin at runtime, how
   many iterations per frame, and on what: the text-RAM MCU handshake
   (0x410002), the I/O poll at 0x3028, the FMGATE thunks' own spins,
   the frame-flag spin. Method: MAME PC-hit counters on the loop
   branch addresses; ares for the FM-stall side. Output: lines per frame
   spent waiting on US versus waiting the way the arcade waited.
3. **THE FRAME BUDGET, DECOMPOSED.** Instruction-cycle cost of the
   game's pass with no stalls (MAME 68K cycle count between IRQ4 entry
   and the frame-flag set, thunks and stalls subtracted) versus the same
   pass on the arcade's model (10 MHz with the per-region DTACK cycles
   from the mapper table). This is the number that ends the "10 vs 7.67"
   argument: the arcade's EFFECTIVE budget per frame, and ours.
4. **THE GATES.** For each stall found in 1-2: can it be moved out of the
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
