# The 32X test cartridge — a machine-characterisation suite

**Status: architecture, nothing built.** Written 2026-09-27 at Mike's request.

## 0. The one design decision that matters

**Build a CHARACTERISATION suite, not a conformance suite.** It must not say
"this machine is correct". It must say "this is what this machine does", and a
separate tool diffs one machine's answers against another's.

This is not fastidiousness. It is the project's own history:

- `cart -> VDP DMA` **does not land**, measured on ares AND the FPGA. A
  conformance suite written from the documentation would have asserted it works.
- ~~the 68K's **identity map at RV=1** reads 0 of 32 on ares and 9% on
  hardware~~ **— WITHDRAWN 2026-09-27, the map does not exist. Nothing in this
  tree sets RV: every write to 0xA15107 is 0 (68S off) or 4 (68S on), T25
  measures the register back as 0x0000, and `md_main.c`'s UNPAIR MODEL block
  says "RV stays 0 FOREVER" by design (NOTES.md "REBASE DESIGN v2"). The probe
  that produced those numbers was testing a map that was never mapped, so its
  divergence measured nothing. See "The RV=1 comments were stale" below,
  while the **bank window** reads 32/32 on ares and 25-27% on hardware. ares
  reports the INVERSE of the rig on one of the two routes.
- the MiSTer core holds the 68K's DTACK for a full SDRAM access on every MD ROM
  read (+18%); **a mask-ROM cartridge adds no wait at all**. Neither is "wrong";
  they are different machines.

In each case the truth came from a DIVERGENCE, not from a spec. A suite whose
output is a fingerprint makes divergence the first-class result. A suite whose
output is PASS/FAIL would have enshrined the wrong answer at least three times.

Corollary: **real silicon is the reference where we have it, and the MiSTer core
is an implementation under test** — not an oracle.

## 1. What a test is

    test := (id, name, bus, clock, run(), control(), readout)

Two fields do the work that this project learned the hard way:

**`control()` is mandatory.** The cart-DMA finding only meant anything because
`WRAM -> CRAM` landed in the SAME FRAME that `cart -> CRAM` did not. A probe
without a same-frame control produces confident nonsense; this repo has a
lesson for it and paid for the lesson twice.

**`clock` must be named.** Three exist and they are not interchangeable:

    MD V/H counters      per-scanline, the 68K's own budget
    SH-2 FRT             master phi/32 (12,052 ticks a vint),
                         slave phi/8 (48,208) -- the slave never sets TCR
    vint boundaries      the only clock the game's frame-flag protocol sees

Counter units have been mislabelled twice in this project (RIGSPAN=4's bytes,
MDA[19]/[20] as a word count over a chunk count). A test that does not name its
clock and resolution is not a test.

## 2. Readout — already solved twice, do not invent a third

**Primary: the name-table barcode.** 80 bits as 40 four-colour cells per row,
six rows voting, drawn unscrolled in the window plane. It survives sprites and
the text layer, and `tools/rig_barcode.py` already decodes it from a screenshot.
That is the machine-readable channel and it needs no debugger, no serial port
and no emulator hooks — so it works identically on ares, MAME, the MiSTer core
and real hardware with a capture card.

**Secondary: the 9-bit CRAM value channel** (`tools/rig_value.py`) for a single
exact number where a whole barcode is overkill.

**Also render a human-readable pass/fail table** from the same data, so the cart
is useful to someone without the tooling.

Three readout hazards already measured, all of which the suite must design for:

- **MiSTer rate-limits screenshots to about one per six seconds.** Shots closer
  together return the PREVIOUS image, which reads as a perfectly repeatable
  measurement. Either hold each result ~8 s or pack many results per frame.
- **Captures can come back stale.** Every frame needs a freshness marker INSIDE
  the 32X layer; an MD-backdrop marker is invisible whenever 32X content covers
  the screen.
- **The FPGA has a layout lottery.** A single launch is not a result; the
  standing discipline is three launches per verdict.

## 3. The suite, by bus

Each line is a test whose mechanism this project has already built or measured.
"lift" means the probe exists as a Makefile flag and would be moved, not written.

### 68K bus
| test | status | note |
|---|---|---|
| cart read via bank window 0x900000 | lift | bank -> 0xA15104, 25-27% on hardware |
| ~~cart read via identity map at RV=1~~ | WITHDRAWN | the map does not exist; see below |
| ROM fetch tax: same loop from WRAM vs cart | lift `BOOTBURNW/R` | isolates the core's DTACK wait |
| 68K FB write at FM=1 | lift | DROPPED on ares and FPGA alike |
| 68K FB write at FM=0 | control for the above | |
| VDP DMA source sweep: WRAM, VRAM, cart | lift `CARTDMAPROBE` + `CARTDMANODMA` | cart does not land; WRAM does |
| VDP DMA cost in active display vs blanking | lift | 384 words halts the 68K ~48 lines active |
| DMA burst placement vs the FM raise | new | after the raise BREAKS the frame on silicon |
| 68K instruction timing census | lift `RIGSPAN=9/13/14` | quarter-lines, cart vs WRAM vs FB |

### SH-2 bus
| test | status | note |
|---|---|---|
| cycles per instruction, cache-resident | lift `SH2BENCH` | 1.125 measured on FPGA |
| cycles per instruction, all-miss | lift `SH2BENCH` | 5.125-8.75, MOVES with contention |
| cache geometry probe: stride sweep | new | 64 sets x 4 ways x 16 B, indexed A[9:4]; a 128-byte stride touches 8 sets |
| SDRAM cached vs uncached (0x06 vs 0x26) | new | |
| slave SDRAM warm-up requirement | lift | the FPGA needs it, ares does not |
| master/slave FRT calibration | lift | phi/32 vs phi/8, 12,052 / 48,208 a vint |

### Inter-CPU
| test | status | note |
|---|---|---|
| COMM register round-trip, both directions | new | and which are claimed by convention |
| DREQ FIFO: full-length push | lift | |
| DREQ FIFO: PARTIAL landing | lift `drq_probe` | ares 233/234 at true length, MAME 62/62 as ZERO |
| FM arbitration: who blocks whom, for how long | lift | |
| interrupt latency, each direction | new | |

### MD VDP
| test | status | note |
|---|---|---|
| CRAM write during active scan vs PEN window | lift `CRAMISR` | 32X CRAM writes need hblank/vblank |
| FS latch timing | lift | ares latches mid-scan and TEARS; FPGA latches in VBLK and defers |
| DMA rate, active vs blanking, by source | lift | |
| plane/window/sprite limits and priority | new | |

### 32X VDP and the composition
| test | status | note |
|---|---|---|
| framebuffer modes, line tables, overwrite image | new | |
| the 32X's 256-entry palette, swept | new | |
| **MD 4x16 9-bit CRAM x 32X palette COMPOSITION** | new | where this project's hardest bugs lived |
| the 3-bit quantiser over all 32 inputs | lift `mdp_quant` | `(v5+2)>>2` clamp 7 |

### Controllers
| test | status | note |
|---|---|---|
| 3-button read | new | |
| 6-button handshake | new | ares default pad is 6-button; a 3-button read misses Y/Z/X/Mode |
| multitap | new | |

## 4. The colour and tile torture pattern

Mike asked for "a high colour palette and tile set to test all available pens".
Be precise about what exists, because the interesting part is the composition:

- the MD VDP has **4 CRAM lines x 16 entries, 9-bit colour** (64 total)
- the 32X framebuffer has **its own 256-entry palette**
- a tile selects ONE MD line for all its pens
- our renderer quantises S16's 5-bit guns to **3 bits** with `(v+2)>>2`

So one screen should sweep, pixel-exact and diffable:

1. all 64 MD entries at all 512 nine-bit values, as a ramp
2. all 256 32X palette entries
3. the composition at every MD-priority x 32X-priority combination
4. the quantiser's 32 inputs against its 8 outputs, with the boundaries marked

That last one is not hypothetical: a red/blue LSB swap and a 3-bit quantiser
boundary each cost a day in this project, and both would be one glance at a
pattern like this.

## 5. Phasing

**Phase 1 — the harness and the lifts.** Registry, barcode transport,
fingerprint file, and the diff tool, plus the ~10 tests marked "lift". This is
mostly moving existing probe code behind one dispatcher, and it is where almost
all the value is, because it makes every future divergence cheap to find.

**Phase 2 — the colour/tile pattern and VDP timing.** Pixel-exact, diffable.

**Phase 3 — controllers, DREQ edge cases, the rest.**

Deliverable per phase: a `.32x` that boots, runs, and prints a fingerprint, plus
`fingerprints/<machine>.txt` for real hardware, MiSTer, ares, MAME and BlastEm,
and `tools/fingerprint_diff.py`.

## 6. What it is worth, and what it cannot do

**Worth:** every hard-won fact in this repo currently lives in prose with a
citation. Prose does not re-run when the MiSTer core is rebuilt, when ares is
updated, or when the next S16B title exposes a different corner. A cart does.
It is also the kit's instrument rather than this game's, so it belongs in
`TOOLKIT.md` and carries to Golden Axe and everything after.

**Cannot:** settle anything about real silicon unless it runs on real silicon.
The MiSTer core is an implementation under test. And a cart cannot tell us which
machine is RIGHT — only that they differ, and where. Choosing the reference is
still a human act, and for this project that reference has always been the
hardware Mike owns.

# The controller UI — text, menus, and why a VALUE beats a verdict

Written 2026-09-27 after checking what the repo already has, which changes what
this costs.

## What already exists, verified not assumed

**A font is already in VRAM.** `md_src/font.s` is 45 8x8 4bpp tiles and
`md_src/md_start.s:99-109` uploads them to VRAM address 0 at boot, forcing
foreground to pen 1. So text costs a name-table write per character and no
upload. Glyph order is a COMPACTED ASCII, decoded from the bitmaps rather than
guessed:

    tile  0      space
    tile  1      '!'
    tiles 2-11   '0'-'9'
    tiles 12-37  'A'-'Z'
    tiles 38-44  ':' '.' '-' '>' '|' '+' '%'

**Constraints that follow, and they shape the wording of every label:**
NO LOWERCASE, no '/', no '=', no brackets. Use ':' and '-'. A 40x28 cell screen
leaves about 38 usable columns, so test names must be short. Uppercase-only
suits a hardware tool, but it is a limit, not a style choice.

**The 32X layer must be blanked for text to show.** This is the thing that made
phase 1's grid paint black: the 32X layer sits ON TOP of the MD planes. Bitmap
mode lives at `0xA15180` from the 68K (the FB control the renderer already uses
is `0xA1518A`); mode 0 is no display, and the MD plane then shows through. So
the UI blanks the 32X layer and only a test that needs it turns it back on.
That is also how the 240p suite's menu can be tile text on a 32X.

**Input already exists.** `read_joypad(0)` in md_main.c, and the button bits are
known from the coin work: 0x0040 A, 0x0080 START, 0x0200 Y. Directions are the
low four bits. Edge-detect on press -- acting on HOLD makes a list unusable --
with a repeat delay for held directions.

## The interaction model: copy the 240p suite

A vertical list, A to enter, B to back, START for run-all. Matching a suite
Mike already uses means the muscle memory transfers, and it is the model every
console test tool uses.

    +--------------------------------------+
    | 32X TEST CART            8 OF 8 PASS |
    |                                      |
    | > T00 WRAM WORD                 PASS |
    |   T01 WRAM LONG                 PASS |
    |   T02 CART BANK LONG            PASS |
    |   T03 CART BANK WORD            PASS |
    |   ...                                |
    |                                      |
    | A:RUN  START:RUN ALL  C:FINGERPRINT  |
    +--------------------------------------+

Three screens, and the third is the one that matters on hardware:

**LIST** -- every test with its verdict, a '>' cursor, and a header tally.

**DETAIL** -- the selected test's MEASURED VALUE beside what was expected:

    CART BANK LONG
      GOT   6000000C
      WANT  6000000C
      VERDICT PASS

**FINGERPRINT** -- every test's value on ONE screen, compact, laid out to be
PHOTOGRAPHED. On real hardware there is no memory dump and no savestate; a
photograph of this screen IS the fingerprint export, and it is the only way a
hardware result reaches `fingerprints/real_32x.txt`.

## The design point: store a VALUE, not a verdict

Phase 1 stores pass/fail. That was enough to get running and it is not enough
to characterise a machine. **A verdict cannot be compared across machines; a
number can.** Two machines that both "fail" a timing test have told you nothing
until you see 48 lines against 12.

So every test gains a 32-bit measured value beside its verdict, and DETAIL and
FINGERPRINT show it. Tests whose answer is inherently a number -- SH-2 cycles
per instruction, DMA cost in lines, the 68K's ROM fetch tax -- then need no
expected value at all: they REPORT, and the diff between machines is the
finding. That is what TESTCART.md means at the top by characterisation rather
than conformance, carried into the data model instead of only the prose.

## Cost, honestly

The font, its upload, and the pad reader all exist, so the UI is a text
primitive, a menu loop, and three screen renderers. The real work is not the
menu: it is converting each test from `tc(ok)` to `tc_val(ok, value)` and
giving every test a short name and an expected value worth printing.

RISK RETIRED 2026-09-27, and three claims fell together in one run.
`*(uint16_t*)0xA15180 = 0` blanks the 32X layer and the MD window plane shows
through; the font's decoded glyph order is right; and the window plane at
0xC000 with reg 0x12 = 0x80 covers the whole screen. The result is a legible
eight-row results table over the verdict colour, and **ares and the MiSTer core
render it PIXEL-IDENTICAL -- 0 of 71,680 pixels differ.** So the UI's
foundation is measured on both machines, not derived, and the text primitive
exists.
