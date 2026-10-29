# HANDOFF — the 68K-time axis: instruments, the core's ROM wait, WRAMCODE (2026-09-24/25)

**STATUS at 12:20 ET 2026-09-25.** The line is rg1 (`make line`), on the
rig, 3/3. Nothing built this session is on the line. Tree committed
locally through ff54025f (native1, ~150 commits ahead of origin; not
pushed, per the standing rule). The rig holds rg1.

Read STATE.md first (THE TIERS, THE AXIS — SPEED, the 60 Hz card), then
LESSONS.md entries dated 2026-09-24 and 2026-09-25 (nine of them). This
file is the map of what those entries add up to, what is built, what is
measured, what waits on Mike, and how to run every instrument.

## 1. What is TRUE now (each with the instrument that established it)

1. **The game's pass never waits on the FM window** on either machine.
   RIGSPAN=2 (max release V of the shared gate per pass) reads 0 on ares
   at four frames and on the FPGA in 22 of 24 samples. The shim does not
   wait at vint entry (fmgate_belt constant). The whole ack/pickup/
   window-end model of 2026-09-23/24 is dead as a frame-rate lever.

2. **The rig barcode was an instrument that cost 48 68K lines a vint**
   (its 384-word VRAM DMA in active display) and turned the line's demo
   from one-vint passes into two-vint ones on ares. Every barcode-era
   FPGA rate number before 17:40 ET 2026-09-24 is handicapped.
   RIGBCEVERY=8 (barcode built and DMA'd every 8th vint) no longer
   perturbs (ares PC ring idle 64/64 again). Use it on every rate rom.

3. **The span hook read the frame flag twice** and lost ~40% of passes
   (a release between the two reads started an unarmed pass). Fixed:
   one read, shared with the game's branch (patch_game.py RIGSPANHOOK,
   the byte at 0xFFA0EE). Every RIGSPAN number before 15:50 ET
   2026-09-24 carried the race.

4. **Light barcode, race-free hook, level-1 demo (step 5):**
   ares 71% (one-vint 242 / two-vint 150 / three-vint 8 per 566 vints);
   FPGA 50% (34 / ~170 / 16 per 440). The FPGA turns one-vint passes
   into two-vint; three-vint is small on both. Title: one-vint on both.
   The scene timer 0xFFF02A IS a pass counter (70% on the line).

5. **Mean pass length (RIGSPAN=11):** ares 232-246 lines; FPGA 358-418.
   The FPGA's demo pass is ~130-150 lines longer. The IRQ4 chain is the
   same length on both (RIGSPAN=12: entry -> shim end 62-72, -> part B
   end 69-96). Single 68K accesses are +5% on the FPGA (RIGSPAN=9 cart
   data reads + WRAM loop, =13 FB writes/reads, =14 the same inside the
   master's blit). MDQUIET=2 (shim DMAs off) and IDLEOFF (no packet
   building) leave the FPGA at ~50% while ares gains from either.

6. **The FPGA term is instruction fetch from the cart, and it is the
   MiSTer core's** (RIGSPAN=15 + CARTBENCH: the same 512-iteration loop
   from the cart 132-148 quarter-lines vs 112 from WRAM; ares 115/112;
   with the SH-2s' art fetches off 130-133). rtl/32X/IF.sv with
   USE_ROM_WAIT=1 (S32X.sv) holds the 68K's DTACK for a full SDRAM
   access (~7 cycles at 107.39 MHz plus two syncs at 53.69 MHz, about
   one 68K clock) on every MD ROM read; the cart ROM is SDRAM channel 1
   of rtl/sdram.sv; the 32X SDRAM is in DDR3 (use_sdr never set). An
   SH-2 ROM request pending at RS_IDLE is granted first (IF.sv:822) —
   that part is real hardware, 0-15% for us. A mask-ROM cartridge adds
   no wait. +18% fixed, up to +32% with arbitration, on fetch-bound
   code; the game runs entirely from the cart.

7. **Tiering (STATE.md THE TIERS, Mike's call):** MiSTer is truth for
   everything except fetch-bound 68K code and stays the acceptance
   gate; ares is closer to hardware truth for fetch-bound 68K code,
   optimistic only by the arbitration share. The core is NOT to be
   rebuilt (fragments distribution, unverifiable, dies with the next
   core update). The port engineers around it.

8. **The 68K-line ranking of the pass on ares** (dump-based, instrument
   cost ~0.3 line): r60_push palette rotor + compares 41 lines a vint,
   its stage ship 18, consumes + sprite pal + pump 15, rowscroll/
   selection 8, part B ~5; the game's own IRQ4 ~20; gate thunks ~7 a
   pass (from OUR rom's counters, COUNTSITES). The chain runs once per
   vint a pass spans. Execution counts must come from OUR rom: the
   arcade trace's text traffic differs (coin state), and 0x3A9A ran 22
   a frame there vs ~7.7 a pass here.

9. **BURSTGATE** (the 0x3A9A text-copy gate once per burst, exact on
   ares by the scene timer): 0 conversions on the FPGA. 2-vint passes
   end spread across their second vint, not piled at the line
   (RIGSPAN=10), so a 3-line cut converts ~2 per 300 vints. Knob, off
   the line.

10. **WRAMCODE=1** (tools/wram_reloc.py: 12 hot blocks, 2968 B, into
    WRAM 0xFF5000, ROM entries `jmp (abs.l)`): exact on ares by the
    scene timer (game WRAM identical below the stack except the
    per-vint palette-cycle slot timers at 0xFFF300; text pages
    identical); FPGA demo mean pass 400/418 -> 356, 358/362 -> 328/332;
    one-vint 34 -> 42, three-vint 16 -> 12 per 440 vints. A quarter to
    a third of the FPGA's excess. Not on the line: awaits Mike's play
    pass and three rig launches of a line candidate.

11. **Dual-chain count (RIGSPAN=16):** 48% of the FPGA's demo chains
    (32% of ares') enter with the game mid-pass; each still runs the
    full push and posts a window the master composes for unchanged
    sprites/scroll. ~33 lines a vint on the FPGA if skipped, ~22 on
    ares. Fidelity trade to rule on: the arcade shows mid-pass palette
    writes during overrun vints; a skipped push delays them one vint
    on those vints only. Alternative that stays exact: palette-only
    push on mid-pass chains.

## 2. What waits on Mike

- **WRAMCODE line candidate:** `make line WRAMCODE=1` (no barcode), his
  play pass, 3 rig launches, then LINE_FLAGS += WRAMCODE=1 and the line
  is named. The tile blob moved 0x268000 -> 0x26A000 for it (mars.ld +
  md_main.c; `make tilesmd-addr` agrees) — that move is already in
  every build, including the current line's rebuild; rg1.32x on the
  rig predates it and is unaffected.
- **The mid-pass push:** rule on the palette-timing trade (item 11).
- **The palette card** (item 1 of the ranking, 41+18 lines a vint) is LIVE
  as of Mike's 2026-09-25 call -- the 2026-08-16 priority order that put
  palette last is discarded, and neither STATE nor this file should have
  recorded it as "held on his word": he had never ruled on it. It is the
  largest item left on the 68K axis and the only one above the rig's
  ~15-line ranking floor. Price it ON THE RIG: O-13's 94.7/82.3 is an ares
  number and does not transfer to this axis. O-12's +C/+E drop belongs
  inside its stage-ship part, not as a separate card.
- **Direction call** (real cartridge / MiSTer / both) — asked, not yet
  answered; the tiering above stands either way.

## 3. Next cards, in order

**Addendum 2026-09-24 evening (this thread):** card 1 is DONE as wc2
(`make line WRAMCODE=1`, second block set from tools/pc_profile.py's
16-line-sampler profile of OUR rom, plus wram_reloc.py's abs.l
call-site rewrite): exact on ares, FPGA mean pass -15 lines, ~20 more
one-vint passes per 900 vints (LESSONS 18:45 ET, two entries). Card 2
(mid-pass push) was RULED palette-only by the decompile and PRICED at
~5 lines a vint on the FPGA -- not built (LESSONS 18:10 ET). The
profile also says the cart residue is now 13% of the vint, so the
FPGA's remaining ~90-100-line excess per pass is NOT cart fetch; the
next instrument is the sampler's class histogram over the barcode
(FPGA vs ares, same rom), a RIGSPAN mode. The launch API no-ops on an
already-running path (LESSONS 18:05 ET): push repeated launches under
distinct names.

**Later the same night (23:55):** (i) the ROM wait covers data reads
(IF.sv:780) but RIGSPAN=9 had priced that half at ~1 cycle a read;
(ii) burst tenure (RS_SH_CONT + 16-byte line fills) tested with
RIGSPAN=17 at raw V 0xF0/0x40/0x10/0x80: 132 everywhere in the demo,
sporadic spikes at any phase -- not the excess; (iii) RIGSPAN=18, the
sampler's class histogram over the barcode, FPGA vs ares on one rom:
only cart game code moves (8 -> 12-17%), and the "60 unexplained
lines" were the second chain of tipped passes; (iv) wc3 (region 2,
WRAMCODE2=1, third block set) exact and 0 conversions: the cart-fetch
axis is spent at wc2; (v) the wc2 purple band did not reproduce (rg1
0/80, wc2 0/60). The 68K levers left are the chain's own lines
(rotor 41 -- LIVE since Mike's 2026-09-25 call, not held; the 2026-08-16
order that put palette last is discarded -- stage ship 18, consumes 15).
All in LESSONS 2026-09-24
19:45-23:50 ET.

1. Second WRAMCODE block set from OUR rom's profile. The residue on
   ares (LESSONS addendum 12:10 ET): pointer-called handlers 0x3352,
   0xEE80, 0xD6D8, 0xD608, 0xDABC, 0x674E, 0xD65C, 0xE2C2, 0x12EC,
   0x103E0, and 0xD5A6 (rejected: its entry takes a branch at +2 —
   start the block at its first safe instruction instead). ~1.3 KB of
   WRAM remains: 616 B at 0xFF5BA0-0xFF5DFF and ~700 B at 0xFF3C30-
   0xFF3EFF (above .bss, below the boot stack at 0xFF4000-0xFF4FF0).
   Profile first: the interrupted-PC ring (0xFFA200, 64 vints) dumped
   on the FPGA rom is the cheaper source (mid-pass 65% of vints there);
   on ares only ~9% of ring samples are mid-pass cart code.
2. Mid-pass push skip or palette-only push (after the ruling).
3. The palette rotor (after Mike's word).

## 4. Instruments (all under RIGBARCODE=1 RIGBCEVERY=8 unless said)

    RIGSPAN=1   byte 5 = raw V at the pass end, byte 6 = vints spanned
    RIGSPAN=2   byte 5 = max release V of the shared gate in the pass
                (needs the RIGWINMAX patcher stamp, auto), byte 6 = span
    RIGSPAN=3   per attract step: span>=2 count/2, span-1 count/2
    RIGSPAN=4   span>=2 count/2, span-0 count/2
    RIGSPAN=5   vints inside passes/4, vints waiting/4 (shim_vblank)
    RIGSPAN=6   interrupted-PC ring classified: ours / idle (rest = game)
    RIGSPAN=7   raw interrupted PC, low 16 bits
    RIGSPAN=8   (dead: no vint without a release while waiting)
    RIGSPAN=9   256 cart reads / 256 WRAM iterations, quarter-lines,
                every 64th vint, past the V jump (waits for V >= 0xF0)
    RIGSPAN=10  span-1 passes ending in lines 0-130 / 131-261 of their
                second vint, /2 each
    RIGSPAN=11  mean pass length in lines /2, passes /4
    RIGSPAN=12  (+CHAINPRICE=1) chain entry -> shim end, -> part B end
    RIGSPAN=13  256 FB word writes / reads, quarter-lines
    RIGSPAN=14  mode 9 run inside the window (waits for raw V 0x40)
    RIGSPAN=15  (+CARTBENCH=1) the same loop from the cart / from WRAM
    RIGSPAN=16  chains entered mid-pass /2, with a fresh frame /2
    COUNTSITES=3A9A,4D88,...   per-gate-site call counters at 0xFFA1F0
    CHAINPRICE=1  V stamps after the shim's vblank (0xFFA3C0) and after
                part B (0xFFA3C2) -- NOTE both sit inside the entry ring
                0xFFA380-0xFFA3FF (slots 32/33); consistent in practice,
                move before reuse. 0xFFA3C4 (an old COUNTBURST word) was
                garbage for that reason.
    Knobs off the line: BURSTGATE, WRAMCODE, IDLEOFF, MDQUIET, POSTPOLL
                (v3: the maps drain yields to a post), TEXTRESTMASK, ...

Raw V to lines after the vint: (V - 0xE0) & 0xFF for V >= 0xE0, else
V + 38; the raw counter jumps 0xEA -> 0xE5 inside vblank (a 5-line
ambiguity for values 0xE5-0xEA).

Every RIGSPAN rom needs the patcher's idle-loop thunk (RIGSPANHOOK,
automatic with RIGSPAN); the thunk region ends at 0xBFF0 and is within
~14 bytes of full with RIGSPAN + BURSTGATE together.

## 5. How things are run

- Ares checks: `ares-headless --frames N --screenshot (N-1):x.png` +
  `python3 tools/rig_barcode.py x.png`; `--dump wram:ADDR:LEN:file`,
  `--dump "32X DRAM:0x1F000:0x1000:file"` (text page, bank 0; 0x3F000
  bank 1). Ares runs are deterministic per rom.
- Same-game-time comparison of two roms of different speed: align on
  the scene timer 0xFFF02A AND the attract step ((0xFFF031>>2)&7) AND
  the frame flag 0xFFF01C == 0 on both, then compare 0xFFC000-0xFFFCFF
  (below the stack) and the two text pages. Never compare at the same
  emulator frame (LESSONS, the BURSTGATE exactness entry).
- Rig: scratchpad rigcap2.sh <rom> 14 36 2 (14 screenshots) then
  `tools/rig_barcode.py --rom <name>`; the rig's screenshot dir keeps
  older files of the same rom name -- dedupe by vint (scratchpad
  spans.py). Every chain ends with `tools/mister_push.sh rom/night/rg1.32x`.
  IDLEOFF roms show a frozen picture on the rig by design; say so.
- Arcade profile: `tools/arcade_trace.lua` (AT_START/AT_LEN), the
  arcade's attract steps by frame: step 5 (level-1 demo) = f1500-2100,
  step 0 = 2200-2700, step 1 (round 1) = 2700-3400, step 3 (title) =
  3900-4500. Use it for routine EXTENTS and structure, never for
  execution counts on our rom.
- WRAMCODE tool: `tools/wram_reloc.py <image> <objdump> FF5000 a-b,c-d
  out.h` (dry run), blocks in tools/game_altbeast.py TABLES
  ['WRAM_BLOCKS'], entries known to be pointer-called in
  ['WRAM_ENTRIES']. Rules and rejects are in the file's docstring; the
  re-disassembly check compares every instruction with objdump's
  resolved target. Region: 0xFF5000-0xFF5DFF (3.5 KB, tile thunks at
  0xFF5E00).

## 6. Rules added this session (LESSONS has each with its measurement)

- Price an instrument on the CPU it measures before reading its result.
- An instrument reading a shared flag reads it ONCE and shares that read.
- Measure the frame, not the window; report the whole span distribution
  with the one-vint column; conversions, not percentages.
- Execution counts come from OUR rom (COUNTSITES), never the arcade trace.
- Gauge instruction fetch separately from data access.
- When a rig disagrees with an emulator by a constant, read the rig's
  RTL for that path; the FPGA is an emulator too.
- abs.w reaches 0xFF8000-0xFFFFFF only; lower WRAM needs abs.l.
- Compare builds of different speed at the same GAME time.
- A predicted saving is not a saving: measure the pass before and after.
