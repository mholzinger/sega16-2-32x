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
