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

**The 68K reaches cart only through the `0x900000` bank window**, not
the RV=1 identity map — measured 0/32 vs 32/32 in the same frame:

    cart address = bank * 0x100000 + winoff
    read it at     0x900000 + winoff        (bank -> 0xA15104)
    bank 3 is the resting value every site restores

This needs no rig test: `mdspr_upload` (md_main.c:449) already uses it,
is on the line, and sprite art renders on the FPGA.

**So 2 copies is the floor**, and it takes the SH-2 out of the payload
path completely.

## 2. Why it is worth doing: throughput IS the defect

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
