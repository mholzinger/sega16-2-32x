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

---------------------------------------------------------------------
## 40. The remaining hot fields, characterised by how they are used

Profiling every immediate written to or compared against each hot field,
restricted to real code. This says what SHAPE each field has; it is not a
claim about meaning beyond what the shape forces.

    $20   set 0-8; tested 17,18,19; bit 7 (x31), bits 0/1 (x14/x12)
          BOTH a small value AND flags in the high bit. 78 functions.
    $21   set 0,1,2,15; tested 1,2,3,4,8; ANIMATION FRAME INDEX (entry 32),
          and the tests are handlers branching on which frame is showing
    $23   set 0-7 and 16; tested 0-8. A SMALL ENUM, ~9 states, 44
          functions. Entry 38 tests it on a TARGET object (`cmpi.b #3` and
          `#4`) before claiming the lock, so it is the actor's mode —
          what it is currently doing — and other actors read it to decide
          whether it can be acted on.
    $2C   bit 4 in 111 of 121 accesses; bits 5,6,7 rare. A FLAG BYTE whose
          only busy bit is 4 = depth-sorted (entry 34).
    $2E   bit 7 in 134 accesses, and `move.b #$80` 36 times. Effectively a
          ONE-BIT FIELD: horizontal flip (entry 32).
    $4A   set to 8,16,24,32,48,50,64,72 — ALL MULTIPLES OF 8 except 50 —
          and tested against 8,16,32,48,64. A size or distance in
          eight-pixel units. 39 functions.
    $4B   set -1,1,4,40; bits 0 and 1. A small enum plus two flags.
    $78   ONLY ever set to -1 and tested against -1, 29 sites. A word
          sentinel: "none / not set".

Two of these are worth the port knowing. **$23 is the actor mode** and is
read across object boundaries, so it is the field that decides whether an
interaction is legal — the same field entry 38's claim path inspects.
And **$78 is a pure sentinel**, so any tooling that diffs object state
should treat 0xFFFF there as "absent" rather than as a value.

METHOD NOTE: this profile only sees IMMEDIATES. A field set from a
register or a table never appears, so absence from a value list is not
evidence the field is unused — $22 has 152 writing functions and would
look thin here because the animation driver reloads it from a script
(entry 32).

---------------------------------------------------------------------
## 41. The three vint-context workers: palette drain, input edges, colour cycling

Entry 39 listed the six functions IRQ4 calls. Three carry the work.

**0x2DBC — the palette upload queue DRAIN.** The consumer of the queue
entry 3 saw being built at 0x3BEC:

    2dbc:  movea.l $FFF402,a2      ; queue base
    2dc4:  tst.b   ($FFF406)       ; anything queued?
    2dc8:  movea.l (a2)+,a1        ; destination
    2dca:  movea.l (a2)+,a0        ; source
    2dcc:  seven `move.l (a0)+,(a1)+`   = 28 BYTES = 14 WORDS
    2dda:  cmpa.l  #$FFFFF800,a2   ; ring wrap

Seven longs is 28 bytes is 14 words — **exactly one sprite palette**
(entry 3: `0x242A0 + 28*index`). Builder and drain agree on the size from
opposite ends of the pipeline, which is the cleanest confirmation of the
palette format yet. Called from IRQ4 at 0x2B4C.

**0x2E74 — input edge detection.**

    2e86:  move.b  $FFF003,d1      ; last frame's bits
    2e8a:  move.b  d0,$FFF003      ; store this frame's
    2e8e:  not.b   d0
    2e90:  and.b   d1,d0           ; the EDGES
    2e94:  btst    #3,d0 ; bsr 0x2FBA

Standard "newly pressed this frame" computation. 0xFFF000-0xFFF00B is the
input state block, and 0xFFF000 carries a counter cleared past 10.

**0x30B2 — the colour-cycling streamer, writing palette RAM directly.**

    30b2:  lea     $FFF300,a5      ; a table of cycling slots
    30b6:  move.b  (a5),d0 ; bpl   ; bit 7 = slot active
    30bc:  subq.b  #1,1(a5)        ; per-slot countdown
    30c2:  lea     $840000,a1      ; PALETTE RAM
    30cc:  lsl.w   #4,d0           ; line * 16
    30d0:  movea.l 2(a5),a0        ; the cycle script
    30d4:  move.w  6(a5),d0        ; script index, wraps on (a0)

So slots at 0xFFF300 are 8 bytes each: active/line byte, timer byte, a
long script pointer, a word index. This is the glow and fade animation,
and it writes **straight into palette RAM every vint, bypassing the
upload queue entirely**.

That last point matters for the port: there are TWO palette write paths,
not one. The queue (0x3BEC build, 0x2DBC drain) handles whole 14-word
sprite palettes; the cycler pokes individual lines at 0x840000 on its own
schedule. Anything that models palette delivery has to cover both.

It also confirms a port fact from the other side: `patch_game.py`'s
DATA_PTR_NORM normalizes "0x30D0 palette-cycle streamer (glow/fade tables
at low 0x1A78E)" — 0x30D0 is the `movea.l 2(a5),a0` above, so the field
being normalized is this table's script pointer.

---------------------------------------------------------------------
## 42. POSSIBLE GAP — the colour cycler's palette writes have no dirty site

Following entry 41. The cycler's actual stores are at 0x30F8-0x30FC:

    30ee:  ...d0 = index * 18        ; 18 bytes per script entry
    30f2:  move.w  (a0)+,d0
    30f4:  move.b  d0,1(a5)          ; reload the slot's countdown
    30f8:  move.l  (a0)+,(a1)+       ]
    30fa:  move.l  (a0)+,(a1)+       ] 12 bytes = SIX COLOURS
    30fc:  move.l  (a0)+,(a1)+       ]   into palette ram via a1

a1 was set at 0x30C2-0x30CE to `0x840000 + (line * 16)`, which
`patch_game.py` remaps to the 0xFF9000 mirror.

**`tools/game_altbeast.py` PAL_DIRTY_SITES has 42 entries and NONE of them
is in 0x30B2-0x3110.** So these writes land in the mirror but are never
flagged in the dirty bitmap that tells the SH-2 which regions to copy.

I am NOT calling this a bug — I do not know the dirty-bit semantics well
enough. It is a question with a specific shape, and there are at least
three ways it could be fine:

  - the PAL32 block bitmap is "installed all-dirty" (pal_thunks.h), so if
    nothing ever re-clears these blocks the writes are carried anyway;
  - another site in the list may already cover the same blocks, since the
    granularity is 32 words, not one write;
  - the cycler may only run in scenes the port does not reach yet.

What makes it worth asking rather than dropping: the symptom of a missed
palette region is exactly the failure family this project keeps hitting —
colours that are right on a still frame and wrong in motion, which is also
what killed CAT1MD on the play pass. A cycler is BY DEFINITION only
visible in motion, so a still-frame comparison cannot see it either way.

Raised in the builder notes section 10 with the addresses.

---------------------------------------------------------------------
## 43. REFINES entry 42 — the dirty-site exclusion is structural, and a THIRD palette path

Checked the other two palette writers against PAL_DIRTY_SITES before
letting entry 42's question stand:

    0x3116  per-scene block writer   COVERED
    0x30C2  colour cycler            not covered
    0x2DC8  the QUEUE DRAIN itself   not covered

**The queue drain is the main sprite palette path and it is also not
covered.** Sprite palettes plainly work, so there must be another
mechanism, and entry 42's alarm was probably misplaced.

The pattern is a design consequence, not an oversight. The dirty-bit
thunks root at a `lea` whose target is known AT PATCH TIME — the same rule
TILE_DIRTY_SITES states. 0x3116 is `lea $840040,a1`, a constant, so it
qualifies. The cycler computes its line at runtime
(`lsl.w #4,d0 ; adda.w d0,a1`) and the drain loads its destination from
the queue, so neither destination exists until the frame runs. **The
mechanism cannot cover dynamically addressed writes by construction.**

So the question narrows from "is the cycler missed?" to "whatever carries
the queue drain's writes — does it also carry the cycler's?" That is a
much cheaper question for them to answer, and it may well be yes.

**And there is a THIRD palette path.** 0x3108, also called from IRQ4:

    3108:  lea     $32AE(pc),a0
    310e:  move.b  $FFF142,d0      ; the scene index
    3112:  lsl.w   #5,d0           ; 32 bytes per scene
    3116:  lea     $840040,a1      ; colour entries 32-47
    3120:  eight `move.l (a0)+,(a1)+`   = 32 bytes = 16 words

A per-scene 32-byte palette block at **0x32AE**, rewritten every vint into
entries 32-47 — two eight-colour tile lines. The data is plainly gradient
ramps (scene 0: 068B 057A 0469 0358 ...), which is the sky. Another
static per-scene table in the same family as the cat1 map (entry 31) and
the floor geometry (entry 35), and likely relevant to the flat-sky
symptom the port has recorded.

So the palette write paths are: the QUEUE (built 0x3BEC, drained 0x2DBC,
14-word sprite palettes), the CYCLER (0x30B2, 6 colours on its own
countdown), and this PER-SCENE BLOCK (0x3108, 16 words every vint).

---------------------------------------------------------------------
## 44. Two sky palette writers — and my oracle run was INVALID, by a rule this project already wrote down

Following entry 43, there are TWO routines writing colour entries 32-47
from per-scene 32-byte tables, and they disagree by construction:

    0x3838  table 0x4050   both 8-word halves IDENTICAL in all 5 scenes
            called once from 0x8E4
    0x3108  table 0x32AE   both halves DIFFER in all 5 scenes
            called from 0x2BD6, inside the IRQ4 handler

0x3108 sits on the NON-BLANK branch of a fade routine: when d0 is non-zero
0x2BC8 zeroes 64 colour entries and skips both sky writers; when it is
zero, 0x2BD0 copies 64 words and then calls 0x3108.

**MISTAKE 1, caught before it shipped.** I compared live memory against
table 0x32AE, saw the second half differ, and briefly had a "the port
drops half the sky" bug. It matches table 0x4050 EXACTLY, 16 of 16. I had
compared against the wrong writer's table.

**MISTAKE 2, the methodology one.** Our rom shows the flat table at every
sampled frame from 700 on, so I ran `mame altbeast` as the oracle and
dumped 0x840040 at the same frame numbers:

    frame   300   arcade FFFF...      ours FFFF...        agree
    frame   700   arcade flat         ours flat           agree
    frame  1000   arcade flat         ours flat           agree
    frame  1800   arcade flat         ours flat           agree
    frame  2400   arcade DIFFERING    ours flat           ??
    frame  3000   arcade scene 1      ours scene 0        ??

The last two rows are worthless. **The arcade run had no inputs, so it is
in attract mode, while ours is playing level 1 from
`play_level1.csv`.** By frame 3000 the arcade is in SCENE 1 and we are
still in scene 0 — different game states, compared by frame number.

That is precisely the error this repo already documents: entry 25 and
`tools/attract_parity.py` both exist because frame-number alignment across
differently-driven runs is meaningless. I wrote entry 25's warning myself
this session and then did it anyway.

**WHAT IS ACTUALLY ESTABLISHED:**

  - Frames 300-1800, same state on both: arcade and port agree EXACTLY.
    The port is correct in that window and the flat sky there is right.
  - The arcade at frame 2400 shows a palette with DIFFERING halves, so
    the gradient path is reachable on real hardware — 0x3108 is not dead
    code.
  - Whether OUR rom reaches it at the equivalent game state is UNTESTED.

To settle it the arcade has to be driven with the same inputs and aligned
on the game's own timeline, which is what `tools/attract_parity.py`
already does. Not asserted to the builder thread until then.

---------------------------------------------------------------------
## 45. The last fan-in tier: Y clamp, a third hitbox, the text stride writer, and the RNG

Fan-in has flattened — after 0x3F72's 26 callers the next unnamed function
has 8 — so this is the end of the productive ranking.

**0x3F72 (26 callers) — the Y velocity clamp, and it VERIFIES entry 29.**

    3f72:  move.w  $1A(fp),d0      ; Y velocity
    3f76:  move.w  $1C(fp),d1      ; Y acceleration
    3f7c:  add.w   d1,d0
    3f7e:  cmp.w   $1E(fp),d0      ; Y velocity limit
    3f84:  move.w  d0,$1A(fp)

Entry 29 derived $1C and $1E "by symmetry with 0x3F40's other arm" —
an inference, flagged as one. Here is the actual routine using exactly
those three fields. The motion block is now read, not guessed, on both
axes.

**0xD4F2 (6 callers) — a THIRD hitbox.** Same shape as entry 34's two:

    d4f2:  move.b $5C(fp) -> +$0C -> $60(fp)
           move.b $5D(fp) -> +$0C -> $62(fp)
           move.b $5E(fp) -> +$10 -> $64(fp)
           move.b $5F(fp) -> +$10 -> $66(fp)

    BOX A  $30-$33 -> $34-$3A     (0xD47E)
    BOX B  $50-$53 -> $54-$5A     (0xD4B8)
    BOX C  $5C-$5F -> $60-$66     (0xD4F2)

Three boxes per object, all signed byte extents expanded on demand. $68,
which entry 35 saw read alongside box B in the floor test, sits just past
box C.

**0x3A9A / 0x3AA4 / 0x3AAE — the text-RAM stride writers.**

    3a9a:  move.b (a0)+,(a1)+ ; addq.l #1,a1     ; write every OTHER byte
    3aa4:  clr.b  (a1)        ; addq.l #2,a1     ; clear every other byte
    3aae:  lea $410000,a1 ; adda.w $FFF024,a1    ; text ram + a cursor

The same one-byte-of-each-word idiom as the tilemap unpacker (entry 10),
here for the text layer, with the write position held at 0xFFF024.

**0x3FBE (5 callers) — the random number generator.**

    3fbe:  move.l  $FFF014,d1      ; the SEED
    3fc2:  bne.s   0x3FCA
    3fc4:  move.l  #$2A6D365A,d1   ; default seed if zero
    3fcc:  asl.l #2,d1 ; add.l d0,d1 ; asl.l #3,d1 ; add.l d0,d1
    3fd6:  swap d1 ; add.w d1,d0 ; ...

A multiply-and-fold generator seeded from **0xFFF014**, with a fixed
fallback constant. Worth knowing for two reasons: it explains why our
runs are byte-reproducible (entry 25's control) — the seed is
deterministic, not sampled from a timer — and any future attempt to
compare two runs that diverge should check whether this seed diverged
first.

---------------------------------------------------------------------
## 46. Seventeen per-scene tables — the data region starts to open up

The scene index at 0xFFF142 is the key to the data half of this rom.
55 real-code sites read it; 17 of them index a table. Enumerating those
enumerates the game's per-scene data.

    table     stride  feeds
    0x01CE2     6     scene descriptor: palette block + tilemap ptr  [10]
    0x0326E     4     indexed at 0x2B70
    0x032AE    32     sky palette, gradient halves                   [43]
    0x04050    32     sky palette, identical halves                  [44]
    0x06DC0     2     a word that indexes 0x73E4 and 0x6D70
    0x073DA     1     -> $0B palette_index   AB AC AD A9 07
    0x092EA     1     -> $0B palette_index   0C 0E 10 12 14
    0x092F0     4     PER-SCENE DISPATCH, `jmp (a0)`
    0x099A2     4     -> $6C
    0x01858     1     -> sound command       94 95 96 94 95
    0x0DEC4     4     floor geometry                                 [35]
    0x1D32A     4     walked with fp = 0xFFD800, the collision group [34]
    0x1D33E     2     self-relative word offsets                     [35]
    0x173A0     1     -> $0B palette_index   4B 4C 4D 4E 4A
    0x17E24     4     PER-SCENE DISPATCH, `jmp (a0)`
    0x1C622     4     five blocks of 0x288 bytes
    0x1724C     -     NOT a pointer table — all five decode to
                      implausible addresses. Word data, my guess was wrong.

**Every pointer table has exactly FIVE valid entries and a sixth that is
garbage** — 0x92F0, 0x99A2, 0x1D32A, 0xDEC4, 0x1C622 all do it, the same
signature the scene descriptor showed in entry 10. Five scenes, and the
tables simply stop. That is now a reliable shape for spotting a per-scene
table and for knowing where it ends.

Three of them are immediately legible:

  - **0x1858 is the per-scene music**: sound commands 0x94, 0x95, 0x96,
    0x94, 0x95, handed to the sound queue at 0x3352 (entry 32). Scenes 0
    and 3 share a track, as do 1 and 4.
  - **0x73DA, 0x92EA and 0x173A0 are per-scene palette identities**, each
    a byte written straight to $0B. They run in sequence per scene
    (AB AC AD A9 07 / 0C 0E 10 12 14 / 4B 4C 4D 4E 4A), which is three
    different actor classes each getting their own palette per level.
  - **0x17E24 and 0x92F0 are per-scene DISPATCH tables** — `jmp (a0)`
    through a pointer chosen by scene. 0x17E24's targets (0x1769E,
    0x17FE2, 0x1891C, 0x191D0, 0x199F6) are the level scripts.

Combined with entries 31, 35 and 44, the static per-scene data now known
is: the tilemap and its cat1 priority bits, the floor geometry, two sky
palettes, three actor palette identities, the music track, the collision
group setup, and two dispatch tables. All of it is rom, all of it
decodable before the game runs.

---------------------------------------------------------------------
## 47. CORRECTS entry 17 — the patcher's own tables reconcile against mine

`tools/game_altbeast.py` REBASE_TABLES lists the dispatch tables the port
already rebases: 0x26DC(8), 0x6D70(8), 0x6D90(12), 0x92F0(6), 0xF556(5),
0x17E24(5), 0x1A076(9). Checking those against entry 17's harvest:

**MY ENTRY 17 IS WRONG about 0x6D70.** I reported "0x06D70, 20 entries"
because my harvester reads longs until one stops looking like a code
address. It ran straight through the end of 0x6D70's EIGHT entries and
into 0x6D90's TWELVE, reporting them as one table of 20. The patcher has
it right and the boundary is real — all 8 at 0x6D70 and all 12 at 0x6D90
are plausible code pointers, and they are different tables.

I half-noticed this at the time (entry 17 listed a separate 0x6DA0 table
whose entries overlap 0x6D70's claimed range) and did not chase it. The
patcher's comment says why the extents are HARD-BOUNDED: "0x6DC0+ is a
WORD index table" — which is exactly the per-scene word table entry 46
found at 0x6DC0. Three sources agree on the boundary.

**Three dispatch tables my scan missed entirely**: 0x6D90(12), 0xF556(5),
0x1A076(9). All verify — every entry is a plausible code address. They
were missed because entry 46's sweep keyed on the SCENE index at
0xFFF142, and these are indexed by something else. A scene-indexed search
finds per-scene tables and nothing more, which is a limit of the method,
not of the rom.

**One of mine the patcher does not list**: 0xDE22(6), all six plausible
and pointing immediately after themselves (0xDE36, 0xDE46, 0xDE56...).
Either it is genuinely not rebased because its targets need no fixup, or
it is a gap in REBASE_TABLES. Worth a question to the builder rather than
an assertion — the consequence of a missed rebase is a jump into
unrelocated space, which is loud, so it is probably the former.

Lesson for the harvester: "read entries until one looks implausible" MERGES
ADJACENT TABLES. Bound them by the next known table start instead, or by
a stated extent. The false table it produced was 20 entries of real code
pointers, so nothing downstream would have flagged it.

---------------------------------------------------------------------
## 48. The sprite pipeline's data: frame table, zoom table, and a BANDED order list

Entry 46 left two large unattributed blocks. Finding who READS them cracked
both, and turned up a structural fact about the sprite order list.

**0x255E0 is the SPRITE FRAME TABLE**, read by 0x3C84:

    3c86:  move.w  $06(fp),d0      ; sprite_id
    3c8a:  add.l d0,d0 ; adda.l a0,a0 ; adda.l d0,a0   ; * 6
    3c90:  move.l  #$255E0,d2      ; the base
    3c9c:  move.w  (a0)+,d1        ; a word
    3c9e:  movea.l (a0),a1         ; and a long pointer

Six bytes per sprite_id: a word and a pointer. `patch_game.py` already
knows this base — its IMM_OVERRIDES comment calls 0x3C92 "sprite
frame-table base consumed via adda.l D2" — so the port had the address
without the structure. The valid prefix is **182 entries**, ending at
0x25A24, and the word field spans 0x0F34-0x15C0, which straddles the 4096
world bias (entry 33), so it is a Y coordinate.

**0x20000 is the ZOOM SCALE TABLE**, read by 0x3CD4:

    3cd4:  lea     $20000,a2
    3ce0:  move.b  (a0)+,d0        ; a size class from the frame data
    3ce2:  lsl.w   #5,d0           ; 32-BYTE ROWS
    3ce6:  move.b  $4E(fp),d0
    3cea:  andi.w  #31,d0          ; 0-31 — the ZOOM LEVEL
    3cee:  move.b  (a2,d0.w),d6    ; scaled result

MacDonald's notes give System 16 zoom as 5 bits, 0 to 31 (entry 21), and
this indexes exactly 0..31 within a 32-byte row. The rows are a scale
ladder — row 3 reads `03 03 03 03 03 03 03 03 03 03 03 02 02 02 02 02`,
holding at 3 then dropping to 2 — and the monotonic decay curve entry 46
spotted at 0x210C4 is more of the same table. **$4E is the zoom level.**

**AND THE ORDER LIST IS BANDED BY PRIORITY.** 0x3CA6:

    3cb6:  lea     $FFEC80,a0      ; the order list (entry 13)
    3cbc:  move.b  $08(fp),d0      ; sprite slot
    3caa:  move.b  $2F(fp),d1      ; the PRIORITY BAND (entry 34)
    3cae:  lsl.w   #6,d1           ; * 64
    3cb0:  move.b  d0,(a0,d1.w)    ; band*64 + slot

So the 256-byte order list entry 13 found is **FOUR BANKS OF 64**, indexed
by the depth band. That is why it is 256 bytes, and it means the upload
loop's walk order IS the draw order: band 0 first, then 1, 2, 3. Three
findings that were separate — the order list (13), the depth bands (34)
and the floor heights (36) — are one mechanism.

Block A also holds four TILE UPLOAD BLOCKS at 0x26C20, 0x2726C, 0x278B8
and 0x28B84, fed to the blitter at 0x258A as [dest][count][count][words].

---------------------------------------------------------------------
## 49. CORRECTS entry 48 — the frame table's word is an OFFSET, not a Y coordinate

Two errors in entry 48, both from reading four instructions and stopping.
The fifth and sixth settle it:

    3c9c:  move.w  (a0)+,d1        ; the word
    3c9e:  movea.l (a0),a1         ; the long
    3ca0:  add.l   d2,d1           ; d1 = word + $255E0   <-- THE BASE
    3ca2:  movea.l d1,a0           ; THAT is the frame pointer
    3ca4:  rts

**ERROR 1: the word is a 16-bit OFFSET from 0x255E0, not a Y coordinate.**
Entry 48 saw the words spanning 0x0F34-0x15C0, noticed that straddles the
4096 world bias, and called it a Y coordinate. `add.l d2,d1` says
otherwise, and d2 is the table base loaded six instructions earlier. The
offsets resolve to 0x26514-0x26BB0, which sits immediately below the tile
upload block at 0x26C20 — a clean, bounded region. The coincidence with
the world bias is exactly that.

**ERROR 2: "182 valid entries" was a bogus filter.** I counted entries
whose LONG field looked like a plausible rom address. Following those
pointers shows them spread uniformly across all 64 4KB buckets of the rom
— the signature of noise — and one of them lands on executable code. Read
by the correct rule instead, **400 consecutive entries** resolve into the
bounded frame-data region.

**The long at +2 remains UNEXPLAINED.** It is loaded into a1 and I have
not traced a consumer. Entry 48 implied more than that. For id 0 it reads
0x0002000A, which as an address is inside the zoom table region, but id 2
is zero and id 3 is 0x2C, so a single reading does not cover them. Left
open rather than guessed.

**THIS IS THE THIRD TIME TODAY** I have asserted structure from a
plausibility filter without following the values through: entry 17 merged
two tables by reading until entries "stopped looking valid" (corrected in
47), entry 37 discarded real code for the same reason, and now this.
The filter is the same each time — "does this look like an address" — and
it is never conclusive. The check that works is following the value to
its consumer, which took two more instructions here.

Standing corrections to entry 48: the frame table is 400 entries of
[offset word][unexplained long]; the frame data is 0x26514-0x26BB0. The
ZOOM TABLE and BANDED ORDER LIST findings in that entry are unaffected —
both were read from the instructions that use them, not filtered.

---------------------------------------------------------------------
## 50. Verification audit: every wrong claim came from the same method

Classified the session's 27 load-bearing claims by HOW each was
established, not by what it asserts.

    established by reading the CONSUMING instruction, or by a
    running frame                                              21
    established by a plausibility filter, a partial read, an
    absence, or an invalid comparison                            6

**The split is perfect. All six in the second group were wrong or
misleading. All 21 in the first group still stand.**

    [17]  0x6D70 "20 entries"          filter: read until implausible
    [30]  0x65CA "save position"       partial read: 2 of 124 bytes
    [37]  census == TAS_SITES          filter: false positive + false negative
    [42]  cycler has no dirty site     absence, with no control case
    [44]  arcade oracle disagrees      frame-number A/B on differently-driven runs
    [48]  frame table 182 / word = Y   filter, plus a number matching a
                                       constant I already held

So the verification pass does not need to re-derive anything. It needs to
ask ONE question per claim: **which instruction consumes this value?** A
claim that can name it was already verified when it was made. A claim that
cannot is a hypothesis regardless of how reasonable it reads.

That is a provenance test, not a quality judgement, and anyone can apply
it without knowing the subject matter.

**The four situations where the bad method gets used:**

  1. **A number matches something already known.** The frame table's words
     span 0x0F34-0x15C0 and the world bias is 4096. The match was
     generated before any check. Having just established the bias is what
     made it salient — the same knowledge that made other entries
     productive manufactured this one.
  2. **An unmarked boundary.** Tables do not declare their length, so any
     stopping rule is a guess wearing a measurement's clothes. It merged
     two real tables into one false one of 20 valid pointers, which
     nothing downstream would have flagged.
  3. **An absence.** Ghidra had not reached it; there was no dirty site.
     Absence is the weakest evidence available and I twice treated it as
     strong. The fix both times was a control: is the KNOWN-GOOD case also
     absent? For the palette question it was, immediately.
  4. **The first coherent reading.** Two instructions of 124 cohered into
     "save position", so I stopped. Coherence is not completeness.

**The uncomfortable part, and the reason the rule has to be mechanical:
the wrong claims did not feel different from the right ones.** Entry 48
asserted the banded order list (correct, consumer-read) and the Y-coordinate
frame table (wrong, filtered) in the same breath with the same confidence.
Confidence does not track provenance, so only provenance can be checked.

---------------------------------------------------------------------
## 51. Completion status of the disassembly, measured rather than claimed

`tools/ghidra/func_profile.py` profiles ALL 720 functions mechanically —
size, callers, callees, the object fields each touches through A6, its
work-RAM globals, the hardware regions it reaches, and its terminator.
720 functions, 63895 bytes.

What the terminator column exposes, which the "99.82% of instructions"
figure hid: **instruction coverage is excellent and FUNCTION BOUNDING is
not.**

    end in a real terminator (rts/jmp/bra/rte)   474   55128 bytes
    end mid-stream (ori.b, move.b, ...)          154    3918 bytes
    other                                         92

The 154 are two different things. Most are legitimate: 0x673E is a
four-byte entry that falls straight through into 0x6742 and shares its
`rts`, which is normal hand-written 68000 and not an error. A few are
mine: functions I SEEDED AT DATA ADDRESSES.

By class, the 720 are:

    pure / register only          277
    object handlers (A6 fields)   258
    touch the object table         72
    touch hardware                 71
    work RAM only                  42

**AND I ALMOST MADE THE SAME MISTAKE A FOURTH TIME.** Six of the
badly-terminated entries — 0x6DCA, 0x6E22, 0x6E7A, 0x6ED2, 0x6F2A,
0x6F82, each exactly 88 bytes and evenly spaced — decode as 22 ascending
code-range longs apiece. That is a textbook 6x22 jump table and I was
about to write it up as one.

Two independent checks say no:

    pointers landing on known function entries:  0/22, 0/22, 0/22,
                                                 1/22, 12/22, 1/22
    real-code instructions referencing the region:  0

Nothing reads it, and the values mostly do not point at function entries.
It might be jump tables into mid-function labels, it might be unreached
code, it might be art. **I do not know, and entry 50's rule says an
unread structure is a hypothesis.** Left unresolved at 0x6DCA-0x6FDA,
528 bytes.

This is the fourth time today the "does it look like an address" filter
produced a confident structure (entries 17, 37, 48, and this). The
difference is that this time the check ran BEFORE the claim, because
entry 50 made the check mechanical. That is the only thing that changed.

**So the honest completion status:** every instruction the reference
disassembly has, we have. 474 functions are cleanly bounded. 148 are
fall-through entries that are correct as they stand. Six are unresolved.
The remaining work is naming, not disassembly — 720 functions profiled,
about 45 named.

---------------------------------------------------------------------
## 52. Function bounding repaired: 101 bogus functions removed, 165 mis-bounds down to 71

Entry 51 measured the defect. `tools/ghidra/fix_bounds.py` repairs it,
run to a fixpoint over three passes.

**Root cause, traced.** `tools/ghidra/seed_harvest.py` read the LINEAR
objdump listing to harvest immediate routine pointers, without filtering
the SOURCE SITE to real code. A `move.l #imm,d(aN)` that exists only
because a linear sweep disassembled data yields a function seed at
whatever that immediate happens to be. Running the harvester with a code
filter now shows the scale: **2156 of 2435 candidate sites are phantoms
over data.** The 279 real ones give 262 targets; the unfiltered run gave
275, and the extra ones landed in padding.

Those bogus functions were invisible in every earlier metric. 0x0000
disassembles as `ori.b #0,d0`, so a run of padding looks like a function
body, and entry 19's "100% of the reference's function starts" was true
and said nothing about the 101 extra entries that were not functions at
all.

**The repair.** Delete a function whose body is mostly zero bytes and
which nothing calls; extend one that ends mid-stream at an address that
is not another function's entry, by deleting and recreating it so Ghidra
recomputes the body from flow. Leave alone one that ends exactly where
the next begins — that is a fall-through and is normal hand-written
68000. Three passes were needed because the first deletions removed the
bogus CALLERS that were keeping other bogus functions alive.

                    before   after
    functions          720     618
    terminated         474     473
    fall-through        81      74
    MIS-BOUNDED        165      71
    correctly bounded  555     547  (77% -> 89%)
    reference starts   433     433  <- nothing real was lost

**The guard that made this safe to do:** 433 of 433 reference function
starts are still present after deleting 101 functions. Without that check
a cleanup like this is indistinguishable from damage.

71 remain. They stop shrinking — the same 69 are offered for extension
every pass and re-extending does not change them — so they need a
different approach, not another iteration. `seed_harvest.py` now takes
`--code` and warns loudly without it.

---------------------------------------------------------------------
## 53. CORRECTS entries 51 and 52 — my mis-bounding metric was measuring nothing

Entries 51 and 52 counted a function as mis-bounded when its last
instruction was not a terminator AND `entry + size` was not another
function's entry.

**A Ghidra function body is an ADDRESS SET, not a range.** 36 of the 618
bodies have more than one range, so `entry + numAddresses` is not the end
of anything. 0x4870 has FOUR ranges spanning 4450 bytes while holding 1222
addresses; 0x500 has two spanning 6484. Every conclusion I drew from that
arithmetic was drawn from a number with no meaning.

That is why `fix_bounds2.py` reported "71 re-formed" on six consecutive
rounds with 71 still remaining. It was not failing to fix them. There was
nothing there to fix, and my survey kept re-reporting the same non-defect.

**Second error inside the correction.** My first honest re-measure asked
"does the body contain a terminator at all" and returned 112 defects —
including 0x3952, which is `set_level_palettes`, which I read by hand in
entry 3 and know ends in `rts` at 0x397C. Its body stops at 0x3972 because
0x3972 IS ITS OWN FUNCTION, reached by the `bsr` at 0x395E. 0x3952 falls
through into it and shares its exit. Correctly bounded; my test was still
wrong.

**The metric that finally holds.** A function is well-bounded if its body
contains a terminator, OR its last instruction falls through to another
function's entry:

    functions                                          618
      bodies with more than one range (normal)          36
      no terminator, falls through to an entry (fine)   49
      NO terminator and no fall-through (real defect)   63

**63, not 165 and not 71 and not 112.** Entries 51 and 52's headline
numbers are superseded by this one.

**What survives from entry 52 unchanged:** the 101 deleted functions were
genuinely bogus — zero-bodied, uncalled, sitting in padding — and that
finding rested on byte content and caller counts, not on the broken
arithmetic. The root cause it traced (phantom sites in the harvester,
2156 of 2435) is also unaffected, and the fix to `seed_harvest.py` stands.

**Fifth instance today, and the first one inside a measurement.** The
other four were claims about the rom. This was a claim about MY OWN TOOL
OUTPUT, which is worse, because every number downstream inherited it. The
tell was available the whole time: `fix_bounds2` reporting identical
counts across six rounds is not a stubborn bug, it is a survey that is not
looking at what it thinks it is.

---------------------------------------------------------------------
## 54. Bounding complete: 560 functions, 10 defects, nothing real lost

Entry 53 fixed the metric; this finishes the job with it.

Of entry 53's 63 genuine defects, **58 are entries the REFERENCE
DISASSEMBLY does not consider code either.** Three independent reasons to
remove each one: no terminator in the body, no fall-through to another
entry, and not code in an independently-produced disassembly. None is a
reference function start. `tools/ghidra/kill_funcs.py` removed them.

                  start    now
    functions       720    560
    >1 range         --     36   (normal)
    fall-through     --     44   (correct)
    REAL DEFECTS    165*    10
    ref starts      433    433

    * entry 51's figure, produced by the broken metric; the comparable
      honest number was never measured on the 720-function project.

**159 of the original 720 "functions" were never functions.** All of them
came from one defect — `seed_harvest.py` harvesting immediate routine
pointers from a linear listing without filtering the source site to real
code (entry 52), where 2156 of 2435 candidate sites are phantoms over
data. The fix is in the tool; the residue is now out of the project.

**550 of 560 functions (98%) are correctly bounded**, and every one of the
reference's 433 function starts survived every deletion. That guard is
what separates this from vandalism — it was checked after each of the four
removal passes, not once at the end.

The 10 remaining: 5 are entries the reference DOES call code, so they are
genuinely truncated and worth extending by hand. The other 5 are
1-to-16-byte stubs that resisted every automated rule and are not worth
more machinery.

**Disassembly coverage is now finished.** Instructions match the reference,
bounding is 98%, and what is left is naming: 560 functions, about 45 named.

---------------------------------------------------------------------
## 55. The annotated function map: all 560 classified

`tools/ghidra/classify.py` -> `docs/audit/function_map.md`. Every function
in the program, with size, callers, the object fields it touches, and a
class. **45 rows are marked READ — read to their return. The other 515
are SIGNATURE matches and are hypotheses**, per entry 50's rule, and the
map says which is which on every row.

The signatures are grounded even though applying them is mechanical: each
one comes from a routine that WAS read. $21/$22/$24 is the animation
triple from entry 32; $0C/$10 with $14/$1A is the 16.16 motion block from
29; $34-$3A, $54-$5A and $60-$66 are the three hitboxes from 34 and 45;
$3E/$3C are the claim locks from 38; a call to 0x3F04 hides a sprite (30),
to 0x3DD8 draws one (33), to 0x3352 makes a sound (32).

    leaf / helper   150      despawns          15
    hardware        117      state change      13
    animation        88      draws             12
    claim / lock     48      palette           12
    collision        33      sound              9
    motion           17      named by reading  45

**410 of 560 functions now carry a behavioural class**, and the shape of
the program is legible from the totals alone: animation is the single
biggest behavioural class at 88, and 48 functions touch a claim lock,
which is a lot of actor-to-actor interaction for a game this size and
explains why the TAS dependency (entries 38, 39) is load-bearing rather
than incidental.

The 150 leaf/helpers touch no object field and no hardware — pure
computation on registers and locals. They are the arithmetic the rest of
the program is built from, and they are the least valuable thing left to
name.

**This completes the coverage task.** Instructions match the reference,
98% of bounds are correct, every function is classified, and every claim
in the map declares whether it was read or inferred.

---------------------------------------------------------------------
## 56. CAT1 IS A BOTTOM-ANCHORED STRIP, AND THE MD WINDOW PLANE IS EXACTLY THAT SHAPE

The builder is at 22% single-vint frames, 1.44 vints per generation, with
slave compose at 1.40 of it and cat1 tiles 48% of that — 0.67 vints.
Removing cat1 from compose lands the generation at ~0.77 and under the
one-vint quantum.

Entry 31 established cat1 is static rom data. This is what its SHAPE is.

    scene 0   2312 cat1   pages 0-4, rows 24-31   ( 8 rows, 7 full)
    scene 1   8960 cat1   pages 0-4, rows  4-31   (28 rows, 28 full)
    scene 2   7360 cat1   pages 0-4, rows  4-31   (23 rows, 23 full)
    scene 3   1280 cat1   pages 0-4, rows 28-31   ( 4 rows, 4 full)
    scene 4   3520 cat1   pages 0-4, rows 21-31   (11 rows, 11 full)

**Every cat1 tile in the game is in a CONTIGUOUS BOTTOM STRIP, full
width, identical across all five background pages. The FOREGROUND page
has ZERO cat1 tiles in any scene.** It is not scattered per-tile
priority. It is one horizontal band anchored to the bottom of the screen,
whose height changes per scene and never within one.

**That is precisely the Mega Drive WINDOW PLANE.** The window is a
rectangle anchored to a screen edge, it draws above plane A, and its
vertical extent is set in whole tile rows (reg 0x12, 8-pixel units, with
a bit selecting up or down from the boundary). A bottom-anchored,
full-width, row-aligned band is the one shape it expresses exactly.

**Why this beats per-tile promotion, which is what CAT1MD tried:**

  - No priority emulation. The window is a hard rectangle, so there is no
    per-tile decision to get wrong and NOTHING TO SHIMMER. Entry 31 proved
    the classification is static; this removes the classification from the
    runtime altogether.
  - Sprite semantics come out right for free. An MD sprite with its
    priority bit set draws ABOVE the window, which is exactly System 16's
    rule that a high-enough-priority sprite beats a cat1 tile.
  - One register write per scene instead of 20480 per-tile decisions.
  - It scales the right way: scene 1 is the worst case at 43.8% cat1 and
    28 rows, and a 28-row window costs the same as a 4-row one.

**Caveats, stated rather than buried:**

  1. The window REPLACES plane A inside its region, so the strip's content
     must be the window's name table. The rows are almost entirely full,
     so there is little to lose — but scene 0's row 24 is 52 of 64 cat1,
     and a rectangle would wrongly promote the other 12. Starting the
     window one row lower trades those 12 tiles for a 7-row window.
  2. I have NOT measured what fraction of compose the strip actually is
     versus what the 48% figure covers — that is the builder's counter,
     not mine.
  3. Window and plane A share a horizontal scroll on real hardware in the
     sense that the window does not scroll at all. **The strip is the
     FLOOR and the floor DOES scroll horizontally.** This is the one
     thing that could sink it, and it needs checking before anything is
     built: if the cat1 strip scrolls with the background, a fixed window
     cannot carry it.

Point 3 is the test that decides this, and it is cheap: the strip is
rows 24-31 of the BACKGROUND plane, and entry 24 measured background
hscroll moving 156 -> 53 across level 1. If the strip's tiles move with
it, the window is wrong and the answer is per-tile after all.

---------------------------------------------------------------------
## 57. Two cat1 hypotheses killed, and the one that survives: HALF THE CAT1 TILES ARE INVISIBLE

Entry 56 proposed mapping the cat1 strip to the MD window plane. **Dead.**
The window cannot scroll, and the strip is not uniform — rows 24-31 of
scene 0 carry 15 to 23 DISTINCT tile indices each, in sequential runs
(12E8, 12E9, 12EA...). It is scrolling artwork, not a repeated texture.

Second try: BG cat1 and FG cat0 are BOTH priority level 2
(ARCHITECTURE.md:541), so they could share one MD plane. **Also dead.**
They collide constantly, and not just in the weak sense of both having a
tile assigned — checking the actual art in `sh_src/tiles.bin`, the
foreground draws opaque pixels over 51% to 97% of cat1 cells depending on
scene. Two layers that overlap cannot be one plane.

**But that second measurement inverts into the useful result.** If the
foreground tile over a cat1 cell is FULLY OPAQUE, the cat1 tile beneath
contributes nothing to the final image and never needs composing:

    scene 0   232 of  500 cat1 cells fully occluded   46%
    scene 1   366 of 1792                             20%
    scene 2  1227 of 1472                             83%
    scene 3   161 of  256                             63%
    scene 4   387 of  704                             55%
    overall  2373 of 4724                             50%

**Half of all cat1 tiles in the game are invisible**, and which half is
decidable at bake time from rom alone — the tilemap (entry 31) and the
tile art the port already decodes. It is a second bitmap beside
`cat1map.bin`, 2560 bytes per scene.

If cat1 compose is 0.67 of the 1.44-vint generation, dropping the occluded
half is about 0.33, landing near 1.11. That does not cross the one-vint
quantum alone, and I am not going to claim it does.

**THE ASSUMPTION THIS RESTS ON, STATED.** That the foreground layer draws
over the background layer when both are priority level 2. The level
governs sprite interleaving; layer-versus-layer order is a separate fixed
rule. It is near-certain and it is still an assumption, and the whole
result collapses without it. `jts16_colmix.v` / `jts16_prio.v` settle it
and I have not read them for this purpose.

Recorded so nobody retries the two dead ones.

---------------------------------------------------------------------
## 58. The occlusion rule is CONFIRMED BY RTL, and 1.11 is not enough on its own

Entry 57's result rested on an assumption: that the foreground layer beats
the background when both are priority level 2. `jts16_prio.v` settles it
and the assumption is discharged.

    lyr0 <= char (text)
    lyr1 <= tile_or_obj(obj, scr1_g, scr1_g[10], obj_prio>=2)   FOREGROUND
    lyr2 <= tile_or_obj(obj, scr2_g, scr2_g[10], obj_prio>=1)   BACKGROUND
    lyr3 <= scr2 with the low bits cleared

    {shadow,pal_addr} = lyr0 opaque ? lyr0 :
                        lyr1 opaque ? lyr1 :
                        lyr2 opaque ? lyr2 : lyr3

**The mixer tests lyr1 before lyr2, unconditionally.** And
`tile_or_obj` (line 58) returns either the sprite or THE TILE — never
nothing — so if the foreground tile pixel is opaque, lyr1 is opaque
whichever branch it takes, and lyr2 is never reached.

**So a background cat1 tile under a fully-opaque foreground tile is
unconditionally invisible.** Not near-certain. Read out of the mixer.
Entry 57's 50% stands: 2373 of 4724 cat1 cells across the five scenes.

**NOW THE COST, AND IT IS NOT THE GOOD NEWS IT LOOKS LIKE.**

    now                                 1.44 vints/gen   22% single-vint
    cat1 occlusion cull   -0.34    ->   1.10 vints/gen

**The quantum is 1.00, and 1.10 is still over it.** A generation costing
1.10 takes two vints exactly as one costing 1.44 does. What changes is the
DISTRIBUTION: at a 1.44 mean, 22% of generations fell under 1.0; at 1.10
a much larger share will, so the single-vint percentage — the metric that
matters — should move substantially. The CEILING does not move at all
until the mean crosses 1.00.

So this is worth building and it is not a solve, and anyone reporting it
as "1.44 down to 1.10" is quoting a number that does not by itself change
what the player sees.

**What crosses.** The sprite half is 0.73 and LOOP29 127 puts ~96% of
records in software for want of palette lines. Entry 9 measured that
three CRAM lines cover 70% of live records with NO colour change, and
MDSPRTOP already got +19% claims choosing the line by record count.

    + sprites, 70% of records to hardware   -0.51   ->   0.59 vints/gen

0.59 is comfortably under the quantum. **Neither lever crosses alone;
together they clear it with room.** That is the shape of the answer: the
cat1 work is necessary and not sufficient, and the sprite-palette work is
the other half.

---------------------------------------------------------------------
## 59. CORRECTS ENTRY 11 — and the MD can express level 1's layering EXACTLY

**Entry 11 is wrong.** It said the page selects never change: foreground
page 7, background page 0, from two write sites. Both of those sites are
in the service-mode region. MEASURED on a running frame instead
(`--dump wram:0xFF8E80:0x10`, frame 2400, level 1):

    0xE80  scr1 / FOREGROUND  = 0x0000  -> page 0
    0xE82  scr2 / BACKGROUND  = 0x5555  -> page 5

**The opposite way round from what I logged, and neither value is one I
found in the code.** The real writer is somewhere I have not located; the
two sites I did find are service mode. Everything entries 56-58 built on
"page 0 is the background" is therefore void, including the occlusion
result — page 5 has ZERO cat1 tiles, so there is no background cat1 in
this game at all.

**All cat1 is FOREGROUND cat1, at priority level 4.**

Now the useful part. Sprite priority is word 4 bits 7-6 (entry 21). Across
five sampled frames of level 1, 83 live records:

    pp=2 : 83 (100%)        pp=3 : none

Every sprite in the level is pp=2. The port's verified rule is
`sprite draws iff 1<<pp > level`, so 4 > 4 is false: **every sprite loses
to FG cat1, and none of them is a boundary case.** There is nothing for a
promotion to get wrong.

**And the Mega Drive's own chain matches System 16's, exactly.**
`srcref/S32X_MiSTer/rtl/GEN/vdp.sv:1772-1782` resolves in this order:

    sprite HIGH > plane A HIGH > plane B HIGH > sprite LOW > plane A LOW > plane B LOW

Map it:

    S16                     level   ->  MD                      rank
    FG cat1  (the strip)      4         plane A, priority SET     2
    sprites  (all pp=2)       -         sprite,  priority clear   4
    FG cat0                   2         plane A, priority clear   5
    BG       (page 5, cat0)   1         plane B, priority clear   6

FG cat1 beats the sprites (2 before 4) — correct. FG cat0 loses to them
(5 after 4) — correct. BG is behind everything — correct. **The whole
tile-versus-sprite layering of level 1 is expressible natively, using the
per-tile priority bit plane A already has, with no software compositing
and nothing to shimmer.** `sh_src/cat1map.bin` is exactly the bit to set.

CAT1MD's revert was not evidence that promotion is unsound. The mapping
is exact.

**WHAT IS STILL IN THE WAY, and it is not priority.** An MD plane tile
draws 4bpp from one of four CRAM lines; a System 16 tile is 3bpp from one
of 128 palettes. The binding constraint was always colour, and it still
is. This result removes the priority objection and does not touch that.

**Checked for level 1 only.** The pp=2 uniformity is measured on scene 0.
Another scene with pp=3 sprites would have a genuine boundary case.

---------------------------------------------------------------------
## 60. THE TILE PALETTE CONSTRAINT IS NOT BINDING — 4 of 5 scenes fit in THREE CRAM lines

Entry 59 ended by saying colour was still in the way. Mike pushed back:
we already knew the arcade does not use all its palettes at once. He was
right and I had carried the project's sprite-side conclusion (entry 9,
where sprites genuinely do not fit) across to tiles without measuring.

Measured. Worst case over ALL horizontal scroll positions, a 40x28
viewport, both displayed pages (FG page 0, BG page 5 — entry 59):

    scene   palettes   distinct colours   greedy lines needed
      0        25            33                  3      FITS 4
      1        11            31                  3      FITS 4
      2        14            33                  3      FITS 4
      3         8            34                  3      FITS 4
      4        16            77                  6      does not fit

**Twenty-five distinct tile palettes collapse to 33 distinct colours**,
because System 16 tile palettes share colours heavily. And the packing is
not merely a colour count — a tile picks ONE MD line, so every 7-colour
palette must sit entirely inside one 15-slot line. A naive greedy
first-fit finds a 3-line partition for four scenes out of five. Greedy is
an upper bound, so 3 is safe and the true optimum may be lower.

**So the whole tile layer can go to the MD VDP for 4 of 5 scenes, and
with a line to spare.** Combined with entry 59 — where the MD priority
chain expresses level 1's tile-versus-sprite layering EXACTLY — there is
no priority obstacle and no colour obstacle to putting both tile planes on
hardware.

That is not 0.67 vints of cat1 compose. It is the tile half of the slave's
compose entirely.

**Scene 4 is the exception and is honest about it**: 77 colours against 60
usable slots, over by 17, and greedy wants 6 lines. It needs merging of
near-identical colours or a different treatment. One scene out of five.

**THREE THINGS NOT MEASURED, and the first could sink it:**

  1. **The colour cycler (entry 41) writes palette ram directly every
     vint.** If it cycles TILE palettes, the colour set is not static and
     the partition has to hold across every cycler state, not just the
     rom values I read. This is the check that matters most.
  2. Vertical scroll. I assumed rows 4-31; entry 24 found vertical scroll
     pinned at 32 for all of level 1, but that is level 1 only.
  3. Scene 4, above.

I was wrong to state colour as the binding constraint without measuring
it for tiles. It binds for sprites and it does not bind for tiles.

---------------------------------------------------------------------
## 61. ALL FIVE SCENES FIT IN FOUR CRAM LINES, and scenes 0-3 leave lines SPARE

Entry 60 measured the tile palettes in System 16's 5-bit colour and found
scene 4 over by 17. That was the wrong space to measure in.

**MD CRAM is 3 bits per channel, not 5** (ARCHITECTURE.md:838). The port
already quantises to it, already measured the loss at max 2 in 0-31, and
already rendered both ways side by side and called them
indistinguishable. Colours that differ by less than one MD step are THE
SAME COLOUR on this hardware. Re-measuring in the space the pixels
actually land in:

    scene   palettes   MD colours   lines used      spare
      0        25          24       [14,13, 0, 0]     2
      1        11          24       [15,12, 0, 0]     2
      2        14          31       [14,14, 6, 0]     1
      3         8          28       [14,12, 4, 0]     1
      4        16          53       [13,15,14,15]     0

**Every scene fits. Scene 4 uses 57 of 60 slots; scenes 0 and 1 use 27
and leave TWO LINES FREE.** Scene 4 needed a best-fit with restarts
rather than first-fit — greedy wanted 5 lines for a set that packs into 4
— so the packing matters and it is a bake-time problem, solved once.

The constraint is not just a colour count: a tile selects ONE line for
all its pens, so every 7-colour palette must sit entirely inside one
15-slot line. That is what the packer enforces.

`tools/bake_tilecram.py` emits the artifact: per scene, four 16-entry MD
CRAM lines, plus for every System 16 tile palette the line it lives on
and the slot each of its seven pens maps to. **That pen map is what a
tile rebake needs** — it rewrites pixel values so a tile indexes its
assigned line directly.

**So the position on the tile layer is:** the priority mapping is exact
(entry 59), the colour fits with room (here), and the artifact to bake it
exists. Nothing in the tile path needs software compositing.

I was wrong twice getting here and both errors were the same shape:
measuring in the arcade's colour space instead of the one the pixels land
in, and using a first-fit where the problem needs a packer.

**The spare lines matter.** Scenes 0-3 leave one or two CRAM lines unused
by tiles. Entry 9 measured sprites needing far more than they can have;
a spare line is a spare line.

STILL UNCHECKED, and unchanged from entry 60: the colour cycler (entry
41) writes palette ram every vint, and if it cycles TILE palettes this
whole partition has to hold in every cycler state, not just at rom values.

---------------------------------------------------------------------
## 62. The cycler does not touch the viewport, and a block error of mine corrected

Checked entry 61's open risk. Dumping the live tile palette region across
six frames of level 1:

    palettes that CHANGE frame to frame:  6, 19, 20, 21    (four of 128)
    palettes scene 0's worst viewport uses:  72-103        (twenty-five)
    intersection:  NONE

**The colour cycler cycles four tile palettes and the viewport uses none
of them.** The risk is closed for level 1.

**AND CHECKING IT EXPOSED A REAL ERROR IN ENTRIES 60 AND 61.** My colour
figures came from `0x232A0 + block*0x400 + p*16`. That block is 1024
bytes — sixty-four palettes. **Every palette above 63 was read from the
wrong block**, and scene 0's viewport uses 72 through 103, so essentially
all of them. `set_level_palettes` (entry 3) loads only 0-63; palettes
64-127 are written to 0x840400 by the routine at 0x2B66 from a WORK RAM
buffer at 0xFFE400, which is not rom at all.

Re-measured against LIVE palette ram, union across six frames so every
cycler state seen is covered:

    scene 0:  25 palettes ->  43 distinct MD colours -> FITS 4 lines,
              sizes [15, 15, 11, 5], 46 of 60 slots

**43, not the 24 entry 61 reported. The conclusion holds and the number
was wrong.** It still fits, with the fourth line nearly empty.

**Scenes 1-4 in entries 60 and 61 are NOT TRUSTWORTHY** — same block
error, and I have live palette data only for level 1. Their packings have
to be redone from a dump of each scene, which needs a playthrough that
reaches them.

`tools/bake_tilecram.py` therefore has a defect: it reads palette colours
from rom, which is right for 0-63 and wrong above. It should take a live
CRAM dump instead. Not fixed yet; noted here so nobody bakes from it.

Sixth self-caught error, and the same shape as the rest: I took a base
address and a stride that were correct in one range and used them outside
it, without checking what the consumer actually reads.

---------------------------------------------------------------------
## 63. What the port has actually patched, classified — and what is left

224 declared patch sites in `tools/game_altbeast.py`, by why they exist:

    address rebasing (the map moved)             62
    transport: tell the SH-2 what changed       103
    timing: keep the 68K out of the FB window    40
    format/idiom the port re-implements          11
    hardware behaves differently                  8

**Not one of them changes what the game COMPUTES.** Every family is the
same access somewhere else, the same access announced to the SH-2, or the
same access at a safe moment. That is why "it looks and plays exactly like
the arcade" is even checkable — the logic has never been touched.

The largest family is TRANSPORT, at 103 sites, and it exists purely
because the SH-2 cannot see what the 68K wrote. Every dirty-bit thunk is
a message saying "this region changed". **That family shrinks as work
moves to the VDP**: a tile plane the VDP draws needs no dirty bits,
because nothing has to be told.

**What is left to tune, from the decompile, ranked by evidence:**

  1. **Tiles to the VDP** — `docs/handoff/PLAN-TILES-TO-VDP.md`. Priority
     mapping exact [59], colour fits [61][62]. The larger half of slave
     compose. Also deletes a chunk of the 103 transport sites.
  2. **Sprite palette lines** — entry 9 measured three CRAM lines
     covering 70% of live records with NO colour change, and scenes 0-3
     leave one or two lines spare after tiles [61]. MDSPRTOP already got
     +19% claims picking the line by record count.
  3. **The sprite upload** [13][15] — one writer, both inputs in work ram,
     so the interception, compare, shadow and packing can all go. It is a
     SIMPLIFICATION and NOT a speed lever: entry 27 deleted the entire
     copy and the frame rate did not move.
  4. **The frame-skip signal** [22] — the game increments 0xFFF144 and
     takes a short path that writes NO video state when it overruns. On
     those vints there is nothing new to compose. Nobody reads that
     signal today.

**What cannot be tuned, and should stop being attacked:** the game's own
compute. The 68K is not the constraint — their master-idle measurement
and my MDSPRPROBE ablation [27] agree from opposite directions, and
deleting the largest 68000 copy in the frame changed nothing.

---------------------------------------------------------------------
## 64. CORRECTS ENTRY 61 — tiles need ALL FOUR CRAM lines, which is the corruption

Entry 61 said scenes 0-3 leave one or two CRAM lines spare after tiles.
**That came from the wrong palette data** (entry 62's block error: 24
colours where the live figure is 43). Repacked against live palette ram:

    scene 0 tiles:  25 palettes, 43 distinct MD colours
      2 lines: no packing exists
      3 lines: no packing exists
      4 lines: FITS [15, 15, 11, 5]

**There are no spare lines. Tiles need all four.**

That is almost certainly what the vi27 screenshots show. In
`20260911_203242-vi27.png` the scene is correct — blue sky, green trees,
grey stone — EXCEPT in the region on the right where the smoke sprites
cluster, where red and yellow blocks appear. In
`20260911_203136-vi27.png` the whole scene is desaturated grey while the
player and the fallen enemy keep correct flesh and orange.

**Sprites and tiles are contending for the same four CRAM lines.** Where
sprite demand is dense the tiles lose their line and show whatever the
sprite palette put there; in the grey frame the tile lines lost outright.
Both pictures are consistent with contention and neither is consistent
with a priority error.

**So the answer to "can you see the sprite-over-cat1 artefact": no.** It
may well be there, but it is invisible next to this. Fixing the sprite
loop for cat1 ordering before resolving the line budget would be spending
sprite-loop budget on the smaller of two problems.

**And the budget problem is real, not a bug.** 43 colours of tiles plus
any sprite palette does not fit in 64 slots. Entry 9 measured the live
sprite demand at 123 distinct colours across 12 palettes. Tiles and
sprites TOGETHER cannot share MD CRAM as things stand. Options, in
increasing order of cost:

  1. Give tiles 3 lines and sprites 1 by dropping the least-used tile
     palettes to software. No 3-line packing exists for all 25 palettes,
     but the viewport rarely shows all 25 at once — the packing was
     computed over the WORST scroll position.
  2. Per-scene tile palettes are static (entry 61's artifact), so the
     split can be chosen per scene rather than globally.
  3. Accept tiles-on-VDP with sprites entirely in the 32X layer, which
     is what the builder's own per-pixel suppression idea implies — the
     32X layer carries sprites and punches holes for cat1.

Option 3 is the one their bit-15 finding already points at, and it needs
NO sprite palette in MD CRAM at all.

---------------------------------------------------------------------
## 65. The cat-1 hole punch is mostly a ROW TEST, and a third of cells need no hole at all

The builder's fix for sprites wrongly covering cat-1: suppress the sprite
pixel where a cat-1 cell covers it and let the MD's cat-1 show through
the hole, using the 32X layer's per-pixel transparency (bit 15 of the
palette entry). Correct, and cheaper than budgeted.

**First: the strip has almost no ragged edge.**

    scene 0   7 rows FULL cat1 (25-31), ONE partial row (24), 24 clear
    scene 1  28 rows FULL (4-31),  zero partial
    scene 2  23 rows FULL (4-31),  zero partial
    scene 3   4 rows FULL (28-31), zero partial
    scene 4  11 rows FULL (21-31), zero partial

**Four scenes of five have NO partial row.** For those the test is a
single compare — `screen row >= N` — with no bitmap lookup anywhere.
Scene 0 needs the bitmap for exactly one row.

**Second: a third to two thirds of cat-1 cells need no hole at all.**
Checking the actual art behind each cat-1 cell:

    scene   cat1 cells   fully opaque   partial   BLANK
      0        500          332 (66%)      158      10
      1       1792          263 (15%)      325    1204
      2       1472          351 (24%)      264     857
      3        256          163 (64%)       87       6
      4        704          208 (30%)      494       2

A BLANK cat-1 cell has the priority bit set on a tile with no pixels —
there is nothing to show through, so no hole is needed. A FULLY OPAQUE
cell can be suppressed whole. Only PARTIAL cells need per-pixel work.

    scene 0:  342 of 500 cells (68%) resolve per-CELL
    scene 1: 1467 of 1792     (82%)
    scene 3:  169 of 256      (66%)

So the map the sprite loop wants is not one bit per tile, it is **two
bits per cell** — skip / suppress-all / consult-pixels — and it is baked
from rom exactly like `cat1map.bin`. The per-pixel path survives for a
fifth to a third of cells.

Worth saying plainly to the builder: their idea is right, and the version
they costed is the expensive one.

---------------------------------------------------------------------
## 66. SCENESEL probe, and the per-scene tile CRAM measured at last

No input script in `discover/inputs` reaches past scene 0 — checked
play_wolf4, play_wolf10, play_native1 and play_native3 at 6000 frames,
all still scene 0. So the later scenes' palettes were unmeasurable and
entries 60-61's figures for them stayed wrong.

**The probe.** 0x662 does `lea $1CDA(pc),a0`, then 0x670 does
`move.b (a0,d0.w),$FFF142` with d0 the round at 0xFFF14E masked to 7.
The table is eight bytes, `0 1 2 3 4 0 0 0`. `make ship-us SCENESEL=N`
rewrites it to all-N so every round loads scene N. One byte per entry,
in place, asserted against the expected table first.

**Measured, live palette ram, worst 40x28 viewport over all 64 scroll
positions, both planes, union across three frames:**

    scene   palettes   MD colours   minimum lines   fill
      0        25          43            4          [15,15,11,5]
      1        11          28            3          [14,14,5]
      2        14          27            2          [13,15]
      4        15          33            3          [14,14,11]
      3         -           -            -          NOT REACHED

**This reverses entry 64 for three scenes of four.** I said there are no
spare CRAM lines. True for SCENE 0 and only scene 0: scene 2 needs TWO
lines and leaves two, scenes 1 and 4 need three and leave one. Scene 0 is
the worst case in the game, and it is the one the builder is working on,
which is why the contention showed up there.

Scene 4 is also much kinder than entry 60 claimed — 33 colours in 3
lines, against the 77-in-6 I got from the bad rom read. That figure is
now doubly retired.

**Scene 3 did not take.** Its rom carries the all-3 table (verified by
byte search in the image) and 0xFFF142 reads 0 at frames 900, 1500, 2200
and 3000. So the patch is present and the scene still does not load. I do
not know why and am not guessing; scene 3 stays unmeasured.

**Two process notes.** One of the four builds hit a transient link error
and the copy step left a STALE rom behind — `scene3.32x` was scene 2's
image. Running each rom and reading 0xFFF142 caught it; the build log did
not. And the run that reported "scene 0" in the batch was that stale rom
re-measuring scene 0, which reproduced 25 palettes / 43 colours /
[15,15,11,5] exactly — an accidental but welcome repeat of entry 62.

---------------------------------------------------------------------
## 67. ANSWER TO THE BUILDER — the wait instruction, and why their zero is not evidence

Their ask: the instruction the main loop waits at, what it tests, and
which of three readings explains a game released every vint that still
advances once every two.

**1. The wait, exactly.**

    397e:  clr.b  $FFF01C        the loop DISCARDS any pending release
    3982:  tst.b  $FFF01C        <- THE WAIT. spins while ZERO
    3986:  beq.s  0x3982
    3988:  dbf    d0,0x397E      d0+1 frames; every gameplay caller
    398c:  rts                   passes moveq #0 = ONE frame

Only four instructions in the whole program touch 0xFFF01C: the two
above, plus IRQ4's `tst.b` at 0x2AB8 and `addq.b #1` at 0x2AC6.

**2. Their reading 1 is right about the shape but does not bite.** The
release is an INCREMENT (`addq.b #1`), read by a `tst.b` — a counter
tested as a level, so setting it to 1 is equivalent. **But the loop
CLEARS IT BEFORE SPINNING.** A release that arrives while the game is
still working is thrown away at 0x397E, and the game then waits for the
NEXT one. The game can never bank a release or catch up; it always waits
for a fresh edge after it finishes.

**3. Their reading 3 is the one I can settle, and the answer is YES.**
Three sites touch the missed-frame counter:

    0x2ABE  addq.w #1   IRQ4, on overrun
    0x00BE2 move.w      read it (the test-mode hex display, entry 22)
    0x00930 clr.w       CLEARS IT

**0x930 is inside the main loop.** At 0x918 the loop does
`subq.w #1,$FFF14C ; beq.s 0x92A`, and 0x92A-0x930 is the exit arm that
clears the counter. So the counter is zeroed every time that countdown
expires. **A zero reading is not evidence that no frames were missed.**
It is consistent with misses being counted and wiped.

The cheap test: read 0xFFF144 EVERY VINT and look for it being non-zero
before the clear, or watch 0x930 execute. Do not sample it at a chosen
frame.

**4. What I can rule out.** The wait is not a hidden multi-frame request.
Of the 34 call sites, all but two pass `moveq #0,d0` — one frame. The two
exceptions pass 120 and 240 and are attract-mode delays, not gameplay.
And the loop shape is one dispatcher pass per wait:

    91a:  jsr 0x398E    the object dispatcher (entry 28)
    920:  moveq #0,d0
    922:  jsr 0x397E    wait exactly one frame
    928:  bra.s 0x90A

**5. So of their three readings, 3 is live and 1 is real but not
binding.** I cannot rule out reading 2 from here — a second gate
elsewhere — but the frame handshake itself has only these four
instructions and no second condition in it.

**One more thing worth their time.** If the counter IS being cleared, then
the premise the port has been built on — that the game fits its budget —
has never actually been tested during gameplay, because the instrument
was being reset. That is worth knowing before anyone concludes the 68000
is or is not the loss.

---------------------------------------------------------------------
## 68. THE TRUE MISS RATE IS 50%, and every 0.0% ever read off that counter was the clear

The builder built MISSKEEP off entry 67 within the hour — same name, same
site, `patch_game.py:995`, citing LOOP-DECOMPILE 67. Measured on it:

    frame 1200   0xFFF144 =  228
    frame 2400   0xFFF144 =  825    +597 over 1200 frames = 50% of vints
    frame 3600   0xFFF144 = 1442    +617 over 1200 frames = 51% of vints

**Half of all vints are missed.** The 0.0% the port has been reading was
the clear at 0x930, exactly as entry 67 predicted.

And 50% is not a coincidental number: a miss means the main loop had not
reached its wait by the next vint, so the loop takes two vints per pass,
which is precisely the "advances once every two vints" the builder
measured from the other side. Two independent instruments now agree.

**WHAT THIS PROVES, AND WHAT IT DOES NOT.**

  PROVES: the 68K misses its frame deadline on half of all vints in the
  shipping build, and every previous reading of that counter was
  worthless.

  DOES NOT PROVE the game is too slow. **The shim runs on the same
  68000.** The counter measures game plus shim against one vint, not the
  game alone. CLAUDE.md's premise is that the game's ~2780 instructions
  fit; the shim's ~2882 on top of them are what this counter is seeing.

The clean separation already exists in their own data: the ablation build
reached 98% single-vint with the compute removed, on the same game code.
So the game fits and the pair does not.

**Consequence for the project's direction.** The premise "the 68000 clock
is not the loss" survives, but the weaker claim it is often used to
support — that the 68K side needs no attention — does not. Half the vints
are being missed on the 68K, and that is where the two-vint cadence comes
from.

**A process failure of my own.** I wrote MISSKEEP as a probe without
checking whether it existed, and my duplicate ran before theirs and
tripped their assert, breaking the build twice before I found it. They had
implemented my finding while I was re-implementing it. Second time today
I have failed to look at what the other thread already did.

---------------------------------------------------------------------
## 69. WHAT PATCHING THE 68K ROM CAN AND CANNOT BUY — re-measured honestly

Entry 27 concluded that deleting the game's sprite copy does not move the
frame rate. That was measured on the counter entry 68 has now shown was
being wiped, so it had to be redone. Rebuilt with both flags:

    1200-frame window, 2400-3600, missed vints
      shipping                        617   (51%)
      sprite record copy DELETED      591   (49%)

**The conclusion survives: 2 points.** Deleting the single largest thing
the game's IRQ4 does — up to 128 records x 12 bytes, ~1536 bytes of
68000 copying every vint — buys two points of miss rate out of fifty.

That is the answer to "what can we patch in the 68K rom to bridge the
gap", and the answer is **very little, because the rom's own per-vint
work is not where the time goes.**

Add up what the game does per vint from the decompile:

    sprite upload      0x2B16   up to 1536 bytes   <- measured at 2 points
    palette drain      0x2DBC   28 bytes per queued entry
    colour cycler      0x30B2   12 bytes when a slot expires
    sky palette        0x3108   32 bytes
    scroll registers   0x2AD2   4 words
    input edges        0x2E74   a handful of bytes

The sprite copy dwarfs the rest combined, and the sprite copy is worth two
points. **The whole of the game's per-vint video work is therefore a few
points of a fifty-point overrun.** There is no 68K-rom-side patch that
bridges this, because the rom is not what is overrunning.

The overrun is the shim: CLAUDE.md's own figure is ~2882 instructions per
vint against the game's ~2780, and the shim is C in `md_src/`, not
patchable bytes in the arcade binary.

**So the levers are where entry 63 put them, and none of them is a rom
patch:**

  1. Tiles to the VDP. Deletes 25 TILE_DIRTY_SITES and the tile half of
     the transport family outright — the shim stops being told about
     writes nobody needs to hear about.
  2. The frame-skip signal. On 50% of vints the game takes the 0x2C06
     short path and writes NO video state, so the shim is composing and
     shipping an unchanged frame. That is a shim-side skip, gated on a
     byte the game already maintains.
  3. Sprites out of MD CRAM entirely, which frees the contention entry 64
     found.

**One correction to entry 63 while I am here.** It said "what cannot be
tuned: the game's own compute". That was right but for a reason I stated
badly — I leaned on entry 27, which was measured on a broken instrument.
It is right because the game's per-vint work is small, which is now
measured properly rather than inferred from a wiped counter.

---------------------------------------------------------------------
## 70. THE ONE SHAPE-CHANGING PATCH: the game knows exactly when its frame is done

Mike's question: what could a rom patch change about the PIPELINE's shape,
rather than just where an access lands — or is shape work solidly a C
refactor?

Mostly it is a refactor. Relocating an access is what a patch does well;
restructuring the transport is `md_src/` work. **There is one exception,
and it is the thing the builder is currently losing time to.**

They measured "0.42 of the echo phase is the slave not working at all —
waiting for its launch or stuck in the handoff." The pipeline has to
INFER when the game's frame is complete, from windows and phases.

**It does not have to infer. The game says so, precisely, four times per
vint:**

    0x2AFC   move.w d0,$410E92     the last of the four scroll registers
    0x2B46   move.w #-1,4(a2)      the sprite list's second end marker
    0x2B4C   bsr 0x2DBC            entering the palette queue drain
    0x397E   clr.b $FFF01C         THE GAME'S FRAME IS COMPLETE — this is
                                   the instant the main loop gives up and
                                   waits for the next vint

The last one is the signal worth having. Everything the game intends to
put on screen this frame has been written by the time it reaches 0x397E.
Nothing after it changes until the next release.

**And it is a six-byte in-place patch, the shape the port already uses.**
The gameplay loop calls it as a clean `jsr`:

    91a:  jsr 0x398E      run every object
    920:  moveq #0,d0
    922:  jsr 0x397E      <- 6 bytes, rewritable to `jsr thunk`
    928:  bra.s 0x90A

The thunk writes a COMM register (or a byte the shim polls) and then jumps
to 0x397E. 34 call sites exist; the gameplay loop is one of them, so this
can be scoped to the loop that matters rather than all of them.

**What it buys, stated as a hypothesis and not a measurement.** It
replaces "guess when the frame is ready" with "be told". If the 0.42 of
the echo phase is the slave waiting on a launch it could have had earlier,
this is the signal that shortens it. I have not measured the echo phase
and cannot; that is their instrument.

**What it does NOT buy.** Nothing about compose cost. The slave still does
the same work, just possibly sooner. This is a latency patch, not a
throughput one — which is the right shape for a bimodal 42/57 problem
where the slow half is waiting rather than computing.

**The honest framing of Mike's question:** shape changes are refactors,
with this one exception, because the pipeline's missing information
already exists in the game and just needs exporting. Everything else —
merging the tilemap's two passes into one word-coherent sweep, replacing
the sprite upload, reading the order list directly — relocates or deletes
work the port already understands, and none of them reshapes the
transport.

---------------------------------------------------------------------
## 71. I BUILT THE SAME PROBE THE BUILDER HAD ALREADY BUILT. Three times tonight.

Entry 70 proposed signalling the slave at the game's frame-completion
point instead of inferring it. I then spent an hour building a probe to
price it. **The builder had already implemented the whole thing** —
`Makefile:1849`, LOOP29 186, citing LOOP-DECOMPILE 70: a thunk at the
wait that raises COMM10 bit 15, the SH-2 side to consume it, and a
FRAMEDONEWAIT timeout for scenes whose wait is not the gameplay loop's.
They are far past a probe.

My duplicate used the same flag name and a different scratch address, so
it silently fought theirs. Removed: the patch_game block, the md_main
thunk, the Makefile flag. `make ship-us FRAMEDONE=1` builds clean on
theirs.

**Third time tonight.** MISSKEEP (entry 68), now this, and earlier I
re-derived the overdraw-as-message-queue idea that is already the port's
architecture. Every time the sequence was the same: I produce a finding,
the builder acts on it within the hour, and I then build the thing they
built. A `git log` would have shown me each time.

**What the wasted hour did produce, and it is worth keeping:**

  1. **My first hook measured nothing and looked convincing.** Hooking
     the wait's CALLER at 0x922 gave V=131, H=62, identical across twelve
     frames. That was uninitialised WRAM. The hit counter I added on
     suspicion read ZERO — the thunk never fired, because 0x922 is the
     ATTRACT loop and does not run during play. **A suspiciously stable
     number is the tell**, and the counter is what proved it.
  2. **The gameplay loop is therefore NOT the one at 0x90A-0x928.** That
     loop is attract. Entry 67 cited 0x922 as "the gameplay loop"; it is
     not, and that correction matters to anyone reading entry 67.
  3. **0x397E can be hooked in place.** Its first instruction is
     `clr.b $FFF01C`, four bytes, and `jsr abs.w` is also four — so the
     entry takes a thunk that runs the displaced clear and returns,
     catching EVERY caller rather than one. That is the right hook and
     it is what the builder's timeout note is working around.
  4. **The address rebase runs BEFORE the patch blocks.** My assert
     caught `jsr $90397E` where I expected `jsr $397E`. Anything patched
     late must use rebased targets.

**The rule I keep breaking**: check what the other thread did with my last
finding BEFORE acting on it myself. Not after the build fails.

---------------------------------------------------------------------
## 72. SCENE 3 LOADS FINE. Entry 66's "it will not take" was a stale rom.

Entry 66 left scene 3 unmeasured: "its rom carries the all-3 table
(verified by byte search in the image) and 0xFFF142 reads 0 at frames
900, 1500, 2200 and 3000." That is an ABSENCE established by four
samples, which is exactly the class entry 50 says produces a wrong
claim. It was wrong.

**The arcade first, because the port is a variable the question does not
need.** `tools/scenesel_arc.lua` runs `mame altbeast` — the oracle —
writes N to the eight bytes at 0x1CDA in the 68K rom REGION at frame 1,
and samples 0xFFF142 EVERY frame:

    SS_N=3 mame altbeast ... -autoboot_script tools/scenesel_arc.lua

    N=2   f142=2 from frame 448
    N=3   f142=3 from frame 448

Identical shape. Scene 3 is not special to the game.

**Then our rom.** `make ship-us SCENESEL=3`, copied out and the artefact
verified by inspection (rom[0x301CDA..0x301CE1] == 03 x8; the 68K image
sits at cart 0x300000, bank 3 -> 0x900000), then sampled every frame:

    f1     f142=0
    f463   f142=3      <- and 3 at every frame after, bar one reload blip
    f1926  f142=0      (one frame)
    f1927  f142=3

Entry 66 sampled 900, 1500, 2200 and 3000. All four read 3 here. So the
read was not the problem and the rom was: entry 66 records that a
transient link error left `scene3.32x` holding scene 2's image, caught
that one, and did not consider that the scene-3 build had the same fault.
**A build that fails leaves the previous rom in place** — the trap is
already written down in HANDOFF-DECOMPILE-2 and it still cost this.

**No build is needed for this at all.** The table lives in the cart
region, which MAME lua can write at frame 1, so
`CD_N=<scene> tools/cram_dump_scene.lua` selects a scene in ANY rom
without touching the tree. Proven by using it to re-derive scene 2 from
a scene-3 rom: 14 palettes, lines [13,15], 28 slots — entry 66's scene 2
figure exactly.

**Scene 3, measured.** `tools/cram_dump_scene.lua` dumps WRAM 0xFF9000
(0x1000 bytes) at three frames four apart, the input format
`bake_tilecram.py --live` takes. Dumps are in the tree as
`discover/cram/scene3_a.bin` b c (frames 1500 / 1504 / 1508).

    scene 3    8 palettes    lines [15, 7]    22 slots    2 lines

Stable: an independent window at 2400 / 2404 / 2408 gives the same three
numbers, and so does the union of all six frames.

**Scene 3 is the kindest scene in the game.** Two lines, two left over.
The completed table, each row from its own live dumps:

    scene 0   25 palettes   lines [15,15,11,5]   4 lines
    scene 1   11 palettes   lines [14,14,5]      3 lines
    scene 2   14 palettes   lines [13,15]        2 lines
    scene 3    8 palettes   lines [15,7]         2 lines
    scene 4   15 palettes   lines [14,14,11]     3 lines

Scene 0 is the only scene that needs all four, which strengthens entry
66's reversal of 64 rather than changing it.

---------------------------------------------------------------------
## 73. The ten bounding defects are eleven, and they are 6 code + 5 data

Entry 54 left "10 remaining: 5 genuinely truncated, 5 stubs that resisted
every automated rule and are not worth more machinery." The second half
was a shrug, and a shrug is a hypothesis. Both halves are now settled with
a named consuming instruction each, and the second half was wrong: those
five are not stubs and not code.

**The audit runs without Ghidra now.** `tools/bound_ref.py` asks
`bound_audit.py`'s question — does the body hold a terminator, and if not
does it fall through into another function — of the same 560 entries in
`function_map.md`, using the reference disassembly as the instruction
stream. objdump per function from its own entry, so the boundaries are the
function's and not a linear listing's. This matters beyond convenience:
the analysed project is the one thing the rebuild rule says to open as
little as possible, and the bounding question no longer needs it.

**The tool's own bug, and it is worth knowing before writing another.**
objdump WRAPS an instruction longer than six bytes onto a second line that
carries an address and bytes but NO mnemonic. Skipping those lines makes
every `movel #next,%fp@(2)` measure two bytes short — and that instruction
is the object state machine's exit idiom, the last thing many object
routines do. Three functions (0x61D6, 0x6396, 0x16BF0) read as defects
purely from that: each installs the function that physically follows it,
which is a fall-through and correctly bounded. First run said 14 defects,
fixed run says 11.

**Six are code and genuinely truncated.** Each walks from its entry to the
first terminator past its declared end and crosses no other entry:

    0x0040E  240 -> 364    reset, bra.w at 0x400
    0x05FA8   50 -> 132    bsr.s at 0x5F96
    0x060E6  124 -> 130    bsr.w at 0x5F08
    0x063CC   22 -> 130    bsr.w at 0x6356
    0x06C44   14 -> 116    ten call sites
    0x18146   60 -> 158    movel #0x18146,fp@(2) at 0x18034

**Five are not functions.** 0x6E7A and 0xDE56 are entries inside runs of
`0000 xxxx` longwords, and 0x6E7A's body is itself 22 longs that all land
inside the rom. The other three are installed by
`movel #addr,fp@(36)` — **object $24, the anim script pointer** in the
struct map. A field that has never held code is a consuming instruction,
not a plausibility filter, so these are animation scripts:

    0x08532   24    movel #0x8532,a0@(36)  at 0x82A2
    0x18F38   24    movel #0x18F38,fp@(36) at 0x18990
    0x1A0B8  114    movel #0x1A0B8,fp@(36) at 0x19F2E

The list is `docs/audit/bound_repairs.md`; the deletions are already in
`kill_funcs.py`'s input format at `docs/audit/bound_kill.txt`.
**Nothing is applied.** Extending six bodies and deleting five entries
means opening the analysed project, and the next person to open it should
do the whole pass at once rather than have me open it for eleven rows.

---------------------------------------------------------------------
## 74. RETRACTION — the framebuffer banks do not diverge. It was our packet.

Entry 12 carried a spare finding: "the two framebuffer banks diverge in
the staged tile region ... 266 of 40960 bytes differ. The game writes
tiles into whichever bank is current and nothing carries them to the
other." The rendering thread flagged that it was measured on page 0 of a
build whose FBX packet lived in page 0, and asked for the redo against
page 12 (LOOP29 138 moved it). This is the redo, and the finding is void.

`ares-headless --frames N --dump dram:0:0x80000`, attract, no input, at
N = 700 and 1000, on `rom/night/vi37.32x` and `rom/night/vi2.32x`.
Tilemap page N is DRAM 0x12000 + N*0x1000; the second bank is +0x20000.

**bank0 vs bank1 at frame 1000, all thirteen pages:**

    vi37   311 differing bytes — all of them page 12
    vi2    385 differing bytes — all of them page 12
    both     0 differing bytes in pages 0-11

Page 12 is where the FBX packet now lives, and the packet is per-bank by
construction: the 68K writes it into the framebuffer that is current. So
the divergence is the transport, not the game, and there is nothing to
carry between banks. The 266 was the same thing seen through page 0.

**Second result from the same dumps, free.** Between frames 700 and 1000
the ONLY page that changed, in either bank on either rom, was page 12:

    vi37  bank0 [(12, 313)]   bank1 [(12, 370)]
    vi2   bank0 [(12, 402)]   bank1 [(12, 349)]

The map is frozen for 300 frames. Entry 12 also read page 0's "197 bytes
between 700 and 1000" as the scrolling plane rewriting its incoming
column — that was the packet too, and the rendering thread had already
retracted it from their side. Both halves of entry 12's spare finding are
now gone, and the game demonstrably scrolls a static map.

**The method note worth keeping:** a per-page breakdown would have caught
this the first time. Summing a diff over a region hides which part of the
region moved, and the part that moved was ours.

---------------------------------------------------------------------
## 75. All five per-scene packs, and the attract step that would have poisoned them

The rendering thread's refuse rule works — zero tiles destroyed on level 1
against ~1300 without it — and cannot ship because the baked table covers
one scene of five. Their ask: the probe plus one dump run per scene. Here
is the run, and a trap that was one sample away from putting three wrong
tables in the emitter.

**The trap: 0xFFF142 says which scene is LOADED, not what is on screen.**
Widening the sample from entry 66's three frames to eighty made scenes 3
and 4 overflow — 51 and 59 slots against 22 and 39, three and five
palettes pushed to the framebuffer. It was not colour cycling. Between
attract screens the game reloads 69 of the 128 palettes into the same
work RAM while 0xFFF142 keeps reading the forced scene, so a dump taken
on the wrong screen is another screen's palette wearing the scene's name.

**0xFFF031 is the attract step, and it says what is on screen.** Snapped
on the arcade with the same table patch, one shot per distinct value:

    0x04  scene backdrop, player standing
    0x08  boot
    0x0C  scene backdrop with the logo
    0x10  the EYE title          <- poison
    0x14  scene backdrop again
    0x00  the high score table   <- poison

Only those two poison. Gating on `f031 not in {0x10, 0x00}` and on the
game's own display bit (0xFFF018 bit 5) gives, from 136-168 dumps per
scene spanning frames 400-4200 at every 20th frame:

    scene 0   25 palettes   lines [15,15,11,5]   46 slots   4 lines
    scene 1   11 palettes   lines [14,14, 5, 0]  33 slots   3 lines
    scene 2   14 palettes   lines [13,15, 0, 0]  28 slots   2 lines
    scene 3    8 palettes   lines [15, 7, 0, 0]  22 slots   2 lines
    scene 4   15 palettes   lines [14,14,11, 0]  39 slots   3 lines

**Every row is entry 66's figure to the slot.** A hundred-plus samples
across 3800 frames find nothing three frames missed, which is the result
worth having: the per-scene tile palette is static, so a static table can
hold it, and the packs above are the whole game. Nothing overflows and
only scene 0 needs four lines.

Gated dumps and their manifests are in `discover/cram/wide/`; the
three-frame sets in `discover/cram/` reproduce every row exactly and stay
as they are.

**On the transformation, what the program supports.** Exactly two
instructions in the whole program write 0xFFF142 — `clr.w` at 0x5DE and
the table read at 0x670 — and the table holds 0-4. There is no sixth
scene id, so a transform slot is a runtime concept and not something the
game will ever ask for by scene number. The transform recolours the
PLAYER, whose palette is the object's own slot/index at $0A/$0B, not a
tile palette, so it is outside what `bake_tilecram.py` measures at all.
Whatever covers it has to come from the sprite side.

---------------------------------------------------------------------
## 76. The dependency census, second half: the hardware SURFACE

Entry 37 built the census and ran it on two instruction classes. It is
the piece that makes a kit rather than a port, and it was the least
advanced of the six open items. This finishes the classes that are
findable from the instruction stream, and adds the half that is not about
mnemonics at all.

**First, it runs without Ghidra now.** `tools/code_stream.py` decodes the
19137 addresses in `repair_seeds` — the REFERENCE disassembly's own
instruction list, already in `altbeast_seeds.json` — one run at a time,
resyncing whenever objdump and the reference disagree. 19137 of 19137,
165 runs. That is wider than the function map, whose 555 bodies hold
15159 of them, and the difference is the whole point: 0x150B6 is a real
TAS in code the seeded project never reached.

**It reproduces entry 37 exactly**: the same 4 TAS in code with 0x150B6
as a candidate, and the same 12 STOP sites.

**The raw-opcode sweep is back, and deliberately narrow.** --stream can
only see where the reference looked, so a sweep over everything else is
needed for the class that bit us once already. It covers TAS, STOP, RESET
and TRAPV — exact words or a tight range with a mode filter. It does NOT
cover CHK or MOVEP: MOVEP's pattern matches the `0000 xxxx` longword that
every rom pointer table is made of, which is entry 37's 673 false hits,
and a list nobody reads is worse than no list.

The sweep found two TAS candidates. **0x150B6 is the real one** —
patch_game's fifth site. **0x03646 is data, checked by hand**: it sits
inside a smooth numeric ramp (62ec 64ea 67e9 6ae8 6de7 70e5 73e4 75e2,
high byte ascending, low byte descending), which is a curve table and not
a routine.

**Now the half that is not mnemonics.** A port has to answer, for every
arcade address the program touches: which region, which direction, what
width. That is what needs somewhere to land in the 32X map, and it is the
generalisable part, because the S16B decode is a property of the board.

    29 addresses accessed directly, 73 more only as an lea base

    tilemap VRAM 0x400000   32 kB, jts16b_main.v:354 (A16=0)
                            NOTHING addresses it absolutely — which is why
                            the port intercepts at the RLE loop heads
    TEXT layer   0x410000   4 kB, jts16b_main.v:329 (A16=1)
                            the SAME chip select, not a mirror of the
                            tilemap; my earlier reading of it as a second
                            window was wrong
    sprite RAM   0x440000   2 kB, one absolute site
    palette RAM  0x840000   4 kB, 12 addresses, all word writes
    I/O          0xC40000   9 addresses, all BYTE and all ODD —
                            jts16b_main.v:489 returns {8'hff, cab_dout},
                            so the board wires the low byte only

**Two new classes came out of it.**

**INTERRUPT MASK — 14 writes to the SR, and only two values.** #0x2700
masks everything, #0x2300 admits level 4 and up, which is the vblank the
port drives. Worth having as a list because the port's IRQ sources are
not the arcade's. The first cut of this class had 88 sites and was
useless: writes to the CCR set X/C for the next instruction and have
nothing to do with interrupts. Filtering to the SR alone is what made it
readable.

**READ-MODIFY-WRITE ON A WRITE-ONLY LATCH — one site, and a hardware
contract nobody had written down.** `bclr #6,0xC40001` at 0x1AFF2. On the
board the read returns 0xFF: jts16b_cabinet.v's `A[13:12]==0` arm only
latches flip and video_en from cpu_dout and never assigns cab_dout
(:199-202), so the read takes the 8'hff default at :189. The instruction
therefore writes 0xBF — flip off, video ON — and looks harmless.
**Our port maps 0xC40001 to the mailbox byte 0xFFB001, which returns
whatever was last written there, not 0xFF.** Latent, not live: 0x1AFF2 is
in the service region, and so are all 12 STOP sites. Which gives one
clean rule instead of two loose ones: **the port cannot enter test mode,
and this is the exact list of why.**

**STORES INTO ROM SPACE — zero.** The program never writes below
0x40000, so the +0x900000 rebase cannot be undone at runtime and nothing
is self-modifying. A clean class is still a kit rule; the check is one
pass and it has to be done per title.

Output is `docs/audit/hazard_census.txt`; the kit rules are in TOOLKIT.md
under the MD-hardware landmines.

---------------------------------------------------------------------
## 77. The rom map: 68.5% of the data named, and an instrument for the rest

Open item 3 was "~29% of rom data unattributed", a figure nobody could
re-derive because there was no tool. `tools/rom_map.py` is that tool. It
marks code from the instruction stream, marks every data region the log
has established with its entry number, and for each remaining run prints
**who points into it** — which is the method that cracked the two big
blocks in entry 48. A block nothing points at is a different kind of
answer from a block with a named reader, and both are useful.

    rom          262144 bytes
      code        75442  28.8%   19137 instructions
      data       186702  71.2%
        named    127865  68.5% of data   44 regions
        unnamed   58837  31.5% of data  172 runs

**The five scene tilemaps are 83628 bytes, a third of the whole rom, and
the extents are COMPUTED.** The scene descriptor at 0x1CE2 carries a
tilemap pointer per scene; running the game's own two-pass RLE to
completion from each one gives the end, and **each end lands within ten
bytes of the next scene's pointer**. That last part is the check. An
extent that merely looks right is a guess; five extents that chain into
each other are not.

    scene 0  0x29E00-0x2EF06   20742
    scene 1  0x2EF10-0x324CB   13755
    scene 2  0x324D0-0x369C1   17649
    scene 3  0x369C0-0x3B0DD   18205
    scene 4  0x3B0E0-0x3E4AC   13260

**The animation scripts name themselves.** 155 longwords are written into
object field $24, which the struct map already calls the anim script
pointer, and entry 73 found three blocks that had been mistaken for
functions because nothing else reads them. Every run containing a $24
target is an animation script by its CONSUMING FIELD, not by shape. Twelve
clusters. The tool derives them rather than listing them, so the same rule
runs on the next title.

**0x242A0 is the ACTOR PALETTE TABLE — 176 records of 28 bytes.** New this
session, and the derivation is worth keeping because every step names an
instruction. `build_palette_upload_queue` at 0x3BEC pushes a (dest, src)
pair per call:

    3c20:  lea 0x840800,a1 ; lea (2,a1,d0.w),a1   dest = palette RAM,
                                                  slot*32 + 2
    3c3a:  lea 0x242a0,a1  ; lea (0,a1,d0.w),a1   src, d0 = index*7 << 2
                                                       = index * 28
    3c5a:  the drain copies exactly 7 longwords = 28 bytes

So the record is 28 bytes and the index is object $0B — the same field the
three per-scene palette-id tables at 0x73DA, 0x92EA and 0x173A0 write
(entry 46). The region runs to the sprite frame table at 0x255E0, which is
**4928 bytes = 176.00 records exactly**, and the largest id those tables
carry is 0xAD = 173. The count and the largest index agree, and neither
was assumed.

**Two corrections.** The palette blocks end at 0x242A0, not 0x24000 — four
blocks of 0x400, which is what `bake_tilecram.py` has always indexed. And
the sixth entry of the scene descriptor is garbage, like every other
per-scene table (entry 46's five-and-a-garbage-sixth signature), so the
tilemap walk must stop at five.

**What is left, largest first, with its reader.** These are the work list,
not a mystery:

    0x21400-0x232A0  7840  62 longwords point at it from records at
                           0x1DD28 and 0x1E85C; content is ascending tile
                           indices
    0x3E4AC-0x40000  6996  a 4-entry pointer table at 0x1834 (0x3F6B0,
                           0x3E4B0, 0x3EDB0, 0x3E4B0), read by the
                           function at 0x1366, which inverts the bytes and
                           masks them to 3 bits
    0x25A24-0x26C20  4604  one pointer, from 0x14E06
    0x1EF20-0x20000  4320  7 pointers, all from data
    0x29000-0x29E00  3584  NOTHING points at it. Between the last tile
                           upload block and scene 0's tilemap, and its
                           content is an ascending tile-index ramp ending
                           in 0xFFFF padding — most likely the real tail of
                           the 0x28B84 block, whose end entry 48 did not
                           measure.

Output is `docs/audit/rom_map.txt`.

---------------------------------------------------------------------
## 78. The map's biggest class was wrong, and the attract demo is a RECORDING

Open item 5 was "515 functions classified by signature, not read". Reading
515 functions is not a session's work, so this does the two things that
are: it makes the existing classification honest, and it reads the
functions worth the most.

**`hardware` meant "touches memory".** `classify.py:83` assigns it when
`func_profile.py` reports any region — and that profiler's region list
includes `workram` and `objtable` (func_profile.py:32-35). So 117
functions carry the label and **45 of them touch an arcade hardware
address**. The other 72 touch work RAM, which is not hardware. It is the
biggest non-leaf class in the map and it is a plausibility label.

`tools/func_profile_ref.py` recomputes the whole profile from the
instruction stream, with the arcade surface kept separate from work RAM,
and needs no Ghidra:

    arcade hw          51      touches a real board address
    object routine     56      touches object fields through A6
    work RAM only      ...     what "hardware" mostly meant
    leaf/helper       170      touches neither

**Then the reading.** Ranked the unread arcade-hardware functions by
callers. Four read to their return:

  - **0x144A `credit_prompt_select`** — picks one of five strings from the
    coin count at 0xFFF000 and the coinage DIP at 0xFFF01A/1B, then tail-
    jumps into the text writer. The strings are at 0x18D4-0x1927: INSERT
    COIN, INSERT MORE COIN, 1 PLAYER START ONLY, 1 OR 2 PLAYER START,
    2 CREDITS 1 PLAY.
  - **0x3AA4 `textram_clear_run`** — `clr.b (a1); addq #2,a1`, d0+1 cells.
    The stride-2 sibling of 0x3A9A, which was already read and named.
  - **0x3AAE `draw_credits_line`** — CREDITS plus the digit, or FREE PLAY,
    from strings at 0x4128.
  - **0x1366 `read_controls_or_demo`**, and this one is the find.

**THE ATTRACT DEMO IS A RECORDED INPUT STREAM, AND THE RECORDER IS STILL
IN THE ROM.** 0x1366 snapshots last frame's inputs, reads 0xC41003,
0xC41007 and 0xC41005, and then branches on 0xFFF026 bit 0:

    13c8:  move.b 0xFFF031,d3      the ATTRACT STEP (entry 75)
    13cc:  andi.w #24,d3
    13d0:  lsr.w  #1,d3            -> 0, 4, 8, 12: a LONGWORD index
    13d2:  lea    (pc,0x1834),a0
    13d6:  movea.l (a0,d3.w),a0    one of four stream pointers
    ...
    13ea:  move.b d0,(a0)+ ; d1 ; d5     RECORD three bytes
    13f2:  move.b (a0)+,d0 ; d1 ; d5     PLAY BACK three bytes

Same routine, both directions. Three bytes per frame — player 1, player 2,
service. The table at 0x1834 holds 0x3F6B0, 0x3E4B0, 0x3EDB0, 0x3E4B0, so
each attract step plays its own stream and two of the four steps share one.

That attributes **0x3E4B0-0x40000 as three demo streams of 0x900 bytes**,
6992 bytes that nothing in the code pointed at, and it explains why the
attract is frame-exact: it is not an AI, it is a tape.

**Rom data attribution is now 72.3%**, up from 68.5%, and the remaining
list is three blocks over 4 kB.

**One cross-check worth recording.** 0x3A9A was already named
`textram_stride_write` by an earlier hand read. Following 0x144A's tail
jump into it arrived at the same description independently. That is the
only kind of confirmation a signature match cannot give you.

---------------------------------------------------------------------
## 79. THE TRANSFORM CHEVRON: records 132-137, table 0x26CC, and it is all rom

The rendering thread has level 2 rendering and level 1's trees pink, and
flagged two things as outside every table it has: the transformation's
chevron renders yellow and orange on red where it should be blue, and the
intro comes out flat purple. Their reading was that the transform
recolours the PLAYER, so it is a sprite palette and no tile table touches
it. That is right, and here is the whole mechanism.

**A sprite palette is 14 words copied VERBATIM out of rom.** Entry 77
found the table; this is what it means for the port.
`build_palette_upload_queue` at 0x3BEC is the ONLY writer above palette
line 63:

    3c20:  lea 0x840800,a1 ; lea (2,a1,d0.w),a1    dest, d0 = $0A * 32
    3c3a:  lea 0x242a0,a1  ; lea (0,a1,d0.w),a1    src,  d0 = $0B * 28
    3c5a:  the drain copies exactly 7 longwords = 28 bytes

Nothing is computed and nothing is allocated. **Lines 0-63 are the tile
and text palettes, lines 64-127 are the actors**, and the split is by
destination address, not by convention.

**The complete set the program can request is 88 of the 176 records.**
Every `$0B` immediate in the code, plus the five tables the six indexed
write sites read: 0x73DA, 0x92EA and 0x173A0 (per-scene, entry 46), plus
two that were not known — **0x26CC** and 0x10136.

**0x26CC IS THE TRANSFORM.** Site 0x24D4:

    24c4:  andi.w #31,$22(fp)      the object's own ANIM FRAME TIMER
    24ca:  lea    (pc,0x26cc),a0
    24d2:  lsr.w  #1,d0            -> 0..15, so it steps every SECOND frame
    24d4:  move.b (a0,d0.w),$0B(fp)

    0x26CC = 132 132 132 132 133 134 135 136 137 0 0 137 136 135 134 133

and records 132-137 are a blue ramp, darkening:

    132  057 046 046 035 024 013 002
    133  057 046 035 035 024 013 001
    134  046 035 035 024 013 002 001
    135  045 035 024 024 013 002 001
    136  035 024 024 023 002 001 000
    137  034 012 001 001 001 001 000

**Measured on the arcade, not inferred.** Tracing object slot 0 — the
player — through the attract with no scene forcing, its palette slot and
index walk together from (0,132) to (5,137) and back down, over frames
2103-2130. The two zeros in the table are the flash.

**So the chevron needs lines 64 to 69, in sequence, changing every second
frame.** That is the shape of the failure the rendering thread is
seeing: not a missing colour, but six records that never reach their
lines, leaving whatever was there — which is why it reads as yellow and
orange on red rather than as black.

**The deliverable is `docs/audit/actor_pal.h`**, generated by
`tools/actor_palettes.py`: all 176 records as MD colour words plus the
88-entry used list. 4928 bytes of rom, entirely static. There is no reason
for a refuse rule to starve a sprite palette; the whole surface can be
baked once and indexed by $0B.

**One caution for the tile side of their problem.** The transform cycle
steps on the object's anim timer, every SECOND frame — the same 2-frame
shape as the round-clear beam. A static table cannot follow it; the
runtime has to apply the index the game asks for that frame.

---------------------------------------------------------------------
## 80. The palette split is ENFORCED: lines 0-63 tiles, 64-127 actors

Entry 79 needs one more thing to be usable as a rule, because "the actor
queue is the only writer above line 63" was true only of ABSOLUTE
addressing. Six palette writers use a computed base, and any of them could
in principle reach line 64. Bounded all of them:

    0x01EF2 0x01F80 0x020A0  clr.w 0x840000, one word
    0x02BB8  0x840000 + 32 longs                      -> 0x840080
    0x02B7E  0x840400 + per-scene table at 0x326E     -> 0x840720 worst
    0x02B94  0x84006C
    0x025BA  0x840720 + 13 longs                      -> 0x840754
    0x025E8  0x025F0  0x0263C  0x840250 / 0x8400B0
    0x030C2  0x840000 + (idx & 127) * 16              -> 0x8407F0
    0x03116 0x03846  0x840040 + 8 longs               -> 0x840060
    0x04544  0x840010 + 20 longs                      -> 0x840060
    0x1A4F0  0x840490 + 19 longs                      -> 0x8404DC
    0x1A934 0x1B0E6  absolute, all below 0x840030
    0x1BAB6  0x840080
    ----
    0x03C20  0x840800 + $0A*32 + 2      THE ACTOR QUEUE

**The closest any of them gets is 0x8407F0 — the colour cycler, one word
short of line 64.** Its index is masked to 127 and scaled by 16, so it
cannot pass 0x8407F0 whatever the descriptor says.

**0x2B7E was the one worth bounding properly**, because its base and its
length both come from the per-scene table at 0x326E that entry 46 listed
without decoding. It is (word offset, word count) per scene:

    scene 0  offset 0x00A0  count 111  -> ends 0x840660
    scene 1  offset 0x0000  count  39  -> ends 0x8404A0
    scene 2  offset 0x0260  count  47  -> ends 0x840720
    scene 3  offset 0x0000  count  35  -> ends 0x840490
    scene 4  offset 0x0200  count  63  -> ends 0x840700
    scene 5  offset 0x0102  count 772  -> would end 0x841116

The sixth entry would cross, and the sixth entry is the garbage one every
per-scene table has (entry 46). Five scenes, five bounded uploads.

**So the split is a property of the program, not a convention:**

    palette lines 0-63    tile and text, ten writers, all bounded
    palette lines 64-127  actors, ONE writer, verbatim from rom 0x242A0

A refuse rule that keys on the destination line needs to apply to 0-63
only. Nothing in lines 64-127 is allocated, computed or contended — it is
a table lookup — so intro, transformation and transitions cannot be
starved by a tile-palette policy that stops at line 63.

---------------------------------------------------------------------
## 81. The pack's SOURCE is confirmed against the arcade, and nothing the map uses moves

The rendering thread has level 1's trees pink and a purple band in the
sky, and has ruled out the pen map's sample choice and the source colours
(the latter by comparing our gated attract dumps against their own in-game
dumps). Their remaining split is pack versus install. Two measurements
narrow it further, and both need the oracle rather than another of our
own dumps.

**First, the palette RAM layout, because two formats live in the same
4 kB and getting it wrong makes every other statement here meaningless:**

    tile palettes    128 x 8 colours    0x840000 + p*16    p = 0..127
    sprite palettes   64 x 16 colours   0x840800 + s*32    s = 0..63

Tile pixels are 3bpp so a tile palette is EIGHT colours — which is why
`bake_tilecram.py` has always read `base + p*16` with pens 1..7, and why
entry 80's "lines 0-63 / 64-127" split was the right boundary described in
the wrong units. The two halves do not overlap: tiles end at 0x8407FF and
sprites start at 0x840800. Every tile-palette writer bounded in entry 80
stays inside the first half, and the actor queue is the only thing in the
second.

The scene tilemaps confirm it from the other side: they reference 34, 16,
18, 14 and 19 distinct tile palettes, almost all of them in 64-127, and
the per-scene upload at 0x2B7E writes exactly that band —
0x840400 + a per-scene offset, which is tile palette 64 upward.

**Second: nothing the map uses is animated.** Measured on the arcade over
800 frames at scene 0, every palette word that changes at all:

    tile pal 19    8 words move, 5486 changes    the colour cycler at 0x30B2
    tile pal 20    6 words move, 1015 changes
    tile pal 21    6 words move, 1015 changes
    tile pal  6    7 words move,  214 changes
    tile pal 0-7   a handful each, at level load
    ---
    tile pal 64-127                NOT ONE WORD MOVES

Scene 0's map uses palettes 74-102. **None of them changes, ever.** So a
static per-scene table is the right shape and colour cycling cannot be
what makes the trees pink. The cycler's three palettes are not in any
scene's map.

**Third, and this is the one only this thread could run: the pack's
SOURCE is byte-identical to the arcade.** `tools/arcade_palram.lua` dumps
the arcade's own palette RAM at 0x840000 on an attract step that shows the
scene (entry 75's f031 gate). The port mirrors palette RAM 1:1 at work RAM
0xFF9000, so the two compare word for word:

    scene 0   34 map-referenced tile palettes,  0 differ
    scene 1   16                                0 differ
    scene 2   18                                0 differ
    scene 3   14                                0 differ
    scene 4   19                                0 differ

Their own check compared two of OUR dumps against each other on three
sets. This is all five scenes, every palette the map references, against
the hardware.

**So the source data is eliminated, and animation is eliminated. What is
left is pack-to-install, which is exactly what their readback verifier
tests.** Nothing here does that job for them; it removes the two
alternatives so a negative result there means something.

References are staged as `discover/cram/arcade/sceneN.bin` and
`tools/palette_oracle.py <scene> <dump.bin>` diffs any 0x1000-byte palette
dump against them, reporting only the palettes the scene's map actually
uses.

---------------------------------------------------------------------
## 82. TIMING HAZARDS — the game paces itself on IRQ4 and nothing else

Entry 76 closed the dependency census for everything an opcode can
signal, and named what it could not see: a loop calibrated in CPU cycles,
or one that busy-waits on something the arcade guarantees. Those are the
classes that should worry a port whose 68000 runs at 7.670 MHz and is NOT
stalled on a video bus the way the arcade's 10 MHz part is. This is that
class, and **the answer is a clean negative.**

`tools/timing_hazards.py` asks three questions separately, because the
three fail differently. Over all 19137 instructions:

    CYCLE DELAY      a dbf branching to ITSELF, empty body      1
    INTERRUPT DELAY  a counted loop around stop                 1
    BUSY-WAIT        test and branch back, <= 3 instructions    3

**One cycle delay in the whole program**, at 0x2D8C: `moveq #127,d0` then
`dbf d0,0x2D8C`. About 1280 cycles — 128 us on the arcade's clock, 167 us
on ours. **It is in the service-switch path**, after the wait at 0x2D82,
and never runs in gameplay.

**So nothing in the gameplay path measures the clock.** That is the check
the scope argument needed and nobody had run: a slower, unstalled 68000
cannot desynchronise this game through a delay loop, because there are no
delay loops to desynchronise.

**The three busy-waits, and two of them are the same byte.**

    0x3982  tst.b 0xFFF01C / beq       the frame flag (entry 67), known
    0x2D82  btst #2,0xFFF0C2 / bne     MCU_COINS bit 2 — SERVICE switch
    0x2DA8  btst #7,0xFFF0C2 / bne     MCU_COINS bit 7 — TEST switch

0xFFF0C2 is MCU_COINS, the byte the i8751 posts, which `patch_game.py`
already names and `md_main.c:4670` already writes. Both waits sit inside
IRQ4 and each ends in an `rte` to a fixed vector — 0x2D82 to test mode at
0x1AFDE, 0x2DA8 to the reset entry at 0x400. They spin until the operator
lets go of the switch. **The port never sets bits 2 or 7** (md_main.c:4670
builds svc from coin, start1 and start2 only), so neither can hang, and
neither can be entered.

**The interrupt delay is 181 STOPs** at 0x1B5B2, in test mode, and it
belongs to the twelve STOP sites entry 37 found.

**Every timing-sensitive site in the program is in the service and test
path.** The cycle delay, the STOP loop, and two of the three busy-waits,
plus the twelve STOPs from entry 37 and the write-only read-modify-write
from entry 76. That is now one rule with one list behind it: the port
cannot enter service or test mode, and nothing else in the program cares
what clock it runs at.

**The detector's own trap, twice over.** A self-branching `dbf` has a
target EQUAL to its own address, so a loop finder that requires
`target < address` drops exactly the shape a cycle delay has — the first
run of this reported zero. And a conditional branch backwards to an
`rts` is a shared EXIT with the same three-instruction shape as a spin;
three of five busy-wait candidates were that, and 0x0F4CE and 0x166F2 were
checked by hand before the filter was written rather than after.

---------------------------------------------------------------------
## 83. MY OWN TOOL INVENTED A BLOCK'S 62 REFERENCES. And the zoom table's real end.

Entry 77's rom map prints, for each unattributed run, who points into it.
The largest run — 0x21400-0x232A0, 7840 bytes — showed **62 pointers**
from records at 0x1DD28 and 0x1E85C. Chasing them was the next job. They
do not exist.

**The records at 0x1DD28 are four WORDS, not two longs:**

    0x1DD28:  0002 1400 0190 1301
    0x1DD30:  0120 1410 0044 0007
    0x1DD38:  0060 1410 00EC 0007
    0x1DD40:  0102 1414 0190 1300

The second word runs 0x1400, 0x1410, 0x1410, 0x1414 — ascending, an index
of some kind. **Whenever the first word happens to be 0x0002, a 4-byte
window over the pair reads 0x00021400 and my scan calls it a tidy pointer
into 0x21400.** Every one of the 62 is that. The block has no references
at all.

**The tell is cheap and it is now in the tool.** A straddle can only ever
produce ONE high word, because that word is a different field holding the
same small value; real pointers into a block come from several. `rom_map.py`
now prints the count of distinct high words and marks a block reached by
exactly one as SUSPECT. It flags 0x21400 and 0x25A24 and leaves
0x1EF20-0x20000 alone, which has seven references across several high
words and is therefore credible. **No candidate is dropped** — the number
that decides it is printed and the reader judges.

This is the same failure entry 50 catalogued as "a number matching a
constant you already hold", committed by a tool rather than by a person,
which is worse: it produced sixty-two of them and they all looked alike.

**And chasing it settled the zoom table's real end.** Entry 48 read the
zoom lookup at 0x3CD4 but bounded the table by eye:

    3ce0:  move.b (a0)+,d0        the size class -- a BYTE
    3ce2:  lsl.w  #5,d0           32-byte rows
    3cea:  andi.w #31,d0          the zoom level, 0-31
    3cee:  move.b (a2,d0.w),d6

A byte shifted left five reaches 8160, plus 31 is 8191. **So the table is
exactly 256 rows of 32 bytes, 0x20000-0x22000**, and the bound is the
addressing rather than a guess. Entry 46's "monotonic decay curve at
0x210C4" and entry 48's "more of the same table" are both inside it. That
attributes 4096 bytes that were sitting in the largest unattributed run.

    rom data named   72.3% -> 73.9%

**What is actually left in that area**: 0x22000-0x232A0, 4768 bytes, 2384
words, every one a valid 13-bit tile index and ascending in runs
(0x1472 0x1473 0x1474 0x152D 0x152E ...). **Nothing in the rom points at
it** — no code reference and no credible data pointer — so it is reached
from a computed base, like 0x29000-0x29E00. Two blocks now sit in that
category and they are the honest end of what pointer-chasing can do.

---------------------------------------------------------------------
## 84. The pipeline has a bootstrap now, and the two address sets are measured

Everything from entry 73 onward runs without Ghidra, but it all stood on
`repair_seeds` — an instruction address list that exists because someone
once had a reference disassembly of THIS title. A new title has neither,
so the kit had a hole exactly where it claims to be general.

`tools/code_walk.py` closes it, and the measurement is the useful part.

**Recursive descent from the vector table gets 4.4%.** All 64 vectors
point into a four-instruction stub, and System 16 dispatches nearly
everything through `jmp (a0)`.

**Running it to a FIXPOINT gets 29.3%.** Each pass harvests, out of the
code it has just decoded, every longword installed as an object routine
pointer (`move.l #addr,$02(a6)`) and every address pushed for an `rte`
dispatch, seeds those and goes again. Five passes, 449 objdump runs, three
seconds. **Zero wrong**: every address it finds, the reference also calls
an instruction — except two, 0xBD6 and 0x4832, and 0xBD6 is a function
entry in our own map, so the reference's list is the one that is short.

**Pointer-table discovery adds nothing here, and the reason is worth
keeping.** A run of longwords that mostly point at decoded code is a jump
table — but the per-scene dispatch tables at 0x92F0 and 0x17E24 have NO
entry reachable any other way. Not one of their targets is in the descent
set, so there is nothing to validate them against. **A table whose every
target is only reachable through the table cannot be bootstrapped.**

**And a plain linear sweep gets 99.8%.** From the reset entry to the code
ceiling: 34748 boundaries, of which 19107 of the reference's 19137, 30
missed and 15641 invented over data.

    descent   5601 addresses   29.3% recall   0 wrong
    linear   34748 addresses   99.8% recall   15641 wrong

**The descent set is a strict SUBSET of the linear one — on 5601
addresses the two never disagree.** So the recipe is not to pick one:
take the linear superset for a hazard census, where missing a TAS is the
failure and a false positive costs one hand check, and the verified set
for a rom map, where a false instruction inflates code coverage and hides
data underneath it. Where they disagree, descent wins.

`TOOLKIT.md` now carries the whole pipeline as a numbered recipe —
addresses, stream, dependency census, timing census, bounds and profile,
rom map — with the trap that bit each step written next to it.

---------------------------------------------------------------------
## 85. Let the running game name the reader — and a rig that reported nothing

Entry 83 left two blocks that no pointer reaches, because they are read
from a computed base. Pointer-chasing is finished on those; the way to
name a consumer is to watch the program read it.

**The first rig reported nothing, and the control is why that is not a
finding.** `install_read_tap` on the 68K program space fires on nothing at
all — not on the two blocks, and not on the zoom table, which every sprite
reads every frame. MAME serves rom reads from the direct access pointer
and taps never see them. **A control range was in the run before anything
was concluded from the silence**, which is the only reason this cost ten
minutes instead of an evening.

Debugger watchpoints do work, because installing one disables the fast
path for that range: `-debug -debugger none`, `wpset`, and a periodic
callback that reads the PC on each stop and resumes.
`tools/rom_reader_wp.lua`. One range per run — the watchpoint does not
tell lua which address it fired on, and which block was read is the whole
question.

    0x29000-0x29E00   PC 0x25A0, frame 20
    0x22000-0x232A0   nothing, in attract or with a game started
    CONTROL 0x1CE2    PC 0x16AA and 0x16B0, frame 452

**0x25A0 is the tile upload loop, so the block is upload data — and
reading the loop gives the format exactly:**

    258a:  move.l #0x400000,d0
    2590:  move.w (a0)+,d0      dest low word: 0x400000 + it
    2594:  move.w (a0)+,d0      cols - 1
    2596:  move.w (a0)+,d1      rows - 1
    259c:  move.w (a0)+,(a2)+   cols words
    25a2:  lea 128(a1),a1       next row: a 64-column tilemap stride
    25a6:  dbf d1,0x2598

Four callers each load a0 and call it a fixed number of times, so the
extents are arithmetic rather than estimates, and the three groups tile
the region contiguously:

    0x26C20-0x278B8    4 blocks   caller 0x2572
    0x278B8-0x28B84    8 blocks   caller 0x2552
    0x28B84-0x291D0    2 blocks   caller 0x2564

Fourteen blocks, every destination inside 0x400000-0x407FFF. **The last
group ends at 0x291D0, not 0x29000** — entry 48 bounded it by eye and
entry 83 guessed the tail was upload data. It is, and now it is measured.

    rom data named   73.9% -> 74.2%

**And the block that stays silent.** 0x22000-0x232A0, 4768 bytes of valid
13-bit tile indices, is read neither during attract nor with a game
started and played to the middle of level 1 — on a rig whose control
fires in the same run. That is a real absence with a stated scope, not an
unexamined one: it says nothing about the later rounds, which this input
script never reaches.

---------------------------------------------------------------------
## 86. LEVEL 4 IS NOT HEAVY. The game asks LESS of it than of levels 1 and 3.

LOOP29 198 ruled out the background for level 4's slowdown and put the
cause "on the sprite or game-logic side". That half is measurable from
the arcade, and it comes back negative too.

**Reaching a later level without playing to it.** 0x64E reads the starting
round from the table at 0x1848 with `(0xFFF031 & 0x18) >> 3`, so writing
one value across that table starts the game at that round —
`tools/round_workload.lua`, `RW_N=<round>`. Verified per run: 0xFFF14E and
0xFFF142 both read the round asked for.

**Same input script every round, 601 samples over frames 1500-4500:**

    round  objects      live sprites   drawn scanlines   zoom sum
      0    7.1 / 18     6.4 / 17       295 / 669          4.7
      1    7.7 / 26     5.0 / 16       213 / 838         39.6
      2    8.7 / 24     6.4 / 21       291 / 856         36.0
      3    6.6 / 18     5.7 / 17       233 / 657          8.7
      4    4.2 / 10     2.9 /  8       139 / 399         10.0

**Round 3 is level 4, and it is below average on every one.** Fewer
objects than rounds 0, 1 and 2; fewer drawn scanlines than 0 and 2; a
fifth of round 1's zoom load. Round 4 is the lightest in the game.

**Getting the sprite count right mattered.** The first pass counted
non-zero bytes in the order list and got 255 of 256 in every round — the
stale contents of a list nobody clears. The hardware's own test is in
`jts16_obj_scan.v:83-85`: word 0 is top in the low byte and bottom in the
high byte, and `badobj = top >= bottom`. Counting records where top <
bottom gives 3 to 6, which is what the screen shows. **Drawn scanlines**,
the sum of `bottom - top`, is the number a software renderer actually
pays, and it is the one worth having.

**So both halves of LOOP29 198's split are now closed from the game side.**
The background was ruled out there; objects, sprites, drawn area and zoom
are ruled out here. Level 4 does not ask the port for more work, so the
cost is something the PORT does differently for that round.

**One caveat, stated rather than buried:** the script walks right and
attacks, so it does not fight the level the way Mike does. It is the same
script in all five runs, which makes the comparison fair for what the
level itself spawns, and it is not a worst case.

**And one hypothesis for the rendering thread, free.** The slowdown
appeared on vi44, and the round-table channel is new (LOOP29 193-197). If
level 4 was not slow before the round tables, the suspect is the install
path, not the level — LOOP29 194 already found a SECOND install site that
was keyed wrongly, and a third that re-installs per frame would cost most
where the table is smallest to detect.

---------------------------------------------------------------------
## 87. Where the 68000's time goes, per round — and level 4 is lighter there too

Entry 86 counted what each round asks for. A count is not a cost, so this
profiles the 68000 directly: MAME's own instruction trace over a fixed
window, histogrammed and attributed to functions.
`tools/round_profile.lua` drives it, `tools/round_profile.py` reads it.

**Sampling does not work and it is worth knowing why.**
`emu.register_periodic` fires ONCE PER FRAME, always at the same point in
the frame, so its "profile" is a single address. The trace is exact and
20 frames of it is 2.7 MB.

**Two things in the trace format cost a whole wrong answer first.** MAME
prints trace addresses in UPPERCASE hex, so a `[0-9a-f]` match keeps only
the addresses that happen to be all digits — 8% of the file. And MAME
COLLAPSES tight loops into `(loops for N instructions)`, so ignoring those
lines counts every loop once. Together they under-reported the work by a
factor of 29, and **the first profile looked entirely plausible** — 449
instructions a frame, a sensible-looking top four. That is the shape of
this failure: not an error message, a believable number.

**20 frames from f2000, same input script, per round:**

    round   instructions     idle      work    work/frame
      0          269973    106283    163690         8184
      1          266870    141097    125773         6288
      2          268505    139908    128597         6429
      3          269862    123720    146142         7307
      4          261018    222722     38296         1914*

    * round 4 is NOT a comparison: the generic script dies on level 5 and
      the trace is of the credit screen — its top routines are
      credit_prompt_select and draw_credits_line.

**Round 3 is level 4, at 7307 instructions of work a frame against level
1's 8184.** Lighter on the 68000 as well as on objects, sprites, drawn
scanlines, zoom and background cells.

**Per frame, by routine:**

    routine                      r0    r1    r2    r3
    irq4_handler               1172  1138  1130  1155
    despawns                    725   819   826   684
    sprite_build_and_cull       807   425   365   625
    object_dispatcher           459   455   430   453
    arcade hw                   357   363   362   368
    floor_collide               365    82   404   380
    collide_box_b               411   244   252   253
    zoom_scale_lookup           360   227   187   320

**IRQ4 is flat at ~1150 a frame in every round**, which is the useful
structural fact: the handler's cost does not vary with the level, so
anything in the port that scales per round is not tracking the game.

**One caveat on the absolute numbers.** MAME does not model the S16B
video bus stall, so these are instruction COUNTS on an unstalled 10 MHz
68000 and are not comparable with LOOP27's 2780-instructions-per-vint
figure, which is a different measurement. The comparison ACROSS rounds is
like for like and that is what the question needed.

---------------------------------------------------------------------
## 88. THE SCOPE NUMBER IS 2.5x LOW. The arcade is not bus-bound.

Entry 87 measured 8184 work instructions a vint on the arcade at level 1.
ARCHITECTURE.md and START-HERE say the game needs **2780**. Two
measurements of the same thing on the same emulator cannot both be right,
and the difference is load-bearing: the whole scope argument rests on it.

**`tools/arcade_trace.py` counted the lines MAME LISTS.** MAME collapses a
tight loop into one `(loops for N instructions)` line, so every loop was
counted once. Same traces, both ways:

    round 0   listed  108661 -> 5719/frame     executed 269973 -> 14209/frame
    round 3   listed   94510 -> 4974/frame     executed 269862 -> 14203/frame

    under-count factor 2.48x and 2.86x

**And the conclusion does not survive it.** The old chain was: 2780
instructions a vint, 127,841 cycles at 7.670 MHz, therefore any cost up to
46 cycles per instruction fits; the arcade itself runs at 45.2, so it is
stalled on its video bus and we do not pay those stalls. **45.2 cycles per
instruction was the artefact.** Re-measured:

    arcade executed            14,209 per vint
      the frame wait            6,025
      WORK                      8,184
    arcade cycles/instruction    11.7      a normal 68000 mix
    our allowance                13.3      127,841 / 8,184

A 14% margin, not a fourfold one. The arcade is not a bus-bound machine
and there are no stalls for us to avoid paying.

**Our own rom, traced identically** — `rom/night/vi39.32x`, MAME's 32X,
level 1, 20 frames. The 68K side is the half MAME models honestly, and
the PC separates the two halves cleanly because the game is rebased to
0x9xxxxx and the shim lives in MD RAM:

    executed                   12,418 per vint
      the frame wait            2,781
      WORK                      9,637
        game  (0x9xxxxx)        4,904   50.9%
        shim  (0xFFxxxx)        4,733   49.1%

**LOOP27 79's ratio is exactly right and always was** — "our shim costs as
much as the game itself", 49.1% against its 47.9%. Only the absolutes
moved, and they moved by the same factor on both sides, which is why the
ratio held while the budget claim did not.

**What this does NOT establish, and the temptation is to say it anyway:**
that the 68000 is now the binding constraint. Our rom still spends 2,781
instructions a vint in the frame wait, and that wait is the game blocked
on the port's frame flag, not proof of spare CPU. The honest statement is
narrower: **the clock has a 14% margin rather than a comfortable one, so
shim instructions cost something they were assumed not to.**

The parser is fixed, and both entry-point documents carry the correction
at the top of their scope sections rather than a quiet edit.

---------------------------------------------------------------------
## 89. The corrected number turns into a 2121-instruction target

Entry 88 killed the "fourfold headroom" claim. What replaces it is better
than a warning, because the same traces give the size of the problem.

    one vint at 7.670 MHz            127,841 cycles
    our measured mix                    10.29 cycles/instruction
    one vint therefore holds           12,420 instructions

    at 60 Hz, one game frame in one vint:
      game, per game frame              9,808
      shim, once instead of twice       4,733
      needed                           14,541
      available                        12,420
      THE GAP                           2,121

**The 60 Hz gap on the 68000 is 17%, not a factor.** And three
instructions carry most of it: 0xFF0964, 0xFF0DD0 and 0xFF1224 are 644,
493 and 470 instructions a vint — 1,607 together, 34% of the shim and 76%
of the gap. Three tight loops.

The second lever is the same size. **Our game side costs 9,808
instructions a game frame where the arcade's costs 8,184** — the same code,
20% more work. LOOP29 182-184 already found that the game discards a
release arriving while it works, which is the shape of a protocol cost
rather than a code cost.

I did NOT map the three PCs to symbols. `rom/md_start.lst` is the current
build's and vi39 is not the current build; attributing a hot address
through a map I cannot prove matches is how a session gets spent
optimising the wrong loop. The rendering thread has the right map.

The plan, its arithmetic and — more importantly — the four assumptions it
rests on are in `docs/handoff/PLAN-68K-BUDGET.md`. The first assumption is
the one that could still move the answer: 10.29 cycles per instruction is
our mix INCLUDING the frame wait, and at 60 Hz there is no wait. If work
instructions average 12 cycles the gap is 3,700, not 2,121. **Measure
cycles before trusting the margin** — the last number that went unchecked
was off by 2.5x.

---------------------------------------------------------------------
## 90. r60_push IS the 60 Hz gap on the 68000

Entry 89 left two things: map the three hot PCs, and price the wait loop
so the margin is real. Both done, and they point at one routine.

**The map had to be earned.** `rom/md_start.lst` is the current build's
and the trace was vi39's; the embedded `md_start.bin` differs by 2453
bytes, so that map does not apply. Snapshotted the current rom WITH its
own map, verified the embedded image byte-identical to `md_start.bin`,
and traced that instead. Its totals land within 2% of vi39's and the hot
addresses are identical, so the layout is stable across both.

**The shim, per vint, by routine:**

    r60_push                    2,621     54.5%
    r60_ship_words.isra.0         690
    r60_blast.constprop.0         512
    md_consume                    349
    shim_vblank                   324
    read_joypad                   115
    get_input                      80
    everything else               122
                                -------
                                  4,813

**`r60_push` alone is more than the whole 60 Hz gap.**

**And the assumption that could have moved the answer does not.** The
frame wait is `tst.b (xxx).W` at 12 cycles plus a taken `beq.s` at 10 —
22 cycles for two instructions. 2,736 idle instructions a vint is 1,368
iterations = 30,096 cycles, leaving 97,745 for 9,698 work instructions:
**10.08 cycles per work instruction**, marginally cheaper than the 10.29
average rather than dearer. In cycles:

    needed at 60 Hz    14,581 instructions x 10.08 = 146,976 cycles
    available                                        127,841
    the gap                                           19,135 cycles
                                                       1,898 instructions

    r60_push                                          26,420 cycles

So the plan reduces to one line: **halve `r60_push` and the 68000 side of
60 Hz is met.** Its own hottest instruction is +0x45C at 517 a vint, and
the two next-largest routines, `r60_ship_words` and `r60_blast`, are the
other two loop heads from entry 89.

Which is a pleasing place to land, because `r60_push` is the packet
transport — the thing ARCHITECTURE.md's first line already calls the whole
project. The correction did not change what to work on. It changed the
belief that the 68000 could afford it.

---------------------------------------------------------------------
## 91. The silent block is silent on all five rounds

Entry 85 found 0x22000-0x232A0 unread in attract and in level 1, with the
rig's control firing in the same run. The round mechanism from entry 86
extends that properly: `RT_ROUND` rewrites the DIP round table at 0x1848,
so the block gets its chance on every level rather than only the first.

    round 0   no read      round 3   no read
    round 1   no read      round 4   no read
    round 2   no read
    CONTROL (scene descriptor, round 3, same rig)   PC 0x16AA and 0x16B0

Five rounds, ~5400 frames each, a game started and played in every one,
and the control fires. **4768 bytes of valid 13-bit tile indices that
nothing in the rom points at and nothing reads on any level.**

That is as far as this method goes. It is either dead data — a title's rom
carries plenty — or it is reached by a path this rig does not enter: two
players, a continue, an ending, a boss state the script never survives to.
Recording it as unread with the scope stated, not as unknown.

---------------------------------------------------------------------
## 92. TILE PALETTES 19-21 COLOUR THE CUTSCENE PAGES, AND THE GAME HAS A ONE-BYTE CUTSCENE SWITCH (2026-09-12)

HANDOFF-DECOMPILE-3 OPEN item 3. Everything below is read off the
consuming instruction or the rom bytes; nothing is a filter or an absence.

**The cycler's descriptors are a fixed rom table, loaded on every scene
load.** 0x1A6BA clears 16 slots (32 longs) at 0xFFF300 and loads the
table at 0x1A6FA — a count word, then (line word, script long) pairs:

    slot 0   line 19   script 0x1A70E
    slot 1   line 20   script 0x1A78E
    slot 2   line 21   script 0x1A78E

One caller, 0x76C, in the scene-load sequence after the tilemap unpack
(0x1694) and the palette load (0x3952). Slots 1 and 2 share a script and
start with the same timer and index, and the stream has exactly two
references to 0xFFF300-0xFFF33F (0x30B2 and 0x1A6BA), so **palettes 20
and 21 are identical on every vint**.

**Script format, from the cycler's own arithmetic (0x30D4-0x30FE):** a
count word, then 18-byte entries of a hold word and EIGHT colour words.
The index advances when the per-slot countdown reaches zero, wraps at
count, the entry's hold reloads the countdown, and four `move.l` copy 16
bytes — one whole 8-colour tile palette. **Entry 42 said three longs and
six colours. It is four longs, eight colours** (0x30F8-0x30FE).

Line 19, 0x1A70E: 7 steps, hold 1 each. Colour 0 is 0x7FFF (white) in
every step; colours 1-7 are the blue ramp 0x4900..0x4F00 rotated one
place per vint (blue = {pal[11:8], pal[14]}, `jts16_colmix.v:58`, so
19/31 up to 31/31 with no red or green). Period 7 vints. Step 4 carries
0x4C00 where the rotation would put 0x4E00 — a rom fact.

Lines 20/21, 0x1A78E: 6 steps, holds 4,2,2,2,2,4 = 16 vints. Colour 0 is
0x0A00 (blue 20/31); colours 1-7 begin as pure red 0x100F (red 31/31); a
yellow head (0x305F 307F 309F 30BF 30DF, green 11 to 27 at red 31) enters
at colour 1 and walks out to colour 5 across the six steps, then the
ramp restarts all-red. A flame lick.

**What consumes them.** S16B tiles take their palette from map-word bits
12:6 (`jts16_scr.v:197`), so palette 19 is tiles 0x4C0-0x4FF, 20 is
0x500-0x53F, 21 is 0x540-0x57F. The two small writers that follow the
unpacker on every scene load (entry 10) lay exactly those:

  - 0x174E: 40x20 cells at 0x40A230 = page 10, row 4, column 24. High
    byte 0xA5 (priority SET, tile 0x5xx), low bytes from 0x199A (800
    bytes, 0x00-0x7E). 728 cells are palette 20, 72 (bytes >= 0x40) are
    palette 21. Rows 8-19 are near-solid byte 0x2A; rows 0-7 are mostly
    0x01 with five 3x4-cell objects (04 05 06 / 09 0A 0B / 0F 10 / 16 17)
    and two 3x3 ones (11 12 13 / 18 19 1A / 25 26 27).
  - 0x170A: 40x20 cells at 0x40B230 = page 11, same row and column. High
    byte 0x04, low bytes from 0x1CBA: four 8-byte rows C0-C7, C8-CF,
    D0-D7, D8-DF, repeated five across and five down. Tiles 0x4C0-0x4DF,
    all palette 19, priority clear.

That is LOOP29 202's chevron plane (page 11, set 19) and its red field
and flames (page 10, sets 20-21). Both pages are resident from the moment
ANY scene loads; the cutscene uploads nothing. What the rendering thread
did not have is the timings above and the switch.

**The switch is WRAM byte 0xFFF148.** 0x3A00, called every frame from the
main loop (0x88C, 0xA38) and from 0x1F32, 0x1FBC, 0x1A462:

    3A00  tst.b  $FFF148 ; beq 3A2A
    3A0C  clr the four scroll shadows 0xFFF0E2/E4/E8/EA
    3A1C  move.w #$AAAA,$FFF0F4        scr1 (foreground) = page 10
    3A22  move.w #$BBBB,$FFF0F6        scr2 (background) = page 11
    3A2A  otherwise: page words from the tables at 0x40F0 (scr1) and
          0x4100 (scr2), indexed by hscroll bits 9-11

IRQ4 copies 0xFFF0F4/F6 out through the pointers at 0xFFF0EC/F0
(0x2B02-0x2B14), set once at 0x51C/0x524 to 0x410E80 and 0x410E82.
**This is the page-select writer entry 59 could not find**: the immediate
sits in a WRAM shadow and the text-RAM store is pointer-indirect. 0x3A1C
is the program's only writer of 0xAAAA/0xBBBB, so pages 10/11 are on
screen iff 0xFFF148 was non-zero when 0x3A00 last ran.

The level tables, for the record:

    0x40F0 scr1  0000 0000 0000 4040 3434 2323 1212 0101
    0x4100 scr2  5555 5555 5555 9595 8989 7878 6767 5656

A level's foreground is pages 4..0 and its background pages 9..5, walked
by the horizontal scroll; entry 59's measured 0x0000/0x5555 is index 0-2.
Pages 12-15 are never selected.

**What 0xFFF148 means.** NOTES-FROM-DECOMPILE section 7 called it the
"solo filter": the dispatcher at 0x3992-0x39A6 runs only the object slot
whose index+1 equals it and diverts every other slot to 0x3F04 (hide). It
is set at 0x9104 (`move.b $FFF109,$FFF148 ; addq.b #1,$FFF148`) by an
object routine that takes the machine over, and cleared at 0x91DC, 0xB12,
0x5D6 and 0x1E68. IRQ4 acts on its EDGE (0x2BA8-0x2BE2; the previous
value is kept at 0xFFF149): on the rise it zeroes 32 longs at 0x840000,
tile palettes 0-7; on the fall it restores those 64 words from rom
0x232A0 and calls 0x3108 (the per-scene sky block) and 0x3128.

So one byte says "cutscene": pages 10/11, a single live object, scroll
zeroed, palettes 0-7 blanked. LOOP29 208-209 retired the palette
detectors and key the cutscene on the claim mix, with a lag they measured
(plane at 1590-1660 against the red field at 1575). The shim already
reads 0xFFF142 at `md_src/md_main.c:2384` and carries it in COMM10 bits
13-15; 0xFFF148 != 0 is one more bit from the same place, exact on the
edge and a frame ahead of the page shadows. Handed over in
NOTES-FROM-DECOMPILE.

**Not read:** the object routine containing 0x9104 (map: function 0x90F4,
"animation + claim/lock + state change + palette + sound"). That the
face, the eye and the intro all pass through it rests on 0x3A1C being the
sole 0xAAAA writer plus LOOP29 208's measurement that all three switch
pages — two consumer-read facts, but the routine itself is unread.

**Map defect, not repaired:** `function_map2.md` names the 1830-byte
function at 0x5BE `test_mode_screen`. It holds the boot path, the attract
loop (entry 71) and the scene-load sequence at 0x740-0x7A0. It is the
main loop.

---------------------------------------------------------------------
## 93. OPEN item 4, one step further: the silent block is level-class content

The 2384 words at 0x22000-0x232A0 by field: palette 47-54 and 81-121,
priority never set, tile index 0xBC0-0x1E44. That is the shape of a
scene page (entry 62 has scene 0's viewport at 72-103), not of the
cutscene pages (19-21, priority on the picture). It ends exactly where
the rom tile-palette block at 0x232A0 begins — the block IRQ4's
cutscene-exit restore reads (entry 92). Still unread on all five rounds
(entry 91). Reads as a page fragment shipped in the rom and never
selected; not proven, and no cheaper method is left than the read tap.

---------------------------------------------------------------------
## 94. Entry 92's unread routine, read: 0x90F4 is the cutscene object's constructor

0x90F4 (218 bytes) builds the object that owns the cutscene, and 0x91CE
(162 bytes) is the routine it installs. Read in full.

Constructor: posts sound 0 (0x3352), hides itself (0x397E), then
`move.b $FFF109,$FFF148 ; addq.b #1` — the solo filter is its OWN slot
index plus one. It then loads a per-round record from the table at
0x99A2 (five longs, indexed by 0xFFF142*4): four sprite-palette ids to
$6C-$6F, a sound command word to $3E, and the animation script after
them to $24. Slots >= 8 add one to each palette id (0x9162-0x9176). The
four ids are claimed with 0x3B2E (entry 2's allocator), the results kept
in $6C-$6F, and the record's sound command is posted on the way out
(0x91C2-0x91C8).

    round 0   rec 0x99B6   pal ids  22 153 155 157   sound 0x46
    round 1   rec 0x9A66   pal ids  22  26  22  26   sound 0x56
    round 2   rec 0x9BAC   pal ids  22  28  22  28   sound 0x54
    round 3   rec 0x9CF2   pal ids  22  30  22  30   sound 0x55
    round 4   rec 0x9E38   pal ids  22 159 161 163   sound 0x46

Per frame (0x91CE): while the script runs, the object sits at
(0xFFF132+160, 0xFFF136+168) — screen centre relative to the camera —
and calls 0x9270 then 0x3D36. When $21 is clear and $22 reads 2 (the
script's end state) it exits: **clears 0xFFF148** (0x91DC), frees the
four palettes with 0x3BCE, hides its sprite (0x3F04), sets byte $1B of
the object at $-44, and posts sound 0x91 twice around a 0x397E.

So entry 92's remaining caveat closes: the only setter of 0xFFF148 is
this constructor and the only in-play clearer is this object's exit. The
byte rises when the cutscene object is spawned and falls when its script
ends, and every cutscene that shows pages 10/11 is this object with one
of the five records. The four per-round palette ids are the face's
actor lines — 22 is common to every round; 153-163 (rounds 0 and 4) and
26/28/30 (rounds 1-3) differ, which is the "transform palette" Mike's
play pass named (CAT1MD memo) seen from the rom side.

---------------------------------------------------------------------
## 95. OPEN item 2, the decompile side: who posts which sound command, and the map covers 72% of the code

The sound thread built its command map by injecting every byte into the
latch in isolation (`tools/soundmap_build.py`, `sndtest/md/sndmap_data.h`:
10 music, 63 ymsfx, 22 speech). What that cannot say is which game event
posts each byte. The program has one sound entry point, 0x3352 (entry
32), so that is a census:

    tools/sound_posts.py --md docs/audit/sound_posts.md

    149 call/jump sites, 144 with an immediate command byte, 46 distinct
    commands; each site named to its function and joined to the sweep's
    class.

The five computed sites are the interesting ones, and they are read:

  - **Level music is a per-round byte table at 0x1858**, posted by the
    main loop at 0x8C8-0x8DE: 0x94 0x95 0x96 0x94 0x95 for rounds 0-4
    (three tracks for five rounds; rounds 3 and 4 reuse 0 and 1). Posted
    as reset (0), then the track, with a 0x397E between — so the sweep's
    "0x94-0x96 music" are the level themes and 0x92 (0xB96, main loop) is
    the attract/title one; 0x90 (five object sites), 0x91 (the cutscene's
    exit, entry 94) and 0x97 (0x5D62) are the rest of the eight.
  - **The round-clear sequence posts 0x93** (0x1A474, unless the round is
    4) and arms the 480-frame timer at 0xFFF02A. The sweep classes 0x93
    as ymsfx because it ends; it is the round-clear jingle.
  - **The cutscene's per-round sound is speech**: 0x46 0x56 0x54 0x55
    0x46 from the 0x99A2 records (entry 94), all `+speech` in the sweep.
  - **The fade is the game's, not the driver's.** The object at 0x16D7E
    posts master-volume commands (SOUND_DRIVER's fade law: 0x01-0x40) as
    its own countdown halves: `d0 = $20 >> 1 ; addi.b #32 ; post` while
    d0 >= 8, so 0x28 down to 0x24; then at step 88 a reset (0), at 89
    full volume (0x40), at 90 command 0xC5 with 0xFFE800 set. Those are
    the census's three "not in sndmap" bytes: 0x00, 0x20 (+d0), 0x40.
  - 0xB6 is posted from 37 sites in the animation/motion routines and
    0xB4 from 12 collision routines: the two commonest effects are a
    footstep-class and a hit-class sound. Not listened to; that is the
    sound thread's rig.

**A caveat that reshapes OPEN item 1.** Naming the sites to functions
exposed 33 sites in no function at all. Measured over the whole stream,
as the UNION of every row's range:

    code bytes in code_stream.txt        75,442
    inside some function_map2 row        59,182   78.4%
    outside every row                    16,260   21.6%   4,044 instructions
                                                          230 runs

Largest runs: 0xF90A-0x1004C (1858 bytes), 0x13B2C-0x14152 (1574),
0x10E20-0x1124E (1070), 0x1B500-0x1B7CE (718), 0x19D36-0x19F66 (560).
`code_stream.py`'s docstring already records the instruction gap (the
555 bodies hold 15,159 of 19,137 = 3,978 outside) and entry 84 cites it;
4,044 here agrees within 2%. The byte share and the run list were not
written down.

**First figure retracted before commit of this entry's successor:** my
first measurement said 28.0%, 21,108 bytes, 5,201 instructions. It took
each address's nearest preceding row as "its" row, and 36 rows nest
inside larger ones, so bytes covered only by an enclosing row were
counted uncovered. The 15,159 figure in the docstring was the check
that caught it. Same shape as entry 50's rule: a number that agrees
with a plausible story was not compared against the one already held. These are object routines
reached through routine POINTERS in the object records (entry 28's
offset 2, e.g. `move.l #$91CE,2(fp)` in entry 94), which no call
instruction names, so the caller-ranked list for OPEN item 1 ranks 78%
of the program. The other 22% has no row to rank.

**Not established:** any function name for the 33 orphan sites beyond
"an object routine in run X". `function_map2.md` is unchanged; the
orphan runs are in `docs/audit/sound_posts.md` as `? ?`.

---------------------------------------------------------------------
## 96. Sizing the map's missing 22%: what a Ghidra pass would have to bound

Entry 95's orphan code, measured against every seed the pipeline holds
(`docs/audit/altbeast_seeds.json` plus the stream's own immediates):

    orphan code                          16,260 bytes   230 runs
    routine pointers landing in it           72 of the 293 in `immediates`
    calls/jumps from mapped code into it     17
    code immediates landing in it             3
    entry points, all sources               90
    bytes reachable from an entry         5,082
    left with no seed at all             11,178 bytes   218 run heads

So the object routines the pointers name are a third of the gap and are
boundable tonight by rule (entry to next entry or run end; 45 of the 90
end on rts/jmp/bra or the `move.l #next,2(fp)` exit idiom). The other
two thirds have no seed: 218 run heads, most of them a handful of bytes
between rows, plus the five big runs of entry 95. The instruction before
an unseeded head is `orib` 71 times and `btst` 33 — which is to say the
head follows a gap in the stream, not a fall-through, and those bytes are
the linear superset's noise as often as they are code.

Not built. A bounding tool that adds the 90 seeded rows is an hour; it
would lift coverage from 78% to ~85% and leave the same 11 KB to argue
about, which is exactly the argument a Ghidra pass with the repairs of
entry 73 is for. Recorded as the size of OPEN item 5, and stopped.

**Also for that pass:** 36 consecutive rows in `function_map2.md` overlap
their predecessor — nested bodies, the thing that made entry 95's first
number wrong.

---------------------------------------------------------------------
## 97. r60_push accounted for, and a third of it removed without changing the packet (2026-09-12)

Mike: go ahead on the 68000 lever (entries 88-90). Rule 3 first: the
routine was re-traced on the rom Mike is playing, then on the tree.

**vi59, MAME, level 1, 20 frames from f2000, symbol map verified byte-
identical to the embedded md_start.bin: r60_push 2,512 instructions a
vint** (entry 90 read 2,621 on the previous build). By loop, with MAME's
collapsed iterations attributed to the loop branch:

    palette pre-scan  (16 longs, C while)         ~570   23%
    rotor over 64 blocks (27 instr per visit)     ~430   17%
    changed-block mask walk (1.6 blocks/vint)     ~330   13%
    straight-line                                 ~250   10%
    rowscroll compare (30 longs, C while)         ~180    7%
    staging pack of dmask words                   ~160    6%
    ids pack                                      ~150    6%
    per-dirty-block setup                         ~125    5%
    record terminator scan (up to 24)              ~86    3%
    lost-push belt copy (17 bytes)                 ~85    3%
    ndirty popcount                                ~82    3%

Two of those are pure equality scans and one is a byte copy: the C
compiles each long to seven instructions (move/cmp/bne/addq/addq/subq/
bne, 64 cycles); cmpm.l + dbne is two (30 cycles). **R60TIGHT=1**
(Makefile, md_main.c r60_ne_longs) replaces the two scans and copies the
belt as four longs. What is shipped is unchanged by construction; proven
by construction is not proven, so **R60TIGHTCHECK=1** runs both scans
and counts disagreements in WRAM:

    MAME, coined level-1 path, 3,000 frames:   18,038 pre-scans,
      13,700 equal / 4,338 not, 0 disagreements; 2,906 rowscroll
      compares, 0 disagreements
    MAME, no-coin attract through the level-2 demo, 6,400 frames:
      36,645 pre-scans, 26,805 equal, 0 disagreements

The rowscroll table never changed in either run (rs_changed 0 of
9,000+), so the helper's "changed" outcome is exercised only on the
palette path — same asm, same constraints, n=30 instead of 16.

**The measurement, on the night-rom recipe** (the tree's `make ship-us`
alone leaves GAMEGATE off and regenerates fmgate_tab.h without the gate;
that rom ran 603 flips against vi59's 947 and would have been a false
baseline. The recipe is the -D set of vi59's .build_flags mapped back to
Makefile variables, verified token-for-token):

    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
        TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 GAMEGATEWAIT=1 \
        TEXTCAPEARLY=1 TAGKEEP=1 PENHOLD=1 PENREPAINT=1 NBUILD1=1 \
        MDSPRTOP=1 MDROUND=1 MDSREFUSE=1 GLOWMASK=1 MDBATCH=24 [R60TIGHT=1]

    MAME 68K per vint, level 1       base       R60TIGHT
      r60_push                       2,512       1,676     -33%
      shim total                     4,559       3,590     -21%
      frame wait                     3,366       4,055
      executed                      12,369      12,097

    ares-headless, coined path, flips per 100 frames, 1600 frames
      vi59    32 100 57 100 77 77 98 13 50 50 46 47 50 50 50 50   947
      base    32 100 57 100 77 77 98 13 50 50 46 47 50 50 50 50   947
      tight   34 100 60 100 77 77 97 19 50 47 46 50 50 50 50 50   957

Base reproduces vi59 exactly, so the recipe is right. The tight rom is
+10 flips, inside LOOP28 87's noise. **That is the expected shape**: the
68000's freed time went to its frame wait, not to the display, because
on this window the 68000 is not what paces the flip. Entry 88 said so:
the 14% margin is a margin, not the binding constraint.

**What it is worth in the 60 Hz arithmetic** (PLAN-68K-BUDGET): the
shim runs once a vint at 60 Hz, so -969 instructions a vint against a
gap of 1,898 leaves ~930. Cycles are better than instructions here (the
asm is 15 cycles an instruction against the C loop's 9): about 4,000 of
the 19,135-cycle gap by the 68000 timing tables, not measured.

**Not done, and where the rest is:** the rotor (430, a 27-instruction
body per visited block) and the mask walk (330) are the next two, and
neither is a copy — both change what the packet carries if written
wrong. The record scan and popcount are 170 together. Halving r60_push
from here needs one of the two big ones.

Rebuilt on HEAD 1ed642b after the builder made vi62b the line (MDBATCHOFF
24), same coined path:

      vi62b             32 100  59 100  67  67  94  15  51  49  41  46  47  50  50  49   917
      vi62b+R60TIGHT    34 100  57 100  68  67  97  17  50  48  44  50  50  50  50  50   932

`rom/night/r60tight1.32x` (md5 see LOOP-DECOMPILE 102) is vi62b's line + R60TIGHT=1, staged in
the tree and NOT pushed to the rig — Mike is on vi59's play pass. Flag
off by default; nothing in the shipping line changes until it is turned
on.

---------------------------------------------------------------------
## 98. The invisible platform: the round tables were baked from page 0, the level is five pages (2026-09-12)

Mike (r60tight1 shots 083715/083735, level 1, score 6800): the player
stands on a ledge that is not drawn; the background shows through it.
Identified before theorised: the arcade corpus at the same scroll
(ref_009808) draws a raised grey masonry ledge on the FOREGROUND plane
there. Ours draws the trees through it, so the FG cells are refused.

**What the cells are.** Level 1's tilemap unpacked from the rom (entry
10 format) and every cell's set checked against round 0's baked table
(`sh_src/pal_rounds_md.h`, `mdr_s_line[0]`, 24 sets):

    FG pages 0-4 use 21 sets; NOT in the table:
        82: 339 cells   87: 302   88: 32   89: 255   90: 176   91: 122
    BG pages 5-9 use 26 sets; NOT in the table:  94: 138   98: 134

Mapped, those FG cells are the diagonal ramps (rows 10-16 on pages 1, 3
and 4) and the masonry blocks under them (rows 15-25 on pages 2-4) —
the stairs and ledges the player climbs. Page 0 has one such cell.

**Why the table lacks them.** `tools/bake_tilecram.py:105` sweeps the
worst-case viewport as `for c0 in range(64)` over one page's words: it
saw page 0 and page 5 of each round and nothing past them. Under
MDS_REFUSE (m_main.c 2070) a set absent from the round's table is
refused a line and its cells draw as backdrop, which is exactly the
BG-through-the-ledge Mike sees, and it will happen at every ledge from
page 1 on in every round the table under-covers.

**All five rounds, same test** (`tools/scene_sets.py`):

    round 0  FG missing 82 87 88 89 90 91 (1,226 cells)   BG 94 98 (272)
    round 1  FG complete                                   BG 1 (1,111) 2 3 74
    round 2  FG 2 (16)                                     BG 2 (100) 3 101 105
    round 3  FG 2 3 72 74 75 (89)                          BG complete
    round 4  FG 109 (112)                                  BG 1 (155) 101 (170)

Set 2 in small counts is probably the page's decorative fringe; set 1
on round 1's BG at 1,111 cells is not.

**Whether a whole-level table can fit** is a colour question the bake
answers; the set counts say it is close to what page 0 already needed:
the worst 40x28 window anywhere in the level holds 19 FG sets on round 0
(page 0: 15), 25 BG (24), and 5-13 on the other rounds against 4-14. So
feed the bake all five pages of each plane instead of one, and let its
packer say whether the lines still close.

---------------------------------------------------------------------
## 99. Tile RAM has NO in-play writer: the residue for baked name tables is zero (2026-09-12)

PLAN-SINGLE-VINT step 2 asked what rewrites tile RAM (0x400000-0x40FFFF)
after a scene has loaded, because a baked MD name table has to carry
that residue. Census over the whole stream, both operand forms (entry
20's lesson): 26 `lea 0x40xxxx` and 2 immediates, 28 sites, every one
read to its caller.

    SCENE LOAD (once per scene, from the 0x740 sequence)
      0x16BE/0x16DE  unpacker, 40,960 bytes, pages 0-9        [entry 10]
      0x174E         page 10 picture (cutscene)                [92]
      0x170A         page 11 texture (cutscene)                [92]
      0x36B0         clear (16K longs) on the scene/boot paths
                     0x562 0x6A6 0xC48 0xCDA 0x1E90 0x1A41A 0x1B0C4
    ROUND CLEAR (0x1A406, from the main loop at 0xBBC)
      0x1A52C        20x20 block at page 0 row 9 col 74 from 0x1C302;
                     page 5 filled with word 0x0200; a per-round block
                     at page 5 (0x405516) from the table at 0x1C622
    ATTRACT STEPS (0xFFF031 step dispatcher, table 0x26DC)
      0x2580 0x2564 0x2572 0x2552  the 14 upload blocks [85], one group
                     per intro step at 0x1F28, 0x2004, 0x2064, 0x21FA
      0xD12/0xD8E    a framed picture on pages 0/5 (bank 3, table
                     0x21320 indexed by 0xFFF14A), main loop 0xC30 path
    BOOT / SERVICE MODE (0x1A924 <- 0x1B500 test path; 0x1B9xx no caller)
      0x1ACD8 clear; 0x1B76A/0x1B7A4 save/restore 1 KB of page 1 to
      WRAM 0xFFFC00 around an MCU handshake; 0x1B9F6, 0x1BA34 screens
    IN PLAY
      none. The one in-play accessor is the routine at 0x683C, which
      READS tile words at computed offsets (`tst.w (a0,dN.w)` through
      0x6936-0x6A84) -- the ground and wall test, which is why a ledge
      the port does not draw still holds the player up.

The IRQ4 handler writes text RAM (page selects through the pointers at
0xFFF0EC/F0, scroll registers by absolute address) and sprite RAM
[13], never tile RAM. No WRAM-held pointer into tile RAM exists: the
only immediates in the range are 0x258A's base and 0x36B0's.

**So for step 2:** between a scene load and the next event above, the
ten tilemap pages are constant. An MD name-table image per page per
scene is a pure function of rom data (the unpacked map, the tile bank
in 0xFFF095, and the baked set->line table), and the residue the master
would still have to build at runtime is the four event classes, each
of which is itself rom data indexed by a WRAM byte the shim can read
(0xFFF142 round, 0xFFF031 attract step, 0xFFF14A picture, 0xFFF148
cutscene). Nothing is computed by the game at play time that a bake
cannot precompute.

Not verified: that the port's own tile-RAM mirror sees no other writer
(the shim's thunks are the port's, not the game's). Scope: the arcade
program.

---------------------------------------------------------------------
## 100. Lever 2 re-measured: the game side is 8-19% over the arcade, and it is content, not protocol (2026-09-12)

Entry 89's second lever — "our game side costs 9,808 instructions a
game frame where the arcade's costs 8,184, a protocol cost" — was a
ratio of two windows driven by different input scripts. Re-measured
with the arcade profile script's exact input timeline on both machines
(coin 600/800, start 1000, then walk 120-of-240 and punch 8-of-40 from
1200), 20 vints from f2000, level 1, MAME:

                          IRQ4    wait calls   game work    per game frame
    arcade                 20        20         163,690        8,184
    ours (play_32x inputs) 20        10          88,826        8,882
    ours (arcade inputs)   20        10          97,788        9,778

Two corrections to entry 89 fall out:

  1. **Frames are counted by calls to the wait, 0x397E, not by exits of
     its loop** — the arcade exits the loop twice a frame, which is how
     entry 88's parser and my first pass here both double-counted. In
     MAME our game runs ONE game frame per TWO vints (10 calls in 20),
     so "per vint" figures for the game side are half a frame.
  2. **The excess is 8.5% on one input script and 19% on the other, and
     it lands in the object routines** — animate_variant +232 a frame,
     floor_collide +172, sprite_build_and_cull +146, depth banding +127,
     zoom lookup +90 — all of which scale with live objects. At 30 Hz
     logic the game receives the same inputs on different game frames,
     so the two windows hold different object populations; the diff is
     content. The structural extras are small and known: IRQ4 runs
     twice a game frame at about half the arcade's per-call cost (620
     against 1,172 — the sprite upload is patched out), and the colour
     cycler runs per vint (+62).

**So lever 2 is not a lever.** There is no discarded-release cost to
recover in the game code; what LOOP29 182-184 saw is the game pacing at
our frame rate. For PLAN-SINGLE-VINT step 4 the arithmetic at true
60 Hz is

    game frame        8,900-9,800   (content)
    shim, R60TIGHT    3,590
    needed           12,500-13,400  against 12,420 available
                     1-8% over, by instruction count

which the rest of r60_push (rotor ~430, mask walk ~330) covers at the
low end and nearly at the high. The 68000 is not what stands between
the line and one vint; step 2 is.

---------------------------------------------------------------------
## 101. Step 2 is the maps SCAN, not the name tables, and its static half is baked and proven (2026-09-12)

Read `build_maps` / `build_maps_chunk` (sh_src/m_main.c 2534-2660,
3240) before proposing anything. It is not a name-table builder. Per
generation it walks 44x28 cells of BOTH planes (plus the alternate page
set when a rowscroll band selects it) to learn which colour sets the
viewport holds (`tcount`), at which priority (`col_lvl`: FG cat0 2 /
cat1 4, BG 1 / 2) and whether a set sits at two levels (`amb_col`); then
`bm_tail_body` scans text and sprites, does the sticky colour-group
allocation and builds the priority LUT. `tcount` is consumed only as
`!= 0` (2694). So the scan's whole output is: for each set, present or
not, and at which cat bits.

With tile RAM static in play (99), that is a function of the rom
tilemap and the scroll. `tools/bake_setcols.py` bakes, per scene, per
page, per column, every (set, cat, first row, last row) the column
holds:

    scene   entries   bytes   max/column   mean/column
      0      5,578   16,734       17          8.7
      1      3,422   10,266       11          5.3
      2      2,953    8,859        9          4.6
      3      3,085    9,255        7          4.8
      4      3,605   10,815       10          5.6
    all five 18,643   55,929

A viewport is then, per plane: 44 columns x (one or two quadrant row
ranges) of extent compares — about 300-500 — against 2,464 cell reads
with three table updates each. **Proven exact against bm_scan_rows's
own window formula** (pq quadrant pages, vx0, vy0, the yf/29-row case,
the 128-column and 512-row wraps): 4,000 random windows over all five
scenes, 0 mismatches. `sh_src/setcols_md.h` is emitted (not wired to
any build).

What this does NOT cover, so the saving is bounded honestly:

  - `bm_tail_body` (text scan, sprite scan, sticky groups, LUT) stays.
    The 0.44 v/gen ablation (LOOP29 168) removed scan AND tail; the
    split between them is unmeasured. PHASECENSUS can split it.
  - The alternate page set for rowscroll bands: same bake, the pages
    are the same rom pages; the caller passes `pq_a` instead of `pq`.
  - The attract intro pages, the cutscene pages 10/11 and the
    round-clear rewrites (99) are not in the table: those screens fall
    back to the walk, keyed on the same bytes the shim can read.

---------------------------------------------------------------------
## 102. NEGATIVE RESULT — starting the mask walk at the first mismatch buys nothing (2026-09-12)

Step 4's next lever was the changed-block mask walk (~330 a vint, 97).
The pre-scan's `dbne` counter already names the first differing long,
so `r60_ne_longs` now returns the remaining count (0 = equal, the same
value the C loop left in `eq`) and the walk starts there, skipping
longs the scan proved equal. Exact by construction; the cross-check
now compares the COUNT, not the truth value:

    MAME, coined level-1 path, 3,000 frames: 18,231 pre-scans,
      4,376 unequal, 0 disagreements on the remaining count

Measured on HEAD (8c4a708, vi66b's line), same rig as 97:

    MAME per vint         base3    tight4
      r60_push            2,511     1,672      (97's tight: 1,676)
      shim                4,556     3,573
    ares flips /1600        921       936      noise

**-4 instructions a vint.** The changed blocks are 1.6 a vint and their
first difference sits early, so there is nothing to skip. Kept — it is
exact and it costs nothing — but it is not a lever, and the walk is not
worth more asm. What is left of r60_push is the rotor (~430) and it is
the last 68K item on the plan; at 1-8% over budget by instruction count
(100) it is worth doing only once step 2 has crossed the wall.

`rom/night/r60tight1.32x` rebuilt on HEAD (md5 c9735c33), staged, not pushed.

---------------------------------------------------------------------
## 103. The builder's fold-4 question: "the level tilemap is on screen" is the attract step, and the page words cannot say it

NOTES 22 asked for one byte that says the level's tilemap is what is on
screen, because the title and the eye read round 0 and cut 0 like level
1 does. Answered in NOTES 23 from things already on record plus two
reads:

  - the attract step 0xFFF031 bits 2-4 through the table at 0x26DC:
    steps 1/3/5 (0x1ED4) are the demo on the level's tilemap, 0/2/4 are
    picture screens uploaded into the level pages, 6/7 the 10-byte tail.
    Entry 75 had already snapped each value to a screen.
  - 0xFFF026 bit 0 is credited play (set 0x1E62, cleared 0x2CEE/0x2D3C).
  - the eye/intro object's page selector (0x2384-0x2470, an orphan
    routine) writes 0xFFF0F4/F6 from tables 0x2714/0x2724 that are
    word-for-word the level's 0x40F0/0x4100. So the page words at the
    eye equal level 1's; the picture is uploaded INTO pages 0-7.

Correction to entry 92/94's caveat and NOTES 17: 0xFFF148 belongs to the
transformation object only. The eye and the intro are attract steps.

---------------------------------------------------------------------
## 104. Fold 3's scheduling census: the transport does not live in the game's idle; the FM-gate spins do (2026-09-12)

PLAN-SINGLE-VINT fold 3: RELBANK crossed the quantum (wall 0.81, LOOP29
183) and the screen stopped; 184 read it as "the discarded vint was the
slot our transport runs in". Read the shim (md_start.s _vblank /
fmgate_ret / fmgate_partb, md_main.c shim_vblank, fbx_late_blast,
patch_game.py's GAMEGATE and FBXPEND thunks) and measured the game's
side in the level-1 traces. Where every 68000 transport piece runs:

    IRQ4 top (interrupt, every vint)   state word; input; r60_go decision
                                       = V entry in 0xDF-0xE8 AND COMM0
                                       clear AND interrupted PC outside
                                       every gated span; announce; the
                                       consumes at FM=0; the pended blast
                                       (pre-post slot); raise FM, post;
                                       r60_push (WRAM only); the release
                                       token for GAMEGATE's thunk at 0x2AB8
    the game's IRQ4 handler            via the rte trampoline, SR 2700
    fmgate_ret / partb (still IRQ)     the FMGATE-path post with the same
                                       COMM0 and span deferral; then the
                                       tail: r60_blast if FM=0, else
                                       fbx_pend = 1
    game context, gated writers only   the shared spin at 0xFFBE8E: spin
                                       while FM=1; if a packet is pending
                                       and V < 0xC0, fbx_late_blast
    the wait at 0x397E                 nothing of ours (PASSCOUNT/FRAMEDONE
                                       are counters; RELBANK is the consume)

**So no transport work runs in the game's idle.** The idle is the game's
own spin on 0xFFF01C. What RELBANK changes is two things, both measured:

  1. **The post's span test.** The gated spans are 4.9% of the game's
     executed pass in level 1 (base3: 4,326 of 88,690 instructions;
     4.5% on the arcade input script), in three spans: 0x3A9A-0x3AFC
     (the text stride writer, 136 a vint), 0x35CC-0x3950 (50), 0x4D80-
     0x4D98 (30). With the game busy at IRQ4 time that is the share of
     vints whose post defers -- and 183's nopost rose 52 -> 225 per
     4,000 vints = 5.6%. Same number.
  2. **The gate spins.** The game enters a gated writer 5.0 times a vint
     in level 1 (4 call sites: 0x98A, 0xA56, 0x127C, 0x4D7E). In MAME the
     spin never iterates (the SH-2 is ~3x fast, FM is down by then); on
     hardware LOOP29 88 measured the same spin at 8-120 lines. Under
     GAMEGATE those spins sit inside time the game was idling anyway.
     Under RELBANK they are ADDED to the game's pass: five waits on the
     master's window per frame, up to the window's length each. A pass
     of 0.64-0.77 vints (100) plus that is over one vint on the tail --
     which is 2 vints again, and stale packets with it.

**The consequence for the plan:** fold 5 (text writers to the WRAM
mirror, never gated) is the PRECONDITION of fold 3, not a cosmetic
sibling. Only the three spans above fire in level 1; TXTWRAM's earlier
failure (LOOP29 5, sprite pairs, "does not move the numbers") was
measured under GAMEGATE, where the spins cost nothing visible. Its
value exists only under RELBANK, and the two have to be measured
together, on the rig, with BOOTGATECHK=1 (exists: paints why the 68K is
not posting, every vint).

**Not explained by any of this:** 184's rig screen went BLACK where the
ares counters moved 5%. That is the hardware-only divergence class of
231, not a schedule fact, and the rig probe above is the instrument.

**A measurement trap, mine, since entry 87.** MAME prints a
sign-extended .w target as EIGHT hex digits (FFFFBE4C), and every
profile in this log matched six. The gate thunks and the spin (46-61
instructions a vint here) were dropped from every count; the r60
figures are A/B on the same rig and unaffected, entry 90's totals are
low by ~0.4%. `tools/arcade_trace.py` and `round_profile.py` share the
regex; fixed to `{6,8}` in the scripts I ran, not yet in the tools.

---------------------------------------------------------------------
## 105. CORRECTION to 103/NOTES 23: 0xFFF026 bit 0 is the ATTRACT bit; the SEGA card is step 2; the HUD writers read

The builder's fold-4 dump (NOTES 24) read 0xFFF026 bit 0 = 1 through
the whole attract. It does, because I had the bit inverted: the input
routine at 0x13C0 reads the joysticks when the bit is CLEAR and the
tape when it is SET (0x13C8-0x13F6, entry 78's recorder). Credited
play is bit 0 == 0. Entry 103 and note 23 said the opposite; note 27
carries the correction and the rule that needs no claim mix, with the
arcade's own bytes per step from a no-input MAME run (0x08 SEGA card,
players 0; 0x0C/0x14/0x04 demos, players 1; 0x10 eye; 0x00 table).

Also read for NOTES 25: the 0x35CC-0x3950 span is a text-RAM clear
(0x369C), two clears that are not text (0x36B0/0x36C4), the score
writer (0x37D0: 8 words at the record's score pointer, or 0x4100D2 for
the high score), the attract card block (0x380A: 12x6 at 0x41024C),
and the lives/beast icons (0x38AA-0x394E: two rows of up to 8 words at
[record+8]+128). Footprints and call sites in note 27.

Provenance note: 103's "bit 0 = credited play" came from the SET site
(0x1E62, "game start") without reading the consumer. The consumer is
the input routine, and it says attract. Entry 50's rule, again.

---------------------------------------------------------------------
## 106. The cat-1 hole map baked (entry 65's two bits), and all cat-1 is foreground in every round

`tools/bake_cat1hole.py` -> `sh_src/cat1hole.bin` / `.h`: per scene per
cell, 0 / 1 / 2 as entry 65 specified. Counts in NOTES 31. One fact
beyond 65: not one cat-1 cell exists on pages 5-9 in any round, so the
suppress never has to ask which plane a cell is on -- only whether the
FG page cell under the pixel is 1 or 2. Entry 65's counts were one page
(the stale page-7 FG); these are all five FG pages per round.

---------------------------------------------------------------------
## 107. CORRECTION: the unpacker's zero run is n+1 (LOOP29 243); entries 10 and 101 carried the bug, and the c1p95c review

Entry 10 wrote the low-byte pass's zero escape as "a zero length means
a single zero literal", and every unpacker this thread wrote followed
it: n zeros, or one when n is 0. LOOP29 243 checked against live tile
RAM: it is n+1 zeros, always. 10,550 of 20,480 words were shifted by
a column per zero run. Consequences here:

  - entry 101's "exact on 4,000 windows, 0 mismatches" compared the bake
    against a window scan of the SAME unpack -- self-consistent, not
    truth-checked. The builder's check with the corrected unpacker
    against live SDRAM (2,000 windows, 0) is the proof; mine was not.
  - entry 98's set lists per page are qualitative and stand; 231's
    set grid was shifted the same way (243 says so).
  - the builder fixed scene_sets.py, bake_setcols.py and
    bake_cat1hole.py in the tree; the maps in c1p95c and sc95 are from
    the corrected tools.

Entry 50's rule was broken the same way again: an unmarked format
detail taken from one reading and carried into three tools without
ever being compared to the running machine.

**Review of c1p95c (Build A) against its card, for the landing
protocol.** One change compiled: `C1PUNCH=1` -- the master's FG
name-table pass writes the hole class per screen cell (and the tile
index for class 2) from `cat1hole.bin` selected by the state word's
round; the slave's sprite compose, for sprites with pp < 3, skips a
run in a class-1 cell and tests the tile's own pixel in a class-2 cell.
SETCOLS code is in the tree but flag-gated and not in this rom. The
linked map is byte-identical to a fresh bake with the corrected
unpacker (0 of 25,600 bytes differ; the pre-fix map would differ in
3,077). The pp < 3 rule is entry 59's (sprites at pp=2 lose to FG
cat-1, pp=3 win). The cutscene pages 10/11 carry the same rom picture
in every scene, so md_round choosing the map is right there too.
Matches the card. What to look for: the zombies rising behind the
ground, and the player's legs through the grass tufts -- the two
opposite cases the per-cell/per-pixel split has to get right at once.

---------------------------------------------------------------------
## 108. 0xFFF095, the tile bank request: its writers, its values per round, and a provenance catch on "per-round" runs (2026-09-13)

Asked by NOTES 37 (2). All five writers and both consumers, from the
program (roms/altbeast/prog68k.asm):

    0x16A8  round start: word from the 6-byte table at 0x1CE2, indexed
            (0xFFF142 & 7) * 6 -> bank.w, script ptr.l
                round 0  1  0x29E00      round 3  2  0x369C0
                round 1  1  0x2EF10      round 4  2  0x3B0E0
                round 2  1  0x324D0      (entries 5+ are not records)
    0x556   reset path (stack + SR init)                         -> 1
    0x632   soft restart, the next-round path (0xBD2 -> 0x62A)   -> 1,
            then 0x16A8 sets the round's own value
    0x1E6C  attract entry (sets 0xFFF031 = 4, 0xFFF026 = 1)      -> 1
    0xC36   the ENDING: reached from 0xBC8 (cmpib #4,0xFFF142;
            bcc 0xC04) at round clear when the round is 4        -> 3

Consumers: the i8751 forwards the byte to the tile bank register
(NOTES.md 99-105; jtcores jts16b_main.v:382-384, tile_bank[2:0] <=
cpu_dout[2:0]), and 0x3966 uses (0xFFF095 & 3) << 10 to pick the second
1 KB of the palette from the blocks at 0x232A0 (entry at line 85 of
this log already had that half). So the byte is the tile bank AND the
palette-bank selector; the ending has its own block 3 of both.

Measured on the arcade (MAME, frame-sampled transitions of the byte
with 0xFFF142 and 0xFFF031): the demos of rounds 3 and 4 run at 2 and
the attract cards between them at 1; credited play in round 0 runs at
1 throughout. For the mask baker: BANK = [1, 1, 1, 2, 2] by round, 3
for the ending only.

**Provenance catch, and it touches earlier entries.** Writing the
round into 0x1848-0x184F (the "DIP round table") changes which round
the ATTRACT DEMOS show; credited play still starts at round 0 (three
runs, 0xFFF026 bit 0 = 0 at f1001 with 0xFFF142 = 0, rounds "forced"
0/3/4). Every "per round" census this thread made through that patch
without a coin (round_profile.py's script has no coin field; the pp=3
census of NOTES 36; entries that say "rounds 0-4 forced by the DIP
table") measured the demo of that round, not play. The demos are the
game's own records, so "pp=3 records exist in every round's map"
stands; "in play" was not established and is now marked as such.
Also: cfg/altbeast.cfg's DSW1 coinage changed (working tree), so one
'Coin 1' pulse no longer credits; four pulses 80 frames apart do.

---------------------------------------------------------------------
## 109. The game's sprite demand per generation and per line, and NEGATIVE: the arcade's sprite chip never runs out of line time on this game (2026-09-13)

Why: Mike's eye on bldHI -- presentation right, "too many sprites on
screen = slowdown". The wall is a run average (1.11); the slowdown is
the heavy frame. Before picking the next slave-side cut I wanted the
shape of the load from the game's own records, not from our counters.

**The census** (MAME, sprite RAM 0x440000, every 4th frame from f1200,
1,051 samples a run; a record's cost = |pitch| x 4 source pixels per
covered row, the drawer's unit; hidden bit word 2 bit 14 honoured):

    scene                 frame px  med / p90 / max     line px max   records med/max
    round 0, credited     14768 / 28368 / 33704            476           9 / 24
    round 1 demo           7084 / 17800 / 28748            468           3 / 16
    round 2 demo          13972 / 23832 / 30200            524           5 / 20
    round 3 demo          10804 / 18928 / 30380            460           5 / 17
    round 4 demo           6992 / 13992 / 22076            380           3 / 8

Peak over median is 2.3x in play; the p90 is 1.9x. A screen is 71,680
pixels, so the heaviest frame composes about half a screen of source
pixels, and the median a fifth. The cost that moves between a light
and a heavy frame is per-PIXEL work; the per-band and per-record
costs (H's cover, the record scan) are the same in both.

**NEGATIVE: no line-budget lever.** jtcores' object pipeline
(srcref/jtcores/cores/s16/hdl/jts16_obj_scan.v, state 0 and ST_DRAW;
jts16_obj_draw.v, the hstart reset) restarts the record walk at every
hstart and cuts any draw in progress, so the silicon has a per-line
budget of one line of 50.35 MHz clocks (about 3,200; pxl_cen = clk/8,
jts16_cen.v) at 4 clocks per 4-pixel word plus the fetch. That is on
the order of 2,000-2,500 source pixels a line. The game's worst line
is 524. The arcade draws every sprite the game posts in every scene
measured; there is no "the arcade dropped it too" subset for us to
skip, and a per-line cap would change nothing. (The exact silicon
fetch rate is not in the RTL's SDRAM model; the conclusion does not
depend on it at a 4-5x margin.)

---------------------------------------------------------------------
## 110. The art's run structure at the heavy window, from the records and the sprite ROM: 18.6K opaque px in 1,107 runs a frame, mean 16.8; a longword run copy is 0.37 of the byte stores (2026-09-13)

For NOTES 40's card (b). The live records over credited play (round
0, my walking script, every 20th frame from f1200, sprite RAM
0x440000) decoded against sh_src/sprites.bin with MAME's row rule
(src/mame/sega/sega16sp.cpp, sega_sys16b_sprite_device::draw): the
row starts at addr + pitch, words are read forward (backward with the
nibbles reversed when word 2 bit 8 is set), pens 0 AND 15 are not
drawn, and the row ends only when a word's LAST nibble is 15. (My
first decoder ended a row at any 15 nibble and found nothing; a 15
inside a word is a transparent pixel, not an end.)

    window                    records  rows   source px  opaque px  runs   mean run
    f2800-3200 (heavy)         18.7     815    25,039     18,573    1,107    16.8
    f1200-5400 (whole run)      9.7     438    15,291     11,036      629    17.5

Cross-check against the builder's 255a at the same window (17-18
records, ~20k pixels, ~1,000 runs of 19.6): same scene within the
scripts' difference.

Where the opaque pixels sit (heavy window; the whole run is within a
point of it, and the whole 1 MB of art reads 90.8 / 76.6 / 48.8 at
8 / 16 / 32):

    in runs >= 4    98.3%      >= 12   86.0%      >= 24   71.9%
    in runs >= 8    91.7%      >= 16   79.9%      >= 32   65.1%

Store count per frame if each run is copied as head bytes + longwords
+ tail bytes: 18,573 byte stores -> 6,909 (0.37) at random alignment,
5,570 (0.30) if the bake emits each run pre-aligned to its x. The
no-carry claim holds by the format: colour = (word 4 low byte) << 4 |
pen (MAME colpri), so a run's base is a multiple of 16 and its pens
are 1-14; adding base * 0x01010101 to four packed pens never carries.

---------------------------------------------------------------------
## 111. A heavy PLAY scene with no input path: the attract demo is a 3-byte-a-frame tape in ROM, and a recorded walk tape makes the first demo a 20-record scene (2026-09-13)

For NOTES 42 (2). Entry 78 found the recorder; this reads the player.

**The tape (0x1366-0x13F6).** Each frame the game reads 0xC41003 (P1),
0xC41007 (P2), 0xC41005 into d0/d1/d5. With 0xFFF026 bit 0 set
(attract) it replaces them from a tape: a0 = long at 0x1834 + ((0xFFF031
& 0x18) >> 1) -- four pointers, one per pair of steps -- plus
0xFFF02A * 3; if 0xFFF15E is set the game WRITES d0/d1/d5 there
instead (the recorder). Bytes are the raw active-low port values
(MAME segas16b.cpp P1: 0x02 button 1, 0x04 button 2, 0x01 button 3,
0x10 down, 0x20 up, 0x40 right, 0x80 left; 0xFF = nothing).

    0x1834   0x3F6B0 (steps 0-1)  0x3E4B0 (2-3)  0x3EDB0 (4-5)  0x3E4B0 (6-7)
    slots    0x3E4B0-0x3EDAF and 0x3EDB0-0x3F6AF are 768 frames each;
             0x3F6B0 runs to the end of the image (794)
    0xFFF02A demo frame counter, zeroed at game start (0x6D0), +1 a
             frame (0x12EC); the demo ends at 698 (0xB08 cmpiw #698)
             or when a player's 0xFFF028/029 bit 5 sets (0xAEA/0xAF4)
    0x1ED4   the demo entry sets BOTH players active (0xFFF028 = 0xFFF029 = 1)
    boot     step order 2 (SEGA card) -> 3 (demo, tape 0x3E4B0) -> 4 (eye)
             -> 5 (demo) -> 7 -> 0 (table) -> 1 (demo) -> ... so the
             rig's "first level-1 demo, 14-28 s" is step 3 = 0x3E4B0

**Measured (MAME, the arcade).** Credited play with no input: the
swarm reaches 21 records / 30-32K source px at frame 980-1280 of the
round -- past the 698 cap. In the demo an idle player dies at ~664.
Raising the cap (0xB0A) and playing a recorded walk-and-attack tape
(Right held, button 1 every 30 frames, recorded from credited play by
reading the three ports each frame, index = 0xFFF02A + 1) with the P2
byte MIRRORED from P1 (both demo players walk) gives, in the step-3
demo:

    demo frame   136   236   336   436   536
    records       20    20    16    20     9
    source kpx    28    25    24    27    19

Two runs identical. Against Mike's heavy window (18.7 records, 25K
source / 18.3K opaque, entry 110) this is the same class or heavier,
2.3-7.3 s after the demo starts, inside the original cap: NO cap
change and NO pointer change are needed, only the 2,304 tape bytes at
0x3E4B0 (tools/tapes/altbeast_walk_p2mirror.hex). The same tape on
steps 5 and 1 plays differently (the game's state differs per step)
and ends early; step 3 is the one the rig's slot lands on.

Also for NOTES 42 (1): at the heavy window the slave's budget is
18,270 ON-SCREEN opaque pixels a generation (sprite x = raw - 184;
only 20 of 393 records sit left of the screen, none right), 25,039
source pixels walked, 1,107 runs, 815 rows, 18.7 records.

---------------------------------------------------------------------
## 112. For NOTES 44: where the protocol can hold the presented rate independent of load, and the four 68K-side counts that tell which (2026-09-13)

Read from md_main.c (the IRQ4 shim) and m_main.c (the master's
V-ISR), not measured. The chain per presented frame:

    68K IRQ4 (line 223)  announce -> raise -> POST COMM0 = 0x2020 -> push
                         the packet in the FM=0 window -> publish -> consume
    master V-ISR         on the post: FLIP if a fresh blit exists and the
                         post is inside the vblank edge, hand the FB back,
                         echo TP_ECHO_OK on COMM4; else echo TP_ECHO_NO.
                         Declines are three (CEN[21..23]): past the vblank
                         edge / nothing drawn (no finished chain) / nothing
                         shipped
    68K                  waits for the echo (the flip-hold tail); GAMEGATE
                         releases the game on a flip, or after
                         GAMEGATE_MAXWAIT vints (0xFFA0F6 releases,
                         0xFFA0F4 fallbacks)

So a presented frame is an OK echo, and 18 OK per 64 vints means 46
declines (or missing posts). Two mechanisms give a load-independent
18, and they are told apart by the decline reason:

  H1 (slave/master side at hardware memory prices): ares steps one
     clock an instruction; on the FPGA the SH-2 chain pays SDRAM and
     cart waits and the FB write floor (0.15 us a byte: a full 8bpp
     pass is ~0.65 vint of FB writes alone). The FIXED part of the
     chain -- clear, the blit of every strip, restore_pages after each
     flip, cap_page copies, the maps drain's cart tables -- can be 2-3
     vints there while the sprite part is small, so the chain finishes
     every 3rd or 4th vint whatever the load. Signature: declines are
     "nothing drawn".
  H2 (68K side phase): the post lands past the vblank edge on hardware
     (the pass + the game's own FB-hole writes spinning on FM + the
     handler), the ISR declines, and the phase re-rolls into a 3-4
     vint cycle. Signature: declines are "past the edge"; fallbacks
     (0xFFA0F4) high.

The four values for the rig's channel, per 64 vints, all on the 68K:
  1. OK echoes            (= presented; ties the channel to BOOTFLIPRATE)
  2. NO echoes, edge      needs the master to put the reason in the
  3. NO echoes, no-draw    echo word (0xF1F0 | reason) -- one line each side
  4. GAMEGATE fallbacks   0xFFA0F4
If the master cannot tag the echo, replace 2/3 by "NO echoes" and the
count of posts whose HV line (0xC00008 at the write) is past 224.

---------------------------------------------------------------------
## 113. For NOTES 46: the lever is the master's pre-flip path inside vblank at hardware prices, and the four stamps that say which stage of it (2026-09-13)

The rig's split (NOTES 46): ~40 of 64 posts declined at the edge guard,
nothing-drawn 0, the post itself inside vblank on 62-63 of 64. So the
decline is not "the post came late in the frame"; it is that the
master's flip_span (m_main.c 7349) reaches its FBCTL write more than
1650 FRT ticks (~36 lines) after the vblank ISR entry. Between entry
and the guard, in order (flip_span's own stages):

    post wait          the master waits for the 68K's 0x2020 (CEN[52])
    palette drain      VBS(1)
    truth drain        cap_drain(DRAIN_CUT / 13)   VBS(3)   "25.0 lines at 9.23"
    slave capture      spin on SYNC[6] (vbs_t2[1]); text_capture() fallback
    the guard          (frt - visr_t0) > 1650 -> decline, echo F1F1

Every one of those is FB or SDRAM traffic that ares prices at one
clock an instruction; the guard was tuned on ares (1748 -> 1650 for
ares' immediate FS latch). On the FPGA the latch is VBLK-gated
(srcref/S32X_MiSTer rtl/32X/VDP.sv:400, `if (VBLK ...) FS <= FBCR.FS`),
so a write anywhere inside vblank is clean there and a write after
vblank is deferred by the silicon, not torn.

Three levers, one per stage, and the stamps decide:
  L1 post late in vblank -> the 68K posts at IRQ4 entry, before it
     stages the push (the post is a COMM write; the push follows the
     echo anyway).
  L2 drains long -> the truth drain leaves the pre-flip path (drain the
     pages dirtied THIS vint only, or after the flip for pages the game
     did not touch; LOOP27 12's split (a)/(b) is the correctness
     question there).
  L3 slave capture wait long -> the slave's capture at hardware prices;
     text_capture on the master instead, or the capture moved earlier.
  L0 (free, marginal, hardware only) guard 1650 -> 1748: the RTL says
     the full vblank is clean on the FPGA; buys only the flips in the
     last two lines.

The measurement: four FRT stamps per capture, 64-tick steps, on the
rig: (1) post seen, (2) after the truth drain, (3) after the slave
capture wait, (4) at the guard -- the ISR's timeline on hardware. The
stage that carries the excess over 1650 names the lever.

FLIP_DEFER is not the lever as built: LOOP27 12 showed its commit at the
ISR top captures at FM=0 and wedges the game; the FBCTL write alone is
safe there, the FB traffic around it is not.

---------------------------------------------------------------------
## 114. For NOTES 48: what the 68K does before its post, why the FBCTL write cannot simply wait on a push signal, and the design that takes the traffic out of vblank (2026-09-13)

**The premise to correct.** r60_push is the WRAM build and runs AFTER
the post (md_main.c 3653-3654: post B, then r60_push "WRAM only; the
window runs under it"). What precedes the post at IRQ4 entry, all of
it framebuffer traffic at FM=0 (3396-3420, 3637-3647):

    md_consume(0x851A00)   md_pkt A: the tile batch, FB-sourced VDP DMA
    md_consume(0x85E800)   md_pkt B
    mdspr_consume / mdspr_upload_pump
    if (fbx_pend) r60_blast(1)   last vint's staged packet, up to R60_ARM
                                 = 936 words into the FB hole

On the FPGA the batch DMA reads the FB at the adapter's rate (~14
lines per 280 words, BUSES.md) and the blast writes it at the FB
floor; 50-70 lines is those, not the build. Then the 68K raises FM and
posts; only then can the master's drains run (they need FM=1: LOOP27
12), then the flip. FM is the mutex on the framebuffer and every
holder's turn is inside vblank -- that is the chain, and on the FPGA
it is longer than vblank.

**Why "FBCTL waits for push-done" does not work.** The traffic must
touch the CURRENT back bank: the batch the master wrote into it, the
hole the master harvests from it. After the FBCTL write that bank is
the front bank, which neither CPU can read or write. So the 68K's
traffic is bound to precede the flip whatever signals it; a push-done
wait would still put 50-70 lines ahead of the FBCTL write. The truth
drain is bound the same way (it reads the game's pages from the bank
about to become front) and cannot move behind the write either.

**The design: move the 68K's traffic to BEFORE IRQ4, in game context.**
The game's pass ends at the 0x397E spin, usually before IRQ4 (147-185
lines of work; FRAMEDONE marks it, COMM10 bit 15), and from the SH-2
span's end (~190) to 223 FM is 0 for the game's own hole writes. In
that slot:
  1. the packet blast: FBX_PEND already has the late-blast vector in
     the generated gate spin (fbx_late_blast via 0xFFA0F8). Make it the
     ONLY blast path: never blast at IRQ4 top.
  2. the batch: the 68K copies md_pkt A/B from the FB into WRAM in the
     spin (the FB read at the adapter rate, but outside vblank), and at
     IRQ4 DMAs WRAM -> VRAM inside vblank. A WRAM-sourced DMA needs no
     FM, so it runs AFTER the post, under the master's drains.
Then IRQ4 is: post (entry + a few lines) -> r60_push in WRAM -> DMA
from WRAM -> wait echo. The master's pre-flip path is the drains
alone, ~22 lines on the rig (1,000 ticks against the 1,650 guard).
Heavy passes that reach IRQ4 with no idle fall back to today's order
and decline as today; the stamps will show the share.

**Measure first (one capture):** the 68K tail split from the existing
HV stamps 0xFFA080/0xFFA086 around the consumes, plus two around the
pending blast: lines for batch A, batch B, pump, blast on the rig; and
the idle lines between FRAMEDONE and IRQ4 entry, which is the slot's
size on hardware.

---------------------------------------------------------------------
## 115. The map's tile demand per vint, from the arcade: 0-2 new codes a frame in steady state, bursts only at cuts. Batch B's 24 tiles a vint are not the map's need (2026-09-13)

For NOTES 50. The visible window of both planes (41 x 30 cells each,
page quadrants and scroll from the latched registers at text RAM
0xE80/0xE90/0xE98 + 2*plane, NOTES.md item 3), every frame, on MAME's
arcade:

                         distinct codes   NEW codes vs      NEW vs the last   name cells
                         on screen        the previous frame 64 frames         changed/frame
    attract demo (701 f) mean 505, max 607   mean 1.4, p90 0, max 378   mean 1.4, p90 0   mean 12.9, p90 0
    credited play (1901) mean 564, max 591   mean 0.4, p90 0, max 561   mean 0.4, p90 0   mean 1.9,  p90 0

946 distinct codes over the demo run, 672 over the play run. The
maxima are scene cuts (a whole window at once: 378 / 561 codes, 2,520
cells). Between cuts the map asks for 0-2 tiles a vint, and the p90 is
zero: most vints need no tile at all. So a k2 batch carrying 24 tiles
on the vints that carry it (NOTES 50: 21-30 lines of the rig's tail)
is not the map's demand. Either the batch is carried on few vints
(the seconds after each cut, 1,120 tiles at 24 a vint = 47 vints), or
it is churn: evictions and set relocations re-shipping resident tiles
(mdalloc_ctr [3], [10], [12], [14]). Which it is decides the cut.

The bank question (NOTES 50): packet B is the k2 packet in the CURRENT
back bank (m_main.c 13586: k1 -> A, k2 -> B), and the master cannot
write the other bank. Beyond the bank: the MD planes' names and tiles
must land in the same vblank as the 32X flip, or the tile planes move
one frame before the sprite layer -- a one-frame skew between ground
and feet, exactly the class of seam Mike's eye rejects. So the DMA
stays in the flip's vblank; the batch's SIZE is the lever.

---------------------------------------------------------------------
## 116. For NOTES 52: the cell chunk's 56-72 words on a static map are the protocol's own weight (the loss-backstop row, headers, edge pairs), and the truth drain has nothing of the game's to drain in play (2026-09-13)

**The chunk, from the emitter (m_main.c ~14840-14960, NT_WRAP):** each
window visits 7 rows of one plane (md_phase: 8 phases cover 28 rows x
2 planes), mirror-diffs each row against md_dbg_nt and ships only
changed cells as spans -- and, every window, forces ONE rotating row
back to full-ship as a LOSS BACKSTOP ("~+3 lines/window": 40 cells, one
40-word span). Header 8 + 7 row headers x 2 + the backstop row 40 +
EDGE42 pairs (2 words a row when sent) = 56-72 words. That is the
measured chunk exactly (NOTES 52: 56-72 words, 177 of 200 vints). The
map itself changes nothing on those vints: entry 115 read p90 = 0 new
cells a frame on the arcade, and entry 99 found no in-play tile-RAM
writer (the level's pages are unpacked once per scene). So on the rig
the 21-30 lines of batch B are: one 40-word FB-sourced DMA and its
six-register setup, ~30 header/edge words read by the 68K through
the window, and the row walk -- none of it for the game.

**The drain, from the ISR (7538-7556):** pg_pending |= COMM10 & 0x1FFF,
where COMM10's low 13 bits are the DIRTY-PAGE mask the tile-RAM write
thunks build at 0xFFB9FE (patch_game.py ~1226; BUSES.md's COMM10 line
said "palette-dirty" and is corrected). In play that mask is zero --
no writer -- so cap_drain should copy no page; what remains in stage
2 is cram_flush_pen (VBS(1)), the page merge (VBS(2)) and PG_STICKY's
watch (pages stay pending 12 cycles after any mark). Which of those is
the rig's ~1,000 ticks a vint is one capture away (the VBS stamps
exist).

**Arithmetic for the guard (1,650 ticks = ~36 lines) on a mid-pass
vint, where note 49's move cannot help:** post = the 68K tail after
IRQ4 entry: batch A 1-9 + batch B + pump 2-6 + blast 0-6; then stage 2.
With batch B at 21-30 and stage 2 at 22 lines the sum is ~50-70 and
every such vint declines. With the chunk reduced to its map demand
(a header, no backstop row, no empty rows) batch B is ~2-4 lines and
the post lands at ~8-20 lines; stage 2 must then be under ~16 lines
for the flip to make the guard. Both cuts are needed; neither alone.

---------------------------------------------------------------------
## 117. For NOTES 56: the game changes 8-10 palette entries a frame (p90 17), the flush's loop reads a 32X register PER ENTRY for a PEN that cannot drop in vblank, and the flush and its callers are CART-ROM resident (2026-09-13)

**1. The game's own palette demand, on the arcade.** Palette RAM
0x840000-0x840FFF (2,048 words) compared entry by entry at every
frame boundary, MAME:

    scene                 entries changed a frame: mean / p50 / p90 / max
    attract demo (701 f)      9.8 / 8 / 17 / 128
    credited play (1901 f)    9.7 / 8 / 17 /  48

That is the colour cyclers (entry 93: three scripts, 8 colours a step,
palettes 19-21) plus the odd fade; the maxima are cuts. Against NOTES
54's count of OUR dirty CRAM entries -- 15-31 a frame in the demo,
36-82 a frame and 164 a generation at the zombie row -- the port
flushes 2-10x what the game changed. The excess is the pen repaint's
own churn (PEN_HOLD / PEN_REPAINT), not the game. (Instrument caveat:
a MAME write tap on that range counted 0 writes while the polled
compare saw changes; only the polled figure is used.)

**2. The flush reads a 32X register once per entry, for a bit the RTL
pins high through the whole of vblank.** cram_flush_pen's burst is
`do { ...cram[i] = ...; } while (d && (MARS_VDP_FBCTL & 0x2000));` --
one uncached 32X register READ per dirty entry to re-test PEN. Per the
FPGA's own RTL (srcref/S32X_MiSTer rtl/32X/VDP.sv:400-406):

    if (H_CNT == 9'h157+3-1 || VBLK || !MODE[0]) PEN <= 1;
    else if (H_CNT == 9'h017-1)                 PEN <= 0;

VBLK forces PEN high every DOT_CE, so inside vblank PEN cannot drop
and every one of those reads is known-true before it is issued. A
write is posted through the SH-2's write buffer; a read is a blocking
round trip. ares prices both at one clock (memory: ares charges
instructions only), the FPGA does not.

**3. The flush and its callers run from CART ROM.** From rom/s16.lst:

    _blit_half   0x060324a8    SDRAM (.ramtext)
    _cap_drain   0x06032358    SDRAM
    _m_main      0x06032d90    SDRAM
    _visr_vbi    0x020462cc    CART ROM
    _flip_span   0x02045df0    CART ROM       (cram_flush_pen inlines here)

m_main.c declares visr_vbi and flip_span without RAMCODE. This repo
has already measured this exact cost once: md_consume "ran from cart
ROM under the master's compose traffic -- a 4-6x fetch-stall on every
instruction" (md_main.c ~515). It is the shape of 900 ticks on the rig
against 300 on ares for a 15-31 iteration loop.

.ramtext in the tree's .lst is 0x6950 = 26,960 bytes against the
28,672 ceiling entry 246 names -- ~1,700 bytes free, and the flush's
loop is ~100 of them. flip_span entire (592 lines of C) does not fit;
the flush alone does, as a noinline RAMCODE function (250b's rule: a
static given a placement is still inlined under LTO without noinline).

---------------------------------------------------------------------
## 118. For NOTES 59: the text capture cannot leave FM=1 (it is an FM rule, not layer sync) -- and it carries a MEDIAN OF ZERO changed words; the game touches one 4-row group on 4-11% of frames (2026-09-13)

**1. The move is impossible, and for a harder reason than the batch's.**
The batch had to stay in vblank for the bank and for layer sync (115).
The text capture is pinned by FM: the game's text staging lives in the
framebuffer hole, and a master FB READ at FM=0 returns garbage --
FM_TEST convicted it, 1507 mismatch against 176 match (m_main.c
5276-5277, the same wall that killed FLIP_DEFER in LOOP27 12). The 68K
raises FM one instruction before it posts, so FM=1 begins at the post
and the capture cannot precede it by any amount. There is no FM=0
slot for it; the question has no version that works.

**2. It also has almost nothing to carry.** The capture copies 928
longs = 3,712 bytes = the 29 text rows at 0x410000 (rows 29-31 are the
scroll/page registers and are already outside it). Measured on the
arcade, those 29 rows compared word by word at every frame boundary,
grouped in the 4-row groups TEXTCAP_MASK uses:

    scene                 groups changed a frame        words changed a frame
    attract (700 f)       mean 0.04, p50 0, p90 0, max 5    mean 0.63, p50 0, max 107
    credited play (1500)  mean 0.11, p50 0, p90 0, max 1    mean 0.29, p50 0, max  13

    frames with ANY change: attract 26 of 700 (3.7%), play 159 of 1500 (10.6%)
    per group (play): g0 27, g2 128, g6 4; g1/g3/g4/g5 never
    per group (attract): g4 14, g6 5, g1/g2 2, g0 1, g5 2; g3 never

On 89-96% of vints the game changed NO text word at all, and when it
did it was ONE group. (Rows 29-31 change on 136-169 frames -- those are
the IRQ4 scroll-register writes, outside the capture, and they are why
a 32-row version of this census reads g7 busy.)

So the pre-flip path spends 830 FRT ticks on the FPGA copying 3,712
bytes whose median change is zero words. TEXTCAP_MASK (LOOP29 147,
already in the tree with its 68K half in patch_game.py) is not a
halving: it is ~0 ticks on nine vints in ten and ~1/8th of 830 on the
rest.

---------------------------------------------------------------------
## 119. NEGATIVE: the rig's 86 named-build captures show NO corrosion, and both things that looked wrong in them are the arcade's own art (2026-09-13)

Mike, on bldJ: "juttery and had some bitmap corrosion", seen both
after a scene change and in steady play, "changes every single probe
and build", and "you have ALL the screenshots on the MISTER". Pulled
every capture the rig holds for a NAMED build -- bldB 5, bldC 9,
bldD 9, bldE 9, bldF 9, bldH 15, bldHI 15, bldJ 15 = 86 -- and
compared them against ref_arcade.

**Two suspects, both cleared:**

  1. The ALTERED BEAST logo is RED in some captures and WHITE in
     others (bldHI 015641 red, bldJ 030647 white). The ARCADE does the
     same: sampling the logo box over ref_000700-000975, the 1,564
     logo pixels are all-red or all-white in blocks and flip between
     them. That is the colour cycler (entry 93, palettes 19-21).
  2. Two hard-edged panels of black/white art with green streaks in
     the graveyard wall (bldJ 030647, x 97-167 y 117-167) read as
     corruption. They are carved stone reliefs with moss. Matched
     against the corpus by mean absolute difference over that exact
     region, the closest arcade frame is ref_001790 at 22/255 -- the
     same two panels, the same green, the same black shadow detail.

**And an automatic check finds nothing anywhere.** Tile-aligned cells
in the graveyard band (y 80-176, 480 cells a frame) holding BOTH a
near-black and a near-white pixel -- which stone and grass never do,
and which garbage art always does: 0 cells in all 28 arcade frames
sampled, and 0 cells in every one of the 86 captures, every build.
The only all-black frames are the demo-to-title fade the builder
already logged.

**So the corrosion is not in any capture we have, and the reason is
structural: every rig capture is ATTRACT.** The launch API plus the
screenshot FIFO can only reach attract mode (memory:
mister-rig-self-service), and the tape probe (111) makes the attract
carry a heavy PLAY-LIKE scene but it is still the game's own recorded
demo, not Mike's hands. Nothing in the corpus covers a credited game.

**What is needed:** one capture during Mike's play. The rig takes it
on command -- `ssh root@mister.office.local "echo screenshot >
/dev/MiSTer_cmd"` -- so the decompile thread can fire it while he
plays and fetch it by name. Until then the corrosion has no evidence
and must not be attributed to any card; cards H/I/J each measured a
demo-aligned pixel diff of zero against the build below them.

---------------------------------------------------------------------
## 120. Card L's arithmetic: the rig's wall has MOVED from the flip path to the generation, and every compose card now has an instrument it never had (2026-09-13)

Card L (TEXTCAPMASK on the line, NOTES 60) reads 27 presented per 64
vints on the rig against the line's 18, with the no-capture ablation
at 28-34. So the masked capture recovers nearly all of the ablation's
headroom and the text copy is off the critical path, as entry 118's
census said it would be.

**What 27 means, and it is not 'still broken'.** Per presented frame:

    line      18/64 = 3.56 vints        ares wall 1.09
    card L    27/64 = 2.37 vints        ares wall 1.07
    ablation  31/64 = 2.06 vints

The flip path's own excess was 150-530 ticks (NOTES 59) and the copy
was 830; with the copy gone the ISR reaches the guard at ~1,100
against 1,650 and the guard should stop declining. It largely has.
What is left, 2.37 vints a frame, is the GENERATION -- and on the FPGA
that is ~2.2x the ares wall of 1.07, which is the same ratio the FPGA
charges everywhere else it prices memory and ares does not (the text
copy 830 vs 450, the CRAM flush 900 vs 300, cart fetch 4-6x).

**The strategic consequence, and it inverts the ordering this thread
has been working to.** Cards H, I and J each cut the slave's compose
and NONE of them moved the rig, because the rig's wall was the flip
path and the compose ran in its shadow (LOOP29 257, NOTES 44-46).
That is no longer true. With the flip path clear the rig's wall IS the
compose, so those cards -- and every future compose card -- now have a
hardware instrument for the first time in this arc.

**The cheap test, two roms, no new code:** apply card L's flag to bldB
and to bldJ and read the rig's rate on both. bldB-plus-L against
bldJ-plus-L is the whole H+I+J stack measured on hardware. If the rig
separates them, the compose cards were real all along and only the
instrument was blind; if it does not, the FPGA's compose cost is
somewhere the ares census does not look.

**And one census, already built:** re-run ECHOCENSUS on card L. Before,
the declines were ~40 of 64 at the vblank edge and 0 for 'nothing
drawn' (NOTES 46). If the generation is now the wall, 'nothing drawn'
is what the remaining ~37 declines must read. If they still read
'past the edge', the guard is closing for a second reason and the
post's own ~900 ticks is next.

---------------------------------------------------------------------
## 121. NO, IT WAS NEVER RULED OUT, AND THE MISSING MEASUREMENT SAYS IT WORKS: the MD plane needs at most 30 QUANTISED COLOURS, and every scene packs into 3 lines x 15 with each set inside one line (2026-09-13)

Mike: "earlier you mentioned rebaking graphics with fewer palette
layers -- did we ever rule that out?" No. LOOP11 proposed the
colour-level pack; LOOP18 doubted it because the 36-colour figure was
an ATTRACT number and named the missing measurement -- "the distinct
MD-quantised colour count of the visible BG window during GAMEPLAY";
LOOP28 553 then measured SETS (mean 5.72, max 45, over 6 on 29% of
vints) and parked the whole question as "real, and not the big lever."
The colour count in gameplay was never taken. Here it is.

**Method.** MAME, arcade. Both tile planes' visible windows walked
from the latched registers (page quadrants 0xE80+2*which, vy 0xE90+,
vx 0xE98+), every cell's colour set = (word >> 6) & 0x7F, each set's
8 palette words at 0x840000 + (set*8+pen)*2, each word quantised to
the MD's 3 bits a channel (r = ((v&0xF)<<1)|((v>>12)&1), then >> 2;
g at bit 4 / 13, b at bit 8 / 14). FG pen 0 is transparent and is
excluded; BG pen 0 is opaque (jts16_prio.v:87) and is counted.

    scene                  sets BG   sets FG   MD-quantised COLOURS (union, max)
    round 0, credited play  14.7/16    7.3/8    23
    round 0 demo            15.3/16    7.6/8    23
    round 1 demo             7.9/8     5.0/5    28
    round 2 demo             6.6/7     6.3/10   24
    round 3 demo             6.9/7     4.9/5    24
    round 4 demo             6.5/10    6.4/10   30

**So the squeeze the allocator fights does not exist at COLOUR level.**
Up to 24 distinct colour SETS are on screen at once (round 0: 16 BG +
8 FG) against six MD slots -- which is why the LRU thrashes and why
LOOP29 231's blank-slot signature keeps returning -- but those 24 sets
draw at most 23 distinct MD colours, and no round exceeds 30. The
shipping MDP_LINES = 3 gives 45 usable pens.

**And it PACKS, which is the part nobody checked.** A tile draws from
ONE CRAM line, so the count is not enough: every set's colours must
fit inside a single line. Partitioning each scene's sets into 3 bins
with a per-bin colour union <= 15 (exhaustive with memo on the bin
states, 20 distinct scenes across the five rounds):

    scenes packed into 3 x 15 with every set inside one line   20 of 20
    worst per-line occupancy                                    8 / 14 / 15
    round 1 and round 4 do NOT fit 2 lines; all five fit 3; 4 lines is slack

Colours are duplicated across lines where a set needs them -- that is
what the spare 45 - 30 buys, and it is a bake-time choice, not a
runtime one.

**What this makes possible.** A per-scene baked line assignment means
mdp_assign_set never evicts, because there is nothing to evict: every
set on screen has a home line for the whole scene. LOOP29 231's black
tile sets, card L's black tiger statue and gravestones, the wrong
logo set, the gravestone flicker and the "stolen pair shows the
stealer's colours" family all share one mechanism -- eviction and
reassignment under pressure -- and this removes the pressure by
construction rather than tuning the LRU.

**Caveats, honestly.** (1) The 3-bit quantisation merges colours the
arcade separates; the bake must check that no merge lands inside one
tile's own gradient (the sky banding LOOP11 flagged). (2) These are
the five rounds' main scenes at 25-frame sampling, not cutscenes, the
boss frames or the ending. (3) Sets whose palette the CYCLERS rewrite
(19-21, entry 93) change colours within a scene: their LINE can be
fixed and their pens repainted in place, which is what the cyclers
already do on the arcade -- but the pack must reserve the pens they
cycle THROUGH, not just the ones they hold at the sample.

---------------------------------------------------------------------
## 122. The text-RAM writer census for fold 5: 10 direct writes (all registers), 0 reads, 61 pointer loads -- and TWO sites that stash a text pointer into an object field (2026-09-13)

NOTES 63a asked whether the seven FMGATE entry points are the whole
glyph-writing set. They are not. Census of 0x410000-0x410FFF over the
whole program, both operand forms:

    direct writes  10  all of them the page/scroll REGISTERS (0x410E80/E82
                       at 0x2AD2-0x2AFC, 0x1B0D6/0x1BA42) plus 0x410002
                       twice in service mode (0x1B77A/0x1B798)
    direct reads    0
    pointer loads  61  6 reach a listed gate; 27 write through their own
                       loop; the rest pass the pointer to a subroutine
    pointer into d0  6  service/boot (0x1B1FE onward)

Own-loop writers that run in play or attract and are NOT in the list:
0x057E (inline `moveb %a1@+,%d0; movew %d0,%a0@+`), 0x1608 -> 0x162E,
0x3766 -> 0x37D0 (the score writer, entry 105), 0x42D8 -> 0x4212,
0x4554/4568/457C/4590/45A4 -> 0x469C, 0x4D12/4D1E -> 0x4D3A, plus
0x0BD8, 0x0DBE, 0x14E8, 0x1592, 0x3818, 0x45BC, 0x45C6, 0x9052,
0x90D8, 0x17BC8, 0x1845E.

**The class that breaks a static rebase.** Two sites store a text-RAM
ADDRESS into an object record and never write through it themselves:

    0x56DC  lea 0x4104B8,%a1 ; movel %a1,%a0@(36) ; rts
    0x64CA  movel #0x41033C,%fp@(108)

The same fields are written from 0x546E, 0x57B2, 0x58AC, 0x58C2,
0x58D8, 0x594A, 0x5BD4, 0x5DA8, 0x5DBE and read back at 0x584A. The
HUD/score destination is therefore DATA in work RAM. Rebasing code
operands cannot reach it, and a missed one writes where nothing reads
-- a silent loss, which is why NOTES 65 recommends masking at the
consumer (`dst = 0xFF8000 | (dst & 0xFFF)`) instead of rewriting each
stash.

**No reader.** Zero direct reads, and every glyph loop reads its
SOURCE and writes its text pointer. m_main.c 162-178 justifies the
post-flip text restore with "the game's own read-modify-writes see
coherent RAM"; that premise is TILE RAM's (entry 99's collision
`tst.w`), not text's, and it does not hold here.

**Instrument note:** MAME `install_write_tap`/`install_read_tap` over
0x410000-0x410DFF counted ZERO accesses in play and in attract while
a polled comparison of the same region saw changes every few frames,
and the same happened on 0x840000 for the palette (entry 117). Taps do
not see these regions on this driver. Poll, or read the program.

---------------------------------------------------------------------
## 123. Mike's chevron defect on bldO, diagnosed from the arcade: the transform's chevron is a SEVEN-SHADE BLUE RAMP rotating one step a frame, and the sets that carry it are in no round's static table (2026-09-13)

Mike on bldO: "the chevron in the beast transformation should flash two
colours behind the flame. Instead we see a single blue." Also missing
random black background tiles, grass shimmer, and slowdown on
sprite-heavy scenes (gravestones, Neff throwing heads). The frame rate
he describes as "crawling towards arcade", which is the win.

**What the arcade actually does.** The attract's transformation screen
runs at 0xFFF148 != 0 for 110 frames (f1054-1164 and again f4439-4549
of a no-coin run). Snapshots every 8 frames through it, sampling the
chevron band at x 20-120, y 20-70:

    frame   the three commonest chevron colours (R,G,B)
    0000    (0,0,206) (0,0,173) (0,0,189)
    0001    (0,0,255) (0,0,156) (0,0,239)
    0002    (0,0,222) (0,0,239) (0,0,206)
    0003    (0,0,189) (0,0,206) (0,0,173)
    0004    (0,0,156) (0,0,173) (0,0,255)
    0005    (0,0,239) (0,0,255) (0,0,222)
    0006    (0,0,206) (0,0,222) (0,0,189)
    0007    = 0000 again

**It is a seven-shade PURE-BLUE ramp (blue 156, 173, 189, 206, 222,
239, 255) rotating one position every frame, period 7.** Red and green
are zero throughout. That is what reads as "flashing two colours"
behind the red flame at speed. Not two colours: seven, in motion.

**Which sets carry it.** Sampling palette sets 17-23 across a 4,600
frame run, only THREE change at all:

    set 17, 18, 22, 23   8 distinct colour words each, every one held for
                         the whole run -- static
    set 19               9 distinct words, the blue-green byte walking
                         (4900, 4A00, 4B00, 4C00, 4D00, 4F00)
    sets 20 and 21       the ramp: 305F, 307F, 309F, 30BF... over 100F

So the chevron is sets 19-21, exactly the cycler scripts at 0x1A70E
and 0x1A78E (entry 93), and LOOP29 202's "pages 10 and 11 with sets
19-21" is the same three.

**And here is the port-side fact that matters.** In
`sh_src/pal_rounds_md.h`, `mdr_s_line[round][19..21]` is ZERO for all
five rounds -- the cycled sets are in no round's static table. Under
MDS_REFUSE a zero means the cell is kept on the 32X FRAMEBUFFER layer,
not the MD plane. So the chevron's colours do not come from the baked
lines at all; they come from the palette DELTA pipeline, which is
exactly where `palscene_bake.py` says the transform has to live ("every
word that distinguishes the scene IS the flash animation... its span
rides the delta pipeline").

**Therefore the colour-line bake cannot fix this defect and must not be
sold as fixing it.** A flat single blue means the per-frame delta for
sets 20/21 is not arriving during the cutscene -- dropped, coalesced,
or pinned by a scene detector that resolves the transform to a static
anchor. That is a delta-path question on the builder's side, and it is
a different card from the sets 22-36 coverage gap.

**Not yet reproduced by me:** the random black background tiles and the
grass shimmer. Eight rig captures of bldO across the attract show a
complete graveyard, correct wall reliefs and correct text; the eye
cutscene shows hard black dither around the iris that may or may not be
the arcade's own. Both want a denser capture sweep or Mike's bldJ
comparison, which he declined; neither is diagnosed here and neither
should be attributed to card O without one.

---------------------------------------------------------------------
## 124. The round/area entry the builder asked for: 0xFFF14E is the PROGRESS COUNTER with three writers, and the tape reader is one branch away from playing back CREDITED play (2026-09-13)

**The round entry, and it is clean.** 0xFFF14E is the game's progress
counter, and it has exactly three writers in the whole program:

    0x05DA  clrw 0xFFF14E                        game start (reset to round 0)
    0x065C  moveb 0x1848[(0xFFF031 & 0x18) >> 3] ATTRACT ONLY (guarded by
                                                 btst #0,0xFFF026 at 0x0646)
    0x0BCA  addqb #1,0xFFF14E                    round clear

and 0xFFF142, the ROUND, is DERIVED from it every scene load:

    0x0662  lea 0x1CDA,%a0 ; moveb %a0@(0,0xFFF14E & 7),0xFFF142
    0x1CDA  = 00 01 02 03 04 00 00 00
    0x1694  lea 0x1CE2 ; index by 0xFFF142 & 7 ; -> tile bank .w + the
            round's packed tilemap pointer .l, then unpack (0x16BE/0x16DE)

So a credited game starts wherever 0xFFF14E says. The minimal patch is
at 0x05DA: `clrw 0xFFF14E` (42 78 F1 4E) and `clrw 0xFFF142` (42 78 F1
42) are eight consecutive bytes, and the second is REDUNDANT because
0x0662 recomputes 0xFFF142 from 0xFFF14E on both the attract and the
credited path. Replace both with `move.w #N,0xFFF14E` (31 FC 00 NN F1
4E, six bytes) plus one `nop`. That is the whole feature: one constant,
and the game boots into round N.

**The area is an object field, not a variable.** The camera X reaches
the scroll registers through 0xFFF120 / 0xFFF0E2, and 0x2390-0x23AC
computes those from `%fp@(16)` -- the camera object's own position, in
a work-RAM record. There is no single "area" byte to write. The spawn
script at 0x1D2DC is keyed on camera X (12-byte records, camX first),
so the area IS the camera position, and the cheap way to reach one is
to start the round and walk -- which the tape already does.

**And the tape is one branch from playing back CREDITED play.** Entry
111 read the reader at 0x13C0-0x13F6; the gate is the FIRST
instruction:

    13C0  btst #0,0xFFF026     attract?
    13C6  beqs 0x13F8          NOT attract -> skip the tape, use the live ports
    13E4  tstb 0xFFF15E        recorder armed?
    13EA  write d0,d1,d5 to the tape   (RECORD)
    13F2  read  d0,d1,d5 from the tape (PLAY BACK)

`beqs 0x13F8` at 0x13C6 is two bytes (67 30). NOP it and the tape drives
CREDITED play as well as the attract. Two dependencies come with it:
the tape pointer is chosen by `(0xFFF031 & 0x18) >> 1` from the table
at 0x1834 -- in credited play 0xFFF031 holds whatever the attract left,
so the pointer must be forced -- and the index is 0xFFF02A, which is
zeroed at 0x06D0 at game start and incremented at 0x12EC. 0x12EC's
caller has to run in play for the index to advance; if it does not, the
patch also has to drive the counter.

**The chevron is NOT gameplay-only.** Measured on a no-coin MAME run:
0xFFF148 goes non-zero at f1054-1164 and again f4439-4549, both with
0xFFF031 = 0x0C, which is a DEMO step. The transformation cutscene runs
inside the attract demo, about 17.6 s in, and the rig's first demo
window is 14-28 s after launch. No entry point is needed to reach it.

**Instrument limit found while trying:** the MiSTer screenshot FIFO
rate-limits to roughly one capture every 6 seconds -- eight requests
one second apart returned eight shots spanning 44 seconds. So the rig
cannot capture consecutive frames, and neither a 110-frame cutscene nor
an alternating shimmer can be sampled through it. Dense picture work
has to be ares (one run per frame, LOOP29 257's trap) or a rom-side
capture.

---------------------------------------------------------------------
## 125. CORRECTION to entry 121 and NOTES 62/65: my per-round set lists were sampled during the SCENE LOAD and carried the previous scene's tile RAM. The definitive lists, and the one thing every round's table is missing (2026-09-14)

Mike on bldP: "much better, still black squares, but typically in the
bottom grass sprite layer or background layer; more visible after
screen transitions -- wolf transformation, Neff smoke transform where
the background changes colour, and level 2."

**My census was wrong and card P was built on it.** Entry 121 walked
the visible windows every 25 frames from f450. The round's tilemap is
not loaded until roughly f575, so every sample before that read the
PREVIOUS scene. That is where "round 2 uses sets 22-36" came from --
those are the intro/SEGA-card screen's sets, not round 2's.

**The definitive method and the definitive lists.** Force the round,
wait for the load, then walk EVERY cell of EVERY tilemap page 0-11
(not the visible window, which only shows where the camera happens to
be) and collect every colour set whose tile code is non-zero:

    round 0   31 sets   19, 20, 21 + 74-101
    round 1   13 sets   19, 20, 21 + 64-73
    round 2   15 sets   19, 20, 21 + 102-113
    round 3   12 sets   19, 20, 21 + 64-72
    round 4   19 sets   19, 20, 21 + 96-111

Cross-checked against the visible window at f700: round 2's window
shows 102-113 and no set below 100, round 4's shows 96-111. Written to
`docs/audit/round_sets_definitive.txt`; `docs/audit/mdpen_scene_sets.txt`
is superseded and should not be fed to any baker.

**Against the shipped table (HEAD, after card P):**

    round   true   in table   PHANTOM (in the table, never in the map)   MISSING
      0      31       30      72, 73                                      19, 20, 21
      1      13       15      0, 1, 2, 74, 75                             19, 20, 21
      2      15       26      0, 1, 2, 22-30, 33, 35, 36                  19, 20, 21
      3      12       14      0, 2, 3, 74, 75                             19, 20, 21
      4      19       21      0, 1, 3, 35, 36                             19, 20, 21

**Two findings, and the second is the important one.**

  1. Round 2's table carries ELEVEN phantom sets from my bad census.
     They cost pens in a table the builder measured as having four
     spare, so they may have displaced real art. Rounds 1, 3 and 4 have
     two to five phantoms each, smaller but the same shape.

  2. **SETS 19, 20 AND 21 ARE IN EVERY ROUND'S TILEMAP AND IN NO
     ROUND'S TABLE.** They are the cycler sets -- the chevron plane,
     pages 10 and 11 (entry 93, LOOP29 202, entry 123). A set absent
     from its round's table is refused and renders as backdrop
     (LOOP29 277), so every cell drawn in 19, 20 or 21 is BLACK.

That is one mechanism under both of Mike's remaining complaints. It is
why the black is worst "after screen transitions -- the wolf
transformation, the Neff smoke transform where the background changes
colour": those are exactly the scenes the cycler plane draws. And it
sits with entry 121's own caveat 3, which said a cycled set's line can
be fixed but its pens must be reserved for the colours it cycles
THROUGH. Nobody acted on that, and the sets were left out of the tables
entirely instead.

**What a fix has to respect.** A static pen map cannot hold a rotating
ramp. Sets 20 and 21 walk seven blue shades and set 19 walks six
(entry 123). So these three sets need a LINE with pens reserved for
their whole cycle and the pen VALUES repainted each frame from the
delta pipeline -- not a fixed snapshot, and not exclusion. Seven pens
for the ramp plus set 19's six is inside one 15-pen line if they share,
which the packing run can answer once someone asks it the right
question.

---------------------------------------------------------------------
## 126. The transformation gate, exactly: the chevron plane is pages 10/11 with page selects 0xAAAA/0xBBBB, and 0xFFF148 matches it to the frame. The SH-2 already receives both (2026-09-14)

NOTES 69 asked which byte marks the player object mid-transformation.
The scene does not need a player byte; it has two exact markers and the
SH-2 already gets one of them every vint.

**Measured, arcade, no-coin run, frames 400-5400.** A gate on "any page
quadrant >= 10 in either plane's page-select register":

    CHEV ON   f=1056   fg=AAAA bg=BBBB   0xFFF148 = 1
    CHEV OFF  f=1165   fg=0000 bg=0000   0xFFF148 = 0
    CHEV ON   f=4441   fg=AAAA bg=BBBB   0xFFF148 = 1
    CHEV OFF  f=4550   fg=0000 bg=0000   0xFFF148 = 0

    0xFFF148 over the whole run: value 1 on 220 frames, value 0 on 4,781

220 = 110 + 110, the two windows to the frame. Nothing else in the run
selects a page >= 10, and 0xFFF148 takes no value but 0 and 1. So both
markers are exact and they agree.

**The page select is the better gate, and it is already in the
pipeline.** 0xFFF148 is a WRAM byte the SH-2 cannot read; it reaches
the master only through MD_STATE's word on COMM14 (fold 4), which is
one more link to be wrong. The page-select REGISTER rides the text
capture -- the S16 keeps its layer regs at text words 0x740-0x7FF and
`latch_layer_regs` already reads them out of TEXT_C every vint
(m_main.c, and BUSES.md section 2). So the master can gate on

    (any 4-bit quadrant of the FG or BG page select) >= 10

with no new channel, no new byte, no detector, and -- the property
that matters here -- nothing palette-derived, so the glow animator
cannot corrupt the evidence the gate depends on, which is what killed
the pscene_cur gate.

**And a discrepancy worth the builder's time.** They report 0xFFF148 =
0 during the scene in our rom. On the arcade it is 1 for exactly the
110 frames the chevron is up. Either their sample was outside the
window (their frame numbers are ares frames of our rom, not MAME
frames of the arcade -- their ~860-960 against this run's 1056-1164 is
a boot-timing difference, not a disagreement about the scene), or
MD_STATE's cut bit is not carrying the byte. Fold 4 built that channel
for exactly this purpose, so if it reads 0 while the game's byte reads
1, that is a defect in the state word and it is worth knowing
independently of the chevron.

**There is no separate "player mid-transformation" state to find.** In
this game the transformation IS this full-screen scene: the player
collects three spirit balls, the picture cuts to the chevron plane with
the head rising, and the scene ends. Pages 10/11 and 0xFFF148 mark the
whole of it.

---------------------------------------------------------------------
## 127. CORRECTION to 92-94: 0xFFF148 is an OBJECT INDEX + 1, not a cutscene flag. And the tape cannot desynchronise from the game, so the demo's divergence is not input timing (2026-09-14)

The builder's page-select probe says our port never selects a page >= 10
in 5,400 frames while the arcade runs the transformation twice, and
proposes that the tape-driven demo diverges because GAMEGATE releases
~62 game frames per 64 vints and open-loop inputs then land at the
wrong moments. Two things from the bytes.

**1. What 0xFFF148 actually is.** The object dispatcher at 0x398E walks
64 slots from 0xFFC000 in 128-byte strides, using 0xFFF109 as the LOOP
INDEX (cleared 0x3992, incremented 0x39BA, bounded at 64 by 0x39BE):

    3996  tstb %fp@(0)              slot active?
    399c  moveb 0xFFF148,%d0
    39a0  beq 0x39a8                zero -> run this object's handler
    39a2  cmpb 0xFFF109,%d0         else run ONLY object (0xFFF148 - 1)
    39a6  bne 0x39b0                every other active object -> jsr 0x3F04
    39a8  moveal %fp@(2),%a0 ; jsr (a0)

and the transformation sets it at 0x9104 with

    moveb 0xFFF109,0xFFF148 ; addqb #1,0xFFF148

i.e. **the object currently running writes its own slot index + 1**, which
freezes every other object and leaves itself the only thing updating.
So 0xFFF148 is "object N-1 owns the frame", and the value 1 that entries
92-94 read as "cutscene on" means object 0, the player, owns it.

That matters for fold 4: MD_STATE carries this byte as a cutscene FLAG.
It is a slot index, so any future use that tests it for a particular
scene is testing which object seized the loop, not which scene is up.
The page select (126) is the scene marker; this is the object marker.

**2. The tape CANNOT drift against the game frame, so the builder's
hypothesis as stated is falsified.** The demo frame counter 0xFFF02A is
incremented at 0x12EC, and the tape is read at 0x13F2 indexed by that
same counter. Their call sites are ONE INSTRUCTION APART:

    97c  bsrw 0x12ec     0xFFF02A += 1
    980  bsrw 0x1366     read the ports / play the tape at index 0xFFF02A

Both are in the same per-game-frame routine. A vint the game does not
get is a game frame it does not run, and the tape does not advance
either. Inputs cannot land "at the wrong moments" through a 62-of-64
release rate; the tape and the game step together by construction.

**So what else can make our demo play a different game?** The one thing
the program READS that our port has to synthesise: tile RAM. Entry 99
found the only in-play tile-RAM accessor is 0x683C, which READS tile
words at computed offsets (0x6936-0x6A84) as the ground and wall test
-- "which is why a ledge the port does not draw still holds the player
up". Our port keeps that data in the framebuffer hole across a
double-buffered bank and replays it with restore_pages after every
flip. If any page is stale or missing in the bank the game reads, the
collision answer differs, the player lands where the arcade did not,
and from that frame on the demo is a different game -- which would
show up exactly as "never collects the three spirit balls".

**The test that would settle it in one run, and it is cheaper than a
frame-by-frame diff:** log object 0's position (0xFFC000's coordinate
fields) once per GAME FRAME on both machines and find the first frame
they differ. If the divergence starts at a landing or a ledge, it is
the collision read-back. If it starts mid-air with identical inputs, it
is something else. Positions are 64 bytes a second; the whole demo fits
in a few KB.

---------------------------------------------------------------------
## 128. Mike's vertical dithered column is the SHADOW fallback, and it degrades exactly where the MD plane shows -- so every cell the colour bake moves to the MD plane makes it worse (2026-09-14)

Mike's play shots (screenshots/20260914_1506xx): a full-height,
hard-edged column of 50% dither, yellow in one frame and blue/white in
the next, with Zeus's head at its top. It is not arcade art -- no S16
effect is a hard-edged full-height rectangle -- and it appears at the
power-up, so the port's game state in credited PLAY does reach the
transformation even though the builder's probe shows the attract DEMO
never does (NOTES 70).

**What the hardware does.** jtcores `jts16_colmix.v:88`:

    gated = (shadow & ~pal[15]) ? { dim(rpal), dim(gpal), dim(bpal) } : ...

A shadow pixel DIMS whatever colour is underneath it, per pixel, every
pixel. It is never a pattern and never a flat colour.

**What we do.** m_main.c 444-450:

    #ifdef DIRECT_FB
    #define DFB_SHADOW_PIX(sx)  if ((sx) & 1) row[sx] = shadow_lut[0];
    #else
    #define DFB_SHADOW_PIX(sx) { uint8_t up_ = urow[sx];
        if (up_)           row[sx] = shadow_lut[up_];
        else if ((sx) & 1) row[sx] = shadow_lut[0]; }
    #endif

When the underlying pixel is KNOWN the shadow is correct -- a lookup
of that colour's dimmed twin. When it is NOT known the code writes one
flat colour, `shadow_lut[0]`, on ODD COLUMNS ONLY. That is a 50%
vertical dither in a single colour, which is precisely the column in
the screenshot.

**And the case where it is not known is the MD plane.** `up_` is the
32X framebuffer's own pixel. Zero there means MD-through: the colour
lives in the Mega Drive's plane, on the other chip, and the SH-2 cannot
read it -- FM=0 framebuffer reads return garbage (FM_TEST, 1507
mismatch against 176 match). So the shadow is exact over 32X content
and degrades to a one-colour column dither over everything the MD plane
draws.

**The interaction nobody has costed.** Every cell moved to the MD plane
-- which is the entire direction of MDSTATIC, MDROUND, the colour-line
bake and card P -- is a cell where a shadow passing over it renders as
this dither instead of as a dim. The two strategies work against each
other, and the more successful the plane bake is, the more visible this
becomes. Mike reporting it now, after card P, is consistent with that.

**What would fix it, in rising cost.** (1) Dither on BOTH parities
rather than odd columns only, so it reads as a uniform 50% veil instead
of a striped curtain -- one character change, no new information
needed, and it is strictly closer to a dim than a stripe is. (2) Ship
the MD plane's own shadow: the Mega Drive has a shadow/highlight mode
and its priority bit selects it, so a shadowed MD cell could be
rendered by the MD itself rather than faked on the 32X layer -- that is
a real card and wants the hardware's rules read first. (3) Keep a
parallel 1-byte-per-pixel record of what the MD plane will draw, so
`up_` is known everywhere -- correct, and it costs a whole extra
screen-sized buffer and the writes to maintain it, which is exactly the
bandwidth the generation card is trying to recover.

(1) is free and should be tested first: if Mike reads a uniform veil as
acceptable where a stripe is not, the defect stops being a blocker
without anyone paying for it.

---------------------------------------------------------------------
## 129. CORRECTION to 125: sets 19/20/21 live ONLY on the chevron pages, so they are NOT Mike's black tiles in the level. The level's black is tile RESIDENCY (2026-09-14)

Entry 125 said sets 19, 20 and 21 are in every round's tilemap and no
round's table, and concluded they are Mike's remaining black squares.
The first half is true and the second half is wrong. Per-page breakdown
of round 0's loaded tilemap:

    pages 0-4 (FG)   74-91          pages 5-9 (BG)   74-101
    page 10          sets 20, 21
    page 11          set 19

Sets 19/20/21 exist ONLY on pages 10 and 11 -- the chevron plane, which
is displayed only while the transformation runs (126: page selects
0xAAAA/0xBBBB for 110 frames). In the ordinary level view the displayed
pages are 0-9 and every set they use, 74-101, IS in round 0's table.

**So the black blocks in Mike's graveyard and boss-fight shots have a
different cause, and the reserved-line card for 19/20/21 will not fix
them.** It remains the right fix for the CHEVRON -- that is what those
three sets are -- but it must not be sold as the fix for the level.

**What the level's black most likely is.** Mike: "more visible after
screen transitions -- the wolf transformation, the Neff smoke transform
where the background changes colour, and level 2." A cell whose tile is
not yet resident in the MD cache renders as the reserved blank slot,
which is backdrop, which is black. The batch that fills the cache is
MD_BATCH = 24 tiles a vint, and entry 115 measured a scene cut asking
for 378-561 new codes at once. 378 at 24 a vint is 16 vints of visible
holes at best, and on the rig only about 26 vints in 64 present, so the
window is longer in wall time. Black rectangles appearing after every
transition and filling in is exactly what that looks like.

**The distinguishing test, and it is Mike's eye not a probe:** does a
black block FILL IN after a second or so, or does it persist? Pop-in
fills. A refused set never fills. His shots are single frames and
cannot tell the two apart, which is why this correction was needed --
125 read a still and assumed the persistent case.

**Also checked, and it holds:** the text layer is present and correct
in every one of the play shots examined -- the score, the second score,
PUSH 2P START, CREDITS 5 and the lives icon -- in the graveyard, at the
boss and in level 2. Whatever Mike is seeing about text being "present
in memory until the transformation" is not visible as a missing or
garbled glyph in these frames, and needs one more sentence from him
before anyone chases it.

---------------------------------------------------------------------
## 130. Mike's two answers: the black blocks PERSIST (so not pop-in), and the stale glyphs are the masked capture's SYMMETRIC failure -- a clear that does not mark (2026-09-14)

**1. The black blocks stay.** Mike: "stays present as a black box until
it moves past the animated background scroll." So it is not tile
residency pop-in (129's hypothesis), which fills in. It is a cell that
is permanently wrong and scrolls with the map.

And it is not a refused colour set either: entry 129 established that
round 0's displayed pages 0-9 use only sets 74-101, and all 28 of those
are in round 0's table. So for the graveyard the cause is neither the
set list nor the fill rate. What is left is per-CELL: the tile CODE
resolving to the blank slot permanently, or that code's ART being blank
in our bake when it is not blank in the ROM. The builder's own
blank-cell census (LOOP29 237: tv_b_noslot, tv_b_cut, tv_b_dirty) is
pointed at exactly this and is the instrument to re-read; and entry
107's unpacker off-by-one is the reason a specific code could be blank
in our art and not in Sega's.

**2. The stale glyphs, and this one has a mechanism.** Mike's
screenshots/20260914_150200-bldP.png, boxed by him: single text glyphs
left scattered across the playfield at screen rows 8-11 -- characters
that were written and should have been cleared after the Zeus pop-in,
and were not.

**This is the masked text capture's other failure direction, and note
65 predicted half of it.** Note 65 warned that a text writer outside
the builder's seven FMGATE entry points would write where nothing reads
and its glyphs would VANISH. The mask has the symmetric failure too: a
CLEAR whose row group is not marked is not captured, so TEXT_U keeps
the old glyph and the port keeps drawing it. A missed write loses a
character; a missed clear KEEPS one. Mike is seeing the second.

Before card O the master captured all 928 longs every vint, so every
clear was picked up whether or not it marked. Card O made the capture
conditional on the mark. So this is a card O regression by construction
and it is the first visible cost of that card.

**The suspects are the list from entry 122**, the text writers that are
NOT among 0x3A9A / 0x3AA4 / 0x3AAE / 0x153E / 0x4D88 / 0x369C /
0x1ACCA: own-loop writers at 0x057E, 0x162E (from 0x1608), 0x37D0
(the score writer, from 0x3766), 0x4212 (from 0x42D8), 0x469C (from
0x4554/4568/457C/4590/45A4), 0x4D3A (from 0x4D12/0x4D1E), plus the two
sites that STASH a text pointer into an object field (0x56DC into
a0+36, 0x64CA into fp+108) and the eight further writers of those
fields. Any of these that clears rather than writes produces exactly
this.

**And it strengthens note 65's recommendation.** Marking at each of
sixty-one call sites is the fragile design; masking the DESTINATION at
the point of use -- so every write and every clear, wherever it comes
from, lands in a mirror whose group mark is derived from the address
itself -- covers the stash sites and the clears in one place. That was
the advice for fold 5; it applies to the mask today.

---------------------------------------------------------------------
## 131. 0xFFF02A is DUAL-PURPOSE: a countdown timer on the card steps and the demo frame counter on the demo steps. And 0x9052 is an unmarked full-playfield text clear that runs only during the transformation (2026-09-14)

**The counter, resolved.** The builder traced 0xFFF02A counting DOWN at
frame 296 and UP at 1051 and concluded it is not a frame counter. Both
readings are right and so is mine; the variable has two jobs. Measured
per attract step over 5,000 frames:

    step 08 (SEGA card)   f400-446    42 -> 0 -> 65535   COUNTDOWN
    step 0C (demo)        f447-1167   reset to 0 at f451, then UP to 698
    step 10 (eye)         f1168-1480  held at 698
    step 14 (demo)        f1481-2098  reset to 0 at f1485, then UP
    step 1C, 00           timers again (180 -> ...)
    step 04 (demo)        f2685-3403  reset to 0 at f2689, then UP

Inside a DEMO step the only decrease in the whole run is the reset at
the step's start, three to four frames in. Between resets it is strictly
+1 a game frame. The writers agree: `addqw #1` at 0x12EC is the demo's,
and `movew #180` / `#240` at 0x1F40, 0x2008, 0x2056 with `subqw #1` at
0x1F74, 0x2048, 0x2094 are the card steps' timer.

**So the tape index is 0xFFF02A and it is valid only while the step is a
demo.** The tape read at 0x13DA does `movew 0xFFF02A,%d2; muluw #3,%d2`
and the pointer itself comes from 0x1834 indexed by `(0xFFF031 & 0x18)
>> 1`, so the game already scopes both to the step. A log that wants a
global index needs (step, 0xFFF02A) or a demo sequence number, because
each demo restarts at 0 -- which is exactly the collision the builder
hit on their second attempt.

Demo steps are 0x04, 0x0C and 0x14 in 0xFFF031; 0x00 is the high-score
table, 0x08 the SEGA card, 0x10 the eye and 0x1C a transition.

**Separately, for the stale glyphs, a routine neither of us had.** At
0x0988 the per-frame path chooses its text clear on 0xFFF148:

    0988  tstw 0xFFF148
    098c  bne 0x996
    098e  jsr 0x3AAE        the ordinary clear (in the builder's seven)
    0994  bra 0x99c
    0996  jsr 0x9052        the TRANSFORMATION clear (in nobody's list)

and 0x9052 is `lea 0x410230,%a0` then twenty rows of twenty longs with a
128-byte stride: rows 4 to 23, columns 24 to 63 -- the ENTIRE visible
playfield text area, cleared every frame for as long as an object holds
the loop.

So during the Zeus pop-in the game stops using its normal clear and
uses this one, and this one is not among the seven sites card O marks.
That is precisely "screen text that should have been cleared after the
Zeus pop-in" and it is unmarked by construction.

**The builder's objection to the mask theory still stands and is worth
keeping.** The mask forces a full capture every 8th vint, so a missed
mark should heal in about 130 ms and Mike's glyphs persist. Either the
backstop is not reaching those rows or the stale text is not a missed
mark. 0x9052 does not resolve that; it only says which clear is the one
going missing if the mask theory survives their measurement.

---------------------------------------------------------------------
## 132. The step machine, read end to end: 0xFFF031 advances at 0x1EBC, reached only from 0x0B1A, and the gate on that path is 0xFFF026 BIT 0. A stuck step 08 predicts that bit is clear in our attract (2026-09-14)

The builder found our attract sits at step 0x08 for its whole run while
0xFFF02A climbs monotonically past 2,139, on bldB, bldJ and bldO alike,
and asked which routine advances 0xFFF031 and what it needs.

**The advance.** One site:

    1EBC  addqb #4,0xFFF031
    1EC0  andib #28,0xFFF031          wrap 0x00..0x1C
    1EC6  moveb 0xFFF031,%d0
    1ECA  lea 0x26DC,%a0 ; moveal %a0@(0,%d0:w),%a0 ; jmp (%a0)

and the table at 0x26DC is

    00 -> 0x1EF2 (high-score)   04 -> 0x1ED4 (demo)   08 -> 0x1F80 (SEGA card)
    0C -> 0x1ED4 (demo)         10 -> 0x20A0 (eye)    14 -> 0x1ED4 (demo)
    18 -> 0x2282                1C -> 0x2282

0x1EBC is not called; it is FALLEN INTO from the attract-entry block at
0x1E54 (which resets the stack, sets 0xFFF026 = 1, clears 0xFFF148,
sets the tile bank, then runs 0x36B0/0x36C4/0x3952/0x3B08/0x1338 and
drops through). Three sites branch to 0x1E54: 0x0B1A, 0x0BAC and
0x0D0E.

**The gate, and it is the answer.** The 0x0B1A path is the demo's own
exit, and the whole chain is gated at its top:

    0AE2  btst #0,0xFFF026        ATTRACT?
    0AE8  beq  0x0B1E             NOT attract -> the credited-game path, never returns here
    0AEA  btst #5,0xFFF028 ; bne 0x1E54     P1 start -> advance
    0AF4  btst #5,0xFFF029 ; bne 0x1E54     P2 start -> advance
    0AFE  tstw 0xFFF148 ; bne 0x0B08 ; bsr 0x144A
    0B08  cmpiw #698,0xFFF02A     the demo's frame cap
    0B0E  bcs  0x097C             under the cap -> keep looping
    0B12  clrb 0xFFF148 ; clrb 0xFFF15E
    0B1A  bra  0x1E54             AT the cap -> ADVANCE THE STEP

So the step advances when 0xFFF02A reaches 698 -- but ONLY if
0xFFF026 bit 0 is SET. With that bit clear, 0x0AE8 diverts to 0x0B1E
before the cap is ever compared, and nothing on that path advances the
step.

**Which predicts our fault exactly.** If 0xFFF026 bit 0 is CLEAR during
our attract then, in one stroke:

  - the step never advances (0x0B08 is unreachable) -- stuck at 0x08;
  - 0xFFF02A is never compared against 698 and never reset, so it climbs
    monotonically forever -- the builder measured 2,139;
  - the TAPE never plays: the reader at 0x13C0 tests the same bit and
    branches past the tape to the live ports (entry 111), so our "demo"
    runs with NO INPUT AT ALL;
  - with no input the player object does something else entirely --
    which is the 769 distinct positions against the arcade's 589, and
    it is not drift, it is a different game;
  - the transformation never runs, so pages 10/11 are never selected
    and 0xFFF148 never sets.

Every symptom in the builder's last four notes falls out of one bit.

**And this bit has burned this project before.** Entry 105 corrected
NOTES 23's inverted reading of 0xFFF026 bit 0 -- it is ATTRACT when
SET, credited play when clear -- and that inversion caused vi90's
black/slow regression. A build that leaves the bit clear during attract
is the same error's twin: the port would be running the credited-game
path with no coin.

**The measurement, one line, either machine:** read 0xFFF026 bit 0 in
our rom during the attract. Set means this theory is wrong and the
fault is further down. Clear means it is right and the question becomes
who cleared it -- 0x1E62 sets it on every attract entry, and 0x06C0
does `andib #1,0xFFF026`, so a patched or skipped 0x1E54 entry is the
place to look.

---------------------------------------------------------------------
## 133. THE 60 Hz BAR, VERIFIED FROM THE GAME'S OWN MISSED-FRAME COUNTER: the arcade drops 1 frame in 55 seconds of play (2026-09-14)

Mike: "do we ACTUALLY know that the arcade game is 60 Hz? Have we
genuinely measured that?" The project has assumed it since the first
loop and nobody had. The game keeps its own counter for it.

**The instrument, in the game's IRQ4 handler:**

    2AAC  (IRQ4 entry)
    2AB0  tstb 0xFFF01E ; bne 0x2C7E        display gate
    2AB8  tstb 0xFFF01C                     did the game consume last frame?
    2ABC  beq 0x2AC6                        yes -> serve this one
    2ABE  addqw #1,0xFFF144                 NO -> COUNT A MISSED FRAME
    2AC2  braw 0x2C06
    2AC6  addqb #1,0xFFF01C                 release the main loop

0xFFF144 is the arcade's own dropped-frame count: every vint in which
the 68000 had not finished the previous frame's pass. It is cleared at
0x0930 (scene entry) and read at 0x0BE2. Nothing else touches it.

**Measured, MAME, sampled every 300 display frames.** Credited play,
walking and attacking, 55 seconds:

    f1200  delta 0    300 game frames  60.0 Hz
    f1500  delta 0    300              60.0
    f1800  counter cleared at the round load
    f2100  delta 1    299              59.8
    f2400..f4500  delta 0 at every sample   60.0

**One missed frame in 3,300 display frames of gameplay.** The attract is
the same: zero at most samples, a burst of 17 across one scene
transition, and a clear at each scene load.

**So the bar is real and it is exactly 60.** The System 16B game logic
runs one pass per display frame and the hardware sustains it with
essentially no drops -- not 30 Hz logic on a 60 Hz display, which is
what many boards of this era do, and which would have made our target
wrong by a factor of two. The manual's "15.75 kHz / 60 Hz" monitor
figure (HANDOFF-DISCOVERY) is the display; this is the LOGIC, measured.

**What it means for the port.** 64 presented frames per 64 vints is the
correct target and there is no cheaper honest bar hiding behind it. It
also means the arcade itself has near-zero headroom at 60 -- the board
is running the same 2,780-instruction pass we are, and finishing it
every frame -- so any claim that "the arcade drops frames here too" is
false for this game and must not be used to excuse ours. The only
exception is the handful of frames at a scene load, which the game
itself clears the counter across.

---------------------------------------------------------------------
## 134. CARD F0's ANSWER DECOMPOSED: the 1.57-vint floor is FRAMEBUFFER PASSES, and ARCHITECTURE.md predicted it on day one (2026-09-14)

The floor probe: with the entire compose ablated the rig still presents
~41 of 64, a floor of 1.57 vints. Census on hardware at last --
protocol 1.57 (64%), slave compose 0.49 (20%), master maps 0.41 (17%).
Same build: ares 0.56 v/gen, rig 1.57. The FPGA charges 2.8x for the
pipeline with the compute removed.

**The floor is not mysterious. It is bandwidth, and this repo wrote the
law down before any of this work started.** ARCHITECTURE.md section 1:

    32X framebuffer write bandwidth   6.76 MB/s MEASURED
    one 320x224 8bpp pass             71,680 bytes
    budget at 60 Hz                   ~1.6 screen passes a frame
    "The blit alone is one full pass."
    "Any proposal that does not change the number of screen passes
     cannot change the framerate."

Arithmetic against the measured floor:

    one vint at 6.76 MB/s        112,700 bytes
    one screen pass              71,680 bytes = 0.636 vints
    clear + blit = two passes    1.27 vints
    measured floor                1.57 vints
    residual for transport, window, flip, 68K handler   0.30

Two full framebuffer passes plus a third of a vint of protocol is 1.57.
That is the floor, to within the spread of the samples.

**So the correct reading of card F0 is not "the protocol is expensive".
It is "we write the screen twice and the hardware affords 1.6 passes".**
The compose work of the last two weeks has been optimising the 0.6 of a
pass that is left after the two mandatory ones. That is why cutting the
whole compose still leaves 41 of 64: the compose was never the thing.

**The lever the repo already built and never shipped.** `DIRECTFB`
makes the compose write straight into the framebuffer back bank, which
makes `blit_half`'s row loop inert -- it removes ONE FULL PASS. Its
Makefile header describes the whole design (clears become FB-row
long-fills, the bank needs no plumbing because 0x04000000 maps the
draw bank by hardware, the flip gates on "this interval actually
composed"). It has never been in the shipping flags: LOOP29 3033 and
6444 both record the DIRECT_FB arm as dead code on the line.

    floor today                  1.57  (clear + blit + protocol)
    blit removed by DIRECTFB    ~0.93  crosses 1.00
    clear also folded in        ~0.30 + the compose

**What it costs, honestly, because it is not free.** Composing into the
displayed-side bank means no read-back: the `urow` read that makes the
shadow exact is gone (entry 128 -- under DIRECT_FB the shadow already
falls to the odd-column dither by construction), and any compose
decision that reads its own destination has to go. The repo's
READ-FREE COMPOSE work (m_main.c 5276) was done for exactly this
reason. And tearing: composing into a bank the display is scanning
needs the flip gate the header describes.

**What I would ask for before the card.** One measurement, and the
existing counters can give it: FRAMEBUFFER BYTES WRITTEN PER
GENERATION, split by clear, compose, stamp and blit, on hardware. If
clear and blit really are ~143 KB of the total then DIRECTFB is worth
0.64 vints and is the single largest lever left in the project. If they
are not, this arithmetic is wrong and I want to know before anyone
spends a week on it.

**And a correction to keep the record straight.** LOOP29 252 priced the
stamp's cost as "the 32X FB write floor"; LOOP29 6444 already corrected
that to SDRAM write-through stores, because the compose target on this
line is sbuf in SDRAM, not the framebuffer. That correction is what
makes this entry's arithmetic work: the two FB passes are the CLEAR and
the BLIT, and nothing else on the line writes the framebuffer in bulk.

---------------------------------------------------------------------
## 135. CORRECTION to 134, forced by the builder's NOCLEAR result: the line has ONE framebuffer pass, not two, and my 0.64 is DERIVED not measured (2026-09-14)

The builder tested 134's decomposition the right way -- NOCLEAR on the
168 stack -- and R FELL, 40.7 to 33.2, floor 1.57 to 1.93. They
attribute it to DIRTYROW enlarging the blit when the clear stops making
rows uniform, and say NOCLEAR therefore cannot test the claim on this
line. Both halves are right, and there is a bigger error underneath.

**Answering their first question straight: 0.64 a pass is DERIVED, not
measured.** ARCHITECTURE.md's 6.76 MB/s traces to LOOP 20 -- 13.6 SH-2
cycles per longword of which 2.7 is instruction issue, from ares at
47.34 us/row. 71,680 bytes at that rate is 10.6 ms, and 10.6 of a
16.67 ms vint is 0.636. So the figure is a per-row ares number scaled
to a full screen. It was never measured as a pass on hardware, and the
rig has since charged 1.8-2.8x what ares charges everywhere it prices
memory. The builder's instinct that the per-pass cost may not be
constant is the right one to hold.

**And the structural error, which is mine.** 134 said "clear + blit,
two full framebuffer passes". On this line the compose target is
`sbuf`, a plain `static uint8_t[336*232]` in SDRAM (m_main.c 1836), and
LOOP29 6444 already corrected 252 on exactly this point. So the line
writes the FRAMEBUFFER once -- the blit -- and the clear is an SDRAM
clear of sbuf, not an FB pass at all.

    134 said   clear (FB) + blit (FB) = 1.27 of the 1.57
    actual     blit (FB) 0.64 + clear (SDRAM) + protocol = 1.57

That is why NOCLEAR could not move the floor by a pass: it never
removed one. It removed an SDRAM clear and, through DIRTYROW, enlarged
the one real FB pass. The builder's diagnosis is right and my
experiment design was wrong at the premise.

**What survives of 134, and it is the part that matters.** DIRECTFB is
still the lever, and the reason is unchanged even though my arithmetic
was: it makes the compose write the FB back bank directly so the
staging-plus-blit copy disappears. The flag's own header cites FBBENCH
measuring FB writes at 0.98-0.99x SDRAM writes, so the copy buys
nothing and costs a full pass of stores. That is a claim about removing
a COPY, not about removing one of two FB passes, and it does not depend
on 134's bad framing.

**What I would NOT do now: the corrected ablation.** NOCLEAR plus a
forced full-row blit would answer a question about my model rather than
about the machine, and it needs two builds. The same question falls out
free of a measurement I already asked for in NOTES 74 -- FB BYTES
WRITTEN PER GENERATION, split by phase, on hardware. If the blit is
~71 KB and the clear is SDRAM, the decomposition is settled without
ablating anything, and the per-pass cost stops being derived.

**Their second question, and my answer is unambiguous: start the
protocol workstream.** 64% of the line's generation sits in it on their
own census, it does not depend on which of us is right about passes,
and the pass question now has a cheaper answer than an ablation.

---------------------------------------------------------------------
## 136. The FB-byte count confirms one pass AND exposes a dead flag: BLIT_SKIP saves 3 bytes of 71,680 against a design that predicted 62.7% (2026-09-14)

The builder's FBBYTES counter, at the blit's row commit with every skip
test above the line:

    build          gens   FB bytes/gen   SDRAM clear/gen
    line (bldS)    2788      71,677          32,714
    F0 floor       3646      71,675          27,558
    a full screen               71,680

**Three confirmations and one surprise.**

  1. One framebuffer pass, exactly. 71,677 of 71,680. Entry 135's
     correction is settled by counting, with no ablation.
  2. The clear is SDRAM, 32,714 bytes into sbuf, because DIRECT_FB is
     not in the shipping flags. It was never a framebuffer pass.
  3. The F0 FLOOR BUILD WRITES THE SAME 71,675. The 1.57-vint floor does
     not sit beside a pass; it CARRIES one. So the pass costs at most
     1.57 and the protocol's share is whatever is left inside it. The
     per-pass cost is now bounded by measurement instead of derived
     from an August ares per-row figure.

**The surprise, and it is worth more than the confirmation.**
`BLIT_SKIP` is in the shipping flags. Its design skips the eight stores
of a 32-pixel group when the group is entirely transparent AND the
target bank already holds zero there, and its own header justifies it
with "ares measured 62.7% of groups entirely transparent after MDBGALL
moved the background to the MD plane". It is saving THREE BYTES of
71,680 -- 0.004% against a predicted 62.7%.

So one of these is true, and each is cheap to tell apart:

  a. the transparency premise expired. The background went to the MD
     plane, but the compose fills sbuf anyway (m_main.c 599 says sbuf
     is "EXPLICITLY ZEROED every row every" generation, and the clear
     count of 32,714 is consistent with a partial fill), so groups are
     no longer transparent by the time the blit sees them;
  b. the SECOND condition never passes. The skip also requires the
     target bank to already hold zero there, tracked in a per-bank
     per-row mask at 0x3A300. If that mask is conservative, or is
     invalidated every flip, the group is transparent and the skip is
     still refused every time.

**Why it matters at this moment.** If 62.7% of groups really are
transparent, the blit is writing ~45 KB a generation that changes
nothing. Against a floor that CONTAINS the pass, that is the largest
single identified quantity left, and it needs no new subsystem -- the
code, the mask and the gating protocol are all already written and
shipping. It is also the cheapest possible test of what an FB byte
actually costs on hardware, which is the number the whole protocol
workstream now hangs on.

**And a flag that saves 0.004% is not free.** It ORs eight longs per
group across 2,240 groups a screen. On the line it is pure cost. Either
it starts paying or it comes off.

**DIRECTFB, reframed by the same count.** It is not "remove one of two
passes" -- entry 135 already retracted that. What it removes is writing
every pixel TWICE, once to sbuf in SDRAM and once to the framebuffer,
and replaces the blit's unconditional 71,677 bytes with compose's
actually-drawn pixels plus a clear that becomes FB writes (32,714 today
in SDRAM). Unless compose draws more than ~39 KB of pixels a
generation, DIRECTFB writes FEWER framebuffer bytes than the blit does,
on top of deleting all of compose's sbuf writes and all of the blit's
sbuf reads. **The number that sizes it is compose's own byte count**,
which the FBBYTES instrument can produce the same way it produced this
table.

---------------------------------------------------------------------
## 137. The floor is NOT bytes, by arithmetic: 12 KB cannot be 1.5 vints at any plausible price. It is LATENCY, and the instrument should count round trips (2026-09-14)

The builder retracted their own 71,677 figure -- the counter sat above
the per-group skip loop and reported an upper bound as a measurement.
Counting at the group store: 926 groups examined a generation, 71.6%
transparent (better than the 62.7% BLIT_SKIP was designed against),
69.9% skipped, **12,181 framebuffer bytes actually written**. Both of
entry 136's hypotheses are dead and the flag is earning its ORs. My
question was right to ask and its premise came from their bad number,
which they say plainly.

**So what is the 1.57?** One vint is 383,500 SH-2 cycles at 23.01 MHz.
The same ablated build reads 0.56 on ares and 1.57 on the rig, a gap of
387,335 cycles a generation. Attribute that gap to each candidate and
read the implied price:

    if it were the 12,181 FB bytes written        31.8 cycles a byte
    if it were the 34,276 bytes of groups read    11.3 cycles a byte
    if it were the 27,558-byte SDRAM clear        14.1 cycles a byte

**All three are implausible, and the first is self-refuting.** At 31.8
cycles a byte a full 71,680-byte screen would cost 2.5 million cycles,
6.5 vints -- and this port demonstrably blits full screens in far less
than that. No per-byte price consistent with the machine working can
put 387,000 cycles into 12 KB. The floor is not stores.

**Which leaves latency, and the log already said so in a different
context.** LOOP29 275's own heading is "THE GENERATION WAITS, IT DOES
NOT COMPUTE", and the slave's histogram there is bimodal -- two
populations, not a spread, which is the signature of waiting on
something that either has or has not happened by a deadline. A
throughput model predicts a spread. A round-trip model predicts exactly
two modes.

**So the protocol workstream's instrument should count ROUND TRIPS, not
bytes.** Per generation, on hardware: how many times does a CPU stop
and wait for another, and how long is each wait. The stamps already
exist -- FRT stamps through flip_span (VB_SPAN), the SYNC[6]/SYNC[7]
slave handshake, the COMM4 echo, the 68K's post wait, GAMEGATE's
release. Nobody has ever laid them end to end on the FPGA and asked
"how much of a generation is one CPU waiting for another".

The prediction that would confirm it: the waits sum to most of 1.57,
and each individual wait is quantised -- to a line, to vblank, or to a
vint -- rather than proportional to any byte count. If the waits are
quantised, the lever is REMOVING ROUND TRIPS, and no amount of doing
less work inside them will move the floor.

**And a method note, because this is the session's third instrument
error and they are all one shape.** LOOP29 283 measured credited play
and called it the attract. 284 scored a moving palette at one instant.
290 counted at the wrong level of a nested loop. In every case the CODE
was right and the READING was wrong, and in every case the number
survived long enough to be built on -- including by me, twice. The
builder's fix is the right one: keep the naive total beside the true
one so the gap is visible in every run. The general form: **a counter
should be validated against a case whose answer is known before its
number is allowed to size anything.**

---------------------------------------------------------------------
## 138. The master never waits for the slave: what that banks, and the circularity it exposes in "generation length" (2026-09-14)

Mike, reading the builder's stamps: "if the master never waits for the
slave then we ALREADY KNOW timing. This might be an answer we can
solidly bank on." He is right, and it settles more than it looks.

**The measurement.** Master's pre-flip stamps on the rig: slave-capture
wait 0.0, every sample, mean and max. Post seen at 22.3 lines, at the
guard also 22.3 -- the truth drain and the capture between them cost
essentially nothing. The master's whole pre-flip life is: enter the
ISR, wait ~22 lines for the 68K's post, flip. Bimodal, with a minority
saturating at 175+ lines and losing the flip.

**BANKABLE 1: the slave is not on the master's critical path.** The
master never blocks on it. So the slave's 0.49 v/gen in the F0 census
is NOT scheduling -- the master is never held up waiting for slave
work to finish.

**BANKABLE 2: therefore the slave's cost must be BUS CONTENTION, and
that is consistent with the one other thing we know.** BLITSHIFT
(moving rows between the two CPUs) was swept in August and did nothing:
the Makefile's own header says the blit is FB-bus-bound and both SH-2s
share the one write path, so moving rows "only relabels which CPU
waits". Put the two together: the slave does not delay the master by
handshake, and work moved between them does not help, but ablating the
slave's work DID move the floor 2.06 -> 1.57. The only mechanism that
satisfies all three is shared-bus contention -- the slave's traffic
steals memory cycles from the master without either one waiting on the
other.

**And that determines which levers can work.** On a shared bus,
redistributing work is worthless (proved) and reducing TOTAL traffic
across both CPUs is the only thing that moves. Every future card should
be sized in bytes-across-both-CPUs, not in per-CPU time. It also
explains why ares is 2.8x optimistic on the same build: ares models two
independent CPUs and charges neither for the other's traffic.

**THE CIRCULARITY, and this is the part to check before anything is
sized.** Under GAMEGATE the 68000 is released once per presented frame
(or by the fallback after GAMEGATE_MAXWAIT). The game's pass ends by
spinning on 0xFFF01C at 0x397E, so THE GAME ADVANCES ONLY WHEN WE
RELEASE IT. A compose generation follows a game frame. So:

    generations per second  <=  releases per second  <=  presented + fallbacks

"The generation takes 2.4 vints" may therefore be partly a CONSEQUENCE
of presenting at 26 of 64, not a cause of it. A loop that gates its own
input rate cannot be measured as if the rate were independent.

**The three-number check that settles it, all counters already
existing, one rig session:** per 64 vints, count

    generations LAUNCHED      (the master's chain start)
    68K releases              0xFFA0F6 delta -- flips plus fallbacks
    frames PRESENTED          BOOTFLIPRATE

  - gens ~= releases > presented: we are producing frames nobody sees.
    The wall is presentation, not production, and the compose numbers
    have been measuring the wrong end.
  - gens ~= presented < releases: the game is running ahead of the
    pipeline and generations are being skipped or coalesced.
  - all three equal: the loop is self-gating and every "wall" figure in
    this log is a measurement of its own feedback, which would make the
    2.47 not a cost but an equilibrium.

The last case is the one that would invalidate the most prior work, so
it is worth an hour before the protocol workstream sizes anything.

---------------------------------------------------------------------
## 139. The slave's echo splits: 0.58-0.78 v/gen of COMPUTE and a FIXED 0.245 v/gen of latency. Consolidating onto one SH-2 goes from "clearly worse" to "a wash", and the fixed term is the lever (2026-09-14)

Mike asked the obvious question: if the bus is the bottleneck and the
SH-2 is not, why not frontload everything onto one CPU? I answered with
the ares phase split (LOOP29 275: echo 1.05, mtask 0.70) and said the
trade was +1.05 serialized against -0.49 contention saved, a net 0.56
loss. **That used ECHO, which is WALL, not busy.** The builder has now
split it.

CHAIN_METER could not answer it: its base 0x2603A7D8 collides with
SPRLATE's lean base and the line carries -DSPR_LATE, so the master's
cached-alias writebacks reverted the slave's uncached sums. The first
read -- "busy 0 against 3817 links" -- was SPRLATE[7] misread as a link
count. Logged as an instrument caveat: **the lean-counter bases are not
private, and a cached/uncached alias pair silently loses the writer that
uses the uncached one.**

The real number was already in the shipping rom and is ungated:
0x26028C80[0] sums the slave's FRT across every command, window and
chain both, measurable on bldS with no probe build. Slave tick = phi/8
= 48,208/vint (the slave never sets TCR).

    input     master ECHO wait   slave BUSY          remainder
    attract   0.822 v/gen        0.580 (39.6% wall)  +0.243
    play2     1.027 v/gen        0.781 (50.0% wall)  +0.246

**Slave compute rises 35% from attract to credited play; the remainder
does not move.** 0.243 and 0.246 across a 35% load change is a fixed
term, and a fixed term under varying load is latency, not work. So
LOOP-DECOMPILE 137's prediction now has a number on it: **~0.245 v/gen
of round-trip latency inside the slave chain.**

### What this does to the consolidation question

Pure ares, both arms in the same instrument (no memory cost charged):

    now (concurrent)   wall = max(mtask 0.70, echo 1.027) = 1.027
    consolidated       wall = 0.70 + 0.781 (busy only)    = 1.48

So in ares, serializing is 0.46 worse -- but consolidation also deletes
the chain, and with it the 0.245, and the rig's slave-contention term
that the floor probe ablated at 0.49 (2.06 -> 1.57):

    +0.781 serialized   -0.245 chain latency   -0.49 contention
    = +0.046 v/gen

**A wash, well inside the error bars.** My "net 0.56 worse" was wrong
because it charged ECHO instead of BUSY. The corrected answer is that
frontloading onto one SH-2 is neither the win nor the loss I called it
-- it is roughly neutral, and it is therefore not worth the rewrite.

**The conclusion that survives either way:** 0.70 + 0.781 = 1.48 v/gen
of pure instruction with memory free, against a 1.57 protocol floor.
**One SH-2 cannot reach 60 Hz by arithmetic**, whatever the bus does.
That kills consolidation as a path to the bar and leaves it as a
possible tidy-up only.

CAVEAT, stated because it is the weak part: mtask 0.70 is card O's ares
figure, the busy/echo pair is bldS. Mixing an ares compute term with a
rig contention term is apples to oranges; ares does not charge the
slave's memory waits, which on the rig would land on the master once
serialized and push the +0.046 negative. The ranking is solid, the
magnitude wants the rig.

### The lever the split exposes

0.245 v/gen is ~10% of the 2.47 wall and it is pure round trips. The
number that makes it actionable is the one we do not have: **links per
generation.** The term is trips x cost-per-trip and we can only attack
it if we know the split. Three links a chain (the R2 close at
s_main.c:371-372 implies R0/R1/R2) would put it at 0.082 v/link = 21.5
lines; thirty links would make it a per-trip cost too small to chase and
a COUNT problem instead. Those are different cards.

CHAIN_METER's [7] was meant to be that count and is void. The ask is one
word: the slave's command count over the same run, against the same
0x26028C80 base that produced the busy figure.

### Instrument note banked from the same session

TRIPCENSUS (the gens/releases/presented triple that NOTES 78 asked for)
validated on ares -- channel tag 0 reads 64 against an independent SDRAM
counter reading 64 in the same run -- after two faults were caught
pre-use: a bare bit-15 COMM6 reader that the 68K's own 0xB101 announce
satisfies, and a 6-bit value that saturated both gens and releases at a
flat 63. A third trap on the rig: **the flood paints MD palette lines
0-1 only**, so on the title screen it covers a few sprites and a naive
reader returned a stable "presented = 64" seven times off a 272-pixel
patch of the INSERT COIN blocks. The reader now requires 40% band
coverage or reports nothing. Rig session in flight; the triple is NOT
yet in.

---------------------------------------------------------------------
## 140. The triple lands MIDDLE: 2.47 v/gen is a real cost. But the 34.1 "timeouts" are not timeouts -- they are the configured design, and they mean the 68K writes its staging twice for every frame we consume (2026-09-14)

LOOP29 293, rig attract, per 64 vints:

    gens 28.8   releases 56.8   fallbacks 34.1   presented 27.8
    token releases 22.7

**Middle case: gens ~= presented < releases.** NOTES 78's circularity is
answered and it is the benign answer. The loop is NOT self-gating, the
generation rate is not an equilibrium set by the presentation rate, and
**every wall figure in this arc stands.** 2.47 v/gen is a cost. That
closes the check I asked for and it clears the way to size cards again.

Second-order: gens 28.8 against presented 27.8 means **96.5% of the
generations we launch get presented.** We are not composing frames that
nobody sees. The compose path has no waste in it; the wall is the cost
of a generation, not the count of them.

### The builder's framing of the 34.1 is wrong, and the arithmetic says so

LOOP29 293 reads "34 of 57 releases are GAMEGATE timeouts." The default
`GAMEGATE_MAXWAIT` is 4 (Makefile 2394). With 27.8 flips there are 36.2
non-flip vints, and `gg_wait` resets on every release, so a MAXWAIT of 4
predicts **~9 fallbacks, not 34.1.** The default cannot be what ran.

Two flags produce 34.1, and both are deliberate:

  * **`GATEFREE=1`** (Makefile 2069-2071 -> `GATE_FREE`, md_main.c
    3546-3551): on a vint with NO window, release anyway and bump
    0xFFA0F4 -- the comment at 3549 literally says *"counted as a
    fallback"*. So these land in the fallback tag by construction while
    being nothing of the kind.
  * **`GAMEGATEWAIT=1`** -> MAXWAIT=1, which fires on every non-flip
    vint: predicts 36.2 against 34.1 measured.

Either way the 34.1 is **the configured intent, not a pathology**, and
it implements Mike's 2026-09-11 call recorded verbatim at md_main.c
3543-3545: *"we get our player missing frames, but we dont slow down
gameplay to catch up. so this is the right progression."* The game runs
at 56.8/64 = **89% of 60 Hz on purpose.** Nothing to fix; the label on
the counter is what is wrong.

Which one is on the line is a one-line answer from the builder and it
matters for the next section.

### The internal check that survives the sampling caveat

293's caveat is fair: the four tags come from different 64-vint windows
and the attract changes scene under the sampler. But the tags satisfy
their own identity **exactly**: token releases + fallbacks = 22.7 + 34.1
= 56.8 = releases, to the reported precision, across separately sampled
windows. An identity that closes to 0.0 between independently drawn
means is evidence the means are sound, not just that the 57-vs-28 gap
beats the spread. The reading holds.

(One residue: token releases 22.7 against presented 27.8. Five flips per
64 vints present without producing a token release. Small, and not worth
a probe yet, but it is not zero and it should not be rounded away.)

### What the middle case OPENS, and it is the last unmeasured term

The 68K runs 56.8 game frames per 64 vints; we compose 28.8. **Roughly
28 game frames per 64 vints are composed by nobody.** The Makefile's own
GATEFREE note predicts the consequence: *"expect MORE tearing (the game
writes its staging twice as often)."*

That is two effects, and only the first has ever been discussed:

  1. **Tearing.** The master's truth drain reads staging that a running
     68K is concurrently writing. This is a correctness term and it is a
     candidate mechanism for the bitmap corrosion Mike reported on the
     night builds -- which has so far been filed under "we are sharing
     the beam" and never tested against this.
  2. **Contention, never measured.** The master's pre-flip reads cross to
     MD-side memory (truth drain, text capture) and arbitrate against a
     68K that is running a full pass twice as often as we need. Card O
     cut those reads and moved the rig 18 -> 21-27, which is consistent
     with cross-bus arbitration mattering, but the 68K's own rate has
     never been varied against v/gen.

This is the last term in the 2.47 that has no number. The slave is
decomposed (0.781 busy + 0.245 latency, entry 139), the master's half is
0.70, the protocol floor is 1.57. The 68K's contribution to SH-2 stall
is unmeasured.

**The isolation is one flag and it is DIAGNOSTIC ONLY.** Build without
GATEFREE (or with MAXWAIT back at 4) so the 68K runs at the compose
rate, and read v/gen:

    v/gen drops  -> the 68K's surplus frames cost the SH-2 directly.
                    There is a real term and a card behind it, and the
                    card is NOT "slow the game down" -- Mike ruled that
                    out -- but re-timing the surplus pass out of the
                    master's pre-flip window.
    v/gen flat   -> the 68K is free. GATEFREE is pure win, the surplus
                    frames cost nothing but 68K work, and the only thing
                    left to explain is the tearing.

**Do not ship the probe build.** It runs gameplay at ~44% speed and Mike
rejected exactly that on 2026-09-11. It is a measurement, and it answers
a question no other instrument we own can reach.

---------------------------------------------------------------------
## 141. NEGATIVE: the 0.245 is ONE trip of ~63 lines, and it is hidden. I called it the lever in NOTES 79 and I withdraw that. The general fault: ares's wall is SLAVE-gated, the rig's is MASTER-gated, so the whole ares phase split aims at a critical path hardware does not have (2026-09-14)

The count landed, same run as busy:

    input     cmds/gen              busy    gap     per trip
    attract   1.01 (chain 2763, window 0)  0.580  +0.243  0.2396 v = 62.8 lines
    play2     1.00 (chain 2559, window 0)  0.781  +0.246  0.2457 v = 64.4 lines

**One trip per generation, ~63 lines, invariant across a 35% change in
slave work.** Cost-per-trip is as flat as the product was. Zero window
commands -- all chain, which also explains the relocated CHAIN_METER
reading links 0: the chain command takes the self-chain branch (three
unmetered `slave_concurrent_k` calls), not the else branch the meter sat
on.

**Both cards from NOTES 79's table are dead, and for the same reason.**
There is nothing to batch (one command), and making the trip cheaper
buys nothing -- because of the builder's qualification, which is the
important part of their message:

  * `echo` is elapsed launch->echo-seen, **not blocking wait**.
  * `mtask` runs concurrently at 0.704 (attract) / 0.687 (play2).
  * So master idle is **at most** echo - mtask = 0.118 attract, 0.340
    play2.
  * And on the rig the master waits for the slave **zero ticks, every
    sample** (LOOP29 291).

The ~63-line trip is real, precisely characterised, and **completely
hidden behind the master's own tail on hardware. Attacking it buys
zero.** I wrote in NOTES 79 that it was "the first fixed cost in this arc
we can attack head-on." That was wrong and it is withdrawn. NEGATIVE.

### The fault underneath it, which is worth more than the finding

This is the third time in this arc that a term derived from the ares
phase split evaporated on hardware, and the reason is now general enough
to state as a rule:

**In ares the generation wall is `max(echo, mtask)` and echo WINS --
1.027 against 0.687, so the SLAVE is the critical path. On the rig the
master never waits for the slave at all, so the MASTER is the critical
path. The two instruments disagree about which CPU the wall is on.**

LOOP29 275 opened the generation card on "the critical path is the
SLAVE, at 1.05 v/gen against the master's 0.70." That sentence is true
in ares and false on hardware, and every card sized from that
decomposition aims at the wrong processor. This is the mechanism behind
my two wrong sizings (NOTES 79's consolidation arithmetic, NOTES 79's
lever) and it should be checked before any future card cites a phase
number.

The reason is not mysterious: ares charges instruction cycles only, so
it reports the slave's compute honestly and the master's memory stalls
not at all. Strip the master's stalls and the slave looks like the
bottleneck. Add them back and it is not close.

### What it does NOT overturn, and in fact confirms twice

The floor probe ablated slave work and moved the floor 2.06 -> 1.57.
That looks like it contradicts "the slave is hidden" and does not:

    slave TIME    is free -- hidden behind the master, zero wait measured
    slave TRAFFIC is not  -- it contends for the one write path, 0.49 v/gen

Both are true simultaneously and together they harden NOTES 78's rule
into a law for the rest of the project: **size every slave card in
BYTES, never in time. A slave card that saves time and not traffic saves
nothing.** Three independent measurements now agree on it (zero wait,
BLITSHIFT's death, the ablation).

### Where that leaves the 2.47, and why the next build matters

The wall is master-gated. The master's own instruction cost is 0.687
v/gen in ares. The rig's generation is 2.2-2.5. **So roughly 1.5-1.8
vints of every generation is the master stalled on memory**, and entry
137 already excluded stores by arithmetic -- 12,181 bytes cannot be 1.5
vints at any plausible price.

That leaves READS, and the master's expensive reads are the ones that
cross to MD-side memory: the truth drain and the text capture
(m_main.c 7726-7730, `cap_drain`). The one piece of hardware evidence we
have fits exactly: card O masked the text capture and moved the rig
18 -> 21-27 presented per 64, the single largest hardware movement of
this arc.

And the 68K is running 56.8 passes per 64 vints against our 28.8
generations, arbitrating against every one of those cross-bus reads.

**So entry 140's isolation build is no longer "what the answer opens".
It is the next measurement, and it is the only one left that can carry
the remaining 1.5-1.8 vints.** Everything else in the generation now has
a number: slave busy 0.781, slave trip 0.245 (hidden), master
instructions 0.687, protocol floor 1.57, stores excluded.

Open ask: do we have a measured per-word cost for a master read across
to MD-side memory? Nothing in SILICON.md carries one. If not, the
isolation build gives it indirectly and should be read that way.

Residue banked: 22.7 token releases against 27.8 presented -- five flips
per 64 vints present without producing a token release.

---------------------------------------------------------------------
## 142. Mike's instinct, read against the bytes: the 68K's WAIT is not free. The spin at 0x3982 holds the MD bus ~91% of the time doing nothing, and STOP #$2000 fits in six bytes (2026-09-14)

Mike: *"I genuinely think we can start our own timing sequence if we
know the sequence we're waiting on... if our stall is simply waits. Like
CMON. this hardware is predictable"* and *"or at least an aggressive
fire with a feedback call that says done/next."*

Two answers. The first is that we already do this and it works. The
second is that reading the wait instruction turns his premise inside
out, and it is the better finding.

### The part that is already proven in our own tree

Open-loop timing, no handshake, is in the code twice:

  * the vblank **edge guard** is 1650 FRT ticks (~36 lines) of pure
    arithmetic -- no rendezvous with anything;
  * **DRAIN_CUT** (m_main.c 7718-7726) already argues Mike's exact case
    in its own comment: the drain *"cannot be shortened into the budget,
    so it does not belong in the flip path at all: take DRAIN_CUT pages
    here and leave the rest to the body, which has the whole active
    display."*

And "aggressive fire with a done/next feedback" is `GAMEGATE_MAXWAIT`:
fire on schedule, backstop the exception. The limit on pure open-loop is
the 68K's pass LENGTH, which is bimodal (147 lines light / 185 heavy,
with a minority arriving 175+ lines late, LOOP29 291) -- so a fixed
schedule serves the common mode and needs a backstop for the rare one,
which is what we have.

### The part that inverts the premise

The premise is "our stall is waits, and waits are schedulable." Reading
the wait says the waits are not the passive thing that word implies.
Entry 67 has the four instructions, and the addressing is absolute
SHORT, which is why they pack into ten bytes:

    397e:  4238 F01C   clr.b $F01C.w      (4)  discard any pending release
    3982:  4A38 F01C   tst.b $F01C.w      (4)  <- THE WAIT
    3986:  67FA        beq.s 0x3982       (2)
    3988:  51C8 FFF4   dbf   d0,0x397E    (4)
    398c:  4E75        rts

**That two-instruction loop is a bus hog.** On a 68000 with no cache,
every iteration is `TST.B abs.w` (12 cycles, 3 bus accesses -- two
instruction words and the operand) plus `BEQ.s` taken (10 cycles, 2
prefetch accesses). **Five bus accesses at 4 clocks minimum = 20 of the
~22 cycles. The 68K holds the MD bus ~91% of the time while doing
nothing at all**, for the whole span between finishing its pass and the
next release -- roughly 77-115 lines of every 262 at the measured pass
lengths.

Every master read that crosses to MD-side memory arbitrates against
that. And under DRAIN_CUT the bulk of the truth drain was deliberately
moved into the body -- which is exactly the span the 68K spends
spinning. **We moved the drain into the contention.**

### The patch, and it fits exactly

`STOP #$2000` halts the 68000 with **zero bus cycles** until an
interrupt. Both release sites already live in IRQ4, so a release can
only arrive at a vint anyway -- nothing is lost by sleeping between
them. The spin is 6 bytes at 0x3982-0x3987 and `JMP abs.l` is 6 bytes,
so it redirects cleanly to a stub in free ROM:

    3982:  4EF9 xxxxxxxx   jmp stub          (6, exact fit)

    stub:  4E72 2000       stop #$2000       wake on IRQ4, no bus
           4A38 F01C       tst.b $F01C.w
           67F8            beq.s stub
           4EF9 00003988   jmp 0x3988        back into the dbf

`dbf d0,0x397E` still runs, so the multi-frame waits (`moveq #N`) are
unaffected. This is squarely inside Mike's 2026-09-10 "never the 68K"
pivot -- it patches a program gate, which is the sanctioned lever.

**Three gates before anyone builds it:**

  1. **STOP is privileged.** If the main loop runs in user mode it traps
     instead. S16B games are overwhelmingly supervisor-resident but this
     must be READ, not assumed -- check the SR along the path to 0x397E.
  2. **`#$2000` sets S=1, mask 0.** That enables every level. Confirm
     the loop's normal SR: if the game deliberately masks anything at
     0x3982, the immediate must match it, not clear it.
  3. A spurious interrupt wakes the STOP early, the `tst` fails, and it
     re-sleeps. Harmless, but it means the stub must loop, not fall
     through -- as written above.

### The disambiguation, which costs nothing because the build is queued

Entry 140's isolation build (MAXWAIT=4, 68K released at the compose rate
instead of every vint) now answers TWO questions, because halving the
releases makes the 68K **work less and spin MORE**:

    v/gen DROPS -> the 68K's WORK is what contends. STOP is a minor card.
    v/gen RISES -> the 68K's SPIN is what contends, and STOP is the
                   largest cheap lever left in the project.
    v/gen FLAT  -> the 68K is off the SH-2's critical path entirely and
                   the remaining 1.5-1.8 vints is master traffic alone.

That third outcome was the only one I had framed. The rise case is new
and it is the one Mike's instinct predicts.

---------------------------------------------------------------------
## 143. Mike's cache idea, checked: we have 2KB of zero-wait on-chip RAM we have NEVER enabled, and the 68K already has the cheap path -- we just never moved the capture onto it (2026-09-14)

Mike: *"if we Have a CACHE we can fill, thats even better because we can
fill a cache, and the CPU on the 68K can drain it by sending all the
work to the VDP."*

The 68K half of that is wrong as stated -- a 68000 has no cache, and MD
VDP DMA moves data into VRAM, which is not where our problem is. But
both halves point at something real and I checked both.

### CARD T2: the TW bit, defined and never used

`sh_src/mars.h:104` defines `SH2_CCTL_TW = 0x08` and **nothing in the
tree ever sets it.** Boot writes CCR = 0x11 (`mars_start.s` 428-430,
`mov #0x11`) = purge + enable, and `cache_purge()` (m_main.c 1966-1968)
rewrites the same `CP | CE`. So we run the SH7604 cache in plain 4-way
mode.

On the SH7604, **TW switches two of the four ways into 2KB of on-chip
RAM at 0xC0000000, zero wait states**, leaving 2KB as a 2-way cache.
That is 2KB of guaranteed-no-stall memory per SH-2 that this project has
never touched -- and the master's problem, by entry 141, is that
1.5-1.8 vints of every generation is memory stall.

The obvious tenant is already sized and already hot:

    cache_tag   1KB   (m_main.c 337, CSETS*NWAYS words) at 0x0603A800 -- SDRAM
    cache_rot   128B  (m_main.c 506) at 0x06028880 -- SDRAM

Both are read and written all through compose, both are pure
bookkeeping, and both currently occupy real cache lines that the tile
data wants. 1.1KB of the 2KB, with room left.

**The trade is real and must be measured, not argued:** the cache drops
4KB -> 2KB, which could cost the compose inner loops more than the
scratchpad saves. **ares cannot rank this** -- it models no data cache
at all (memory: ares charges instruction cycles only), so an ares
A/B would show the pointer change and none of the effect. **Rig only.**

### CARD T3: invert the text capture onto the path the 68K already has

The 68K cannot cache, but it does not need to. The repo already measured
its cheap path: **a 68K word into the DREQ FIFO costs ~2.4 lines on
hardware; into the FB, ~0.1** (md_main.c 3556-3561). That is a 24x gap
and `FB_XPORT` already ships on it -- the packet goes 68K -> FB at FM=0
before the post, and the MiSTer base is built with FBXPORT=1.

**So the producer already has a cheap write path. What has never been
moved onto it is the CAPTURE.** Today the master READS 928 longwords of
MD text RAM across the bus every generation, arbitrating against a 68K
that (entry 142) holds that bus ~91% of the time spinning. Card O masked
those reads and produced the largest hardware movement of the arc,
18 -> 21-27 presented per 64.

But entry 118 measured the thing that makes the mask look timid: **the
game changes ZERO text words on 89-96% of frames.** So if the 68K writes
its changed text words into the FB during its own pass, at 0.1 lines a
word, then on nine frames in ten it writes nothing and **the master
reads nothing at all** -- not a mask, not a compare, nothing. The
cross-bus read disappears rather than shrinking.

That is strictly better than the mask, and on the same transport that
already carries the packet.

Unknowns to settle before it is a card rather than an idea:

  1. **Does the 68K have FM=0 time to do it?** The push already sits at
     FM=0 before the post; the text writes would extend that span.
  2. **Ordering.** The 68K's text writes happen throughout its pass; the
     FB push is one point in it. Either the game's text writers are
     patched to write both places, or the pass tail diffs and emits --
     and entry 122's writer census (two pointer-stash sites) is what
     decides which.
  3. 68K FB writes are dropped at FM=1 (memory: flip-latch-fm-hold), so
     the emit window is hard-bounded.

### Why both of these sit behind the queued build

Entry 142's isolation build tells us whether the 68K's WORK or its SPIN
is what contends. T3 only pays if cross-bus traffic is the wall at all,
and T2 only pays if the master's own stalls are. The build separates
them. Neither should be built first.

---------------------------------------------------------------------
## 144. Mike's pipelining idea is already SHIPPING -- and that is the finding, because it means the latency era is over and every live card is now a traffic card (2026-09-14)

Mike: *"if we can always generate two frames, then we send two and the
next is ready the moment the sent/confirm byte is noticed... that way
its just FAST FAST."*

**It is built, it is named, and it is on the ship line.** m_main.c 1087
declares `nat_gen_ready` -- *"closed generation awaiting its blit"* --
and 1088-1097 describes exactly Mike's shape: under `BLIT_CHASE` *"the
launch may open a generation over a READY one because the fence
(SYNC[14]) orders the slave behind the blit."* The Makefile calls it the
pipelining arc:

    LAUNCHEARLY=1   step 1: launch before apply_cram/publish/ack instead
                    of after. "Byte-identical picture; the compose gets
                    the window tail back."  (Makefile 2158-2160)
    BLITCHASE=1     step 2: post the slave's blit half before the
                    pre-ack work, launch, THEN blit the master's half.
                    (Makefile 2164-2166)

Both are in `SHIP_COMMON` (Makefile 2745). Every build in this arc,
including bldS, already generates over a ready generation.

### Why that is worth an entry rather than a one-line "already done"

It closes the loop on three separate measurements that have each looked
like a puzzle:

  * the master never waits for the slave (zero ticks, LOOP29 291);
  * master idle is at most 0.34 v/gen in ares and zero on the rig
    (entry 141);
  * gens 28.8 against presented 27.8 -- 96.5% of launches present
    (entry 140).

Those are not three coincidences. **They are what a FULL pipeline looks
like.** The overlap Mike is describing is already extracting every gap
there was to extract, which is precisely why there is no idle left for a
further stage to hide.

**And that is the structural fact this project should now be run on:
pipelining converts idle into work. We have no idle. So no further
rearrangement of time can pay, at all, ever, until traffic comes down.**

### The consequence for the card list

Read the three live cards against it and they all say the same thing:

    Card T   (STOP #$2000)     removes the 68K's ~91% MD-bus occupancy
    Card T2  (SH2_CCTL_TW)     removes SDRAM accesses (2KB on-chip RAM)
    Card T3  (invert capture)  removes 928 longwords of cross-bus read

**Every live card removes traffic. Not one of them rearranges time.**
That is the signature of a project that has finished its latency work,
and it is the first time in this arc the card list has been coherent in
that way. Entry 141 reached the same place by arithmetic (the wall is
master-gated and 1.5-1.8 vints of it is memory stall); this reaches it
by inventory.

### The one stage that genuinely has not been tried, and why it still fails

Triple buffering. We are NOT hardware-limited to two: the compose target
is `sbuf` in SDRAM (entry 135), not a 32X framebuffer bank, so a third
buffer is a memory question rather than a silicon one.

It still does not pay. A third stage absorbs QUANTISATION loss -- a
generation that finishes just past the 35.9-line flip window and waits a
whole vint (LOOP28 107). Entry 140 measured that loss at **1 generation
per 64 vints** (28.8 launched, 27.8 presented). There is nothing there to
recover, and a third sbuf costs SDRAM traffic to clear and fill, which
is the exact resource we are short of. **Triple buffering would make it
worse.** Logged so nobody re-proposes it.

---------------------------------------------------------------------
## 145. The MAXWAIT=4 null does NOT retire the 68K, because the independent variable never moved: a cacheless 68000 holds the MD bus at ~91% whether it is WORKING or SPINNING. And the whole generation is outside the stamped region (2026-09-14)

LOOP29 294: the flag is `GAMEGATEWAIT` (`-DGAMEGATE_MAXWAIT=1`,
GATE_FREE not compiled), so entry 140's 36.2 prediction landed on the
measured 34.1 and the counter's label was the only thing wrong. Good.

The ablation: MAXWAIT=4, rig, attract, 40 shots each.

    per 64 vints        line (MAXWAIT=1)   ablation (MAXWAIT=4)
    generations             28.8               30.7
    68K releases            56.8               26.7
    GAMEGATE fallbacks      34.1                0.2
    frames presented        27.8               26.0

The flag worked -- fallbacks collapsed, the 68K's advance rate halved --
and the compose rate did not move. The builder's conclusion: *"The 68K is
free. The arbitration-pressure card is dead before it was written."*

**That conclusion does not follow, and the reason is the same instrument
fault this session has caught four times: the variable that was changed
is not the variable in the hypothesis.**

### The confound

The hypothesis is that the master's cross-bus reads arbitrate against
the 68K's **bus occupancy**. MAXWAIT=4 changed the 68K's **release
rate**. Those are only the same quantity if a released 68K uses the bus
and an unreleased one does not.

**It does not work that way, and entry 142 has the arithmetic.** The
68000 has no cache, so every instruction fetch is a bus cycle. The wait
loop at 0x3982 is `TST.B abs.w` (12 cycles, 3 accesses) plus `BEQ.s`
taken (10 cycles, 2 accesses) -- **five accesses in ~22 cycles, ~91%
occupancy.** Game-pass code is a mix of register and memory work and
sits in the same band, plausibly a little lower where an instruction has
internal cycles to burn.

So halving the releases did not halve the 68K's bus occupancy. **It
swapped 68K work for 68K spin, and both saturate.** If anything the
ablation raised occupancy slightly, since the spin is the tightest loop
in the program. The independent variable moved by a few percent, not by
half, and a flat result across a few percent is exactly what a null
measures when nothing was varied.

**Consequence: the null is real but it bounds the wrong thing.** It
bounds the effect of the 68K's RELEASE RATE on compose, and that is
genuinely zero and worth banking -- the game can advance at 60 Hz for
free, which retires any worry that GAMEGATEWAIT=1 is costing us. It does
not bound arbitration at all, and the builder's "at most a quarter of
the stall is 68K arbitration" inherits the same fault.

**Card T survives, and it is now the ONLY clean test of the hypothesis**,
because `STOP #$2000` is the one change that takes 68K occupancy from
~91% to ~0 rather than moving it between two saturating modes.

### The second thing, which may be larger

The builder notes the drain stage costs 25.0 lines in ares and
essentially ZERO on the rig (LOOP29 291: post seen 22.3, at the guard
22.3, the drain sitting between them) and offers it as an instrument
gap. It may not be one. `DRAINCUT` is **not** on the ship line, so
m_main.c:7728's `cap_drain(13)` -- *"ALL of it -- correctness"* -- is
what runs, and entry 115 measured the map needing **0-2 new tile codes a
vint, p90 zero.** A full-budget drain with nothing pending costs nothing.
So "~0 on the rig" and "25.0 in ares at 9.23 pages a flip" are probably
not the same condition measured twice; they are a quiet vint and a busy
one. Neither number should be used until that is settled, and the rig
stamps are one sample each by the builder's own caveat.

### Where the missing time actually is, and it is not subtle

Everything either thread has stamped lives inside the V-ISR, and LOOP29
291 puts the master's whole pre-flip life at ~22 lines. **A generation is
2.2 vints = ~577 lines. The stamped region is under 4% of it.**

We have spent this arc decomposing the 4% and inferring the 96% by
subtraction. The ~1.1 v/gen that entry 141 could not name is not hiding
-- it is in the part of the generation nobody has ever instrumented.

**That is the next instrument, and it needs no new channel:** the same
STAMP_CENSUS technique the builder has already validated twice, applied
to the BODY -- the compose path between windows, where `cap_drain` has a
second site at m_main.c:13413 and where the blit, the tile cache fills
and the sbuf work all live.

### Banked without argument

  * The 68K's release rate does not cost compose. The game can run its
    logic at 60 Hz for free. GAMEGATEWAIT=1 is not a tax.
  * Tearing is untouched by this build -- it changed the writer's rate,
    which is the confound and not the control. Correct, and it means the
    staging-vs-drain race is still open.
  * LOOP29 275 annotated as an instruction-count ranking and never a
    critical path. That is entry 141's rule and it is now in both logs.
  * The rig is back on bldS, the line, off the gw4 diagnostic.

---------------------------------------------------------------------
## 146. CARD T's gates, read off the program: privilege CLEARED, and the immediate in my own card was WRONG -- it is STOP #$2300, not #$2000. Also: the wait has 34 call sites, not 11 (2026-09-14)

The builder said yes to the SR read. Done, off
`srcref/alteredbeast/Disassembly/altered_beast.asm`.

### Gate 1 -- privilege: CLEARED

**Every write to SR in the entire program is `move #$2700,sr` or
`move #$2300,sr`.** Sixteen sites, listed:

    2700:  0x412  0x4A4  0x2E08  0x3372  0x3BEE  0x1B834
    2300:  0x552  0x5C2  0x62E  0x1E5E  0x2E14  0x337E  0x3BFA  0x1AFE4

Plus four `move sr,dN` READS (0xFB92, 0x1064A, 0x10FB6, 0x13EA4), which
cannot change mode. There is **no `move <ea>,sr` with a computed value,
and no `andi`/`ori`/`eori` to SR anywhere in the program.**

Both written values have bit 13 set. The 68000 boots supervisor and
**this program never leaves supervisor mode.** `STOP` is legal at
0x3982. Gate 1 is closed by exhaustion, not by assumption.

### Gate 2 -- the immediate: my card was wrong

NOTES 82 specified `STOP #$2000`. That is mask 0, which enables every
interrupt level. **The program's running state is `#$2300` -- mask 3 --
which masks levels 1-3 deliberately and permits IRQ4 and above.**

`#$2000` would hand the game three interrupt levels it spends sixteen
instructions keeping masked. **The correct immediate is `STOP #$2300`.**
It keeps S set (so no privilege violation), it keeps the game's own
mask, and it still wakes on IRQ4, which is the only thing that needs to
wake it.

That is the whole value of reading a gate instead of assuming it.

### Gate 2b -- can the wait be reached with interrupts OFF?

`STOP` loads the immediate into SR, so the stub is self-correcting
whatever the caller's SR was -- but that means it must never be reached
from a deliberate interrupts-off region, or it would silently re-enable
them. Checked exhaustively:

  * The four `#$2700` critical sections in the running program are 12
    bytes long (0x2E08->0x2E14, 0x3372->0x337E, 0x3BEE->0x3BFA) -- three
    instructions, no room for a `jsr`.
  * 0x412->0x552 is the boot disable; the lowest call site is 0x640.
  * 0x1B834 disables and is a decompressor; **no call site lies in
    0x1B800-0x1B9FF.**

**None of the call sites lies inside an interrupts-off region.** Gate 2b
closed.

### Gate 3 -- spurious wake

Unchanged and already correct: the stub must loop back to the STOP, not
fall through. As written in NOTES 82.

### CORRECTION: 34 call sites, not 11

Entry 67 reported 11. The disassembly's xref list gives **34**:

    0640 08D6 0904 0922 0946 0ADC 0B8E 0BA0 0C30 0C64 0C7C 0C8A 0CCE
    0CFA 0D02 1620 1E7A 1F4E 1F6E 2024 203E 2050 207C 220C 2228 225A
    468A 5DF6 90FE 923E 1A40E 1A480 1A4A8 1A4BC

All of the form `jsr (countdown_a0_loops).l`. This is the game's
**universal frame wait** -- boot, attract, and gameplay all funnel
through it. That cuts both ways and both should be said: patching one
routine covers every wait in the program, and patching one routine puts
every wait in the program at risk.

### The corrected stub

    3982:  4EF9 xxxxxxxx   jmp stub               (6 bytes, exact fit)

    stub:  4E72 2300       stop  #$2300           wake on IRQ4, no bus
           4A38 F01C       tst.b $F01C.w
           67F8            beq.s stub
           4EF9 00003988   jmp   0x3988           back into the dbf

One residue for the builder rather than for me: **`STOP` is an unusual
instruction and the rig is an FPGA 68000 core, not a real one.** It is
cycle-accurate by reputation and FX68K implements STOP, but no build of
ours has ever executed one. It wants a smoke test before it wants a
measurement.

### On sequencing: body stamps first, and I would not reverse it

Card T patches a routine with 34 call sites -- the largest blast radius
of any card in the arc. Its payoff is also unknown until we know where
the ~1.5 v/gen lives: if the body stamps put the time in the blit (SDRAM
and FB writes, no MD-side reads at all) then Card T cannot help and the
patch was spent for nothing.

Body stamps are pure instrumentation with no risk to the line, and they
are the thing that tells us whether Card T is aimed at anything.
Measure, then patch.

---------------------------------------------------------------------
## 147. CARD U, the DEADLINE FLIP: present on a fixed cadence and let the frame tear rather than drop it. We already have evidence of how partial presentation looks -- ROWDEFER is ON the ship line and its artifact is one of Mike's boxed defects (2026-09-14)

Mike: *"do we have any sense of pushing frames without waiting for
timing from the master?"*

The idea: flip on a fixed 2-vint cadence unconditionally. If the
generation is not finished, present what exists. Cadence locks, judder
goes, and the cost is a tear instead of a dropped frame.

This is the right shape for our situation and entry 144's rule does not
forbid it -- it removes no traffic, but it does not try to: **it attacks
VARIANCE, which entry (the PM readout) identified as the actual barrier
to a locked 30, and which no card in this arc has touched.** Every card
so far chased the mean.

### We already ship a version of it, and it is documented as superseded

`ROWDEFER=1` is in `SHIP_COMMON` (Makefile 2763). The Makefile's own
note on it (2216-2225) reads:

    "re-arm the row-defer ship gate (historical). It killed the purple
     band on 2026-08-25, but the BG backstop then fixed the purple at
     the ROOT, and the gate's cost surfaced in Mike's corpus 2026-08-26:
     ~26 deferred rows/frame ship TWO-frame-stale -- the thin displaced
     strips he boxed in frame 800. With the gate off: purple still 0
     (backstop holds), BAD1 best-ever 160. Superseded; off by default."

**So the flag is documented as superseded, with a measured cosmetic cost
Mike himself boxed, and it is explicitly ON in the ship line.** The
`ifndef` at 2223 means `ROWDEFER=1` suppresses `NO_ROW_DEFER`, so the
gate is live. Either it was deliberately re-armed for a reason not
written down, or it is a flag that survived a revert. **That is a
question for the builder and it is worth asking regardless of Card U**,
because it is a cosmetic cost we are paying against a root-cause fix
that the note says already holds.

### What ROWDEFER tells us about Card U, and it is encouraging

ROWDEFER is partial presentation in the WORST possible shape:
**~26 stale rows SCATTERED through the frame.** Scattered staleness
reads as "displaced strips" -- obviously broken, and Mike caught it
immediately in a still.

Card U is partial presentation in the BEST possible shape: **one
CONTIGUOUS boundary** at whatever row the blit reached. Everything above
is current, everything below is exactly one frame old. On a 30 Hz
decimated stream of a slow-walking game, one frame of staleness below a
stationary seam is close to invisible -- and the seam is stationary
because the same scene produces the same workload vint after vint.

So ROWDEFER does not condemn Card U. It argues that if we do this, it
must be **contiguous, not per-row.**

And the rows below the seam are not garbage: `BLITSKIP` measured 69.9%
of bytes skipped because they are unchanged, so an unfinished blit
leaves content that is one frame stale, not undefined.

### The constraint that decides whether it is buildable

**If we flip mid-blit, the blit keeps writing into the bank that is now
being DISPLAYED.** That turns a stationary seam into a moving tear,
which is far worse than the dropped frame we are trying to avoid. So
Card U needs the blit to be flip-aware: either it aborts at the flip, or
it completes into the correct bank.

The machinery may already be close. `BLIT_CHASE`'s SYNC[14] row fence
already orders the slave behind the blit, and `nat_skip_run` already
counts consecutive overrun windows. Whether there is a row watermark the
ISR can read at flip time is the builder's to answer.

### The number that decides whether it is WORTH building, and it is free

Card U pays if overruns are small: a generation 10% past 2 vints tears
near the bottom of the screen; one 50% past tears across the middle.

**That distribution comes straight out of the body stamps already being
run.** No new probe. When they land, the histogram of generation length
against the 2-vint boundary answers it directly:

    most overruns small  -> seam sits low, Card U is nearly free
    most overruns large  -> seam crosses the action, not worth it
    bimodal              -> the two populations tear in two places,
                            which crawls, and it is not worth it

Given the builder has measured bimodality on two separate quantities
this arc, the third outcome is a live possibility and should not be
assumed away.

---------------------------------------------------------------------
## 148. RETRACTION: I called 1.57 a "protocol floor" and treated it as a law. It is an UNDECOMPOSED RESIDUAL, and the one distribution we have says the wall may be a rare catastrophic stall rather than a throughput ceiling (2026-09-14)

Mike: *"I'm VERY hesitant to accept 30 as our limit just because we have
inefficiency in our architecture."*

He is right and my PM readout was wrong in a way worth writing down.

### What I did wrong

I reported card F0's 1.57 v/gen as a "protocol floor" and derived a
~38 fps ceiling from it, then built a two-project plan on that ceiling.
**1.57 is a measured residual that nobody has ever decomposed.** It is
what was left when compose was ablated. Nothing about it has been
attributed to a mechanism.

A residual you cannot explain is not a limit. It is a bug you have not
found yet. Treating it as a law was the same error I have been
correcting in the builder all session -- accepting a number without
checking what quantity it measures.

### There is no hardware law here at all

Worth stating because it is the actual physics and it is easy to lose:
**the 32X VDP scans the framebuffer out continuously whether we touch it
or not, and a flip is ONE register write.** The hardware imposes no
per-frame cost on presentation. Every one of our 2.2 vints is our own
software. There is no silicon reason 60 is unreachable.

### And the one distribution we have points at a rare stall, not a ceiling

LOOP29 291, the master's pre-flip stamps:

    post seen        mean 22.3 lines
    at the guard     mean 22.3 lines     (drain + capture ~free)
    guard MAX        [63,63,63,33,63,44] -- 63 is SATURATED, >= 175 lines

The edge guard's budget is 1650 FRT ticks = ~36 lines. **A typical vint
reaches the flip point at 22 lines, comfortably inside a 36-line budget.
The maximum saturates the counter at 175+ lines -- five times over, and
we do not know how far because it pegged.**

The builder's own reading: *"Two populations, not a spread -- which is
the signature of waiting on a deadline, not of doing work."*

**If typical generations make the window easily and a subset blows it by
5x, then the wall is not the cost of a frame. It is whatever makes the
subset explode.** That is a bug-shaped problem, not a ceiling-shaped
one, and it is exactly what Mike is refusing to accept as a limit.

### The honest caveat, which is why this is a direction and not a finding

**The 22.3 means are ONE SAMPLE EACH** -- the builder flagged this and it
matters here more than anywhere. Six samples of a saturating max and one
sample of a mean cannot give a distribution, and **the distribution is
the entire question.** We do not know whether 5% of generations blow the
window or 57% of them.

The arithmetic that says we cannot assume the happy reading: we present
27.8 of 64, so **57% of vints do not flip.** If the mean really were 22
lines against a 36-line budget, far more than 43% should make it. So
either the single-sample mean is unrepresentative, or a large fraction
sits in the slow population. Those two readings have completely
different consequences and nothing we own separates them.

### What this does to the plan

The "protocol redesign" project I named in the PM readout is premature
and possibly nonexistent. It is replaced by one probe:

**Decompose the 1.57.** Run the SAME body stamps the builder is starting
against an ablated build, and report the DISTRIBUTION of generation
length -- not the mean. Specifically:

    1. what fraction of generations miss the 36-line flip window
    2. for those that miss, by how much (an unsaturated counter -- the
       current one pegs at 63 and that is now the binding limitation)
    3. what is executing during the slow population

Until (2) has an unsaturated counter we are guessing about the most
important number in the project.

**And the 60 Hz question is reopened.** If the slow population is a
minority with a nameable cause, fixing it locks the cadence without
making anything faster, and the ceiling I quoted does not exist. I
should not have quoted it.

---------------------------------------------------------------------
## 149. THE 60 Hz ARCHITECTURE ALREADY EXISTS AND IS ~80% SHIPPED. This whole arc has been optimising the software renderer that the pivot DELETES, and the last slice is blocked on a colour defect this thread has spent the arc mapping (2026-09-14)

Mike: *"I think you are doing too much validation instead of
architecting an answer that simply FEEDS FRAMES."*

He is right, and the correction is sharper than he put it: **the
architecture is written, it is verified in slices, most of it is on the
ship line, and this arc has been shaving the part of the renderer that
the remaining slice removes.**

### The arithmetic that condemns the current direction

12,181 bytes are actually written to the FB per frame (entry 136). A
generation costs 2.2 vints = ~843,700 cycles across both CPUs.

    843,700 cycles / 12,181 bytes = 69 CYCLES PER BYTE WRITTEN

A write costs 2-8. **We are spending 10-30x the store cost on deciding
and fetching what to store.** Entry 137 reached "not stores" by
arithmetic; this is the same fact stated as a ratio, and it says the
renderer's cost is per-pixel WORK, not per-pixel traffic.

You cannot shave 69 down to 10 by removing round trips, widening a
counter, or re-timing a handshake. **Every card in this arc -- T, T2,
T3, U -- attacks the 10, and the 59 is the per-pixel decision the pivot
deletes.**

### What ARCHITECTURE.md Section 4 already says, and it fits in one vint

    "residual 32X pass  ~= 0.26 of a screen   against a 1.6-pass budget
     leaving            ~= 1.3 passes for sprites
     That is the whole point of the exercise, and it fits."

The FG layer splits: FG cat-1 (priority) tiles stay in the 32X
framebuffer painted over the sprites; the MD VDP draws FG cat-0 plus the
whole BG. Measured demand: 307-340 priority tiles of 1189 visible, ~26%
of the screen. The blocker is priority depth (S16 interleaves ten deep,
`jts16_prio.v:84-95`, against the 32X's one boundary), NOT colour --
peak demand is 21 distinct sets against an MD capacity of 8, a 2.6x
grind and not a 32x wall.

### And most of it is already on the ship line

    MDBGALL=1   BG on the MD plane
    MDSPR=1     mob-class sprites as MD HARDWARE sprites --
                "claimed records never touch compose or the FB"
    MDSTATIC=1  per-scene static MD pen tables, pinned against eviction
    PENMATCH=1  FB groups painted with the MD's quantised pens

**The pivot is not a proposal. It is ~80% shipped.**

### The missing slice, and what actually blocks it

`CAT1MD` is NOT on the ship line. The Makefile (2181-2187):

    "C1 step 1: FG cat-1 cells are emitted on MD plane A with the
     priority bit instead of blanked. Pixel-neutral by construction
     while the FB keeps composing cat1; step 2 restricts the FB cat1
     pass to SH-2 sprite rows/strips (THE 0.44 V/GEN SLAVE LEVER)."

Step 1 was built, **passed on stills, FAILED Mike's play pass on
2026-09-07 (shimmer, transform palette), and was reverted.**

**So the last architectural cut in the project is blocked on a COLOUR
defect, not on a performance question or a design unknown.** 0.44 v/gen
is the named figure for step 2 alone; Section 4's design arithmetic
promises considerably more and is the thing that actually reaches the
bar.

### The part that makes this actionable rather than a lament

**The colour work this thread has done all arc is the groundwork for
exactly that shimmer.** `docs/audit/round_sets_definitive.txt` -- the
per-round set lists walked cell by cell at f700, replacing the sampled
census that was taken during the scene load and was wrong -- plus the
MDP_LINES colour-to-line bake and the cycled-set findings (entries
123/125/129) are precisely the data CAT1MD needed and did not have on
2026-09-07.

CAT1MD failed against a colour model we have since proved wrong and
rebuilt. **It has never been tried against the corrected data.**

### The recommendation

Stop shaving the software renderer. Re-open CAT1MD step 1 against
`round_sets_definitive.txt` and the corrected per-round bake, gate it on
Mike's play pass as before, and if it holds, build step 2.

The measurement work is not wasted -- the body stamps still tell us
where the residual lives and the widened counter is still needed. But it
is no longer the main line. **The main line is finishing the pivot.**

---------------------------------------------------------------------
## 150. THE 2.9x TAX IS THE PROJECT. It is uniform across every stage, it matches F0's independent 2.8x, and a uniform multiplier on WORK has one obvious candidate mechanism ares cannot model: INSTRUCTION FETCH, amplified by a full cache purge every window (2026-09-14)

LOOP29's body stamps, and the builder's prediction was overturned by
them -- which is the finding:

    stage        ares    rig    factor
    window       14.0    38.0   2.71x
    ship          8.0    16.0   2.00x
    maps drain    4.0    10.0   2.50x
    residual      4.0    22.5   5.62x
    GENERATION   30.0    86.5   2.88x

**Every stage inflates by roughly the same factor and the shape is
preserved. Nothing is hiding.** And it lands on card F0's independently
measured 2.8x for the pipeline with compute removed -- two unrelated
measurements of one tax.

The builder's consequence is right and should be carried: **no re-timing
card can win, because there is no stage to move out of the way when
every stage pays the same multiplier.** That retires Card U's throughput
case (its variance case survives) and it retires re-timing generally.

### What a UNIFORM multiplier actually implicates

The builder calls it *"a broad memory-stall cost proportional to work
done."* True as far as it goes, but the uniformity is the clue and it
narrows things further than that.

These stages do very different memory work. `ship` is the blit -- almost
pure writes. `maps drain` is almost pure reads. `window` is mixed
compose. **If the tax were DATA traffic, stages of different data
intensity would inflate differently. They do not -- 2.0x, 2.5x, 2.7x
against a 2.9x whole.**

The one cost that scales with *work* rather than with *data*, applies
identically to a write loop and a read loop, and is charged at **exactly
zero** by our reference instrument, is **INSTRUCTION FETCH.** ares
charges SH-2 instruction cycles only; it does not charge fetching the
instruction. On hardware every instruction is fetched, and a fetch that
misses costs an SDRAM line fill.

### And we purge the entire cache every window

`cache_purge()` appears at **9 sites** in m_main.c, and the coherency
design has it running at least once per window -- m_main.c 656-658 states
it plainly: *"The slave purges its cache at every window start, the
master's writes are write-through."*

A full purge invalidates all 4KB. **Every instruction of the compose
path is then a cold fetch until the loop re-warms, once per window, for
the life of the frame.** That is a uniform multiplier on work, applied
to read loops and write loops alike, invisible to ares -- which is
precisely the shape of the measurement.

**This is a hypothesis with a named mechanism, not an attribution.** It
could also be SDRAM bandwidth that ares prices at zero, in which case
the uniformity is coincidence. But it is the first candidate that
explains why the factor does not vary with what the stage does.

### The test, and it is cheap

Two builds, no new instrument -- the body stamps already in the tree
read it directly:

  1. **Purge less.** The purges exist for coherency against data the
     slave and 68K write. The tree already has the finer tool: shared
     data read through the UNCACHED alias (0x26xxxxxx) and never
     purged, with code and read-only art staying cached permanently.
     C1_CACHED (m_main.c 651-659) is this argument already made in one
     place. If the stage factors fall, it is fetch.
  2. **If they do not fall, it is data bandwidth**, and the pivot is
     the only lever left.

### What this does to the standing cards

  * **Card U's throughput case is dead.** Its variance case is not.
  * **Card T's prediction sharpens into a discriminator.** If the tax is
    MD-bus arbitration, taking the 68K off the bus moves EVERY stage
    uniformly. **If the tax is SH-2 instruction fetch, Card T moves
    NOTHING**, because fetch is SDRAM-side and the 68K is not on that
    bus. One build now separates two hypotheses.
  * **Card T2 flips sign and may be the strongest card in the set.**
    I sized it as "2KB of scratchpad for cache_tag." If the tax is
    fetch, the right tenant is not data at all -- it is **the compose
    inner loop's CODE, in on-chip RAM that no purge can invalidate and
    no fetch can miss.** That is a different and much larger card than
    the one I wrote.
  * **Entry 149's recommendation survives and is strengthened.** A 2.9x
    tax multiplies whatever work exists, so removing 74% of the work
    (the pivot) removes 74% of the tax with it. The two levers compose.

### The arithmetic that puts 60 back in range

    today                            2.22 v/gen
    tax 2.9x -> 1.5x                ~1.15 v/gen
    plus the pivot's work removal    below 1.00

**That is the first credible path to the bar in this arc**, and it needs
both levers, not either one.

### Banked from the same report

  * **17% of the period is unstamped**: NAT_WALL[0] is launch->close at
    1.84 v/gen against a 2.22 period, so **0.38 v/gen sits between a
    close and the next launch with no stamp on it.** Cheapest unexamined
    thing in the tree and it should be closed before anything is sized.
  * Sample honesty accepted: n=5-6, factors good to ~±0.5, and the
    residual's 5.62x is a difference of noisy medians. **Not treating it
    as a real outlier**, which also means the uniformity claim above
    rests on the other three, and they agree.

---------------------------------------------------------------------
## 151. The fetch/data split HAS a live-machine test: the SH7604 CCR carries ID and OD bits that disable instruction-fill and data-fill SEPARATELY while leaving hits working. Derived from the RTL. And NOTES 88 withdrawn -- CAT1MD step 1 already ships (2026-09-14)

### First, withdraw NOTES 88

The builder: CAT1MD step 1 is already on the line (fold 1, commit
43ef418, 2026-09-12), `-DCAT1_MD` is in bldS's flags, and **Mike passed
bldS on 2026-09-14.** Nothing to rebuild. My premise was wrong.

And my diagnosis of the 2026-09-07 shimmer was wrong too: LOOP29 150/151
put it on **step 2's moving renderer boundary** -- FB cat1 over sprite
rows, MD plane A elsewhere, the same tile rendered in 5-bit and 3-bit
with the boundary tracking the sprites. **Corrected colour tables do not
touch that.** The transform palette was cleared separately and is fine
on the accepted base.

So the live question is step 2 only, and LOOP29 151 already names the
fix: one renderer per tile per scene, or two made pixel-identical. That
is a design item, not a data problem.

### Second, the gap: closed, nothing there

BODYGAP: close->launch **0.25 v/gen, 10-13% of period**, n=5, range
9-15 -- the tightest spread of any tag this arc. Wall ~2.23 against a
period of 2.1-2.4. **The wall IS the period; there is no dead time
between generations.** The builder's earlier 0.38 mixed a gen count from
one run with a median from another. Closed.

### Third, and this is the contribution: the fetch test exists

The builder's position after three failed attempts: *"the only direction
that leaves the machine alive is adding cached traffic."* Their extended
rule is right -- an ablation must move the hypothesis's quantity AND
leave the machine doing comparable work -- and CACHEOFF failed the
second half so hard that the master could not close a generation at all.

**But removing the cache is not the only way to move instruction fetch,
because the SH7604 separates the two streams in hardware.**

From the RTL, `srcref/S32X_MiSTer/rtl/SH/SH7604/SH7604_pkg.sv:92-101`,
the CCR bitfield is packed MSB-first:

    bits 7-6  W    way specification
    bit  5    UNUSED
    bit  4    CP   cache purge          0x10   (matches mars.h)
    bit  3    TW   two-way mode         0x08   (matches mars.h)
    bit  2    OD   data replacement disable      0x04  -- NOT in mars.h
    bit  1    ID   instruction replacement disable 0x02 -- NOT in mars.h
    bit  0    CE   cache enable         0x01   (matches mars.h)

And `CACHE.sv:499` is the whole mechanism in one line:

    CACHE_UPDATE <= CBUS_ID ? ~CCR.ID : ~CCR.OD;

**An instruction access fills the cache only when ID is clear; a data
access fills it only when OD is clear.** CE is untouched, so `CACHE.sv:
522` still services HITS (`CACHE_AREA && HIT && CCR.CE`). Half the cache
keeps working in each build, which is exactly the "comparable work"
condition CACHEOFF violated.

### The two builds

    CCR = CP|ID|CE = 0x13   instructions never refill, DATA still cached
    CCR = CP|OD|CE = 0x15   data never refills, INSTRUCTIONS still cached

Read the body stamps against the 0x11 baseline:

    ID severe, OD mild   -> the 2.9x is FETCH. Card T2's new form (the
                            compose inner loop in on-chip RAM) is the
                            card, and Card T moves nothing.
    OD severe, ID mild   -> it is DATA. The pivot is the only lever.
    both severe          -> both, and the ratio gives the split.

**IMPLEMENTATION TRAP, and it is the exact class that killed
PURGESTRESS:** `cache_purge()` (m_main.c 1968) rewrites CCR as
`CP | CE` every window, and `mars_start.s` 429 boots it as `0x11`.
**Either site left alone silently clears ID/OD and the variable does not
move.** Both must carry the bit, and CCR should be read back and
reported the way CACHEOFF v2 did (CCR reads 0 was the thing that proved
that ablation honest).

`mars.h` 103-105 defines CP, TW and CE and **does not define OD or ID**
-- they need adding, and the RTL above is the citation.

### Honest caveat

ID=1 may prove as severe as CACHEOFF: with no instruction refill ever,
the compose path runs from SDRAM permanently. If it kills the generation
the same way, the pair still forces an attribution -- because OD=1 is
the complementary probe and **whichever one survives tells us which
stream the machine can afford to lose.** That is a real answer either
way, unlike CACHEOFF, which lost both streams at once and could separate
nothing.

---------------------------------------------------------------------
## 152. THE MECHANISM, WITH A NUMBER: the hot compose path is 27,072 BYTES against a 4 KB cache -- 6.6x oversubscribed. That kills Card T2 arithmetically, explains the uniform tax, and means this renderer's whole optimisation history was conducted against an instrument that rewards the wrong thing (2026-09-14)

The builder's T2 measurement: **half the cache costs 1.48x the
generation** (n=4 properly sampled, three independent decodes converging
1.47/1.46/1.48, CCR confirming TW|CE on 4 of 4). And **no instruction
caching is a dead machine.**

Both are prices. Neither is the mechanism. The mechanism is in the map
file:

    rom/s16.lst:16    __ramtext_size = 0x69c0 = 27,072 BYTES

**The SDRAM-resident hot path is 27 KB. The SH7604 cache is 4 KB. The
compose loop is 6.6x oversubscribed.**

### That single number explains every result in this sub-arc

  * **Why the tax is UNIFORM across stages** (entry 150's puzzle). Every
    stage runs out of the same oversubscribed instruction stream, so
    every stage pays the same miss rate regardless of whether its DATA
    is reads or writes. The uniformity was never about data intensity;
    it is about all stages sharing one thrashing I-stream.
  * **Why halving the cache costs 48%.** 4 KB holds 15% of the path;
    2 KB holds 7%. Both thrash; the smaller one thrashes worse.
  * **Why ID=1 is a dead machine.** With no instruction refill at all,
    27 KB is fetched from SDRAM continuously.
  * **Why ares is 2.9x optimistic.** It charges instruction cycles and
    not the fetch, and there are 27 KB of instructions to fetch.

### It kills Card T2, arithmetically, with no rig cycle

TW gives 2 KB of on-chip RAM. **2 KB covers 7.4% of a 27 KB hot path, at
a measured cost of 48% of the generation.** There is no tenant that wins
that trade. Card T2 is dead and I withdraw it.

Note the shape of the error I made: I proposed T2 twice, first with the
wrong tenant (data) and then with the right one (code), and never once
asked how big the code was. **The footprint was one grep away in a file
the build already emits.**

### And it corrects my own mechanism from entry 150

I attributed the tax to *"a full cache purge every window."* **The purge
is close to a red herring.** A 27 KB path in a 4 KB cache thrashes
whether or not anyone purges it -- the purge removes lines that were
about to be evicted anyway. So "purge less", which the builder could not
test through three attempts, would have bought little, and the failed
attempts cost us nothing.

**The footprint is the mechanism. Not the purge, not the coherency
model, not the alias choice.**

### The finding that matters most, and it is uncomfortable

ares charges instruction COUNT and not instruction FETCH. **So for the
entire optimisation history of this renderer, every specialisation that
traded code SIZE for instruction COUNT measured as a win -- and on
hardware was a loss.**

The tree carries the fingerprints. m_main.c:691: *"ROM: a few hundred
calls a generation; the inline form spilled .ramtext."* That decision,
and every one like it, was made against the wrong cost model. And the
repo already recorded the symptom without naming the cause -- memory
`speed-gate-resolution-floor`: **"64 bytes of dead .data moves the
level-1 ladder 18 points."** Tiny footprint changes moving the
measurement hugely is the signature of a thrashing cache, and it has
been sitting in the notes since before this arc.

### The card this opens, and it is first-class

**CODE SIZE IS A LEVER AND HAS NEVER BEEN TREATED AS ONE.** 27 KB to
under 4 KB is not plausible, but the relevant target is not the whole
path -- it is the INNER LOOP that runs per-tile and per-pixel. If the
per-pixel and per-cell loops fit in 4 KB together, the outer path's
misses amortise over a whole tile instead of over a pixel.

Concretely, and every one of these is the reverse of a past decision:

    de-specialise duplicated variants into one parameterised loop
    stop unrolling loops whose bodies are memory-bound anyway
    move cold setup out of RAMCODE back to cart ROM
    measure every change in __ramtext_size, not in instruction count

**And this is rankable OFFLINE for the first time in the project.**
`__ramtext_size` is emitted by every build. A code-size ladder costs no
rig cycles and no ares runs -- and given that ares actively misleads
here, an offline proxy is not a compromise, it is a better instrument.

### The pivot wins twice

Entry 149 argued CAT1MD on work removal. **It also removes CODE:** every
tile class the MD draws is a compose path the SH-2 no longer needs
resident. The two levers were never independent.

### FLAG on the instrument bug, and it reaches further than the builder said

The sampler was `rm -rf`ing its own directory on every harness restart,
so most runs decoded two or three shots of sixteen. The builder scopes
the re-read to LOOP29 296-299.

**LOOP29 295 -- the body stamps -- carries the same thin-n signature
(n=5-6, "ranges wide"), and entry 150 built the uniform-tax hypothesis
on the UNIFORMITY of those five factors.** Uniformity across noisy
truncated medians is exactly what a truncated sample can manufacture.
The 2.9x is load-bearing for entries 150, 151 and the ID/OD design, and
it should be re-read with `--pull-only` before any of it sizes anything.

The 27 KB figure does not depend on it -- that one is from the map file
and needs no rig at all.

---------------------------------------------------------------------
## 153. The occlusion "contradiction" is a DENOMINATOR, not a disagreement -- both bakes count 2,373 occluded cells. And the footprint card has an exact target from the cache geometry: 1,024 bytes of contiguous hot code costs ONE WAY, and there are four (2026-09-14)

### The occlusion discrepancy, settled

The builder found `bake_cat1vis.py`'s docstring citing entries 57/58 for
*"50% of all cat1 cells, 83% in scene 2"* against today's bake reporting
**10% overall, 17% in scene 2 (23,432 cells, 21,059 visible).**

They are not in conflict. **Both compute 2,373 occluded cells.**

    entry 57    2,373 of  4,724 = 50.2%
    today       2,373 of 23,432 = 10.1%    (23,432 - 21,059 = 2,373)

An identical occluded count against a denominator 4.96x larger is not
two measurements disagreeing -- it is **one measurement on two different
cell populations.** The occlusion test never changed; the set of cells
fed to it did.

Entry 57's per-scene table sums to exactly 4,724 across all five scenes
(500 + 1792 + 1472 + 256 + 704), so it is not a per-scene-vs-total
error either. The ~5x is the population: entry 57 counted cat1 cells in
a narrower region, today's bake walks a wider one, and **the 18,708
extra cells are essentially all "visible"** -- which is what you get when
you add cells that have no foreground tile over them at all.

**The likely mechanism, stated as a hypothesis with its test:** entry
57's figures came from the cells the scene actually displays; today's
bake covers whole tilemap pages including regions the scroll never
reaches. An S16 page is 64x32 = 2,048 cells, and 23,432 / 5 scenes =
4,686 per scene, which is about two pages. Off-page cells have nothing
drawn over them, so they count as visible and dilute the ratio.

**Test, and it is one line of the bake:** restrict the cell population to
those reachable by the scene's scroll range and check whether the
denominator returns to ~4,724. If it does, the docstring is right, the
bake's report is right, and only the LABEL is wrong -- it should say
"10% of all tilemap cells / 50% of DISPLAYED cells."

**Which figure sizes step 2: the DISPLAYED one.** Cells the scroll never
reaches are never composed, so they cannot be saved. **50% remains the
number for sizing occlusion, and 10% is the number for sizing the
bitmap.** Neither is wrong; they answer different questions.

(Entry 57's stated assumption -- FG draws over BG at priority level 2 --
was RTL-confirmed in entry 58, so the occlusion rule itself is not in
doubt.)

### The footprint card has an exact target, derived from the cache

From `srcref/S32X_MiSTer/rtl/SH/SH7604/CACHE.sv:143-151`, the set index
is **`CBUS_A[9:4]`** -- six bits, 64 sets, four ways (`WAY0..WAY3`),
16-byte lines. 64 x 4 x 16 = 4,096 bytes, confirming the geometry from
the RTL rather than from recall.

**The consequence the builder's table needs: the set pattern repeats
every 1,024 bytes.** Bits [9:4] are the index, so two code blocks 1,024
bytes apart in address land in the same set. Therefore:

**A contiguous hot region of S bytes consumes ceil(S / 1024) WAYS in
every set it covers. There are four ways. So the entire hot working set
must total <= 4,096 bytes to be resident, and every 1,024 bytes of hot
code costs one of the four ways across all 64 sets.**

Applied to their measurement:

    _compose_pass    4,836 bytes  = 4.7 ways   SELF-EVICTING ALONE
    _m_main         18,552 bytes  = 18.1 ways
    total hot                       ~23 ways against a budget of 4

**`_compose_pass` overflows the entire four-way capacity on its own, by
740 bytes, before `_m_main` is considered at all.** Splitting m_main is
necessary and is not sufficient.

**The design target, which is much sharper than "shrink it":**

    inner loop (per-pixel/per-cell)   <= 2,048 bytes  = 2 ways
    everything else resident during
    compose                            <= 2,048 bytes  = 2 ways

Two ways hold the inner loop permanently; two absorb the rest. A 4,096-
byte inner loop would occupy the whole cache and leave nothing for
anything else, so "get compose_pass under 4 KB" is NOT the target --
**2 KB is.**

The builder's read of the symbol table is right and their conclusion --
split `_m_main` rather than shrink a top five -- is right. This just
gives the split a number to aim at, and says compose_pass needs work too
rather than merely needing to be left alone.

---------------------------------------------------------------------
## 154. The occlusion gate is ONE HARDCODED PAGE PAIRING, and that is worth up to 5x the saving. 18,708 cat1 cells are not "visible" -- they are UNTESTED (2026-09-14)

The builder found the mechanism and it is exact rather than approximate:
occlusion is only tested where BG page 0 sits under FG page 7. Page-0
cat1 is **4,724 -- entry 57's number to the cell** -- and scene 2 reads
83.4% against entry 57's "83%". My scroll-range hypothesis (entry 153)
was directionally right and the page gate is the real cause. Corrected.

**But reading the bake changes what the numbers mean.**
`tools/bake_cat1vis.py:45`:

    BG_PAGE, FG_PAGE = 0, 7     # scr2 draws page 0, scr1 page 7 (entry 11)

and line 99 only evaluates opacity when `page == BG_PAGE`. **That is one
hardcoded pairing out of the ten pages a scene's tilemap carries.**

So the correct statement is not "10% of cat1 cells are occluded." It is:

    2,373 of 4,724 page-0 cells   TESTED, 50.2% occluded
    18,708 cells on other pages   NEVER TESTED, counted as visible
                                  by default

**The 18,708 are not known to be visible. They are unexamined**, and the
bake's own comment says why -- it encodes one page pairing from entry 11
and the game demonstrably uses others. **Entry 124 found pages 10/11
carrying the transformation, and entry 126 is the page-select gate
itself.** The pairing is not a constant.

### What it is worth

If the other pairings occlude at anything like the page-0 rate, total
occlusion is ~11,700 cells rather than 2,373 -- **about 5x the saving,
decidable at bake time from rom alone, with no rig cycle and no runtime
change.** Against step 2's 0.44 v/gen that is not a rounding error.

**The honest counter-case, which is the thing to check first:** some of
those pages may be scenery variants the game rarely or never selects, so
their cells are occluded but also never composed -- real occlusion,
zero saving. That is exactly what distinguishes a 5x card from nothing,
and it is a decompile question rather than a bake question.

### The split of work

**Mine:** enumerate the (BG page, FG page) pairs the game actually
selects, per scene, from the page-select machinery -- the same registers
entry 126 documented and entry 124 used to find pages 10/11. Output is a
short table: scene, pairing, and whether it is reached in normal play.

**The builder's:** parameterise `bake_cat1vis.py` over that table
instead of the `0, 7` constant, and re-report with both denominators.

Neither needs the rig. Both are cheap. And the result either multiplies
step 2's occlusion saving by ~5 or rules it out for good -- which is the
right shape for a first move.

### Banked from the same exchange

  * Cache geometry confirmed by the builder against CACHE.sv 143-151
    independently: `WAY_TAG[n] = WAYn[CBUS_A[9:4]][TAG] == CBUS_A[28:10]`
    with `reg [5:0] LRU [64]`. Six bits, 64 sets, 4 ways, 16-byte lines,
    tag from bit 10 -- the 1,024-byte repeat is derived, not recalled,
    by both threads separately.
  * Targets recorded on their side: 2 KB inner loop / 2 KB resident,
    4 KB explicitly NOT the goal, layout treated as load-bearing.
  * `_compose_pass` accepted as a second target rather than the thing
    being protected.
  * Both bakes now carry their denominator in the tool, which is the
    durable fix -- the stale docstring could not have survived it.

---------------------------------------------------------------------
## 155. THE PAIRING IS LOCKED: fg = bg + 5, every sample, every quadrant. So the legal set is FIVE pairs, not 25 -- and the bake's (0,7) is a pairing the game NEVER makes (2026-09-14)

My half of NOTES 94. `tools/arcade_pagesel.lua`, arcade, no-coin, frames
120-5400, POLLING text words 0x740/0x741 (bytes 0x410E80/0x410E82 -- the
page selects `latch_layer_regs` already reads, m_main.c 2915) per
quadrant per frame, keyed on round 0xFFF142 and attract step 0xFFF031.
Full census in `docs/audit/pagesel_census.txt`.

**Every distinct (which0, which1) observed in 5,280 frames:**

    count  which0  which1  delta
        8     0       0      0     transitions only, 23-101 frames
        8     0       5     +5
        8     1       6     +5
        4     2       7     +5
        4    10      11     +1     the chevron (entry 126)

**The two planes are LOCKED at +5.** Not once in the run does a quadrant
show any other delta. Quadrants differ from each other -- round 1 at
f3405 has quads 0/2 on (2,7) while quads 1/3 are on (1,6), which is a
horizontal scroll straddling a page boundary -- but **within a quadrant
the pairing is always N and N+5.**

### So the legal pairing set is exactly five

    (bg, fg) = (0,5) (1,6) (2,7) (3,8) (4,9)

One fg per bg, determined and not chosen. That matches the builder's own
mask finding exactly and from the other direction: they found pages 0-4
share one cat1 mask and 5-9 share another. **A game that pairs N with
N+5 is a game with two five-page planes kept in lockstep.** Two
independent derivations of one structure.

### Which makes two of our numbers wrong

**1. `bake_cat1vis.py`'s `BG_PAGE, FG_PAGE = 0, 7` is not a
simplification -- it is a pairing that never occurs.** Page 0's occluder
is page 5. **The standing 2,373 figure tests page 0 against a foreground
it is never drawn under**, so it is neither a floor nor a ceiling on the
real saving; it is a different quantity. It could move either way when
corrected.

**2. The 12,080 ceiling is unreachable.** It took the best legal fg per
bg page -- fg 7 or 9 for scene 0, fg 9 for scene 2. **The fg is not
selectable.** With bg=N forced to fg=N+5 the true figure is one number,
not a maximisation, and the builder can produce it now:

    --pairs 0:0:5,0:1:6,0:2:7,0:3:8,0:4:9   (and the same for each scene)

### Caveats, stated because the induction is narrow

  * **No-coin attract only: rounds 0 and 1** (progress 0 and 1). Rounds
    2-4 are unobserved, and bg pages 3 and 4 never appear. The +5 rule
    is consistent across every one of 5,280 frames but it is an
    induction over two rounds.
  * **(0,0) appears only at transitions** -- 23 frames at f2686, 101 at
    f444 -- and is almost certainly a blanked load, not a composed
    pairing. It should not be fed to the bake without checking the
    display gate.
  * **(10,11) is the chevron**, delta +1, already documented in entry
    126, and it is the one exception to the rule. Its cells are the
    transformation art; whether they are worth occluding is a separate
    question from rounds.

**Confirming rounds 2-4 needs a gameplay run rather than the attract.**
`tools/health_mame.lua`'s coin/start pattern reaches level 1; reaching
rounds 3-4 needs a longer scripted play. Worth doing before the bake's
output is trusted for those scenes, and it is a probe I can run.

---------------------------------------------------------------------
## 156. The +5 rule HOLDS in rounds 2, 3 and 4 -- the 79% is confirmed. And a new one: bg pages 3 and 4 are never selected in any round's demo (2026-09-14)

The builder needed rounds 2-4 because scenes 2/3/4 carry 8,824 of the
11,187 occluded cells and my census was attract-only. Closed without a
scripted playthrough, using entry 108's lever: **writing the round into
the DIP round table at 0x1848-0x184F changes which round the ATTRACT
DEMOS show.** Three runs, 9,000 frames each,
`tools/arcade_pagesel_round.lua`, data in `docs/audit/pagesel_round{2,3,4}.txt`.

**Provenance first, because entry 108 is also the entry that caught a
whole census taken through this same patch.** The runs log 0xFFF142 and
the tile bank 0xFFF095 beside every sample:

    forced 2    round 2, bank 1
    forced 3    round 3, bank 1 and 2
    forced 4    round 4, bank 1 and 2

Entry 108 measured BANK = [1,1,1,2,2] by round. **Bank 2 appears only in
the forced-3 and forced-4 runs and never in forced-2.** So the patch took
and the rounds genuinely loaded; this is not the round-0 demo wearing a
different label.

### The result

    forced round   distinct (which0, which1)
        2          (0,0) (0,5) (1,6) (2,7) (10,11)
        3          (0,0) (0,5) (1,6) (2,7)
        4          (0,0) (0,5) (1,6) (2,7)

**Every pairing in every round is +5.** No exception in 27,000 frames
across three rounds, on top of 5,280 attract frames. The induction the
builder flagged as carrying 79% of the number is now a measurement in
all five rounds. **(0,5) (1,6) (2,7) (3,8) (4,9) stands.**

### The new finding, and it cuts the other way

**bg pages 3 and 4 never appear. In any round.** Only pages 0, 1 and 2
are ever selected into a quadrant, in every round including 3 and 4.

If those pages are never displayed, then **cat1 cells on bg pages 3 and
4 are never composed, and occluding them saves nothing** -- they would
be inflating the 11,187 with cells that cost nothing to begin with.

**The question the builder can answer instantly from data he already
has: how many of the 11,187 sit on bg pages 3 and 4?** If it is a small
share this is a footnote. If it is large, the card shrinks.

### The limit of this probe, stated plainly

**A demo is a recorded tape (entry 111) and it is a PARTIAL traversal of
its level.** So what I have shown is that pages 3/4 are not reached by
the demo, not that they are never displayed. A full playthrough scrolls
further and may select them at the far end of a round.

So the honest split:

    +5 pairing rule          MEASURED in all five rounds. Settled.
    pages 3/4 never composed HYPOTHESIS. Demo-only evidence, and a demo
                             does not traverse a whole level.

**Settling the second one does need the scripted gameplay run I offered
and the DIP trick cannot substitute for it** -- the lever changes which
round the demo shows, not how far it goes. It is worth running only if
the builder's page-3/4 share comes back large, which is a free query on
his side and should gate my probe rather than the other way round.

---------------------------------------------------------------------
## 157. SETTLED FROM ROM, NO PROBE NEEDED: the page select is an 8-entry TABLE LOOKUP, and pages 3 and 4 ARE in it. The builder's prediction was right and the 11,187 stands in full (2026-09-14)

The builder's uniformity argument said pages 3/4 should be reachable and
predicted my demo-only hypothesis would fail. **It fails, and the
program proves it exhaustively rather than by sampling.**

### The chain, from the register back to the table

    0051C  move.l #word_410E80,(FFF0EC).w   the two page-select register
    00524  move.l #byte_410E82,(FFF0F0).w   addresses, stashed as pointers

    02B02  movea.l (FFF0EC).w,a0            the vblank copier:
    02B06  move.w  (FFF0F4).w,d0            FFF0F4 -> 0x410E80
    02B0A  move.w  d0,(a0)
    02B0C  movea.l (FFF0F0).w,a0
    02B10  move.w  (FFF0F6).w,d0            FFF0F6 -> 0x410E82
    02B14  move.w  d0,(a0)

So the page selects are staged in WRAM at **0xFFF0F4 (which0) and
0xFFF0F6 (which1)**, and whoever writes those decides every page the
game can ever show. `sub_3A00` is the in-game writer:

    03A2A  lea    byte_40F0(pc),a0    which0 table
    03A2E  lea    byte_4100(pc),a1    which1 table
    03A32  move.w (FFF0E0).w,d0       camera X
    03A36  add.w  (FFF124).w,d0
    03A3A  addi.w #$C0,d0
    03A4C  ror.w  #8,d0
    03A4E  andi.w #$E,d0              -> index 0,2,..,14: EIGHT entries
    03A52  move.w (a0,d0.w),d1 -> FFF0F4
    03A5A  move.w (a1,d0.w),d1 -> FFF0F6

**Two fixed 8-word tables, indexed by (camera X >> 9) & 7, not by round.
They enumerate every page-select word the game can produce.**

### The tables, decoded

    idx   which0(BG)  which1(FG)   quadrant pairs (UL,UR,LL,LR nibbles)
     0      0x0000      0x5555      (0,5)
     1      0x0000      0x5555      (0,5)
     2      0x0000      0x5555      (0,5)
     3      0x4040      0x9595      (0,5) and (4,9)
     4      0x3434      0x8989      (4,9) and (3,8)
     5      0x2323      0x7878      (3,8) and (2,7)
     6      0x1212      0x6767      (2,7) and (1,6)
     7      0x0101      0x5656      (1,6) and (0,5)

**Two results, both exhaustive rather than sampled:**

**1. The +5 rule is not an induction at all -- it is a property of the
tables.** Every quadrant pair in both tables is (N, N+5). There is no
camera position that can produce anything else, so the rule holds for
every round, every scene, forever, without a single frame of measurement.

**2. Pages 3 and 4 ARE selected**, at indices 3, 4 and 5. **My hypothesis
is dead and the builder called it.** The demo simply never scrolls past
index 2 -- exactly the "a demo is a partial traversal" reading, which I
had listed as the alternative and which is now confirmed.

**So the 4,430 cells the builder found on pages 3/4 are real saving, the
11,187 stands in full, and the gameplay probe is unnecessary.** Cancelled
before it ran, which is the best outcome available for it.

### Independent confirmation that this is the right routine

`sub_3A00` branches on 0xFFF148 and, when set, writes
`#$AAAA -> FFF0F4` and `#$BBBB -> FFF0F6` (0x3A1C-0x3A22). Entry 126
MEASURED exactly `fg=AAAA bg=BBBB` on the chevron frames. The constants
in the program and the values on the wire agree, so the routine I am
reading is the one that drives the register.

### One flag against my own entry 127

Entry 127 corrected 0xFFF148 to "object slot index + 1, NOT a cutscene
flag." But `sub_3A00` tests it as a BOOLEAN to select the chevron page
pair, and entry 126 measured it taking only 0 and 1 across a whole run.
Either it is dual-purpose like 0xFFF02A (entry 131) or entry 127's
correction was about a different access. **Not load-bearing for anything
current, but it should not sit in the log unremarked.**

---------------------------------------------------------------------
## 158. NOT DONE: "CAT1MD step 2 has its eligibility" is wrong -- the cat-1 SHARE was measured and the ELIGIBILITY was not. I ran the coarse half: 83.3% MD-resident, and the only refusals are the chevron sets (2026-09-14)

Mike, on both threads declaring the day closed: *"It sounds like both
you AND the builder gave up."* Checking rather than reassuring, and he
is right that something was declared finished that is not.

**The builder's close says "CAT1MD step 2 has its eligibility (cat-1 is
36-44% of tiles in scenes 1/2)." That is the cat-1 SHARE, not the
eligibility.** NOTES 90 defines eligibility as: *a tile code is
MD-ELIGIBLE iff every pen it uses belongs to a colour set the round's
baked MD line assignment holds* -- the condition that makes the two
renderers byte-identical and the moving boundary invisible. **That
number has never been produced, and it is the card's go/no-go gate.**

Two different quantities, and the one we have is the one that does not
decide anything.

### The coarse half, run now

`mdr_s_line[round][set]` in `sh_src/pal_rounds_md.h` gives each set's MD
line, and the file's own header states the rule: *"A set absent here is
REFUSED an MD line and renders as BACKDROP (m_main.c 2357) -- black
tiles, not a fallback."* Line 0 is refusal. Against
`docs/audit/round_sets_definitive.txt`:

    round  sets  MD-resident  REFUSED  refused sets
      0     31       28          3     [19, 20, 21]
      1     13       10          3     [19, 20, 21]
      2     15       12          3     [19, 20, 21]
      3     12        9          3     [19, 20, 21]
      4     19       16          3     [19, 20, 21]

    TOTAL 90 set-slots, 75 resident = 83.3%

**Nothing in the ordinary scenery is refused. The only refusals, in
every round, are sets 19/20/21** -- which entries 123 and 129 identified
as the cycler/chevron ramp, present only on chevron pages and
FB-rendered regardless. So the refusal is by design and costs the card
nothing.

**The card is NOT killed. 83.3% is a go.**

### But the half that decides the shimmer is still unrun

Set-residency is necessary, not sufficient. `mdr_line_c[round][48]` is
3 lines x 16 entries, and multiple 8-colour sets share each line's 15
usable pens -- so **a set can hold a line and still have individual pens
approximated**, which is precisely the "pen exhaustion" case m_main.c
4327-4333 names as the only place the two renderers differ.

**The real eligibility test is per-PEN, not per-set**, and it needs
`bake_tilecram.py`'s colour data compared against each line's 15 pens.
That is NOTES 90 step 1 and it remains the first thing to do.

### The wider point, recorded because it is the process failure

The day produced sixteen log entries, thirteen handoff notes and **zero
builds**. Both threads then wrote that nothing was owed. **Two threads
declaring "design-complete, nothing outstanding" is exactly the state in
which a project stops moving**, and the correct close was never "nothing
owed" -- it was "the builder goes and builds."

And neither thread touched the things Mike actually sees on bldS:
**leftover screen text after the transformation, the remaining black
tiles, the shadow-column dither over MD-plane content.** They were open
this morning and they are open now. They are not blocked on anything
measured today.

---------------------------------------------------------------------
## 159. C1NOFB changes the plan: the line is past step 2, so step 2 is not the next card. And C1PUNCH masks at CELL granularity what is a PIXEL-granular occlusion -- tonight's `fully_opaque` test is exactly the gate it is missing (2026-09-14)

The builder's find, verified in the Makefile:

    C1NOFB (2007-2012)  "PLAN-TILES-TO-VDP step 3. Removes the FB cat-1
                        pass ENTIRELY so MD plane A's priority bit
                        carries cat1 on its own. Sprites then wrongly
                        cover cat1 where they overlap... so this is the
                        step-3 MEASUREMENT and not a ship."
    C1PUNCH (1109-1112) "sprite pixels of priority < 3 are not written
                        into cells the FG holds as cat-1 (a 40x28 mask
                        from the master's name-table pass)... Needs
                        CAT1MD + C1NOFB."

Both are on bldS. **So the line is not at step 1 awaiting step 2 -- it
is at step 3, past both, with a compensator.**

**Consequence: NOTES 90's step-2 design is obsolete as a card.** Step 2
restricts the FB cat-1 pass to sprite rows; step 3 deletes the pass
outright. **The 0.44 v/gen step 2 was worth has already been banked.**
Writing a design doc for it now would be designing something the line
has already gone past. The builder is right that there is no card there,
and right to say so rather than hand over a re-stamped rom.

### But their point 2 is half right, and the other half is a card

They say the occlusion work has no consumer because *"the framebuffer
already doesn't compose those cells."* True for the SKIP framing. **It
is not true for the CORRECTNESS framing, and that is where the work
lands.**

**C1PUNCH suppresses sprite pixels across a whole 40x28 CELL wherever
the plane holds a cat-1 tile. Sprite occlusion is PIXEL-granular.** So
for a cat-1 tile that is fully opaque, punching the whole cell is
correct -- every pixel of it should be in front. **For a cat-1 tile with
transparent pixels, punching the whole cell is WRONG: the sprite should
show through the holes and does not.** An 8x8 block of the sprite
disappears behind a tile that is mostly empty.

That is a systematic error, it is on the line Mike passed, and the
Makefile's "not a ship" comment is describing the uncompensated half of
it rather than being stale.

**The gate C1PUNCH needs is exactly the test tonight's bake already
implements:** `bake_cat1vis.py`'s `fully_opaque(idx)` -- *"return 0 not
in tiles[o:o+64]"*. Same function, different application. Tonight it
asked "is the FG tile over this BG cat-1 cell fully opaque". The card
asks "is THIS cat-1 tile fully opaque", and punches only those.

    punch a cell   iff its cat-1 tile is fully opaque
    otherwise      do not punch -- the sprite wins, which is wrong in
                   the other direction but only on the tile's OPAQUE
                   pixels, a strictly smaller error than losing the
                   whole 8x8 block

**Card C1P: bake a per-tile-code "fully opaque" bit for cat-1 tiles and
gate C1PUNCH on it.** One bit per tile code, the existing function, no
runtime cost beyond a table lookup the punch pass already does per cell.

### One thing to confirm before it is built, and it is a naming question

The Makefile says C1PUNCH keys on *"cells the FG holds as cat-1"*, while
the builder's page census found all 23,432 cat-1 cells on **pages 0-4**,
which we have been calling BG. Those cannot both be the same plane under
one naming. **The card does not depend on the answer -- whichever plane
C1PUNCH keys on, it should punch only fully-opaque tiles -- but the bake
must be pointed at the right plane's tile codes** or it will gate on the
wrong set. Builder's to resolve; it is one read of the punch pass.

### And the sizing, which is free

The share of cat-1 tile codes that are fully opaque is one run of the
existing bake. If nearly all are opaque, C1PUNCH is nearly correct today
and the card is a small polish. If many are sparse, the line is losing
sprite blocks constantly and this is a visible-defect fix, not a polish.
**That number should be produced before Mike is asked to look at
anything.**

## 160. The builder's plane correction is right and my 47.7% is void -- but crossing it with the round census names a cause for Mike's BLACK TILES: the chevron sets are refused because the bake's demand model CANNOT SEE THEM (2026-09-14)

### The correction, accepted in full

`snap[0]` is the FOREGROUND. Three independent citations, all checked
here rather than taken on report:

  * `m_main.c:2909` `latch_layer_regs` fills `snap[0]` from text word
    0x740 and `snap[1]` from 0x741.
  * `m_main.c:15042` the compose walk sets `isfg = (md_phase >= 5)` and
    then `const layer_regs *wl = isfg ? &snap[0] : bl;` -- snap[0] is
    reached only on the fg phases. The comment above it says it outright:
    *"phases 1-4 = Plane B (BG layer), phases 5-8 = Plane A (FG cat-0)"*.
  * `docs/audit/pagesel_census.txt` -- which0 holds 0-4, which1 holds
    5-9, in every sampled frame.

So pages 0-4 are the FOREGROUND. `bake_cat1hole.py:15-16` had it right
and `bake_cat1vis.py` had it inverted; the builder marked the file
(9810f1c) rather than quietly restating the number, which is the right
disposal.

**Consequence: entry 158's 47.7% is VOID, and so is the whole occlusion
line it sat on.** All 23,432 cat-1 cells are on the topmost tile plane
and `jts16_prio.v:83-95` tests the foreground first, so no tile plane is
above them. Tile-occlusion of cat-1 is ZERO. The bake was measuring
whether the background covers the foreground, which is the impossible
direction. Entries 57, 58, 153, 156, 157 and 158 all quoted numbers from
that direction; they are all void as SAVINGS. The (N, N+5) pairing
result survives untouched -- same table, correctly read, only the labels
were swapped.

**That is the fourth number of mine to come out of the record today and
the second from this same file.** The pattern is worth naming: every one
of them was a number I computed correctly from a premise I never checked.
The pairing, the +5 rule, the table lookup -- all derived from bytes and
all still standing. The occlusion shares -- all derived from a LABEL --
all gone.

### What crossing the correction with the round census turns up

`round_sets_definitive.txt` says sets **19, 20, 21** -- the cycler /
chevron plane -- appear in **every one of the five rounds**. Crossing
that list against `mdr_s_line` in `sh_src/pal_rounds_md.h`:

    round 0: 31 sets used | granted 28 | REFUSED 3 -> [19, 20, 21]
    round 1: 13 sets used | granted 10 | REFUSED 3 -> [19, 20, 21]
    round 2: 15 sets used | granted 12 | REFUSED 3 -> [19, 20, 21]
    round 3: 12 sets used | granted  9 | REFUSED 3 -> [19, 20, 21]
    round 4: 19 sets used | granted 16 | REFUSED 3 -> [19, 20, 21]

**In all five rounds the ONLY sets refused an MD line are 19, 20 and 21.
Nothing else is refused, ever.** The header of that generated file says
what a refusal costs: *"A set absent here is REFUSED an MD line and
renders as BACKDROP (m_main.c 2357) -- black tiles, not a fallback."*

That is a single named cause for one of the three defects Mike still
sees on bldS, and it is the same three sets in every round.

### And the mechanism is NOT the pen budget

The obvious reading is that the three lines are full. Counting the
0xFFFF holes in `mdr_line_c` (indices 0, 16, 32 are each a line's
transparent pen and never available):

    round 0:  2 free pens      round 3: 10 free pens
    round 1:  1 free pen       round 4:  0 free pens
    round 2:  1 free pen

Tight -- but round 3 has room for a set and still refuses all three, so
capacity is not the rule doing the refusing.

**The rule is `bake_tilecram.py:142`.** `worst_viewport` builds demand by
walking the ROM's packed tilemaps and counting cells per set; a set with
no cells is never a candidate. Counting those cells here, from the rom,
over all ten pages of all five rounds:

    round 0:      0 of 17937 non-blank cells in sets 19/20/21
    round 1:      0 of 19462
    round 2:      0 of 15861
    round 3:      0 of 17972
    round 4:      0 of 11784
    TOTAL:        0

**Zero. The chevron sets hold no cell in any round's ROM tilemap.** But
the arcade census that found them is explicit about its method -- *"every
cell of every tilemap page walked AFTER the round has loaded (f700)"* --
which is LIVE tile RAM, not the packed rom map.

So the two disagree, and the disagreement is the finding: **the chevron
plane is written into tile RAM at RUNTIME, and the bake models demand
from the ROM map, so it cannot see the plane at all.** It is not
outvoted in the packer. It never reaches the packer.

**`--also` is the existing escape hatch for exactly this** -- its help
text says *"The worst-case viewport only sees palettes the..."* -- but
`bake_tilecram.py:299` gates it to `s == a.live_scene`, one scene, and
the regenerate command in the generated header does not pass it at all.

### Card MDCHEV

Pin sets 19/20/21 in every round. Two parts, and the second is the one
that needs a decision:

  1. **Lift `--also` off `live_scene`** so it pins across all five
     rounds, and add it to the regenerate recipe in the header. Small.
  2. **A static pen table cannot hold a CYCLER.** Even pinned, the
     chevron's colours change per frame, so pinning buys the tiles their
     art back and freezes their animation. The honest options are (a)
     compose those cells on the 32X framebuffer, which costs FB passes
     and is the thing the pivot is removing, or (b) give the cycler a
     line of its own and have the 68K rewrite 16 CRAM words in vblank --
     it already writes CRAM there, and 16 words is nothing.

**(b) is the architectural answer and it is cheap**, but it needs a free
line, and only round 3 has one. So it is a packing question first.

**The number that sizes all of this is the one I could not get from the
rom: how many CELLS the chevron plane covers once it is written at
runtime.** That is a live tile-RAM read on the builder's side and it
decides whether this is a visible-defect fix or a footnote. If the
chevron is the background cycler behind the whole level it is large; if
it is the three-column chevron strip it is small.

## 161. MDCHEV was wrong and the builder killed it the right way. Pages 10/11 are sub_3A00's CUTSCENE branch -- and the face is not tiles at all, it is a zoomed SPRITE with four RUNTIME-ALLOCATED palettes (2026-09-16)

### My card was wrong, and the way it was wrong is the lesson

The builder packed sets 20/21 successfully -- the bake fits with a pen
spare -- and the resulting rom is **pixel-identical to the line** on both
the face and eye screens. **Disproof by construction beats my derivation,
and it should: the card was built on a number I had no business
quoting.**

Entry 160 said sets 19/20/21 hold zero cells in any round's rom tilemap
and concluded the bake "cannot see them". The zero is real -- I
re-checked it here, all ten pages, all five rounds -- but it is a
**truncation artefact**, not evidence. `bt.unpack` returns exactly 20480
words for every round. That is `TILES_N`, that is ten pages, and **the
face screen is on pages 10 and 11.** I measured the absence of something
outside the array's range and read it as a property of the game.

**That is the same failure as the plane labels two days ago**: a number
computed correctly from a premise I never checked. Third time this week.
The check that would have caught it was one line -- `len(w)/2048` -- and
I printed cells per set without ever printing how many pages I had.

### Where pages 10/11 come from: settled from rom

`sub_3A00` at 0x3A00 has two branches and I only ever read the second.

    03A00  tst.b   (byte_FFF148).w
    03A04  beq.s   loc_3A2A            <- the in-game table lookup (entry 157)
    03A06  andi.w  #$1FF,d1
    03A0A  moveq   #0,d0
    03A0C  move.w  d0,(unk_FFF0E2).w   <- all four scroll registers
    03A10  move.w  d0,(unk_FFF0E4).w
    03A14  move.w  d0,(unk_FFF0E8).w
    03A18  move.w  d0,(unk_FFF0EA).w
    03A1C  move.w  #$AAAA,(unk_FFF0F4).w
    03A22  move.w  #$BBBB,(unk_FFF0F6).w
    03A28  rts

**When `byte_FFF148` is non-zero: plane 0 is page 10 in all four
quadrants, plane 1 is page 11 in all four, and both planes are pinned to
scroll zero.** A static, unscrolled, full-screen pair. That is the face
screen, and it is the branch the entry-157 table lookup never reaches.

So the builder's "pages 10/11" and my old "chevron branch writes
#$AAAA/#$BBBB" are the same three instructions. I had both halves and
never joined them.

### And the face is NOT a tile screen

`sub_90F4` at 0x90F4 is what sets that flag, and it does much more:

    09104  move.b  (byte_FFF109).w,(byte_FFF148).w
    0910A  addq.b  #1,(byte_FFF148).w      <- the cutscene id, +1
    09110  move.l  a6,-(sp)
    09114  move.w  #$8000,status(a6)       <- a SPRITE object, active
    0911A  move.l  #sub_91CE,routine(a6)
    09122  move.l  #off_99A2,frame_tableset(a6)
    09130  move.b  (game_level).w,d0       <- one table per level
    0913C  move.l  (a0)+,$6C(a6)           <- FOUR palette indices
    09140  move.w  (a0)+,zoom(a6)          <- and a ZOOM factor

then four consecutive `jsr (RequestPaletteUpdate)` at 0x9180, 0x9192,
0x91A4 and 0x91B6, each storing the returned `palette_bank` back into
`$6C..$6F(a6)`; and `sub_91CE` tears the object down with four matching
`ReleasePaletteSlot` calls.

**The transformation face is a four-palette ZOOMED SPRITE. Pages 10/11
are the backdrop behind it.** The builder's own numbers say the same
thing from the other side: 1,600 cells, six distinct colours, **no skin**
-- that is a flat backdrop rendered correctly and a sprite missing on
top of it.

The five tables at `off_99A2`, one per level -- palette indices then zoom:

    level 0  byte_99B6   0x16, 0x99, 0x9B, 0x9D   zoom 0x46
    level 1  byte_9A66   0x16, 0x1A, 0x16, 0x1A   zoom 0x56
    level 2  byte_9BAC   0x16, 0x1C, 0x16, 0x1C   zoom 0x54
    level 3  byte_9CF2   0x16, 0x1E, 0x16, 0x1E   zoom 0x55
    level 4  byte_9E38   0x16, 0x9F, 0xA1, 0xA3   zoom 0x46

Levels 1-3 use two distinct palettes repeated; 0 and 4 use four. Index
0x16 is common to all five. And at 0x9162 there is a second variant:

    09162  cmpi.b  #8,(byte_FFF109).w
    09168  bcs.s   loc_917A
    0916A  addq.b  #1,$6C(a6) ... $6D, $6E, $6F

**cutscene ids >= 8 use all four indices PLUS ONE.** Two face variants
per level, not one.

### Why this cannot appear in any static bake, by construction

Those four palettes are **allocated at runtime by the allocator**, and
the slot number is written back into the object. They are not a colour
set the worst-case viewport could ever harvest, because no tilemap cell
ever names them. **Every per-round static pen table is structurally blind
to the transformation face**, and widening the page walk to 10/11 would
not change that -- it would only fix the backdrop.

`RequestPaletteUpdate` also has a **fallback**: `palette_fallback_slot`
at WORKRAM 0xFFF401. **A face drawn in six flat colours with no skin is
what an allocator fallback looks like**, and it is the same symptom class
as the flat-sky/missing-clouds finding (first-come pen pack, LRU slots).

### What I do NOT know, and it is the next rung

**Who WRITES the tile words into pages 10/11.** The scene table at 0x1CE2
has exactly five entries -- entry 5 onward decodes as the ASCII copyright
string ("SEGA 1988", "THIS GAME IS TO BE USED ONLY...") -- so there is no
sixth blob reachable that way, and no round's blob extends past page 9.
Something fills those pages outside the scene unpack and I have not found
it.

That is one live read on the rig: **dump tile RAM 0x40A000-0x40BFFF on
the face screen.** It settles whether the backdrop art is right, and a
diff against the rom's blobs settles whether it came from the scene
unpack at all.

### So MDCHEV re-points, and it is smaller than I claimed

Not a palette-refusal card. Two separate things:

  1. **The backdrop**: widen the bake's page walk past 9 so pages 10/11
     get pen tables. Real, and cheap, and the builder already proved the
     packer has room.
  2. **The face itself**: a zoomed sprite with four runtime palettes.
     Whether our rom creates that object at all is the question, and it
     is a sprite question, not a tile-art one.

**(2) is the defect Mike sees. (1) is the thing I misdiagnosed as (2).**

## 162. The FB theory has a confound and a hole, and the gate it needs already exists: GLOW_PAGE's "any page nibble >= 10" (2026-09-16)

### The confound: the proposed test cannot produce the face

The builder's build is "suppress the FB compose on that screen and see
whether the face appears from underneath."

**The face cannot appear.** Entry 161 settled from rom that the
transformation face is a four-palette ZOOMED SPRITE built by `sub_90F4`
at 0x90F4, not tile art. **Our sprites are composed in the 32X
framebuffer.** Suppressing the FB compose removes the only surface the
face could ever be drawn on.

So the outcome space is:

    FB suppressed, ZIGZAG appears     -> FB theory CONFIRMED
    FB suppressed, still flat         -> FB theory DEAD
    FB suppressed, face appears       -> IMPOSSIBLE, either way

**The test is sound; only its success criterion is wrong.** Judged on
"does the face appear" a correct confirmation reads as a failure. The
criterion has to be the BACKDROP -- the zigzag -- and the face is a
second, separate bug that this build cannot speak to.

### The hole: 67.3% transparent does not fit a uniformly flat screen

The builder's own frame census at f1585: the FB is **67.3% transparent**,
plus 4.9% pen 18, 2.8% pen 23, 4.3% pen 128 -- about 12% flat fill.

**A transparent FB pixel shows the MD planes.** So two-thirds of that
screen is already showing the MD through, and if the MD planes held the
right art in the right colours, **two-thirds of the zigzag should already
be visible.** It is not.

FB coverage therefore does not explain a screen that is flat
*everywhere*. It explains a screen that is flat over ~12% of its area.
Either the flat pens are concentrated exactly where the picture should be
and the rest is genuinely empty backdrop, or the MD planes are not
showing pages 10/11 at all and the FB is a second-order effect.

**That is a spatial question, not a histogram question**: where are the
flat pixels? Scattered across the frame -> the FB is painting over
everything. One contiguous block -> the FB is innocent and the MD side is
the fault.

### The gate already exists, and it is proven exact

The builder plans a "scene/page gate" for the suppress build. **The repo
has one, it is exactly this screen, and it is already argued correct** --
`m_main.c:14620-14650`, behind `GLOW_PAGE`:

    /* The chevron plane is pages 10 and 11 and nothing else in a whole
     * arcade run selects a page >= 10, so "any quadrant of either
     * plane's page select >= 10" is an exact marker for the
     * transformation. */
    uint16_t gp_f = TEXT_C[0x740], gp_b = TEXT_C[0x741];
    uint16_t gp_a = (uint16_t)(gp_f & ((gp_f << 1) | (gp_f << 2)) & 0x8888u);
    uint16_t gp_c = (uint16_t)(gp_b & ((gp_b << 1) | (gp_b << 2)) & 0x8888u);
    glow_chev = (uint8_t)((gp_a | gp_c) != 0);

Branchless, four nibbles per word in one mask, and the comment records
that the looped form cost 0.07 v/gen (LOOP29 286). **`glow_chev` is the
gate. The suppress build should reuse it, not invent one.**

The same comment also corrects my note 101 on one point, and the repo's
reading is sharper than mine: **0xFFF148 is an OBJECT marker, not a scene
flag -- dispatcher at 0x398E, value = slot + 1.** That is consistent with
`sub_90F4` doing `move.b (byte_FFF109).w,(byte_FFF148).w` then
`addq.b #1` -- 0xFFF109 is a slot index, not a cutscene id. My "cutscene
id + 1" should read "object slot + 1".

### And there is already a measured anomaly on this exact screen

The `GLOW_PAGE` block carries it (LOOP29 284/285): **"the page gate fires
at once, but only 2 of 8 chevron frames show the game's own sets 20/21."**

Six of eight frames on this screen do not show the right sets, and that
was measured before any of this week's theories existed. It is
palette-side and it is upstream of the FB. **The builder's "does palette
matter -- NO, pixel-identical" was a test of the STATIC bake; the
animator handover is a different mechanism and 285 says it is already
known to be slow.**

### So the order of work

  1. Reuse `glow_chev`, do not write a gate.
  2. Judge the suppress build on the **zigzag**, never on the face.
  3. Before or alongside it: **where are the flat pixels spatially?**
     That question separates the FB theory from an MD-side fault more
     cheaply than a build does.
  4. The face is a sprite and needs its own investigation whatever
     happens to the backdrop.

## 163. The transformation palettes are QUEUED, and the arcade's queue has NO BUDGET -- it drains completely every vblank. So the partial landing is ours, and "one colour, white" means ZERO landed (2026-09-16)

### The builder's measurement, and what the rom says should happen

Across eight frames of the transformation screen: set 19 never lands
(1 of 4 colours, and that one white); sets 20/21 land complete in 1 of 8;
both present 0 of 8. Upstream of the FB, and the static bake is provably
not the path.

**Sets 19/20/21 are SPRITE PALETTE SLOTS, not baked colour sets.** That is
why packing them into the static bake changed no pixel -- there was never
a path from the bake to those slots. Their contents are decided at
runtime by the allocator and delivered by a queue.

### The protocol, from rom

`RequestPaletteUpdate` (0x3B2E) **does not write a palette.** It resolves
a slot and falls through to `QueuePaletteUpdate` (0x3BEC), which appends
one 8-byte entry -- destination pointer, source pointer -- to a 64-entry
circular buffer at 0xFFFFF600-0xFFFFF800, and bumps
`palette_process_count`.

The drain, at 0x3C4E-0x3C82:

    03C5A more_palettes:
    03C5A     movea.l (a2)+,a1          ; destination
    03C5C     movea.l (a2)+,a0          ; source
    03C5E     move.l  (a0)+,(a1)+       ; 7 longs
    ...       (x7)
    03C7A     subq.b  #1,(a3)           ; one less
    03C7C     bne.s   more_palettes     ; <-- NO BUDGET

**There is no cap. The loop runs until the count is zero.** Whatever the
game queued lands in that vblank, all of it, every time.

**So the arcade has no rate limit here and cannot produce a partial
landing. Every missing colour on our side is ours.**

### And "one colour, and it is white" means ZERO landed

Two details of the copy decide how to read the builder's census:

    03C1E  lsl.w   #5,d0              ; slot * 32
    03C26  lea     2(a1,d0.w),a1      ; dest = base + slot*32 + 2
    03C34-03C38                       ; source index * 28

Destination starts at **+2** and runs **28 bytes** -- bytes 2..29 of a
32-byte slot. **Colour 0 (bytes 0-1) and colour 15 (bytes 30-31) are
NEVER WRITTEN by this path, ever, for any palette.**

So a slot holding exactly one non-default colour at index 0 has received
**nothing at all**; that colour is whatever our init left there. **"1 of 4
colours, and that one is white" is not a partial landing. It is a total
miss plus an initialisation value**, and it should be counted as 0 of 8,
not 1 of 4. The set is in the same state as before the screen started.

Corollary worth carrying: a source palette is **14 colours, 28 bytes**,
while a destination slot is **32 bytes**. `tools/actor_palettes.py:10`
already records the ×28 stride for the sprite path, so this is known
ground -- but it means any reader that walks `palette_lookup` with a
32-byte stride drifts 4 bytes per index. At the level-0 face's index 0x99
that is 612 bytes of drift, which would read as plausible-looking wrong
colours rather than as an obvious failure.

### The transformation queues FOUR entries with no vint between them

`sub_90F4` calls `RequestPaletteUpdate` four times back to back --
0x9180, 0x9192, 0x91A4, 0x91B6 -- with only register shuffling in
between. **All four are queued inside one frame and the arcade lands all
four in the next vblank.** We land 0 to 2 of the three sets the builder
sampled.

### This has a name here already: the LOST-PUSH BELT

A queued palette push that never reaches its destination is exactly the
failure the belt was built for -- `m_main.c:12497` *"LOST-PUSH BELT
(2026-09-05, Mike's black boss on the JP...)"*, with the detector in
`tools/state_health.py:63-79` printing *"LOST-PUSH palette words
(shadow==game, PAL_SH stale)"*.

**That is the instrument for this, it already exists, and it should be
run on the transformation frames before anything is built** -- the same
shape as the `glow_chev` catch. Black boss, missing face: both are
palette pushes that were queued and lost.

### What to hand over

  1. **Recount the census: "one colour and it is white" is ZERO
     landed**, because colour 0 is never written by this path.
  2. **The arcade has no drain budget** -- any partial landing is ours,
     so the question is not "why is the game slow to send" but "where do
     our pushes go".
  3. **Run `state_health.py`'s LOST-PUSH line on the transformation
     frames.** The detector predates this screen and covers exactly this
     failure.
  4. Colours 0 and 15 of every slot are never written by the game --
     anything our side shows at those indices is our own initialisation
     and must not be read as game data.

## 164. The chevron sets DO reach MD CRAM -- as someone else's pen. mdp_claim_pen's nearest-colour fallback is the mechanism, and the allocator's own comment carries the budget that causes it (2026-09-16)

### The builder isolated it to PAL_SH -> MD CRAM line assignment. That is `mdp_claim_pen` (m_main.c:2447), and it explains their data exactly

To place set `s`'s pixel `p`, whose quantised colour is `q`, on line `l`:

    1. a pen already holding q            -> SHARE it
    2. else a free pen (0xFFFF)           -> claim EXCLUSIVELY
    3. else NEAREST-COLOUR FALLBACK       -> DIAG[36]++

And the budget is stated outright in the `DRIFT_VOL` comment above it
(LOOP29 154): ***"it needs a free pen and 363 of 366 burned claims have
none (2-3 MD CRAM lines, 128 colour sets)."***

**Sets 19/20/21 are CYCLERS.** The builder's own dump proves it -- the
same seven-colour ring, phase-rotated between the arcade, our 0xFF9000
mirror and PAL_SH. **A cycler's `q` is different every frame, so it must
RE-CLAIM every frame.** With no free pen it falls to step 3 and is given
the nearest pen, **which belongs to another set.**

**So "sets 19/20/21 never reach MD CRAM" is not quite what is happening.
They reach it every frame, as somebody else's pen.** Which is exactly the
builder's own observation, arrived at independently: *"set 19's lone
matching white sits at MD CRAM index 14 -- another set's colour
coinciding."* **That is not a coincidence. That is step 3.** The nearest
pen happened to be white, and white is what the ring's first entry
(0x7FFF) quantises to.

Their measurement and the code agree with no gap left.

### And `mdp_s_vol` was built for this and cannot work

Line 2466: `if (mdp_s_vol[s] >= 2 && freepen) pen16 = 0;` -- a volatile
set prefers an exclusive pen over sharing. **The guard is `&& freepen`,
and the comment says there is never one.** The DRIFT_VOL follow-up marks
the PEN instead so sharers skip it -- but that only stops *other* sets
drifting; it does nothing for the cycler itself, which still needs a pen
it can rewrite.

**The existing volatile machinery is the right idea aimed at a pen supply
that does not exist on a level screen.**

### The fix is a supply problem, and this screen has the supply

The 363-of-366 figure is for a LEVEL screen -- 128 colour sets competing
for 2-3 lines. **The transformation screen is not a level screen.**

    what is on it   pages 10 and 11 only, both planes, scroll pinned to
                    zero (sub_3A00's cutscene branch, entry 161)
    what it needs   sets 19/20/21 = 7 ring colours each, plus white
    what exists     2-3 MD lines x 15 usable pens = 30 to 45 pens

**Three cyclers need at most 21 exclusive pens against 30-45.** It fits
with room to spare -- **if the level's 28 sets are released first.** If
the allocator is still holding the previous screen's sets when the
chevron comes up, every pen is owned, every claim falls to step 3, and
the cycler is painted in the level's colours. That is the picture Mike
sees.

**And the gate to do it on already exists and is already proven exact:
`glow_chev` (m_main.c:14620-14650), "any page nibble >= 10".** The
eviction is already contemplated in the code, too -- `mdp_free_set`'s
guard at 2348 reads *"table set: never freed inside its scene (209: a
cutscene may evict it)"*.

### Card CHEVPEN

On the `glow_chev` edge, release the level's sets so the chevron's three
can claim exclusive pens, and let their CRAM words refresh per frame.
Three things make it small:

  1. **`glow_chev` is the gate** -- written, branchless, proven exact.
  2. **`mds_install` already installs a scene's tables wholesale** and
     invalidates selectively (2933), so there is a precedent for a
     scene-scale re-assignment.
  3. **The screen is static** -- scroll pinned to zero, both planes one
     page each. Nothing else is competing for the pens while it is up.

**The one number to get first: `DIAG[36]` (nearest-colour fallbacks) over
the chevron frames.** If it spikes on this screen the diagnosis is
confirmed outright and no reasoning is needed. It is already counted --
`mdp_claim_pen` increments it on every step-3 claim.

### Two corrections taken from the builder

**Their correction to my index-0 rule is right.** Set 19's matching white
sits at MD CRAM index 14, inside the written range, so the rule does not
move this screen's arithmetic. The rule stands generally -- colours 0 and
15 are never written by `QueuePaletteUpdate` -- but it was not what
produced their white. Step 3 was. **My conclusion survived for a reason I
had wrong.**

**And the belt is clean**: 0/0, 0/0, 1/0 across the three transformation
frames, the one lost word being 0x036, the BLINK word `glow_bake.py`
leaves unbaked by design. **Black boss and this are NOT one bug**, and my
note 103's suggestion that they might be is withdrawn. Rebuilding the
detector's logic on `--dump` because headless ares writes no `.bs1` was
the right call and the three inputs were the right three.

## 165. CHEVPEN's door found, and it is a contradiction inside m_main.c: 0xFFF148 drives mds_onscreen as a "cutscene byte" at 6611, and the rom CLEARS it at animation frame 2 -- mid-screen (2026-09-16)

### The builder's arithmetic kills my mechanism, and it is right

My step-3 story predicts three cyclers re-claiming every frame: 3 sets x
8 pens ~= 24 fallbacks/frame, ~2,000 across the screen. **Observed: 64
total.** Two orders short, and far wider than the `.bss` wipe can carry.
**Entry 164's mechanism is withdrawn.** The sets are not falling to
nearest-colour; they are not reaching `mdp_claim_pen` at all.

Their reading of their own instrument is also right on both counts: a
counter that decreases across deterministic runs is being wiped
(`m_main.c:1060` says so), and it is double-booked -- `m_main.c:15023`
defines `r60_pkt_flip` as `DIAG[36] & 1`. **The number could not have
settled this either way and they said so before I could.**

### So: which door. It is `mds_onscreen`, and the rom decides it

`mdp_assign_set` is reached from the claim path only for sets the MD
side is willing to carry. `mds_onscreen` gates that whole regime:

    2348  if (mds_pin[s] && mds_onscreen)     -> table set, never freed
    2721  if (!mds_onscreen)                  -> cutscene regime
    2857  if (mds_pin[s2] && mds_onscreen)    -> skip as eviction victim
    2867  if (mds_pin[s2] && !mds_onscreen)   -> age 255, evict first

And `mds_onscreen` is driven, at `m_main.c:6611`, by the state word:

    /* FOLD 4: the game's own cutscene byte (0xFFF148, via the state
     * word) forces OFF with no detector lag -- the face plane in the
     * first frame. */
    unsigned so = md_state_on();
    if (so != 2) on = (uint8_t)so;

with `md_state_on()` returning 0 on `MD_STATE_CUT(w)`, and NOTES 23's
signal deriving that cut bit from **0xFFF148 != 0**.

### The contradiction, and it is between two comments in the same file

**`m_main.c:6611` calls 0xFFF148 "the game's own cutscene byte".**

**`m_main.c:14625` says the opposite, and it is the one that matches the
rom:** *"(0xFFF148 is an OBJECT marker, not a scene flag -- dispatcher at
0x398E, value = slot + 1)"*. That is precisely why `glow_chev` was built.

**One of these is load-bearing for `mds_onscreen` and it is the wrong
one.**

### And the rom says when it breaks: animation frame 2

`sub_90F4` sets the byte on entry (0x9104-0x910A) and `sub_91CE` clears
it:

    091CE  tst.b   $21(a6)
    091D2  bne.s   loc_924C
    091D4  cmpi.w  #2,animation(a6)     <- animation is set to 1 at 0x914C
    091DA  bne.s   loc_924C
    091DC  clr.b   (byte_FFF148).w      <- CLEARED, screen still up
    ...
    091FA  jsr     (ReleasePaletteSlot) <- and all four palettes released
    09206  jsr     (ReleasePaletteSlot)
    09212  jsr     (ReleasePaletteSlot)

**The flag goes to zero at the SECOND animation step -- early, while the
transformation screen is still on display -- and the game releases the
four palette slots at the same instant.**

### The predicted consequence, and it matches every measurement so far

If `mds_onscreen` follows that byte, then a few frames into the chevron
screen the flag clears, `on` goes back to 1, and `m_main.c:6624` fires
`mds_install(r9, ...)` -- **the round's table is re-installed and the
level's 28 sets are re-pinned across every pen.** Sets 19/20/21 are in no
table, so `mds_pin` is 0 for them (2966), and from that frame on they are
the first eviction victims, never assigned, never reaching
`mdp_claim_pen`.

**That predicts exactly what the builder measured: sets 20/21 complete in
1 of 8 frames, and 0 of 8 with both.** The one good frame is before the
clear. It also matches LOOP29 285's older "only 2 of 8 chevron frames
show the game's own sets 20/21" from a completely different instrument.

**This is a prediction, not a finding.** The rom half is solid; the
consequence is inferred and has to be measured.

### The test, folded into the build they already want

Their `mdp_assign_set` probe is the right build. **Log `mds_onscreen`
per frame beside it.** Then:

    mds_onscreen 0 -> 1 partway into the chevron   -> the door, confirmed
    mds_onscreen stays 0 all the way through       -> door is elsewhere,
                                                      and the refusal
                                                      reason in their
                                                      counter names it

Either way their probe is what settles it, and one extra byte per frame
makes it decide between two hypotheses instead of one.

### If it confirms, CHEVPEN changes shape

Not "free pens" -- **"do not hand the screen back to the level's table
while the screen is still up."** `glow_chev` is already the correct
signal for that and does not depend on 0xFFF148 at all. The fix is to let
`glow_chev` hold `mds_onscreen` off for the duration of the chevron
plane, rather than a byte the game clears mid-screen for its own reasons.

**The builder's instinct not to spend the build twice was right, and this
is why: the pens were never the constraint, the same way the bake's room
was never the constraint.**

### The two loose threads they flagged

**1. DIAG[36] / r60_pkt_flip is a real bug and it is not about this
screen.** `m_main.c:15023` makes the R60 packet-flip parity the low bit
of a diagnostic counter that `m_main.c:1060` says something wipes every
frame ("OPEN BUG, find the writer"). **So the flip parity is not an
alternation -- it is whatever a wiped fallback count's low bit happens to
be.** Worth filing now and fixing away from this chain; it should be its
own counter.

**2. `mdp_free_set`'s guard at 2348 IS the intended release path, and it
is gated on the same flag.** `if (mds_pin[s] && mds_onscreen)` -- so a
cutscene evicting a table set requires `mds_onscreen` to be 0. **It is
not unfired by oversight; it is unfired because the flag it depends on
goes back to 1 mid-screen.** Same root as (the predicted) door. Fix the
flag and this guard starts working on its own.

## 166. The door prediction did not fire, and the builder read it before building. Meanwhile the eye "blackout" is not a blackout: the rom sets the backdrop black on purpose, so 100% black / 1 colour means NOTHING WAS DRAWN (2026-09-16)

### Entry 165's trigger: not present on our build

0xFFF148 reads 1 across 1550-1595 -- the entire visible life of the
chevron screen, 45+ frames, **including the one frame where 20/21 land
complete** -- and clears only between 1595 and 1610, by which point the
picture is already 100% black and the attract step has moved 0x0C ->
0x10. **There is no window where the flag reads 0 while the chevron is
displayed.**

So "clears a few frames in, screen still up, `on` returns to 1,
`mds_install` re-pins" **is not what this build does.** The rom reading of
0x91D4/0x91DC stands as a statement about the arcade; our build does not
reach that state while the screen is up, which is a different claim, and
the builder drew that distinction themselves rather than letting my
prediction stand on rom evidence alone.

**The probe still decides.** The flag is one input; `md_state_on()` can
return 2 (don't-care) independently, so `mds_onscreen`'s output has not
been read. The one-byte addition keeps its value for the reason the
builder gives: it separates *"door never opens"* from *"door opens and
something else closes it."* Their framing is better than mine was.

**And the 6611 / 14625 contradiction stands regardless of this bug.** One
comment calls 0xFFF148 the cutscene byte, the other says object marker /
slot + 1 / dispatcher 0x398E, and the rom agrees with the second. Worth
fixing on its own.

### The new defect: ~50 frames of 100% black at step 0x10 where the arcade shows the eye

Settled from rom, and it reframes the symptom.

`altered_beast_eye_attract_screen` at 0x20A0 opens with:

    020A0  clr.w   (PALETTE_RAM).l          <- backdrop pen := BLACK
    020A6  jsr     (sub_153E).l             <- 10 words to text RAM 0x410D6A

**The game deliberately sets the backdrop to black at entry.** So a frame
reading *"100% black, ONE distinct colour"* is not a blackout, a gate
fault, or a lost palette: **it is the correct backdrop with nothing drawn
on top of it.** The bug is emptiness, not blackness -- which points at a
different half of the pipeline than a blackout would.

`sub_153E` is tiny (10 words of text), so it explains no delay.

### What is supposed to fill that screen, and both parts go through paths we already suspect

**Part 1, immediately at entry (0x20B8-0x2150): four sprite objects.**

    #1  0x20BC  status 0x8000, routine glowing_logo_attract_screen,
                pos (0x1180, 0x1020)
    #2  0x20E6  status 0x8000, routine loc_23DC, pos (0x1168, 0x1019)
    #3  0x2110  status 0x8000, routine loc_228C, sprite_id 0x1DD,
                slot 2, palette_bank 0x5E,
                0x213A  jsr (RequestPaletteUpdate)   <-- the QUEUED path
    #4  0x2144  status 0x8000, routine loc_2338

**Part 2, later (called from 0x21FA): `sub_2552`, an 8-block tilemap
upload** from `word_278B8` via `sub_258A`.

**Sprite #3 requests its palette through `RequestPaletteUpdate` -- the
same queue as the transformation face (entry 163).** And a sprite whose
palette has not landed draws black.

### The discriminator, and it is cheap

*"100% black, 1 distinct colour"* is consistent with two very different
faults, and the builder's existing pen census separates them in one read:

    sprite PIXELS present, all black   -> the sprites are drawn and their
                                          PALETTE has not landed; same
                                          family as the face, and the
                                          queued path is the suspect
    no sprite pixels at all            -> the sprite objects are not being
                                          created or not reaching the FB;
                                          a different bug entirely

**Ask which, before theorising.** It costs one frame dump they already
know how to take.

### Why this may be the more visible of the two

The builder is right to flag it. The chevron renders *flat but present*;
this renders *absent*, for ~50 frames, where the arcade has a picture.
**On Mike's eye a 50-frame hole is louder than a wrong colour**, and the
eye screen is in the attract loop, so it repeats.

### Closes taken

DIAG[36] / `r60_pkt_flip` filed separately -- the parity is a wiped
count's low bit, not an alternation, and needs its own counter.

And their acceptance of 2348: a cutscene evicting a table set requires
`mds_onscreen == 0`, so **the guard is not unfired by oversight, it is
gated on the flag under investigation.** If the flag is the bug, the
guard heals itself.

## 167. The cycler OWNS NO PENS. mdp_pen_own is written only on a FREE-pen claim, and it is what drives the live CRAM refresh -- so a set that shares every pen is invisible to the refresh forever (2026-09-16)

### The builder's probe result, and why it is the last clue needed

Sets 19/20/21 are **assigned exactly twice each over ~1,600 frames, every
assignment returning success.** No refusals. `mdp_assign_set` is not the
door -- the door is open and the sets walk through it.

Their reading is right: the pens are claimed at assign time and never
revisited as the ring turns. **The question is what refreshes a line's
pens when the set's colours change underneath it.** Here it is.

### mdp_pen_own drives the refresh, and it has THREE writers -- all the same condition

`m_main.c:463` states the role outright:

    #define mdp_pen_own ((uint8_t *)0x0603D1A0) /* [3][16][2] owner
                                                 * set,pixel -- drives
                                                 * the live CRAM refresh */

And the refresh reads it at 16028-16029 (`os = mdp_pen_own[...]`, `op =
mdp_pen_own[...+1]`), repainting from the OWNER's `PAL_SH` entry.

Now every write to it in the claim path:

    2496-2497  inside  if (!pen16 && freepen)              <- FREE pen only
    2557-2558  inside  if (mdp_line_c[l*16+op] == 0xFFFF)  <- FREE pen only
    3009-3010  mds_install, table sets, rebuilt from the maps

**There is no other writer.** `mdp_claim_pen`'s three branches:

    1. SHARE a pen already holding q   -> mdp_pen_rc++ ONLY. No owner.
    2. Claim a FREE pen                -> owner written.
    3. Nearest-colour fallback         -> mdp_pen_rc++ ONLY. No owner.

**Two of the three claim paths increment a refcount and never record an
owner.**

### So: a set that shares every pen owns nothing, and the refresh cannot see it

At assign time the cycler's ring is at some phase. Its seven colours are
matched by pens already on the line holding those exact quantised values
-- because those colours are the LEVEL's colours, and the level's sets
own the line. **Every claim takes branch 1.** The cycler owns zero pens.

From then on `mdp_pen_own` names only the level's sets, the refresh
repaints only their colours, and **the ring rotates in PAL_SH with
nothing downstream watching it.** MD CRAM holds the phase that happened
to be current at assign, or whatever a later owner overwrote.

### Every number on the board is a prediction of this

    assignment succeeds, no refusals     branch 1 IS success
    no fallback storm (64 vs ~2,000)     branch 1, not branch 3 --
                                         which is why entry 164 was
                                         two orders out
    complete in exactly 1 frame of 8     the frame nearest an assign
    LOOP29 285's older 2-of-8            same shape, different instrument
    the white at MD CRAM index 14        another set's OWN claim, exactly
                                         as the builder described it --
                                         the cycler is merely pointed at
                                         a pen it does not own
    assigned only TWICE in 1,600 frames  nothing ever forces a re-claim,
                                         because nothing is watching

**The builder's own sentence -- "another set's own claim, not set 19
handed a neighbour's pen" -- is this mechanism stated precisely, and they
had it before I did.** Entry 164 got the destination right and the branch
wrong.

### And mdp_s_vol, the existing guard, cannot fire in time

`mdp_claim_pen:2466` already has the rule:

    if (mdp_s_vol[s] >= 2 && freepen)
        pen16 = 0;        /* volatile set: prefer an EXCLUSIVE pen */

`mdp_s_vol[s]` increments at 16019, on observing the set's colours
change. **But the chevron sets assign twice in the screen's whole life.**
The counter needs to be >= 2 **at the moment of the claim**, and the only
two claims happen before anything has had cause to raise it. **The guard
is correct and arrives after the only two opportunities to use it.**

### And this is where the supply arithmetic finally does work

Entry 164's supply numbers survived the builder's demolition of its
mechanism, and they are what makes the fix viable:

    needed     3 cyclers x 7 ring colours = 21 exclusive pens
    available  2-3 MD lines x 15 usable   = 30 to 45

**There is room to give all three cyclers exclusive pens on this screen.**
The constraint was never the bake's room, never the pen supply, and never
assignment -- **it is that sharing is free and ownership is not recorded
for it.**

### CHEVPEN, third and final shape

Not "free pens", not "release the level's sets". **Force the chevron sets
to take branch 2 -- exclusive pens -- at their assign**, so
`mdp_pen_own` names them and the live refresh starts tracking the ring.
`glow_chev` is the gate and `mdp_s_vol` is the existing lever; it needs
to be true *before* the first claim, not after two observations.

### The read that confirms it before any build

**Dump `mdp_pen_own` for the chevron's line (32 bytes) during the chevron
screen.** Prediction: **sets 19, 20 and 21 never appear as owners.** If
they do appear, this is wrong too and the refresh itself is the fault.

That is 32 bytes and no compile -- the same shape as the last three
reads, and the builder has been right to insist on it every time.

### Method note taken

**Grepping for a literal address is not a free-space test** -- live code
reaches scratch through base pointers, and 0x26028DE0 / 0x26028DA0 both
cost the builder a build despite zero literal hits. **The authority is
the map comment at `m_main.c:1329`, which lists neither.** Recording it
here because it applies to anything I propose that needs scratch.

## 168. Set 19 loses BECAUSE its colours are common. Its ring quantises to four values, three of which are a blue ramp the level's sets already hold -- so three claims take branch 1 (share, silent) and only white is free (2026-09-16)

### The builder's ownership dump, and the number that falls out of the quantiser

    f1560   0  85 85 85 87 85 85 20 85 85 87 20 21 21 19   0
    f1575   0  20 20 20 87 85 85 20 85 85 87 20 21 21 19   0
    f1585   0  20 20 20 87 85 85 20 85 85 87 20 21 21 19   0
    f1600   0  85 85 85 87 85 85 87 85 85 87 87 88 87 87   0

Set 19 owns **pen 14 and nothing else**, all screen, against four colours
it needs. Their question: why does 19 lose the race when 20/21 win.

**It is not a race. Run set 19's ring through `mdp_quant`:**

    0x7FFF -> q=511  rgb3=(7,7,7)   WHITE
    0x4B00 -> q=384  rgb3=(0,0,6)
    0x4C00 -> q=384  rgb3=(0,0,6)
    0x4D00 -> q=448  rgb3=(0,0,7)
    0x4E00 -> q=448  rgb3=(0,0,7)
    0x4F00 -> q=448  rgb3=(0,0,7)
    0x4900 -> q=320  rgb3=(0,0,5)
    0x4A00 -> q=320  rgb3=(0,0,5)

    DISTINCT: 4 of 8

**Eight ring entries collapse to exactly four quantised values -- which is
the builder's "four colours it needs", derived independently.** Two
instruments agreeing on 4 is worth more than either alone.

### And three of the four are a blue ramp

    q=320  (0,0,5)
    q=384  (0,0,6)
    q=448  (0,0,7)
    q=511  (7,7,7)  white

**Set 19 is pure blue in three adjacent steps, plus white.** Blue is the
most common colour on a level screen -- it is the sky. **The level's sets
85/87 almost certainly already hold q=320, 384 and/or 448 exactly.**

`mdp_claim_pen`'s branch order decides the rest:

    1. SHARE a pen already holding q   <- taken first, records NO owner
    2. claim a FREE pen                <- records owner
    3. nearest-colour                  <- records no owner

**So set 19's three blues hit branch 1 against the level's sky pens and
vanish silently. Only WHITE was not already on line 0, so white took
branch 2 -- and that is pen 14, the one pen set 19 owns.**

Which closes with the builder's own earlier observation exactly: *"set
19's lone matching white sits at MD CRAM index 14."* **It is at index 14
because that is the one colour it had to claim fresh.**

### So entry 167 was half right, and the half matters

I claimed the cyclers own no pens and take branch 1 throughout. The dump
shows 20/21 taking **branch 2** and owning seven pens between them --
**my blanket claim was wrong and the builder's correction stands.**

But branch 1 *is* what happens to set 19, for three of its four colours,
and the consequence I described is exactly what set 19 suffers: **a
shared pen is frozen at the owner's colour, and the ring rotates with
nothing watching.** The mechanism was right about set 19 and wrong as a
statement about all three.

**The discriminator is not the set. It is whether the set's colours
already exist on the line.** Sets whose colours are unusual force branch
2 and get tracked; sets whose colours are common get shared and frozen.

**Sharing is preferred, and for a CYCLER sharing is always wrong.** That
is the bug in one sentence.

### Fault (a) and fault (b) are one fix and one fix

The builder's split is right and both are needed:

  **(a) set 19 wins 1 pen of 4 from the first frame.** Cause: branch 1
  preferred over branch 2 for colours the level already holds. Fix:
  **never share for a cycler -- force branch 2.** That is CHEVPEN's third
  shape and it now has its reason.

  **(b) all three evicted at ~1595.** The builder withdrew their note-106
  negative: the clear happens at the END of the screen, not a few frames
  in, and `mds_pin` is 0 for non-table sets (2966) so the 2348 guard
  cannot hold them. **Fold 4 is real; I was predicting the right event at
  the wrong time, and they found the right time.**

Fix (b) alone gives a screen that holds one pen of four. **Fix (a) alone
gives a screen with four correct pens that lose them at 1595.** The
zigzag needs both.

### The supply arithmetic still covers it

    needed     set 19 four, sets 20/21 seven = 11 exclusive pens
    available  2-3 lines x 15 usable = 30 to 45

**Forcing branch 2 for all three costs 11 pens of 30-45.** It fits, and
it fits more easily than entry 164's 21 because quantisation collapses
the rings.

### The read that confirms (a) before any build

**`mdp_line_c` for line 0 at set 19's assign.** Prediction: **q=320, 384
and 448 are already present (branch 1), q=511 is not (branch 2, pen
14).** 32 bytes, same shape as the last four reads.

If instead all four are absent, branch 2 was available for all of them
and something else refused -- and then the claim ORDER inside
`mdp_assign_set` is the next thing to read, not the line contents.

## 169. "Four colours it needs" was MY error. I measured the palette's distinct colours; the mask measures the ART's pen indices. Set 19 with one pen is probably correct -- and the one free observation nobody has taken is what f1575 LOOKS like (2026-09-16)

### The falsification, accepted

Line 0 at f1500: `0019 0022 002A 009B 002B 0033 00E4 0034 003C 00ED 012D
0136 017F 01BF`. **None of q=320, 384, 448 or 511.** Set 19's blues were
never on the line, so the sky-collision half of entry 168 is dead. And at
f1555 seven pens read 0xFFFF -- **branch 2 was available and set 19 did
not take it.** Not outbid, not outshared, not starved.

**And the refresh works.** `mdp_pen_own` names 19 at pen 14 and the
colour tracks the ring frame to frame: 01C0 -> 0180 -> 01C0 -> 01C0. The
live CRAM refresh is doing exactly what it was built to do. **Entry 167's
consequence is dead too, not just its blanket claim.**

`mdp_claim_pen` was called once for set 19 because **its mask carries one
bit.** The builder is right that this moves off allocation entirely.

### The correction is mine, and it is worth stating plainly

**My "four colours it needs" and their mask's "one" are not in conflict,
because they measure different things.**

I ran the PALETTE through `mdp_quant` and found 8 ring entries collapsing
to 4 distinct values. That is a fact about the palette. **It says nothing
about how many pen INDICES the art uses**, and I presented it as though
it did -- which handed the builder a target number that was never the
art's.

`m_main.c:2623-2624` sets the frame I should have used: *"a System 16
background tile is 3bpp -- at most 8 pens out of its colour set."*
**Eight pen indices exist; the mask says set 19's tiles use one of them.
Both numbers are right and only one of them is about the renderer.**

### So the builder's instinct is probably correct: set 19 is correct as built

**A rotating ring whose art uses one pen index needs exactly one MD pen,
refreshed per frame -- which is what is happening, measured.** I have no
argument against that and I should not manufacture one; four mechanisms
have now died in this path (static bake, pen supply, assignment,
sharing), all four mine, all four killed by a measurement taken before a
build.

**And it implies something about the picture: set 19 alone cannot draw a
zigzag.** One pen index paints a solid region whose colour cycles. **The
zigzag must come from 19 + 20 + 21 together** -- 1 + 5 + 2 = 8 pens.

### The free observation nobody has taken

**At f1575 all eight pens are owned, correct, and tracking.** That is the
frame the builder measured 20/21 complete in MD CRAM, and set 19 has been
right the whole time.

**So what does f1575 LOOK like?** Nobody has said. Every report on this
screen has been a census -- pens, owners, colours, masks -- and the one
question that separates the remaining hypotheses is whether the picture
is correct on the frame where the data says it should be.

    f1575 looks RIGHT   -> allocation path is clean, (a) is a phantom,
                           and (b) -- the eviction at ~1595 -- is the
                           WHOLE bug. Fix it and the screen holds.
    f1575 looks FLAT    -> every pen is owned, correct and tracking and
                           the picture is still wrong, so the fault is
                           DOWNSTREAM of colour entirely: the tile
                           PATTERN remap or the nametable.

**It costs nothing. They already have the frame.**

### If it is flat, there is prior art for the downstream case, on this exact plane

`mdp_assign_set:2709-2716`:

    /* 222: a set assigned while the round is OFF screen is a cutscene's.
     * Its tags may survive from a previous visit with a different pen
     * map (216/220: THE CHEVRON PLANE DREW IN ALTERNATE ROWS FROM
     * EXACTLY THAT), so drop them here, at the moment the set gets its
     * pens. */
    if (!mds_onscreen) {
        ...
        mdp_wipe_set_tags(s);
    }

**A stale pen map on the chevron plane is a documented prior failure of
this exact screen, and the wipe that fixes it is gated on
`!mds_onscreen`.** If `mds_onscreen` reads 1 at the chevron's assign, the
wipe does not run and the tiles keep patterns converted under a previous
visit's map -- correct pens, correct colours, wrong pixels.

**I am offering this as a pointer, not a claim.** The difference from my
last four is that it is not my derivation: the codebase already says it
happened here. The check is a counter -- does `mdp_wipe_set_tags`
(MDA(14)) run for sets 19/20/21 -- and it rides the probe that already
exists.

### And (b) is untouched by all of this

The eviction at ~1595 is measured, real, and nothing in this entry bears
on it. **It stands as the one confirmed defect on this screen.**

## 170. "Scrolls in red" is the finding. sub_3A00's cutscene branch pins ALL FOUR scroll words to zero -- so a chevron screen that scrolls is displaying the WRONG WINDOW of pages 10/11, and every colour mechanism died because colour was never broken (2026-09-16)

### The observation that matters is three words long

The builder's f1575 report: *"renders flat blue, flat red, no zigzag, no
face, **scrolls in red**."*

**That screen cannot scroll.** Entry 161 settled it from rom and I did
not connect it:

    03A0A  moveq   #0,d0
    03A0C  move.w  d0,(unk_FFF0E2).w
    03A10  move.w  d0,(unk_FFF0E4).w
    03A14  move.w  d0,(unk_FFF0E8).w
    03A18  move.w  d0,(unk_FFF0EA).w
    03A1C  move.w  #$AAAA,(unk_FFF0F4).w
    03A22  move.w  #$BBBB,(unk_FFF0F6).w

**All four scroll registers are zeroed in the same six instructions that
select pages 10 and 11.** The cutscene screen is static by construction.

### The mapping is exact, both ends

The vblank copy at 0x2ACA-0x2AFC:

    FFF0E2 -> 0x410E98  = text word 0x74C   plane 0 X
    FFF0E4 -> 0x410E9A  = text word 0x74D   plane 1 X
    FFF0E8 -> 0x410E90  = text word 0x748   plane 0 Y
    FFF0EA -> 0x410E92  = text word 0x749   plane 1 Y

And `latch_layer_regs` (m_main.c:2909-2915):

    xraw = TEXT_C[0x74C + which]
    ysc  = TEXT_C[0x748 + which] & 0x1FF

**Same four words, same order. Our side reads exactly what the game
writes.** So on the chevron screen all four must read zero -- and if the
picture moves, they do not.

### What that predicts, and it accounts for the whole arc

**If scroll is not zero we are displaying the wrong 320x224 window of
pages 10/11.** Flat blue and flat red are a uniform region of the page.
**The zigzag and the face are elsewhere in the page, off-screen.**

Which explains every measurement taken in ten exchanges:

    colours correct in PAL_SH        because they ARE correct
    pens owned, tracking the ring    because that path WORKS
    art baked, 91/91 masks in VRAM   because the art IS there
    five colour mechanisms died      because colour was never broken

**Everything downstream checked out because everything downstream is
right. We have been auditing a working pipeline pointed at the wrong
part of the map.**

### And it is a better fit than the stale-tag pointer I gave them

I offered `mdp_wipe_set_tags` / 2709-2716 last message -- *right pens,
right colours, wrong pixels*. **A stale pen map does not make a static
screen scroll.** The scroll is unexplained by it, and an explanation that
leaves the loudest symptom on the floor is the weaker one. **Take the
scroll first.**

### The read, and it is four words

**`TEXT_C[0x748]`, `[0x749]`, `[0x74C]`, `[0x74D]` during the chevron
screen.**

    all four ZERO      -> scroll is honoured; the movement comes from
                          somewhere else and the stale-tag pointer moves
                          back up the list
    any NON-ZERO       -> the cutscene branch's zeroing is not reaching
                          our latch, we are windowed into the wrong part
                          of pages 10/11, and THAT is the bug

Four words. No build. Same shape as the f1575 test.

### If they are non-zero, the next question is already framed

`sub_3A00` has two branches and writes the scroll words in **both** --
the cutscene branch writes zeros (0x3A0C-0x3A18), the in-game branch
writes computed values (0x3A44, 0x3A6E). **So the question would not be
"who zeroes it" but "does our text-RAM mirror carry the cutscene
branch's write, or a stale in-game value from before the transformation
started".** That is the same class as the page selects, which do land --
so the page words update and the scroll words may not, in the same
vblank copy.

### On the arc

Five mechanisms of mine died in the allocation path and the builder
killed each with a measurement taken before a build. **The f1575 test
only closed it because they took it and reported what they saw rather
than what the census said.** *"I had the frame in hand for two days and
never looked at it"* -- that is the same failure I made five times in a
row from the other direction, and it is worth both of us carrying
forward: **the census answers the question you asked; the picture
answers the one you did not.**

**(b) is still the one confirmed defect and still worth building.** This
does not displace it -- but if the scroll words are non-zero, (b) is a
real bug on a screen that was pointed the wrong way regardless.

## 171. Bug LOCATED by the builder: owned pens are never repainted from PAL_SH. And one arithmetic point -- all eight holding the SAME value is not a phase freeze, it is a claim that happened before the palette landed (2026-09-16)

### Scroll falsified, and the ambiguity was language not data

All four scroll words read 0000 at f1560/1575/1600 with pages AAAA/BBBB
-- exactly `sub_3A00`'s cutscene branch. And the mirror is live, not
stale: f300 reads 1212/6767 scroll 00C0, f700-1400 steps 0192 -> 0189 ->
017D -> 0146, f1900 reads 0000/5555. **Entry 170 is dead.** *"Scrolls in
red"* meant the decorative flame ornaments, not motion.

### The located bug

Line 0 at f1575, owner and held colour against what the ring says:

    pen  1: set 20 pixel 3  holds 0007  should be 02F
    pen  2: set 20 pixel 5  holds 0007  should be 01F
    pen  3: set 20 pixel 6  holds 0007  should be 007   <- right by luck
    pen  7: set 20 pixel 1  holds 0007  should be 03F
    pen 11: set 20 pixel 2  holds 0007  should be 037
    pen 12: set 21 pixel 4  holds 0007  should be 027
    pen 13: set 21 pixel 1  holds 0007  should be 03F
    pen 14: set 19 pixel 1  holds 01C0  in set 19's ring, OK

Ownership correct and distinct across five pixel indices; **every pen
holds the same colour.** And no writer of `mdp_line_c` refreshes from
PAL_SH per frame -- 2433 (free), 2495 (claim), 2556, 2818 all write at
claim or free time, and 16028 re-quantises PAL_SH only as the drift
detector (DRQR[6]).

**That is the first thing in this arc that is located rather than
suspected, and the builder found it.**

### And it corrects entry 169's retraction back the other way

I withdrew 167's consequence when the builder showed pen 14 tracking. They
now identify that as **the drift path firing on one pen, not a refresh
reaching all eight.** So 167's consequence was right and my withdrawal
was premature -- I gave up a correct claim on one pen's worth of
counter-evidence.

### The one arithmetic point, and it may make their fix cheaper

**All eight pens holding the IDENTICAL value is not a phase freeze.**

Set 20's ring rotates. Frozen at any phase P, the pen for pixel p holds
`ring[(p + P) mod 8]` -- **eight different values, all of them valid ring
colours.** Observed is one value, eight times.

The correct values are a ramp that differs only in GREEN:

    007 = b0 g0 r7      027 = b0 g4 r7      037 = b0 g6 r7
    01F = b0 g3 r7      02F = b0 g5 r7      03F = b0 g7 r7

**r=7 throughout, g stepping 0 to 7 -- the yellow-to-orange flame
gradient.** Every pen sits at g=0, the bottom of it.

**So the claims did not read a rotated ring. They read a PAL_SH that was
uniform at that moment** -- which is what PAL_SH looks like before the
transformation's four queued palette updates land. `sub_90F4` queues them
(0x9180/0x9192/0x91A4/0x91B6) and the arcade drains in the NEXT vblank
(entry 163). **If the assign runs on first tile sighting, it claims a
frame before the colours arrive.**

That fits "assigned exactly twice, early" and it fits one-of-eight being
right by coincidence.

**It does not change whether their fix works** -- a per-frame repaint
corrects a pre-palette claim and a rotating ring alike, and a cycler needs
the repaint regardless. **It may let them do it cheaper**: if the cause is
claim-before-palette, a single re-claim when the palette lands fixes the
static half, and the per-frame repaint is then only needed for the
rotation itself.

**Offered as a sharpening, not a blocker. They should build.**

### Standing

Two changes, both small, both the builder's design: repaint owned pens
from PAL_SH per frame for rotating sets, and hold the chevron's sets
against the ~1595 eviction. **First thing all week that moves a pixel.**

## 172. THE ZIGZAG DRAWS. The mechanism was mdp_pen_rc == 1 at 16082 -- the in-place recolour fires only on SOLE-OWNED pens, which is why pen 14 tracked and the other seven froze (2026-09-16)

    non-field centre   distinct colours
    arcade      31.2%                22
    bldS         0.0%                 3   FLAT
    chevfix     83.3%                 2   ZIGZAG DRAWS

**First pixel moved in this arc.** Built behind CHEVFIX=1, not a play
candidate.

### The mechanism, and it is the builder's

`m_main.c:16082` gates the in-place recolour on **`mdp_pen_rc == 1`**. A
SHARED pen falls through to tolerated drift and is never repainted.

**That is the missing piece.** It explains both halves of the evidence
that made me withdraw entry 167:

    set 19's pen 14 tracked      sole-owned, rc == 1, recolour fires
    the other seven froze        shared, rc > 1, recolour skipped

So 167's consequence was right, my withdrawal in 169 was wrong, and the
builder says so themselves. **I gave up a correct claim on one pen's
worth of counter-evidence, and the unease about doing it was the right
instinct to have acted on.** Worth carrying: a single counter-example to
a mechanism is a reason to ask which case it is, not to withdraw.

The fix is CHEVPEN's branch-2 shape after all -- force free pens under
`glow_chev` -- plus holding the sets in `mdp_free_set` against the ~1595
eviction `mds_pin` cannot stop.

### The remaining colour error is entry 171's arithmetic, confirmed

Olive and black, two values. **Eight pens at one identical value was never
a phase freeze** -- a freeze gives eight different ring values. It is a
claim against a uniform PAL_SH, taken before the transformation's four
queued updates drain (entry 163). Exclusive pens now, still a frame
early.

**Their next step is the right one and it fell out for free**: re-claim
once when PAL_SH actually changes, and let the existing per-frame
recolour carry the rotation -- which it already does for sole-owned pens.

### Two numbers to watch before this goes to Mike

**1. 22 distinct on the arcade against 2 today.** After the re-claim
lands, expect roughly **8** -- the pens sets 19/20/21 own between them
(1 + 5 + 2). Quantisation explains part of the 22 -> 8 gap (entry 168:
set 19's eight ring entries collapse to four in 3-bit), **but not all of
it. The rest is pen budget, and the supply arithmetic says there is
room** -- 11 owned against 30-45 available. **Do not read ~8 as done; read
it as the next question.**

**2. 83.3% non-field centre against the arcade's 31.2%.** Ours covers
**more** than the arcade does. That may be the metric rather than the
picture, but it is a 2.7x overshoot on the one geometric number we have
and it is worth one look before a play pass rather than after.

### Standing

The eye screen and the ~1610 blackout are untested against this build.
**(b) is fixed and (a) is fixed; the colour is one change away.**

## 173. Ornaments draw. The remaining question is arithmetic, not a mechanism: SEVEN distinct values are expected in mdp_line_c and THREE are there. Here are the seven (2026-09-16)

    chevfix2   83.3% non-field, 3 distinct   zigzag + ornaments

The re-claim fires once per set at the uniform->ramp transition, let
through the (b) hold by a `chev_force` flag. **Geometry close to
complete.**

### Their classifier catch is the more valuable half of that message

*"Field colour is defined in my file as the arcade's blue/red/orange/
black. Once our palette is olive, every pixel reads non-field by
construction."* **So 83.3% measures the classifier failing, not
coverage, and it is meaningless until the colours are right.**

They caught it before a play pass and retired the number themselves.
**That is the fourth instrument in this arc that was lying, and the first
one caught by its own author before it reached anybody.**

### The remaining fault, stated as arithmetic

Pens are exclusive, the art indexes them correctly, and they hold the
wrong values. **So this is no longer about allocation at all -- it is
about which colours reach `mdp_line_c`.** From the builder's own
"should be" list plus entry 168's ring:

    set 20, five pens   0x007  0x01F  0x02F  0x037  0x03F
    set 21, two pens    0x027  0x03F        (0x03F shared with 20)
    ---------------------------------------------------------
    union                0x007  0x01F  0x027  0x02F  0x037  0x03F   = 6
    set 19, one pen      one of 0x140 0x180 0x1C0 0x1FF, rotating   = 1

    EXPECTED DISTINCT AT ANY INSTANT:  7
    OBSERVED:                          3
    MISSING:                           4

**Four of seven values never reach the line.** That is the whole
remaining defect, quantified, with the target list to diff against.

`mdp_line_c[l*16+pen] = mdp_quant(PAL_SH[s*8+p])` is the only assignment.
**PAL_SH has already been shown correct at f1575** (their dump matched the
arcade word for word), and `mdp_quant` is deterministic. **So either the
re-claim reads PAL_SH at a moment when only some of the eight entries
have updated, or fewer than seven claims actually run.**

**Both are counters, not derivations, and both ride the probe that
exists.** I am not proposing a mechanism -- five have died and the
builder's framing is right that this is a different question. **Dump
`mdp_line_c` for line 0 against the seven above; the missing four name
the failing pixels directly.**

### The withdrawal lesson, both directions

They took it: *"pen 14 tracked because it was sole-owned; that was a clue
about `mdp_pen_rc`, and I read it as a refutation."* **It cost me a
correct mechanism and it cost them a build.** Recording it as the durable
finding of this arc, above any of the code:

**A single counter-example to a mechanism is a reason to ask WHICH CASE
it is, not to withdraw the mechanism.**

### And the bar, which has not moved

The builder states it plainly and I am recording it here so it is in the
log and not only in a relay: **MOTION is 9.3 fps against a bar of 60, and
`__ramtext` is 27,072 bytes against a 4 KB cache.** Nothing this week
touched either.

**The attract screen is a visible defect worth fixing. It is not the
bar.** The `_m_main` split is.

## 174. The builder caught their own regression on the diff. Stale mdp_pen_own is real but probably BENIGN at runtime -- it is a DIAGNOSTIC hazard, and PEN_HOLD is the thing that will bite the (c) fix (2026-09-16)

### What they found, and it is a good catch on themselves

In chevfix2, sets 19/20/21 own **zero** pens. Line 0 reads nearly all
0xFFFF while the owner bytes still say 85/87/88. **(c) frees the sets and
nothing re-assigns them, because (b) is holding those same sets against
`mdp_free_set`. The two changes fight.**

And it retracts their own picture reading: *"ornaments now render"* was
art drawing through a nearly empty line, and 2 -> 3 distinct came from
emptiness, not correctness. **They reported an improvement that was a
regression and caught it one message later, on a diff they asked for.**

**chevfix (a+b, no re-claim) is the better build**, and it is the state
entry 173's seven-value diff was designed to interrogate. The arithmetic
stands; it was pointed at the wrong rom.

### The stale owner: real, and I would not inflate it

`mdp_free_set` clears `mdp_line_c` at 2433 and does not clear
`mdp_pen_own`. Confirmed -- the only clears are 2988 (`mds_install`) and
10809 (init).

**But at runtime it is probably harmless.** A freed pen reads 0xFFFF, no
tile indexes it, and the next `mdp_claim_pen` writes owner and colour
together. The owner refresh repainting a free pen writes a value nothing
reads.

**Where it is NOT harmless is in a dump.** It made `mdp_pen_own` say
85/87/88 for pens that were free, and that is exactly what misled the
builder into reading chevfix2 as an improvement.

**The durable rule, and it would have saved this round:**

    never read mdp_pen_own without masking on mdp_line_c != 0xFFFF

**An owner byte is only meaningful for a pen that holds a colour.** Worth
putting in the probe rather than in anybody's memory -- that is the fifth
instrument in this arc to mislead, and the second to do it by reporting
stale state as live.

### PEN_HOLD is what will bite the (c) fix

Their plan -- *"the free needs a matching assign in the same breath"* --
is right in shape, but `mdp_free_set:2428` has a wrinkle that changes
what "free" means:

    #ifdef PEN_HOLD
    /* LOOP29 158: HOLD THE PENS ... this loop releases the set's pens,
     * another set takes them, and the re-assign cannot go home ...
     * Keeping the refcount RESERVES them across the gap. */

**Under `PEN_HOLD` the free deliberately does NOT release the refcount.**
So a free-then-assign pair does not behave like release-then-claim: the
pens stay reserved, and whether the re-assign lands on the same pens
depends on `TAGKEEP`'s land-where-you-were path (2770-2790), not on the
pens being available.

**Which flags are on in chevfix decides whether (c) even can work as
written.** Worth checking before building it, not after -- it is a
`.build_flags` read, not a compile.

### Standing

  * Run the seven-value diff against **chevfix**, not chevfix2.
  * Fix (c) so the free and the assign are one operation, with
    `PEN_HOLD`'s semantics accounted for.
  * **The bar has still not moved: 27,072 bytes against 4 KB, MOTION 9.3
    against 60.**
