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
