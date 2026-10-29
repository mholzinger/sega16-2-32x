# THE PORT'S OWN OUTPUT PATH — WHAT IT COSTS

2026-10-02. Costed from measurements taken the same day, each reproducible. This is
a costing, not a plan of record; `docs/design/PORT-ARCH.md` is that.

The port runs a whole arcade frame of decompiled 68K code on the SH-2 and reproduces
16,376 of 16,384 work-RAM bytes (FULL-PORT.md 41). It drives no pixels. This is what
driving them would take.

---

## 1. THE TRANSPORT COSTS NOTHING, AND THAT IS THE WHOLE POINT

The 68K side spends **2,882 instructions per vint** feeding the 32X — as much as the
game itself needs (2,780). `ARCHITECTURE.md` and `CLAUDE.md` both say the pipeline,
not the 68000's clock, is what holds up parity.

**Under the port that number has no counterpart.** The port writes work RAM at
`_WRAM_BASE = 0x0602CC00`, which is 32X SDRAM. The renderer already runs on the SH-2
and already reads SDRAM. Nothing crosses the 68K bus, nothing is packetised, nothing
waits on a DREQ FIFO. The output path is a pointer.

For scale, measured over 256 consecutive arcade frames, one frame changes:

    object records   13.6 B      globals          8.9 B
    low 0xFFC000      9.1 B      SPRITE RECORDS   4.5 B
    0xFFE000 block    2.4 B      stack/top        1.0 B
    TOTAL            39.7 B mean, 139 B max,  of 16,384

**Forty bytes a frame.** 2.3 KB/s at 60 Hz. The thing the 68K spends half its budget
shipping is forty bytes, and under the port it is already in the right memory.

## 2. TIME: 27% OF A VINT, MEASURED

    one real arcade frame on the SH-2      2.88 ms   (ares, PORTSEED=64)
    the hardware ratio                     1.59x     (FULL-PORT.md 11)
    -> on hardware                         4.58 ms   = 27% of a 16.67 ms vint

So the game logic fits with room, which was already the finding at the four-call
body (2.07 ms) and survives the whole real frame.

### THE RENDERER, MEASURED — AND THE PORT BELONGS ON THE SLAVE

`ares-headless --profile` over 1,200 frames of the line, cycles attributed by symbol.
The SH-2 budget is 23.01 MHz / 60 Hz = **383,500 cycles per vint per CPU**:

    master   383,664 cyc/vint   NO dominant spin (biggest single PC 0.9%)
                                -> effectively 100% COMMITTED
    slave    383,664 cyc/vint   187,945 of them (49.0%) in ONE nine-instruction
                                loop at _s_main+0x150..0x160 -- a wait
                                -> 195,719 cyc/vint of real work, 51%

**The master has nothing to give. The slave is half idle.**

    the port's frame      2.88 ms ares x 1.59 hardware = 4.58 ms = 105,441 cycles
    the slave's idle      187,945 cycles per vint
    -> the game logic is 56% OF THE SLAVE'S IDLE TIME, with 44% to spare

So the answer to "can the SH-2 carry the game as well as the picture" is **yes, on the
SLAVE**, and the master's renderer is untouched. That reframes the output path: it is
not a question of finding budget, it is a scheduling question -- the slave's idle is a
WAIT on the master, so the game frame has to finish before the master needs its
output. A rendezvous, not a shortage.

Caveat kept for accuracy: ares charges instruction cycles only. The 1.59x factor was
measured on real hardware for exactly this decompiled code (FULL-PORT.md 11), so it is
the right correction, but it is one number standing in for cache and SDRAM behaviour.

## 3b. SUPERSEDED 2026-10-03 — FREE-RUN IS NOW NET CORRECT

Section 3 below is kept as written because it is the measurement this document was
costed against. It is no longer the state. Four defect classes in the GENERATED PORT
were fixed on 2026-10-03, each provable from the 68K's own instruction bytes
(FULL-PORT.md 53-60):

                                 free-run mean   net        per-frame mean   net
    2026-10-02, section 3           138.8        +1.7          18.0        -25.8
    2026-10-03, after the four       45.2       -91.9          12.9        -30.9

Free-run is net NEGATIVE at every sampled frame, f1 (-37) to f175 (-182), and
per-frame's last frame is THREE wrong bytes of 16,384 against a control of 53.

**AND THE HONEST LIMIT.** A 250-frame endurance run (the captures end at 9256) scores
248 wrong against a control of 240 -- net +8. The game's own frame counter matches the
arcade exactly (1473 both), so the port runs the logic; it drifts. **So the port is
strongly correct for about 180 frames (3 seconds) and back to parity with doing
nothing by 250 (4.2 s).** A playable build needs far longer, and the gate on that is
now ONE object: the port kills the player at free-run frame 109 (FULL-PORT.md 60).

The defect classes, because they are the reusable part:

  1. **41 misaligned 32-bit globals, 231 refs.** An SH-2 `mov.l` at an address 2 mod 4
     is a CPU address error. Only a packed struct + `asm()` name + macro makes gcc
     emit byte-wise code; `__attribute__((aligned(1)))` on a scalar is IGNORED.
  2. **A 68K pointer held in a LOCAL across two statements**, dereferenced
     untranslated -- the two-statement form of a rule the generator already had.
  3. **CPORT_HI**: a 16-bit access at a long's own address is its HIGH half on a
     big-endian machine, and C's narrowing takes the LOW half.
  4. **Handler pointers stored into 68K memory stored an SH-2 address.** 1,224 sites.
     Worth -105 B/frame on its own, and verified closed by a sweep: no 32-bit value
     anywhere in the port's work RAM is an SH-2 address any more.

## 3. ACCURACY: FRAME-BY-FRAME YES, FREE-RUN NO

176 frames, each curve with the frame's own captured controller state:

    PER-FRAME (re-seeded, independent)   mean 18.0 wrong   control 43.8   net -25.8
    FREE-RUN  (seeded once, compounds)   mean 138.8        control 137.1  net  +1.7

The port is solidly correct frame by frame — better than the arcade's own delta, over
176 frames, not just at the seed. **It does not hold a free-run.** +1.7 B/frame against
the control is enormously better than the +9.9 of 2026-10-01, but it is still
divergence, and a playable build IS a free-run.

**This is the gap, and it is the one that matters.** Everything else here is
engineering; this is correctness.

## 4b. CART SPACE, MEASURED (2026-10-03) — 251 KB OVER, AND THE TILE-ART BUILD LINKS

Section 4 below was a COUNT, and it was wrong in two ways: it listed `.text` AND the
art blobs separately when **`.text` already contains 2 MB of raw art** (tiles.bin
1024 KB + sprites.bin 1024 KB, which the SH-2 software renderer reads), and it never
counted `.gamehigh` as a saving. From a build that actually links:

    .text      2446.6     .tilesmd    506.5     .portrom    256.0
    .ramtext     27.0     .mdsprart    20.5     .portcode   420.1
    .data         1.1     .palscenes    4.2
                                                          --------
    without .sprbake                                        3682.0 KB   LINKS
    + .sprbake (baked sprite art)                            665.5
                                                          --------
                                                            4347.5 KB  vs 4096
                                                                       251.5 OVER

`.portdata` (50.6 KB) is measurement-only, so the real gap is **~201 KB**.

**`NOGAMEHIGH=1` is what made the tile-art build fit.** Under the port the 68000 does
not execute the game -- it keeps the i8751 shim, the VDP and the framebuffer handoff --
so the 256 KB REBASED 68K image is dead weight.

    make line NOSPRBAKE=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 NOGAMEHIGH=1 \
         RIGBARCODE=1 WRAMCODE=1

links with the full 506 KB baked tile blob in the cart: the first port rom carrying
real art. It needed `.tilesmd` moved 0x26A000 -> 0x26C000 (and `.porttest` with it),
CONDITIONAL on PORTFULL so the accepted line stays byte-identical.

THE REMAINING ~201 KB, and the levers are not the ones section 4 names:
 * `.sprbake` 665 KB is BAKED sprite art for the MD sprite plane; `sprites.bin`
   1024 KB inside `.text` is the RAW art the SH-2 composes from. Whether both are
   needed once the port runs is an ARCHITECTURAL question about how much of the screen
   the MD planes carry, not a packing one.
 * `.portdata` 50.6 KB goes for free.
 * `.portcode` is 420 KB at -Os for a 716-function closure; a tighter root set was
   already measured NOT to be a lever (section 4 below), but WRAM-resident hot code
   was never priced.

## 4. CART SPACE: ABOUT 290 KB OVER, ON A FIRST COUNT

`.sprbake` and `.portrom` both want 0x02340000, which is why PORTFULL needs
`NOSPRBAKE=1`. A PLAYABLE port build needs both. Counting what a playable build
wants against the 4 MB cart:

    .text (renderer + MD side)   2,505 KB
    .tilesmd  (baked tile art)     506 KB
    .sprbake  (baked sprite art)   681 KB
    .portrom  (the 68K image)      256 KB
    .portcode (the port, -Os)      412 KB
    .palscenes + .mdsprart          26 KB
                                 --------
                                 4,386 KB   against 4,096 KB  ->  ~290 KB OVER

`.portdata` (52 KB) is measurement-only and goes. `.gamehigh` (256 KB, the REBASED 68K
program for the MD's bank window) is already excluded above: under the port the MD
does not run the game, so it should go too — that is a saving already counted, and
without it the gap would be ~550 KB.

Levers, largest first: the two baked-art blobs (1.2 MB together), and whatever of
`.text` is packet transport that the port makes unnecessary.

**A ROOT CENSUS IS NOT THE LEVER I SUGGESTED IT WAS.** Measured: the seeded frame
executes **73 of the 1,464 functions in `.portcode`, 20.5 KB of 471 KB**. The other
450 KB is dead FOR THAT FRAME, not dead for the game -- a different game state reaches
different code, and 720 of the 846 decompiled functions are already rooted, so the
headroom there is small. What the 5% figure is actually good for is the opposite
question: **the per-frame working set is 20.5 KB**, which is small enough that a
WRAM-resident hot set is worth pricing, the way `WRAMCODE` already does for the 68K.

## 5. WHAT HAS TO BE BUILT

1. **Point the renderer at the port's work RAM.** Sprite records at 0xFFF800, the
   palette, the tilemap and scroll — read from `_WRAM_BASE` instead of from what the
   68K staged. This is the output path, and it is a source change, not a mechanism.
2. **Boot and free-run.** `main_entry` (0x005BE) once, then `frame_rotated` (0x0AE2)
   per vint. Free-run RUNS today — 900 frames with no hang — it is the ACCURACY that
   is unfinished (section 3).
3. **Real controller input.** The MMIO stubs take the arcade capture today
   (`tools/l2_io_pack.py`); they need the live pad. The cell addresses are already
   fixed and placed (`DAT_00c41003`, `DAT_00c41007`, ...).
4. **Cart space** (section 4).
5. **The MD side keeps** the i8751 shim, the VDP, and the framebuffer handoff, and
   loses the game and the feed.

## 6. WHAT TO MEASURE NEXT, IN ORDER

1. ~~The SH-2 renderer's per-frame cost~~ — **DONE** (section 2). The master is fully
   committed; the slave is 49% idle and the game logic is 56% of that idle.
2. ~~**The free-run gap** (+1.7 B/frame)~~ — **LARGELY CLOSED 2026-10-03**, see 3b:
   -91.9 B/frame over 176 frames, net negative at every sampled frame. What is left
   is ONE object (the player, from free-run frame 109) and the 180-frame horizon.
3. **The slave rendezvous.** Its idle is a WAIT on the master, so the schedule, not
   the budget, is what has to be shown to work.
4. ~~A `.portcode` root census~~ — **DONE**, and it is not a cart lever (section 4).
   The 20.5 KB per-frame working set is worth pricing as WRAM-resident code instead.

---

## 7. THE OUTPUT SURFACE EXISTS AND IS NOW LAID OUT CORRECTLY (2026-10-02)

Two facts settle what the output path actually is.

**The port's 16 KB work-RAM block IS the complete game state.** Scanned the whole
decompiled program: it touches 68K work RAM 0xFFC000..0xFFFF82 and **nothing below
0xFFC000** — 383 distinct symbols, zero outside the block. The `0xFF7000` and
`0xFFBA00` that `r60_push` reads are OUR SHIM's staging, not the game's.

**And the game's VIDEO writes already land in SDRAM the SH-2 can read.** The game
writes the System 16 hardware addresses; `cport_layout.py` places those in the port's
MMIO block. After one seeded frame:

    tile/text 0x400000    69,632 B    4,833 B of content   0940 0940 0940 0941 0942 0943
    sprite    0x440000     2,048 B       25 B
    palette   0x840000     4,096 B        5 B
    I/O sparse             1,024 B        5 B

So the renderer's future input is already being produced, in the right memory, with
nothing to transport.

### TWO FAULTS FIXED TO GET THERE

**The regions were not reserved.** `_MMIO_BASE` and `_WRAM_BASE` are absolute symbols
PROVIDEd by `layout.ld` — hand-picked addresses in the gap above `.bss`, invisible to
`mars.ld`. Nothing stopped `.bss` growing into them and nothing stopped anything else
using the gap. Now `.portmmio` (0x06019000, 0x12C00) and `.portwram` (0x0602CC00,
0x4000) are declared NOLOAD sections with ASSERTs.

**Page compaction broke the tilemap.** The first layout gave every distinct 68K page
its own 4 KB slot, on the argument that the program does arithmetic within a page and
never between pages. **That is false for the tilemap**: 0x400000-0x410FFF is one
contiguous 68 KB region the game walks, so a write crossing a 4 KB boundary landed in
whatever 68K page was compacted next — tilemap rows turned up inside the I/O PORTS
page. Each video region now keeps its true size and internal offsets; only the I/O
ports are compacted, at a 0x100 stride since every one sits at offset <= 7.

    79,872 B of video + 1,024 B of I/O  =  0x12C00
    against the 80,896 between .bss and the port's work RAM
