# SLIM PIPELINE — cart art to the VDP in the fewest steps

**The goal, in Mike's words (2026-09-19):** *"get the tile and art data
off the cart directly to the VDP in as few fast steps as possible."*
Accuracy first, then the fewest payload copies the hardware allows.

Everything below is measured. Where a number is an estimate it says so.

## 1. What the hardware allows

| route | payload copies | verdict |
|---|---|---|
| cart → VRAM by VDP DMA | **1** | **BLOCKED** |
| cart → 68K → VRAM | **2** | **available** |
| cart → SH-2 → FB → VRAM | 3 | what ships today |

**VDP DMA cannot read cart** (2026-09-19, ares and FPGA, with an
on-screen control in the same frame: WRAM→CRAM lands, cart→CRAM does
not). The transfer executes; the cart data never arrives. See LESSONS.

**The 68K can read cart BOTH ways on hardware** — corrected 2026-09-19
from digital rig captures, after ares sent me the wrong way:

| route | ares | **hardware** |
|---|---|---|
| bank window `0x900000` + bank 2 | 32/32 | **25-27% every frame** |
| identity map `0x200000` at RV=1 | 0/32 | **9%, one frame of four** |

ares reports the inverse of the rig on the identity map. The bank-window
convention below is still correct and is the more reliable of the two:

    cart address = bank * 0x100000 + winoff
    read it at     0x900000 + winoff        (bank -> 0xA15104)
    bank 3 is the resting value every site restores

**RETRACTED:** this document claimed the route needed no rig test
because `mdspr_upload` uses it and sprite art renders. Instrumented:
`mdspr_upload_pump` runs **zero times in 3000 frames**. It is dead code
and proves nothing. The rig test above is what establishes the fact.

## 1b. STATUS 2026-09-20: the slim pipeline RUNS on ares and on the FPGA

`make line TILESLIM=1 SLIMCAP=40` (rom/night/slim21.32x). Three steps,
one vint apart, each where the hardware allows it (md_main.c, "SLIM
PIPELINE STATE"):

    1. md_consume (top of vblank, FM=0)   copy records out of the FB packet
    2. partb_hook (after the game's IRQ4, before the FM raise)
                                          68K reads the art from cart -> WRAM
    3. slim_dma, from partb_hook of the NEXT vint (after that vint's
       game IRQ4; NOT the consume top -- LESSONS 2026-09-20, the palette
       gate)                           68K->VDP DMA, WRAM -> VRAM
       Inline (unbaked) records are DMA'd from the FB in the consume,
       the FB route's own idiom.

Records are MIXED: a baked set is 2 words (slot|fg<<15, blk*64+code); a
set the bake does not cover ships its pixels as a 17-word record flagged
by bit 14 of the slot word. The 68K walks by flag.

ares, level-1 play script, 1200 frames: scene timer 720 (line 718),
3120 tiles staged = 3110 DMA'd, 0 stray, 5672 px different from the line
at frame 1100 (the one-vint lag), 0 px different at frame 600.
FPGA: the attract demo shows the full level background (99.6% non-black,
the line's colour mix). slim18-20 (DMA at the consume top) showed art
without a palette until the Neff cut; Mike's play pass on slim21 is the
gate. "Slower" on slim18 is unmeasured on slim21.

**Bake coverage, measured:** 2278 of 3120 shipped tiles were INLINE
(unbaked sets, pixels converted by the SH-2), 842 baked. The "SH-2 out
of the payload path" claim holds for 27% of the tiles until the bake
covers the rest.

**What every earlier slim build died of** (LESSONS 2026-09-20): the boot
stack over the FM-gate thunk table, then the unbounded fetch address on
pixel words the fallthrough emitted. Neither was the cart route, the
bank switch, the port writes, or volume.

## 1c. (superseded) the slim build still black-screens on hardware

`TILESLIM=1` renders correctly in ares and produces a **fully black
screen** on the FPGA — 0.0% non-black, so the HUD and both planes are
gone too, which is a wedged or half-disabled vint handler and not
missing tile art.

RULED OUT so far, each with a control:
- **the cart read route** — both work on hardware (above)
- **the bank switch** — `BANKPOKE=1` is the line plus only the
  `0xA15104` switch and restore every vint; plays identically
- **volume** — it fails identically at `SLIMCAP=8` as at 40
- **the record stride** — the second emit site advanced `o` by 17 words
  per 2-word record, structurally corrupting the packet. Real bug, now
  `MD_REC_W` everywhere. Fixing it did NOT fix the black screen.

NOT yet ruled out: the `} else if (0) {` splice in md_main.c leaves the
post-loop tail of the FB branch dead under TILE_SLIM, and the 68K writes
VRAM by PORT WRITES where the FB route uses DMA.

**So 2 copies is the floor**, and it takes the SH-2 out of the payload
path completely.

## 2. RETRACTED: the throughput numbers that motivated this

**The MDBATCH sweep below is VOID and so is the conclusion built on it.**

It was read from `0xFFA246`/`0xFFA248`, which sit in the block
`md_main.c:848` itself calls **clobbered**, and the 16-bit one *wraps*
(the slim route ships >65536 tiles in 3000 frames). A clean 32-bit
counter at `0xFFB200`, validated against a second counter at `0xFFB300`
and a sentinel at `0xFFB308` (both agree, sentinel intact), reports:

    total 3202 tiles over 3000 frames, on only ~398 batches
    PEAK 40 tiles in one vint
    IDENTICAL -- to the byte -- across FB vs SLIM, MDBATCH 24/48/320,
    and blank-batch 40/300

Byte-identical totals across builds that differ is a broken instrument,
not a result. **So it is not established that MDBATCH changes anything,
nor that the packet is the throughput wall.** The 40 is suspicious (688
packet words / 17-word records = 40) but suspicion is not measurement,
and three separate knobs failed to move it.

*What still stands on its own:* the slim pipeline BUILDS and RENDERS
correctly -- SH-2 emits 2-word records, the 68K fetches art from cart
through the bank window, picture verified correct. Two payload copies
instead of three is a structural fact, not a measurement.

*What is needed before this ships:* a throughput instrument that
demonstrably RESPONDS to a knob. Until one exists, no throughput claim
from this document may be quoted.

## 2b. The original (VOID) argument, kept so nobody re-derives it

Black tiles are residency — the art is not in VRAM when the cell draws
(rig census: `noslot` 1.20/frame, `dirty` 1.20/frame, `cut` dead). So
tiles/vint is not a speed metric, it is a **correctness** metric.

MDBATCH sweep (DMACENSUS=1, ares, 3000 frames of level 1):

| MDBATCH | tiles/vint | |
|---|---|---|
| 24 (was the line) | 8.82 | |
| 48 | **18.61** | |
| 64 | 18.61 | saturated |
| 96 | 4.93 | **collapses** |
| 160 | 5.22 | collapses |

8.8 was never demand — it was the cap. The collapse past 64 is the
**688-word packet body overflowing**: at 17 words a record, only ~40
fit.

Raising it to 48 also made the **transformation zigzag draw for the
first time without `CHEVFIX`** (3 → 4 distinct colours of an expected
7). Those tiles were never wrong; they were not arriving.

## 3. The design

    SH-2   writes a 2-WORD record          (was 17 words)
             word 0: slot | fg<<15
             word 1: blk*64 + (code & 63)
    DREQ   ships the packet
    68K    sets the bank, reads 16 words from 0x900000+winoff,
           writes them to the VDP data port

The 68K recomputes the cart address with the same arithmetic
`md_emit_art` does now:

    src = TILESMD_BASE + 5*128*2 + blk*4096 + (code & 63)*64 + (fg ? 32 : 0)

688 words holds **~40 records today, ~344 at 2 words**. The packet stops
being the wall.

## 4. What then becomes the limit, and what is actually proven

`mdspr_upload_pump()` moves **up to 512 words per vint** through the
bank window, once per vint, on the line, working on hardware.

| | words/vint | tiles/vint |
|---|---|---|
| line (MDBATCH=24) | 141 | 8.8 |
| mdb48 | 298 | 18.6 |
| **mdspr_upload's proven budget** | **512** | **32** |
| arithmetic ceiling from vblank | ~1440 | ~90 |

**Target ~32 tiles/vint**, because that is the number shipping code
already achieves. 3.6× the line, 1.7× mdb48.

**The ~90 figure is an estimate with no precedent** — ~200 cycles/tile
against ~18,500 cycles of NTSC vblank. Do not design to it.

**And the 512 is a BURST, not a steady state.** `mdspr_up_left` is set
at scene switch and drains over several vints while the game is quiet;
tile shipping is sustained during gameplay. Treat 512 as a ceiling to
approach and measure, not a budget to spend.

## 5. Gates before this can ship

1. `tools/frametest.py` against the line — `blob_in_cart` first, then
   `black_cells` and `hot_glyph_cells` per checkpoint, anchored on the
   68K scene timer and never on an emulator frame.
2. The 68K's own words/vint, counted, against the 512 precedent.
3. Mike's play pass. Pacing is his instrument; ares charges SH-2
   instruction cycles only and cannot see the tail.

## 6. What this does not fix

The Zeus message (incomplete delivery **and** never cleared — one
defect, not two) and the wolf transformation (pre-existing, sprite
layer, confirmed missing on `bldS`). Neither is a tile-throughput
problem, and neither should be attributed to this work.
