# IS THERE A PATHWAY TO A SINGLE VINT?

2026-10-02. Yes, and the arithmetic closes for the first time. Every number here is
measured and cited; the one term that is NOT measured is named in section 5.

---

## 1. THE RENDERER IS NOT THE WALL — 71% OF PASSES ALREADY FIT IN ONE VINT

`STATE.md:1060`, level-1 demo with the light barcode:

    ares  566 vints: one-vint 242, two-vint 150, three-vint 8  = 71%
    FPGA  440 vints: one-vint  34, two-vint ~170, three-vint 16 = 50%

and the record's own conclusion, same entry: **"The lever we own is 68K lines per
pass."** The SH-2 renderer already draws a frame inside one vint most of the time. The
29% that do not are passes that STRADDLE into a second vint, and what makes them
straddle is the 68K.

(The FPGA's 50% against ares' 71% was traced to the MiSTer core's ROM DTACK wait,
`IF.sv USE_ROM_WAIT=1` — a core rebuild, Mike's call, not silicon.)

## 2. WHAT THE 68K ACTUALLY SPENDS, AND WHAT THE PORT REMOVES

`ARCHITECTURE.md`, our rom at level 1, re-measured with loops expanded:

    executed      12,418 instructions per vint
      frame wait   2,781
      WORK         9,637   =  game 4,904  +  shim 4,733
    budget       127,841 cycles/vint at 7.670 MHz  ->  13.3 cycles/instruction
                                                       (a 14% margin, not a fourfold one)

**The game's 4,904 instructions per vint — 51% of the 68K's work — leave the 68000
outright.** They run on the SH-2 instead. That is what the port IS.

The shim's 4,733 is the pipeline that marshals the game's output out of 68K work RAM
and into the 32X. Under the port that output is ALREADY in 32X SDRAM, at
`_WRAM_BASE = 0x0602CC00`, which the renderer already reads. **The marshalling half of
the shim has no counterpart.** What survives is feeding the MD VDP's own layers — real
work, but no longer gated on the 68K having computed anything first.

## 3. THE GAME FITS ON THE SLAVE, AND THE MASTER NEVER SEES IT

`ares-headless --profile`, 1,200 frames of the line, 23.01 MHz / 60 Hz = 383,500
cycles per vint per CPU:

    master   383,664 cyc/vint   NO dominant spin (biggest single PC 0.9%)  -> saturated
    slave    383,664 cyc/vint   187,945 of them (49.0%) in ONE nine-instruction
                                wait loop at _s_main+0x150..0x160

    the port's frame   2.88 ms ares x 1.59 hardware = 4.58 ms = 105,441 cycles
    the slave's idle   187,945 cycles per vint
    -> 56% OF THE SLAVE'S IDLE TIME, 44% to spare

**The master's renderer is untouched.** The game logic goes where there is already
half a CPU doing nothing.

## 4. THE TRANSPORT IS FREE

One arcade frame changes **39.7 bytes of 16,384** (mean over 256 consecutive frames;
139 max). 2.3 KB/s at 60 Hz. And it does not have to go anywhere: the port writes it
into the SDRAM the renderer reads. No packets, no DREQ FIFO, no bus crossing. The
thing the 68K spends half its budget shipping is forty bytes, already in the right
memory.

## 5. THE PATHWAY, AND THE ONE TERM THAT IS NOT MEASURED

    1. 71% of passes already fit one vint; the renderer is not the constraint
    2. the deficit is 68K lines per pass
    3. the port removes the game's 4,904 instructions/vint from the 68K outright
    4. and the marshalling half of the shim's 4,733 with it
    5. the game logic lands in the slave's idle, at 56% of it
    6. the master draws exactly as it does today

**NOT MEASURED: whether 71% becomes 100%.** Removing the 68K's work removes the
*ranked* cause of the two-vint passes; it does not prove there is no second cause. The
honest claim is that the one lever the record names is the one the port pulls, all the
way, and that no other number in the chain blocks a single vint.

Three things still stand between here and a build that can be measured for cadence:

    the port's free-run accuracy   net correct at 8 frames, diverging by 176
    the output path                renderer reads _WRAM_BASE; boot; live inputs
    cart space                     ~290 KB over 4 MB, the two baked-art blobs
                                   being the levers

## 6. WHAT THE PORT TAUGHT THAT NO OTHER ROUTE WOULD HAVE

The architecture is not what the documents said. Each of these came out of porting the
code and would not have come out of profiling the existing build:

* the 68000 is NOT bus-stalled at 45 cycles/instruction — that figure was a
  loop-collapsing artefact; it runs at 11.7 and our margin is 14%, not 4x
  (`ARCHITECTURE.md`)
* the frame loop is ROTATED around its vsync, so one arcade frame starts at 0x0AE2 and
  not at the loop head (FULL-PORT.md 37)
* the game's whole per-frame state change is forty bytes
* the slave SH-2 is half idle and nobody had looked
* and the gate that measured all of this was rewarding a port that did not run
  (FULL-PORT.md 42)
