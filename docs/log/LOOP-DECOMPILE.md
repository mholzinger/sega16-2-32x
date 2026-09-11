# LOOP-DECOMPILE — the Altered Beast decompile thread

Working log for the thread described in `docs/handoff/HANDOFF-DECOMPILE.md`.
Newest last. Every entry carries the date, the command, the evidence and
one sentence of conclusion. NEGATIVE RESULTS are marked in the heading.
A corrected entry is marked WRONG in place and names its successor.

---------------------------------------------------------------------
## 1. Rig up: Ghidra project imported (2026-09-10)

    tools/ghidra_run.sh import

Auto-analysis succeeded on `roms/altbeast/prog68k.bin` (262144 bytes,
`68000:BE:32:default`, base 0x0). Project lives at
`/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/ghidra_proj`
and is outside the repo, as required.

The linear disassembly used for cross-checks throughout this log is

    $MARSDEV/m68k-elf/bin/m68k-elf-objdump -D -b binary -m 68000 \
        roms/altbeast/prog68k.bin

`m68k-elf-objdump` is not on PATH; it ships with marsdev at
`$MARSDEV/m68k-elf/bin/`. HANDOFF-DECOMPILE quotes the bare name.

---------------------------------------------------------------------
## 2. $0A is the allocated SLOT, not a compile-time palette id — HANDOFF-DECOMPILE is WRONG on this

HANDOFF-DECOMPILE says "`palette_bank` is a COMPILE-TIME CONSTANT per
actor type — the game does `move.w #$5D,palette_bank(a6)` at 0x1FE0".
Our bytes at 0x1FE0 disassemble to

    1fe0:  3d7c 005d 000a    move.w #$005D,10(a6)

which is a WORD write covering both fields: it puts 0x00 in $0A and
0x5D in $0B. All six immediate word writes to $0A in the program put
zero in the high byte, so none of them sets $0A to anything.

$0A is written at runtime by the allocator:

    3b6c:  1d41 000a         move.b d1,10(a6)     slot number
    3b60:  422e 000a         clr.b  10(a6)        on allocation failure

with d1 bounded against 63 (0x3B3E, 0x3B48, against the queue head at
0xFFF400). $0B is the compile-time identity: `RequestPaletteUpdate`
(0x3B2E) reads `move.b 11(a6),d0` and uses it to index the request table
at 0xFFF500, which holds the slot each index was given.

Conclusion: $0B (0..0xAE) is the palette the artist chose; $0A (0..63)
is the hardware line the allocator handed out this frame. A census of
$0A writes answers a different question than the handoff intends.

---------------------------------------------------------------------
## 3. The palette table: 0x242A0, 28-byte stride, 14 pens (2026-09-10)

The queue builder at 0x3BEC computes both ends of the copy:

    3c1a:  102e 000a         move.b 10(a6),d0     slot
    3c1e:  eb48              lsl.w  #5,d0         slot * 32
    3c20:  43f9 0084 0800    lea    $840800,a1
    3c26:  43f1 0002         lea    2(a1,d0.w),a1 dst, skipping pen 0

    3c2e:  102e 000b         move.b 11(a6),d0     palette_index
    3c32..3c38                ((n<<3) - n) << 2   = 28n
    3c3a:  43f9 0002 42a0    lea    $242A0,a1
    3c44:  2089              move.l a1,(a0)       src

So each palette is 14 words at `0x242A0 + 28*index`, and each hardware
line is 16 words at `0x840800 + 32*slot` with pen 0 left transparent.
The queue itself is 64 entries of two longs at the pointer in 0xFFF402,
write index 0xFFF406, wrapped by subtracting 512 at 0x3C12.

Decoding the words with the System 16 gun layout (R = bits 0-3 << 1 |
bit 12, G = bits 4-7 << 1 | bit 13, B = bits 8-11 << 1 | bit 14, bit 15
shade) sends 0x7FFF to (31,31,31) and 0x0000 to (0,0,0), which is the
check that pins the format.

    tools/palette_demand.py

reproduces the whole table. 168 of the 176 entries are distinct; there
are four exact-duplicate groups, the largest being
0x16/0x17/0x18/0x99/0x9B/0x9D.

Separately, `set_level_palettes` (0x3952) copies 1024 bytes to 0x840000
from one of four blocks at 0x232A0 + 0x400*(0xFFF095 & 3). That is the
background half and it is a different table from the sprite one above.

---------------------------------------------------------------------
## 4. NEGATIVE RESULT — no two live level-1 sprite palettes can share a CRAM line

HANDOFF-DECOMPILE question 1 asks whether two frequently co-resident
actor classes could share one palette for free. They cannot.

    ares-headless --frames 3000 --input discover/inputs/play_level1.csv \
        --dump wram:0xFFF400:0x200:palstate_3000.bin rom/s16.32x
    tools/palette_demand.py --wram palstate_3000.bin

At frame 3000 the request table holds a slot for 17 palette indices.
Twelve of them are real palettes; their pairwise colour unions are

    0x28+0x58=20  0x29+0x58=20  0x2A+0x58=20  0x57+0x58=20  0x28+0x2A=21

against the 15 usable pens in one line. Zero pairs fit. Across all
twelve the program is asking for 123 distinct colours at once.

Conclusion: sharing is not a lever on level 1. The four exact-duplicate
groups in entry 3 are real but none of them is live here.

---------------------------------------------------------------------
## 5. Five of the seventeen live palettes are single-colour (2026-09-10)

Same dump as entry 4. Indices 0x00, 0x01, 0x03, 0x07 hold one distinct
colour across all fourteen pens, and 0x49 holds two:

    0x00 -> slot 13  all 0000        0x01 -> slot 8   all 000F
    0x03 -> slot 1   all 00FF        0x07 -> slot 12  all 0FFF
    0x49 -> slot 4   30FF then 7FFF

Each consumes a whole hardware line to carry one colour. Collapsing the
five into a single line with five used pens is a pen remap at bake time
and frees four of the seventeen.

Unproven: whether any of the five appear in the crowded 28-line band
LOOP29 126/127 measured. That needs a per-band census, not this dump.

Also from the same dump, consistent with LOOP29 115: the refcount table
at 0xFFF440 shows a nonzero count for only 10 of the 17 allocated slots,
so a refcount of zero does not mean the slot is free.

---------------------------------------------------------------------
## 6. NEGATIVE RESULT — Ghidra auto-analysis reaches 45% of the palette-write sites

    tools/ghidra_run.sh script palette_census.py OUT.json

finds 88 writes to object offsets $0A/$0B. A linear objdump sweep for
the same operand shape finds 199, and Ghidra's 88 are a strict subset.
Sampling the missing ones with `tools/ghidra/probe_undef.py` returns
`DATA undefined` for 0x5450, 0x5F2E, 0x6C6C, 0x91F4, 0xDA7E, 0x145DA,
0x261B6 and others: Ghidra never disassembled those bytes, presumably
behind jump tables it did not resolve.

The linear sweep is not the answer either, since it disassembles data as
instructions and will invent sites. Neither number is the demand.

Conclusion: do not quote a Ghidra-only census as the full palette
demand. The reliable statement today is the runtime one in entry 4,
because it counts what the game actually allocated.

---------------------------------------------------------------------
## 7. The sprite record's colour field is word 4, bits 5-0 (2026-09-10)

LOOP29 126 read the sprite colour set as `w[4] & 0x3F` off the game's
sprite RAM. That is correct and it is now grounded in the RTL rather
than in convention.

`srcref/jtcores/cores/s16/hdl/jts16_obj_scan.v:184` assigns
`pal <= tbl_dout[5:0]` at state 5 on the System 16B path (`MODEL`).
The word index is pinned by state 2 two cycles earlier, which takes
`xpos <= tbl_dout[8:0]` — word 1 — so state 5 is reading word 4.
Bank is bits 11-8 and priority bits 7-6 of the same word.

The field holds the SLOT the allocator handed out ($0A,
docs/log/LOOP-DECOMPILE.md 2), so a census of sprite RAM must map it
back through the request table at 0xFFF500 before it means a palette.

---------------------------------------------------------------------
## 8. NEGATIVE RESULT — collapsing the trivial palettes buys one line, not four

Entry 5 found five single-colour palettes each burning a hardware line
and asked whether any of them sit in the crowded band. Measured:

    tools/palette_bands.py rom/s16.32x

which dumps sprite RAM at 0xFF7000 and the request table at 0xFFF500
together, at frames 1800/2600/3000/3400/3800 of play_level1, and splits
each 28-line band into real and trivial colour sets:

    frame | b0  b1  b2    b3    b4    b5   b6   b7  | frame
     1800 | 0+0 0+0 5+0   4+0   4+0   1+0  1+0  0+0 | 6+0
     2600 | 0+0 0+0 4+1   3+1   3+1   1+0  1+0  0+0 | 5+1
     3000 | 0+0 0+0 2+1   2+1   5+1   3+0  2+0  0+0 | 5+1
     3400 | 0+0 0+0 3+1   3+1   3+1   1+0  4+0  2+0 | 7+1
     3800 | 0+0 0+0 3+1   3+1   3+1   1+0  4+0  2+0 | 7+1

    worst BAND real colour sets: 5

Only one of the five trivial palettes ever reaches a band: 0x01, solid
(30,0,0), in bands 2/3/4 of four of the five frames. The other four hold
slots without being drawn.

Conclusion: collapsing the trivial palettes takes the worst band from
six sets to five. The port has three sprite lines after the text ramp,
so this does not close the gap on its own. Entry 5's "frees four of the
seventeen" is right about slots and wrong about lines; four of those
slots were not costing a line in the first place.

Every slot referenced by a sprite record was present in the request
table at all five frames, so nothing was dropped from the count.

NOT a reproduction of LOOP29 126. That entry's per-frame whole-frame
counts were 5/6/6/6/5 and these are 6/6/6/8/8, and the rows do not line
up frame for frame. Different rom build is the obvious candidate and it
has not been checked. Take the shape of the result, not the row.

---------------------------------------------------------------------
## 9. Three CRAM lines cover 70% of live sprite records, with no colour change

Same five dumps as entry 8, counting live sprite RECORDS per palette
rather than distinct sets per band:

    frame  records  top1  top2  top3  top4
     1800       21   33%   52%   71%   90%
     2600       15   47%   60%   73%   87%
     3000       12   58%   67%   75%   83%
     3400       15   53%   60%   67%   73%
     3800       20   35%   50%   65%   80%

    three lines cover 58 of 83 records across the five frames = 70%

One palette dominates every frame: index 0x32 carries 7 or 8 records on
its own, 33-58% of the frame. The next three are 0x28, 0x29 and 0x2A,
which are a related family by eye (all begin 5CEF) but union to 20-23
colours, so they cannot be merged into one line without changing what
the game looks like.

Conclusion: picking the three sprite lines per frame by record count
sends about 70% of records to the VDP with no colour drift at all. The
port renders roughly 4% in hardware today (LOOP29 127), and that entry
puts 39% of the frame budget in the framebuffer paint those records
cause. This is a lever on that number and it needs no palette surgery.

Unproven here: whether the per-frame choice can be made without a
visible seam when the winning set changes between frames, and whether
records-per-palette is the right proxy for painted pixels. Both are
rendering-thread questions.

---------------------------------------------------------------------
## 10. The tilemap unpacker, decoded (2026-09-10)

HANDOFF-DECOMPILE question 2. Verified against our bytes with

    $MARSDEV/m68k-elf/bin/m68k-elf-objdump -D -b binary -m 68000 \
        --start-address=0x1694 --stop-address=0x1760 roms/altbeast/prog68k.bin

`unpack_level_tilemap` (0x1694) indexes a six-byte descriptor at 0x1CE2
with `0xFFF142 & 7`. The word goes to 0xFFF095, which is the same
location `set_level_palettes` reads to pick its background palette block
(entry 3). The long is the packed data pointer.

    scene 0  block 1  data 0x029E00      scene 3  block 2  data 0x0369C0
    scene 1  block 1  data 0x02EF10      scene 4  block 2  data 0x03B0E0
    scene 2  block 1  data 0x0324D0

There are five entries. Indices 5-7 read into the copyright string, so
the mask at 0x169C is wider than the table and 0xFFF142 must never
exceed 4.

The map arrives as TWO separate full sweeps of the same region, not one:

  - 0x16BE writes the HIGH byte of every tile word, from 0x400000,
    stride 2, 20480 words. Encoding is (count, value) pairs; the count
    drives a `dbf`, so a run is count+1 bytes.
  - 0x16DE writes the LOW byte, from 0x400001, same stride and count.
    A non-zero byte is a literal. Zero is the escape: the next byte is a
    run length of zeros, and a zero length means a single zero literal.

That is 40960 individual byte writes per scene load, in one burst, before
`set_level_palettes` is called at 0x760. Two smaller writers follow:
0x174E lays a 40x20 block at 0x40A230 with a constant high byte of 0xA5,
and 0x170A lays a blocked pattern at 0x40B230 with a constant high byte
of 0x04. Both step 128 bytes per row, so they sit in 64-tile-wide pages.

The rebase is intact: the three `lea $400000` in the arcade program
appear as `lea $852000` in `md_src/game_body.bin`, same count.

---------------------------------------------------------------------
## 11. Plane B is tile page 0 and Plane A is page 7, for the whole game

The System 16B layer registers live in a 0x200-byte block at text RAM
0x410E00. Offsets from `srcref/jtcores/cores/s16/hdl/jts16_mmr.v:94-105`:

    0xE80 scr1_pages_std   0xE82 scr2_pages_std
    0xE84 scr1_pages_alt   0xE86 scr2_pages_alt
    0xE90 scr1_vpos_std    0xE92 scr2_vpos_std
    0xE94 scr1_vpos_alt    0xE96 scr2_vpos_alt
    0xE98 scr1_hpos_std    0xE9A scr2_hpos_std
    0xE9C scr1_hpos_alt    0xE9E scr2_hpos_alt

A `pages` word is four 4-bit page numbers, one per quadrant
(`jts16_scr.v:107-110`, upper-left in bits 15-12 down to lower-right in
bits 3-0).

The whole program writes the page selects at exactly two sites, both
with the same constants and no register-indirect writer anywhere:

    1b0d6:  move.w #$7777,$410E80      scr1 = page 7 in all four
    1b0de:  move.w #$0000,$410E82      scr2 = page 0 in all four
    1ba42:  move.w #$7777,$410E80
    1ba4a:  clr.w  $410E82

The four scroll registers are written every vint from inside the IRQ4
handler (0x2AD2, 0x2AE0, 0x2AEE, 0x2AFC — hpos and vpos for both
planes), so scroll moves and pages never do.

Conclusion: scr2 always draws tile page 0 and scr1 always draws page 7.
A scene change rewrites page CONTENT, never the selects. Page 0 is
0x400000-0x400FFF, which is FB staging 0x852000 and 32X DRAM 0x12000.

---------------------------------------------------------------------
## 12. NEGATIVE RESULT — the tilemap source is healthy; the Plane B defect is downstream

The two-pass structure in entry 10 suggested a parity split: if a
framebuffer bank flip landed between the high-byte sweep and the
low-byte sweep, one bank would hold high bytes over stale low bytes.
Measured and false.

    ares-headless --frames N --dump dram:0:0x80000:dram_N.bin rom/s16.32x

at N = 400, 700, 1000, no input. The staged region (DRAM 0x12000, and
the second bank at 0x32000) is populated in both parities and in both
banks:

    frame 700 bank0   hi nonzero 17985/20480   lo nonzero 17642/20480
    frame 700 bank1   hi nonzero 17976/20480   lo nonzero 17632/20480

And both source pages from entry 11 fill and then behave as expected:

    page 0 (scr2)  frame 400   42 distinct words -> 700  507 -> 1000  530
    page 7 (scr1)  frame 400   56 distinct words -> 700  400 -> 1000  400

Page 0 keeps changing after the load (197 bytes between 700 and 1000),
which is the scrolling plane rewriting its incoming column. Page 7 is
static after the load.

Conclusion: the unpacker, the rebase and the framebuffer staging are all
innocent. START-HERE's location of the defect in Plane B's UPLOAD path
stands, and the 4 KB to instrument is DRAM 0x12000-0x12FFF, the source
page scr2 draws from.

One real but separate finding from the same dumps: the two framebuffer
banks diverge in the staged tile region. At frame 1000 bank1 is
byte-identical to bank1 at frame 700, 300 frames stale, while bank0 has
moved on; 266 of 40960 bytes differ. The game writes tiles into whichever
bank is current and nothing carries them to the other. Not enough to
explain the title screen, and worth a look on its own.

---------------------------------------------------------------------
## 13. Sprite RAM has exactly ONE writer, and it is a loop in IRQ4 (2026-09-10)

HANDOFF-DECOMPILE question 3. A census of every reference to the arcade
sprite-RAM range 0x440000-0x44FFFF finds three sites, two of which write
the same end marker. Note that objdump prints ABSOLUTE operands in hex
but IMMEDIATE operands in DECIMAL, so a grep for `0x440000` misses the
`movea.l #$440000` sites entirely; search for 4456448 as well.

The upload is 0x2B16-0x2B46, inside the IRQ4 handler that starts at
0x2AAC:

    2b16:  movea.w #$EC80,a0        order list, 256 bytes
    2b1a:  movea.w #$F800,a1        record pool
    2b1e:  movea.l #$440000,a2      hardware sprite RAM
    2b24:  move.w  #255,d1
    2b2c:  move.b  (a0)+,d0         one order byte per entry
    2b2e:  bmi     ...              bit 7 set: skip, dest does NOT advance
    2b30:  lsl.w   #4,d0            record index * 16
    2b32:  lea     (a1,d0.w),a3
    2b36:  move.l  (a3)+,(a2)+      three longs = 12 of the 16 bytes
    2b3c:  addq.w  #4,a2            words 6 and 7 are never rewritten
    2b42:  move.w  #-1,(a2)         end markers
    2b46:  move.w  #-1,4(a2)

So the order list is a 256-entry display list of record indices, the
hardware list is COMPACTED (a skipped entry consumes no hardware slot),
and the pool holds at most 128 records of 16 bytes at 0xFFF800.

The clear routine at 0x36C4 zeroes 0x440000 and 0xFFF800 in lockstep,
62 iterations, which is the pairing stated independently.

VERIFIED ON A RUNNING FRAME. Dumping all three regions together

    ares-headless --frames 3000 --input discover/inputs/play_level1.csv \
        --dump wram:0xFFEC80:0x100:order.bin \
        --dump wram:0xFFF800:0x800:pool.bin \
        --dump wram:0xFF7000:0x800:hwspr.bin  rom/s16.32x

and replaying the loop above in Python reproduces the rebased hardware
mirror: 12 of 256 order entries live, and 194 of the first 198 bytes
match. Two of the four misses are bytes 194-195, which the loop leaves
untouched between its two end markers, so the real skew is two bytes
inside records — the pool moved on after the vint wrote the mirror.

The twelve live entries are the same twelve live records the band census
counted at frame 3000 (entry 9), from a different dump.

---------------------------------------------------------------------
## 14. Text RAM is the opposite: 61 scattered writers

Same census over 0x410000-0x410FFF. The layer is 64 words per row and
the visible 40 columns start at column 24, which the tilemap blocks in
entry 10 confirm (0x40A230 is row 4, column 24, and is 40 wide).

Six sites write the base 0x410000 and are clear or fill routines; one of
them, 0x369C, clears all 4096 bytes as 1024 longs. Six write the layer
register block (entry 11). The remaining ~49 are individual `lea` sites
each aimed at one screen position, for example

    0x0057E, 0x01608, 0x090D8   row  0 col 24   the HUD row
    0x04554, 0x04568, 0x0457C   rows 7-9        attract text
    0x1B020..0x1B856            rows 2-25       the service-mode screens

There is no single upload and no shared thunk. Every writer stores
straight into the layer.

---------------------------------------------------------------------
## 15. What this means for the O(writes) pipeline

LOOP27 80 specified a write-through at the patch thunk and said the
blocker was that we intercept writes blindly. Entries 13 and 14 split
that problem in two, and the halves want opposite treatments.

  - SPRITE RAM needs no interception at all. There is one writer, its
    address is known, and both its inputs (0xFFEC80 and 0xFFF800) are in
    work RAM the shim can read directly. Replacing the loop at 0x2B16
    with our own pass is exact, because nothing else writes the region.
    That deletes the interception, the compare, the shadow and the
    packing for the sprite half in one move.
  - TEXT RAM cannot be done that way. 49 independent writers store
    straight into the layer, so the blind write-through is the right
    mechanism there and should stay.

Unproven: what the loop at 0x2B16 costs per vint, and whether replacing
it interacts with the FM window (the sprite upload is currently
DELIBERATELY ungated, tools/patch_game.py:1004). Both are
rendering-thread measurements, not decompile ones.

---------------------------------------------------------------------
## 16. The program is 19137 instructions in 433 functions, and all code is below 0x1EF1E

Scope measurement, so the decompile can be sized rather than guessed.

Classifying every address-bearing line of the reference disassembly by
its directive (a count, not a copy) gives 19137 code lines and 26942 data
lines, 433 functions, and a highest code address of 0x1EF1E. Everything
above that is data: 135938 bytes, 52% of the rom. The rom budget is

    0x00000-0x242A0  code and tables        56.5%
    0x242A0-0x255E0  sprite palette table    1.9%   (entry 3)
    0x255E0-0x29E00  tables                  7.0%
    0x29E00-0x40000  packed tilemap data    34.6%   (entry 10)

The reference has 1223 hand-named labels against 2909 auto-generated
ones, and its author disclaims the enemy handlers, so roughly 30% of it
carries a human reading and the rest does not.

Of the functions Ghidra reached before seeding, 31 touch textram,
tileram, palette, spriteram, io, the MCU mailboxes or the frame flag —
5834 bytes. That is the subset this thread exists to document. 124 of
234 were under 64 bytes and only 3 over a kilobyte.

---------------------------------------------------------------------
## 17. Jump-table seeding: 234 functions to 534, 46% of instructions to 61%

Ghidra's auto-analysis stopped at two shapes, both of which put the
target in DATA so nothing in the instruction stream refers to it:

  A. a PC-relative table of longs, dispatched as
         lea TABLE(pc),a0 ; movea.l (a0,dN.w),a0 ; jmp (a0)
     Four of them: 0x026DC (8 entries, site 0x1ECA), 0x06D70 (20, site
     0x49A6), 0x06DA0 (8, site 0x4C00), 0x0DE22 (6, site 0xD9F6).
  B. a routine pointer stored into an object field by
         move.l #ADDR,$xx(aN)
     and later reached through the object struct's $02 routine field
     (0x39A8: movea.l 2(a6),a0 ; jsr (a0)). 275 distinct targets from
     293 sites.

    tools/ghidra/seed_harvest.py LISTING.dis roms/altbeast/prog68k.bin --json S.json
    tools/ghidra_run.sh script seed_apply.py S.json

303 seeds, of which 204 were bytes Ghidra had never disassembled. Three
seeds failed and were discarded (0x3000, 0x7C38, 0x18F7E).

    before   234 functions   8822 instructions (46% of the program)
    after    534 functions  11765 instructions (61%)

Cross-checked against the reference's 433 function starts: 255 agree,
178 are still missed, 279 are Ghidra-only. The Ghidra-only entries are
mostly finer granularity rather than error — they include 0x36C4, 0x3BEC
and 0x170A, all of which entries 3, 10 and 13 read by hand. NO function
landed above the 0x1F000 code ceiling, which is the check that would
have caught seeds disassembling data.

The 178 still missing are a reachability cascade, not a third dispatch
shape. The run at 0x6936 is reached by plain `bsr` from 0x6850, 0x6888
and 0x68FE, callers that were themselves unreached.

The `adda.w (a0),a0` sites are NOT a third shape. The real one, at
0xE052-0xE062, builds a data pointer from a self-relative word table at
0x1D33E indexed by 0xFFF142, and is never jumped to. The rest are
phantoms from disassembling data linearly.

---------------------------------------------------------------------
## 18. NEGATIVE RESULT — re-running Ghidra analysis over seeded code destroys it

Having seeded, the obvious next move is to let the analyser chase the
call graph from the newly reached code. It does the opposite.

    analyzeHeadless PROJ altbeast -process prog68k.bin      (no -noanalysis)

    seeded        534 functions  11765 instructions
    re-analysed   450 functions   9423 instructions
    re-seeded     536 functions  10130 instructions

The analysers clear code they do not believe, and re-applying the seeds
afterwards does not recover it: the seed addresses come back but the
fall-through and branch-reached code behind them does not.

The order is import once, seed once, stop. `tools/ghidra/rebuild.sh`
does exactly that and reproduces 534/11765 exactly from an empty project.
Do not run `-process` without `-noanalysis` against this project.

---------------------------------------------------------------------
## 19. Seeding converged: 100% of the functions, 99.82% of the instructions

Entry 17 left 178 functions and 3240 instructions unreached. Closing them
took three more passes and two mistakes worth recording.

The reference disassembly is, as HANDOFF-DECOMPILE says, a STRONG source
of ADDRESSES. Using its 433 function starts as seeds — every one of which
still has to disassemble in OUR bytes to count — is the sanctioned use of
it, and it is what closed the gap.

    pass 1  4 dispatch tables + 275 routine pointers   534 fn  11765 ins
    pass 2  + 433 reference function starts            714 fn  16514 ins
    pass 3  + 191 missing-run starts                   901 fn  19107 ins
    pass 4  + every missing reference instruction     2121 fn  20355 ins
    pass 5  entry points separated from disassembly    714 fn  20355 ins
    pass 6  + the repair pass, run FIRST               714 fn  20355 ins

    final: 433 of 433 reference function starts present (100%)
           19102 of 19137 reference instructions (99.82%)
           0 instructions above the 0x1F000 code ceiling

MISTAKE 1, pass 4: creating a function at every seeded address. Coverage
was right and the program was ruined — 2121 functions for 433 real ones,
because every address that merely needed disassembling became an entry
point and the call graph fragmented. `seed_apply.py` now takes
`function_seeds` separately from `seeds`; only genuine entries (table
targets, routine pointers, reference function starts) become functions.

MISTAKE 2, pass 5: a speculative seed can land ONE WORD inside a real
instruction, and Ghidra then disassembles the whole run misaligned. At
0x1580 it produced `btst.b D1,-(A2)`, which is the second half of the
`lea` at 0x157E; the identical lea/bra pairs at 0x1576, 0x1586 and 0x158E
prove the alignment. The main loop's guard only rejects a seed landing in
an instruction that ALREADY exists, so a bad seed applied first wins.
`seed_apply.py` now runs a REPAIR PASS FIRST: any reference instruction
address that is not an instruction start has whatever covers or blocks it
cleared and is re-disassembled. Ordering matters — running repair after
the seeds fixed 10 runs, running it first fixed 116.

Of the 35 instructions still missing, one is 0x181D, an ODD address. A
68000 instruction cannot start there, so the reference is wrong at that
point and our bytes win. Fourteen of the rest are above 0x1D000, in the
tail past the last marked function.

1253 instructions are Ghidra-only. They are UNVERIFIED: some will be code
the reference left as data, some will be Ghidra disassembling data.
Nothing sits above the code ceiling, which is the only check applied.

    tools/ghidra/rebuild.sh     # import, repair, seed — reproduces the above

Do not re-run `-process` without `-noanalysis` afterwards (entry 18).

---------------------------------------------------------------------
## 20. The video surface is 75 functions and 8814 bytes — and a census that reads only absolute operands misses most of it

With the program fully disassembled (entry 19), the question "how much of
this rom is video" becomes answerable.

    tools/ghidra_run.sh script video_map.py OUT.json
    tools/ghidra/video_map_md.py OUT.json > docs/audit/video_map.md

75 functions touch tileram, textram, palette, spriteram, the IO ports,
the MCU mailboxes or the frame flag. 8814 bytes, 3.4% of the rom.

    spriteram    3 accesses / 3 functions      palette  31 / 17
    tileram     25 accesses / 12 functions     io       24 /  9
    textram     57 accesses / 37 functions     mailbox   8 /  4

TWO MEASUREMENT BUGS, both mine, both worth the space because either one
alone produces a confident wrong number.

  1. `timing_census.py` counts an access only when the ADDRESS IS THE
     OPERAND. This program mostly does
         movea.l #$440000,a2 ... move.l (a3)+,(a2)+
     so the sprite upload — the busiest video routine in the game
     (entry 13) — does not appear in its output at all. Its census found
     39 functions where the correct count is 75, and it missed
     `set_level_palettes` at 0x3952, which entry 3 had already read by
     hand. Counting both forms is the fix.
  2. My first `video_map.py` was WORSE, at 17 functions and zero
     immediates, because Ghidra answers `getOffset()` for an Address
     operand and `getValue()` for a Scalar, and I asked only for the
     first inside a bare `except: continue`. A silent exception handler
     turned a whole addressing mode into "no hits found".

Separately: **0x2AAC, the IRQ4 handler, was not a function at all.** It
is reached by `bra.w` from the vector trampoline at 0x404, and Ghidra
creates functions at CALL targets, not branch targets. Resolving the
exception vectors one branch deep and seeding the results fixes it; the
handler is 784 bytes and touches six of the eight regions, more than any
other routine in the program. Any census that ran before this entry was
blind to it.

The inventory names 11 routines and leaves 64 unnamed on purpose. An
address is not a meaning.

---------------------------------------------------------------------
## 21. There is primary System 16B documentation in the tree, and it confirms entries 3, 10, 11 and 13

Mike pointed at `srcref/jtcores/cores/s16` and `cores/s16b`. Two things
follow.

**`cores/s16b` has no video RTL.** It is CPU, mapper, sound, timer and
the MC8123 (`jts16b_main.v`, `jts16b_mapper.v`, `jts16b_snd.v`,
`jts16b_timer.v`, `jtmc8123.v`). The video is shared from `cores/s16`
with the `S16B` macro set — `jts16_video.v:95` reads
`localparam MODEL = \`ifdef S16B 1 \`else 0` and `cores/s16b/cfg/macros.def`
defines `S16B`. So the MODEL==1 paths in `jts16_obj_scan.v` and
`jts16_mmr.v` that entries 7 and 11 cite ARE the System 16B spec.

**`cores/s16b/doc/` is primary documentation**, which CLAUDE.md's "no
published documentation for most of what matters" does not lead you to
expect: Charles MacDonald's System 16B hardware notes (`s16b.txt`,
2001-2003, tested on a board), MAME's `segas16b.cpp` and `segaic16_m.cpp`,
and the 315-5195 mapper schematics as a PDF.

Everything this thread derived independently agrees with it:

  - **Colour word format** (entry 3, derived by checking 0x7FFF -> white):
    D15 shade, D12/D13/D14 the low bit of R/G/B, D0-D3 red bits 1-4,
    D4-D7 green, D8-D11 blue. Exactly the layout.
  - **Sprite palette field** (entry 7): word 4 is `1111bbbbppcccccc`,
    c = bits 5-0. Three sources now agree — the notes, the RTL and
    LOOP29 126's reading.
  - **Sprite palette base** (entry 3, derived from 0x3C20): "Sprites use
    color entries 1024-2047, divided into 64 16-color palettes". Entry
    1024 is byte 0x800, so `0x840800 + 32*slot` is exactly right, and the
    64 slots are why the allocator bounds against 63 (entry 2).
  - **Visible text columns** (entry 14, derived from the block writers):
    "the viewable portion starts at column 24 and goes to column 63".
  - **Register offsets** (entry 11): $E80/$E82 page select, $E90/$E92
    vertical scroll, $E98/$E9A horizontal, alternates at +4, plus column
    scroll tables at $F00/$F40 and row scroll at $F80/$FC0.

TWO THINGS THE DOCUMENT ADDS.

1. **Entry 13's "words 6 and 7 are never rewritten" is not a quirk, it is
   required.** Word 7 ($0E) is the sprite END ADDRESS and *the hardware
   writes it*: "the final address used when the sprite has been rendered
   is written to the sprite end address field of the sprite RAM entry".
   Word 6 ($0C) is unused. The upload copies words 0-5 because those are
   exactly the six the software owns. Any write-through that replays all
   eight words would fight the sprite engine.

2. **Entry 11 named the layers by RTL signal; the document names them by
   function.** $E80/scr1 is the FOREGROUND page select and $E82/scr2 is
   the BACKGROUND. So "scr2 always draws page 0" reads: the BACKGROUND
   plane always draws tile page 0. That is the plane START-HERE's open
   bug is about, and it confirms entry 12's conclusion — the 4 KB to
   instrument, 32X DRAM 0x12000, is the background source page.

CHECKED AND NOT NEW: the notes say tile layers use 128 EIGHT-colour
palettes (confirmed by `jts16_scr.v:56`, "1 priority + 7 palette + 3
colour"), which suggests two tile sets per 16-pen MD line. The port
already knows: `sh_src/m_main.c:1758` and LOOP28 99 state it and the
allocator is built on it. Not a finding.

---------------------------------------------------------------------
## 22. The game is frame-locked, it counts its own overruns, and it shows the count on screen

Mike's model: the game code is gated by the 60 Hz screen, not by 68000
throughput, so the 32X's slower 7.67 MHz part costs nothing. The
mechanism is confirmed. The conclusion needs one correction.

**The gate is four instructions and the whole program has four
references to the flag.**

    397e:  clr.b  $FFF01C          the main loop gives up the frame
    3982:  tst.b  $FFF01C
    3986:  beq.s  0x3982           spin until IRQ4 sets it
    3988:  dbf    d0,0x397E        d0+1 frames, so callers can wait N
    398c:  rts

11 call sites. The only other references to 0xFFF01C are the two in
IRQ4. There is no second path and no timer fallback: the main loop
BLOCKS until vblank, so the game cannot run faster than 60 Hz whatever
CPU it is given. The arcade's 10 MHz part and our 7.67 MHz part sit in
the same three-instruction spin. That part of the model is exactly right.

**The game notices when the frame did not fit, and degrades on purpose.**

    2ab0:  tst.b  $FFF01E          suspend latch -> skip everything
    2ab4:  bne.w  0x2C7E
    2ab8:  tst.b  $FFF01C          flag STILL set?
    2abc:  beq.s  0x2AC6           no: the loop is waiting, normal frame
    2abe:  addq.w #1,$FFF144       yes: the loop overran. COUNT IT
    2ac2:  bra.w  0x2C06           and take the SHORT PATH
    2ac6:  addq.b #1,$FFF01C       normal: release the loop, then the
    2aca:  ...                     scroll regs and the sprite upload

The short path at 0x2C06 skips the four scroll register writes AND the
sprite upload (entry 13) entirely. So an overrun does not tear; it holds
the previous frame's video state and slips the animation by one frame.
0xFFF144 is incremented at exactly one place, cleared at 0x930, and read
at 0xBE2. Nothing else touches it.

**Measured on our rom.** The counter is the game's own verdict and needs
no instrumentation:

    ares-headless --frames N --input discover/inputs/play_level1.csv \
        --dump wram:0xFFF140:0x10:miss.bin rom/s16.32x

    frame 1200   0xFFF144 =   52
    frame 2400   0xFFF144 =  635    +583 over 1200 frames = 49% missed
    frame 3600   0xFFF144 = 1220    +585 over 1200 frames = 49% missed

Half the vints overrun during level 1. So we ARE losing speed — but not
to the clock, which is what the model gets right. The frame budget is
shared, and CLAUDE.md's own figures put the game at ~2780 instructions
per vint and the port's pipeline at ~2882 on top of it, in the same 60 Hz
box. The 68000 is not slow; the box has twice as much in it.

**And the game displays the counter.** 0xBE2 reads 0xFFF144, rotates out
four nibbles, converts each to ASCII hex and writes them to 0x4101D4 —
text row 3, column 42, which is visible column 18 (entry 21). The
containing routine is 0x005BE, the largest function in the program, which
reads the IO ports and is reached through a dispatch case: the service /
test mode screen.

That makes the game its own speedometer. The same four digits can be read
on the arcade and on our port, on real hardware, with no probe build and
no emulator support. Worth wiring into the acceptance pass.

---------------------------------------------------------------------
## 23. What already fits the MD VDP, and the four patch points

Mike: what can be patched so the game fits the shape of the Genesis VDP?
Everything below comes from entries 10-13, 21 and 22, so it is stated
against our own bytes rather than against the general System 16 case.
Altered Beast turns out to use a small, well-behaved subset of what the
System 16B video hardware can do.

**ALREADY THE SAME SHAPE**

  1. **Scroll is whole-plane, both layers, and that is provable.** All
     four scroll writes in IRQ4 mask with `andi.w #$1FF` before storing
     (0x2ACE, 0x2ADC, 0x2AEA, 0x2AF8), which clears bit 15. Bit 15 of
     hpos is the row-scroll enable and bit 15 of vpos the column-scroll
     enable (`jts16_mmr.v:67-70`). And the row/column scroll tables at
     text RAM 0xF00-0xFFF are NEVER WRITTEN — zero references in the
     program. So there is no per-row or per-column scroll to emulate.
     Two 9-bit values per layer per frame, which fits inside the MD's
     10-bit full-screen scroll with room over.
  2. **Page selects are constant** (entry 11): foreground page 7,
     background page 0, written at two sites with the same immediates and
     never varied. An MD name table base register is set once at init and
     left alone. There is no page remapping to chase.
  3. **Plane geometry matches.** A System 16 page is 64x32 tiles, which
     is an MD plane size. The text layer is 64x28 with only columns 24-63
     visible (entry 21).
  4. **Tile palettes are 3bpp** so two share a 16-pen MD line, which the
     port already builds on (`sh_src/m_main.c:1758`, LOOP28 99).

**STILL THE WRONG SHAPE**

  5. Sprites are horizontal strips with a signed pitch and 5-bit X and Y
     zoom (entry 21's layout); the MD has tile-based sprites and no
     scaling. LOOP29 119 measured only 2.0-3.8% of records as genuinely
     zoomed, so this is a small residue, not the bulk.
  6. Sprite palettes are 64 lines of 16 against the MD's 4, and entry 9
     showed no two live level-1 palettes union to 15 or fewer.

**THE FOUR PATCH POINTS, ranked by ratio of payoff to risk**

  A. **The four scroll writes** at 0x2AD2, 0x2AE0, 0x2AEE, 0x2AFC. Each
     is a `move.w d0,$410Exx` with the value already masked to the MD's
     range. Retargeting them writes MD scroll directly and deletes a
     conversion step from the shim. Four instructions, and finding 1
     proves nothing else is needed for correctness.
  B. **The sprite upload loop** at 0x2B16 (entry 13, entry 15). One
     writer, both inputs in work RAM, and the hardware owns words 6 and 7
     so a replacement must not write them (entry 21).
  C. **The two page-select writes** at 0x1B0D6 and 0x1BA42. They can
     become no-ops once the MD name table bases are fixed at init.
  D. **The tilemap unpacker** at 0x16BE and 0x16DE. Note the ordering
     constraint from entry 10: the high and low bytes of every name table
     word arrive in SEPARATE FULL PASSES, so a per-write conversion never
     sees a complete word. Intercept at the two loop heads, not at the
     stores.

NOT MEASURED HERE: whether the System 16 and MD scroll sign conventions
agree, and what the tile priority bit costs. Both are rendering-thread
questions.

---------------------------------------------------------------------
## 24. The scroll conversion is a constant: MD hscroll = S16 hpos - 192

Entry 23 left the scroll sign convention unmeasured, and it gates the
cheapest patch. Measured on our own rom rather than read out of the shim:

    ares-headless --frames N --input discover/inputs/play_level1.csv \
        --dump wram:0xFFF0E0:0x10:s16.bin \
        --dump vram:0xFC00:0x8:hs.bin  rom/s16.32x

0xFC00 is the MD hscroll table (md_start.s:242 sets reg 0x8D), plane A at
+0 and plane B at +2.

    frame   S16 fgH bgH   MD hsA hsB   (md - s16) mod 1024
     1400       156 156      988 988          832
     2200       147 147      979 979          832
     3000       112 112      945 945          833
     3800        53  53      885 885          832

**Same sign, constant offset.** 832 mod 1024 is -192, and 192 is 24 tile
columns — exactly the visible-window origin entry 21 got from the
hardware notes ("the viewable portion starts at column 24"). The
conversion is not a convention to be discovered, it is the difference
between a 64-column name table shown from column 24 and an MD plane shown
from column 0.

The single 833 at frame 3000 is one pixel and appears once in four
samples; the likely cause is sampling the two memories at different
points within a frame, not a second rule. Unproven either way.

VERTICAL IS UNMEASURABLE IN THIS SCENE. Both vertical registers sit at 32
for the whole of level 1 across 2400 sampled frames, and the MD VSRAM
value equals them exactly. That is consistent with a straight copy at
offset 0 and it does NOT establish the sign, because nothing moved. A
vertically scrolling scene is needed before anyone relies on it.

Also worth recording: **the two tile layers scroll together in level 1.**
fgH and bgH are equal at every frame sampled. Whatever parallax the game
has, it is not in this scene.

**This makes patch point A a six-byte in-place rewrite.** The store is
`move.w d0,$410E9x` = 33C0 + 4 bytes. A `jsr abs.l` is 4EB9 + 4 bytes.
Identical length, so each of the four sites can become a call to a
handler with no reflow and no address shifting — the shape
`tools/patch_game.py` already uses for its dispatcher thunks (it writes
`0x4EB8` + word + `nop` at DISPATCHERS).

---------------------------------------------------------------------
## 25. MDHSCR: the game drives MD hscroll from its own scroll stores

First patch off this thread. `make ship-us MDHSCR=1`. The default build is
untouched; everything is behind the flag.

**What it does.** `tools/patch_game.py` rewrites the two HORIZONTAL scroll
stores in IRQ4 — 0x2AD2 foreground, 0x2AEE background — from
`move.w d0,<remapped 0x410E9x>.l` (6 bytes) to `jsr (FFFFB3xx).w ; nop`
(also 6), the same in-place shape the DISPATCHERS rewrite already uses.
The thunk bodies are GENERATED by the patcher into `md_src/hscr_thunks.h`,
because only the patcher knows what the text-RAM register remapped to
under the active flag set (it resolved to 0xFF8E98 / 0xFF8E9A here).
`md_main.c` copies them to 0xFFB300 / 0xFFB320 at boot.

Each 32-byte thunk does the ORIGINAL store, then `sub.w #192`,
`and.w #$3FF`, sets the VDP address for the hscroll entry and writes it.
d0 is dead at both sites (the next instruction reloads it) and no
conditional branch reads the CCR in between, so the thunk is free to
clobber it.

**Correctness: confirmed.** In the patched build the measured relation
(entry 24) holds EXACTLY at every sample, while the baseline is one short
at two of three — the shim writing a value the game has already moved
past:

    frame   base S16/MD      mdhscr S16/MD
     2200   140 -> 973       145 -> 977      (145 + 832 = 977 exactly)
     3000    57 -> 890        76 -> 908      ( 76 + 832 = 908 exactly)
     3600    51 -> 883        52 -> 884      ( 52 + 832 = 884 exactly)

**NEGATIVE ON MY OWN PREMISE.** I built this as purely additive and
predicted a pixel-identical build. It is not. The game state is identical
through frame 1600 (the scroll register matches exactly at 400, 800, 1200
and 1600) and has diverged by 2200. A control settles the cause: the same
baseline rom run twice is BYTE-IDENTICAL in both the miss counter and the
scroll state, so the divergence is the patch, not the rig.

Two extra thunk calls per vint are enough to flip frames when 49% of them
already overrun (entry 22). Miss counts move by about five per interval
IN BOTH DIRECTIONS (base 77/147/44/235 against mdhscr 73/142/50/240), so
there is no measurable speed cost — but there is no measurable saving
either, because ADDITIVE MEANS PURE ADDED WORK. The shim still produces
sc[3]/sc[7] and still writes them.

**Consequence for the next step.** Frame-number-aligned A/B cannot gate
this family: any timing change desyncs a long run, which is exactly why
`tools/attract_parity.py` aligns on the game's own timeline. And the
payoff needs the REPLACEMENT step — dropping the two
`*vdp_data_port = sc[3]/sc[7]` writes in md_main.c and the SH-2 work
behind them — which is a rendering-thread change, not a decompile one.
This entry is the evidence that the game side of it works.

VERTICAL IS DELIBERATELY NOT PATCHED. Entry 24 could not establish its
sign, because level 1 never scrolls vertically.

---------------------------------------------------------------------
## 26. MDHSCR phase 2 — correct, and NEUTRAL, because scroll is two words

Mike's framing: a patch is three phases. Change what the program ASKS FOR
so bigger chunks do more without flooding the VDP; then change the C that
builds the 32X side; then rebake art if its shape changed. Entry 25 did
phase 1 as an ADDITION and stopped at the phase-2 boundary, which is why
it could not show a saving. Phase 2 is now done: under
`MD_HSCROLL_DIRECT` md_main.c drops its own
`*vdp_data_port = sc[3]/sc[7]` pair, because the game wrote both entries
itself one vint earlier. Phase 3 does not apply — no art changes shape.

    frame  S16 fgH   MD hsA/hsB   exact   phase2 miss   base miss   base fgH
      400      192        0    0     yes           75          77        192
      800      192        0    0     yes          149         147        192
     1200      174     1006 1006     yes           46          44        174
     1600      156      988  988     yes          240         235        156
     2200      132      965  965      no*         552         551        140
     3000       54      886  886     yes          943         950         57

The shim no longer writes hscroll at all and the picture still tracks:
the conversion holds exactly at five of six samples. (*The 2200 miss is
one pixel and is a sampling artefact — 0xFFF0E2 is the game's source
word and the main loop can advance it after IRQ4 has already written the
VDP from it. The baseline shows the same skew.)

**NEUTRAL ON SPEED, and that is the real result.** Miss counts move by a
few in both directions (75/149/46/240/552/943 against 77/147/44/235/551/
950). Removing the shim's hscroll write deletes ONE control write and TWO
data writes per vint. Against the ~2882 instructions per vint the pipeline
costs (CLAUDE.md), that is far below what this rig can resolve.

Game state is IDENTICAL to the baseline through frame 1600 and diverges
by 2200, same as entry 25 — two thunk calls per vint still flip frames at
a 49% miss rate.

**So the deliverable is the MECHANISM, not this payload.** What is now
proven end to end: a six-byte in-place rewrite of a game store, into a
patcher-generated thunk that knows the active flag set's remap, driving
the MD VDP directly, with the shim's duplicate removed. Scroll was the
smallest chunk in the program — two words a frame. The same mechanism
applied to the sprite upload (entry 13: one loop, up to 128 records of 12
bytes, one writer, both inputs in work RAM) is the payload where "bigger
chunks without flooding the VDP" has something to bite on.

---------------------------------------------------------------------
## 27. NEGATIVE — deleting the whole sprite copy does not move the frame rate

`make ship-us MDSPRPROBE=1`. RENDERS WRONG BY DESIGN: the record copy
inside the game's own upload loop (0x2B30, 14 bytes) is replaced by
`lea 16(a2),a2` plus nops, so the list geometry and end markers are
unchanged but no record data is written. It prices the copy — up to
128 records x 12 bytes per vint — off the game's own missed-frame
counter (entry 22).

    frame   probe miss   base miss   delta
      400           71          77      -6
      800          129         147     -18
     1200           57          44     +13
     1600          240         235      +5
     2200          540         551     -11
     3000          927         950     -23

Mixed sign, and the runs desync from 1200 on, same as entry 25. Deleting
the single largest 68000 memory-to-memory copy in the frame does not
reduce missed frames.

**This corroborates the rendering thread from a different direction.**
Their 2026-09-10 measurement (commit 801be42) says the master is IDLE at
0.44 vints/generation while the slave is saturated at 1.40 vints of
compose with zero idle polls. If that is right, no amount of 68000 work
removed should move the frame rate — and none does. Two unrelated methods,
the same conclusion.

**CORRECTION TO MY OWN ENTRY 15.** It called the sprite write-through
architecture the thing that "unblocks the O(writes) pipeline", inheriting
LOOP27 80's premise that the 68K pipeline is the frame-rate constraint.
That premise is stale. The single-writer result stands and is still worth
building — as a SIMPLIFICATION, deleting the interception, the compare,
the shadow and the packing — but it is not a speed lever and entry 15
should not be read as promising one.

I also worked for several hours against a stale ranking because I did not
`git pull`. The other thread's replies (801be42, 53700ee, 8681991) were
committed to this repo, not sent through chat. READ THE LOG BEFORE
RANKING WORK.

Their question 5 — how the game classifies a tile as CATEGORY 1, which is
48% of the saturated processor's work — is aimed at the actual bottleneck
and is where this thread should go next.

---------------------------------------------------------------------
## 28. The object table: 64 slots of 128 bytes, and the struct by census

The dispatcher at 0x398E, read against our bytes:

    398e:  movea.w #$C000,fp        object table base = 0xFFC000
    3996:  tst.b   (fp)             bit 7 of $00 = ACTIVE
    399a:  bpl.s   0x39B6           inactive: skip
    39a8:  movea.l 2(fp),a0         $02 = routine pointer
    39ac:  jsr     (a0)
    39b6:  lea     128(fp),fp       STRIDE 128
    39be:  cmpi.b  #64,$FFF109      64 SLOTS
    39c4:  bcs.s   0x3996

So objects live at 0xFFC000-0xFFDFFF, 64 x 128 bytes, and 0xFFF109 is the
current index. 0xFFF148 is a SOLO FILTER: when non-zero, only the slot
whose index matches runs its routine and every other slot is sent to
0x3F04 instead.

HANDOFF-DECOMPILE lists fields $00-$16. That is 23 bytes of 128, so 82%
of every object was undocumented.

**Derived by census, not by reading.** A6 is the object base by
convention (the dispatcher sets it; handlers inherit it), so every
`(d16,A6)` access in the program is a field access:

    tools/ghidra_run.sh script object_census.py OUT.json

5608 accesses, 90 of the 128 offsets touched. The heaviest:

    off   accs  funcs  writes/reads   note
    0x22   535    152      503/32     byte
    0x21   422    114      311/111    byte
    0x24   324     96      280/44     LONG — patch_game already knows
                                      this one: DATA_PTR_NORM 0xDBA8
                                      `movea.l (0x24,A6),A4`
    0x14   267     66      215/52     anim_frame (known)
    0x20   258     78      228/30     byte
    0x23   252     44      227/25     byte
    0x2E   242     80      228/14     byte
    0x00   225    120      196/29     status (known)
    0x02   101     74       99/2      routine pointer — 99 writes, and
                                      the 2 reads are the dispatcher

Four structural results the census gives for free:

  1. **0x22 is the busiest field in the program** — more accesses than
     any other, touched by 152 functions, and written 16 times for every
     read through A6. A flag handlers SET and something else consumes
     through a different register. It is the single most valuable unknown
     in the struct.
  2. **0x20-0x23 is a hot four-byte cluster**, all byte-sized and all
     write-dominated. Likely one group, not four unrelated flags.
  3. **0x6C is an ARRAY** — the census flags indexed access, which
     matches 0x5D18 `move.b (0x6c,A6,D0w),(0xa,A6)`: a per-object table
     of palette slots selected by index. 0x6C-0x6F.
  4. **The object is 128 bytes but the dense core is 0x00-0x4F.** Above
     0x50 the counts collapse into single digits and few functions —
     per-class scratch, not shared structure.

The untouched offsets are almost all ODD (0x03, 0x05, 0x07, 0x0F, 0x11,
0x13, ...), which is the interior of word fields and is a consistency
check on the widths rather than a gap.

UNPROVEN. This says WHERE the fields are and how they are used, not what
they mean. The width column in the generated map is crude — it takes the
dominant access size and stops at the next touched offset, so a long at
0x02 or 0x24 is displayed as 2 bytes because the following word is also
addressed directly. Read the size histogram, not the width.

NEXT: name 0x22 by watching it. Dump 0xFFC000-0xFFDFFF across consecutive
frames; fields that change every frame are position and animation, fields
that change only at state transitions are configuration. That separates
the two classes without reading a handler.

---------------------------------------------------------------------
## 29. The motion block, and HANDOFF-DECOMPILE's struct is WRONG from $0E on

Reading the high-fan-in functions (entry 30) gave the motion layout
outright, because the integrators state it in code.

    3f24:  move.w  $14(fp),d0      ; take the velocity
    3f28:  ext.l   d0
    3f2a:  lsl.l   #8,d0           ; << 8
    3f2c:  add.l   d0,$0C(fp)      ; ADD IT TO A LONG AT $0C
    3f32:  move.w  $1A(fp),d0      ; and the other axis
    3f3a:  add.l   d0,$10(fp)      ; LONG AT $10

    3f40:  move.w  $14(fp),d0      ; velocity
    3f44:  move.w  $16(fp),d1      ; delta
    3f4a:  add.w   d1,d0
    3f4c:  cmp.w   $18(fp),d0      ; against a limit
    3f50:  bgt.s   ...             ; clamp

So the motion block is

    $0C  long   X position, 16.16 fixed ($0C.w is the pixel, $0E the fraction)
    $10  long   Y position, 16.16 fixed ($10.w is the pixel, $12 the fraction)
    $14  word   X velocity, 8.8 (added to X as vel<<8, so 256 = 1 px/frame)
    $16  word   X acceleration
    $18  word   X velocity limit
    $1A  word   Y velocity
    $1C  word   Y acceleration   (by symmetry with 0x3F40's other arm)
    $1E  word   Y velocity limit

**HANDOFF-DECOMPILE lists `$0E x_vel, $12 y_vel, $14 anim_frame,
$16 anim_timer`. Those four labels are wrong.** $0E and $12 are the
FRACTIONAL halves of the positions at $0C and $10; $14 and $16 are the X
velocity and its acceleration. Treat the brief as superseded from $0E on
unless someone produces a handler that uses them as it says.

CONFIRMED ON A RUNNING FRAME. Dumping 0xFFC000-0xFFDFFF at frames
2400-2405 and counting per-byte changes over the 19 active slots:

    0x0D  13%    integer LOW byte of X   — moves
    0x0E  11%    fraction HIGH byte of X — accumulates
    0x0C   0%    integer HIGH byte of X  — never, in 5 frames

That is exactly the signature of a 16.16 position: the fraction and the
low integer byte move, the high integer byte does not. A word-sized
position at $0C with a separate velocity at $0E could not produce it.

(Change rates are low across the board because most of the 19 live slots
are static scenery and, at a 49% miss rate (entry 22), the game does not
advance on every frame. Rank the rates against each other, not against
100%.)

---------------------------------------------------------------------
## 30. Fan-in: 25 functions absorb 55% of the call graph, and 22 had no name

Ranking all 720 functions by caller count:

    callers  entry      size  what it is
        77   0x03352     104  unnamed (touches the MCU mailbox)
        76   0x03F04      28  HIDE THIS OBJECT'S SPRITE — named below
        62   0x03B2E      62  request_palette_update
        59   0x03BCE      30  release_palette_slot
        47   0x065AA      32  RESTORE POSITION — named below
        38   0x0669C     162  unnamed
        36   0x0D47E      58  unnamed
        34   0x03DD8     298  unnamed
        30   0x03F20       4  the motion integrator (entry 29)
        23   0x065CA     124  SAVE POSITION — named below

The top 25 absorb 725 of 1324 call edges. Three were named before today.
Naming here is worth ten times naming a handler.

**0x3F04 — hide this object's sprite.** This is what the dispatcher calls
for every slot the solo filter excludes (entry 28), and 76 other sites
call it directly:

    3f04:  bclr  #3,(fp)          clear status bit 3
    3f0c:  move.b $08(fp),d6      sprite_slot
    3f10:  lsl.w  #4,d6           * 16 bytes per record
    3f12:  add.w  #-2048,d6       + 0xF800 = THE RECORD POOL (entry 13)
    3f18:  clr.l  (a0)            zero word 0 = top/bottom

Zeroing word 0 makes top >= bottom, which the sprite hardware skips
outright (`s16b.txt`: "If the top value is equal to or greater than the
bottom value, the sprite is not displayed"). So this is the standard
hide, and it independently confirms that $08 indexes the pool at
0xFFF800.

**0x65AA / 0x65CA — restore and save position.**

    65aa:  move.w $40(fp),d0 ; move.w d0,$0C(fp) ; move.w d0,$28(fp)
    65b6:  move.w $44(fp),d0 ; move.w d0,$10(fp) ; move.w d0,$2A(fp)
    65ca:  move.w $0C(fp),$40(fp)      (the reverse)

So **$40 and $44 are a saved X/Y pair**, and $28/$2A are a mirror updated
alongside the live position. 47 callers restore, 23 save.

NEXT: 0x03352 (77 callers, the highest in the program, touches the MCU
mailbox) and 0x0669C (38 callers, 162 bytes) are the two biggest unnamed
pieces of infrastructure left.

---------------------------------------------------------------------
## 31. QUESTION 5 ANSWERED — cat1 is a ROM bit, fully static, decodable at bake time

The rendering thread's question 5 (801be42): how does the game decide a
tile is CATEGORY 1? Cat1 is 48% of the saturated slave's compose, and
CAT1MD was reverted for shimmer, which they read as a classification
defect "likely fixable if the priority rule is knowable statically".

**It is knowable statically. The game does not decide at all.**

`s16b.txt` section 6 gives the tile word: bit 15 is the PRIORITY FLAG,
the lower bits carry the palette and the 8192-entry tile index. Cat1 is
that bit. And bit 15 of the word is bit 7 of the HIGH BYTE — which is
exactly what the unpacker's first pass writes (entry 10, 0x16BE), from a
run-length stream of (count, value) pairs in the rom.

So the priority of every tile in every scene is determined before the
game runs. Decoding the high-byte pass straight out of the rom for all
five scenes:

    scene  data ptr   cat1 tiles   share   distinct high bytes
      0    0x29E00        2312     11.3%          13
      1    0x2EF10        8960     43.8%           6
      2    0x324D0        7360     35.9%           6
      3    0x369C0        1280      6.2%           5
      4    0x3B0E0        3520     17.2%           6

Five to thirteen distinct values across a whole 20480-tile scene is
itself a check on the decode; a wrong offset gives noise.

**VERIFIED AGAINST A RUNNING FRAME, EXACTLY.**

    ares-headless --frames N --input discover/inputs/play_level1.csv \
        --dump dram:0x12000:0xA000:tm.bin \
        --dump wram:0xFFF140:0x10:sc.bin  base.32x

Game tile RAM 0x400000 is FB staging 0x852000 is 32X DRAM 0x12000, so
the even bytes of that dump are the live high bytes. At frames 1200 and
2400, with 0xFFF142 = 0:

    live vs rom decode, byte for byte:   20480 / 20480   (100.0%)
    priority bit alone:                  20480 / 20480   (100.0%)

The rom decode reproduces the live map exactly, at two frames 1200 apart.

**Three consequences.**

  1. Cat1 promotion can be a BAKE-TIME decision. A per-scene bitmap of
     20480 bits is 2560 bytes; all five scenes are 12.5 KB. Nothing has
     to be classified per frame.
  2. A static decision CANNOT SHIMMER. Whatever CAT1MD's shimmer was, it
     was not the classification being genuinely ambiguous — the input is
     constant. That reopens CAT1MD as a fixable idea rather than a
     rejected one.
  3. Scene 3 is 6.2% cat1 and scene 1 is 43.8%. The cost of composing
     cat1 in software is a SEVENFOLD swing between scenes, so any
     measurement of "cat1 costs 48%" is scene-specific and level 1
     (scene 0, 11.3%) is at the cheap end.

**AND IT CLOSES MY OWN LOOSE END.** Entry 12 reported page 0 changing by
197 bytes between frames 700 and 1000 and read it as the scrolling plane
rewriting its incoming column. Wrong: the high bytes of the entire map
are byte-identical to the rom at frames 1200 apart, so the game does not
rewrite the map at all — it scrolls the view across static pages. Those
changing bytes were the R60 packet living inside page 0, which the
rendering thread has since moved to page 12 (LOOP29 138). Their
correction was right and my reading of it was wrong.

---------------------------------------------------------------------
## 32. The two biggest unnamed functions: the sound queue, and the animation driver

Entry 30 left 0x03352 (77 callers, the most in the program) and 0x0669C
(38 callers) unnamed. Both decode cleanly.

**0x3352 — enqueue a sound command, with de-duplication.**

    3352:  tst.b   d0              ; d0 = command
    3354:  beq.w   0x33B0          ; command 0 = RESET
    3372:  move.w  #$2700,sr       ; INTERRUPTS OFF while reading the
    3376:  movea.w $FFF03E,a0      ;   write pointer and
    337a:  move.b  $FFF03C,d1      ;   the count
    337e:  move.w  #$2300,sr       ; on again
    3396:  cmp.b   (a0)+,d0        ; scan the pending entries
    3398:  beq.s   0x33AE          ; ALREADY QUEUED -> drop it
    339a:  cmpa.w  #$F060,a0       ; ring wraps at 0xFFF060
    33a0:  movea.w #$F040,a0       ;   back to 0xFFF040
    33a8:  move.b  d0,(a0)         ; append
    33aa:  addq.b  #1,$FFF03C      ; count++
    33b0:  move.b  d0,$FFF03C      ; reset path: count = 0
    33b4:  move.b  d0,$FFF0C4      ;   and the MCU mailbox byte

So the sound queue is a 32-byte ring at 0xFFF040-0xFFF05F, write pointer
0xFFF03E, count 0xFFF03C, drained into the MCU mailbox at 0xFFF0C4. The
de-dup scan is why a held button does not stack a hundred copies of one
effect. 77 callers: everything in the game that makes a noise.

**0x669C — the animation driver. And it names $22, the busiest field in
the program.**

    669c:  move.w  $0C(fp),$40(fp)   ; save X   (confirms $40/$44, entry 30)
    66a2:  move.w  $10(fp),$44(fp)   ; save Y
    66a8:  movea.l $24(fp),a0        ; THE ANIMATION SCRIPT POINTER
    66ae:  move.b  $21(fp),d0        ; current frame index
    66b2:  subq.b  #1,$22(fp)        ; TICK THE FRAME TIMER
    66b6:  beq.s   0x66C0            ; expired -> advance
    66b8:  addq.l  #3,a0             ; not yet: skip this entry
    66c0:  addq.w  #1,d0             ; advance the frame index
    66c2:  cmp.w   (a0)+,d0          ; against the script's loop count
    66c6:  moveq   #0,d0             ; wrap
    66ca:  move.b  (a0)+,$22(fp)     ; RELOAD THE TIMER from the script
    66ce:  bclr #0,(fp) ; bclr #1,(fp) ; then pull new status bits,
    66da:  move.b  (a0)+,d0 ; or.b d0,(fp)  ;   offsets and sizes
    66f8:  btst    #7,$2E(fp)        ; flip flag
    66fe:  bchg #0,(fp) ; neg.w d0 ; neg.b d2 ; neg.b d3 ; exg d2,d3

**$22 is the ANIMATION FRAME TIMER** — a per-object countdown ticked here
and reloaded from the script when it expires. That explains every number
in the entry 28 census: 503 "writes" against 32 reads because
`subq.b #1,$22(fp)` is a read-modify-write; 228 of the immediate writes
being `#1` because that is a handler saying "advance on the next tick";
compares against 1 and 2 because callers test whether it is about to
expire; and 152 functions touching it because every animated actor does.

So the animation block is

    $21  byte  current frame index
    $22  byte  frame timer, counts down to 0 then reloads from the script
    $24  long  animation script pointer  (the field patch_game already
               normalizes as DATA_PTR_NORM 0xDBA8)
    $2E  bit 7 horizontal flip
    $32  word  read from the script each advance
    $40/$44   saved X/Y

This is further evidence HANDOFF-DECOMPILE's `$14 anim_frame,
$16 anim_timer` are wrong (entry 29): the real animation fields are $21
and $22, and $14/$16 are X velocity and acceleration.

CONSISTENT WITH THE RUNNING FRAME. $22 was the most-changing field across
frames 2400-2405 at 19%, highest of any offset. Not 100%, because most of
the 19 live slots are static scenery whose handlers never call this, and
the game skips work on missed vints (entry 22).

---------------------------------------------------------------------
## 33. 0x3DD8 — world-to-screen, culling, and the 192 turns up again

34 callers, 298 bytes, the third-largest unnamed piece of infrastructure.
It converts an object's world position into a hardware sprite position and
culls it if it lands off screen.

    3de4:  move.w  $10(fp),d0     ; Y (the integer half of the 16.16, entry 29)
    3de8:  subi.w  #4096,d0       ; world bias
    3dec:  add.w   $FFF0FA,d0     ; camera Y
    3df0:  sub.w   $FFF128,d0     ; Y offset
    3df6:  asr.w #1,d6 ; subx.w d6,d1 ; add.w d6,d0   ; half-height
    3dfe:  bmi.w   0x3F04         ; OFF TOP -> hide (entry 30)

    3e02:  move.w  $0C(fp),d2     ; X
    3e06:  subi.w  #4096,d2
    3e0a:  add.w   $FFF0F8,d2     ; camera X
    3e0e:  add.w   $FFF120,d2     ; X offset
    3e12:  addi.w  #192,d2        ; <-- THE VISIBLE-WINDOW ORIGIN
    3e1c:  cmpi.w  #512,d2
    3e20:  bcc.w   0x3F04         ; OFF RIGHT -> hide
    3e26:  cmpi.w  #185,d4
    3e2a:  bcs.w   0x3F04         ; OFF LEFT -> hide

    3e32:  move.b  $FFF018,d5
    3e36:  btst    #6,d5          ; CABINET SCREEN FLIP
    3e3c:  eori.b  #2,d4          ;   inverts the object's X-flip bit

**The 192 is the same 192.** Entry 24 measured the scroll conversion as
MD = S16 - 192 and argued it was the visible-window origin rather than a
convention. Here the GAME ITSELF adds 192 when turning a world X into a
hardware sprite X. Two independent uses of the same constant, one
measured on a running frame and one read out of the game's own code.
That closes any doubt about the direction of the scroll conversion.

The cull window, 185 to 512, sits against `s16b.txt`'s stated sprite X
range of 0x00B6 (182) leftmost to 0x1F5 (501) rightmost — the game's
margins are a few pixels inside the hardware's.

**Globals named by this routine:**

    0xFFF0F8  camera X        0xFFF120  X offset (shake/scroll trim)
    0xFFF0FA  camera Y        0xFFF128  Y offset
    0xFFF018  I/O latch; bit 6 = screen flip (cabinet DIP)

And it explains a large share of 0x3F04's 76 callers: every cull path in
this routine is one of them, so "sprite hidden" is frequently just "object
is off screen", which is another reason not to read a blank pool record as
a transport fault (notes section 7).

---------------------------------------------------------------------
## 34. Five more by fan-in: two hitboxes, the collision test, fixed player slots, depth banding

**0xD47E (36 callers) and 0xD4B8 (25) — build the two hitboxes.** Both
are the same shape on different field groups:

    d47e:  move.b $30(fp),d0 ; ext.w d0 ; add.w $0C(fp),d0 ; move.w d0,$34(fp)
           move.b $31(fp) -> $36      (the other X edge)
           move.b $32(fp) -> $38      (+ $10, the Y position)
           move.b $33(fp) -> $3A
    d4b8:  the identical four steps on $50-$53 -> $54-$5A

So each object carries TWO boxes, each as four SIGNED BYTE extents
relative to its position, expanded to absolute words on demand:

    BOX A   $30-$33 extents  ->  $34 $36 (X lo/hi)  $38 $3A (Y lo/hi)
    BOX B   $50-$53 extents  ->  $54 $56 (X lo/hi)  $58 $5A (Y lo/hi)

**0xD8BA (17 callers) — the collision test, and a second object group.**

    d8ba:  tst.w   $FFF130          ; collision enabled at all?
    d8c0:  lea     $FFD800,a4       ; a group of
    d8c4:  moveq   #13,d1           ;   14 entries,
    d8cc:  lea     128(a4),a4       ;   stride 128 — the OBJECT stride
    d8d6:  move.w  $56(a4),d4 ; sub.w $54(fp),d4 ; bcs -> miss
    d8e0:  move.w  $56(fp),d5 ; sub.w $54(a4),d5 ; bcs -> miss
    d8ea:  move.w  $5A(a4),d6 ; sub.w $58(fp),d6 ; bcs -> miss

A textbook axis-aligned overlap test on BOX B. 0xFFD800 is
0xFFC000 + 0x1800, i.e. object slot 48, so **slots 48-61 are the group
everything else is tested against.**

**0xD5A6 (34 callers) — fixed player slots.**

    d5a6:  lea $FFC000,a4 ; cmpi.b #1,$FFF028    ; slot 0  = PLAYER 1
    d5c0:  lea $FFC400,a4 ; cmpi.b #1,$FFF029    ; slot 8  = PLAYER 2
    d5d8:  lea $FFC080,a4                        ; slot 1

So the object table is not a free pool: **slot 0 is player 1 and slot 8
is player 2**, with 0xFFF028/0xFFF029 as their active flags.

**0x3D14 (19 callers) — the sprite builder with DEPTH BANDING.** It is
0x3DD8 (entry 33) with a preamble:

    3d14:  btst    #4,$2C(fp)      ; is this object depth-sorted?
    3d1e:  move.w  $10(fp),d1      ; Y
    3d22:  cmpi.w  #4232,d1        ; band 0
    3d2a:  cmpi.w  #4296,d1        ; band 1, else band 2
    3d32:  move.b  d0,$2F(fp)      ; store the band
    3d36:  ...then the normal world-to-screen build

Y is biased by 4096 (entry 33), so the band edges are screen rows 136 and
200. That is the beat-em-up depth rule — how far up the screen an actor
stands decides which of three priority bands it draws in — and $2F is
where the result lands, which makes $2F the sprite priority the port's
ladder consumes.

Running total on the struct: $00 status, $02 routine, $08 sprite slot,
$0A/$0B palette, $0C/$10 positions, $14-$1E motion, $21/$22/$24 animation,
$2C flags, $2F priority band, $30-$3A box A, $40/$44 saved position,
$50-$5A box B.

---------------------------------------------------------------------
## 35. CORRECTION to entry 30, the animation script format, and per-scene floor geometry

**ENTRY 30 IS WRONG about 0x65CA.** I named it "save position" from its
first two instructions and did not read the other 122 bytes. It saves the
position and then continues into a full animation update — script pointer
at $24, frame index at $21, tick $22, advance and reload — i.e. it is an
ANIMATION DRIVER VARIANT of 0x669C (entry 32), not a save routine. Entry
30's naming of 0x65AA (restore position) stands; that one was read to its
`rts`. My own rule, read to the return before naming, and I broke it.

**The animation script format.** 0x674E (24 callers) indexes it:

    674e:  movea.l $24(fp),a0      ; script
    6754:  move.w  (a0)+,d0        ; header word
    675a:  move.b  $21(fp),d0      ; frame index
    675e:  add.w d0,d0 ; move.w d0,d1 ; add.w d0,d0 ; add.w d1,d0
                                    ; = index * 6
    6766:  adda.w  d0,a0           ; SIX BYTES PER FRAME
    6768:  move.b  (a0)+,$48(fp)
    676c:  move.b  (a0)+,$49(fp)

So a frame entry is six bytes and $48/$49 receive two of them. 0x6654
(16 callers) is the same walk with `cmpi.b #1,$22(fp)` instead of a
decrement — a lookahead that reads the NEXT frame without consuming the
timer.

**0xD99C (17 callers) — floor collision against per-scene geometry.**

    d99c:  tst.w   $1A(fp)         ; Y velocity; only when falling
    d9a4:  lea     $DEC4(pc),a0
    d9aa:  move.b  $FFF142,d0      ; the SCENE index (entry 10)
    d9b2:  movea.l (a0,d0.w*4),a0  ; per-scene geometry pointer
    d9b6:  move.w  $5A(fp),d6      ; box B Y max (entry 34)
    d9be:  cmp.w   (a0),d6
    d9c6:  move.w  4(a0),d0 ; cmp.w $54(fp),d0

**A new data table: 0xDEC4**, five long pointers, one per scene — and the
sixth entry is out of range, the same five-and-stop shape as the scene
descriptor table at 0x1CE2. The scene 0 geometry at 0xDED8 is triples:

    1078 1440 14D8      Y = 0x1078, X from 0x1440 to 0x14D8
    1078 1508 1580
    1078 1738 1778
    1078 17A8 1810

Y is biased by 4096 (entry 33), so 0x1078 is screen row 120 — and the
depth bands in entry 34 sit at rows 136 and 200. These are walkable floor
segments: a height and an X span. That is the ground the actors stand on,
and it is static per-scene rom data like the cat1 map (entry 31).

Struct additions: $48/$49 from the animation frame entry, $68 read
alongside box B in the floor test, $7C used by 0xE1E8 to decrement
counters at 0xFFF154/0xFFF156.

---------------------------------------------------------------------
## 36. The floor heights ARE the depth bands — two readings cross-validate

Decoding all five per-scene geometry tables from entry 35:

    scene 0  @0x0DED8   8 segments   rows 120, 178, 216
    scene 1  @0x0DF0E   1 segment    row 216, spanning the whole X range
    scene 2  @0x0DF14  22 segments   rows 120, 184, 216
    scene 3  @0x0DF9E  11 segments   rows 120, 184, 216
    scene 4  @0x0DFE0   5 segments   row 118

Every scene's floors sit at three heights, and they are the same three:
~120, ~180, 216. Scene 1 is a single flat floor across the level.

**Entry 34's depth bands are at rows 136 and 200.** Those are exactly the
dividers between these three floor heights:

    floor 120  <  band edge 136  <  floor 180  <  band edge 200  <  floor 216

So the three walkable depths and the three sprite priority bands are one
system: an actor standing on the upper walkway draws behind one on the
middle floor, which draws behind one at the front. The depth rule I read
out of 0x3D14 and the geometry I read out of 0xDEC4 were derived
independently, from different routines and different data, and they line
up. That is a strong check on both.

Practical consequence for the port: an actor's priority band is a pure
function of its Y, the band edges are two constants, and the floors it can
stand on are static rom data. Nothing in the depth system needs observing
at runtime.

---------------------------------------------------------------------
## 37. The arcade-dependency census — the generalisable half of the kit

`tools/hazard_census.py`. This is the piece that makes a KIT rather than
a port: the reason each dependency matters is a property of the HARDWARE,
so a rule learned on one title holds for every title. TAS is the existence
proof — nobody found it by diffing two roms, somebody understood one game
well enough to know it relied on a locked read-modify-write, and that
became a rule about the class.

    tools/hazard_census.py LISTING.dis --code INSTRS.txt

**TAS — 4 confirmed in code, 1 candidate, and that is patch_game's 5.**

    0x02268  0x0E098  0x0EAC0  0x12E84     in the analysed code
    0x150B6                                 candidate, below the ceiling

`tools/game_altbeast.py` TAS_SITES lists exactly those five. 0x150B6 is
REAL code that the seeded project never reached, which patch_game found
by hand.

**STOP — 12 sites, none of them patched.** 0x1A9E6 and eleven more in
0x1B0CA-0x1B9E6, which is the service/test-mode region (entry 22 put the
test screen at 0x005BE, and these are its siblings). The game halts until
an interrupt the arcade guarantees; on 32X it resumes only if that
interrupt actually arrives. Not a live risk while the port never enters
test mode, and a hang the moment it does.

**I CLAIMED THIS REPRODUCED THE HAND KEY EXACTLY BEFORE CHECKING, AND IT
DID NOT.** Two bugs in opposite directions, both mine:

  1. FALSE POSITIVE. I reported 0xE1DC, which is `tas d2` — a DATA
     REGISTER. There is no bus cycle, so the dropped write phase cannot
     touch it. patch_game excludes it correctly and I did not.
  2. FALSE NEGATIVE, and the dangerous one. I filtered matches through
     Ghidra's instruction set and silently DISCARDED everything outside
     it — which would have thrown away 0x150B6, a real TAS that
     patch_game does patch. **Ghidra not reaching an address does not
     make it data.** The tool now reports those as CANDIDATES instead of
     dropping them.

The cheap resolver for candidates is the code ceiling (entry 16): no code
exists above 0x1EF1E, so three of the four candidates are data with no
hand check needed, and only 0x150B6 survives to be looked at. One
constant, re-derived per title, does most of the triage.

A scan that silently drops what it cannot confirm produces a clean-looking
list with the hard cases missing. Report the uncertainty.

---------------------------------------------------------------------
## 38. $3E is an object CLAIM LOCK, and that is why TAS matters here

Entry 37 left 0x150B6 as a candidate. It is unambiguously real code:

    150a6:  cmpi.b  #3,$23(a0)      ; inspect the TARGET object
    150ae:  cmpi.b  #4,$23(a0)
    150b6:  tas     $3E(a0)         ; TRY TO CLAIM IT
    150ba:  bne.s   0x1511A         ; already claimed -> give up
    150bc:  move.b  #0,$23(fp)      ; won it: reset OUR state,
    150c2:  clr.b   $21(fp)         ;   animation frame index,
    150c6:  move.b  #1,$22(fp)      ;   and timer (entry 32)

So $3E is a per-object **claim lock**: an actor test-and-sets it on
ANOTHER object, and on success takes it over and restarts its own
animation. 0xEAC0 does the same thing on the same field. $3C is a second
lock, claimed at 0x12E84. The five TAS sites are:

    0x02268  tas $FFC020        object slot 0 field $20 — PLAYER 1
    0x0E098  tas $FFF15A        a global
    0x0EAC0  tas $3E(a0)        claim another object
    0x12E84  tas $3C(fp)        claim on the second lock
    0x150B6  tas $3E(a0)        claim another object

$3E is touched 173 times and $3C 88 times, and both are released with a
plain `clr.b` (0xEADE, 0xEDCA, 0x150D4, 0x176C8, 0x18000) — claim with
TAS, release with a store, the standard pattern.

**This is exactly why the TAS dependency is load-bearing rather than
cosmetic.** If the latch never sets (entry 37: the MD bus arbiter drops
the write phase), every claim reports the object as free. Two actors then
both believe they own the same target, and the failure shows up as
interaction bugs — grabs, hits and transforms landing on something
already taken — not as a crash. That is the worst kind of dependency:
silent, gameplay-only, and invisible to any pixel diff on a still frame.

For the kit: a TAS in a System 16 title is very likely an object claim,
so the class matters for every game with actor-to-actor interaction, and
the replacement has to preserve the ATOMICITY, not just the value.

---------------------------------------------------------------------
## 39. The TAS replacement is not atomic, and on this game that is safe

Entry 38 showed $3E is a claim lock, so the TAS replacement's correctness
matters. `tools/patch_game.py` rewrites each site to a shim thunk that is

    tst.b <ea>      ; TAS's exact N/Z/V/C
    st    <ea>      ; set the byte, no CC change
    rts

**That is two bus cycles with a window between them.** A real TAS is one
indivisible read-modify-write. If an interrupt fires between the `tst.b`
and the `st`, and the handler claims the same lock, both claimants win —
precisely the double-claim entry 38 says the dependency exists to prevent.

**Measured: interrupt context cannot reach a claim.** Every call target
inside the IRQ4 handler body (0x2AAC-0x2C90) is one of six:

    0x2DBC  0x2E50  0x2E74  0x30B2  0x3108  0x3128

Walking those three levels deep reaches 24 functions, and **none of them
is a TAS site and none is the object dispatcher at 0x398E**. The
dispatcher is called only from the main loop (0x892, 0x91A, 0xA54, 0x1F66,
0x2036 and others), so object handlers — the only things that claim locks
— never run in interrupt context.

So the window exists and nothing can step into it. The replacement is
correct FOR THIS GAME, for a reason that is a property of the game's
structure rather than of the technique.

**FOR THE KIT, THIS IS A CONDITION TO RE-CHECK PER TITLE, NOT A SOLVED
PROBLEM.** The rewrite is safe only while no interrupt-context code
claims a lock. A System 16 title that runs object logic from its vblank
handler — which is a normal thing to do — would need a real atomic
replacement, and the failure would be an occasional double-claim: rare,
gameplay-only, and invisible to any still-frame comparison. Any title
adopting this kit needs this reachability check run, not assumed.
