# HANDOFF — the advisor thread (rewritten 2026-10-10 evening)

This thread does not build. It reads the builder thread's report, checks every number against
the previous one, and writes the next block for Mike to paste. Mike is the acceptance gate and
the rig operator; nothing else needs him at the keyboard. From 2026-10-10 the advisor also
runs its own reads (ares-headless, the rig over ssh) when a question needs no builder.

## The project, 2026-10-10 evening

| axis | state | next |
|---|---|---|
| logic, attract | 20 of 20 records at 20 ticks, stable under a real pad; gate reads 1 of 20 on 73d1446f -- H 02ba#13 is the ONLY NOT REACHED class left; B 0228 and B 0246 are both reached at their own ticks with records same/0 bytes (they went NOT REACHED on the tilemap-fix rom at clock x1.018 and came back at x1.031, so that class tracks the clock, not a lost record; tests/demo-b/README) | nothing |
| logic, gameplay | no tape | record the MAME input tape, gate the port against it |
| picture, attract | **THE LINE IS marks2** (sha1 f868b509, commit 0c8955d4). PASSED Mike's play pass 2026-10-12: *"I can tell the speed of the gameplay is right even if we are getting maybe 7 frames or so a second."* No tile glitches reported -- the marked-page sum held. Gates: check-demo 2 of 20 (both named NOT REACHED, no differing record), flips per 100 vints 46 / 48.5, longest hold 3 / 3, tile-RAM parity 20,480 of 20,480 at 700 and 2000, dirty-page census 0/0/0. | **THE GAP IN MIKE'S OWN UNITS: logic 60/sec, presented 8/sec in demo A and 24/sec in demo B.** Demo A is the one to chase. |
| picture, gameplay | untouched; the allocator still dynamic there | static tables per round, same tools, after the attract passes Mike |
| speed, hardware | **THE HASH IS GONE FROM THE LINE (2026-10-12).** The renderer took its dirty-page set by hashing 53 KB of SDRAM every game frame -- 25.7% of the master body, the largest named phase that is not the game tick -- and now takes it from `port_tile_dirty`, set at every tile-RAM store site. `PORTTILEHASH=1` restores the hash as the ORACLE and the census still reads 0/0/0 beside it. CANDIDATE `rom/port/marks1.32x` (sha1 902e66fc, commit 666e4c4f), **matched on the GAME's own (step, tick), never on emu frame** -- marks1 is 22 ticks ahead of clean1 at frame 700, so a frame-matched window measures different content. demo A at marks1 f680 tick 00D8 vs clean1 f700 tick 00D6; demo B at f1756 vs f1800, both tick 008E. <br> clean1 -> marks1: check-demo 4 of 20 -> **2 of 20** (no differing record either way) / clock x1.018 -> x1.008 / **flips per 100 vints 12 -> 45 and 32 -> 48.5** / **longest hold 8 -> 3 and 6 -> 3** / updates per 60 11 -> 29 and 29 -> 31 / MOTION 9 -> 8 and 12 -> 25 / parity 20,480 of 20,480 at 700 and 2000 both / census 0/0/0 both. <br> MOTION IS FLAT IN DEMO A BECAUSE THE METRIC FAILS, NOT THE ROM: its 5% threshold reads a tripled flip rate as flat, which is why `presented_fps` now leads with updates/60. <br> **check-demo's 2 of 20 IS NOT A FIX.** The clock move lands the walk on the other side of the object update, the same luck penfix1 had at x1.023. The A 0140 card STAYS OPEN. | Mike's pass on marks1. Then the remaining generation cost -- PORT_HIST on marks1 at the matched windows, what the 5-8 bodies per generation are now spent on -- then the `dat_syms.h` width card. |

**METRIC RULE, 2026-10-12, calibrated against Mike:** RANK on flips per 100 and longest hold; REPORT MOTION as the number Mike will see. His "maybe 7 frames or so a second" on marks2 matches MOTION 8/60 in demo A almost exactly, while updates/60 reads 29 there -- those are real sub-0.25%-threshold changes that do NOT read as frame rate to a person. MOTION fails as a RANKING between roms (it read marks1's tripled flip rate as flat) and succeeds as an ABSOLUTE number a human recognises. I argued to demote it; Mike's number corrected me.


**EVERY PLAIN-ROM NUMBER SINCE d696b228 WAS TAKEN ON A STALE GENERATED UNIT (2026-10-11).**
`cport_prep` refuses a hand file whose signature disagrees with `protos.h`, but it only checks
when it actually runs -- and `port.o` did not depend on `tools/cport_prep.py` or `hand/*.c`, so
the unit was regenerated only when something else moved. Eleven signatures sat broken from
d696b228 until today and no build ever failed. Found by deleting `build/port` to test whether a
reference rom was reproducible: it was not, and neither was HEAD.

RE-MEASURED ON A CLEAN BUILD, and only these: `check-demo` 4 of 20 at x1.018 (three named NOT
REACHED plus the A 0140 sampling delta), tile-RAM parity 20,480 of 20,480 at 700 and 2000, and
the dirty-page census 0/0/0 at emu 480/1200/2400. Clean builds reproduce to 4 stamp bytes.
EVERYTHING ELSE plain-rom in that range is MARKED STALE BY DATE, not re-derived -- the rig line
and the attract anchors are deliberately not re-measured. `rom/port/penfix1.32x` was gated
directly and PASSES (3 of 20, no differing record), so Mike's pass stands; `clean1.32x`
replaces it as the current pass rom because it is the first reproducible one.

**OPEN CARD -- A 0140.** Every live slot reads ONE GAME FRAME AHEAD at the matched tick. Byte
0x22 is +1 on all fourteen (0,1,2,3,4,5,8,17,18,19,20,40,62,63), slots 1-5 carry +1 in byte
0x20, and the only position fields that move do so by one frame of motion: slot 8 x
`11cf`->`11cd`, slot 62 x `1254`->`1253` and y `10b4`->`10b8`, slot 63 y `10b4`->`10b8`.
SAMPLING POINT, NOT STATE. It is not a named NOT REACHED class and must not be made one until
somebody explains why the matched tick lands on the far side of the object update. penfix1 at
x1.023 lands at 0141 and reads clean, which is luck, not correctness.

| hour death | open, untouched since 10-08 | counter census + seeded probe |
| sound | not wired | after the picture passes |

## Instruments (all in the repo)

- The logo ALTERNATES red and white through the attract and we are inverted at both sampled
  frames: at 700 the arcade is RED and ours white, at 2000 the arcade is WHITE and ours red
  (checked against screenshots/tiles-junk/arcade_*_43.png, 2026-10-10). A phase error, not a
  colour error. The earlier one-line note had the 2000 case backwards.
- `tools/nt_dump_ares.py` now takes every SDRAM offset from `tools/sdram_map.py` by SYMBOL. It had `snap` hardcoded at 0x052E8 against the port's 0x39940, which is why
  the 2026-10-10 tag-table numbers were of another block. A wrong base reads a neighbour as a layer_regs; it never faults.
- `make check-demo` (PICTURE=all before any m_main.c commit). Two NOT REACHED classes are named
  and are not failures; a differing record is.
- `tools/anchor_nine.py ctl cand` / `tools/anchor_align.py`: the picture metric at the game's own
  frame. EYE PAN IS BROKEN for every rom (the pan is not keyed to the game-frame counter). A mean
  luma of 65 is junk the same as 83: the anchors are RETIRED as a verdict for the demos; a 4:3
  frame beside the arcade is the verdict.
- `tools/attract_parity.py`: ONLY at each rom's own bisected offset; four-line roms cut 22-27
  frames later than the three-line control, and rows pinned at the control's offset misread face
  by +13.
- nt_audit's replay (measurement 3.1): per on-screen cell, tilemap triple vs the NT slot's tag.
  Classes MATCH / STALE / FOLDED / BLANK / OTHER. This is what found the wipe.
- `tools/presented_fps.py ROM --windows name:frame,... [--no-input]`: MOTION fps, any-change
  fps, and LONGEST HOLD (consecutive frames showing the same picture) per scene. Hold and rate
  together are the gate shape for any speed card, both roms. It is NOT md5-of-the-frame: bit
  identity counts one changed pixel as a new picture, which is anim_rate.py's rule that scored
  a CONFETTI build at 3x the line (LOOP28 97). Scene windows come from check_demo.py's SCENES.
- `tools/frame_timeline.py`, PORT_HIST (per-phase 68K/SH-2 lines), `tools/sdram_map.py --quiet`
  (in check-demo, printing, seven pre-existing overlaps untriaged), `tools/rig_bars.py` (a strip
  row one colour across 320 px is NO BAR), `tools/build_id.py show` (check the commit before
  trusting any rom).
- Roms for Mike live in rom/port/ with a README line (sha1, commit, flags, gate). Never the
  scratchpad. rom/port/static1.32x = 44d1673f = the landed tree.

## Rules, each with the failure that paid for it (this fortnight, newest first)

- **Every probe counter address comes from THAT BUILD's own `rom/s16.lst`.** The `pp_*`
  block is `.bss`, so adding one field relayouts all of it: `pp_mark_frames` moved
  0x1D68 -> 0x1DA8 when `pp_never_first` was added. Reading it at the old address gave
  132, then 65694, then 4094 -- NON-MONOTONIC, which is the only reason it was caught;
  a plausible-looking wrong number would have been believed. Same rule `nt_dump_ares`
  now follows for `snap` after it read 0x052E8 for a build that put it at 0x39940.
  Corollary: a counter read across two builds is not a trend, it is two addresses.

1. A FRAME BESIDE THE ARCADE BEFORE ANY NUMBER. Six days went to a colour card while every demo
   screen was junk; the anchors said 80-100 all week and the advisor read 83 -> 65 as progress.
2. One read per turn, a build only when a count names the line. 2026-10-10 afternoon: five
   reads, zero builds, cause named. The week before: twelve builds, three false regressions.
3. The builder reports ONCE per verified result: instrument proven in the same turn (symbol
   present, value moved from a baseline, address declared in sdram_map and --quiet clean), every
   ratio with its denominator. Failures that forced it: a pad that never compiled (twice),
   counters aliased onto SPRPEN, 9-bit CRAM dumps read as 12-bit, 2 KB blocks costed as 4 KB,
   "81 assign calls a frame" read off the rom that manufactured them, "1,151 demand" page-wide
   vs 340 on-screen, "set bases" from a six-slot sample.
4. An advisor spec that touches code cites the call site it read. Three advisor specs were wrong
   for not doing so: the step-4 hard refuse (mdp_assign_set is also the slot-claim path; x1.860),
   "the key drops six bits" (it does not), "the tags are set bases" (19 of 434).
5. Nothing lands before Mike's pass. c71d5752 was merged before his pass was read; the pass then
   failed the LINE (junk on both roms), so the landing stood as an improvement, by luck not rule.
6. A standing difference gets a pad control; a pad is proven by the listing (symbol, size, the
   next symbol shifted), not by the flag stamp.
7. `git revert` of a commit that removed a Makefile flag puts the flag BACK (two night builds
   carried the cap by accident). A branch checked out in any worktree cannot be checked out
   elsewhere (a "control" built the wrong tree). Verify build_id before reading a rom.
8. Rig reading = 3 launches under distinct names, log-proven, shots at 82/152/222 s, differenced.
   A line is never one reading.
9. Overnight = one long turn driven by background tasks and their notifications; the 2026-10-09
   night ran 23:39-00:45 and stopped when its list ran out, which was the advisor ending the turn.
10. The advisor does not touch the shared working tree (branches, checkouts, builds); specs are
    text. The builder keeps the context.

## The advisor's own errors, 2026-10-10, for the next advisor to not repeat

- Three frozen-tick / repeated-countdown ALIGNMENT ARTIFACTS filed as port defects,
  2026-10-11 morning: the eleven cells 0xFFF0E2-0xFFF0F7, the text registers, and the
  logo step's tile RAM. All three read right once the arcade is sampled at the same
  countdown and offset (arcade 284-286 against port 300: 26624/26624). A tick that is
  frozen or that repeats does not name a unique frame, so comparing at the same TICK
  compares different moments. Check the countdown before filing a cell as wrong.

- Re-sent completed blocks twice (the housekeeping block, the five-item block).
- Specified step 4 without reading mdp_assign_set's callers.
- Shelved the static-half build in the morning on a cost page whose block size was 2x wrong,
  then reversed on the builder's eye finding.
- Accepted "the price is negative" from one point of a curve.
- Accepted the builder's "set bases" and wrote a spec on it.
- Managed by anchor numbers for six days without looking at a frame.

**CLOSED 2026-10-11: the cells 0xFFF0E2-0xFFF0F7 are NOT a defect.** penfix1 against the
arcade dumps at four scenes (E 0034 / A 00ee / H 02ba / B 01fe). The range is ELEVEN word
cells, not twelve.

| cell | E 0034 | A 00ee | H 02ba | B 01fe | |
|---|---|---|---|---|---|
| F0E2 | 00C0 | 0187 | 0140 | 00D6 | exact |
| F0E4 | 00C0 | 0187 | 0158 | 00D6 | exact |
| F0E6 | 0000 | 0000 | 0000 | 0000 | exact |
| F0E8 | 0020 | 0020 | 0020 | 0020 | exact |
| F0EA | 0020 | 0020 | 0019 | 0020 | exact |
| F0EC-F3 | | | | | DIFFER, and correctly: EXCLUDED at `sweep_demo.py:24` |
| F0F4 | 1212 | 0101 | 0101 | 0101 | exact |
| F0F6 | 6767 | 5656 | 5656 | 5656 | exact |

Seven cells exact at every scene. The four that differ are the published register pointers,
and the difference is exactly the pointer domain, identical at all four scenes: the port holds
0x06026E80/82 where the arcade holds 0x00410E80/82, which is A68VIDA's video map
(0x410000-0x411000 -> 0x06026000 + offset, `w68.h`) applied correctly. The sweep already
excludes them by name. There is nothing to fix in this range; a card aimed at it is aimed at
the one place the port is demonstrably right.

**THE BLACK-TOP FRAMES: the display gate does NOT explain them, and the ARCADE DOES THE SAME
THING (2026-10-11).** Two measurements.

DISP_CENSUS (0x28F7C) on penfix1 at ports 1090/1100/1150/1200/1320/1325/1330/1645/1650/1655:
blanks entered go 2 -> 3 at ~1200 and 3 -> 4 at ~1645, held-vints 37 -> 44 at ~1320. The top
eight rows read black at 1090, 1100 and 1150 -- BEFORE any of those blanks -- and read NOT
black at 1330 and 1645. The gate's events and the black tops do not correlate.

`mame altbeast`, demo A: top eight rows nonzero at frames 600, 700, 800, 900, 1000 and 1050
(2560 lit pixels each) and **fully black at 1100 and 1150**. The arcade blanks its own top
eight rows in the demo-A tail. Ours doing it there is faithful.

No fix, as the card said, and none is wanted: this is the third thing in one day that measured
as a defect and turned out to be the arcade's own behaviour (the bottom band was a crop, the
33-frame hold is 32 on the arcade, and this).

**THE PAGE-2 WRITER IS FUN_00002572, 2026-10-11 (elimination pass, reads only).** Every
rectangle-list entry point decoded from the 68K and from its own data in `prog68k.bin`:

| fn | 68K | data | blits | destinations (page, row, w x h) | reaches p2 r8? | marks? |
|---|---|---|---|---|---|---|
| `FUN_00002552` | 0x2552 | 0x278B8 | 8 (`movew #7,d3` + `bsrs`/`dbf`) | p0 r9 x3, p1 r9 x2, p6 r11, p5 r11, p5 r12 | no | yes, via 258a |
| `FUN_00002564` | 0x2564 | 0x28B84 | 2 (`bsrw` + `braw` tail) | 0x7400 p7 r8 20x20, 0x7428 p7 r8 20x20 | no | yes, via 258a |
| **`FUN_00002572`** | **0x2572** | **0x26C20** | **4 (3x `bsrs` + `bras` tail)** | **0x2400 p2 r8 20x20, 0x2428 p2 r8 20x20**, 0x7400 p7 r8, 0x7428 p7 r8 | **YES** | via 258a |
| `FUN_00002580` | 0x2580 | 0x2726C | 2 (`bsrw` + fall-through) | 0x7400 p7 r8 20x20, 0x7428 p7 r8 20x20 | no | yes, hand file since c4d02904 |

`FUN_00002572` is the ONLY entry point whose destinations reach page 2, and its first two
records are 20 wide x 20 tall at 0x2400 and 0x2428 -- SIDE BY SIDE, so together 40 columns x
20 rows = **800 cells, which is exactly the 800-word change measured at emu 458** (rows 8-27,
all 40 columns). Geometry and count both match; this is not a shortlist, it is the writer.

Call sites: `altbeast_port.c:3729` (the function), `:3735-3738` (its four 258a calls).
Decoded from `prog68k.bin` 0x26C20, not from the decompiler.

**AND ON PAPER IT MARKS.** All four of its blits go through `FUN_0000258a`, which is a hand
file carrying the mark, and the mark for 0x2400/20x20 computes page 2 correctly (`uOff >> 12`
= 2, `uLast` 0x2DCF >> 12 = 2). So the open question is no longer WHO writes page 2 -- it is
why that writer's mark does not reach the census. That is a different question from the one
this card started with, and it is cheap to answer: count the pages `FUN_0000258a` marks, per
call, and compare against the census.

CORRECTION TO MY OWN EARLIER READING: I called the emu-458 change "a 40x20 rectangle", which
read as one blit. It is TWO 20x20 blits side by side. The shape was right, the count was not.

**THE FLIP REFUSAL CENSUS, ON THE LINE (2026-10-12).** `VBSPAN=1` (implies FLIPCENSUS),
rom vbspan1 of 277b6636. Counters already existed; no new instrument was invented.

| | reason | file:line | demo A 700-800 | demo B 1700-1900 |
|---|---|---|---|---|
| CEN[21] | declined: past the vblank edge | m_main.c:10210 | 24 | 93 |
| CEN[22] | declined: nothing drawn | m_main.c:10243 | **0** | **0** |
| CEN[23] | **declined: nothing shipped** | m_main.c:10261 | **72** | **96** |
| CEN[20] | a flip DID happen | m_main.c:14982 | 12 | 64 |

**THE DOMINANT REASON IS "NOTHING SHIPPED": 72 of 96 declines in demo A (75%), 96 of 189 in
demo B.** The vblank edge is 25% and 49%.

**THIS OVERTURNS THE RECORD.** `Makefile:1184` (LOOP28 94) says the pre-flip path measured
4717 ticks against a 1650-tick edge guard, "which is why almost every flip declines". On the
line the edge is a MINORITY reason in demo A. That sentence is stale; it was true of the
double-buffered line and is not true of clean1.

**AND IT IS NOT THE "NOTHING CHANGED" BRANCH EITHER.** `nat_shipped` does not mean "the
picture moved" -- `m_main.c:1675` defines it as *"hidden bank holds a fresh WHOLE FRAME"*, set
only where a generation completes (`:15279`, `:16964`). `!nat_shipped` means THE COMPOSE DID
NOT FINISH A GENERATION, not that nothing was worth showing. CEN[22] "nothing drawn" is 0 in
both windows, which is the counter that would have carried a genuinely static scene.

**SO THE BODY COST DOES GATE MOTION, AND PARKING THE HASH ON "94 BODIES PER 100 VINTS" WAS
REASONED FROM THE WRONG UNIT.** A master *body* is a CHUNK, not a generation. 94 bodies and
12-20 flips per 100 vints means a generation costs roughly **5 to 8 bodies**. Anything that
makes a body cheaper shortens the generation and lands more flips. The page hash at 25.7% of
the body is therefore a motion lever, not merely a logic one -- and its correctness
precondition (census 0/0/0) is already met.

**THE 68K DEPENDENCY WALK ON k1/k2: THE WALK WAS ON DEAD CODE (2026-10-12).** The first pass
marked three NEEDS CHANGE from md_main.c's source. The advisor's filter was right and the
walk was wrong: on the port build the 68K compiles the **R60 shim**, not the window/cadence
block, and five of the six sites are not in the unit at all.

**Makefile:341-346 says it in a comment nobody had read:** *"MD side: the R60 shim replaces
the whole window/cadence block; only FM_GATE ... is needed there."* `R60=1` adds
`-DR60 -DK2_FREE -DVISR_FLIP -DSNAP_ONE -DWIN_TWO -DCUT_30` to **SHCCFLAGS** and `-DR60`
alone to **MDCCFLAGS**. Confirmed against `build/port/.build_flags`, whose 68K half (36 flags,
up to `-DNO_GAMEHIGH`) carries no `WIN_TWO`, no `K2_FREE`, no `SNAP_ONE`, no `CUT_30`, no
`VISR_FLIP`. All five are SH-2-only.

Method, so this is checkable: `m68k-elf-gcc -E` with exactly that 68K flag half, line markers
walked to map each surviving output line back to its source line. Comment lines strip in cpp,
so the first pass's line numbers (`:4784`, `:490`, `:5561`, `:5650`) were untestable as given
and the code line under each comment is the one marked here.

| site (code line) | mark | why |
|---|---|---|
| `md_main.c:4799` `if (*mars_comm0)` | **LIVE, SAFE** | inside `shim_vblank` (`:4002`, `0x00FF1918` in `build/port/rom/md_start.elf`). No k distinction. |
| `md_main.c:1324` `md_consume` | **LIVE, SAFE** | of its nine call sites only `:4984`/`:4989` survive, back to back in the same vint. There is no A/B-to-k1/k2 pairing on this build to break: the earlier **UNKNOWN** resolves to SAFE. |
| `md_main.c:490` the FM_GATE pre-window call | **DEAD** | the pre-window consumes it describes are `:5382-3` and `:5512`, all three stripped. The earlier **UNKNOWN** resolves to DEAD. |
| `md_main.c:5563` `v = v_entry` | **DEAD** | `#ifdef K2_FREE` (`:5553`). So are both arms of the V-gate beneath it, `:5581` and `:5583`. |
| `md_main.c:5654-5700` the SACRIFICIAL k1 stores | **DEAD** | the whole block strips: `:5662 if (1)`, `:5664` the wcmd window select, `:5666`, `:5688` the FM raise, `:5696`/`:5698` the heartbeat. |
| `md_main.c:54`, `:2515` the assertions | **DEAD, and already R60-aware** | `:53` reads `#if (!defined(WIN_TWO) \|\| !defined(FB_TEXT_READ)) && !defined(R60)` -- the exemption is written into the guard, with `:55` explaining it. `:2514` needs `K2_FREE`, absent. `:2517` is the R60 equivalent and it is satisfied (`FM_GATE` is on the 68K half). |

**ZERO NEEDS CHANGE ARE COMPILED. The table is empty and item 1 is closed.**

**AND THE LIVE SHIM IS ALREADY ONE WINDOW PER VINT.** `shim_vblank`'s surviving body runs one
entry gate (`:4806-4822`, one `r60_go`), one arm announce (`:4844`), then both packets
consumed back to back (`:4984`, `:4989`) plus `mdspr_consume` and `mdspr_upload_pump`. No
window index, no k2 branch, no deferred flip.

**SO THE 30 Hz IS NOT THE 68K's. It is entirely SH-2-side** -- `WIN_TWO` and `CUT_30` are on
the SH-2 half of `.build_flags` and nowhere else. P2 on the port build is a **renderer card**,
not a two-sided one, subject only to item 2's share.

**THE LESSON, and it is the second time this exact shape has cost a block:** the port build's
68K unit is NOT md_main.c as written. Mark a 68K site compiled-or-not against
`build/port/.build_flags` BEFORE walking it. A dependency walk over `#ifdef`-dead code reads
as a real finding and is worth nothing.

**THE GENERATION STAMP RING: THE COMPOSE DOES NOT CLOSE IN ITS LAUNCH VINT (2026-10-12).**
`make port PORT_EXTRA="PHASECENSUS=1 GENRING=1"`, rom `build/port/rom/genring.32x`, stamp
c729dde6 content (see the commit for the final one). Reader `tools/gen_ring.py`. Per
generation it records the V-ISR vint number (DIAG[49]) at launch, slave echo landed, master
tail done, compose close, ship and flip, plus the launch's offset into its own vint, into a
64-entry ring at SDRAM 0x38000. The ring holds the last 64 generations before the named
frame, so each read names the vint span it actually covers.

**THE GATE NUMBER, which item 3 turns on: 0.0% in demo A steady, 34-46% across the cut.
The bar was 90%.**

| read | ring covers vints | close in launch vint | ship in launch vint | launch offset, lines of 262 |
|---|---|---|---|---|
| f455 (the cut) | 324..441 | **46.0%** (29 of 63) | 0 of 63 | min 10 median 12 max 14 |
| f500 (the cut) | 369..486 | **34.4%** (22 of 64) | 0 of 63 | min 10 median 12 max 17 |
| f680 (demo A) | 532..666 | **0.0%** (0 of 63) | 0 of 63 | min 11 median 13 max 17 |
| f780 (demo A) | 637..766 | **0.0%** (0 of 64) | 0 of 63 | min 12 median 12 max 17 |

**IT IS NOT LAUNCH PLACEMENT.** The launch sits at a median of 12 lines into its vint in
every window, max 17 of 262 -- 4.6% of the way in. A compose that took even 0.9 vints would
close inside its launch vint. None do in demo A.

**THE OFFSET DISTRIBUTIONS**, vints after launch, at f780 / f500:

| event | f780 | f500 |
|---|---|---|
| echo | +1 67%, +2 33% | +0 75%, +1 19%, +2 6% |
| master tail | +0 25%, +1 67%, +2 8% | +0 38%, +1 56%, rest 2-8 |
| close | +1 58%, +2 42% | +0 34%, +1 55%, rest 2-8 |
| ship | +2 95%, +3 5% | +1 38%, +2 54%, rest 3-10 |
| flip | +3 93%, +4 7% | +2 86%, rest 3-9 |

**THE LONG POLE IS THE ECHO AS THE MASTER SEES IT, NOT THE MASTER TAIL.** Windowed
PHASE_CENSUS means (later point minus earlier, divided by generations closed between):

| window | gens | echo | mtask | lag | ship | flip |
|---|---|---|---|---|---|---|
| 455->500 | 16 | **1.409v** | 1.289v | 0.036v | 0.671v | 0.668v |
| 500->680 | 85 | **1.672v** | 1.182v | 0.003v | 0.589v | 0.604v |
| 680->780 | 50 | **1.662v** | 1.014v | 0.004v | 0.520v | 0.602v |

Lag is ~0.00v, so the close really is immediately after `max(echo, mtask)` -- the poll is not
costing anything and the echo is simply later than the tail. **This reverses what the Makefile
records at :2252** (*"PHASECENSUS puts mtask at 2.73 of a 2.74-vint wall"*), which was the
pre-port line. On the port build the slave chain gates the compose.

**AND THE ECHO IS BIMODAL, so the cost is a LATE THIRD and not a slow chain.** PHH at f780,
cumulative since boot, 467 generations: **63.8% under 0.625 vints, 31.7% at or above 1.375
vints, 4.5% in all six bins between.** mtask is bimodal too but shallower (67% under 1.0v,
31% at or above 1.375v). CAVEAT, and it matters: that histogram is cumulative, so it mixes
the cheap early scenes with demo A, and the windowed means above say demo A sits in the slow
mode. Whether the bimodality survives inside demo A alone is NOT established by this read.

**TWO THINGS THE RING FOUND THAT WERE NOT ASKED FOR.** `PHPER` ship periods at f780 are 212
at one vint, 232 at two, 18 at three, 4 at four or more -- the one/two alternation IS the
presented cadence. And `PH[10]` counts 236 flips against 467 generations closed: **half the
generations never reach the screen at all.**

**DECISION, by item 3's own rule: the card is COMPOSE COST regardless of item 1.** The share
is 0% where it matters. The candidate build does not start.

**AND THE INSTRUMENT HAD A BUG WORTH RECORDING.** Keyed on "most recently launched
generation" alone, ship stamped the NEXT generation's slot and read offset 0 at 100% while
close read +1 to +2 -- an impossible pipeline, which is how it was caught. A generation's
events outlive the next launch, so the ring now carries two cursors: `gr_open` for
launch/echo/tail/close and `gr_post`, handed over at close, for ship and flip. The first
build also read empty: `GR_LAUNCH()` had been placed beside `trip_launch++`, which is
`TRIP_CENSUS`-only and off.

**THE RING, WINDOWED: THE ECHO IS NOT A LATE THIRD, IT IS SLOW EVERY TIME (2026-10-12).**
`tools/gen_ring_classify.py`, reads only, same rom. Two claims from the first ring read are
WRONG and the windowed data is what says so.

**1. THE BIMODALITY DOES NOT SURVIVE. It was never in demo A at all.** PHH subtracted between
reads, so each row is that window's own histogram instead of the cumulative one:

| window | n | echo FAST <0.625v | echo SLOW >=1.375v | middle |
|---|---|---|---|---|
| 455->500 | 16 | **0.0%** | 75.0% | 25.0% |
| 500->680 | 85 | **0.0%** | 96.5% | 3.5% |
| 680->780 | 50 | **0.0%** | **100.0%** | 0.0% |

**Zero fast echoes in any window from frame 455 on.** The cumulative histogram's 63.8% fast
mode is entirely pre-455 -- boot and title. I flagged that caveat when I reported it and it
has now resolved against my reading: *the late third was an artefact of mixing scenes.* The
echo in demo A is pinned in the top open-ended bin, and the windowed mean puts it at 1.662
vints.

mtask is the one with real spread: 680->780 reads 24% fast, 52% middle, 24% slow, mean 1.014
vints. So the two phases are not the same shape at all -- the master tail varies with the
scene, the slave chain does not.

**2. "HALF THE GENERATIONS NEVER FLIP" IS NOT DEMO A EITHER.** Classified per generation,
excluding the last 4 vints of each ring as too young to judge:

| read | (a) flipped | (b) shipped, never flipped | (c) closed, never shipped | (d) flipped late |
|---|---|---|---|---|
| f455 (cut) | 28 **45.9%** | 33 54.1% | 0 | 0 |
| f500 (cut) | 26 **41.9%** | 34 54.8% | 0 | 2 (3.2%) |
| f680 (demo A) | 57 **91.9%** | 4 6.5% | 0 | 1 (1.6%) |
| f780 (demo A) | 58 **93.5%** | 3 4.8% | 0 | 1 (1.6%) |

**In demo A 92-94% of generations flip.** The 236-against-467 is the cut and what precedes
it, where barely 42-46% do. Nothing is ever (c) closed-and-never-shipped. CAVEAT on (b): the
ring carries no bank parity, so (b) is "shipped and never flipped, with at least one later
ship" -- that its content was overwritten in the SAME hidden bank is inferred from ships
alternating, not measured. And the ship's offset inside its vint is not in the ring: field 6
carries the LAUNCH offset only, so splitting (b) by ship phase needs a build.

NATIVE.md:41-44 (*"sbuf can never ship mid-compose because a new generation cannot launch
while one awaits its blit"*) is not being violated -- `BLIT_CHASE` supersedes it on purpose
and says so at m_main.c:1668-1673: *"the launch may open a generation over a READY one
because the fence (SYNC[14]) orders the slave behind the blit"*.

**3. THE FENCE CROSS-TAB CANNOT ANSWER THE QUESTION, AND THE FENCE IS TOO SMALL ANYWAY.**

The fence sites: master sets `SYNC[14] = 112 + BLIT_SHIFT` (=136) at m_main.c:15232 and
publishes rows with `SYNC[14] = y` at m_main.c:8909, opening it with `SYNC[14] = 224` at
:13419 and :17067. The slave waits at **s_main.c:241-245**, `slave_fence(row)`:
`while (SYNC[14] < row && (uint16_t)(frt_s() - t0) < 24000)`. All of it is compiled --
`BLIT_CHASE`, `LAUNCH_EARLY`, `BLIT_SHIFT=24`, `BAND_SHIFT=36` are all on the SH-2 half.

**The cross-tab has no control group.** 61 of 62 settled generations at every read have
another generation's ship falling between their launch and their echo. At this cadence the
overlap is universal, so "overlapped" versus "did not" compares 61 against 1 and measures
nothing. The 51% slow among the overlapped at f680 is not evidence.

**And the fence is bounded at 0.500 vints exactly.** s_main.c:244's timeout is 24000 slave
ticks at phi/8 = 23.01/8 MHz = 2.876 MHz, so 8.34 ms against a 16.68 ms vint. Even if every
compose spent the fence's FULL timeout waiting, 0.500 of the echo's 1.662 vints is accounted
for and **0.875 vints at minimum is the slave's own work**. The fence cannot be the main term.

**4. THE LEVER IS THE SLAVE COMPOSE BODY.** Not fence serialisation (bounded at 0.500v by
construction, against a 1.662v echo), not ship-phase overwrite (5-7% in demo A; it is 54% at
the cut, which is a real and separate defect but not the one Mike sees at 8/sec).

**THE ONE THING THIS READ CANNOT SETTLE, and it is the next instrument.** `PHS[0]` is stamped
by the master's first POLL that sees `SYNC[1]`, so "echo at 1.662 vints" is the master's
OBSERVATION, not the slave's completion. The ring cannot tell a slave that takes 1.66 vints
from a slave that finishes at 0.9 and is not looked at until 1.66. Weak evidence against pure
quantization: PHH's own comment says quantization shows as *"a pile-up in the bins straddling
1.0v"*, and the pile-up is in the top bin instead. `st_s()` already stamps the slave side
under PHASE_CENSUS (m_main.c:1780, s_main.c); carrying the slave's own completion into the
ring is one build and it decides whether this card is a slave-body card or a master-poll card.

**THE CANDIDATE, written not built.** Bring the slave chain under ~0.95 vints (launch sits
0.05 vints in), a 1.75x cut, OR move the poll if the instrument above says the body is
already fast enough. Gate: updates per 60 at a matched tick against the arcade's 59, with
flips per 100 vints and longest hold as the ranking pair, and `check-demo` unchanged at 2 of
20. Do not gate on MOTION -- it read marks1's tripled flip rate as flat.

**IT IS A BODY CARD, AND THE BAND IS BAND 2 (2026-10-12).** `build/port/rom/genring2.32x`,
build_id **810974e9**, `PHASECENSUS=1 GENRING=1`. The slave now stamps its own chain into a
second ring at 0x38A00 (s_main.c), keyed to the master's slot by latching `GR[448]` at
pickup. `tools/gen_ring_slave.py` joins them.

| window | slave chain complete, ITS OWN clock | master first poll (PHS[0], whole vints) | poll term |
|---|---|---|---|
| f455 (cut) | **0.293v** (median 0.313, max 0.313) | +0 for 63 of 64 | ~0 |
| f500 (cut) | **0.482v** (median 0.313, max 1.160) | +0 for 48, +1 for 12, +2 for 4 | ~0 |
| f680 (demo A) | **1.337v** (median 1.370, min 0.916, max 1.916) | +1 for 29, +2 for 34 | **0.203v** |
| f780 (demo A) | **1.363v** (median 1.370, min 1.214, max 1.542) | +1 for 36, +2 for 27 | **0.065v** |

**THE POLL IS NOT THE TERM. THE SLAVE'S OWN CHAIN IS 1.34-1.36 VINTS IN DEMO A** against a
budget of ~0.95 (the launch sits 12 lines in, so the chain must finish inside 250 of 262
lines). The master's observation lag adds 0.07-0.20 vints on top. Read the poll term as an
upper bound only: `PHS[0]` is quantised to whole vints while the slave's completion is
fractional, which is why f455 computes a nonsensical -0.277 -- there the slave finishes at
0.29v and the poll correctly reads "+0 vints", and the two resolutions do not subtract.

**THE SPLIT, lines since pickup (262/vint):**

| | f455 | f500 | f680 | f780 |
|---|---|---|---|---|
| band 0 body | 6 (7.8%) | 18 (14.0%) | 60 (17.1%) | 53 (14.9%) |
| **fence 1 wait** | **0** | **0** | **0** | **0** |
| band 1 body | 7 (9.1%) | 25 (20.0%) | 108 (30.9%) | 110 (30.7%) |
| **fence 2 wait** | 16 (21.3%) | 13 (10.5%) | **0** | **0** |
| **band 2 body** | 47 (**61.8%**) | 70 (**55.5%**) | 182 (**52.0%**) | 194 (**54.5%**) |
| chain total | 77 | 126 | 350 | 357 |

**THE FENCE COSTS EXACTLY ZERO IN DEMO A.** Both waits measure 0 lines, mean and max, at f680
and f780. It is 13-16 lines (0.05-0.06v) at the cut, on fence 2 only. My 0.500-vint figure was
the timeout BOUND; the measured value is nothing where it matters. **FENCE TIMEOUTS EVER
(GR[449]): 0** -- the silent-tear rate is zero and now has an instrument that says so.

**BAND 2 IS THE CARD.** It is 52-62% of the chain in every window and it is the band that
GROWS: 47 lines at the cut to 182-194 in demo A, roughly 4x, while band 0 goes 6 to 53-60 and
band 1 goes 7 to 108-110. The whole demo A chain is 4.6x the cut's.

**THE ARITHMETIC OF THE CUT.** Chain 350-357 lines, budget 250. Needs **~100-107 lines off,
29-30%**. Band 2 alone carries 182-194, so band 2 at roughly half its present cost closes it;
nothing else is big enough on its own.

**THE NEXT READ, no build: band 2's DENOMINATOR.** What band 2 composes in demo A -- sprite
count, zoomed sprite rows, cat1 cells, text rows per generation, from the latched records --
so the cost has something per-unit under it before anything is cut. Not done here; it is a
separate read and this block's build is spent.

---

**CARD, SEPARATE: THE CUT SHIPS 54% OF ITS GENERATIONS TO NOTHING (2026-10-12).** At f455 and
f500, 54.1% and 54.8% of settled generations are SHIPPED AND NEVER FLIPPED (33 of 61, 34 of
62), against 4.8-6.5% in demo A. That is the whole of the 236-flips-against-467-generations
figure. The slave chain is FAST there (0.29-0.48 vints, closing in the launch vint 34-46% of
the time), so this is not a compose-cost problem and it does not share a lever with band 2.

CAVEAT, unchanged from how it was first written: the ring carries no bank parity, so what is
MEASURED is "shipped, never flipped, with at least one later ship". That the content was
overwritten in the SAME hidden bank is INFERRED from ships alternating, not measured. The
ship's offset inside its vint is also not in the ring -- field 6 is the LAUNCH offset only --
so splitting these by ship phase needs a build. Nothing is ever closed-and-never-shipped (0
at every read), so the loss is strictly between ship and flip.

**THE PASS SPLIT: SPRITES ARE 74% OF THE CHAIN, AND IT IS NOT THE UNPACK (2026-10-12).**
Reads only on genring2 (810974e9). `tools/band_denom.py`.

**1. THE PASS SPLIT IS PER CHAIN, NOT PER BAND.** `STB` is a running sum over all bands and
all chains (`s_main.c:97-98`), and `st_s(10..12)` live inside `slave_concurrent_k`
(`m_main.c:11797/:11804/:11823/:11835`), which runs once per band. Differencing two reads
gives the per-chain split and nothing finer; per-band would need a build. f680 to f780, 50
chains, 184 slave ticks = 1 line:

| pass | lines/chain | share |
|---|---|---|
| strip clear (STB[0]) | 38.1 | 10.8% |
| **strip sprites (STB[1])** | **260.6** | **73.7%** |
| cat1 (STB[2]) | 0.1 | 0.03% |
| text + service (STB[3]) | 54.6 | 15.4% |
| **total** | **353.4** | |

**That 353.4 is an independent confirmation of the ring: the slave ring measured 350 lines at
f680 and 357 at f780 for the same chains.** Two instruments, different stamps, same number.
**cat1 is 0.1 lines. It is not a term and should stop being listed as one.**

**2. THE DENOMINATORS**, from the latched `SPR_SNAP` (the compose's only sprite source,
m_main.c:9542), means over six frame points 680-780. Band rows from
`slave_concurrent_k` (m_main.c:11611-13) with this rom's `BAND_SHIFT=36`,
`BAND_SHIFT_RG2=40`: band 0 = 0-71, band 1 = 72-143, band 2 = 144-223 (80 rows, not 72).

| band | rows | sprites | sprite output rows | row-pixels | zoomed | zoomed rows | chain lines (f680/f780) |
|---|---|---|---|---|---|---|---|
| band 0 | 72 | **0.0** | **0.0** | **0** | 0.0 | 0.0 | 60 / 53 |
| band 1 | 72 | 11.3 | 203.8 | 7825 | 1.8 | 16.2 | 108 / 110 |
| band 2 | 80 | 11.5 | 301.3 | 11891 | **0.0** | **0.0** | 182 / 194 |

**BAND 0 HAS ZERO SPRITES IN DEMO A AT EVERY ONE OF THE SIX FRAMES, AND STILL COSTS 53-60
LINES.** That is the non-sprite floor per band, measured directly rather than modelled.

**ZOOM IS NOT THE ANSWER AND IT IS NOT CLOSE.** Band 2 had **zero** zoomed sprites at all six
frames while band 1 had some. The expensive band is the one with no zoom in it.

**3. THE LINE: IT IS COUNT, AND THE REMEDY NAMED FOR IT IS THE WRONG ONE.**

Count: band 2 carries 1.48x band 1's sprite output rows (301 vs 204) and 1.52x its row-pixels,
with 80 rows against 72. Unit cost: INFERRED, and the inference is stated -- taking band 0's
53 lines as the per-band non-sprite floor leaves ~37 lines of the chain's 92.8 non-sprite for
bands 1 and 2, so band 1's sprite share is ~92 lines and band 2's ~175. Per row-pixel that is
0.0118 against 0.0147, a **1.25x** premium, against a **1.52x** count. Count dominates; the
premium is real but secondary and cannot be zoom.

**BUT §3.1 OF SPRITE-PIPELINE-AFTER-PORT ("render straight from the object table, no
intermediate record, no packing, no unpacking") WILL NOT PAY THE 100 LINES.** The arithmetic
refuses it: 260.6 lines across 22.8 records is **11.4 lines per record**, which at 184 ticks
a line and phi/8 is ~16,800 SH-2 cycles to unpack one 8-word record. The cost is not per
record. Per row-pixel it is 2.43 slave ticks, ~19.5 SH-2 cycles, across 19,716 row-pixels a
generation -- **the cost is the per-pixel inner loop of the sprite plot**. The lever is that
loop's cost per pixel, or plotting fewer pixels. Removing the record unpack is worth real
things (§3.3 scale stepping, §3.5 per-sprite decisions) but it is not this ~100 lines.

**THE CANDIDATE, written not built.** Take ~100-107 lines (29-30%) out of the sprite plot's
per-pixel path, which is 73.7% of the chain and the only term large enough. Gate: updates per
60 at a matched tick against the arcade's 59, ranked on flips per 100 vints and longest hold,
`check-demo` unchanged at 2 of 20, and **band 2 lines per generation (182-194 today) as the
direct number**.

**UNSETTLED, and it bounds everything above.** `SPR_SNAP` is a SNAPSHOT at the run's end, so
each frame point is ONE generation's sprite set, not a mean over the 64 in the ring; six
points were read for that reason, but they are six generations out of ~50. And the per-band
sprite/non-sprite split is inferred from band 0's floor, not measured -- per-band pass sums
need a build.

**THE RIG'S PRESENTED-FRAME BISECT: NO ROM COLLAPSES, AND clean1 WAS THE FROZEN ONE
(2026-10-14).** `BOOTFLIPRATE=1`, presented frames per 64 vints off the value channel
(bias bit 7, count in the low 7 -- md_main.c:4166-4182, FS bank changes between vint tops).
**Three log-proven launches per rom, distinct names, 22-24 samples each:**

| rom | per-launch means | mean | median | max | fps of 60 |
|---|---|---|---|---|---|
| **clean1** (86bef0f7) | 0.62, 0.75, 0.75 | **0.71** | 0.0 | 3 | **0.7** |
| **marks2** (0c8955d4) | 8.75, 7.12, 9.75 | **8.54** | 7.0 | 24 | **8.0** |
| line, HEAD | 8.33, 9.50, 8.75 | 8.91 | 8.0 | 20 | 8.4 |
| **c1r** (1cb9d626) | 7.50, 8.25, 9.38 | **8.45** | 8.0 | 19 | **7.9** |

**NOTHING COLLAPSES AT c1r.** It is 8.45 against the line's 8.91 and marks2's 8.54, medians
all 8, inside a spread that runs 2 to 24. **And marks2 is not the culprit either: clean1 is
the near-frozen rom at 0.71 frames per 64 vints**, which is what marks2 was accepted for and
is now confirmed on hardware rather than on ares. The line stays marks2.

**SO WHAT MIKE SAW IS REAL AND IS NOT A COLLAPSE.** c1r runs the ENGINE faster (+9 game
ticks by f700 on ares) while presented frames stay at 8 of 64. More motion happens between
each presented frame, which reads as a worse slideshow even though the frame rate did not
move. That is the FBXPORT / PORTTICKFREE shape exactly: **logic up, picture flat.** c1r is
not the line, and it should not become the line.

**AND A CORRECTION TO EVERY RIG VERDICT I HAVE GIVEN IN THIS ARC.** `tools/rig_bars.py`
measures **ticks per second and the one-vint share** -- the ENGINE rate. It does not measure
presented frames. So "the rig is flat at 42.2 / 66.0", which I reported for PORTHOT and
again for c1r, was always a statement about engine rate and never about the picture. The
picture needs `BOOTFLIPRATE` and the value channel. **Mike's eye caught a distinction my
instrument could not express**, and the pair -- ticks/s AND presented frames per 64 -- is
what a rig reading has to carry from here.

**THE STANDING NUMBER: the port presents ~8 frames per 64 vints on real hardware, 8 fps of
60, and has since marks2.** Every ares cadence number in this document sits above that
ceiling.

**THE COST IS SPREAD: 240 PCs CARRY 80% OF IT. THAT IS THE ANSWER (2026-10-14).** No build.

**1. THE PC TABLE.** btab minus c1r, f600-f700, master, inside `_m_main`
(0x06033900-0x060378a8). Positive delta totals **80,973 cyc/vint** across the function:

| pc | c1r | btab | delta | cum |
|---|---|---|---|---|
| 0x060360b4 | 0 | 3,434 | +3,434 | 4.2% |
| 0x0603745e | 0 | 2,024 | +2,024 | 6.7% |
| 0x060373b0 | 6 | 1,658 | +1,651 | 8.8% |
| 0x0603754c | 0 | 1,652 | +1,651 | 10.8% |
| 0x060362d6 | 2,155 | 3,110 | +955 | 12.0% |
| 0x060360c6..0x060360ca (4 PCs) | 0 | 755 each | +755 each | 16.7% |
| 0x060373ae, 0x0603754a | 0 | 736 each | +735 each | 18.5% |
| 0x060360c0..0x060360bc (3 PCs) | 0 | ~600 each | ~+600 each | 20.7% |

**The top 15 carry 20.7%. It takes 240 PCs to reach 80%.** The delta is not a hot site; it
is the whole function doing more.

**THE TOP FIVE, NAMED from their context:**

1. **0x060360b4** -- `c1rt_class`'s 64-pixel mask loop (m_main.c:1151-1167), already named.
2. **0x0603745e** -- `mov.l @(r0,r2),r3 / dt r1 / mov.l r3,@r0 / add #4,r0` with r1 = 0x170:
   a **368-longword (1,472 byte) block copy**.
3. **0x060373b0** -- `mov.l @r2+,r7 / mov.l r7,@r3` into 0x1DFD8000+r2, immediately followed
   by a load of `_fbx_seq_seen`: an **FBX packet staging copy**.
4. **0x0603754c** -- the same shape into 0x1DFE0080, also followed by `_fbx_seq_seen`: the
   **second FBX staging copy**.
5. **0x060362d6** -- `mov.l @r9,r0 / cmp/eq r0,r14` walking a longword array: a **tag scan**.

Four of the five are *shipping and staging*, not computing. The picture is consistent and
boring: **with a second table resident the rom ships more art and stages more packets per
vint, everywhere, and no single site dominates.**

**2. THE EVICTIONS ARE NOT THE RENUMBERED SETS.** `cs_[22]`/`cs_[23]` sampled at f620, f660,
f700: c1r 486/488/492 with last cset 80, 80, 86; btab 746/772/802 with last cset **78, 80,
101**. None of the four renumbered sets (92, 93, 94, 100) appears. And the bake-bug
hypothesis is already ruled out independently: table 11's index row has **no 0xFFFF entries**
for demo A's sets, and 25 of 28 share table 0's blocks by content key exactly as the dedup
intends. (Caveat: three samples of a ~77-per-100-vint rate is weak evidence on its own; the
index check is the strong one.)

**SO: SPREAD, AND NOT A BAKE BUG.** Nothing here reopens (b). The structural reading stands
-- a second resident palette table makes this renderer ship more of everything -- and it is
now supported by the shape of the delta rather than only by the failure of four diagnoses.

**THE REFUSE-BYPASS IS REAL IN SOURCE AND IS NOT THE COST: NOT CONFIRMED (2026-10-14).**
Fourth attempt, fourth symptom. The table approach is closed.

**THE BYPASS IS REAL.** m_main.c:4100-4107 under `MD_ROUND`:

        unsigned ti = (mds_onscreen && md_round < MDROUND_N) ? md_round : 0xFFu;
        if (ti < MDSTATIC_N && !mds_s_line[ti][cset] ...) refuse

`ti` is a TABLE index bounded by `MDROUND_N` = 11, and the guard tests `MDSTATIC_N` = **2**
(pal_scenes_md.h:5), the old pscene bound. **So every installed table above index 1 bypasses
the refuse** -- table 10 at the title, table 11, and gameplay rounds 2-4. Table 0 passes.
That is a genuine defect in the tree and it should be filed whatever happens to (b).

**BUT IT IS NOT THE 40 TICKS.** The predicted signature was hundreds of assigns on btab.
Per 100 vints, f600->f700:

| counter | c1r | btab |
|---|---|---|
| assigns `cs_[24]` | **+9** | **+11** |
| evictions `cs_[22]` | +10 | +77 |
| claims `cs_[20]` | +8 | +1 |
| line-less `cs_[18]` | 0 | 0 |

**Assigns are 9 against 11, not hundreds.** Evictions are 7.7x but 77 per 100 vints cannot
be 52,943 cyc/vint at any plausible cost per eviction. The confirmation criterion the card
set is not met.

**AND THE DIFFERENTIAL SYMBOLS ARE NOT THE ALLOCATOR.** btab against c1r, master cyc/vint:

| symbol | c1r | btab | delta |
|---|---|---|---|
| `_m_main` | 118,697 | 171,640 | **+52,943** |
| `_visr_vbi` | 44,872 | 50,644 | +5,772 |
| `___lshrsi3_r0` | 1,992 | 6,188 | +4,195 |
| `_md_emit_art.constprop.0` | 293 | 2,816 | +2,524 |
| `___ashlsi3_r0` | 5,970 | 8,170 | +2,200 |
| `_mdp_note_tile.isra.0` | 12 | 1,283 | +1,271 |

`mdp_assign_set`, `mdp_extend_set`, `mdp_near` and `mdp_free_set` -- the four the card
predicted -- do **not** appear. 98% of the delta is inside `_m_main` itself.

**FOUR ATTEMPTS, FOUR SYMPTOMS, ONE SHAPE.** 37 ticks blamed on a short set list; 43 on the
cat-1 mask bound; 40 with the union corrected and the bound fixed; and now the refuse bypass,
real in source and measured at 2 extra assigns per 100 vints. Each diagnosis was true as far
as it went; none was the cause. **The table approach is closed by item 4's own rule.**

**WHAT SURVIVES IT:** `C1MASKROUND` (gated, staged), the refuse-bypass as a filed defect, the
tilemap union method, the numbering-preserving packer, and the detector pair (the tick at
f700 and `_m_main` cyc/vint).

**THE UNION IS RIGHT NOW AND (b) STILL COSTS 40 TICKS WITH THE FIX ON. THIRD FAILURE, THIRD
DIFFERENT SYMPTOM: STOP THE TABLE APPROACH (2026-10-14).**

**1. THE UNION FROM THE TILEMAP.** `(w >> 6) & 0x7F` over the tile RAM at f520, 600, 700,
800, 900, 1000 and 1080: **31 distinct csets, identical at every frame** --
19, 20, 21, 74-101. The diff against the md_tag union is exactly the class predicted:

- **in the tilemap, missed by md_tag: 90, 91, 94, 99** -- the sets with few or no resident
  art slots, which is why a tag census could not see them;
- in md_tag, absent from the tilemap: 0, 37-40, 44, 45 -- sprite and text csets, which want
  no tile line at all.

(Sets 72 and 73 appear in neither. They were a stale header row.) **Caveat: this walks the
whole tile RAM, not the visible window, so it is a SUPERSET** -- safe for a palette table,
since no set can be left unlined, but it may carry sets demo A never shows.

**2. THE REPACK ON THAT UNION FITS.** 31 sets, line-4 sets 92/93/94/100 needing 11 colours
of which 4 are reused, 7 new slots against 9 free: **lines [15, 15, 13, 0], line 4 EMPTY, 4
sets renumbered, 318 blocks against the ~335 ceiling.** It links.

**3. AND IT IS STILL 40 TICKS BEHIND, WITH `C1MASKROUND=1` ON** (both flags verified in
`.build_flags`): btab f700 reads **195 against c1r's 235**. The cat-1 mask fix is correct
and is a small win on its own, and it is **not** the cause of the table's cost.

Where the cost sits now, btab against c1r, f600-f700, master cyc/vint:

| symbol | c1r | btab | delta |
|---|---|---|---|
| `_m_main` | 118,697 | 171,640 | **+52,943** |
| `_visr_vbi` | 44,872 | 50,644 | +5,772 |
| `___lshrsi3_r0` | 1,992 | 6,188 | +4,195 |
| `_md_emit_art.constprop.0` | 293 | 2,816 | +2,524 |
| `_mdp_note_tile.isra.0` | 12 | 1,283 | +1,271 |

**The mask loop is still running, AND art is being re-emitted, AND tiles re-noted.** That is
slot churn across the board, not one symptom.

**THREE ATTEMPTS, THREE SYMPTOMS, ONE SHAPE.** 37 ticks blamed on a short set list; 43 on
the cat-1 mask bound; 40 now with the union correct and the bound fixed. Each diagnosis was
true as far as it went and none was the cause. **A second resident palette table is
expensive in this renderer for structural reasons** -- the slot/art/mask state is keyed on
assumptions a single table satisfies -- and symptom-chasing is not converging.

**RECOMMENDATION: stop the per-step table approach.** Freeing an MD line for sprites by
giving demo A its own table has now cost four boxes and produced three negatives and one
unrelated fix. If the sprite offload is still wanted, it needs a route that does not add a
resident table, and if none exists, the offload should be closed with these numbers against
it. `C1MASKROUND` stands on its own merits and is the one keeper.

**THE FIX IS BUILT AND GATED: `C1MASKROUND=1`. NEUTRAL ON HARDWARE, BETTER ON LOGIC, AND IT
UNBLOCKS ANY TABLE ABOVE INDEX 4 (2026-10-13).** Rom `c1r.32x` / `c1rrig.32x`, build_id
1cb9d626, control the line at the same HEAD.

**AND MY TITLE PREDICTION WAS WRONG.** I said the line was probably paying the 58k at the
title through table 10. It is not. The title's `_m_main` IS high -- **159,247 cyc/vint
against demo A's 112,114** -- but its hot PCs are the main loop's own poll of `pp_due`,
`visr_park` and `nat_gen_open` at 0x06033b60-0x06033ba0: **the master idling with little to
draw.** `___lshrsi3_r0` moves only 2,578 against 1,847, where the broken table moved it to
6,110. Withdrawn.

**THE FIX.** `c1mask_install` now takes `WRAM_BASE[0x3142] & 7`, the round, instead of the
table index. Justified by the baker: `bake_cat1mask.py:22` builds scene s from
`SCENE_TABLE + s * 6` at 0x1CE2, the 68K's per-round table, so **scene s IS round s** and
the palette table was never the right key. The round is 3 bits, so the `>= CAT1MASK_SCENES`
bound cannot be reached.

| gate | line | c1r |
|---|---|---|
| game tick at f700 | 226 | **235 (+9, the game runs FASTER)** |
| check-demo | 2 of 20 | **1 of 20**, 1 NOT REACHED |
| port clock | x1.010 | x1.010 |
| title picture at f350 | -- | **0 differing pixels** |
| demo A at matched tick 226 | -- | 14,316 px (the cadence band; one frame of natural motion is 10,188) |
| `_m_main` cyc/vint, title | 159,247 | 160,864 |
| `_m_main` cyc/vint, demo A | 112,114 | 118,697 (more work per vint: it is not idling) |
| updates/60, MOTION, hold | 25.3 / 11.0 / 9 | 24.3 / 10.7 / 9 |
| **rig, three launches** | **42.2 t/s, 66.0%** | **42.8, 41.4, 42.4 -> 42.2 t/s; 67.0, 64.3, 66.7 -> 66.0%** |

**THE RIG IS FLAT.** Identical to the control on both numbers. So on hardware the fix is
neutral: it is not a speed card. What it IS is the **precondition** -- without it any
palette table above index 4 costs +58,039 cyc/vint and 43 game ticks, which is what killed
(b) twice, and with it (b) and (c) become buildable.

Treat `check-demo` 2 -> 1 with the usual caution: a moved clock can land the walk on the
other side of an object update, as penfix1 showed. The +9 ticks and the identical title are
the solid numbers.

**NAMED: `CAT1MASK_SCENES` IS 5 AND THE TABLE INDEX IS THE SCENE INDEX, SO ANY TABLE ABOVE 4
LOSES ITS BAKED CAT-1 MASKS AND REBUILDS THEM PER TILE PER FRAME (2026-10-13).**

**THE LOOP IS `c1rt_class`, m_main.c:1151-1167.** Disassembled at demoa 0x060360b4: outer
count 8, inner count 8, `mov.b @r6+,r4`, `0x80 >> r4` through `___lshrsi3_r0`, `or` into r5,
one `mov.b r5,@r14` per outer pass. That is exactly

        for (py = 0; py < 8; py++) { mb = 0;
            for (x = 0; x < 8; x++) if (px[py*8+x]) mb |= 0x80u >> x;
            c1mask_bits[... + py] = mb; }

the RUNTIME fallback that builds a tile's opacity mask when the scene's BAKED set does not
carry it. **It is not `mdp_note_tile`** -- that is `_mdp_note_tile.isra.0` in cart at
0x02044284 and its counters say so: `cs_[18]` reads 3 on ctl3 against 7 on demoa over 700
frames, not thousands. Candidate (a) is refuted by its own detector.

**THE CAUSE.** `mds_install(unsigned sc, ...)` is called with the TABLE INDEX
(`mds_install(rk, ...)` at :8602, `mds_install(r9, ...)` at :8616) and passes it straight to
`c1mask_install(sc)` at :4462. That function begins:

        if (sc >= CAT1MASK_SCENES) { c1mask_n = 0; return; }   /* m_main.c:1171 */

and `CAT1MASK_SCENES` is **5** (sh_src/cat1mask.h:4). **Table 11 is not 0-4, so the baked
set is emptied and every cat-1 tile takes the runtime builder, every frame.** That accounts
for every observation at once: identical on demoa and keep4 (both install 11); zero on ctl3
(table 0 is in range); independent of pen numbering and of the empty line 4; continuous and
growing; the selector itself free; no extra installs, because the fallback is per TILE and
not per install.

**AND THE LINE ALREADY PAYS THIS AT THE TITLE.** `MDR_ATTRACT_TABLE` is **10**, also above
4, and the static half installs it at the title by the same path. **PREDICTED, NOT YET
MEASURED** -- the one read that settles it is the master's `_m_main` cyc/vint across a title
frame against a demo frame on the LINE, no build. If it holds, the line has been paying a
per-tile variable-shift mask rebuild through every title screen, and nobody has profiled the
master there.

**THE FIX IS NOT A BIGGER UNION.** Both of my failed attempts (37 ticks, then 43) were read
as a short set list; they were not. `c1mask_install` needs the table index MAPPED to a baked
cat-1 scene, or `cat1mask.h` needs an entry for every installable table. Until then **any
table index above 4 costs ~58,000 cyc/vint**, which is why (b) and (c) cannot be built and
why a bigger union would not have saved either of them.

**THE DETECTOR, for any future table change:** the game tick at f700 against the line's, and
`_m_main` cyc/vint from the profile. `cs_[18]` is NOT the detector -- it stayed at 7.

**THE 43 TICKS ARE NOT THE INSTALL: NO OSCILLATION, NO REPAINT. IT IS A LIBGCC SHIFT LOOP
IN `_m_main` (2026-10-13).** Reads only, on the four roms rebuilt with their own lsts
(`mds_ctr` lands at 0x060189c8 on all four, so they are directly comparable).

**1. THE INSTALL CENSUS KILLS THE HYSTERESIS SUSPECT.** `mds_ctr` at f500, f600 and f700:

| rom | installs MDS[0] | flushes MDS[2] | MDS[5] |
|---|---|---|---|
| ctl3 | 5 | 2 | 66304 |
| demoa | **5** | **2** | **66304** |
| nofire | 5 | 2 | 66304 |
| keep4 | **5** | **2** | **66304** |

**Identical on all four and unchanged from f500 to f700.** There is no oscillation and no
extra install at all -- the demo-A table is selected but never re-installed. My hysteresis
hypothesis and the addendum's per-frame `:16090` detector are both ruled out by this, as is
the "why does install(11) cost what install(0) does not" question: neither install happens.

**2. AND NO CRAM REPAINT.** DIAG[19] CRAM writes performed / DIAG[20] groups skipped, over
the same vints: ctl3 675/1416 at f700, demoa **351/903**. demoa does FEWER CRAM writes, not
more. The memo-miss path in the addendum's (b) is ruled out too.

**3. THE WHOLE COST IS `_m_main`.** Master profile differenced f600->f700, 100 vints,
cyc/vint:

| symbol | ctl3 | demoa | delta |
|---|---|---|---|
| **`_m_main`** | 111,190 | **169,228** | **+58,039** |
| `___lshrsi3_r0` | 1,859 | 6,110 | **+4,251** |
| `_port_object_dispatch` | 22,625 | 14,409 | -8,215 |
| `_visr_vbi` | 44,658 | 50,289 | +5,631 |

Everything else falls because demoa runs fewer game frames. **+58,039 cyc/vint is 15% of the
master's 384,017 and is the 43 ticks.**

**4. AND INSIDE `_m_main` IT IS A VARIABLE-SHIFT LOOP THAT ctl3 NEVER ENTERS.** The hot
offsets are ZERO on ctl3: `+0x27b4` (3,375 cyc/vint), `+0x3b5e` (2,024), `+0x3ab0` (1,658),
`+0x3c4c` (1,652), plus their neighbours. Disassembled at demoa's 0x060360b4:

        bt      +0x27c6
        mov.l   <___lshrsi3_r0>,r1
        mov     r11,r4
        jsr     @r1                  <- a libgcc VARIABLE SHIFT, per iteration
        mov     r7,r0
        or      r0,r5
        add     #1,r8
        dt      r3
        bf.s    +0x27b4              <- inner loop
        mov.b   r5,@r14              <- one byte built per outer iteration

A double loop building a mask byte a bit at a time through `___lshrsi3_r0`, which is the
call the SH-2 needs because it has no variable shift (the same trap m_main.c:7595 already
warns about on the zoom path). **It does not run on the control at all.**

**WHAT IS NOT ESTABLISHED: which C site that is.** It is inlined into `_m_main` and I did
not trace it back to source. That is the next read and it is small -- and it matters,
because a per-frame bitwise mask rebuild that only fires for a non-zero table index is as
likely to be a LINE defect waiting for a second table as it is a property of this card.

TREE: reverted and verified, `make port` reads tick 226 at f700.

**OPTION (b) IS DEAD: INSTALLING A SECOND TABLE DURING THE DEMOS COSTS ~43 GAME TICKS,
WHATEVER IS IN IT (2026-10-13).** The packing problem is solved; the table switch is not.

**THE PACKING WORKS.** Relocating the line-4 sets must REUSE colours already on lines 1-3:
demo A's 27 lined sets occupy 47 SLOTS but only 41 DISTINCT COLOURS, because the shipped
table repeats six colours across lines. Four of the 11 colours sets 92/93/100 need are
already present, so only 7 new slots are wanted against 9 free:

    28 sets, 7 slots freed by the 6 sets demo A never shows
    of 11 colours, 4 REUSED, 7 new slots taken, 2 still free
    lines [15, 15, 13, 0] -- line 4 EMPTY; RENUMBERED: 92, 93, 100 (3 sets)
    316 blocks against the ~335 ceiling, and it LINKS.

**AND THE ROM IS 43 GAME TICKS BEHIND ITS CONTROL AT f700** (192 against 235), with the gap
GROWING -- 26 ticks at f600, 43 at f700 -- so the cost is continuous, not a one-off switch.

**THE A/B CHAIN THAT LOCATES IT.** Each row is a build, same HEAD, tick at f600/f700:

| build | what it isolates | f600 | f700 | verdict |
|---|---|---|---|---|
| `ctl3` control (table present, flag off) | -- | 136 | 235 | baseline |
| `nofire` selector compiled in, compare unreachable (`MDDEMOASTEP=255`) | the selector's PER-CALL cost | **136** | **235** | **exactly the control: the selector is FREE** |
| `demoa` line 4 emptied, 3 sets renumbered | the full change | 110 | 192 | -43 |
| `keep4` line 4 KEPT, **0 sets renumbered, 0 new blocks** | the table's CONTENT | 110 | **194** | **-41, the same** |

So it is not the selector (0), not the renumbering (0), and not the empty line 4 (0).
**It is installing a second table during the demos at all.** `keep4`'s table is table 0
minus only the six sets demo A never shows, with every pen number identical, and it costs
the same 41 ticks.

**LEADING SUSPECT, NOT PROVEN: the install hysteresis.** `mds_install`'s gate is
`mds_cl_t > mds_cl_n` -- table cells against non-table cells -- and the MD_ATTRACT_SPLIT
note at m_main.c:2333-2345 already records that gate deadlocking when the installed table
does not describe what is on screen: *"the path that would install the RIGHT table is
switched off BY the wrong table being installed. A deadlock, not a tuning problem."* Six
absent sets are enough to move that ratio. **This is a hypothesis with a citation, not a
measurement**; the counter that would settle it is a per-frame record of which table is
installed, which is a probe build.

**SO (b) AND (c) ARE BOTH BLOCKED**, and not on cart space or on packing -- on the cost of
having two tables live across one attract. Freeing an MD line for sprites needs either that
cost explained and removed, or a way to free the line WITHOUT a second table (one table that
already fits three lines for both demos, which `round_split --demo` says is impossible at 33
sets).

TREE: reverted and verified -- `make port` reads tick 226 at f700, the known-good line's.

**THE CART IS NOT THE BLOCKER: THE BAKE ALREADY DEDUPES, AND A NUMBERING-PRESERVING TABLE
COSTS SIX BLOCKS AND LINKS (2026-10-13).** But the set list I built it from was wrong twice
over, and with the corrected list the construction misses by two colour slots. No rom.

**1. THE BAKE ALREADY DEDUPES BY CONTENT.** `bake_tiles_md.py:191` keys every block on
`(cs, pen map, layer mask)` and shares across tables -- the advisor's observation (1) is
already implemented. So a new table costs blocks only for sets whose KEY changes, and both
of my earlier attempts changed it needlessly: the fresh three-line repack renumbered every
set (31 blocks) and the measured layer masks changed the mask half of the key as well
(which is why 17 "single-plane" sets did not save what their block count promised).

**2. PRESERVING TABLE 0'S NUMBERING COSTS SIX BLOCKS AND LINKS.** `round_split.py --demoa
--preserve` (new) builds demo A's table as table 0 with the sets demo A never shows removed
and only its line-4 sets relocated into the colours they free:

| | blocks | links? |
|---|---|---|
| the line, 11 tables | 310 | yes |
| fresh 3-line repack, 12 tables | 341 | **no, over by 11,263 B** |
| **numbering preserved** | **316** | **YES** |

So the block ceiling is about 335 and the line sits at 310: **~25 blocks of headroom, and a
preserving table needs 6.** **My "11,263 bytes short" is retired, and so is the need to move
`.palscenes` or reach the 256 KB hole** -- that room is real and worth having, but this card
does not need it.

**3. AND THE SET LIST WAS WRONG TWICE.** Both errors are mine and both are the same shape:

- **The cset is `(md_tag >> 16) & 0x7F`**, the port's own extraction at m_main.c:3676. I
  masked 0x7FFF, which let a second flag bit through and invented csets.
- **I sampled frames 680-780, a 100-frame window of a 600-frame phase.** The step byte reads
  0x0C from f500 to f1100. Over the whole span the union is **34 csets, not 30** -- sets 0,
  19, 20 and 21 appear as the phase turns over at f1090 and my table gave them no MD line.

The rom built from the wrong list ran **37 game ticks behind the line by f700** (198 against
235) with ~45% of pixels differing. That is what a set with no line costs.

**4. WITH THE CORRECTED LIST THE PRESERVING CONSTRUCTION MISSES BY TWO SLOTS.** 28 sets need
lines, the 6 sets demo A never shows free only 7 slots, and the three line-4 sets (92, 93,
100) need 11 colours against **9 free**. The fix is a partial renumber -- relocate one or
two small sets as well, which the 25-block headroom easily affords -- and it is not built.

**5. A TRAP FOR THE NEXT ATTEMPT: ADDING A TABLE SHIFTS THE CLOCK.** A build carrying the
demo-A table with the flag OFF still read tick 235 at f700 against the line's 226. So a
candidate can never be frame-matched against the line -- **the gate must be tick-matched**,
and my first identity run compared the candidate against that contaminated control and was
meaningless.

**TREE: verified clean.** `make port` rebuilt and is pixel-identical to the known-good line
rom at f700 with the same tick (226). Worth stating because `sh_src/tiles_md.bin` on disk
had been stale since 2026-10-10 and the bake does not reproduce its byte size -- the
regenerated blob is correct, which the identity proves.

**OPTION (b) IS BUILT AND IS 11,263 BYTES SHORT OF THE CART (2026-10-13).** The step byte
is established, the table packs, the selector is wired -- and the tile art it needs does not
fit. Six 2 KB blocks.

**THE STEP BYTE IS 0xFFF031**, established from the port's own work RAM, not from the chop
table's label. An aligned dump diff over demo A (680, 740, 780) against demo B (1756, 1800)
left 195 bytes constant-within and different-between in the globals region; **0xFFF031 is
the first of them and the only one carrying the 0x0C/0x14 pair**. Traced across the attract:

| frames | 300, 400 | 500-1100 | 1300, 1500 | 1700-1900 |
|---|---|---|---|---|
| 0xFFF031 | 0x08 title | **0x0C DEMO A** | 0x10 interlude | **0x14 DEMO B** |

Constant inside each phase, changing only at the boundaries. **0xFFF022, which the existing
static compare uses, cannot serve: it reads 0x09 in BOTH demos** (same dumps). The Makefile
already calls 0xFFF031 "scene state" at the CUT30 note, which corroborates it.

**THE TABLE PACKS AND THE SELECTOR IS WIRED.** `SPLIT_LINES=3 tools/round_split.py --demoa`
(new) packs demo A's 24 table sets into **[15, 14, 15], line 4 EMPTY**, appends it as table
11 and defines `MDR_DEMOA_TABLE`; `md_attract_table()` picks it on `WRAM_BASE[0x3031] ==
0x0C` under `MDDEMOA=1`, demo B keeping table 0 untouched.

**AND THE TILE ART DOES NOT FIT.** A new table re-bakes its sets' MD-format art:

| | blocks of 2 KB | tiles_md.bin |
|---|---|---|
| 11 tables (the line) | 310 | 637,696 B |
| 12 tables, demo A added | **341** | 701,440 B |
| the table's own cost | **31 (63,488 B)** | |

**`.tilesmd` then runs to 0x002FABFF against `.palscenes` at 0x002F8000: OVER BY 11,263
BYTES, six blocks.**

The measured layer masks already paid for most of it. `emit_masks()` inherits each set's
mask from its source table and defaults to 3 (both planes), which would have been 48 blocks;
**the name table's own plane bit (md_tag bit 15) says 17 of the 24 sets are SINGLE-plane, so
31 blocks, 34,816 bytes less.** That saving is in the number above and it is still short.

**SO (b) NEEDS SIX BLOCKS OF CART, AND (c) SITS BEHIND IT.** Three ways out, none built:
drop six more sets to backdrop (costs exactly the colour (b) existed to save); move
`.palscenes`, which is 0x909D of art at a fixed LMA; or find the blocks elsewhere in a cart
whose port build has ~10 KB spare. **The cart, not the palette, is now the binding
constraint on the whole offload arc.**

TREE STATE: the generated files are reverted and `make port` builds; the recovered
`tiles_md.bin` was verified picture-identical to the previous line rom (0 differing pixels
at f700), which matters because the shipped copy on disk had been stale since 2026-10-10 and
the bake does not reproduce its byte size.

**THE OFFLOAD OPTIONS TABLE: (b) IS FREE, (c) REACHES THE THRESHOLD, (d) IS IMPOSSIBLE
(2026-10-13).** Reads only, on the line.

**1. PER-STEP TILE TABLES.** Demo A's on-screen set is STABLE: the name table at 680, 700,
720, 740, 760, 780 reads **30 distinct csets every frame, union exactly 30**, 971-975 live
cells. Packed with `round_split`'s own `describe`/`pack` (its 3-line limit lifted so the
minimum can be found):

| demo A set | sets | distinct colours | 2 lines of 15 | 3 lines | 4 lines |
|---|---|---|---|---|---|
| all 30 on screen | 30 | 42 | no | **no** | yes [15,14,12,10] |
| 24, the six already-backdropped excluded | 24 | **37** | no | **YES [15,14,15]** | yes |

**Sets 37-40, 44 and 45 have NO MD line TODAY and already render as backdrop** (f700: 6 sets
with no line). So a per-step demo-A table that keeps exactly today's backdrop set
**fits three lines and frees line 4 at ZERO NEW tile loss.** Two lines is impossible: 37
colours against 30.

**The selector** is `md_attract_table()`, m_main.c:2353-2359 -- `WRAM_BASE[0x3142] & 7` for
the round, with a single byte compare `WRAM_BASE[0x3022] == 0x0C` picking the attract
table. It is a plain inline returning a table index, so a second discriminator is additive,
one more compare. **The step byte's own address is NOT in my hands** -- the advisor's 0C/14
come from the chop table -- and it has to be named before this is built.

**2. SPRITE PEN OVERLAP AND MERGE ERROR.** Colours are `PAL_SH[1024 + cset*16 + p]`, p=1..14
(m_main.c:6843), quantised with the live `mdp_q5` form:

| set | live pens | distinct quantised colours |
|---|---|---|
| 0x07 | 14 | 14 |
| 0x00 | 14 | 13 |
| 0x02 (player, for the record) | 14 | 13 |

**0x07 and 0x00 share only 3 colours exactly; their union is 24, against the 15 one MD line
holds.** Greedy nearest-colour merge to 15: **max squared distance 5 = 2.24 levels of 7,
mean squared 1.00 = 1 level.** So a merged line moves the worst pen about two steps of eight
and the average pen one.

**3. THE OPTIONS TABLE**

| | chain | demo-A tile cells losing colour | sprite colour error | MD lines | blocker |
|---|---|---|---|---|---|
| (a) line 4 taken, 0x07 alone | ~264 | **104 of 975 (10.7%)** | none | 1 | none |
| (b) per-step demo-A table on 3 lines, 0x07 alone | ~264 | **0** | none | 1 | the step byte's address |
| (c) (b) + 0x00 merged onto 0x07's line | **~201** | **0** | max 2.24 / mean 1.0 levels | 1 | as (b) |
| (d) demo A on 2 tile lines, 0x07 and 0x00 separate | ~201 | 0 | none | 2 | **STRUCK: 37 colours do not fit 30** |

**(b) strictly dominates (a)** -- same chain, none of the 104-cell loss -- and costs only the
per-step table. **(c) is the only row that reaches the 250 threshold**, and it buys that with
a palette error on two mob sets rather than with lost tile colour, which is the side of the
line Mike's release bar puts in the "our methods/palette OK" column.

**THE P3 SPRITE OFFLOAD, CENSUSED: ONE SET IS AFFORDABLE AND IT COSTS 10.7% OF THE
ON-SCREEN TILE CELLS THEIR COLOURS (2026-10-13).** Reads only, on the line (`ctl.32x`).

**1. SPRITE SETS IN DEMO A** (SPR_SNAP, six points 680-780, cset = `e[4] & 0x3F`):

| cset | sprites | rows | row-pixels | % frame | band-2 rowpx | % band 2 | class |
|---|---|---|---|---|---|---|---|
| **0x07** | 3.0 | 174.7 | **6,563** | **33.5%** | 4,223 | **35.9%** | mob, ELIGIBLE |
| **0x00** | 2.0 | 128.0 | 4,608 | 23.5% | 2,880 | 24.5% | walkers, ELIGIBLE |
| 0x02 | 1.0 | 70.5 | 4,037 | 20.6% | 2,293 | 19.5% | **player, STAYS** |
| 0x03 | 1.0 | 58.5 | 2,871 | 14.7% | 1,592 | 13.5% | hero-shaped, STAYS |
| 0x08 | 2.8 | 47.7 | 953 | 4.9% | 450 | 3.8% | eligible |
| 0x01 | 2.0 | 12.0 | 336 | 1.7% | 336 | 2.9% | eligible |
| 0x09 | 0.8 | 8.8 | 216 | 1.1% | 0 | 0.0% | eligible |
| total | | | 19,583 | | 11,774 | | |

**Mobs are 64.7% of frame row-pixels, heroes 35.3%.** But **one MD line holds ONE sprite set
(15 pens)**, so the trade is one set, and the top one is 0x07 at 33.5%.

**2/3. THE FOURTH LINE IS NOT FREE.** `round_split --demo`: the demo half carries **33 sets
and does NOT pack into three lines of 15** -- that is why `MDP_LINES4` exists. `mdp_s_line`
at 680/720/780 is identical: **line 1 = 4 sets, line 2 = 9, line 3 = 16, line 4 = 4**.

And line 4's sets are genuinely on screen. From the name table at f700 (`nt_dump_ares` ->
`md_tag`, plane bit masked off), **30 distinct csets over 975 live cells**:

| MD line | sets on screen | cells |
|---|---|---|
| 1 | 2 | 91 |
| 2 | 4 | 139 |
| 3 | 15 | 537 |
| **4** | **3** (92, 93, 100) | **104** |
| none | 6 (37-40, 44, 45) | already BACKDROP |

**Taking line 4 for sprites costs 104 of 975 on-screen cells (10.7%) their palette**, joining
the six sets already rendering as backdrop. (Correction to my own first pass: I read
`mdp_s_used` as an on-screen flag and it is a per-pixel mask, m_main.c:3794 -- the name
table is the instrument, not that array.)

**4. VDP CAPACITY IS NOT THE CONSTRAINT.** Set 0x07 is 3.0 sprites a frame averaging ~58
rows, so ~2 cells of 32x32 each: at most 3 on any scanline against the VDP's 20 per line and
80 per frame. And the art blob is intact and linked on the line -- `_md_sprart_blob` at cart
0x022F9100, 20,960 bytes in the 27,392-byte slot to 0x300000 -- so the addendum's blocking
item (b) does **not** block.

**5. THE VERDICT, numbers only.** Moving set 0x07 to the VDP sheds **33.5% of the sprite
plot. The plot is 267 lines of the chain's 353, so ~89 lines leave: chain 353 -> ~264**,
against the 250 a one-vint frame needs. Adding 0x00 would shed another 23.5% (~63 lines,
chain -> ~201) **but needs a second MD line, and there is not one.**

**THE TRADE, for Mike under accuracy-before-speed:** one sprite set to the VDP, chain 353 ->
~264 (still short of 250), in exchange for **104 of 975 on-screen tile cells losing their
colours in demo A**. My read is that 10.7% of the screen going backdrop-coloured is a
visible loss and ~89 lines does not reach the threshold that would pay for it -- but the
number is his call, not mine.

**THE BAND REBALANCE IS MEASURED DEAD, AND THE RECORD NAMES WHAT WOULD CHANGE THAT
(2026-10-13).** No build. The premise check caught it before the box was spent.

`m_main.c:1715-1718`, at the `NAT_MLO` macro the card points to: *"BANDSHIFT=36 /
RG2SHIFT=40: every master range is EMPTY (docs/design/ORACLE.md: the rebalance measured
dead, 4 points)."* And `ORACLE.md:57`: **"Master row rebalance (BANDSHIFT sweep): DEAD,
measured 4 points."** The full result is `LESSONS.md:1600`:

> Addendum (probe13, MTASKINWIN with the master owning rows, SHIPBANDSHIFT=4
> SHIPRG2SHIFT=0): DEAD. The master's stage-1 strip took ~176 lines in the window (**its
> chunk runs from cart ROM -- nat_mchunk is not RAMCODE and there is no .ramtext room** --
> and it **serialises before the blit**), FM drop 221-234, **game speed 41.9%**. **A
> both-CPU compose needs the master's half in RAM code and overlapped with the slave's, not
> queued ahead of the blit.** Knob kept off.

**Lowering BAND_SHIFT is exactly the knob probe13 turned.** Building it again without the
two conditions that entry names would reproduce 41.9% game speed.

**WHAT HAS CHANGED SINCE, and it is half of one condition:** PORTHOT proves the `.ramtext`
room exists (the cap moves to 0x39000 for 5,324 bytes, and the dispatcher-only build needs
no cap move at all) and proves what RAM residency is worth on this code -- CPI 12.61 to
3.63. So *"nat_mchunk is not RAMCODE and there is no .ramtext room"* is now answerable.
**The other condition -- overlapped with the slave's, not queued ahead of the blit -- is
untouched and unspecified**, and it is the structural half.

**ITEM 1'S ANSWER, which stands on its own and corrects something I said last round.** I
reported that the master has no idle. That was wrong in the way that matters:

| | hotdisp | porthot2 |
|---|---|---|
| master total | 384,017 cyc/vint | 384,018 |
| cart-resident | 245,970 (64.1%) | 231,435 (60.3%) |
| SDRAM | 138,048 (35.9%) | 152,583 (39.7%) |
| unaccounted | **0** | **0** |
| `_visr_vbi` FRT spin | **40,151 (10.5%)** | **40,767 (10.6%)** |

Every cycle is accounted, because a CPU that never halts always sums to the vint -- so
"idle" only ever appears as a spin. **But the V-ISR spin is the master waiting on the 68K's
post, not on its own work, so compose done inside it is free** provided it finishes before
the post arrives and is chunked finely enough not to delay the response.

And the game has margin to spare: `pp_clk_dbg[1]` (vints that found a game frame still
owed) reads **17 at f680 and 17 at f780 -- zero over the whole window**, with
`pp_clk_dbg[0]` (>1 frame waiting) at 0 ever. The master is never behind.

**SO THE ROW BUDGET IS ~40,000 cyc/vint.** Band 0 costs the slave 53-60 lines for 72 rows,
1 slave line = 1,472 SH-2 cycles, so ~1,178 cycles a row: **~34 rows on ares, ~17 rig-scaled
while game code stays in cart.** Rounded to the 12-row strip that is **two strips on ares,
one on the rig** -- and both numbers are the budget BEFORE the serialisation probe13
measured, which is a cost this sizing does not include.

**PORTHOT GATES CLEAN, AND THE RIG SAYS IT BUYS NOTHING (2026-10-13).**

**1. THE HARNESS IS FIXED.** `check_demo.py` takes `DEMO_PROBE=1` to gate a NAMED rom, and
`make check-demo` points at `$(PORT_SLUG).32x` and exports it when `PORT_EXTRA` is set. The
accidental-probe refusal is untouched. A probe's run now prints a `PROBE:` line first with
the rom, its build_id and its `PORT_EXTRA`, so a probe's result cannot be read as the
line's. Proved both ways:

| run | PROBE line | port clock | check-demo |
|---|---|---|---|
| `make check-demo` (the line) | none | x1.010 | **2 of 20**, 2 NOT REACHED |
| `hotdisp.32x` as a probe | yes, build_id 3d60102a | x1.013 | **2 of 20**, 2 NOT REACHED |
| `porthot2.32x` as a probe | yes, build_id f1519eb4 | x1.008 | **2 of 20**, 2 NOT REACHED |

**2. THE REGRESSION'S MECHANISM, confirmed and fixed.** `cport_dispatch.py:61` builds the
set of functions "in the unit" with `^(?:uint|ulonglong) (\w+)\(` anchored at column 0, so
`PORTHOT uint FUN_000103e0(` was not seen as a definition: the handler fell into
`missing_fn` at :78, got no dispatch-table row, and the binary search at port_l2.c:990-993
skipped the object. `FUN_0000669c` kept running because it is reached by direct C calls that
follow the symbol. Fixed by emitting the attribute on its OWN LINE, and
`cport_dispatch.py` now HARD ERRORS on any definition hidden from its anchor by a prefix
attribute. **The dispatch table reads 766 handlers on both the line and the PORTHOT build**
-- that count is the regression's signature and it now matches.

With the fix all four run, and the move does what it was meant to:

| function | ctl | porthot2 |
|---|---|---|
| `_port_object_dispatch` | 22,053 CPI 3.93 | 14,665 CPI 2.44 |
| `_FUN_0000669c` | 8,733 CPI 9.12 | 2,839 CPI 2.80 |
| `_FUN_000103e0` | 9,361 CPI 12.61 | 2,888 CPI 3.63 |
| `_FUN_00010824` | 7,823 CPI 13.33 | 2,298 CPI 3.67 |
| the four | 47,970 | **22,691 (-25,279)** |
| master cart-resident | 267,551 | **231,435 (-13.5%)** |

**3. AND THE RIG SAYS IT IS WORTH NOTHING. Three launches each, log-proven, distinct
names:**

| rom | ticks/s | 1-vint share |
|---|---|---|
| `ctlrig` (the line + PORTRIG) | 42.0, 42.0, 42.6 -> **42.2** | 66.0, 66.0, 66.0 -> **66.0%** |
| `hotrig` (PORTHOT dispatcher + PORTRIG) | 41.4, 42.4, 42.2 -> **42.0** | 65.3, 66.3, 65.7 -> **65.8%** |

**-0.2 ticks/s, inside the spread. An 8.1% cut in master cart cycles moves the rig not at
all** -- which is the same thing the ares motion numbers said (updates 25.3 -> 26.3) and
which the card's own framing predicted: the slave carries the chain, so master headroom is
not frame rate until the band rebalance converts it. **The rig has now priced that
conversion as the whole of the value.**

**AND THE STORED LINE FIGURE IS STALE.** `rig-line-is-34` records 34.8 ticks/s and 39%
one-vint from 2026-10-09. Today's control reads **42.2 and 66.0% over three launches**. The
line has moved since (marks2 and everything after), and the old number should not be used
as a baseline again.

**NOTE ON WHAT WAS RIG-MEASURED:** `hotrig` is the DISPATCHER-ONLY build. `PORTHOT` with
`CPORT_HOT` raises the `.ramtext` cap into 0x38000, which `RIG_SCRAP` owns, so the all-four
build cannot carry `PORTRIG` -- the Makefile now errors on that combination and says so.
The dispatcher alone needs no cap move (`.ramtext` ends 0x06037d54), which is why it is the
one that could be measured.

**PORTHOT: THE DISPATCHER MOVE IS CLEAN AND WORTH 8.1%; THE GENERATED-FUNCTION MOVE IS A
REGRESSION (2026-10-13).** Roms `hotdisp.32x` (dispatcher only) and `porthot.32x` (all
four), both build_id 3d60102a, control `ctl.32x`.

**THE DREQ CENSUS FIRST: THE PUSH IS DEAD.** `--trace-dreq` over 800 frames of `ctl.32x`
returns **ZERO rows** -- no start, no push, no read. And the arm is a compile-time no-op:

    RAMCODE static void dreq_rearm(int k) {
    #ifdef FB_XPORT
        /* the FB route never enables DREQ, and an armed DMA pointed at
         * SPR_LAND is a loaded gun aimed at the packet we just copied there */
        (void)k; return;                                  /* m_main.c:12879-12883 */

`FB_XPORT` is on the line, so the LOOP25 "SDRAM-quiet" rationale in the spin's comment is
retired: there is no FIFO transfer to stay quiet for. **The wait itself is NOT retired** --
the advisor's own re-read is right, the do-while at m_main.c:11262-11290 is the ISR polling
for the 68K's post so the flip lands in the same vblank, and skipping it skips the flip.
What it costs is the 68K's announce-to-post time. The exit distribution is not measured.

**THE BUILD, bisected.** `make port PORT_EXTRA="PORTHOT=1"` moves four functions into
`.ramtext`; `CPORT_HOT=` moves only the hand-written dispatcher.

| | ctl | hotdisp (dispatcher only) | porthot (all four) |
|---|---|---|---|
| `_port_object_dispatch` | 22,053 c/v, CPI 3.93 | **13,935, CPI 2.47** | 13,062, CPI 2.02 |
| `_FUN_000103e0` | 9,361, CPI 12.61 | 9,220, CPI 12.67 | **0** |
| `_FUN_0000669c` | 8,733, CPI 9.12 | 8,784, CPI 9.34 | 1,843, CPI 2.80 |
| `_FUN_00010824` | 7,823, CPI 13.33 | 7,650, CPI 13.18 | **0** |
| master cart-resident | 267,551 | **245,970 (-8.1%)** | 209,677 (-21.6%) |

**`hotdisp` IS CLEAN: every other function behaves exactly as on the control and the
dispatcher's CPI falls 3.93 to 2.47 for 21,581 cyc/vint.** It is the candidate.

**`porthot` IS A REGRESSION. `FUN_000103e0` and `FUN_00010824` NEVER EXECUTE -- zero
cumulative cycles from boot, and their thunks `_thk_FUN_000103e0` / `_thk_FUN_00010824` read
zero too**, against 285,092 and 223,756 on the control. Ruled out: it is not a window
artefact (tick-matched 206->306, porthot f671-771, still zero); it is not a failed boot copy
(the live SDRAM at 0x37ea0 matches the ELF's `.ramtext` bytes exactly); the symbols are
placed correctly (`nm` puts them at 0x06037eac and 0x06038704) and the code there
disassembles as their real prologues. **The dispatcher never selects those table entries,
and why is unexplained.** `CPORT_HOT` must not be used until it is.

**MOTION ON `hotdisp`, the candidate:** updates per 60 **25.3 -> 26.3**, MOTION **11.0 ->
12.3**, worst hold 9 -> 9, per-scene updates [36,11,29] -> [42,9,28]. Both headline numbers
move the right way this time, by about one unit each -- which is still the resolution floor,
so read it as "not a regression" rather than as the win. **The win here is master headroom
(21,581 cyc/vint), not presented frames, because the slave still carries the chain.** The
band rebalance is what would convert it, and that is the next build, not this one.

**NOT RUN: the rig launch.** The card asked for one for direction, since cart fetch costs
more on the FPGA than on ares and this change is precisely about where code runs -- so the
rig is the instrument that matters most for this candidate, and its number is missing.

**AND `check-demo` REFUSED A THIRD CANDIDATE.** `make check-demo PORT_EXTRA=PORTHOT=1` exits
with *"the gate measures the default port build; a probe is named and is not gated"*, as it
did for `PORTPLOTW`. Three candidates in a row have now gone ungated on logic. **This is a
harness limitation, not a flag-list one, and it should be fixed in the harness** -- every
candidate worth measuring is a `PORT_EXTRA` build by construction.

**THE MASTER: THREE READS, AND TWO OF THEM CHANGE THE PLAN (2026-10-12/13).** All on
`ctl.32x` (the line, build_id d0b5aee6) with its own lst. Master, f680-780: 384,017
cyc/vint, **cart-resident 267,550 = 69.7%, CPI 3.06** -- the advisor's figures reproduce.

**1. THE V-ISR SPIN IS `m_main.c:11282`, AND IT IS DELIBERATE.** `_visr_vbi` is 44,747
cyc/vint = **11.7% of the master, CPI 1.05**, and **91% of it is nine PCs at
0x02046d50-0x02046d60 executing 4,503 times a vint** -- an FRT delta wait
(`mov.b @r7 / mov.b @r3 / shll8 / extu.b / or / sub / extu.w / cmp/hi / bf`). Source:

    #ifdef R60
        } while ((uint16_t)(frt() - t0) < 1650);        /* m_main.c:11282 */

1,650 master ticks of 12,052 a vint = 13.7% of a vint, ~36 scanlines. **But the comment
beneath it says the spin is load-bearing:** *"the shim consumes the MD-plane packets AND
runs its push between announce and post (push-before-post: the push drains against this
very spin -- SDRAM-quiet -- per the LOOP25 law)"*. So the master is not merely idling: it
is staying off SDRAM so the DREQ push can drain. **Giving the master work during this
window is in direct tension with why the window exists**, and that has to be answered
before any band rebalance, not after.

**2. CACHE_C IS NOT 32 KB FREE. IT IS 6 KB.** The census says the tile cache is genuinely
dead -- `miss_n[0..3]` reads **0,0,0,0** at both 680 and 780, and `cache_tag` (0x3A800)
holds **0 of 512** codes, so `tile_pixels` is never called. But its declared
0x29000-0x31000 span is occupied by things that are not the tile cache:

| span | tenant | live? |
|---|---|---|
| 0x29000-0x2A800 | nothing (4 stray bytes in the 0x29400 block) | **FREE, 6,144 B** |
| 0x2A800-0x2C800 | **`_PORT_CORO_BASE` .. `_PORT_CORO_TOP`** -- the port's coroutine stack | LIVE |
| 0x2C800-0x2CC00 | MMIO shim aliases (`_MMIO_BASE`, `_DAT_00c4xxxx`) | absolute syms |
| 0x2CC00-0x31000 | **`.portmirror`** -- the port's work RAM (mars.ld:198) | LIVE |

A dump of the whole 32 KB reads 11,369 nonzero bytes, all of it above 0x2A800. **The hot set
does not have a 16 KB home. It has 6 KB.**

**3. THE HOT LIST, ranked, with sizes.** Master cart GAME functions (glue excluded),
190,187 cyc/vint over 144 functions:

| function | cyc/vint | CPI | bytes |
|---|---|---|---|
| `_port_object_dispatch` | 22,053 | 3.93 | 544 |
| `_FUN_000103e0` | 9,361 | **12.61** | 2,136 |
| `_FUN_0000669c` | 8,733 | 9.12 | 344 |
| `_FUN_000049ba` | 8,664 | **13.50** | 1,672 |
| `_FUN_00010824` | 7,823 | **13.33** | 1,220 |
| `_sprite_build_depth_banded` | 7,195 | 10.44 | 526 |
| `_floor_collide` | 7,147 | 8.88 | 228 |
| `_port_zoom_scale` | 6,682 | 8.86 | 148 |

**The largest prefix fitting 6,144 bytes is the first five: 5,916 bytes carrying 56,634
cyc/vint = 14.7% of the master.** At a cache-resident CPI of ~1.5 against today's ~10.5 that
is worth roughly 48,000 cyc/vint, ~12.5% of the master.

**4. THE BUILD IS NOT DONE, AND THE OBSTACLE IS THE LINKER, NOT THE SPAN.** The card assumed
`.portcode` already places sections it can select from. It does not:

- **The port is compiled WITHOUT `-ffunction-sections`** -- `defs.o` has one `.text` of
  0x318 -- so no per-function section exists to place.
- **All four `FUN_*` live in one 550 KB `port.o` with 784 text symbols**, so file-based
  selection (`EXCLUDE_FILE`) cannot isolate them either.
- And `.portcode`'s `sh_src/portfull/*.o(.text .text.*)` at mars.ld:271 is defined **before**
  `.ramtext` at :304, so it would claim any `.text.<fn>` section first whatever the later
  section asks for.

**The path that works, specified:** have `cport_prep` inject
`__attribute__((section(".porthot")))` on a named list -- the same mechanism it already uses
to swap hand files -- so the sections carry a name `.portcode`'s wildcard never matches.
Then place `*(.porthot)` inside `.ramtext` (which is already boot-copied by
`mars_start.s`, so no new copy loop) and raise the `.ramtext` cap from 0x06038000 toward
0x06039000, which is probe scrap and free on the line. `.ramtext` has 1,228 bytes free
today, so the cap has to move for anything past the first two functions, and a `PORTHOT`
build must then exclude `GENRING`/`PORT_RIG`/`ROWSHIP_PROBE`, which own 0x38000.

**AND I OPTIMISED THE SMALLER OF THE TWO PLOT LOOPS (2026-10-12).** `compose_pass` contains
**TWO** 4x-unrolled byte plot loops, not one, and the word path displaced part of the
smaller:

| ctl, f680-780 | loop top | iterations | cycles | share of `compose_pass` |
|---|---|---|---|---|
| plot loop A | 0x06032b80 | **35,110** | 1,857,330 | **41.7%** |
| plot loop B | 0x06032bf8 | 19,336 | 909,192 | **20.4%** |
| both | | | | **62.1%** |

`PORTPLOTW` replaced part of **loop B only**, and only the 27% of its groups that were
4-aligned. So it addressed 20.4% x 27% = **5.5% of compose_pass** and made that 5.9% worse:
a net of about -0.3%, which is the -1.7% measured, inside the noise. The two numbers finally
agree and the card was aimed at a fifth of the target.

Both loops have the same shape -- nibbles extracted high-to-low (`shlr8`, `shlr2/shlr2`,
`and #15`), the `(pix-1) < 14u` test, `mov.b` to `@r2 / @(1,r2) / @(2,r2) / @(3,r2)`,
`add #4,r2` -- and NEITHER carries an `sx < 320` test, so both are NC variants. **Which
source line each is has NOT been established and that is the first step of the next card**,
not a guess to act on.

**THE PER-ROW CLASS IS SMALLER THAN I SAID.** Measured on `ctl` by execution rate, the
per-row-class PCs are **11.7% of `compose_pass`**, not the 30% the earlier coarse
classification gave. Its biggest single block is 0x06031ffc-0x0603206c, 3,526 executions at
62.1 cycles each, 4.9% of `compose_pass`: the **per-sprite record decode** (`mov.w` of
`e[0]`, `e[1]`, `e[2]`, `e[3]`, `e[4]`, `e[5]`, then the `pp` extract), and it re-runs **for
every strip**, not once per sprite. That is a real structural waste but it is 5% of
`compose_pass`, not the lever.

**SO THE RANKING INSIDE `compose_pass`, measured, is: plot loop A 41.7%, plot loop B 20.4%,
per-row class 11.7%, everything else the remainder.** Any next card should start from loop A,
and it should NOT reuse the word-store mechanism, which is now measured at 1.059x.

**THE WORD PLOT IS DEAD AT ANY COVERAGE: IT IS 1.059x THE BYTE LOOP PER GROUP
(2026-10-12).** Measured, denominator-free -- each loop's iteration count comes from its own
loop-top PC, so no generation count is involved. Window f680-780, slave, interpreter:

| loop | top | iterations | cycles/4-px group | instr/group |
|---|---|---|---|---|
| ctl, byte loop | 0x06032bf8 | 19,336 | **54.40** | 46.36 |
| plotw, WORD path | 0x06032dec | 6,792 | **57.60** | 44.17 |
| plotw, byte fallback | 0x06032c20 | 18,388 | 54.66 | 45.93 |

**WORD / BYTE = 1.059. The bar was 0.60. At 100% coverage it would be 5.9% WORSE.** Finishing
P=1,2,3 would have made the rom slower, and the only reason that is not what happened is
that the number was taken first.

**WHY THE ARITHMETIC WAS WRONG, since that is the reusable part.** I predicted ~21
instructions a group against ~43 and got **44.17 against 46.36 -- a 5% instruction saving,
not 50%.** What the estimate left out, all of it visible in the listing at 0x06032dec:

- **`m >> 7` is FOUR instructions** (`shlr2 / shlr2 / shlr / shlr2`). The SH-2 has no barrel
  shift and no variable shift, so the arithmetic-mask trick that saved a second 512-byte
  table costs seven instructions to build the mask.
- **The byte-indexed `u16` table costs `extu.b`, `add r1,r1`, `mov r1,r0` per lookup** before
  `mov.w @(r0,r8),r1` -- index scaling is not free.
- Assembling the 32-bit pen word from two `u16` loads is another shift and or.
- Only the stores actually shrink, 4 to 1, and stores were never the majority of the group.

**And the CPI went the wrong way too: 1.30 for the word path against 1.17 for the byte
loop** -- the table loads miss where the byte loop's register work does not. So the word path
loses on both terms.

**DROPPED. `PORTPLOTW` stays in the tree, flagged, off the line, with these numbers**, so
that nobody rebuilds it from the same arithmetic. `PORTPLOTWV` stays with it: the on-rom
verifier (0 disagreements over 676,144 pixels) is the identity instrument for any future
cadence-shifting change and is worth more than the path it was built to check.

**THE WORD PLOT: BIT-EXACT AND NOT YET A WIN (2026-10-12).** `PORTPLOTW=1`, roms
`ctl.32x` / `plotw.32x` / `plotwv.32x`, build_id **d0b5aee6** (verify rom **265d3b70**).

**IDENTITY: PASSED, AND NOT BY SCREENSHOT.** The screenshot route does not work here and the
measurement says why: the candidate runs the game FASTER -- tick 0x00CE at frame 677 against
the control's 680 -- so at a matched game tick the two show DIFFERENT generations. ctl f680
against plotw f677 differs in 14,776 pixels, of which **45.0% fall in band 0, which carries
ZERO sprites in demo A at all six sampled frames**, and only 13.4% in band 2, which carries
the most. That is the inverse of a plot bug. For scale, one frame of natural motion on the
control alone is 10,188 pixels (f680 to f681), and ares is deterministic (same rom, same
frame, 0).

So the proof was moved onto the rom. `PORTPLOTWV=1` recomputes, for every longword the word
path writes, the four bytes the byte loop would have written, and counts disagreements:

    1200 frames: 169,036 longwords = 676,144 pixels written, 0 disagreements. BIT-EXACT.

**SPEED: NO MEASURABLE WIN.**

| | control | plotw | |
|---|---|---|---|
| updates per 60 | 25.3 | **24.3** | -1.0 |
| MOTION per 60 | 11.0 | **12.3** | +1.3 |
| worst hold | 9 | 9 | -- |
| `compose_pass` cycles per vint | 44,915 | 44,144 | **-1.7%** |

The two presented-frame numbers move in OPPOSITE directions by about one unit, which is the
resolution floor this project already has a lesson about (64 bytes of dead `.data` moved the
level-1 ladder 18 points). -1.7% of compose is the only number with a consistent sign, and
it is roughly what the coverage predicts.

**WHY SO SMALL: ONLY P=0 IS IMPLEMENTED.** The destination advances a byte per pixel and the
source a nibble, so a sprite whose `(xpos - 184) % 4` is not 0 straddles every longword.
Measured over six frames of demo A: **P=0 is 37.0% of this path's row-pixels, P=1 21.8%,
P=2 19.5%, P=3 21.8%.** The other three need a 2-word window shifted by `4*(4-P)` -- a
constant per sprite but a different constant per case, so three more loop bodies -- and the
run's `pen 15 ends it` test then lands mid-group and needs a per-pixel tail. That is the
remaining 63% and it is where the win is.

**AND THE CHAIN NUMBER IS NOT AVAILABLE FOR THIS CANDIDATE.** `PORTPLOTW` plus
`PHASECENSUS`+`GENRING` **overflows `.ramtext`** (`ld: .ramtext overflows its
0x31000-0x38000 SDRAM slot`). The line has 1,228 bytes free, the word path takes 344, the
instrumentation needs ~1,136. So band 2 lines per generation could not be read on the
candidate, and `NAT_WALL[1]` is not a usable substitute: `PSRC` aliases 0x28F50 when
`PHASECENSUS` is off, and it read 2 chains per 100 vints, which is false.

**check-demo COULD NOT BE RUN ON THE CANDIDATE, by the gate's own design.** `make
check-demo PORT_EXTRA=PORTPLOTW=1` refuses: *"the gate measures the default port build; a
probe is named and is not gated"*, and `check_demo.py` refuses a named rom directly for the
same reason. Gating it would mean promoting `PORTPLOTW` into `PORT_FLAGS`, which is a line
change, and the verdict is that it does not belong on the line. Logic risk is low on its
face -- the path writes the same bytes to the same addresses, proven -- but the gate has
not run and this row should not be read as if it had.

**VERDICT: EXACT, KEPT ON THE FLAG, NOT ON THE LINE.** It is not the card's PARTIAL (which
wanted 250-353 with identity) because the chain number could not be taken; on the metrics
that could, it is a no-op. The mechanism is now PROVEN exact over 676k pixels, which is the
expensive half of the work, and the three straddling cases are the cheap half in risk terms
and the whole of the payoff.

**THE DENOMINATOR RULE, and the 2.3x closed (2026-10-12).** `--profile` forces the
interpreter, **and the interpreter does not launch the same number of generations as the
recompiler.** Verified here by dumping the ring's launch counter inside the profile runs
themselves: over f680-780 the interpreter launched **14** chains (354 to 368) where the
recompiler launched **50** (418 to 468). Every per-generation figure I derived from the
profile was divided by 50 and should have been divided by 14.

**RULE: a per-generation figure from `--profile` takes its generation count from the SAME
run. Dump `GR[448]` in the profile run. The two cores do not launch alike.**

Re-divided, and the two instruments agree:

| | profile (interpreter, 14 chains) | slave FRT |
|---|---|---|
| `compose_pass` | 220.4 lines/chain | -- |
| `slave_concurrent_k` | 136.0 lines/chain | -- |
| sum | **356.5** | ring chain **350-357**, STB **353.4** |
| `GP[0]` compose_sprites | -- | **267.2 lines/chain** |

**THE CONVERSION CONTROL, one function three ways** (`GP[3]`, added for this): `blit_half`
reads **17.37 lines** on the slave FRT against **16.51 lines** of profile cycles --
**agreement 1.052**, inside the 5% bar, with no ablation needed. The `PLOTOFF` ablation was
built (`noplot.32x`) and is no longer required to settle this; it stays in the tree as
`make ... PLOTOFF=1`.

**SO: "sprites are ~76% of the chain" IS UN-SUSPENDED**, at 267.2 of ~353 lines, and the
per-row-pixel figure is **16.5 cycles / 14.5 instructions inside `compose_pass`**, 20.0
cycles by the FRT bracket.

**THE LOOP, EXACTLY -- AND IT IS NOT WHAT THE CARD ASSUMED.** Hot block at
`0x06032c64-0x06032cc2` in gr4, 2,507 iterations a chain, **four pixels per iteration**:

    mov r1,r0 / shlr2 / shlr2 / and #15,r0      extract the nibble
    mov r0,r8 / add #-1,r8 / cmp/hi r7,r8       (pix-1) < 14u, r7 = 13
    bt.s <skip> / add r4,r0                     base + pix
    mov.b r0,@(2,r2)                            the store
    ... x4, then  add #1,r6 / bra / add #4,r2

**THERE IS NO `c1_cell` CALL IN IT.** `punch` is false on this path, so hoisting the call
out of the pixel -- half the card's proposed shape -- has nothing to hoist. The other half
stands and is visible in the listing: **four separate `mov.b` stores per 4-pixel iteration,
with `r2` advancing by 4.** About 10 instructions per pixel, ~43 per iteration.

**THE CLASS SPLIT (item 2): 100% PLAIN.** Six frame points 680-780, `SPR_SNAP`: **no shadow
records (cset 0x3F) and no gated records (pp <= 1) in demo A at any point.** Band 1 is 11.3
sprites / 204.5 rows / 7,869 row-pixels, band 2 is 11.5 / 315.5 / **12,031**, all plain. A
plain-sprite fast path would cover the whole of demo A, not a fraction.

**WHERE THE CHAIN GOES, by execution rate** (per chain, both compose symbols):

| class | instr/chain | cycles | lines | share |
|---|---|---|---|---|
| the plot inner loop (>=1000 execs) | 261,470 | 295,216 | **200.6** | 57% |
| per sprite row (300-1000) | 129,725 | 153,412 | 104.2 | 30% |
| per strip / per sprite (20-300) | 56,553 | 61,594 | 41.8 | 12% |

**THE CANDIDATE, specified, NOT BUILT.** Replace the 4x byte-store unroll with two 256-entry
tables (hi byte, lo byte) giving two pen bytes and a transparency mask each, assembled into
one 32-bit pen word and one 32-bit mask, ORed with `base * 0x01010101`, applied as
`mov.l @r2,r3 / and / or / mov.l r3,@r2`. ~21 instructions per 4 pixels against ~43, on 57%
of the compose. Run still ends on pen 15; pen 0 and pen 15 both leave the mask clear, which
is what makes it bit-exact.

**THE RISK THAT MUST BE SOLVED FIRST, and it is why this was not rushed:** `r2` advances 4 at
a time but starts at `row + (xpos - 184)`, which is arbitrary, so `mov.l` would fault on the
SH-2 for three sprites in four. The path needs a head peel of 0-3 pixels per row to reach
4-byte alignment and a tail peel, both on today's byte store. That peel is the actual work
and it is where a rushed version loses pixel identity -- and the gate makes "faster but not
pixel-identical" a NEGATIVE.

**THE 2.3x IS NOT THE COLUMN AND NOT THE CONVERSION. BOTH ARE NOW ELIMINATED (2026-10-12).**
Reads only, no build.

**1. THE COLUMN WAS CYCLES.** Slave, f680 to f780: cycle column sums to **384,017 per vint**
against the budget `23.01 MHz / 59.92 Hz = 384,012` -- it is the whole budget by
construction, which is the check that identifies it. The instruction column sums to 369,600
per vint. **Overall CPI 1.039**; over `compose_pass` 1.134, `slave_concurrent_k` 1.173,
`_s_main` 1.001. (`blit_half` is the one place ares charges more: CPI 2.207.) A
cycles-per-instruction effect can account for at most **1.17x** over the compose range, not
2.3x, and the figure in question already came from the cycle column.

**2. THE FRT-TO-CYCLES CONVERSION IS VERIFIED, by a control that needs no build.** The pace
loop at s_main.c:209-210 is `while ((uint16_t)(frt_s() - tq) < 16);` -- a CONSTANT 16-tick
bound, so it converts ares's FRT into ares's cycles directly. Over the window: **200,754
pace visits, 2,950,726 iterations, 26,556,773 cycles in the 9-instruction body**.

| | |
|---|---|
| cycles per pace visit | **132.3** |
| expected if FRT = phi/8 (16 ticks x 8) | **128.0** |
| residual | +4.3 cycles, which is the loop's own overshoot granularity (half of its 9-cycle body) |
| implied FRT ticks per vint | 46,447 against phi/8's 48,002, **ratio 0.968** |

**So phi/8 is right and 184 ticks per line is right.** Every "lines" figure in this document
stands as a unit. The conversion is not the fault.

**3. WHAT THE CONTROL DID SETTLE, as a new hard fact.** 200,754 visits over 100 vints is
2,008 per vint at 16 ticks = 32,120 of 48,002 ticks, so **the slave is in the pace loop 67%
of wall time**. The chain is 357 of the 529-line generation period, **67.5% of wall time**.
Those sum to 135%, so **the pace loop runs INSIDE the chain -- about 229 of its 357 lines.**
Chain 357 = compose code 112 + pace ~229 reconciles to within rounding.

**4. WHAT STILL DOES NOT RECONCILE.** `GP[0]` brackets `compose_sprites` and reads 259.6
lines a chain. `compose_sprites` (m_main.c:6992-7960) contains **no** `slave_service_stream`
call and no park, and the service-stream calls in the strip loop are bracketed separately as
`GP[1]` at 0.7 lines. So the pace loop should be outside `GP[0]`, and `GP[0]` should read
near the profile's 69-112 lines. It reads 259.6. **That specific gap is unexplained.**

Note for whoever picks this up: `compose_sprites` has EIGHT call sites (m_main.c:8405, 8408,
11811, 11814, 14404, 14407, 14620, 14655). Only the strip-loop pair at :11811/:11814 is
GP-bracketed. Several of the others are master-side (`nat_par`). The master's profile shows
no `compose_pass` cycles in this window, so the master is not running it here -- but the
call-site count is the first thing to rule out properly, and it was not.

**VERDICT: they do not reconcile. Stop, per the card's item 4.** The suspension stands:
"sprites are 74% of the chain" and every per-pass figure derived from it remain suspended.
The candidate build is not authorised by anything measured so far.

**STOP: TWO INSTRUMENTS DISAGREE 2.3x ABOUT THE SPRITE PLOT, SO THE CANDIDATE HAS NO
PREMISE (2026-10-12).** The optimisation build was NOT made. The one authorised build was
spent on `GP` (rom `gr3.32x`, build_id **6cc8a73d**), a probe that tested whether `STB[1]`
was contaminated. It is not -- and that left a contradiction that has to close first.

**WHAT WAS TESTED AND DISPROVED.** `st_s(11)` is written at m_main.c:11804 AND again at
:11823, so the interval between them contains `slave_service_stream()` at :11805. The FM
park inside that stream (s_main.c:196-211) is inlined into `_s_main`'s address range, which
looked like the explanation for the profile and STB disagreeing. `GP` brackets
`compose_sprites` alone, the service stream alone, and cat1+text alone:

| bracket | ticks/chain | lines/chain |
|---|---|---|
| `GP[0]` compose_sprites only | 47,769 | **259.6** |
| `GP[1]` slave_service_stream only | 132 | 0.7 |
| `GP[2]` cat1 + text | 0 | 0 (never reached) |
| `STB[1]` as reported before | 47,714 | 259.3 |

**`GP[0]` and `STB[1]` agree to 0.1%. STB was never contaminated and my contamination
hypothesis was wrong.** The service stream inside the strip loop costs 0.7 lines a chain.

**THE CONTRADICTION.** ares `--profile`, both reads differenced f680 to f780, symbolised
against the rom's OWN lst (the first attempt symbolised genring2's profile against gr3's
listing -- invalid, and re-run; the picture did not change):

| slave, demo A | cycles | share | lines/gen |
|---|---|---|---|
| `_s_main` (a 9-instruction FRT spin at 0x06037e14-24) | 29,491,416 | **76.8%** | 400.7 |
| `_compose_pass` | 5,080,028 | 13.2% | 69.0 |
| `_slave_concurrent_k` | 3,182,572 | 8.3% | 43.2 |
| `_blit_half` | 457,426 | 1.2% | 6.2 |

Every cycle is accounted; nothing is missing. **The profile says ALL compose code on the
slave is 112 lines per generation. `GP`, on the slave's own FRT, says `compose_sprites`
ALONE is 259.6.** A factor of 2.3, and the two cannot both be true: 259.6 lines of compose
plus 400.7 of spin exceeds the 529-line generation period.

**WHAT HAS BEEN RULED OUT.** Not symbolisation (re-run against the matching lst). Not the
master running compose (the master's unattributed 67.7% is all `0x0204xxxx`, the port's game
code out of cart; the slave's is 0.4%). Not the service stream (0.7 lines). Not FRT wrap (a
strip is ~2,500 ticks of a 65,536 range). The slave FRT rate of 48,208/vint is confirmed
independently by s_main.c:283 and by the fence's 24,000-tick bound reading as 0.498 vints.

**SO THE LEVER IS UNDECIDED.** If the profile is right, the sprite plot is 21% of the period
and the FM spin is 77%, and the candidate attacks the wrong thing entirely. If `GP`/`STB`
are right, the sprite plot is ~49% and the candidate is well aimed. **Everything reported on
2026-10-12 about the pass split rests on the FRT-derived numbers, so that whole report is
suspended until this closes**, including "sprites are 74% of the chain".

**THE READ THAT CLOSES IT, no build:** take `--profile` and the `GP` brackets from the SAME
run and compare them on one known span -- bracket a span whose cycle count the profile can
also give exactly (`blit_half` is a good candidate: it has its own symbol, 6.2 lines/gen by
the profile, and can be `GP`-bracketed). If the two disagree on `blit_half` by the same 2.3x,
the fault is in the FRT-to-cycles conversion and every "lines" figure in this document needs
rescaling. If they agree on `blit_half`, the fault is specific to `compose_sprites` and the
profile is mis-attributing it.

**THE BLIT IS NOT THE REASON FOR TWO WINDOWS: IT FITS IN ONE VINT WITH ~93% TO SPARE
(2026-10-12).** `blit_half` (lst 0x06032F6C-0x060331EC) cycles from the ares profile on the
line:

| window | master | slave | worst CPU DOUBLED (112 rows in one window) |
|---|---|---|---|
| demo A 680-780, near-static | 756/vint (0.2%) | 1332/vint (0.3%) | 0.7% of a vint |
| **across the CUT 455-500, busy** | **13,010/vint (3.39%)** | 12,564/vint (3.27%) | **6.78% of a vint** |

Against the vint's 384,012 SH-2 cycles. Even at a scene cut, doing BOTH halves in one window
costs under 7% of the vint by this measure.

CAVEAT, and it is the one the record already carries: **ares charges instruction cycles only**
-- no SDRAM or uncached waits -- and the rig blit split is stores 35 / instructions 35 /
loads 20 / audit 12, so better than half the real blit is memory traffic ares does not charge.
Scale the 6.78% by that and it is still about 20% of a vint. It fits either way, and the
margin is large enough that the conclusion does not turn on the scaling factor.

**SO WIN_TWO'S TWO-WINDOW SPLIT IS NOT PAID FOR BY BLIT COST ON THE PORT BUILD.** The record
already says the reason was the 68K transport (2026-09-27 supersession) and the port deleted
that. `Makefile:3380` states the consequence outright: "60Hz when a generation closes in a
vint; whole-frame-coherent 30Hz when it does not" -- and 30 updates per 60 IS that 30Hz,
measured. The arcade's 59 is one window per frame.

WHAT I DID NOT MEASURE, and it is item 1: the per-generation event timeline (launch, chain
finish, tail, k1, k2, flip). `PORT_HIST=1` does not link on the line (.ramtext overflow,
card 4b), and the TOK/RIGTASK marks do not carry vint stamps I can read without a build. What
IS established: a generation spans 2.2 vints (100 bodies per 100 vints, 46 flips per 100),
and the blit is 3.4% of a vint, so the second vint is NOT blit cost. By elimination it is the
k1/k2 structure itself, which is what the record says it is -- but by ELIMINATION, not by a
timeline.

68K SIDE, partial: `md_main.c:490` says the packet consume is "called post-window as always,
and ALSO pre-window on k2 vints under FM_GATE". One window per vint keeps the consume rate at
one per vint, which is what it already is; the k2 pre-window call becomes moot rather than
broken. I did NOT walk every 68K dependency and will not claim "nothing breaks".

**WE ARE ALREADY AT PARITY ON MOTION. THE GAP IS updates/60, 30 AGAINST 59 (2026-10-12).**
The control nobody had taken: the ARCADE's own numbers, same rule, demo A frames 700-759,
with its scroll near-frozen (0xFFF0E2 goes 0187 -> 0183 over 59 frames) exactly as ours is:

| | arcade | the line (marks2) | rotation attempt 3 |
|---|---|---|---|
| MOTION (>5%) | **8 of 59** | 8 of 60 | 7 of 60 |
| updates (>0.25%) | **59 of 59** | 29 of 60 | 30 of 60 |

**THE ARCADE SCORES MOTION 8 TOO.** The large-change column is AT PARITY and has been all
along. What the arcade does that we do not is change SOMETHING every single frame -- 59 of 59
against our 30 -- with the scroll nearly still, so that is sprite and animation detail.

**THIS CORRECTS THE METRIC CALL AGAIN, AND AGAINST MY OWN CORRECTION.** I demoted MOTION,
then Mike's "about 7" matched MOTION 8 and I promoted it back and wrote the rule. Both moves
were wrong in the same way: MOTION matches his WORDS but it is a column where we already
equal the arcade, so it cannot be the defect. **updates/60 is the gap and always was.** The
rule should read: rank on flips and updates/60 against the ARCADE's own numbers at a matched
window; MOTION is a parity check, not a target.

ROTATION ATTEMPT 3 IS NEUTRAL AND STAYS OFF. Per-rotation cap with the walked chunk ROTATING
(`md_rot & 3`) plus the mirror re-stamp restores plane B to **74 blank cells, the line's
number**, with plane A 0 -- but every eye column is flat: MOTION 7 against 8, updates 30
against 29, hold 3 against 3, flips 46 against 46. Doubling plane A's walk rate changes
nothing a person sees, because the FG's change rate is set by the game's content and walking
more often only splits the same work into smaller pieces. The 9-vint revisit was never the
binding constraint; 60/9 = 6.67 matching Mike's "about 7" was a coincidence of two numbers
near 7.

**WHY THE ROTATION WAS WORSE: PLANE B'S CELLS GO BLANK, NOT PLANE A'S THRASH (2026-10-12).**
Both candidate mechanisms are wrong, and the measurement is one number:

| at emu 689, absolute | plane B blank cells | plane A blank cells |
|---|---|---|
| marks2 (the line) | **74** of 1120 | 0 |
| PORTROTATE=1 | **271** of 1120 | 0 |

**A QUARTER OF THE BACKGROUND RESOLVES TO THE BLANK SLOT** when plane B is never walked. That
is the picture getting worse, and it is far more than the "30 cells of scroll" I used to argue
plane B could not matter -- I was counting what B SHIPS, not what B LOSES.

AND IT IS NOT PLANE A THRASHING EITHER: plane A has **0 blank cells on both roms**, both roms
emit the **same 8 NT chunks** over the nine frames, and claims went DOWN with the extra walks
(15 to 9), not up. The extra plane-A walks are clean; they simply find nothing left to do,
which is why the mirror diff read 0 cells re-shipped.

THE MECHANISM, then: a cell's slot stays claimed because its plane keeps being WALKED. Starve
plane B and plane A's claims evict B's slots underneath it, so B's cells resolve to
`MD_BLANK_SLOT`. The walk is residency maintenance, not just a shipping opportunity.

**LETTER (b), with the cause corrected: the per-rotation cap is the next build.** Capping the
skip at N of the four B phases keeps B walked every rotation, so its slots stay claimed, and
N becomes the dial. This does NOT block behind card 0 -- card 0's ~500 never-converging cells
are plane A's, and plane A is measurably clean here.

COUNTER WARNING, filed: `DIAG[50]` and `DIAG[53]` are in the documented collision block
(`m_main.c` ~184-195, "each written by three subsystems") and CANNOT be read as
evictions/claims. The 1945 and 1944 they returned over nine frames are not evictions. I used
them because the card named them; the blank-cell count is what actually answered this.

**NEGATIVE: ADAPTIVE ROTATION MEASURED WORSE, AND THE BACKSTOP IS WHY (2026-10-12).**
Built (`c9d3b837`), measured, defaulted OFF. At the matched demo-A tick 00D6/00D7:

| | marks2 (the line) | PORTROTATE=1 |
|---|---|---|
| MOTION | 8/60 | **6/60** |
| updates per 60 | 29/60 | **26/60** |
| longest hold | 3 | **5** |

| skip-rate row, emu 680-780 (100 vints) | |
|---|---|
| plane-B skips | **45** |
| real plane-B walks | **0** |
| backstop walks | 0 |
| cells re-shipped, 9 frames | **0** of 2240 |

**CAUSE, named: the backstop is counted in ROTATIONS and a rotation still advances while B is
skipped.** 16 rotations is about 144 vints, so in any 100-vint window it cannot fire -- plane B
was skipped 45 times and walked ZERO times, i.e. starved outright rather than deprioritised.
The BG's name table stops being maintained and the picture gets worse, which is what the three
columns say.

The idea is NOT disproved: the transport census that motivated it still stands (7 tiles in 9
frames, nothing waiting). What is disproved is this bound. The next attempt must cap the skip
PER ROTATION -- at most N of the four B phases -- so plane B is deprioritised and never
starved, and the cap is then a number to tune rather than a cliff.

DEFAULTED OFF AND THE LINE IS PROVABLY UNTOUCHED: `walk_phase` is `#define`d to `md_phase`
when the flag is off, so the default rom differs from `rom/port/marks2.32x` in **16 bytes, all
build stamp**. `PORTROTATE=1` opts in.

**THE ROTATION IS THE WHOLE OF MIKE'S 7 FPS, CONFIRMED FROM md_phase (2026-10-12).**
`DIAG[34]` carries `md_phase | md_forced<<8` (`m_main.c:18206`). marks2, demo A:

| frame | 680 | 681 | 682 | 683 | 684 | 685 | 686 | 687 | 688 | 689 |
|---|---|---|---|---|---|---|---|---|---|---|
| md_phase | 5 | 6 | 7 | 8 | **0** | 1 | 2 | 3 | 4 | 5 |
| NT cells shipped | - | 87 | 142 | 160 | 118 | 30 | 0 | 0 | 0 | 0 |

**A NINE-PHASE ROTATION**: 5-8 plane A, 0 tiles, 1-4 plane B (`m_main.c:18313`). The cell
counts lag one frame because the mirror diff at N shows the work of N-1.

**"FOUR IDLE FRAMES" IS WITHDRAWN.** Those four bodies ran plane B's phases 2-4 and shipped
nothing because the BG is hardware-scrolled and had nothing to ship. The advisor's reading is
right and the arithmetic is exact: **a plane-A band is revisited once per 9 vints, so the FG
refreshes at 60/9 = 6.67 Hz, which is Mike's "about 7" with nothing left over.**

**THE TRANSPORT IS NOT THE CEILING, AND IT IS NOT CLOSE.** `slim_diag` (MD 0xFF3CCE, read
straight out of `build/port/rom/md_start.elf` with `m68k-elf-nm` -- no build needed) over the
same nine frames: **7 tiles fetched, 2 fetch calls, 0 stray records, 0 DMA waits, 0 inline
tiles.** The packet is nearly empty every body. Nothing here argues with the 512-words-per-vint
budget; there is enormous headroom.

CONTEXT FOR THIS WINDOW: hscroll A and B are FROZEN at 392 for all nine frames, so demo A is
not scrolling here. What moves is sprites and FG tiles, which is why the FG's 6.7 Hz is what
Mike perceives as the frame rate.

**THE CANDIDATE, WRITTEN NOT BUILT -- ADAPTIVE ROTATION.** A plane-B chunk whose pages carry no
marks and whose coarse scroll has not moved is SKIPPED, and the next plane-A band is walked
instead, continuing while the packet has room. By the measured numbers plane A would go from
4 of 9 phases to 4 of 5, taking the FG revisit period from 9 vints to 5 -- **6.7 Hz to 12 Hz**
-- and the transport census says the room exists.
GATE: FG band revisit period measured from `md_phase`, 9 vints down to the new period; flips
per 100, updates per 60, longest hold at matched (step, tick); MOTION reported as the number
Mike will see; check-demo, census and parity unchanged; packet overflow 0 (`slim_diag` stray
and DMA-wait stay 0); rom built AFTER the commit.

**BAND COVERAGE, MEASURED: ONE BAND PER FRAME AND FOUR IDLE FRAMES IN NINE (2026-10-12).**
marks2, demo A f680-689, cells RE-SHIPPED per frame read from the `md_dbg_nt` mirror diff
(the mirror records the last entry shipped per cell, so a changed entry IS a re-ship):

| frame | plane B cells | plane A cells | band (rows/7) |
|---|---|---|---|
| 681 | 0 | 87 | 1 |
| 682 | 0 | 142 | 2 |
| 683 | 0 | 160 | 3 |
| 684 | 0 | 118 | 3 |
| 685 | **30** | 0 | 0 |
| 686-689 | **0** | **0** | **none** |

**A GENERATION TOUCHES ONE BAND, THE BANDS ROTATE, AND THEN THE COMPOSE GOES IDLE FOR FOUR
FRAMES.** 537 cells of the screen's 2240 are re-shipped across nine frames -- 24% -- which is
**1.6 full-screen refreshes per second**, against 46 flips per second. That is the whole of
Mike's 8/sec: we flip 46 times over a screen that is being rebuilt 1.6 times.

PLANE B IS BARELY RE-SHIPPED AT ALL -- 30 cells in nine frames against plane A's 507. That is
correct for a scrolling BG (the hardware scroll registers move it and only newly exposed
columns need cells), and it means the cost is concentrated in the FG.

**THE BODY IS PINNED TO THE VINT BY CONSTRUCTION**, which settles the other half. `pl_due` is
set in the V-ISR (`m_main.c:10682`) and `pp_due` is "one game frame per master vint, cap 1"
(`:10668`), dropped rather than caught up (`:13861`). The loop top consumes them at `:13790`
and `:13860`. So 100 bodies per 100 vints is BY DESIGN, the 23,409 idle spins are the loop
waiting for the next vint, and a generation of 2.2 bodies is 2.2 VINTS because of that gate --
not because a body is expensive. **The only lever is how much one body carries**, and today a
body carries one band and often nothing.

NOT MEASURED: the packet fill per body (item 3). `slim_diag` is a `static` in `md_src/md_main.c`
and there is no MD symbol map in the tree, so its address cannot be had without a build.

**THE GENERATION ARITHMETIC ON THE LINE, AND THE LEVER HAS MOVED (2026-10-12).**

`PORT_HIST=1` NO LONGER BUILDS on the line: `.ramtext overflows its 0x31000-0x38000 SDRAM
slot`. The section split is therefore NOT available for marks2 and the two-rom phase table the
card asked for cannot be produced without finding room in `.ramtext`. `PORTHIST=2` (the vint
histogram alone) does build, and gives the arithmetic:

| | clean1 | **marks2** |
|---|---|---|
| real bodies per 100 vints (1-vint + 2-vint passes) | 94 | **100** |
| flips per 100 vints | 12 | **46** |
| **bodies per generation** | **7.8** | **2.2** |
| idle 0-vint spins per 100 vints | 154 | **23,409** |

The hash removal cut the generation from 7.8 bodies to 2.2 -- that is where the flip rate came
from. It also turned the master loop into a fast spinner when there is nothing to do: 234
iterations per vint against clean1's 1.5, because an iteration that finds no work no longer
costs a 13-page hash.

**AND THE LEVER IS NO LONGER THE FLIP RATE.** In demo A the line already flips **46 times a
second**, of which **29 change the picture at all** and only **8 change more than 5%** -- which
is Mike's "about 7". The arcade changes substantially every frame, 60 a second. So we are
presenting often and each presented frame carries LITTLE NEW: the composite is partial, which
is what the banded R0-R3 generation is built to do. Raising flips further cannot raise what
Mike sees; a flip that carries a tenth of a frame reads as a tenth of a frame.

**THE NEXT QUESTION IS THEREFORE NOT "why so few flips" BUT "why does a flip carry so little
change"** -- how much of the screen one generation actually refreshes, and whether the band
rotation means any given band is refreshed once every N generations. That is a different
measurement from anything taken so far, and it is where demo A's 8/sec lives.

**THE CHOP, MEASURED ON THE LINE (2026-10-11, after Mike's pass).** `presented_fps`, windows
labelled from the game, clean1 against penfix1:

| window | step/key | clean1 MOTION | clean1 hold | penfix1 MOTION | penfix1 hold |
|---|---|---|---|---|---|
| f100 | 08 / 0C47 | 0/60 | 7 | 0/60 | 8 |
| f300 | 08 / 0C47 | 4/60 | **32** | 4/60 | **33** |
| f700 | 0C / 0947 | **9/60** | 8 | 8/60 | 7 |
| f1300 | 10 / 0C47 | 2/60 | 9 | 3/60 | 9 |
| f1800 | 14 / 0947 | 12/60 | 6 | 14/60 | 6 |

MOTION 5.4 fps against 5.8; worst hold 32 against 33. **clean1 did NOT move the chop** -- it is
within noise of penfix1, so the chop predates both roms and is not something this week created.
The moving scenes are f700 and f1800: **9 and 12 substantial updates per 60 frames against the
arcade's 60.** That is what Mike saw.

**PHASE SPLIT OF THE WORST MOVING WINDOW** (demo A, f700, step 0C key 0947), PORT_HIST=1 on the
line's code, emu 700-800, 100 vints, regions+ISR summing to 100.0% of `pp_pass_cyc`:

| | phase | share |
|---|---|---|
| 16 | TAIL: after the game frame, through the 7-row walk, to the loop top | **54.9%** |
| 17 | loop top .. the game frame (this IS the game tick) | 21.6% |
| 13 | ISR | 14.1% |
| 21 | tile payload, art emit from cart | 8.7% |
| 20 | launch decision .. tile payload | 0.7% |

Nested inside those: **page hash 25.7%**, port tick 21.3%, nat_ph_flip 21.6%, flip_span 2.8%,
`apply_cram` 0.8%, every WAIT together 0.3%.

**THE 10-10 RECORD'S SUSPECT IS DEAD AGAIN, ON THE LINE: `WAIT flip_span SYNC` is 0.0%** -- 82
FRT ticks over 17 calls. The "40% of the frame" in that record was a mislabelled region, struck
2026-10-11 and struck again here. Do not reach for it a third time.

VINTS PER PASS: 88 one-vint, 6 two-vint, 154 zero-vint = **94 frame bodies per 100 vints**. The
master finishes a body 94 times and asks for a flip about 20 times. The body is not the problem.

**THE LEVER IS THE PAGE HASH, 25.7%, AND IT IS NOW SAFE TO DELETE.** The earlier card required
`pp_miss_never` = 0 before the hash could be replaced by `port_tile_dirty`; the clear_tileram
mark got the census to 0/0/0 across the whole attract on a clean build. That precondition is
met for the first time.

**THE PASS, BISECTED, 2026-10-11. The lever is the PAGE HASH at 26.5%.** PORT_HIST=1 on
penfix1's code, demo B frames 1800-1900, 100 vints, `pp_pass_cyc` 1,203,023 FRT ticks.

HOW THE NESTING WAS REMOVED: the `PHMARK` chain is disjoint BY CONSTRUCTION -- each mark
charges the interval since the previous mark with the ISR accumulator subtracted -- so only the
chained regions are top level, and the `PHSEC` brackets are nested inside them and are NOT
added. Regions 85.5% + ISR 14.5% = **100.0%** of the pass; that is the check that the split is
real. Before this, THREE marks shared a bucket with a bracket (`PHMARK(2)` with
`PHSEC(2, fbx_lift)`, `PHMARK(4)` with `PHSEC(4, cap_drain)`, `PHMARK(8)` with `PHWAIT8`), so
indices 2, 4 and 8 in the earlier table were mixtures too, not just 10. They now charge 17, 18
and 19.

| region | | ticks | share of pass | calls/100v |
|---|---|---|---|---|
| 16 | after the game frame, through the 7-row walk, to the loop top | 689,815 | **57.3%** | 730 |
| 17 | loop top .. the game frame | 215,607 | 17.9% | 97 |
| 21 | the tile payload, art emit from cart | 115,581 | 9.6% | 89 |
| 20 | launch decision .. tile payload | 8,145 | 0.7% | 11 |
| 18, 19 | never fire -- that path is not taken | 0 | 0.0% | 0 |
| 13 | ISR (charged separately, subtracted from every region) | 173,873 | 14.5% | 100 |

Nested inside those, the named brackets: **page hash 319,134 = 26.5%** (98 calls, once per
body), port tick 17.6%, flip_span 3.2%, apply_cram 1.0%, nat_window_launch 0.7%, every WAIT
together 0.6%.

Region 17 IS the game tick (17.9% against the tick bracket's 17.6%) and is excluded as a lever.
Region 16 is broad because marks 18 and 19 never fire, so it carries the page hash, the walk and
the loop wrap together.

**THE LEVER: the page hash, 26.5% of the pass, once per frame body, and it is not the game
tick.** It is the renderer hashing all 13 tilemap pages every game frame to find out which
moved. `PORTTILEDIRTY=1` already exists and already does the other thing -- the port names the
page at the store site (`Makefile:4407`, `w68.h`'s A68VID/A68VIDA set the bit) -- and it is NOT
in the port build's flags today.

VINTS PER PASS over the same 100 vints: 630 passes, 537 of them 0-vint (85.2%), 86 one-vint
(13.7%), 7 two-vint (1.1%). So ~93 frame bodies per 100 vints against 18-21 flips per 100
frames: the master FINISHES a body four to five times more often than it asks for a flip, which
says the flip is gated on the composite completing across bands, not on body completion.

**"flip_span SYNC is 40% of the frame" IS WRONG, 2026-10-11. It is 0.0%.** PORT_HIST=1 on
penfix1's code, demo B frames 1800-1900, 100 vints:

| # | phase | FRT ticks | share of sum | calls |
|---|---|---|---|---|
| 16 | **TAIL REGION (unlabelled)** | 812,887 | **40.1%** | 536 |
| 1 | page hash | 302,645 | 14.9% | 93 |
| 2 | packet lift | 222,349 | 11.0% | 200 |
| 0 | pass region 0 | 214,659 | 10.6% | 93 |
| 6 | port tick | 212,511 | 10.5% | 93 |
| 13 | ISR | 177,953 | 8.8% | 100 |
| 9 | WAIT slave pickup/done | 4,559 | 0.2% | 38 |
| 7 | WAIT 68K COMM0 post | 453 | 0.0% | 73 |
| 10 | **WAIT flip_span SYNC** | **54** | **0.0%** | 20 |

Section 10 was charged at FOUR sites: three `PHSEC(10, while (SYNC[...]))` spins AND
`PHMARK(10)` at `:13761`, which closes the previous pass's whole TAIL REGION. A region was
being summed into a wait. The mark now charges index 16 (`pp_cur`/`pp_sec_cyc`/`pp_sec_n`
widened to 20) and the two separate cleanly: the region is the 40%, the wait is 54 ticks.

**THE MASTER WAITS FOR NOTHING.** All three WAIT buckets together are 0.2% of accounted time --
not the 68K's ack, not vblank, not the slave. The flip rate is not a wait; FS writes equal
flips exactly (37/37 at 500-700, 42/42 at 1700-1900), so nothing is deferred or lost either.
The master simply only ASKS for ~20 flips per 100 frames.

NO BUILD SPENT ON A FIX. The largest phase is unlabelled time, and building against a
catch-all is building blind. Next read, not a fix: bisect index 16 with more `PHMARK`s.
Sections sum to 1.68x `pp_pass_cyc`, so shares are of the SUM; the nesting is not disentangled.

**THE LOGO STEP'S 33-FRAME HOLD IS FAITHFUL, 2026-10-11. Not a defect.** Measured both sides.

THE 68K IS NOT STALLED. The game's countdown at 0xFFF02A decrements by exactly one per
emulated frame across port frames 300-360 (009C down to 0060, step 08 key 0C47 throughout) --
full 60 Hz logic. The picture changed at frames 310, 316, 322 and 328 and then not again
through 360.

THE ARCADE DOES THE SAME THING. `mame altbeast`, frames 280-400 sampled every 2 through its own
step 08: **8 of 60 samples changed, and its LONGEST HOLD IS 32 FRAMES**, ending at frame 346.
Ours is 33. The logo step is a near-static screen on the real board and holding it is correct.

ONE REAL DIFFERENCE, open and NOT a hold: when the arcade updates it changes **26-34%** of the
screen; our four updates change **6.2%** each. Same update RATE (8/60 against 4/30), a quarter
of the area. That is worth a look -- it is the logo's own colour alternation, measured
separately as a phase error that this build fixed at both sampled frames -- but it is not what
`presented_fps` was reporting, and "the port runs the logo step as a slide show" is withdrawn.

Lesson, the second in one day: compare against the ORACLE before calling a number a defect.
The bottom band was a crop and this was the arcade's own pacing. Neither cost a build, but both
cost a card.

**BOTTOM BAND CLOSED 2026-10-11: it was the SCREENSHOT CROP, not the rom.** The active picture
in a raw 1415x243 ares frame is rows **11..234** and cols 65..1344 -- 224 x 1280 exactly.
Re-measured on penfix1 at 700 and 2000 scanning EVERY column: lit span 11..234, and no
fully-black row anywhere inside it. Cropping at y=19 starts the 224-row window eight lines into
the picture and ends it with eight lines of BORDER, which reads as a solid black band at screen
rows 216-223 on every frame of every rom.

It had already been measured once: `rig_barcode.py:44` carries "active area rows 11-234
(measured 2026-09-25; 19 was 8 lines low)". The correction reached three tools and not the
other three. `tools/active_area.py` is now the ONE definition and `presented_fps.py`,
`attract_parity.py` and `night_run.py` use it. **Every attract_parity row ever produced
compared a window shifted down by eight lines.**

My own error on top of it: I "measured" the span as 19..234 with a scan that stepped every 7th
column, which missed the lit pixels in rows 11..18. 234-19+1 = 216, which is exactly the
"FB covers 216 of 224 rows" I then reported. A sampled scan is not a measurement of an extent.

penfix1 after the fix, black rows per window: **f700 0, f1800 0** -- the demos have none. f100
and f300 read 57 and f1300 65, which are the logo and high-score steps and their own backdrop,
not a band.

The three suspects below were ruled out correctly and are still ruled out; the FB zero at
216-223 is the through colour over a displayed MD row 27.

**(superseded) all three named suspects are RULED OUT, and no build was spent.**
penfix1 at frame 700.

| suspect | measurement | verdict |
|---|---|---|
| `BG_BOTTOM_HOLES` / the rows 24-27 cover test (`m_main.c` ~18596, the PURPLE BACKSTOP) | both blank the MD cell to `MD_BLANK_SLOT` (1023). `md_dbg_nt` tile row 27 reads plane A **40 of 40** non-blank, plane B **35 of 40** | NOT it: nothing is blanked at row 27 |
| the FB clear | `FBCLEAR` is `[2][224]` indexed by SCREEN row (`:11409`, `FBCLEAR[_y]` / `FBCLEAR[224+_y]`, `_y` = 0..223), not by sbuf row | NOT it: correctly sized and in range |
| the 32X FB line table | lines 216..223 read 0x8800 0x88A0 0x8940 0x89E0 0x8A80 0x8B20 0x8BC0 0x8C60 -- exactly linear, exactly right | NOT it |

**What IS measured:** the 32X framebuffer holds data for screen rows up to 215 and **zero from
216 to 223** (y=213 67 nonzero bytes, y=215 54, y=216..223 all 0). So the FB covers 216 of the
224 rows, and the MD plane's row 27 -- which our own mirror says is fully populated -- is not
reaching the screen either.

`sbuf` CANNOT discriminate composer-from-blit at a dump: it is explicitly zeroed per row per
frame (`m_main.c` ~960), so an end-of-frame dump reads zero on every row, including rows whose
pixels did reach the FB. A probe for this needs the dump taken mid-compose, not at frame end.

Next question, unanswered: why the blit stops at 216 and why MD row 27 does not display.

**SPEED BASELINE, penfix1.32x (2b2d3c99), 2026-10-11.** `tools/presented_fps.py ROM
--windows 100,300,700,1300,1800 --no-input --game-labels`. Windows are labelled by the GAME
(step 0xFFF031, key 0xFFF022, tick 0xFFF02A, read from SDRAM at _WRAM_BASE, not by check_demo
letters, which are arcade frame ranges and only say where we GUESS the port is).

| window | step | key | tick | MOTION/60 | any-change/60 | longest hold |
|---|---|---|---|---|---|---|
| f100 | 08 | 0C47 | 006E | 0 | 12 | 8 |
| f300 | 08 | 0C47 | 009D | 4 | 5 | **33** |
| f700 | 0C | 0947 | 00DB | 8 | 11 | 7 |
| f1300 | 10 | 0C47 | 02BA | 3 | 9 | 9 |
| f1800 | 14 | 0947 | 008C | 14 | 15 | 6 |

MOTION 5.8 fps, any-change 10.4, worst hold 33. Two things the table says that a scene-letter
table could not: f100 and f300 are the SAME step and key (08 / 0C47) and behave nothing alike
-- 0 motion with an 8-frame hold against 4 motion with a **33-frame hold**, over half a second
on one picture -- so the stall is INSIDE step 08, not a property of it. And key 0x0C47 vs
0x0947 is the static/demo half split the scene tables already key on.

**CLOSED 2026-10-11: the held wolf flash is a SPEED symptom, not a palette one.** Record 3
flashes set 14 for ONE frame (1997, 2002); spr_pair and cram_key read right every frame. The
flips on penfix1 are at 1985 1995 2001 2003 2007 (`ares --trace-flip`), so the composite built
at 1995 is DISPLAYED for six frames. Nothing holds the colour -- the picture holds. Any further
work on it belongs to the presented-rate card, not the palette card.

**NEGATIVE, 2026-10-11: the spr_pair memo is NOT the held sprite flash.** The theory was that
a pair rebound to a set it held before hits `cram_memo` on both key and gen and keeps its old
colours, so `cram_keygen[pr*2]`/`[pr*2+1]` were set to 0xFFFF at all six rebind sites
(`m_main.c` :5158 :5221 :5231 :11892 :16778 :16879 -- the sibling `grp_key` invalidation at
:5208 is the same idea). Built and measured, red pixels in the wolf band (y 80-140) per frame,
demo B:

| frame | 1994 | 1995 | 1996 | 1997 | 1998 | 1999 | 2000 | 2001 | 2002 |
|---|---|---|---|---|---|---|---|---|---|
| penfix1 (2b2d3c99) | 107 | 2090 | 2090 | 2090 | 2090 | 2090 | 2090 | 2 | 2 |
| with the invalidation | 1554 | 1554 | 1554 | 1554 | 1554 | 1554 | 1 | 1 | 1 |

**The hold is still six frames.** It moved one frame earlier and lost a quarter of its pixels;
it did not shorten. check-demo was 2 of 20, unchanged. REVERTED, so `native1` keeps matching
`rom/port/penfix1.32x`. The memo was a real missing invalidation and is not this mechanism --
do not spend a second build on it without a different theory for why the flash PERSISTS.

## Open cards, in order

0. **THE BOTTOM BAND IS TAG-STALE ON EVERY ROM, AND NOTHING FIXES IT YET.** Exposed by the
   marks1 glitch card on 2026-10-12 and NOT fixed by marks2. Plane A view rows 24-27 read
   **39 / 40 / 40 / 40 stale of 40** and NEVER converge -- measured on marks1 at emu 486, 489,
   492, 495, 500 and on clean1 at 499, 510, 530, identical on both. The BG beneath them is
   blanked (38 / 19 / 11 / 7 of 40 across those rows, identical on both) by the PURPLE
   BACKSTOP at `m_main.c:18663`, which blanks on the promise that the FG covers.
   **`fgcov` NEVER TESTS RESIDENCY**: `m_main.c:18648` is `fgcov = fw != 0` (the FG tilemap
   word is nonzero) and `:18659` is `fg_tile_opaque(...)` (the ART is opaque). The only
   residency guard is `!C1REF_GET(row, col)` at `:18663`, which catches a cell REFUSED a slot
   -- a STALE cell HAS a slot, so it passes and the BG is blanked under a cell whose art is
   not what the cell wants. The backstop's own comment already states the rule it is
   violating: *"an opaque FG cell that was REFUSED a slot is not on the MD, so do not blank
   the BG beneath it -- that is the black tile."*
   WHY IT IS NOT VISIBLE TODAY: the slots those rows name happen to hold the right art on the
   line. marks1 made them hold the wrong art and the band went black and purple at once, which
   is how the standing defect surfaced. It is one art-eviction away from being visible again.
   THE CANDIDATE FIX, not built: require the FG cell resident AND current (tag == key) before
   blanking, so a stale FG cell shows the BG beneath instead of the backstop. Ranked AFTER
   Mike's pass on marks2.

1. The pen-block move is APPLIED (73d1446f, cherry-picked 2026-10-10 21:40 from 10cc2144,
   which was on NO BRANCH -- it was written 2026-10-09 and never landed; no document in this
   tree ever claimed it had). mdp_pen_rc 0x3C300, mdp_pen_own 0x3C340; sdram_map reports no
   mdp_* overlap. It is NOT a pass: VDP CRAM nonzero per line at frame 2000 reads 0 / 12 / 13
   / 12 against the 11 / 15 / 15 / 13 the baked table wants. The GROUND bands are gone at both
   700 and 2000; the TOP-OF-SKY band is not (all-black rows 8-23 at 700, 104-112 at 2000, and
   216-223 at both; the arcade has none at either frame). Line 0 is still entirely zero, which
   is the same shape as the remaining band. Nothing went to Mike. Next: why VDP line 0 takes
   no pens -- the orphan's own note says its 15-word DMA to CRAM 0x02 never lands and names
   the missing 0x8F02 autoinc reset (md_main.c:617) as the suspect.
2. Gameplay: static tables per round, allocator off, same tools; then the gameplay tape.
3. Speed: STRUCK 2026-10-11, mislabelled PHMARK(10) -- the SYNC wait is 0.0%. (was: the flip_span SYNC wait, +5.1% per game frame on the static rom; 40% of the frame on
   both). 68K handler length and ack-vs-vblank at demo 2 first; the FB pipeline per the record
   after.
4b. **ROOM IN `.ramtext`.** `PORTHIST=1` no longer links on the line --
   "`.ramtext` overflows its 0x31000-0x38000 SDRAM slot (probe scrap 0x38000-0x39000,
   SPR_LAND 0x39000)". The per-phase instrument is therefore unavailable on the line;
   `PORTHIST=2` (the vint histogram) still builds. Ranked below the width card.

4. The 22 keyed assign calls for sets with no line: redo only with a declared counter block.
5. Hour death. 6. Sound. 7. The seven sdram_map overlaps. 8. Eye-pan anchor keyed on scroll.

## Mike

Short, numbers in tables, no "honest", certainty tags on facts. First line = the action, last
line = the next action. He will not validate a rom twice: if the advisor named it, it is the
rom. Use the rig over ssh yourself (mister.office.local is reachable from Brooklyn). He is
ready to kill the project over cycle time: an iteration is an hour, not a day.

To start the next advisor session: read this file, then the builder's latest report, and write
the next block. Look at the frames in screenshots/tiles-junk/ before anything else.

---------------------------------------------------------------------

# OVERNIGHT 2026-10-13/14 — P1, THE FM BEAT AND THE 68K SHIM

> **READ THIS FIRST.**
>
> **EVERY BOARD NUMBER BELOW WAS MEASURED ON `mister.tv.local`, AND THAT RIG
> CANNOT SHOW THIS RENDERER'S PICTURE.** The same build -- r60win1,
> `516677c0`, no cached-window change -- draws the whole picture at FULL
> DENSITY on `mister.office.local`: nonblack 11,864 / 8,877 / 7,019 over
> three captures, both instrument bars present, correct colours. So the
> speckled quarter-density sprites, the five refused instrument channels, the
> ~2 pixels per several hundred stores, and the dozen hypotheses excluded
> against those numbers were all chasing a RIG, not the renderer. The two
> MiSTers are being diffed (rbf, MiSTer binary, S32X.CFG, MiSTer.ini).
> Read every "board" sentence below as "on tv". The ares facts and the facts
> read from the core's RTL source stand on their own.
>
> **EVERY "DRAW LINES PER PASS" NUMBER I REPORTED IS AMBIGUOUS BY 356.**
> `r60_frt()` returns a uint16_t and the pass is timed as
> `(uint16_t)(r60_frt() - t0)`, so the delta WRAPS at 65,536 ticks = 356
> slave lines at 184 ticks a line. 212.1, 122.7, 133.5, 141.0, 148.6 and
> 149.8 could each be that value or that value plus 356. The advisor's
> master-clock event ring resolves the real figure: the pass ran **419
> lines** plus 38 for the CRAM commit, which is why every beat variant cost
> two vints. **The headline "the clear was the pass, 212 to 123 lines, -42%"
> is therefore UNSAFE as stated** -- the direction of the change is
> corroborated by the flip count rising (293 to 310) but the magnitude is
> not established. An accumulator that extends to 32 bits, or a coarser
> clock, is needed before any of these are quoted again.
>
> **The fourteen parts are in the order they were written, so the early ones
> also state things the later ones RETRACT. Do not act on a claim without
> checking this list.**
>
> **RETRACTED, do not use:**
> * "a frame is split across the banks" (second half) — it was a single-bank
>   mid-pass dump. Withdrawn in the third part.
> * "~40% board density", and every board density figure before the eleventh
>   part — they compared a screenshot taken at an arbitrary attract moment
>   against a fixed ares frame. Not a measurement.
> * "ares pins the overwrite image to bank 0" (seventh part) — the ares source
>   refutes it. The table was real; no flip had applied since frame 447.
>   Withdrawn in the eighth part.
> * "the FM byte write may differ from marks2' word write" — the core lanes it
>   correctly (BSC.sv:645-668). Withdrawn in the fourteenth part.
> * "nothing written to rows 216-223 reaches the MiSTer's screen" — tv only;
>   office shows the bottom bars.
> * "the cached write-through window is the board fix" — it is a NO-OP on tv
>   and the uncached build draws on office. Kept as the default on its own
>   merits (native1 m_main.c:3119-3124), credited with nothing.
>
> **CURRENT, and measured:**
> * THE LINE is `r60win1` (`11ed8267`): the FM beat, the new 10-line 68K shim,
>   the per-row clear, the vint-counted span, the FS request at FM=1, and the
>   no-overlap rule. ares f1100: 678 flips / 678 passes / 678 68K consumes,
>   0 backstops, 133.5 lines per pass, and THE SPRITES ON THE DISPLAY.
> * ON ares THE PICTURE IS `--screenshot`, NEVER A DUMP (sixth part).
> * An SH-2 FBCTL write at FM=0 is DROPPED and leaves no trace row (eighth).
> * The board RUNS the loop (thirteenth part) — true on tv, and on office it
>   runs AND draws.
> * **THE RENDERER'S PICTURE IS CORRECT ON REAL HARDWARE.** P1's board gate is
>   met on office by the line as built overnight.
> * The MD planes need the slot allocator carried; they are ares-gated and
>   nothing in the board thread blocks starting them.

Branch `rebuild60-p1`. Six commits, `685f98e6` through `71bcc6eb`. native1
untouched. Everything below is measured; nothing is inferred from feel.

## ITEM 1+2 — THE FM BEAT AND A NEW 68K SHIM.  PASSED on ares.  `685f98e6`

The beat as implemented:

  `sh_src/r60/r60_main.c` `visr_vbi`   flip; the V-ISR owns no FM
  `sh_src/r60/r60_main.c` `m_main`     publish at FM=1, OPEN the span the
        moment the frame is finished and published, CLOSE it on the 68K's
        COMM0 post or at the backstop. GO is gated on the span being closed,
        so the slave never draws at FM=0.
  `md_src/r60_shim.h`                  the new 68K vint shim, whole: count,
        return if FM is up, md_consume(A), md_consume(B), post COMM0, return.
        The old shim_vblank compiles as shim_vblank_legacy; nothing calls it.

f700: 68K vints 688 — ONE PER VINT, against 2 in 700 frames before. 68K
packet consumes 204-210. Spans closed on the post 188, on the backstop 106.
Master 687 vints, 294 flips, 294 slave passes, 1,022 sprites.

Five faults, each measured, each in its commit message: nothing raised FM
once the new shim stopped doing it; FM is a BYTE write at 0x20004000 and the
SH-2 interrupt enables are the next byte (a word write took the master's own
V-ISR count to 49,000 for 687 real entries); R60_TDROP sat inside the
longword R60_DIAG[0]; the span re-opened between a close and the next flip
(7,664 closes for 295 frames); and the backstop is not vblank's length — the
span opens mid-active-display and must survive the wait for the next vblank.

## ITEM 4 — THE BARS.  BUILT, read on ares, INVISIBLE on the board.  `04b8f007`

Slave, screen rows 216..223, through the direct image. Bar A = presented
frames per 64 vints x 5, bar B = backstop raises x 5; pen 255 reserved and
forced white by the CRAM commit. ares f700: **19.0 presented frames per 64
vints, 9.0 backstops**.

The CRAM commit moved to the vint edge AT FM=1 (it needs FM for the VDP side
and vblank for PEN; deferring it to "when both hold" landed 323 of 687,
committing at the edge lands 481-493).

## ITEM 5 — THE DIVIDER.  MEASURED on ares.  `53edb43a`

                        GOVERNOR=2        GOVERNOR=1
  presented per 64          27.3              47.1
  flips / 687 vints         293               506
  68K consumes              204               393
  backstop raises           89                113
  slave draw per pass       213.1 lines       217.3 lines
  sprite row-pixels/pass    3,992             2,988

GOVERNOR=1 presents 47.1 per 64 vints, 73.6% of one per vint. **The pass is
the CLEAR, not the sprites**: the draw cost holds at 213-217 lines of 262
while the pixels it puts down FALL 3,992 to 2,988, so it is not tracking the
work. The full-screen clear is 17,920 long writes every pass whatever is on
screen. Measurement only, as instructed; not acted on.

## ITEM 3 — THE BOARD.  NEGATIVE, AND NOT UNDERSTOOD.  `1c3baa8d`, `71bcc6eb`

The board does not show the picture the rom draws, and it is NOT a stale rom:
with the slave drawing nothing (`R60NOSPR=1`) the board goes BLACK, so every
rom measured was the one pushed and the sprites on screen are genuinely the
slave's.

A solid 320-pixel row of a constant pen, painted every pass, renders 320 of
320 on ares and TWO on the board — through the DIRECT image and through the
OVERWRITE image, at rows 100..109, at rows 130..146 (inside the very rows
where sprites land), as long stores, as byte stores spaced apart, and as
tight byte stores. No row on the board ever reaches 320 lit; the maximum seen
is 125, and the rows that ARE lit do not correspond to any block written.
Sprites appear in the right places, speckled, at about a quarter density.
The MD layer never shows: the 68K painting the backdrop green every vint
(`R60MDPROBE=1`) leaves the board black, and writing 32X CRAM entry 255 from
the 68K's own 0xA15200 path — md_main.c's MDSTAGE path, which this board
honours at boot — changes nothing.

**SHARPENED, 00:20.** The slave fills ALL 224 rows of the hidden bank with an
identical pen ramp (x selects the pen, so every live CRAM entry paints its own
column) and the board displays **SCREEN ROW 0 ONLY** -- full width, first lit
column 0, last 318, 125 of 320 columns lit, three launches agreeing. ares
displays all 224 rows of the same build.

That retires the palette (125 entries are demonstrably live, in the row that
does show) and the image choice (direct and overwrite behave alike). Also
tried and changed nothing: long / spaced / tight stores; the ramp before the
sprite loop instead of after (that one moved it from 0 to 22-60 of 320, the
only variation seen); the 32X line table written into BOTH banks at boot
(Hw32xInit writes only one -- mars.c:43-48 flips, waits, then fills -- so that
was a real bug and is fixed in `018f1dcb`, it just is not this one); and
MARS_VDP_DISPMODE plus the full line table re-asserted every presented frame.

The one thing that still does not fit: in the NORMAL build the board shows
sprites spread over rows 120..183, so multi-row writes do reach it there. The
difference between that and the ramp is the VOLUME written per pass -- 4,000
pixels against 71,680. A framebuffer write path that drops a long burst would
explain the ramp (one row survives), the solid blocks (two pixels of 320) and
the sprite speckle (a quarter density) together, and it is the only story left
standing. Spacing the stores apart did not help, but that test spaced them by
two uncached reads, which may not be enough.

Every channel this renderer can write has been tried. The next step needs an
instrument that does not go through the 32X framebuffer, 32X CRAM or the MD
layer, or someone who knows the core.

**This blocks the MD PLANES independently of the packet**: a plane the MD
layer cannot show is not worth sending.

## MEASUREMENT FAULTS FOUND (these invalidate earlier numbers)

1. **The framebuffer is TWO 128 KB banks and `--dump dram:0x200` reads one.**
   Which bank holds the finished picture alternates with the flip, and a dump
   can catch the slave MID-PASS: one read gave 2,768 px and no bars while the
   other bank held 12,211 px with every bar and block complete. Dump BOTH
   (`dram:0x200` and `dram:0x20200`) and take the complete one.
2. The old shim's beam stamps (0xFFA080-86) are last-value telemetry, not
   counts, and 0xFFA020 is written by BOTH consume calls with B second, so
   neither can witness packet A. The per-buffer census at 0xFFA076/0xFFA07A is
   the honest witness.
3. The tick counter is WRAM offset 0x302A — the work-RAM image is 16 KB, not
   64 KB. Reading 0xF02A made the game look frozen for two probes.

## WHAT ITEM 4's PLANES ACTUALLY NEED (scoped, not built)

Not a lift: the old publish is inline in m_main's 5,700-line body
(m_main.c:18441) and the hard part is the MD VRAM SLOT ALLOCATOR — a 128-set,
8-way LRU cache with a cset+plane hash, two-choice placement and CAT1
pressure rules (m_main.c:19180-19280). `r60_publish_planes()` writes the
smallest valid packet the 68K's own consume accepts (md_main.c:1441, 2321,
2342-2412) and the 68K consumes it; what is missing is cells, which need that
allocator, and a visible MD layer.

## ROMS ON THE RIG

`r60p1H.32x` is the clean build (no probe flags), BUILD 71bcc6eb, pushed and
launched. Probe roms: r60nospr, r60barprobe2, r60rate, r60mdprobe,
r60cram68k. Screenshots in `screenshots/r60/`, three launches per rom.

## OVERNIGHT, SECOND HALF (00:25-01:15) — THE PASS, THE SPAN, AND THE FLIP

Three more things measured and fixed on ares, in this order, each from the
previous one's number.

**THE PASS WAS THE CLEAR** (`763ffb62`). Item 5 showed the draw cost not
tracking the work; the full-screen clear is 17,920 long writes every pass
whatever is on screen. Each bank now carries a 224-bit row mask of what was
drawn into it and a pass clears exactly those rows -- two masks, because the
banks alternate and the rows to erase are the ones written two passes ago into
THIS bank. **Draw per pass 212.1 -> 122.7 lines, -42%.** Verified by eye and by
a long run (9,831 px over 116 rows at f700, 4,546 over 85 at f1400, 2,088 over
30 at f2400: it falls with the scene, nothing accumulates). Two faults on the
way, both caught by LOOKING at the picture and neither visible in the
counters: boot garbage, because a per-row clear never erases what the bank
powered up holding (both banks now get one full wipe at boot); and the sprites
smeared across dozens of positions, because the mark in `r60_sprite` had
silently not applied and only the bars were marking the mask.

**THE SPAN IS COUNTED IN VINTS, NOT TICKS** (`33457abf`). A tick backstop
cannot express what the span is for. At vblank's own length every span closed
on the backstop and the 68K consumed 1 of 295. At one frame it worked until
the row clear made the pass shorter -- the span then opened earlier and
expired just before the vblank it was waiting for, and consumes fell 208 -> 23.
Two vint edges is exact:

  **294 flips, 294 passes, 294 68K consumes, 294 spans closed ON THE POST,
  ZERO backstops, 1,130 sprites, 122.3 lines per pass.**

**A QUARTER OF THE FLIP REQUESTS NEVER TAKE.** The master waits for FS to read
back what it asked for before letting the slave start. It times out on 78 of
297 passes and the count does not move when the bound goes from 2,000 ticks to
a whole frame -- so these are not late applies, the write is ignored. The wait
must be the MASTER's (the slave read FS as unmatched on 278 of 280 passes: the
32X VDP registers are not reliably readable from there) and must be BOUNDED
(gating on the match alone deadlocked at one flip in 685 vints). Declining the
flip in the V-ISR while one is pending ALSO deadlocks -- at the vblank edge the
apply has not happened yet, so FS never matches there. Tried, reverted.

**OPEN DEFECT:** a frame is split across the banks -- bank 0 holding 3,563
image pixels and no bars, bank 1 holding the 640 bar pixels and no image, same
dump. The bars are painted immediately after the sprites in the same pass, so
the bank changed between them. It tracks the 78 timeouts.

**THE BOARD MOVED, for the first time.** The trend is consistent and it is
evidence about the mechanism:

    full clear, no FS wait     1 row lit of a full-screen ramp, bar 0 px
    row clear                  117-131 rows lit, bar 1-10 px
    row clear + FS wait        67-103 rows lit, bar up to 33 px

Shapes are now complete and correctly placed on the board (stelae, figures,
the wolf) and rendered almost entirely WHITE. One more probe ruled out the
obvious reading of that: with EVERY sprite pixel written in the reserved pen
whose CRAM entry the renderer forces to white (`R60ONEPEN=255`), the board
still showed 101-238 pixels in OTHER colours -- content that build never
wrote. So the displayed framebuffer is substantially not the one the slave
writes, which is the same conclusion the bank split reaches from the ares side.

**THE LINE for the morning: `r60p1L.32x`, BUILD 33457abf, on the rig.**

## OVERNIGHT, THIRD PART (01:15-02:20) — THE ADVISOR'S FLIP BLOCK

**ITEM 1, --trace-flip on the line build.** It answers the question and the
answer is that ares is clean: **220 SH-2 writes, 217 inside vblank, 2
deferred, and every write is followed by its own `flip` row at the same
vcounter.** Flips apply immediately here. So the deferral the record describes
(the 2026-09-10 flip latch) is a BOARD phenomenon and ares cannot show it.

**ITEM 2, the flip protocol** (`ab61702f`). Built as specified: the V-ISR
DECLINES rather than waits, on a pending flip (tracked by the renderer) and on
FBCTL bit 15 being clear (outside vblank); the main loop clears pending when
FS reads back the requested value, and GO is issued only then. On ares
**neither decline ever fires** -- consistent with the trace -- and the beat is
unchanged: 294 flips, 294 passes, 294 68K consumes, 1,072 sprites, 122.8 lines
per pass.

Two shapes of this that CANNOT work, both measured, so they are not retried:
reading FS in the V-ISR to decide whether to flip (at the vblank edge the
apply has not happened, FS never matches, the renderer deadlocks at one flip
in 685 vints) and putting the wait in the SLAVE (it read FS as unmatched on
278 of 280 passes against the master's 231 of 271 matched -- the 32X VDP
registers are not reliably readable from there).

**THE "SPLIT BANK" WAS MINE, AND IT IS WITHDRAWN.** I reported it from a
single dump at f700 and built a mechanism on it. Dumping BOTH banks at four
frames: f900 bank 0 holds 4,665 image AND 420 bar pixels, f1100 holds 6,802
and 640. A bank does hold a whole frame. f700 and f2000 caught the slave
mid-pass between the sprite loop and the bar paint -- the hazard I had
documented six commits earlier and then reasoned straight past. There is no
split, and the ares side is healthy.

**ITEM 4's BOARD GATE: STILL FAILING, and the write-granularity theories are
now dead too** (`257ecf62`). The board renders the sprites correctly shaped
and correctly placed at about 40% density, and the density does not move with
how the pixels are written. Three ways, three launches each:

    byte stores, overwrite image (the line)   shapes right, almost all WHITE
    byte stores, direct image                 colours back (3,609 coloured to
                                              1,317 white), still ~40%
    word PAIRS, overwrite image (R60WORDPLOT) 4,340 to 1,750, still ~40%

The word-pair path is the documented idiom -- the overwrite image exists so a
word write can carry a transparent pixel -- and adjacent pixels sharing a
16-bit word would have explained 40% exactly. It does not fix it. Kept as a
flag, not the line: it is not bit-identical on ares (20 bytes at f1100) and it
does not help the board. It did find one real bug through that check, a
negative-x index of (unsigned)(-1)>>1.

So, for the board, these are now excluded: the palette, the framebuffer image,
the write width, the write rate, the write order, the row range, the line
table (per bank), the display mode, a stale rom, the bank choice, and the
flip protocol. What remains unexplained is a constant ~40% of the slave's
pixels reaching the screen, with the shapes and positions right.

**ITEM 5, the planes, NOT STARTED** -- it is gated on item 4's board shot and
that gate has not passed.

**THE LINE: `r60p1M.32x`, BUILD 257ecf62, on the rig.**

## OVERNIGHT, FOURTH PART (02:20-03:30) — THE CORE, AND HALF AN ANSWER

The RTL read goes through nothing of ours and it pays. All in
`srcref/S32X_MiSTer/rtl/32X`:

  **IF.sv:974-983** the guard that would refuse a new SH-side VDP access while
  one is in flight is COMMENTED OUT. Any new bus start on that region
  re-latches `VDP_A` / `VDP_DO` / the strobes over a pending access, which is
  then never performed. BOTH SH-2s share the one path (:922-924).
  **VDP.sv:176-190** an access is accepted only when `!FEN` and `!FIFO_FULL`,
  otherwise `ACK_N` stays high and the cycle WAITS -- nothing is dropped at the
  VDP. `FEN` is `FILL_EXEC | RFRH` (:411), fill or refresh, NOT display fetch,
  so probe A was dead and was not run.
  **VDP.sv:513-514** the write enable is PER BYTE LANE and for A[17] (the
  overwrite image) each lane is gated on its own byte being non-zero, so byte
  writes through the overwrite image are correct in the core. In the DIRECT
  image a byte write of value ZERO is dropped unless the other lane is also
  enabled -- a real if minor bug of ours: the bars' zero tail never erases
  there.
  The VDP region is 0x20004100-41FF, 0x20004200-43FF and 0x24000000-2403FFFF
  (IF.sv:922-924). **FM at 0x20004000 and the COMM registers are NOT in it.**

**PROBE D -- the master makes no VDP-region access while the slave draws.**
CRAM commit, packet publish and the FS write all moved into the window between
DONE and the next GO; the V-ISR left touching nothing but its counter and
pp_due. Three launches:

  **THE WHITE IS GONE: 1,750 white pixels -> 410 -> 33 -> 1**, the content now
  overwhelmingly coloured and the RIGHT colours. That is the cross-CPU
  collision and the commented-out guard is the mechanism.
  **The density did not move**: ~30-40%, with the master silent.

**PROBE B -- longword stores, four pixels per access, the old blit's idiom:**
board 2,575 against ares' 8,592, about 30%. Four times fewer accesses does not
buy four times the density, so the access is not the unit of loss and the
single-CPU re-latch does not explain the density either.

ares under probe D: 685 flips in 1,085 vints, 684 68K consumes, 0 backstops,
both banks holding content.

**VERDICT: colour SOLVED and the cause named in the core's own source; density
UNSOLVED.** The no-overlap discipline should become the protocol regardless --
it is free, it is what the commercial model does, and SILICON.md wants the note
("MiSTer IF.sv:974-983 has no in-flight guard; do not interleave SH-side VDP
accesses from the two CPUs"). It is NOT on the line tonight because probe D's
loop also dropped the governor and moved the flip out of the V-ISR; the right
version keeps the beat and moves only the three accesses, and that deserves
doing awake.

Probe roms: r60probed, r60long2, r60solidspr (the last draws each record as a
solid bar with NO art read: the board shows 2-11 pixels where ares shows 158
rows, which is how we learned that a tighter write loop loses MORE).

## OVERNIGHT, FIFTH PART (03:30-04:30) — PACING IS DEAD, AND A BETTER INSTRUMENT

**PROBE E(a): the FIFO-rate account does not survive.** `R60PACE=n` puts n nops
after every framebuffer store, in the sprite plot AND the bar paint. ares
output is identical at PACE=0 and PACE=6 (660 bar pixels both). On the board,
three launches each:

    PACE=0   bar px  1, 1, 1   of 660
    PACE=6   bar px  1, 1, 1   of 660

Six nops is ~260 ns between stores against the ~130 ns the FIFO needs to drain
one (VDPFIFO.v:80 four entries; VDP.sv:234-247 ~7 clk_sys per pop). It changes
NOTHING, so **probe E(c)'s sweep cannot rescue it either -- 6 already clears
the threshold** and the knee is not at 3. Not run.

**AND THE MEASUREMENT I SHOULD HAVE USED ALL NIGHT.** The bar's length is known
exactly -- R60_BARA/B pixels of one pen in known rows -- while the attract
MOVES between captures. Every board "density" figure before this one compared a
screenshot taken at an arbitrary moment against a fixed ares frame, which is
not a measurement; **the "~40%" carries that error and should not be quoted.**
The bar does not: **1 of 660, repeatably, three launches, twice.**

**WHICH SPLITS THE BOARD'S BEHAVIOUR IN TWO, and this is the new fact:**

    the BARS, DIRECT image, rows 216-223     ~0%   (1 of 660)
    the SPRITES, OVERWRITE image, rows ~100-200   visibly present

Two differences at once -- the image and the rows -- and I could not separate
them: the probe that paints the bar through the OVERWRITE image in the same
rows read 0 on ARES as well (a mid-pass bank in the dump), so the board reading
from it is void. That is the experiment to run first when this resumes, with
both banks dumped.

VDP.sv:513-514 does treat the two images differently -- in the DIRECT image a
byte write of value ZERO is dropped unless the other lane is enabled, which is
a real bug of ours for the bar's TAIL -- but it does not explain the LIT part
of the bar going missing.

**THE LINE: `r60p1P.32x`, on the rig.** f1100: 463 flips, 463 passes, 463 68K
consumes, 462 on the post, 0 backstops, 2,088 sprites, 148.9 lines per pass,
bank 0 holding 6,802 image and 640 bar pixels.

## OVERNIGHT, SIXTH PART (04:45-06:30) — THE BUG REPRODUCES ON ARES

**THE METHOD IS THE FINDING.** `ares-headless --screenshot N:file.png` captures
the DISPLAY -- through the line table, CRAM and the MD mix -- while every check
I ran all night was a DRAM dump, which bypasses all three. That is why ares
agreed with itself and disagreed with the board.

**ares --screenshot of the line at f1099: a black field, a green MD border and
the white bar at the bottom left. NO SPRITES. A dump of the same frame counts
thousands of sprite pixels.** Control: the same capture of marks2 differs, so
the instrument works. **No rig round-trip is needed to chase this any more.**

RULE, and it belongs in the repo's own voice: on ares the PICTURE is
`--screenshot`; a dump counts pixels and is not the picture.

Three candidates tested and all three excluded BY MEASUREMENT:

1. **The global layer priority.** `MARS_VDP_PRIO_32X` (DISPMODE bit 7) really
   was lost: Hw32xInit writes no priority bit (mars.c:40), the old renderer set
   the full value (m_main.c:8708, :13820) and the 68K's copy (md_main.c:4592)
   is inside the LEGACY shim r60 never calls. It is written now -- asserted
   late with FM up, because SH-side VDP access is gated on FM in the core
   (IF.sv:974) and md_main drops FM at the end of its boot (:7380) -- and it
   **reads back 0x8081**. The display did not change by one byte.
2. **The per-pen priority**, CRAM bit 15 on every entry: not one byte.
3. **The palette.** PAL_SH holds 1,146 non-zero words; the +1024 in the
   commit's source is CORRECT (sets 0-3 are ALL ZERO at `PAL_SH[set*16]` and
   hold 13-14 colours at `PAL_SH[1024 + set*16]`; both heroes here are set 0,
   so the low half blacks out exactly them -- I removed it and had to put it
   back). And read back from 32X CRAM, bank 11, the bank the records name:
   `0000 4110 2AFC 5B5F 531E ...` against the expected
   `0000 1A98 2AFC 5B5F 531E ...` -- real colours, matching entry for entry.

**A CORRECTION I OWE:** I first read the CRAM-flood probe (every entry forced
bright) as proving the sprites were drawn-but-black, because it lit 243 of 243
display rows. It lights EVERY row, including rows no sprite touches -- that is
the 32X layer going opaque because entry 0 was flooded too, not the sprites
appearing. A test that lights every row cannot tell you a particular row's
pixels arrived. The commit saying otherwise is corrected in `6748fad8`.

**STATE: the sprites are in the framebuffer, their colours are in CRAM, and
the display shows black.** The probe blocks DO reach the display (solid rows
at FB 100-109 and 130-146 appear as display rows 112-120 and 144-152), so the
path is not dead -- it is something the sprite pixels have and those blocks do
not. That is the next question and it is now a cheap ares question.

**THE LINE: `r60p1R.32x`** -- f1100: 488 flips, 488 passes, 488 68K consumes,
0 backstops, 2,113 sprites, 143.8 lines per pass.

## OVERNIGHT, SEVENTH PART (06:45-07:00) — THE ares DEFECT, VERIFIED, AND THE BOARD'S ROWS

**ares pins the OVERWRITE image to bank 0.** Verified on my own run, the line
build, both banks at three consecutive frames:

    f1098  bank0 image 6802 bar 640 | bank1 image 0 bar 640
    f1099  bank0 image 2166 bar   0 | bank1 image 0 bar 640
    f1100  bank0 image 6802 bar 640 | bank1 image 0 bar 640

Sprite pixels land in bank 0 on EVERY frame and NEVER in bank 1; the bar,
written through the DIRECT image, is in both. The display alternates, so ares
shows the sprites only on frames that display bank 0. A defect in the
ares-debug fork -- not the 32X, not the MiSTer core, whose VDP.sv:511-514
takes `FB_DRAW_A` from the FIFO entry with the bank selected by FS for BOTH
images.

**It retires two of last night's readings as emulator artefacts:** "the
sprites are drawn and the display shows black" was a bank-1 frame, and "the
probe blocks reach the display but the sprites do not" was the blocks being
direct writes in the displayed bank while the sprites were overwrite writes in
the other. The R60BAROVR control that "read 0 on ares too" is the same thing,
not the mid-pass dump I blamed.

**ares GATE until the fork is fixed:** judge the picture at a frame whose
displayed bank is 0 -- two screenshots a frame apart, keep the one with the
sprites.

**THE BOARD'S SYMPTOM IS NOW ONE CLEAN FACT.** The bar at rows 216-223 reads
**1, 1, 47 of 660 through the OVERWRITE image** (R60BAROVR=1, three launches)
against 1, 1, 1 through the direct image. Both ~0, so by the stated rule it is
**THE ROWS, not the image**: nothing this renderer writes to rows 216-223
reaches the board's screen, while the sprites at rows ~100-200 do. The bars
are not a usable instrument on the board where they sit; the cheapest next
move is to put them in rows that work.

## OVERNIGHT, EIGHTH PART (07:10-08:00) — THE FLIP NEVER APPLIED, AND ares NOW DRAWS

**RETRACTION FIRST.** The ares "overwrite image pinned to bank 0" lesson is WRONG and is
retracted in `LESSONS.md` (`9ee1d9a3`). ares-debug bus-internal.cpp:51-52 and :60-61 both
write `vdp.bbram`; there is no per-image bank. The table it rested on -- sprites always in
half 0, bar in both -- was real, but it was every pass writing the SAME half because the
flip never applied, with the half-1 bar a fossil.

**THE ACTUAL BUG, and it was mine.** An SH-2 write to `MARS_VDP_FBCTL` is DROPPED at FM=0
(MiSTer IF.sv:974 latches SH-side VDP access only with FM; ares returns before its own
trace hook, io-internal.cpp:344-349, so a dropped write leaves no trace row). The master
opens the 68K's FM=0 span on EXACTLY the condition the V-ISR needed in order to flip --
`DONE==GO && PUB==GO` -- and earlier in the vint. **So every flip request since the beat
(`685f98e6`) was written at FM=0 and thrown away.**

The evidence was in my own first trace and I walked past it: 220 SH-2 writes whose LAST is
at frame 447 of 1101. I called it a truncated file. It also retires, as this one bug: the
"FS matched 231 of 271" census, the "gating on the match deadlocked" result, the 78-247
"gave up waiting for FS" counts, and the mid-pass bank splits.

**THE FIX** (`850ac6ba`): the flip request moves to the span-open site, where FM is still
up, immediately before the drop with nothing between. It is then outside vblank, which is
harmless -- the hardware defers it to the next vblank start (VDP.sv:400).

**GATE ON ares, r60flip1, all four:**

    (a) --trace-flip to f1101   699 writes, 664 deferred, 701 flips,
                                LAST write and flip at frame 1099
                                (was: last at 447, none in 650 frames)
    (b) both halves             f1098 H1 +4347, f1099 H0 +6276, f1100 H0 +4342
                                (was: H1 image 0, 0 bytes changed, for 650 frames)
    (c) counters                695 flips, 695 passes, 695 68K post-closes,
                                FM re-raises 0, backstops 0, ISR declines 0/0
    (d) screenshot f1098/f1099  124 of 243 display rows with content, both
                                (was 23 -- the border alone). THE SPRITES ARE
                                ON THE DISPLAY: the attract's hero close-up in
                                its own colours.

One deviation from the specified gate: DIAG2[12] ("gave up on FS") is not 0 but 608 of 695.
The FPEND clear polls FBCTL while FM is DOWN, where the SH-2 cannot read it either, so it
always clears on the timer rather than on the apply. Nothing blocks on it -- 695 flips, 695
passes -- but the GO gate is therefore timer-driven, not apply-driven. Worth tidying.

**THE BOARD IS UNCHANGED.** r60flip1b, three launches: image 750 / 2046 / 5206, bar still
**1 of 800**, the same speckled white sprites. So the two bugs were separate: this one was
ares', and the board's remains. The rows-216-223 verdict was measured under the no-flip
regime and is marked in LESSONS as needing re-verification, but the flip fix did not move
it, so it probably stands.

## OVERNIGHT, NINTH PART (08:15-08:45) — r60win1, THE NO-OVERLAP RULE IN FULL

**ares: every gate passes** (`11ed8267`). Against r60flip1:

    trace                 698 writes, 700 flips, last at f1099 (continuous)
    flips/passes/post     678 / 678 / 678, backstops 0, V-ISR declines 0, 0
    DIAG2[12] gave up     0            (was 608 of 695)
    CRAM commits          678, PEN timeouts 0
    display rows          124 of 243 at f1098, unchanged
    draw per pass         133.5 lines against 132-135, inside the 2 allowed

The audit is in the commit: every master VDP-region access now happens only
with the slave idle, FM up and the span closed.

**Two ways of getting it wrong, both worth keeping:** "the slave is idle" is
NOT sufficient on its own, because `DONE == GO` is also true while the span is
open (the span OPENS on it) -- gating the CRAM commit on it alone put the
write at FM=0 and the display went back to 23 rows. And once BOTH conditions
are required the V-ISR is the wrong place for the commit: a vint edge almost
never lands where they hold, so it fired 5 times in 1,091 vints. It belongs in
the window, outside vblank, waiting on PEN per bank.

**BOARD: the white did NOT drop.** Three launches: WHITE 0 / 282 / 1508,
coloured 749 / 1744 / 3795, bar 2. The richest capture is 28% white against
r60direct's 27%. By the rule in the block, **the overlap card is exhausted and
the rows/pens card takes over, with r60win1 as its base.**

The no-overlap discipline stays regardless: it is correct, it is what the
commercial model does, and it is what took DIAG2[12] to zero.

## OVERNIGHT, TENTH PART (08:50-09:10) — r60rb1, AND THE BOARD GIVES THE THIRD ANSWER

**The instrument.** Inside the 68K's span (FM=0 by construction) the shim reads
the write-side framebuffer bank through its own 0x840000 window -- rows 60-168
step 2, x step 4, 4,320 reads -- and reports the non-zero count as the 32X
background colour via 0xA15200: green >= 400, yellow 100-399, red below. The
count also goes to COMM10. The master skips CRAM entry 0 while the probe is
on, so the verdict is the 68K's alone.

**ares calibration: GREEN**, 34,005 of the sampled pixels, the hero's skin tone
beside it, 243 of 243 display rows with content. Path and thresholds good.

**BOARD, three launches: THE BACKGROUND STAYS BLACK.** Not green, not yellow,
not red -- unchanged. That is the third outcome the block named, and it
invalidates the probe's premise: **the 68K's write to 32X CRAM at 0xA15200 is
not landing from inside the vint handler**, although md_main.c's MDSTAGE
colours land from the same address at boot. The board cannot be asked this
question this way.

What separates boot from the loop is NOT FM -- both are FM=0. The two
candidates are the master's own CRAM burst re-latching over the 68K's single
write through the core's unguarded SH-side VDP path (IF.sv:974-983), or the
68K never reaching this code on the board. Telling them apart needs COMM10
read back somewhere visible, which is the same problem one level down.

Stopped and reported, per the block.

## OVERNIGHT, ELEVENTH PART (09:15-10:00) — r60bars2, AND ZERO IS A READING

**ares:** bar A 256 px (full), bar B 256 px, raw readback 5,828 against the
predicted ~5,500; 671 flips / 671 posts / 0 backstops. Draw 141.0 lines per
pass against 133.5 -- **+7.5, outside the +3 allowed**, which is the
8,640-byte readback's own cost.

**BOARD: all six readings ZERO**, three launches of each of three variants,
while sprites in the SAME ROWS display in the same captures (up to 6,040 px):

    r60bars2    bars after the sprites, pen 0x11     A 0 0 0   B 0 0 0
    r60bars2w   same, pen 255 (forced white)         A 0 0 0   B 0 0 0
    r60bf1      bars BEFORE the sprites, pen 255     A 0 0 0   B 0 0 0

Two confounds removed on the way, neither of which changed anything: the
first cut drew the bars in a game pen whose CRAM entry the renderer does not
control, and the second tested POSITION IN THE PASS -- the one pattern that
had held all night (probe blocks 0 after the sprite loop, 22-60 before it).

**ZERO IS A READING, NOT A FAILURE.** Bar A is zero when the 68K posts zero
of posts+backstops; bar B when the master's readback finds under 640 non-zero
bytes. By the block's own table: **"A zero, B low: both; take the 68K first."**

**THE DESIGN'S FLAW IS MINE AND IT MATTERS:** zero width and not-drawn are
indistinguishable. A one-step minimum -- always draw the first 32 px, so the
floor means "I ran" and only the steps above it carry the number -- makes the
next reading honest. Worth doing before anything is concluded from a zero.

The line (r60win1) is back on the rig.

## OVERNIGHT, TWELFTH PART (10:05-10:30) — r60bars3, AND THE INTERLEAVE IS DEAD

Three bars, rows 100-103 / 106-109 / 112-115, pen 255, each a known 256 px
(32 px unconditional floor, then 224 fixed), differing ONLY in the filler
between stores: four nops, a CART word read (the sprite loop's own access), an
uncached SDRAM word read.

    ares     all three 256 of 256
    BOARD    0, 0, 0 of 256 -- every bar, every launch, three launches

The captures are live scenes (757 / 2,092 / 5,298 px lit overall) and rows
100-115 carry exactly SIX lit pixels in all three -- the same tiny constant
every deliberate write on this board has produced all night (a 320-px solid
row gave 2). It does not move with the filler.

**"A store lands only when it is followed by a slow non-VDP bus cycle" is
DEAD.** A cart read between stores, the exact access the sprite loop makes,
buys nothing. By the block's table: all three zero, so the 68K card is next,
with the floored A/B bars.

**THE FLOOR DID ITS JOB:** these zeros are not r60bars2's ambiguous
zero-width. Nothing the slave writes to those rows arrives. What remains
unexplained is why the SPRITES in the rows just below -- same code, same pass,
same image -- do.

Draw cost 149.8 lines per pass against r60win1's 133.5; that is the three
256-store bars, not the renderer.

## THIRTEENTH PART (10:35-10:50) — ZERO-BUILD: THE BOARD IS RUNNING

The premise under all 45 board builds -- that the machine is running and
losing stores, rather than stopped with a frozen picture -- tested without a
build.

**It was already answered by data in hand:** the three r60bars3 captures were
ONE launch, 14 s apart, and they are three DISTINCT images (757 / 2,092 /
5,298 px).

**And confirmed over a longer run.** r60win1, one launch, five captures at
~5 / 20 / 40 / 60 / 120 s:

    sprite px   4,124    6,982    456    4,036    755
    distinct    5 of 5, every one changed from the one before

So the board runs the loop continuously for at least two minutes and the
picture changes throughout. **The "it stops shortly after boot" hypothesis is
dead, and the selective loss is real.**

The marks2 capture-path control was conditional on r60win1 reading identical
across all five. It did not, so the capture path is demonstrably live and the
control was not spent.

By the block's table: the loop runs, so the 68K card with the floored A/B
bars is next -- Mike's call.

## FOURTEENTH PART (11:00-11:10) — THE FREEZE PROBE, BUILT AND CALIBRATED, LAUNCHES HELD

Bar instruments are dead on the board (r60bars3's unconditional floors read
zero), so a probe cannot answer in pixels. It answers by STOPPING instead,
riding the one channel proven to carry information.

    R60FZ=1   stop issuing GO once the 68K has POSTED >= 8 times in 64 vints
    R60FZ=2   stop once the span hit the BACKSTOP >= 8 times in 64 vints

ares, screenshots at f300 and f900:

    r60fz1   IDENTICAL   frozen, as designed -- the 68K posts on ares
    r60fz2   different   live, as designed -- backstops are 0 on ares

**The two board launches are HELD for Mike.** He is awake, the overnight
mandate has expired, and I have twice told him I am holding the board thread.
The roms are built and calibrated; it is one command.

**THE FM BYTE-WRITE IDEA IS WITHDRAWN** before anyone spends a build on it. I
raised it as a real difference (the master sets FM as a BYTE at 0x20004000,
marks2's 68K as a WORD at 0xA15100). The core's bus controller lanes it
correctly -- BSC.sv:645-668 puts an A[1:0]=00 byte store on DO[15:8] with the
upper strobe, so IF.sv:498 sees SHDI[15]=1 -- and the readback is honest. The
two forms are equivalent.

## THE MD PLANES — THE DESIGN, SCOPED AND READY TO BUILD (not built)

**The allocator CAN be carried; the walk CANNOT.** The claim/evict loop
(native1 m_main.c:19180-19280) is self-contained given (code, cset, isfg,
win_no) and comes across nearly verbatim, with its state at the addresses it
already uses: `md_tag` 0x0603B400 [1024 longs], `md_ref` 0x0603B000 [1024
bytes], `md_dirty` 0x06028EC0 [32 longs]. **Note `md_dirty` begins exactly
where r60's DIAG2 block ends (0x28EC0) — DIAG2 must not grow past 16 longs.**
The CAT1 pressure rule drops out with CAT1.

The WALK cannot be lifted: it is woven through cat1code/cat1scr/c1cell_release,
the rows-24-27 rowscroll band with its own page set, fgcov and the purple
backstop. REBUILD §5 says to delete that machinery, so r60 writes a new one.

**The pieces, all located:**

    cell decode     code = w & 0x1FFF; if (code & 0x1000) code = (code & 0xFFF)
                    + bank1 * 0x1000; cset = (w >> 6) & 0x7F
                    (code and cset DO overlap -- the S16B quirk, carry it)
    tilemap         TILEMAP_U 0x26019000, 13 pages x 0x800 words; a cell is
                    pg[(vx >> 9) & 1][(vx >> 3) & 0x3F], two pages side by side
    art             px = altbeast_tiles + (key & 0xFFFF) * 64 (one byte a
                    pixel); map = mdp_s_map + cset * 8; out byte =
                    (map[r[2k]] << 4) | map[r[2k+1]], 32 bytes a tile
    art packet      WRONG, AND IT COST THE FIRST BUILD -- see the correction
                    below. There is no separate type-0 art packet on this
                    path.
    NT packet       TYPE != 0: NT_WRAP's seven row records then the hscroll
                    delta tail (md_main.c:2342-2412); nbase 0xC000 plane A,
                    0xE000 plane B
    palette         48 words at packet offset 688 -> CRAM entries 16-63, i.e.
                    MD palette lines 1-3; line 0 stays the grey/text ramp
    NT word         (palette_line << 13) | slot

**So TWO packets per frame, which is what the 68K already consumes: A = type 0
art, B = type 1 name table.** r60 currently publishes one empty A.

**The pen map is the one piece with no owner.** `mdp_s_map[cset*8 + p]` is
filled by the old renderer's pen allocator, which r60 does not have. r60 owns
the MD palette, so it can define its own: identity (map[p] = p) and three
colour sets on the plane at once, assigned to palette lines 1-3, their 16
colours each emitted in the 48-word block. That is the same ceiling the old
renderer had (mdp_s_line) and it is what makes the first build tractable.

**First increment, gated:** plane B only, no scroll, three colour sets,
inline art (no baked blob, no cart reads). ares picture + check-demo, then
three office launches.

### CORRECTED AFTER THE FIRST BUILD — four things in front of the pipeline

The background is now ON THE OFFICE BOARD (the temple behind the hero at the
attract's fireball scene), advisor commits `164cc237` and `e06e6cdf`. The
pipeline in `r60_planes.c` was right; four things in front of it were not,
and the first is mine twice over:

1. **THE ART CONTRACT ABOVE WAS READ OUT OF DEAD CODE.** md_main.c:2323 is
   `#else /* legacy staged consume */`, so the TYPE 0 reader at :2326-2340 is
   compiled OUT under R60. The live path is the DIRECT one, and it reads art
   only as the ART TAIL of the NAME-TABLE packet: `sc[1]` bit 14, then [n],
   then inline records `slot | 0x4000` plus 16 words (:2027, slim_walk :735).
   So packet A was never read by anything: VRAM tile memory changed by ZERO
   bytes between frames while 555 batches counted as consumed. The art rides
   packet B's tail, about 21 records a frame before the palette at 688.
   **I located a contract inside an #else branch and quoted it here with its
   line numbers. Check which arm of the #ifdef you are reading.**
2. **`pb[1]` bit 15 (palette present) was never set**, so the 48 words I was
   dutifully writing never landed -- MD CRAM held four entries.
3. **Tile 0 held art.** `MD_BLANK_SLOT` is 1023 in native1 but must be 0
   here: the empty cell and the whole of plane A are tile 0, so plane A
   painted that art over plane B. Slot 0 must be unclaimable and re-sent as
   a zero record every window.
4. **32X CRAM entry 0 needs bit 15 and must be rewritten every commit** (ares
   vdp.cpp:71-81), because the 68K's MDSTAGE boot colours overwrite it late.

And a sprite bug Mike saw on the board: the plot drew its first row from the
UNSTEPPED address -- jts16_obj_scan.v:78 steps first. Fixed in `164cc237`.

**My two display candidates were both wrong:** the hscroll one is moot on the
direct path (hscroll comes from the header words sc[3]/sc[7]) and the pen-map
transparency one was not it either.

**Still open on the planes:** a full refresh is 4 windows, 21 art records a
frame, 3 palette lines a scene, and plane A -- the S16 FG tile plane -- is
not built.

## THE PLANES — FIRST BUILD: THE PIPELINE REACHES MD VRAM, THE LAYER DOES NOT SHOW

`sh_src/r60/r60_planes.c`, `R60PLANES=1`, commit `2347d9ff`.

**Carried:** the slot allocator from native1 m_main.c:19180-19280, state at its
existing addresses. **New:** the walk, because the old one is inseparable from
CAT1, the rowscroll band and the purple backstop.

**Working, ares f1101:** 55,816 cells with content and 38,696 with a palette
line through the walk; both packets consumed (655 and 655 of 656); **6,640
non-zero bytes of tile art in MD VRAM** and **784 live name-table entries at
0xE000**.

**A real gap closed on the way:** NOTHING HAD EVER SET THE MD VDP'S PLANE
REGISTERS. The game's boot does that, and under NO_GAMEHIGH there is no game
-- the shim set regs 0, 1, 11 and 15 only, so plane B's nametable base read 0
(the tile area) however correct VRAM was. Regs 2, 4, 5, 7, 13 and 16 are set
now.

**Still black.** Three candidates, none tested, in the order I would try them:

1. **The hscroll table at 0xFC00 is never initialised.** md_consume's
   NT_WRAP branch does not set stg[3]/stg[4]; only the non-NT_WRAP branch
   does. So plane B's horizontal scroll is whatever VRAM powered up holding.
   One line to test and it fits the symptom exactly.
2. **The identity pen map sends S16 pixel 0 to MD pen 0, which is
   TRANSPARENT.** If the background tiles are mostly pixel 0 the plane is
   being drawn correctly and is invisible.
3. The 32X priority and the MD layer enable want checking together now that
   both layers are actually live.

Stopped deliberately at fifteen hours with the error rate rising, not because
the card is blocked.
