# The sprite pipeline once the game runs on the SH-2

**Status: Mike's framing, 2026-09-27 — "we have a BRAND new need to redefine the sprite
pipeline and it might be the best thing we've had available to us. we can do anything so
long as it's fast enough to fit in a vint."**

## 1. The pipeline is not a design, it is a workaround

Every piece of the current feed exists because **the SH-2 cannot read MD work RAM**, so the
game's state has to cross a bus boundary before it can be drawn:

* DREQ packet transport, and partial landings (`drq_probe.py`: ares read 233 of 234 short
  pushes at their true length, MAME 62 of 62 as zero)
* the lost-push belt — black sprites are a palette push lost on the FIFO
* staging pages, `FBXSTAGE` / `FBXPEND` / `FBXECHO`, the pre-flip lift
* the **2-word sprite record** of `SLIM-PIPELINE.md` — a COMPRESSION FORMAT, chosen to fit a
  packet, not because it is what a renderer wants
* the proven 512-words/vint throughput budget — a TRANSPORT limit
* `MDBATCH`, the packet-B corruption that parked `TWOPOST`, R60's torn packets, FM-window
  contention for the push

**It costs 2,882 instructions/vint — as much as the game itself (2,780).** CLAUDE.md already
says the pipeline IS the project. With the game on the SH-2, the boundary is gone and so is
all of the above, including its entire bug class.

## 2. The budget

    SH-2 per vint                 384,012 cycles  (23.01 MHz / 59.92)
    game logic                    ~2,780 68K instructions' worth of work
    cache                         4 KB, 4-way, 16-byte lines, 64 entries,
                                  entry = (addr >> 4) & 0x3F   (SH7604 8.1)
    cart miss                     ~5x an SDRAM miss (4.96 ares / 4.73 BlastEm)

Running the game on the SH-2 costs a few percent of that budget. **It buys back 2,882
instructions/vint of 68K work and deletes the transport entirely.** What it does NOT make
cheaper is the pixel work: the blit is load-bound (`frame-threshold-law`) and stays exactly
as expensive. This is a change in what the renderer READS, not in what it writes.

## 3. What becomes possible

Each of these is currently impossible *because of the transport*, not because of the SH-2:

1. **Render straight from the object table.** No intermediate record, no packing, no
   unpacking. The 2-word format can go.
2. **Priority and ordering in place.** The game's own order list (`order_list_insert`) is in
   SDRAM next to the records; today the order has to survive a packet.
3. **Real scale values.** `release-bar-flawless` records scale stepping as a MUST-FIX. The
   steps come from a compressed record; with the game's own scale word available per sprite,
   the artefact has no reason to exist.
4. **No throughput ceiling.** 512 words/vint was the transport's limit. The only remaining
   limit is the vint itself.
5. **Per-sprite decisions at draw time** — clipping, shadow, cset, plane — with the full
   record in hand rather than whatever fitted the packet.

## 4. What is NOT settled by this

* **The 128 -> 144 byte re-stride stops being an optimisation and becomes structural.** With
  the renderer and the game logic sharing one 4 KB cache, a stride whose `S/16` is even puts
  8 records in 4 ways at every field offset (derived today, `SILICON.md` §6e).
* **Which SH-2 runs the game** is still open (`FULL-PORT.md` §6(c)); `SELFCHAIN` falsified
  "put it on the idle slave".
* **This reverses `REBUILD.md` §1's premise** — "we run the original System 16 68K binary on
  the MD 68K, that is the project's premise and its fidelity asset". Reopening that is Mike's
  call and `FULL-PORT.md` §2 states it plainly.
* Nothing here is measured. The budget arithmetic is sound and the deletions follow from the
  architecture, but **no frame has been rendered this way.** The gate is still L2 on one
  frame.
