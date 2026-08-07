# LOOP 11 — THE MK2 PIVOT: hold FM, compose direct, delete the blit

Kickoff doc for a FRESH session. Read this, then LOOP10.md's result
sections, then LOOP.md's negatives list. This is an ARCHITECTURE change,
not another optimisation — LOOP 10 ended with Mike saying the port is
"looking better while sacrificing playability" and "framerate in the first
half is kind of ridiculous", and with four separate micro-optimisations
killed by their own falsifiers. The box is full. Stop adding to it.

## WHY: the blit is the tax, and MK2 proves it is optional

Mortal Kombat II (32X, 1995) — fully disassembled, see the technique list
at the bottom — **has no blit at all**. It waits for vblank, toggles FS,
then draws DIRECTLY into the buffer it just hid. No backbuffer, no copy.

We compose into `sbuf` in SDRAM and then copy it to the framebuffer. That
copy is ~56 lines of every 262-line frame with the 68K stalled, it is the
largest pre-ack term, and it is a MEASURED HARDWARE FLOOR — 0.744 lines
per blitted row, 6.76 MB/s, identical in every configuration ever tried,
and DMA is 1.77x SLOWER. It cannot be optimised. It can only be DELETED.

It also forces the 3-window pipeline, which is why a complete frame ships
every 3 vints — **20 Hz by design**, with ~30% of cycles skipping a third
on top. That is the framerate Mike is describing.

## THE BLOCKER, STATED EXACTLY (this is the whole problem)

MK2 sets `FM=1` once at boot and never gives the framebuffer back, because
its 68000 does no drawing. Ours cannot, and the reason is a hard resource
constraint, not a refactor:

**The 68K can only write to two places: MD work RAM (64 KB, and the arcade
game already needs most of it) and the 32X framebuffer via the 0x840000
window. It has NO access to 32X SDRAM at all.**

So the arcade game's video RAM was remapped into the framebuffer:

    game tile RAM  0x840000  ->  MD 0x852000   (FB staging)
    game palette             ->  MD 0x85F000   (FB staging)

(`md_src/md_main.c:21`, `:652-656`.) That is the same trick MK2 uses for
scratch — but for us it means the 68K needs `FM=0` during gameplay to land
its video writes, and the SH-2 needs `FM=1` to touch the framebuffer. Hence
the per-frame handoff, hence the window, hence the 68K stall, hence the
blit. Every cost in the port descends from this one mapping.

## THE PATH OUT — and the evidence it is viable

Get the game's video RAM out of the framebuffer, and FM never has to drop.

Two of the three pieces are **already streamed** and no longer need FB
staging at all:
  - the **sprite list**, over the DREQ FIFO into `SPR_LAND` (working)
  - the **palette**, as region pairs in the same DREQ packet (LOOP 10)

That leaves the **tilemap**. It looks like the hard one and it is not:

> "DIRTY-ONLY page sync (write-observer ring): copy at most 3 pending
> pages per k1 — **steady state is ZERO** (1988 design preloads rounds;
> tile writes happen at transitions)."   — `sh_src/m_main.c`, copy_pages

**The tilemap barely changes during gameplay.** The write-observer ring
that detects dirty pages already exists and already works. Streaming a
handful of dirty pages at scene transitions is a far smaller problem than
the 128 KB-per-frame blit it would let us delete.

## THE PIVOT, IN ORDER

1. **PREMISE VERIFIED (MAME attract, `make TILERATE=1`, 1197 cycles):**

       cycles with ANY dirty tilemap page   4.2%
       pages pending per cycle              0.32
       pages copied per cycle               0.22

   **95.8% of cycles the tilemap does not change.** Streaming a fifth of
   a page per cycle to delete a 128 KB-per-frame blit is not a close call.
   STILL OWED: the same number from ares GAMEPLAY, not attract — Mike's
   complaint is specifically "the first half of level gameplay", and a
   scrolling-heavy stretch could differ from the demo scenes.

   NOTE ON THE FIRST INSTRUMENT, which was WRONG. A MAME
   `install_write_tap` on the MD's 0x840000-0x85FFFF window reported ZERO
   writes — and a control tap on MD work RAM froze at exactly 155531
   across frames 600/1200/1800, proving taps stop firing once the game
   runs through the rebased 0x900000 bank window. The zero meant nothing.
   Always run the control before believing a zero.
2. **Move the tilemap to a streamed path.** Dirty pages go over DREQ or a
   COMM channel into SDRAM, exactly as the palette does now. Retire
   `copy_pages` and FB staging for tiles.
3. **Relocate the two 68K FB READ-BACKS** — the collision `tst.w`s at
   `0x6936+` and the round-transition scratch in page 1 at `0x1B760`
   (LOOP.md iteration 1b). These are the last non-tilemap FB users.
4. **Hold FM=1 permanently.** The window collapses to a vint handshake
   with no stall: the 68K never waits on the SH-2 again.
5. **Compose straight into the hidden framebuffer.** Flip first, then
   draw into the buffer just hidden (MK2's order). The blit is deleted,
   `sbuf` is deleted, and the 3-window pipeline is no longer forced —
   the frame rate ceiling stops being 20 Hz.

NOTE ON THE 85.8% MEASUREMENT. LOOP 9 recorded "composing straight into
the FB: FM=0 writes land only 85.8% of the time, worse than a flat no."
**That was measured at FM=0**, which is the failure mode this pivot
removes. It is not evidence against step 5; it is evidence FOR step 4.
Re-run `make FMTEST=1` with FM held and confirm 100% before building on it.

## MK2 TECHNIQUES WORTH TAKING (all DISASSEMBLED from the 1995 ROM)

1. **Overwrite image at `0x24020000`** — the VDP discards any byte written
   as `0x00`, so transparency is free hardware: no mask, no compare, no
   branch per pixel. MK2 uses it for 37 of its 48 FB references and
   reserves CRAM index 0 permanently. **`compose_sprites` currently gates
   per pixel with uncached destination reads, and sprites are our largest
   remaining wait term (69.6% of it).** Highest-value item on this list.
2. **VDP auto-fill for clears** (`AFSA 0x4106`, `AFLEN 0x4105`,
   `AFDATA 0x4108`, poll `0x410B` bit 1). Zero CPU stores. **Re-arm every
   256 words — auto-fill wraps inside a 256-word block.** Never used here.
3. **35 KB of scratch inside the framebuffer** between the line table and
   the bitmap, physically separate from the 256 KB SDRAM. We are at
   **656 bytes** of region-guard headroom and have been rationing
   `.ramtext` for three loops.
4. **`GBR = 0x20004000`** makes every system register, all 8 COMM
   registers and the VDP block a one-instruction access with no literal
   pool. Same pressure, same relief.
5. **Stride wider than the screen** (MK2: 368 bytes against 320 pixels) so
   sprites hang off the edge with no clipping logic. Costs 10 KB.
6. **Toggle FS with `not` on the low byte of `FBCTL`** — bits 7-1 are
   read-only status, so complementing the byte is a clean toggle with no
   mask and no read-modify-write.

## WHAT MK2 GETS WRONG (do not copy)

It ships 668 bytes/frame through COMM in **67 two-phase handshakes**, with
the master SH-2 spinning inside its interrupt handler and the 68000
spinning in its loop, both blocked the whole time. It never uses the DREQ
FIFO or the SH-2 DMAC. **Our streaming path is already well ahead of the
commercial bar** — that is worth knowing before assuming MK2 is better at
everything.

## GATES (unchanged, and they have overruled the metrics repeatedly)

1. Boots + full attract, no hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build. **Judge the STATICS**
   (title, eyehold). Reference at b6620f4: title 2.44, eyehold 3.37,
   scream 47.14, demo 48.98, demo2 19.35, TOTAL 24.26.
3. Region guard (`grep ' _end$' rom/s16.lst`, limit 0x06019000; currently
   0x06018d70, 656 bytes), rom stamped `normal`, `make PRESSURE=1` builds.
4. **PLAYABILITY — Mike's ares pass is the acceptance gate.**

## THE ares BASELINE THIS PIVOT MUST BEAT (b6620f4, 4365 cycles)

    vints/cycle 3.03    V-gate rejects 1.0%    blit skips 31.5%
    restore past vblank 0.8%   dreq_incomplete 21.4%
    worst handler 253 of 262 lines (margin 9)
    black frames 10.5% of a raw capture
    same-build run-to-run spread is WIDE: blit skips have read 23.5-43.5%
    on the same code. Judge against the RANGE, never one run.

## THE METHOD LESSONS FROM LOOP 10 — these cost the most time

- **BUILD THE FALSIFIER BEFORE THE THIRD FIX.** Three speculative fixes
  were written for a prescan-miss diagnosis that a single marker build
  disproved in one capture.
- **MAME CANNOT SEE THE TERMS THAT MATTER.** `flipwait` reads a flat
  0.000ms and `dreq_incomplete` reads 0 there. But `DIAG[7]` (V-gate
  rejects) DID rank a change correctly — it is a valid cheap pre-ares
  screen. Know which is which.
- **ASK WHAT HAPPENS ON EVERY CYCLE, not just one.** "Skip and leave last
  cycle's pixels" is fine once and catastrophic forever.
- **JUDGE AGAINST THE RANGE.** A single max from the longest run of a
  session is not a trend; it read as an 8-line crisis that 11800 windows
  then showed was a mean of 33.1 lines.
