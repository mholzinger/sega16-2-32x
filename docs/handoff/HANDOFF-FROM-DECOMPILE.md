# HANDOFF — decompile thread to the new builder thread (2026-09-25)

Complementary to `docs/handoff/HANDOFF-68K-FETCH.md`, which is the
builder's own map of the 68K-time axis. **Read that one first.** This
file holds only what the DECOMPILE side has: facts derived from the
arcade ROM, the rulings that are ROM questions rather than judgement
calls, and the numbers of mine that were withdrawn so the new thread
does not inherit them.

**What this thread is for:** reading the arcade 68K program and deriving
game facts from bytes. When the builder and I disagree, that gap is the
bug. A measurement beats an argument, including mine — six of my
mechanisms died to the builder's measurements in one week and every one
of those deaths was correct.

---

## 1. THE MID-PASS PALETTE RULING — settled from rom, no judgement needed

The builder asked Mike to rule on whether skipping the mid-pass push is
acceptable, on the grounds that "the arcade's palette RAM is written
directly, so an overrun vint on the arcade shows the previous sprites
with any palette writes made so far."

**It is not written directly during the pass, and the concern is still
correct — for a different reason.**

    0x3B2E  RequestPaletteUpdate   does NOT write a palette. It resolves
                                   a slot and falls into:
    0x3BEC  QueuePaletteUpdate     appends an 8-byte entry (dest ptr,
                                   source ptr) to a 64-entry circular
                                   buffer at 0xFFFFF600-0xFFFFF800 and
                                   bumps palette_process_count.
    0x2B4C  bsr.w sprite_palette_copy   <- IN THE VBLANK HANDLER,
    0x2DBC  sprite_palette_copy         alongside the scroll copy
                                        (0x2ACA) and the sprite-table
                                        copy (0x2B16).
    0x2DC8  palette_copy_loop      drains the WHOLE queue, NO BUDGET:
                                   `subq.b #1,(a3) / bne palette_copy_loop`

**So the game QUEUES during its pass and the VBLANK HANDLER DRAINS every
vint — including overrun vints.** The arcade therefore does land palette
writes on mid-pass vints. Skipping our mid-pass push delays them by one
vint on exactly those vints. **The builder's fidelity concern is real.**

**Two further rom facts that size the trade:**

  * Each queue entry writes **14 colours / 28 bytes** to `dest + 2`
    (`0x3C1E` slot*32, `0x3C26` lea 2(a1,d0.w), source stride x28).
    **Colours 0 and 15 of every slot are NEVER written by this path.**
  * The drain has **no cap**. Whatever is queued lands that vblank.

**What is visibly at stake:** the colour CYCLERS. Sets 19/20/21 rotate a
ring every frame (the chevron/flame plane, present in all five rounds).
A one-vint delay on overrun vints makes that rotation stutter on exactly
the frames that are already slow. Fades are slower-moving and would not
show it.

**Recommendation, and it is the builder's own alternative:
palette-only push on mid-pass chains.** It reproduces the arcade
exactly — queue during the pass, land in vblank — while still skipping
the records, rowscroll and regs. **It saves less than the full skip
(the palette rotor is 41 of the chain's 84 lines and stays), so price it
before building: the full skip's ~33 lines a vint is NOT what
palette-only buys.**

## 2. Rom facts that bear on the current axis

    the game's own work        14,209 instructions/vint executed with
                               loops expanded, of which 8,184 are WORK
                               and 6,025 the frame-wait spin
                               (LOOP-DECOMPILE 88; the earlier 3,691 /
                               2,780 figures were low by 2.5-2.9x
                               because MAME collapses tight loops)
    arcade cycles/instruction  11.7 -- a NORMAL 68000 mix, NOT bus-bound.
                               The old "45.2, the arcade is stalled on
                               its video bus" was the same artefact.
    our allowance              13.3 cycles/instruction. A 14% margin
                               over the game's own mix, not a fourfold
                               one. Shim instructions cost something.
    camera velocity            table at rom 0x1878, 8 words, indexed by
                               active_enemies clamped to 7; math at
                               0x39CE gives px/frame = v/256. MAX 0x0080
                               = 0.5 px/frame. The other three writers
                               of FFF158 all write ZERO -- this is the
                               only non-zero camera speed in the binary,
                               every round, every screen.
    vertical scroll            world_yposition has FOUR writers and all
                               four write #$1020. unk_FFF0E6 has ONE
                               writer, #0. Y scroll is the constant 0x20
                               on both planes for the entire game.
    tile RAM                   no in-play writer (m_main.c:3199 /
                               LOOP-DECOMPILE 99). The tilemap is static
                               during play.
    the cutscene branch        sub_3A00's non-default branch (0x3A0C-
                               0x3A22) zeroes ALL FOUR scroll registers
                               and selects pages 10/11 (#$AAAA/#$BBBB).
                               The transformation screen is static by
                               construction. glow_chev (m_main.c:14620)
                               is the exact gate -- do not write another.
    0xFFF148                   an OBJECT marker, value = slot + 1
                               (dispatcher 0x398E). NOT a scene flag,
                               despite m_main.c:6611 calling it "the
                               game's own cutscene byte". That
                               contradiction is unresolved and is
                               load-bearing for mds_onscreen.
    the sprite record          8 words; the arcade's own field map is at
                               altered_beast.asm:58263-58277. ~74 bits of
                               defined fields in 128. +E is documented as
                               "Scratch space for current address" and
                               +C is undocumented; the builder measured
                               that NEITHER has a reader in the master.

## 3. Numbers of mine that were WITHDRAWN — do not inherit these

    "154 passes within 12 lines"   FICTION. I derived a density from two
                                   aggregate counts (ares 188 one-vint :
                                   FPGA 34) without looking at where
                                   passes actually end. The builder
                                   measured it: two-vint passes end
                                   SPREAD across their second vint
                                   (RIGSPAN=10). I called it "the best
                                   news this project has had in weeks"
                                   and it fed the item-4 ranking that
                                   converted nothing.
    "0x3A9A runs 22 times/frame"   From the ARCADE trace. Our rom runs it
                                   ~7.7 times a pass (COUNTSITES). The
                                   arcade's text traffic differs (coin
                                   state). I offered an arcade number as
                                   ours and a build was spent on it.
    cat-1 occlusion 47.7%          VOID. Pages 0-4 are the FOREGROUND,
                                   not the background (snap[0],
                                   m_main.c:2909/:15042, pagesel_census).
                                   jts16_prio.v:83-95 tests the
                                   foreground first, so tile-occlusion of
                                   cat-1 is ZERO. Entries 57, 58, 153,
                                   156, 157, 158 all quoted from the
                                   impossible direction.
    "step-3 nearest-colour storm"  Predicted ~2,000 fallbacks; 64
                                   observed. Two orders out.
    BUSES.md's per-word costs      STALE (header added 2026-09-24). Its
                                   57-line push predates FBXPORT. Use
                                   LESSONS for live costs.

**The pattern in all of them: the arithmetic was right and the premise
was unchecked.** Run the premise check in `LOOP-PROTOCOL.md` before
taking anything from this thread.

## 4. Open decompile cards in MESSAGES.md

    O-13  palette DISCOVERY re-priced against the FB transport. Item 1
          of the builder's ranking (41+18 lines). PALNOCMP exists and
          was reverted at 74.1%, but the Makefile's own note says that
          measured the shadow copy, not the idea. DEFERRED by the
          2026-08-16 priority order ("palette LAST"), which predates
          this axis. Mike has not ruled on this card. And O-13's own
          instrument note says the 94.7/82.3 ceiling is an ARES number
          not to be trusted on this axis -- the FPGA figure is the
          rotor's 41 lines a vint (STATE.md:603).
    O-10  sprite record encoding. The +C/+E half is ANSWERED (it lives
          in stage ship, item 2) and belongs inside that card, not
          separately. The delta-codec half is DEAD-ish: the palette
          rotor proves delta machinery is the expensive thing.
    O-7   lift 12 per-title WRAM constants out of engine C (~47 sites).
          TOOLKIT work, no gate, unblocks a second title.
    O-6   leftover text glyphs -- the last visible defect Mike named.
    O-1   transport ablation. PARKED with the speed axis.
    O-4   NTSKIP correctness (the row key does not fold allocator
          state). Not a speed card since the rig said flat.

## 5. What to ask this thread for

Good: anything about what the ARCADE does — timings, protocols, field
layouts, which routine writes what and when, whether a proposed change
breaks arcade behaviour. I can settle those from bytes.

Bad: execution counts on OUR rom (use COUNTSITES), anything about our
renderer's internals (you have the source and the instruments), and any
prediction I have not checked a premise for.

**And tell me when I am wrong. The record above is what that produced.**

## 6. The second WRAMCODE block set: the 11 residue PCs resolved from rom

Derived for card 1 of `HANDOFF-68K-FETCH.md` section 3. Source is
`srcref/alteredbeast/Disassembly/altered_beast.asm` for names and
`roms/altbeast/prog68k.asm` / `prog68k.bin` for every count below.
**Counts are static reference counts from OUR program image, not
execution counts.** The 66-sample profile still needs the longer FPGA
ring dump; this section says what to relocate once it does, and fixes
one boundary the residue list gets wrong.

### 6.1 Names and true extents

    entry    routine                    end     raw   emit*  residues
    00D5A6   do_collision_checks        00D776   464   464    4  (see 6.2)
    00674E   get_enemy_hit_bounds       006936   488   560    1
    00EE80   sub_EE80                   00F14C   716   728    1
    0103E0   sub_103E0                  010824  1092  1108    1
    00DABC   rock_object                00DB98   220   222    1
    003352   sub_3352                   0033BA   104   104    1
    0012EC   sub_12EC                   001306    26    26    1
    00E2C2   sub_E2C2                   00E2DC    26    26    1

*emit = raw + 2 per (d16,PC) operand + 6 per external Bcc, per the
tool's own rewrite rules. The dry run is authoritative; these are
upper-bound estimates to size the packing.

**No candidate hits a hard reject.** Zero `(d8,PC,Xn)` operands and zero
external DBcc across all eight ranges. `get_enemy_hit_bounds` is the
only one with meaningful growth: 12 external Bcc, +72 bytes.

### 6.2 Four of the eleven are one contiguous block, and the residue list's implied boundary cuts off the hottest routine

D5A6, D608, D65C and D6D8 are four entries inside a single contiguous
collision region. Taking them as separate blocks is wrong twice: their
mutual calls would become external, and the region's hot inner routine
sits past the last of them.

    00D5A6  do_collision_checks      externally reachable (6.3)
    00D5D8  simple_collision         internal only
    00D608  powered_collision        internal only, 7 internal bsr
    00D65C  simple_collision         internal only, 4 internal bsr
    00D672  always_true              internal only
    00D6CE  store_reference          internal only
    00D6D8  collision_detection      externally reachable, 1 jsr
    00D73C  collision_bounds_check   internal only, 5 internal bsr
    00D776  rock_me_up_buttercup     <- block ends here

**`collision_bounds_check` (0xD73C) is the body of the 24-iteration
loop.** `collision_detection` at 0xD6D8 does `moveq #23,d0` /
`enemy_collision_loop` / `dbf d0` calling it once per enemy, plus four
more calls for the two players and the two attack-object tables — up to
28 calls per invocation. A block that stops at 0xD73C leaves it in ROM
and gives up most of the saving. **Take 0xD5A6-0xD776, 464 bytes, one
block.**

That structure is also why this region is hot independently of the thin
sample: 44 static call sites into it (6.3), and each call runs a
24-iteration loop over the enemy array. The sample and the code agree.
**The other seven candidates rest on the 66-sample profile alone.**

### 6.3 Why the tool rejected 0xD5A6, and the fix

`do_collision_checks` is the second of a two-entry pair:

    0D5A2  sub_D5A2:            moveq  #-1,d3
    0D5A4                       bra.s  loc_D5A8
    0D5A6  do_collision_checks: moveq  #0,d3
    0D5A8  loc_D5A8:            lea    (player1_data_table).w,a4

The two entries differ only in d3. A 6-byte `jmp (abs.l)` stub at 0xD5A6
would overwrite `loc_D5A8`, which is the `bra.s` target from 0xD5A4.
**The reject is correct.**

**The fix is to write no stub at all and rewrite the call sites.** Every
reference to 0xD5A6 in the image is static and 4-byte-operand:

    43   jsr 0xd5a6           4eb9 0000 d5a6
     1   lea 0xd5a6,%a0       41f9 0000 d5a6   at 0x136B0
     0   pointer-table entries

The `lea` at 0x136B0 is the selector for the pair — it loads 0xD5A6,
then three `cmpi.b` tests can replace it with 0xD5A2 at 0x136CE, and
0x136D4 does `jsr (a0)`. All 44 are `abs.l` operands, so each rewrite is
a same-size 4-byte patch. **Leave 0x136CE (the 0xD5A2 arm) pointing at
ROM**: sub_D5A2 stays where it is, falls into the intact ROM copy, and
stays correct. It is the guarded arm, not the common path.

The one reference to 0xD6D8 (`jsr` at 0xDB52) gets the same treatment.
**The other six entries in the block need nothing** — they are reached
only from inside it, so the tool's internal-branch re-encoding covers
them.

Checked and dismissed: the byte sequence `0000 D608` appears once in the
image at file offset 0x10629, an ODD offset, spanning the operand of
`bra.w 0x10700` into the next opcode. It is not a reference.
`powered_collision` has no external caller.

### 6.4 A packing that fits the two free regions

Region A is 0xFF5BA0-0xFF5DFF (608 B), region B is 0xFF3C30-0xFF3EFF
(720 B). A block must fit inside ONE region; they are not contiguous.

    A  D5A6-D776  collision block      464      \
       3352-33BA  sub_3352             104       > 594 / 608,  6 residues
       12EC-1306  sub_12EC              26      /
    B  674E-6936  get_enemy_hit_bounds 560      \
       E2C2-E2DC  sub_E2C2              26       > 586 / 720,  2 residues

Eight of the eleven, ~1180 bytes. Left out: `rock_object` (222, no room),
`sub_EE80` (728, over region B by ~8 on the estimate — worth one dry run
against B before writing it off), `sub_103E0` (1108, does not fit in
either region).

If `get_enemy_hit_bounds` proves awkward — it is the only block needing
external-Bcc rewrites, 12 of them — `rock_object` (222) plus
`sub_E2C2` (26) is the low-risk substitute for region B at 248 bytes,
trading one residue for zero rewrites.

**Caveat on region B: 0xFF3C30-0xFF3EFF sits directly below the boot
stack at 0xFF4000-0xFF4FF0.** LESSONS "boot stack over the thunk page"
(2026-09-20) is exactly this failure: every "hardware-only" black screen
that session was the 68K boot stack overwriting code placed under it.
Put the first block there behind a check that the stack low-water mark
clears 0xFF3F00, not on the assumption that it does.

## 7. The core's ROM wait does not distinguish a fetch from a data read

Section 6 was written before wc2 landed and is spent: the builder's
block `(0xD5A8, 0xD776)` already includes `collision_bounds_check` at
0xD73C, and starting at 0xD5A8 rather than 0xD5A6 solves the two-entry
pair better than the call-site rewrite §6.3 proposed — the `moveq #0,d3`
stays in ROM and falls into the stub. §6 stands only as the extent
table and the boot-stack caveat on 0xFF3C30.

**What is still open is the inference in the builder's profile finding.**

`srcref/S32X_MiSTer/rtl/32X/IF.sv:780`:

    wire MD_ROM_SEL = VA[23:20] < 4'h4 & ADCR.ADEN;   //000000-3FFFFF

That is a pure address decode. **The file contains no reference to the
68000's function-code lines at all** (grep for `FC` in IF.sv returns one
unrelated hit, the 32X id register at :194). The `RS_MD_RW -> RS_MD_WAIT
-> RS_MD_READ` path at :877-895 is therefore entered for **every** 68K
access in 0x000000-0x3FFFFF, an operand read exactly as much as an
instruction fetch.

**Consequence for the profile.** A PC sampler attributes a sample to
where the PC is, not to where the operand lives. A cart data read issued
by a WRAM-resident routine is counted in "WRAM game code", not in "cart
game code". So the split

    WRAM game code 38% / idle 34% / shim 14% / cart game code 13%

does **not** bound the core's ROM wait at 8-10 lines a vint. It bounds
the remaining *fetch* share. The wait's data-read share is unmeasured
and sits inside the 38%.

**What is NOT established:** how large that share is. The relocated
blocks contain only 23 cart-address operands in 1,075 instructions, and
all of them are `lea` — which computes an address without reading
memory. The reads happen afterwards through those pointers
(`(a0)+` table walks), which cannot be counted statically. **No number
is offered here.**

**The measurement that settles it** is the data-access half of the rule
LESSONS already carries ("gauge instruction fetch separately from data
access"). CARTBENCH ran the fetch half: the same loop from cart 132-148
quarter-lines vs 112 from WRAM. RIGSPAN=9 already captures 256 cart
reads in byte 5 and 256 WRAM iterations in byte 6 — **comparing those
two bytes to each other on the FPGA** prices a cart data read against a
WRAM one on the same machine. If that comparison has already been
captured it is a lookup, not a rig run.

If cart data reads do carry the wait, the lever is not more WRAMCODE —
the region is full at 3536 of 3584 bytes, and §6's leftovers are capped
below BURSTGATE's cut, which converted nothing. It would be relocating
DATA, for which there is no room, which would make the residue a
property of the core rather than of the port.

## 8. The 60 unexplained lines: the ROM port is held across an SH-2 cache-line burst, and every gauge so far ran in vblank

The builder's accounting of wc2's 80-90 lines a pass over ares: general
5% ~12, residual fetch ~8, data reads 2-6, **~60 unexplained by any
per-access gauge.** That is not a "where" question yet. There is a
mechanism in the RTL that a per-access gauge cannot measure by
construction.

### The mechanism

`srcref/S32X_MiSTer/rtl/32X/IF.sv`, the ROM-port state machine:

    RS_IDLE:    SH-2 request wins over the 68K's whenever the 68K is not
                already asserting AS for a 32X ROM select (:822-833).
    RS_SH_CONT: if (SH_ROM_SEL && !SHBS_N) -> back to RS_SH_WAIT,
                otherwise release and return to RS_IDLE (:864-877).

**RS_SH_CONT loops.** Once the SH-2 holds the ROM port it keeps it for as
long as it keeps asserting, so consecutive SH-2 reads are one unbroken
tenure and the 68K's pending request waits for the whole of it.

The SH-2's cart reads are not single words. `rtl/SH/SH7604/CACHE.sv:488`:

    IBADDR <= {IBADDR[31:4], IBADDR[3:2] + 2'd1, 2'b00};

The fill walks `IBADDR[3:2]` through four values with `IBBURST` asserted
(`:572` sets it, `:513`/`:552` clear it) — **4 longwords, one 16-byte
cache line, which is 8 word transactions on the 16-bit cart port.** So a
single SH-2 instruction- or data-cache miss on cart art occupies the ROM
port for up to 8 back-to-back transactions, and a 68K cart access that
arrives mid-burst waits for the remainder, not for the one wait state
the per-access gauges measured.

### Why no gauge has seen it

`RIGSPAN=9` runs its cart-read and WRAM loops **"every 64th vint, past
the V jump (waits for V >= 0xF0)"** — inside vblank, when the master is
not streaming art. Both of the builder's numbers above (cart 77-80, WRAM
112-115) are therefore uncontended costs. The same applies to any
CARTBENCH run taken in that window, which would explain why turning the
SH-2s' art fetches off moved it only 132-148 -> 130-133: in that window
they were largely off already.

### The instrument already exists

`RIGSPAN=14` is documented in `HANDOFF-68K-FETCH.md` section 4 as **"mode
9 run inside the window (waits for raw V 0x40)"** — the same two loops,
taken while the master composes. **Its bytes have never been reported.**
Reading RIGSPAN=14 against RIGSPAN=9 on the same rom on the FPGA is a
lookup or one launch, not a new instrument.

    prediction   in-window cart-read byte rises sharply; the WRAM-loop
                 byte does not. Contention on the ROM port owns the 60.
    falsifier 1  both bytes rise about equally -> not the ROM port;
                 general bus or refresh contention, and WRAM code is no
                 protection either.
    falsifier 2  neither byte moves in-window -> the 60 lines are not
                 per-access at all and the class histogram is right.

A second falsifier is free from data already held: if contention is the
cause, the 68K's excess must be **concentrated in the ~106 lines where
the master composes**, not spread evenly across the ~330-line pass. A
per-phase split of the excess separates the two without any new gauge.

### If it is contention

The lever is not code or data placement — it is **when** the master
fetches art relative to the 68K's pass, and the burst granularity. Both
are ours to change; neither is reachable by more WRAMCODE. It would also
mean the MiSTer core's 68K penalty is larger than the +18% fixed / 0-15%
arbitration split in STATE's THE TIERS, and that the arbitration half is
the dominant term rather than the fixed one. **Real hardware has the
arbitration but not the wait state**, so this would change what the rig
can and cannot rank, not just how fast it is.

## 9. The master's cart streamer is cache_fill, it runs in the early-vint slot, and that is the one phase CARTBENCH samples

Correction first: §8 said RIGSPAN=14's bytes had never been reported. They
had (LESSONS addendum 05:10 ET, rigc14). The builder's reading of them is
right and so is the objection — mode 14 times data reads, mode 15 times
fetch, and only mode 15 has ever run in the contended phase.

### What the master's cart traffic actually is

`sh_src/m_main.c:7025 cache_fill`:

    const uint32_t *src = (const uint32_t *)(altbeast_tiles + code * 64);
    uint32_t *dst = (uint32_t *)(CACHE_C + (s4 + way) * 64);
    for (int k = 0; k < 16; k += 4) { dst[k+0]=src[k+0]; ... dst[k+3]=src[k+3]; }

**64 bytes per tile as 16 consecutive longwords from cart**, budget up to
384 tiles (`m_main.c:13090`, `:13331`). That is ~24 KB of cart reads in a
single drain, which at a 16-byte line is **~1,536 IBBURST line fills
back to back**. `altbeast_tiles` (`m_main.c:52`) is a plain linked cart
symbol, so it is read through the **cached** window; each 16-byte line is
one uninterruptible 4-longword burst (`CACHE.sv:488`).

**When it runs:** behind `dt = frt() - t_vint; if (dt <= 8000)`
(`m_main.c:13085-13090`) — the early-vint idle slot, explicitly commented
"walker idle work, outside the window".

### Why that reconciles CARTBENCH with mode 14

CARTBENCH waits for raw V >= 0xF0 — the early-vint slot, **the one phase
where cache_fill runs**. Its 132-148 spread collapsing to 130-133 with
the master's cart idle work off is the tenure signature, as the builder
says. Mode 14 samples raw V 0x40 (+102 lines, active display), where the
master composes from the SDRAM tile cache rather than from cart. **Its
null is the expected result, not a refutation.** Contention looks
confined to the cache_fill slot.

### Consequence for the c17 phase set

c17_0x10 (+54), c17_0x40 (+102) and c17_0x80 (+166) are all in active
display. **None of them samples the cache_fill slot.** If all three read
130-133, the builder's stated criterion ("cart at 132-148 everywhere
means fetch contention is the same inside and outside the window") would
read as "no contention anywhere", when the reading that fits is
"contention is confined to a phase none of the three sampled".

**Add a fourth rom at raw V 0xF0** so the comparison is c17@0xF0 against
c17@0x40 — the same loop, the same wait mechanism, one phase apart —
rather than mode 15 against mode 14, which differ in both.

### The lever, if it confirms

`IF.sv` `RS_SH_CONT` releases the port whenever the SH-2 stops asserting.
In `cache_fill` each 16-byte line arrives as one uninterruptible
4-longword burst, so a tile is 4 tenures of 8 word transactions and the
68K's worst-case wait is 8.

Reading `altbeast_tiles` through the **uncached mirror**
(`0x22000000u | addr`) makes each longword its own 2-word transaction,
with the SDRAM store between them deasserting SH_ROM_SEL and returning
the machine to RS_IDLE: **16 short tenures per tile instead of 4 long
ones, the same total words, worst-case 68K wait 8 -> 2.** It trades SH-2
art throughput for 68K latency, it is a one-line change in `cache_fill`,
and the `0x20000000u |` idiom is already used at `m_main.c:884`, `:898`,
`:899`, `:1019`.

**Not established:** how much of the 60 lines this is worth. The
uncached read is also slower per word for the SH-2, so the drain budget
may need re-pricing. Measure c17@0xF0 first; the lever is only worth
building if that phase is where the cart byte moves.

## 10. The cut's colour rotation is NOT queued. It writes palette RAM directly. (2026-09-25)

The builder's stated next step was "confirm the game queues the rotation, then
find where our path drops it." **It does not queue, so the queue is the wrong
place to look.** The cycler is `0x30B2`, and it is 21 instructions:

    30B2  lea 0xFFFFF300,a5      the slot table, 8 BYTES PER ENTRY:
                                   [0]   slot number, bit 7 = entry active
                                   [1]   countdown timer
                                   [2-5] pointer to the ring script
                                   [6-7] current index into the ring
    30B6  moveb a5@(0),d0
    30BA  bpl  0x3106            bit 7 clear -> end of table, rts
    30BC  subqb #1,a5@(1)        tick this slot's timer
    30C0  bne  0x3100            not expired -> next entry
    30C2  lea 0x840000,a1        *** PALETTE RAM, DIRECTLY ***
    30C8  andiw #127,d0
    30CC  lslw #4,d0             slot * 16
    30CE  addaw d0,a1            a1 = palette base + slot*16
    30D0  moveal a5@(2),a0       the ring script
    30D4..30F0                   advance the index, wrap on the script's count
    30F2  movew a0@+,d0
    30F4  moveb d0,a5@(1)        reload the timer FROM THE SCRIPT
    30F8  movel a0@+,a1@+        *** 4 x movel = 16 BYTES = 8 COLOURS ***
    30FA  movel a0@+,a1@+
    30FC  movel a0@+,a1@+
    30FE  movel a0@+,a1@+
    3100  lea a5@(8),a5          next slot entry
    3104  bra 0x30B6
    3106  rts

**Four facts that bear directly on the defect:**

1. **No queue.** `RequestPaletteUpdate` (0x3B2E) has 116 callers and
   `QueuePaletteUpdate` (0x3BEC) has ZERO direct callers — it is only reached by
   falling through 0x3B2E. The cycler uses neither. Anything of ours that
   watches the queue cannot see the rotation.
2. **The unit is 8 colours / 16 bytes**, at `base + slot*16`. The queue path
   writes 28 bytes to `slot*32 + 2`. **These are two different granularities and
   two different address schemes into the same palette RAM.** The builder's
   observation that "set 19's EIGHT framebuffer pens" come out flat matches the
   cycler's unit exactly, not the queue's.
3. **It runs in the vblank handler.** One caller: `0x2C16 bsr.w 0x30B2`, inside
   the IRQ4 handler that also holds the scroll copy (0x2ACA), the sprite-table
   copy (0x2B16) and the palette queue drain (0x2B4C). So the rotation lands
   once per vint, in vblank, on every vint including overrun ones -- the same
   timing as section 1's ruling, by a different mechanism.
4. **Our patcher already handles the site.** `tools/patch_game.py:1021` says
   "base is 0xFF9000 but the write lands at +((D0 & 0x7F) << 4)" and displaces
   `lea 0x840000,A1` to `lea 0xFF9000,A1` (`:1033`, `:1134`). That is the
   cycler's addressing exactly. **So the game's write is not being dropped; it
   lands in WRAM 0xFF9000 + slot*16.**

**Therefore the gap is downstream of 0xFF9000, not in delivery.** The question
to ask is whether the FB pen bake for the cut's slots re-reads 0xFF9000 after
the cycler has written it, or works from a copy taken before. That is a
different investigation from the one the builder had queued, and it is cheaper.

**On the blues.** The arcade's live field blues 25/23/21/19 (5-bit) exist as a
contiguous four-colour ramp in 20 of the 256 palette sets at `0x242A0 + id*28`.
Most are neutral (r=g=b) or cyan (g=b); only three are blue-DOMINANT:

    set 74  c5-c8  B25(r0,g19) B23(r0,g16) B21(r0,g13) B19(r0,g10)
    set 77  c4-c7  B25(r12,g18) B23(r8,g15) B21(r4,g12) B19(r0,g9)
    set 78  c4-c7  identical to 77 -- a base/+1 variant pair

77/78 being a near-identical consecutive pair fits `sub_90F4`, which adds 1 to
all four of its palette ids when `byte_FFF109 >= 8` (0x9162-0x9176).

**And an arithmetic floor the builder should know before chasing the painter:**
5-bit 25, 23, 21, 19 quantised to the Mega Drive's 3 bits per channel is
6, 5, 5, 4. **23 and 21 collapse onto the same value.** So a painter fed from a
3-bit MD line can show at most THREE of the four blues, whatever else is fixed.
It cannot be the whole story of "seven copies of one blue", but it is a ceiling
on that path.

**Withdrawn from my side:** section 9's earlier attempt to read the blues out of
`off_99A2`'s four ids (0x16/0x99/0x9B/0x9D for level 0) was the wrong numbering.
Those are palette SET ids; the builder's "sets 19/20/21" are CRAM SLOT indices,
and the allocator at 0xFFFFF500/0xFFFFF480 maps between them at runtime. Those
four sets' blues top out at 22 with bit 14 clear and are not the field.

## 11. The re-split's ceiling is 3 shades on the line and 4 with PALQUANT, which is already implemented (2026-09-25)

The builder's chain is complete and their fix is correctly located. One
arithmetic addition to the go/no-go, computed over their own measured ramp
(group 19 rotates 5-bit blue 19, 21, 23, 25, 27, 29, 31):

    5-bit   live (+2)>>2 clamped    PAL_QUANT_RAMP table
       19          5                      4
       21          5                      5
       23          6                      6
       25          6                      6
       27          7                      6
       29          7                      7
       31          7                      7
    distinct    3                         4

**`PALQUANT=1` yields FOUR distinct levels where the line's quantiser yields
three**, and four is exactly what the arcade oracle observed in the field
(25/23/21/19). On those four inputs alone the line's quantiser gives 2 distinct
levels and the ramp table gives 3.

`PAL_QUANT_RAMP` is implemented (`m_main.c:2932`, the 32-entry `mdp_q5` table)
and is NOT in `LINE_FLAGS`. **But it differs from the live quantiser on 16 of 32
inputs**, so it is a GLOBAL colour change across every set in the game, not a
local fix. It needs Mike's eyes on its own build. **It is a separate decision
and must not ride along with the re-split.**

**The framing that matters more than the shade count.** Right now the field is
STATIC -- the builder measured the quantised table reading one value on all
eight pens and never changing, because the seven pens share one line pen and
shared-pen drift is tolerated, so nothing ever updates it. The arcade's effect
is a moving shimmer. **Re-splitting the pens restores the MOTION**, and motion
returns at three levels as readily as at four. The visible defect is
static-versus-moving, not three shades versus seven.

So: the re-split is worth building on its own, and the fourth shade is a
later, separately-gated question.

## 12. glow_chev is DERIVED; the rom has an exact cut marker, and four other writers of the same words (2026-09-25)

`CHEV_FIX`'s guard is `glow_chev && s >= 19u && s <= 21u` (`m_main.c:3036`,
`:3205`), so the whole flag rests on `glow_chev`, which is

    m_main.c:16420   glow_chev = (uint8_t)((gp_a | gp_c) != 0);
    m_main.c:2086    /* the chevron plane is up (pages >= 10) */

**That is a DERIVED test over page numbers. The rom has an exact marker.**

    0x3A00  tst.b  (byte_FFF148)          the branch gate
    0x3A04  beq.s  0x3A2A                 default path
    0x3A0C-0x3A18  move.w d0(=0) -> FFF0E2, FFF0E4, FFF0E8, FFF0EA
    0x3A1C  move.w #$AAAA, (FFF0F4)       <-- pages 10,10,10,10
    0x3A22  move.w #$BBBB, (FFF0F6)       <-- pages 11,11,11,11
    0x3A28  rts

`0xAAAA` / `0xBBBB` are written **nowhere else in the image**. The vblank
handler reads these words at `0x2B06` and `0x2B10` and copies them to the page
selects.

**But the same two words have FOUR OTHER WRITERS, all variable:**

    0x23BA  move.w d1, (FFF0F4)
    0x246E  move.w d1, (FFF0F6)
    0x3A56  move.w d1, (FFF0F4)
    0x3A5E  move.w d1, (FFF0F6)

So "pages >= 10" and "the cut is up" are not the same condition. A variable
writer can put a high page there without the cut, and — the case that fits this
bug — **a variable writer can overwrite the cut's page value DURING the cut and
drop `glow_chev` mid-scene**, which would release CHEV_FIX's guard exactly when
it is needed and leave sets 19-21 free to be evicted. `0x3A56`/`0x3A5E` are
inside the same routine, 46 bytes past the cut branch.

### A third read, as cheap as the builder's two

Dump `0xFFF0F4` and `0xFFF0F6` across the cut alongside `glow_chev`:

    both == AAAA/BBBB and glow_chev == 0   the derived test is broken; gate it
                                           on the literal marker instead
    both == AAAA/BBBB and glow_chev == 1   the gate holds; the failure is the
                                           free-pen half, i.e. assignment
    values change mid-cut                  a variable writer is overwriting the
                                           marker; that is the "what changed"
    never AAAA/BBBB                        the cut is not taking sub_3A00's
                                           branch at all and every assumption
                                           above this line needs re-checking

**Recommendation: gate on the literal.** `FFF0F4 == 0xAAAA && FFF0F6 == 0xBBBB`
is unambiguous, has a single writer pair, and cannot be produced by any other
screen. `(gp_a | gp_c) != 0` can.

### The unresolved contradiction is now load-bearing

`byte_FFF148` gates the branch. **§2 of this file records that FFF148 is an
OBJECT marker whose value is slot+1 (dispatcher `0x398E`), NOT a scene flag,
despite `m_main.c:6611` calling it "the game's own cutscene byte".** That
contradiction has sat unresolved since it was written. It means `sub_3A00` takes
the cut branch whenever a particular object occupies a slot — which may be a
superset or a subset of "the transformation screen is up". **If the third read
comes back "values change mid-cut", this is the reason, and resolving FFF148 is
the fix rather than anything in the allocator.**

### On the builder's two dead attempts

Both correctly recorded. One addition to the span-based one: their diagnosis
("the claim immediately re-merges it onto the same equal-colour pens") is the
same structure as the arcade's own allocator, which resolves a set id to a slot
and only reassigns on eviction (`0x3B2E`, tables at `0xFFFFF500` set->slot,
`0xFFFFF480` slot->set, `0xFFFFF440` refcount). **The arcade never re-splits
either — it does not need to, because it has no pen-sharing layer.** Pen sharing
is ours. So there is no arcade behaviour to copy here and no fidelity argument
for any particular re-split rule; it is purely our allocator's problem, and
CHEV_FIX's exclusive-pen approach is the shape that matches what the hardware
does.

## 13. The three cyclers, from rom — and slots 20/21 are colour-identical, so it fits in ONE line (2026-09-25)

The builder's capacity finding is confirmed independently from rom, and rom
supplies a narrower fix than spreading across three MD lines.

The installer is `0x1A6BA`. It zeroes 32 longwords at `0xFFF300` (so the live
table is **0xFFF300-0xFFF380, 16 entries of 8 bytes**, not 32), then walks a
script at `0x1A6FA`:

    count word = 3
      cycler 0   slot 19 (0x13)   ring script 0x01A70E
      cycler 1   slot 20 (0x14)   ring script 0x01A78E
      cycler 2   slot 21 (0x15)   ring script 0x01A78E   <-- SAME POINTER

**Exactly three cyclers, slots 19/20/21 — the builder's numbers exactly. And
slots 20 and 21 run the SAME ring script.**

Ring format, from the cycler's own arithmetic (`0x30E4-0x30F0`: index x 18):
`[0..1]` step count, then N steps of 18 bytes = 2-byte timer + 16 bytes = 8
colours.

    slot 19     7 steps, timer 1 every step (so it advances EVERY VINT)
                blues rotate: [31,19,21,23,25,27,29,31]
                           -> [31,29,31,19,21,23,25,27]
                           -> [31,25,27,29,31,19,21,23]
                pen 0 fixed at 31, pens 1-7 a 7-position rotation.
                THAT IS THE SHIMMER.
    slots 20/21 6 steps, timers 4/2/2..., blues [20,0,0,0,0,0,0,0]
                (the flame ramp lives in red/green, not blue)

**Slots 20 and 21 are colour-identical at all times, by construction.** Same
script pointer; the installer zeroes the table so both start at ring index 0
(`a5@(6)`); both take their initial timer from the same `script[2]`
(`0x1A6E0`). The cycler ticks each slot's own timer but they were initialised
identically, so they never come out of phase for the life of the screen.

### The fix this implies

    naive                3 sets x 8 pens = 24 on a 16-pen line   cannot fit
    20/21 sharing pens   slot 19's 8 + one shared group for 20/21
                         (the builder measured slot 20 using 6 distinct)
                         = 14 of 16                              FITS, 2 spare

**So this does not need a cross-line change.** Two sets that hold identical
colours forever can share one pen group with no loss, which is exactly the
merge the allocator already does — it is only wrong when the sets' colours
DIFFER. Here they provably never do.

**That reframes CARD CHEVPEN's branch (a).** "Prefer an exclusive pen" is right
for slot 19 and wrong for slots 20/21: they should be deliberately merged with
each other and exclusive against 19. The rom says which pairs are safe to merge,
and it says so statically — the script pointers are equal in the image.

**Generalisable, and worth a toolkit note:** any two cycler entries in the
`0x1A6FA` script sharing a ring pointer are safe to merge for the life of that
screen. That test is one comparison at bake time, per title.
