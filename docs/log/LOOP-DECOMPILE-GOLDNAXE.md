# LOOP-DECOMPILE-GOLDNAXE — the Golden Axe decompile thread

Format as `docs/log/LOOP-DECOMPILE.md` (Altered Beast). Brief:
`docs/handoff/HANDOFF-DECOMPILE-GOLDNAXE.md`. Provenance rule (AB entry
50): a claim names the instruction that consumes the value, or the
running frame that showed it, or it is marked HYPOTHESIS.

Set: `goldnaxe` (set 6 US, i8751 317-0123A), MAME 0.288, arcade oracle
`mame goldnaxe -rompath ./mame`. Program image `roms/goldnaxe/prog68k.bin`.

---------------------------------------------------------------------
## 1. Rung 1: the program image (2026-09-12)

`roms/goldnaxe/prog68k.bin` = `epr-12545.ic2` at the even byte,
`epr-12544.ic1` at the odd byte, 524,288 bytes. Byte order taken from
MAME's own load table, not guessed: `mame -listxml goldnaxe` reports
`maincpu off=00000000 epr-12545.ic2`, `off=00000001 epr-12544.ic1`
(the `ROM_LOAD16_BYTE` pair in `segas16b.cpp`).

Proof the order is right (a swapped image would fail all three):

    offset 0   initial SP  0xFFFFFF00     top of work RAM (region 3 below)
    offset 4   reset PC    0x000400       inside the image
    vectors    IRQ1-3,5-7 -> 0x40C (rte); IRQ4 -> 0x404 -> bra 0x2F60

The boot at 0x40E disassembles as a boot (`m68k-elf-objdump -D -b
binary -m 68000`): clears d0-d7/a0-a6, writes 0x80 to 0xC43007 and
0xC40001 (same two I/O writes AB's boot makes), clears 0xFFC000-0xFFFFFF
(3840 longs = 15 KB, keeping byte 0xFFEC00 across the clear), then
`jsr 0x6AC0; jsr 0x4D76` and the DSW decode. So the same 315-5296 I/O at
0xC4xxxx and the same 16 KB work-RAM window as AB.

**What the boot does NOT do: program the 315-5195.** AB's boot copies a
16-byte mapper table from ROM 0x1986 to 0xFE0020 at 0x440; Golden Axe's
boot has no such copy. The only `0xFE0020` literal in the image is at
0x58822, in a routine at 0x5880E that copies 16 bytes from 0x2F89 — an
ODD address in the middle of the IRQ4 handler (the bytes there are the
`lea 0x1F2000,a0` instruction) — and nothing references 0x5880E/0x58814
by absolute address. HYPOTHESIS: stale code from another link of the
program, never reached. Rung 4's trace will say whether it ever runs.

The mapper is programmed by the MCU alone. Entry 2.

---------------------------------------------------------------------
## 2. THE MEMORY MAP IS NOT ALTERED BEAST'S, and the table in the MCU ROM at 0xFEA is a decoy

The brief says region 0 is 512 KB and "same board as AB"; the rest of the
map was assumed AB's (tile RAM 0x400000, sprites 0x440000, palette
0x840000). Byte search seemed to confirm it: `317-0123a.c2` holds, at the
same offset 0xFEA as AB's MCU, the byte-identical table
`02 00 02 08 00 3F 00 FF 04 44 0D 40 00 84 00 C4`.

**That table is never used.** Captured from the running arcade
(`tools/s16b_map_probe.lua`, a write tap on the i8751's external data
bus, MAME space `xdata`, where every 315-5195 register write lands):

    frame 6, MCU -> regs 10..1F:  02 00 | 00 1F | 08 1E | 00 FF | 04 20 | 0D 10 | 00 14 | 00 C4

That table lives at MCU ROM 0x522, loaded by `MOV DPTR,#0522h` at MCU
0x515. Decoded with `315_5195.cpp compute_region` (size code &3 -> 64K /
128K / 512K / 2M; base = byte<<16 & ~mask) and the region roles from
`segas16b.cpp memory_mapper` (case 7 I/O, 6 palette, 5 tile+text, 4
sprites, 3 work RAM, 2/1/0 board-specific):

| r | bytes | 68K range | what | AB's |
|---|---|---|---|---|
| 0 | 02 00 | 0x000000-0x07FFFF | program ROM, 512 KB | 0x000000 (256 KB used) |
| 1 | 00 1F | 0x1F0000-0x1FFFFF | 171-5797 bank/math: **tile bank at 0x1F2001/3**, 315-5248 multiplier at +0x0000, compare/timer at +0x1000 (`rom_5797_bank_math_w`, segas16b.cpp:1017) | 0x080000 (2nd ROM, empty) |
| 2 | 08 1E | 0x1E0000-0x1EFFFF | 5797 region 2, `unknown_rgn2` (segas16b.cpp:931); 49 writes in 1200 frames from 0xABxx | 0x3F0000 tile bank |
| 3 | 00 FF | 0xFF0000-0xFFFFFF | work RAM 16 KB, effective 0xFFC000-0xFFFFFF | same |
| 4 | 04 20 | 0x200000-0x20FFFF | sprite RAM 2 KB — **BUT SEE ENTRY 3, THE MCU MOVES IT** | 0x440000 |
| 5 | 0D 10 | 0x100000-0x11FFFF | tile RAM 64 KB at 0x100000, text RAM 4 KB at 0x110000 | 0x400000 / 0x410000 |
| 6 | 00 14 | 0x140000-0x14FFFF | palette RAM 4 KB | 0x840000 |
| 7 | 00 C4 | 0xC40000-0xC4FFFF | I/O 315-5296 | same |

Cross-check against what the 68K actually writes (same probe, tap
reinstalled every frame, 1200 frames of attract, by 64 KB page):

    10xxxx  247,475   tile RAM   (0x39AE fills it: lea 0x100000 / 16384 longs)
    11xxxx   58,662   text RAM   (0x3972, 0x5854, 0x3EF6 ...)
    14xxxx   12,120   palette    (0x3C8C x6144, then the 0x1172.. cyclers)
    1Fxxxx    4,285   tile bank  (0x2F96 = the IRQ4 movep, 2 per vint) + math at 0xA8D6..
    1Exxxx       49   region 2
    20xxxx    2,352   sprite RAM at its boot base
    50/60/70/80/90xxxx  401,270   sprite RAM at the MCU's OTHER bases (entry 3)
    C4xxxx    2,316   I/O (0x3098/0x30A0 every vint: 0xC40001 <- 0xFFEC18, 0xC43001 <- 0xFFEC94)

Every page the 68K writes is explained by the table; nothing lands
outside it except the moving sprite base. **The table at 0xFEA is what
a byte search finds and it is wrong for this title.** The kit rule that
follows: the S16B memory map is captured from the MCU's register writes
on the running arcade, never read out of a ROM.

Probe trap, paid once: `install_write_tap` installed at script start
reads 0 writes for the whole run, because `update_mapping()`
(315_5195.cpp) unmaps 0x000000-0xFFFFFF on every region-register write
and drops the tap with it. Reinstall the tap every frame (the tool does).

---------------------------------------------------------------------
## 3. THE MCU MOVES SPRITE RAM EVERY FRAME and tells the 68K where it went

The 68K's sprite-list copy (0x3018-0x3082, `move.l (a3)+,(a2)+` x3 +
`addq #4,a2` per record, source lists at 0xFFF400/0xFFF800, terminator
`move.w #-1,(a2)` at 0x3084) writes through a2, and a2 comes from
`move.w 0xFFECC4,d0; swap d0; movea.l d0,a2` at 0x2F80-0x2F86 — the
high word of the long at 0xFFECC4, which boot sets to 0x00200000 at
0x586. Only two other sites read it (0x39BA, 0x568CE, both
`movea.l 0xFFECC4,a0` before an 80-record clear) and NO 68K instruction
writes it after boot.

**The MCU writes it, and remaps region 4 to match, every vblank.** From
the xdata tap, frames 12-16, identical each frame:

    regs 10-1F <- 02 00 00 1F 08 1E 00 FF 04 [50] 0D 10 00 14 00 C4   region 4 base = 0x50
    regs 0A,0B,0C <- 7F F6 62  (= 68K address 0xFFECC4)
    regs 00,01    <- 00 50
    reg  05       <- 01        (write latch: 0xFFECC4 <- 0x0050)

Observed on the 68K side: 0xFFECC4 reads 0x00500000 from frame 7,
0x00000000 at frame 11, 0x00200000 at frame 12 (boot re-store), 0x00500000
from frame 13; at frame 1200 it reads 0x00200000 again and 0x500000 reads
back as open bus (`FF00 FF20 FF0F ...`) while 0x200000 holds live sprite
records. Over 1200 frames the sprite writes landed in pages 0x20, 0x50,
0x60, 0x70, 0x80 and 0x90, so the base rotates through at least six
values. This is 317-0123A's protection: sprite RAM is wherever the MCU
says this frame, and a program that ignores 0xFFECC4 draws nothing.

**For the port this is good news.** Our shim replaces the MCU
(NOTES.md "i8751 MCU — it's the system's conductor"), so it owns
0xFFECC4: set it ONCE to the port's sprite staging buffer and never
rotate. The sprite writes are already indirect, so no patch site is
needed for them at all — the opposite of AB, where 0x440000 literals had
to be rebased. NOTES-FROM-DECOMPILE-GOLDNAXE 1.

The rest of the MCU's per-vblank protocol, as captured (the CONSUMING 68K
instructions are rung 5 work; until then these are observations, not
mailbox claims):

    reg 04 <- 0B                              raise IRQ4 (the 68K's vblank comes from the MCU, as AB)
    read  0xC41002, 0xC41006, 0xC42002, 0xC42000   P1, P2, DSW2, DSW1 through the mapper
    write 0xFFECD0 <- FFFF, 0xFFECD2 <- FFFD   (P1/P2 images? inverted; HYPOTHESIS)
    write 0xFFEC96 <- FFxx                     (coin/service byte? HYPOTHESIS)
    write 0xFFECD8/DA/DC/DE <- 048C 159D 26AE 37BF   a constant signature, every frame
    read  0xFFECD4, 0xFFEC1E, 0xFFECFC          (busy/handshake candidates)
    frame 10: reads 68K ROM 0x714..0x7FE, then reg 06 <- 02, reg 02 <- 00
              (ROM checksum before releasing the 68K from reset, as AB's
              MCU checksums 2 KB; THE PATCHER MUST LEAVE 0x714-0x7FE
              BYTE-EXACT or emulate the check in the shim)
    frame 11: reg 03 <- 40                      sound latch init/silence (AB's MCU writes the same 0x40)

4,117 register writes in 1,200 frames = 3.4 per frame after boot.

---------------------------------------------------------------------
## 4. Rung 2 started: rig, census, discriminator candidates

`tools/ghidra_run.sh` takes `GAME=goldnaxe` (project `goldnaxe`, image
`roms/goldnaxe/prog68k.bin`, census default
`docs/audit/goldnaxe/timing_census.json`, gitignored). Import + auto-
analysis: 360 functions, 207 backward branches. `tools/ghidra/
timing_census.py` carries a per-game hardware table (`HW_GOLDNAXE`, the
map of entry 2); the census re-run on it has 1,392 hw refs: workram
1313, palette 22, bank_math 16, textram 11, io 9, tileram 8, rgn2 8,
frameflag candidates 3, mapper 1, sprite_base_var 1. Those are LITERAL
sites only — the tile fill at 0x39AE and the sprite copy write through
registers and are invisible here, which is why rung 3 runs on the arcade.

Arcade attract, headless snapshots every 240-300 frames to 5400:

    f240   FBI "Winners Don't Use Drugs" (US set)
    f480   title logo mid-animation, "INSERT COIN"
    f720   title: full GOLDEN AXE logo, SEGA 1989 — unique, asymmetric, full-bleed
    f1200  attract demo: forest stage, hero + red silhouettes (the shadow/hilite look)
    f1500  back on the title

Layer registers at those frames (text RAM + 0xE80.., names from AB entry
11 / `jts16_mmr.v:94-105`; base 0x110000 here):

    f720   scr1/scr2 pages 1100 / 2222   vpos 0000 / 015F   hpos 00C0 / 00C0
    f1200  scr1/scr2 pages F1E0 / F7E6   vpos 00BF / 00BF   hpos 009C / 00A5
    f1500  scr1/scr2 pages 1111 / 0000   vpos 0000 / 0000   hpos 00C0 / 00C0

**The attract is deterministic under no input**: two independent
headless runs gave identical register words at all three frames and
identical snapshot MD5s (63145ae6.. / cc9a51bd.. / ded06635..).

**DISCRIMINATOR PINNED: frame 1200 of the no-input attract.** Both
planes carry non-neutral X (0x9C, 0xA5; 0xC0 is the neutral value that
validates both signs, TOOLKIT "Geometry-convention rule"), non-neutral
Y (0xBF), and the forest art is unique and asymmetric (the big tree
trunk right of centre, the hero). The title at f720 is NOT an X-sign
discriminator (hpos 0xC0 on both planes); it pins page select and the
scr2 Y offset (0x15F) instead. Regression rule for the builder: our
rom's f1200 must match the arcade's `cc9a51bd..` frame, or the
register-to-pixel convention is wrong somewhere.

Rung 2 done.

---------------------------------------------------------------------
## 5. Rung 3: the arcade write census (TOOLKIT step a) — what changes per frame

`tools/s16b_write_census.lua` (new kit tool: the arcade-side census, per
region, tap reinstalled per frame, sprite RAM classified by the live
base variable). Run: coin f600, `1 Player Start` f800 (Golden Axe's
field name — `P1 Start` does not exist here), Button 1 at f900 picks the
first hero, walk right + attacks; window f1500-f3000 verified by its own
snapshots: f1500 = the "His majesty and the princess" intro, f3000 =
stage 1, three enemies on screen. Output `docs/audit/goldnaxe/write_census.txt`.

    region      total   writes/frame in the window   extent
    tileram    249,508        0.0                    100000-10FFFE
    textram    265,095      142.5                    110000-110FFE
    palette     55,472       27.9                    140000-1409DC
    spriteram  461,062       42.3                    200000-900500 (six bases)
    bank_math    6,261        2.2                    1F0000-1F2002
    rgn2           588        0.4                    1E0000-1E0008
    io           5,905        2.0                    C40000-C43034

**Tile RAM: ZERO writes per frame during play.** All 249,508 land before
f1500: `0x39AE` (the 16384-long fill, 183,402), `0x2012/0x2038/0x2026`
(row unpacks into 0x100000-0x10BFFE), `0x206A` (0x10C000-0x10EFFE). Stage
1 is a static tilemap loaded at the cut; the planes then move by scroll
registers only. AB's level 1 also streamed nothing per frame, so the
kit's "load at the cut, scroll after" shape holds; the difference is
where the cut happens (rung 7).

**Text RAM is the heavy per-frame writer, 142.5/frame**, and the extents
split it: `0xC918` 44,790 writes to 0x11005A-0x11016C and `0xC900`
29,860 to 0x1100C4-0x110156 (the HUD rows, top of the text map);
`0x3EB4/0x3EBE` 38,228 to 0x110746-0x110CF8 and `0x3EF6` 18,585 to
0x110BCE-0x110C60 — those are above the 64x28 text map (0x000-0x6FF)
in the row/column scroll table area. HYPOTHESIS until the consuming
instruction is read: Golden Axe uses per-row scroll during play (the
forest's parallax), and that table is rewritten every frame. If so it is
a per-frame delivery unit the AB port never needed (AB's rows are
static), and it sizes rung 7's answer.

**Palette: 27.9/frame, a FIXED 24-word footprint.** `0x3C8C` clears
0x140000-0x1407FE at cuts (6,144 = 3 full clears); during play the
writers are the block 0x1172-0x11A6, each writing one word pair in
0x140050-0x14007E every frame (3,672 = 2.4/frame each, 16 sites). That
is pens 0x28-0x3F of palette set 0: a colour cycler with a 24-word
footprint, versus AB's paired 128-word pushes (TOOLKIT step c). The
delivery unit for Golden Axe's per-frame palette is 48 bytes, if the
consumer confirms the block is the only per-frame writer (extent
0x1409DC says something wrote pens up to 0x4EE at least once).

**Sprite RAM: 42.3/frame through one copier.** `0x302A/C/E` (150,620
each = 3 longs per record) and the terminators at `0x308A/0x308E` every
frame; the 80-record clears `0x39CC/0x39CE` at cuts. Extents run
0x200000-0x900500 because the base rotates (entry 3). 42 writes/frame is
14 records/frame of 16 bytes: light.

**Golden Axe uses the 5797 board's math chips.** `0xAB6E/0xAB76` write
the 315-5248 multiplier at 0x1F0000/2 (84 each), `0xAE3E-0xAE78` write
the compare/timer at 0x1F1000-0x1F1008 (29 each), and `0xABC8-0xAC02`
write region 2 at 0x1E0000-0x1E0008 (84 each) — MAME's `unknown_rgn2`,
which this game exercises 588 times in 3000 frames. AB used none of
these. **The port has to answer their READS**, which a write tap cannot
see: rung 4's trace must capture reads of 0x1F0000-0x1F1FFF and
0x1E0000-0x1E000F, and the shim needs a multiplier/compare emulation
(jtcores `jts16b_mul.v`, `jts16b_timer.v` are the spec).
NOTES-FROM-DECOMPILE-GOLDNAXE 4.

**I/O: two writes per vint**, `0x3098` 0xC40000 <- 0xFFEC18 (the
display/flip/coin-counter byte — display gate is bit 5, same port as AB)
and `0x30A0` 0xC43000 <- 0xFFEC94.

**One direct sound-latch write.** `0x367E` writes 0xFE0006 once in 3000
frames — mapper reg 3, the Z80 latch — the only 68K touch of the latch;
AB's 68K never touched it (the MCU posts). Rung 6 decides whether the
posting convention is "MCU mailbox, with one direct exception" or
something else.

Rung 3 done. The three numbers the builder needs: per frame in play,
text 142.5 (with a probable row-scroll table), palette 28 over 24
words, sprites 42; tiles 0.

---------------------------------------------------------------------
## 6. Rung 4: the frame protocol and the 68000's work per vint

**The trace.** `tools/arcade_trace.lua` now takes any play driver
(`AT_PLAY=tools/auto_goldnaxe.lua`, new: coin f600, `1 Player Start`
f800, Button 1 f900 picks the first hero, walk right + attacks);
`tools/arcade_trace.py` takes the title's addresses
(`AT_IDLE=0x3C9C,0x3CA0,0x3CA2,0x3CA8,0x3CB0,0x3CB8,0x3CC0 AT_IRQ4=0x2F60`)
and now counts collapsed wait-loop iterations as idle (it counted
listed lines only, which reported the wait as 0.19%; entry 88's
correction applied to the wait as well as the total). Eight frames of
stage-1 play from f2400, three enemies on screen:

    executed per vint          15,279     (AB entry 88: 14,209)
      the frame wait            6,415     42.0%   flag spin + signature spin
      WORK                      8,864     (AB: 8,184, +8%)
        IRQ4 handler            2,044 / 1,989 alternating   23% of work
    arcade cycles/instruction    10.9     (166,667 / 15,279) — a normal 68000 mix, as AB
    our allowance                14.4     (127,841 / 8,864)
    CPU location at every vblank: 0x3CA0, the wait loop — the game
    finishes its frame with margin in stage 1

The same conclusion as AB's entry 88: the arcade is not bus-bound and
there are no stalls to avoid paying; the 68K margin is 14.4/10.9 = 32%
before any shim cost. Biggest routines per vint (Ghidra names, census
functions; 20.5% = 3,129 instructions fall outside Ghidra's 360
function bounds and are unattributed — AB's entry 51 problem again,
naming is rung 7): FUN_3cc8 622, FUN_c924 617, FUN_c4b2 434, FUN_bd54
333, FUN_306a (the sprite copy) 295, FUN_40e6 275.

**The frame wait, with its consumers** (`0x3C90`):

    3C90  clr.l $FFECD8 ; clr.l $FFECDC        wipe the MCU signature
    3C98  clr.b $FFEC1C                         drop the frame flag
    3C9C  tst.b $FFEC1C ; beq 3C9C              spin until IRQ4 sets it (0x2F7A addq.b #1,$FFEC1C)
    3CA2  cmpi.w #$048C,$FFECD8 ; bne 3CA2      spin until the MCU has rewritten
    3CAA  cmpi.w #$159D,$FFECDA ; bne 3CA2        its four signature words
    3CB2  cmpi.w #$26AE,$FFECDC ; bne 3CA2        (entry 3 saw the MCU write
    3CBA  cmpi.w #$37BF,$FFECDE ; bne 3CA2         them every vblank)
    3CC2  dbf d0,3C90 ; rts                     wait N frames

So the vint handshake is TWO conditions: IRQ4 must set 0xFFEC1C, and
the conductor must then write 048C 159D 26AE 37BF to 0xFFECD8-DE, every
frame, after the game cleared them. A shim that only raises IRQ4 hangs
at 0x3CA2. A second copy of the routine at 0x56BA4 (the 0x568xx block is
a partial duplicate of 0x3990-0x3D00: 776 of 880 bytes equal) waits N
frames without the signature check.

**IRQ4 (`0x2F60`, vector 0x404, rte at 0x3172)**: saves all registers;
`tst.b $FFEC1E` skips the whole frame side (-> 0x316E) when set;
`tst.b $FFEC1C` non-zero = the game had not consumed the last flag ->
`addq.w #1,$FFED4C` (a missed-frame counter, AB's 0xFFF144) and skip
to 0x30FC; else sets the flag, `movea.l` the sprite base from 0xFFECC4,
writes the tile bank (`movep.w d0,1(a0)`, a0 = 0x1F2000, value
0xFFEC95), copies the sprite lists (0x3018-0x3082, entry 3), writes
0xC40001 <- 0xFFEC18 (0x3092: display gate is bit 5 of EC18; 20 `bset
#5` sites and 17 `bclr #5` sites in the program) and 0xC43001 <-
0xFFEC94 (0x309A — NOT the sound path, see below), runs 30-frame
counters (EC20/EC21), pushes four longs from a table at 0x66ED0 indexed
by `0xFFECC3 & 7` to palette 0x140040-4F (a cycler for pens 0x20-0x27,
every vint), then calls 0x3338, 0x3314 (the sound ring pop) and 0x3586.

**The sound-post path — proven by trace, after three taps lied.**

    68K   0x3650-0x3672  push: find a free slot in the ring 0xFFEC40-5F, store, addq.b #1,$FFEC3C
    68K   0x3314-0x3336  pop (called by IRQ4 every vint): if $FFEC3C != 0, move.b (a0)+ -> $FFECFC
                          with read pointer $FFEC3E wrapping at EC60, count--
    MCU   0x82E-0x84F    main loop: read 68K 0xFFECFC through the mapper (regs 07-09, 05<-02,
                          wait reg 2 bit 6), `inc a; jz` = skip if 0xFF, else `dec a` and
                          movx @r0 with r0=3 = the Z80 latch; then rewrite the mailbox to 0xFF
                          via regs 0A-0C from the table at MCU 0x0B1A
    Z80   $0038 -> $0089 in a,($C0)   one IRQ per command
    68K   0x3674-0x367E  the direct path: move.b d0,$FFEC3C ; move.b d0,$FE0007 (mapper reg 3
                          straight from the 68K) — ran once in 3000 frames (f1077, value 0x00)

Counted in one 165-frame window (f795-f960, start + hero select):
17 ring stores at 0x366C, 17 pops at 0x3322, 17 Z80 IRQ entries at
$0038. Same convention as AB (mailbox byte, 0xFF = empty, MCU forwards
and clears) at a different address: **0xFFECFC**. Share of the 68K's
work: under 0.2% per vint.

**Three lua taps under-reported this and the MAME trace settled it.**
(1) A 68K write tap on 0xC43001 saw only 0xFF — that port is not the
latch at all, and the tap can also die mid-frame: `update_mapping()`
unmaps the whole space whenever the MCU changes region 4's base.
(2) An MCU `xdata` write tap recorded 2 writes to mapper reg 3 in 3000
frames; the Z80 got 17 IRQs in 165 of them. (3) A Z80 `io` read tap
counted 3 reads of port $C0 while the Z80 trace over the same frames
shows 17. Taps are for counting bulk traffic; anything protocol-critical
is proven with `trace` (the debugger's instruction log is exact, entry
88's tool). The write census of entry 5 stands for bulk regions (its
counts match the trace's IRQ4 rate) and is NOT to be read for
one-per-frame events.

**Also NOT sound: the 315-5296 writes.** Boot writes 0xC43007 <- 0x80
(AB's "TODO identify"), 0xC43035 <- 0x1F, 0x13, then two DSW-derived
bytes; per vint 0xC43001 <- 0xFFEC94 (0xFF throughout play). I/O-chip
configuration and outputs; which port is which is a rung-7 read of the
315-5296 (jtcores `jts16b_cabinet.v`).

Rung 4 done. The three numbers: 15,279 executed / 8,864 work per vint;
sound post < 0.2%; the biggest routine is the IRQ4 handler at ~2,000.

---------------------------------------------------------------------
## 7. Rung 5 started: the alignment carries two keys, the rest is hand work

`roms/goldnaxe/prog68k.asm` (objdump -D, 171,912 lines vs AB's 94,227)
and `python3 tools/game_derive.py altbeast goldnaxe` (3 min 0 s):
22/35 keys written, but the numbers behind them are:

    TILE_DIRTY_SITES 0/25   PAL_DIRTY_SITES 0/42   TAS_SITES 0/5
    REBASE_TABLES 0/7       TEXT_IDIOM 0/8         DATA_EXCLUDE 0/2
    FMGATE_ENTRIES 10/27    FMGATE_SPANS 3/11      HARVESTED_HANDLERS 4 of 43
    MCU_SND   0xFFF0C4 -> 0xFFECFC   (2 sites)   CONFIRMED by entry 6's trace
    MCU_COINS 0xFFF0C2 -> 0xFFEC96   (2 sites)   consistent with the MCU's per-vblank write (entry 3)
    MCU_BUSY  unmapped                           HYPOTHESIS: 0xFFECD4, the third word the MCU reads each vblank

Golden Axe is a different program, not a re-link, so the alignment
that carried altbeastj does not carry it; the 10 "mapped" FMGATE
entries and the 4 harvested handlers (0x102/0x104/0x106 = the vector
table) are alignment noise, not evidence, and `tools/game_goldnaxe.py`
says so in its banner. Two keys came out right because the MCU mailbox
idiom (read a work-RAM word, compare the high byte with 0xFF) is the
same code shape in both programs.

The honest cost line for TOOLKIT stage 3: rungs 1-4 took one session
(git: f12aa49 21:18 -> d963961 22:0x on 2026-09-12); rung 5 starts from
the census, not from AB's tables. Its order, from entry 5's writers:
palette sites (0x3C8C, the 0x1172-0x11A6 cycler block, the IRQ4 push at
0x30EE), text sites (0xC918/0xC900/0xC8CC HUD rows; 0x3EB4/0x3EBE/0x3EF6
into the 0x110746-0x110CF8 block — identify it first), tile loaders
(0x39AE, 0x2012-0x206A, 0x5880), the tile bank movep (0x2F94), the
math-chip sites (0xAB6E/76, 0xAE3E-78, 0xABC8-0xAC02, plus their READS
from a trace), the direct latch site (0x3674), and the sprite base
(none: it is the variable 0xFFECC4).

---------------------------------------------------------------------
## 8. Rung 5, palette: 53 dirty sites, one queue drain, no runtime cycler

`tools/game_goldnaxe.py` rewritten by hand (the derive output is gone;
its two surviving keys are carried). New keys the patcher does not have
yet, because this title needs them: `MEMMAP` (the source ranges of
entry 2 — patch_game.py's remap() hard-codes AB's), `SPRITE_BASE_VAR`,
`MCU_SIGNATURE`, `FRAME_FLAG`/`FRAME_SKIP_FLAG`/`MISSED_FRAME_CTR`,
`MCU_ROM_CHECK`, `PAL_PTR_USE_SITES`, and `None` for every AB idiom
that has no counterpart here (the patcher must accept None, not only
"present").

**Palette writers, all 95 literal sites in the listing classified.**
53 `PAL_DIRTY_SITES` in the first half, each with its footprint read
from the loop after it (masks are 256-byte regions of the 4 KB palette):

    every frame   0x115A (0x140050, 16 B) 0x1176 (0x140060, 16-32 B)   the 0x2DCC colour table indexed by 0xFFE037/0xFFE0B7
    every vint    0x30EE (0x140040, 16 B)                                 IRQ4, table 0x66ED0 indexed by 0xFFECC3 & 7
    at cuts       0x3C64 clear-all (0x000-0x7FF), 0x6412 (0x080-0x31F), 0x5436/0x5448/0x488C/0x60CE/0x3B60/0x7142
    init blocks   0x5A84.. and 0x6B78.. (12 `move.w #imm` each into pens 0-0x13), 0x9D82.. (6), 0x36360/68/78, 0x365A0/AC, 0x37052, 0x3711C, 0x3725C

Every site's displaced bytes were checked to carry the 0x0014xxxx
literal as their last long, the shape the patcher asserts. The one
that did not — 0x5538 `move.l #0x140720,56(a6)`, literal mid-
instruction — is a pointer stored into an object field and used at
0x55A0 (`movea.l 56(a2),a1`, four bytes); it gets its own key with the
mark-at-use rule from LOOP29 166.

**The queue drain is AB's PAL_THUNK_B idiom byte for byte.** IRQ4's
`bsr 0x3280` drains a (dst, src) queue at 0xFFF002/0xFFF006 with
`movea.l (a2)+,a1 ; movea.l (a2)+,a0` at 0x328C (225A 205A) and 7 longs
per entry — 28 bytes, same as AB's 0x2DC8/0x3C5A — so the patcher's
thunk B (region from a1, marks r and r+1) applies unchanged. The
pusher seen in play is 0x4060 (0x140800 + (d0<<5) + 2 into the queue:
the census's 0x800-0x9DC writes). **No runtime-offset cycler exists**
(0x3B5C and 0x3C82 add d0 to the SOURCE, not the destination), so
PAL_THUNK_A/APOST are None.

**42 duplicate sites in the second half** (0x53EA2-0x5CC42, a partial
copy of the low code) are listed separately: no trace has executed any
PC above 0x3FFFF except banks 01 and 03 (object handlers at 0x18Cxx-
0x1E7xx and 0x37Dxx). HYPOTHESIS: unreachable; the builder can thunk or
skip them, remap() rebases their literals either way.

Slot budget: AB's palette thunks end at 0xBCF4 where the FM-gate thunks
start (fmgate_tab.h) and the shim RAM ends at 0xBFFF; 53 + 1 sites =
0x360 bytes from 0xBA00 leaves 0x2A0 for FM-gate thunks (AB uses 27
x 16 = 0x1B0). Fits, without the duplicates.

Also corrected: the census tools' PCs are the instruction AFTER the
writer (MAME's PC in a write tap has advanced; the 0x2F94 movep reads
as 0x2F96). The tools now say so; entry 5's PCs read that way.

---------------------------------------------------------------------
## 9. Rung 5, tiles and TAS: 24 dirty sites, 8 table-offset blitters, 16 tas

All 57 tile-RAM literals classified (first half; 23 duplicates in the
second half listed apart). Footprints read from each loop:

    whole plane    0x399E (movea.l fill, the census's 183,402 writes), 0x6ED6 (fill), 0xC59A/0xC5A0 (clear, two bases), 0xC5E2 (tilemap builder)
    RLE loader     0x1FE2/0x1FEA even passes, 0x1FF2/0x1FFA odd passes: pages 0-5 and 6-B; 0x2056 bytes into pages C-E
    table-offset   0x2166, 0x50B4, 0x586A, 0x36404, 0x36484, 0x364AE, 0x36AD8, 0x37092: `lea base ; adda.w (aN)+` — AB's
                   strip-blitter precise thunk (page from the table's first word) applies; two read the word through a2/a3
    scratch        0x60F6 saves 16 KB of WRAM into pages 1-4 (0x6126 restores: read-only, remap only); 0x4C62 keeps
                   counters IN tile RAM page 2 (subq.b on (a0)+) — a read-modify-write the FB staging must honour
    small          0x6356-0x636E, 0x63AC (page 0-1), 0x73A6/0x73B6/0x73C4 (28 rows of 9 longs, one page each), 0x7AE8/0xC4B2
                   (computed offsets inside the 0x10C000 plane)
    pointer        0x731A computes a tile address into object fields; the store is 0x7398 after `movea.l 56(a6),a0`
                   at 0x7390 — mark at use (TILE_PTR_USE_SITES)

The RLE loader is AB's idiom with a different instruction layout
(`RLE_EVEN_PASS` = 0x200A, the layout is in the table's comment); the
odd pass at 0x201E has a zero-run branch AB's does not.

**TAS: 22 in the listing, 16 in reachable code**, on object fields
(73/72/120/70(a6)), 3(a0), 0xFFEC2A.w, and eight on 0x25E1(a1) /
0x3DF9(a1) in bank 04 — the MD drops TAS's write cycle, so each form
needs a shim thunk; the table lists the sites and their four bytes,
thunk addresses left for the builder.

DATA_EXCLUDE: every hardware-looking operand objdump prints was
listed (95 + 57 + 170) and all are in instruction context; the ASCII
run at 0x6818 decodes to `move.l 0x202020,d0`, outside every remap
range. Empty for now.

Correction to entry 5: the 0x110746-0x110CF8 text writes are NOT a
row-scroll table. Text RAM is a 64-column map at 128 bytes a row, so
0x746 is row 14 column 35: the intro cutscene's speech box ("HIS
MAJESTY AND THE PRINCESS..." in the f1500 snapshot), typed by
0x3EB4/0x3EBE/0x3EF6. That is the LOOP29 225 typewriter class (FM
gate the glyph store, not the state routine). Golden Axe's per-frame
text load in play is therefore the HUD rows 0-2 (0xC8CC/0xC900/0xC918
from the leas at 0xC764-0xC7A2), about 60 writes a frame, plus the
dialogue when a box is open.

---------------------------------------------------------------------
## 10. Rung 5, text and relocation: the table is complete except for what the port's own design decides

`tools/game_goldnaxe.py` now carries every key the AB patcher reads,
each either derived with its consumer named, or None with the reason.

**Text.** Layer registers: IRQ4 writes the four scroll words every vint
(0x2FA0-0x2FCA, `move.w d0,abs.l`), page selects at cuts (0x5A74/7C,
0x639E/A6) — AB's MDHSCR shape. The shared copy/clear loop heads are
AB's 0x3A9A/0x3AA4 idiom byte for byte: 0x3EB0 `move.b (a0)+,(a1)+ ;
addq.l #1,a1` and 0x3EBA `clr.b (a1) ; addq.l #2,a1` (the CLR
read-modify-write site). AB's two TXT_WRAM writers both exist here:
the credit line at 0x3EE2 (`lea 0x110000,a1 ; adda.w 0xFFEC24,a1`,
clears 9 glyphs; 18,585 census writes at row 23) and the HUD at 0xC750
(magic pots row 25 through 0xC7B0, name/score rows 0-1 through 0xC8A6,
per player from the credited flags 0xFFEC28/29 and the player objects
0xFFC000/0xFFC200). 170 text literals in the first half: 22 on row 0,
16 on row 25, 8 each on rows 4 and 10, 8 layer registers.

**Relocation.** Objects: 64 x 128 B at 0xFFC000 and 16 x 64 B at
0xFFE100, active = byte 0 bit 7, handler long at +2 (the dispatch loops
at 0x3CD0 / 0x3D12). Six `movea.l 2(a6),a0 ; jsr/jmp (a0)` funnels =
DISPATCHERS. `tools/s16b_handler_harvest.lua` read the handler field of
every active object every frame on the arcade: 37 distinct values over
5400 frames of attract plus 5400 of stage-1 play (29 / 19, 8 play-only),
all on instruction boundaries — HARVESTED_HANDLERS, stage 1 only.

Jump tables: 93 `lea pc(tbl) ; movea.l (a0,dN.w)` tables, bounded by
`tools/s16b_jumptables.py` three ways (docs/audit/goldnaxe/jumptables.txt):
22 by the consumer's own andi/cmpi immediate, 38 by adjacency (the
pointer run reaches the next table's start — the 51-entry state table
+ 7-entry table pairs of each object module, e.g. 0xEFB0 + 51 x 4 =
0xF07C), 33 by the run ending on a non-pointer long, which is the
filter AB's entry 17 warned about and stays HYPOTHESIS until each
consumer's index is read. Two of the 93 hold data pointers (the RLE
level tables 0x2A3C, 0xCCD6) and are rebased the same way.

Low reads: the score printer takes a0 = 0x0 / 0x80 (0x3AC4/DA/E0/E6) and
0x5F74/0x5F7A read the longs at 0x3F0/0x3F8 (7796 7796, FFFFFFFF) as a
compare list — six LOW_VECTOR_READS. One abs.w transfer into code:
0x5F52 `jmp 0x45C.w` — ABSW_JMP, AB's idiom.

**FM gate.** `tools/s16b_fmgate_spans.py` (the generic form of LOOP
23's fmgate_derive.py, which carried AB's censuses as literals) wraps
every FB-destined writer — 44 static sites from this table plus 40
executed writers from the census — in its rts-bounded region and
scans the whole listing for control transfers into those regions: 26
spans, 55 entries (docs/audit/goldnaxe/fmgate_spans.txt). The only
VINT-context writers are the layer registers, which are shadowed, so
every gate is MAIN-context. Which text regions are FB-destined depends
on the port's text path, so FMGATE_ENTRIES stays None and the spans
and per-span entries are in the file for the builder to cut.

**Still None, and why**: PAL_THUNK_A/APOST/LAUNCH and STRIP_BLITTER_*
(no such idiom here), TEXT_IDIOM and TXT_WRAM_CLEAR_SITES (depend on
the text remap destination), BOOT_JUMPINS/PCREL (whether the boot is
displaced into RAM is the shim design's call; GA's boot runs from ROM
0x40E-0x5A0), SPAWN_* (AB's spawn script; GA's stage scripts are
reached through the jump tables and the harvested handlers instead),
STRIDE_TABLES/IMM_OVERRIDES/DATA_PTR_NORM (the port's rebase-scan report
is the census that finds them; none surfaced in the literal scan).

Rung 5 status: the mechanical derivation is done; what remains is a
patcher that reads MEMMAP instead of AB's addresses and tolerates None,
and the hand reads the HYPOTHESIS labels ask for (33 jump-table counts,
MCU_BUSY's consumer, the 0x71F2 footprint). The kit gained three tools
that took no AB literals: the handler harvest, the jump-table bounder,
the FM-gate span deriver.
