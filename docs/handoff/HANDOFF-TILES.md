# HANDOFF — the junk tile planes. READ THIS FIRST.

Written 2026-10-10 for a fresh thread. The picture card is **wrong TILES, not colours.**

**Branch:** `native1`, tip `4399b0e4`. The negative edit was reverted at `4399b0e4`;
the measurement is kept in `3dfd4e07`.

**A WARNING ABOUT THIS DOCUMENT'S AUTHOR.** The thread that produced it mismeasured
repeatedly on 2026-10-10 — a pad control that never compiled (twice), a counter that collided
with an existing one, scrap addresses that were not free, a ratio quoted against the wrong
denominator, and a "set bases" signature generalised from six samples that did not survive
contact with all 449. Every number below that is marked MEASURED was taken from a dump with
its instrument proven in the same pass. Anything not so marked is a lead, not a fact. Re-read
a counter before you build on it.

**SUPERSEDED 2026-10-10 20:35 — SECTIONS 2 TO 4 WERE A WRONG INSTRUMENT.**
`tools/nt_dump_ares.py` read `snap` at a hardcoded `0x052E8`; the port build puts it at
`0x39940` (`m_main.c:4378`, WIN_TWO's gap). Every class count in §2-§4 is therefore of some
other SDRAM block, including the 1,649 class (b) cells and the 572 `tag == 0xFFFFFFFF` slots
of §4. At the right address `44d1673f` reads plane B 943 ok / 126 stale and plane A 515/605
at frame 700, and B 1069/0, A 801/319 at 730 and 760. The unmodified rom reads 255 at 700 and
189 at 730, which is the whole of `3dfd4e07`'s "255 -> 189": **that result is UNMEASURED, not
disproved.** (Re-measured at the right address 2026-10-10 21:0x: the pre-fix rom reads plane B
943 ok / 126 stale and plane A 515 / 605 at 700, reproducing the numbers above exactly, so the
instrument is now agreed by two threads.)

**The cause was not in the tag table at all.** `tilemap_unpack_hi` returned 0 instead of its
advanced source cursor, so `tilemap_unpack_lo` decoded the low byte of all 20,480 tilemap words
from pointer 0 (`0327ec60`). Port tile RAM against the arcade's, MEASURED: **2,430 of 20,480
equal words before, 20,480 of 20,480 after**, at frames 700 and 2000 both, with all 18,050
pre-fix mismatches keeping the arcade's HIGH byte and none its low. §3's "missing dirty mark"
bullet stands only as a code fact, not as a measurement.

**What the fix did NOT fix.** The map is now exact and the junk is a different defect: distinct
on-screen tiles the walk demands went **117 -> 493** at 700 (the wrong low bytes had collapsed
the map onto 117 codes), and plane A still reads 568 ok / 552 stale at 700, plane B 655 / 393.
Shipped keys equal wanted keys in every run (493 of 493), so the art is present and the fault is
in which slot a cell names. NOTE ON 726: that arcade figure was taken by another method; the 493
here counts only the 2 x 1120 on-screen tilemap cells and excludes the text layer and sprites.
Do not treat 493/726 as a ratio until one method counts both sides.

**THE BLACK BANDS WERE THE REFCOUNT ALIAS, AND THEY ARE HALF GONE (2026-10-10 21:40).**
`mdp_pen_rc` sat at 0x3C460, which is `mdp_line_c` line 3 (indices 48-63), and `mdp_pen_own`
at 0x3D1A0 overran `md_dbg_nt`. The emit at `m_main.c:19388` ships black for rc == 0, so the
aliased refcounts blacked out whole bands. The move (`73d1446f`, cherry-picked from the
orphaned `10cc2144`) puts them at 0x3C300 / 0x3C340. MEASURED: all-black screen rows at
frame 700 go 8-23 + 170 + 173 + 176-183 + 216-223 down to 8-23 + 216-223, and at 2000
104-119 + 168-183 + 216-223 down to 104-112 + 216-223. The arcade has NO all-black row at
either frame. VDP CRAM nonzero per line at 2000: 0 / 12 / 13 / 12, wanted 11 / 15 / 15 / 13.
The ground bands are gone; the top-of-sky band and the bottom eight rows are not, and VDP
line 0 still takes no pens at all.

## 0d. A 0140 is a ONE-FRAME SAMPLING OFFSET, and the pace suspect is cport_build (2026-10-11)

**THE 96 BYTES ARE NOT CORRUPTION.** Field-level diff of `aw_A_784.bin` against
`port_2a9b11cc_807.bin`, field names from `tests/demo-b/records.py:4`:

- **byte 0x22 is +1 on EVERY ONE of the 14 live slots** (0,1,2,3,4,5,8,17,18,19,20,40,62,63).
  A uniform off-by-one on a per-game-frame counter inside `state[0x20:0x24]`.
- slots 1-5 also carry +1 in byte 0x20 (02->03, 06->07, 0a->0b, 0e->0f, 12->13).
- the only position fields that move are one frame of motion: slot 8 x `11cf`->`11cd`,
  slot 62 x `1254`->`1253` and y `10b4`->`10b8`, slot 63 y `10b4`->`10b8`.

So the port is sampled ONE GAME FRAME further along than the arcade at the matched tick. By
the letter of the card this is "position and state" and triggers a build; by the DELTA it is
the sampling-point class. The discriminator that matters is the uniform +1, not the field name.

**THE PACE MOVE IS STILL REAL AND IS NOT port_l2.c.** Build 1, HEAD with `sh_src/port_l2.c`
reverted to `c4d02904` (the commit behind rom f6f2f68a; `f6f2f68a` is a rom id, not a git ref):

| build | rom | clock | check-demo | A 0140 |
|---|---|---|---|---|
| HEAD | d334f8b0 | x1.018 | 4 of 20 | 96 bytes |
| control, no clear_tileram | 2a9b11cc | x1.018 | 4 of 20 | 96 bytes |
| **build 1, port_l2 reverted** | **746ef856** | **x1.018** | **4 of 20** | **96 bytes** |
| reference | f6f2f68a | x1.031 | 2 of 20 | 0 bytes |

My layout suspect is wrong. And every `pp_*` counter added in the range is inside
`#if defined(PORT_TILEDIRTY)` (m_main.c:2386-2403), so it vanishes in a plain build --
with `clear_tileram` and `port_l2.c` both cleared by build, NO `sh_src` change in
`c4d02904..HEAD` can reach the plain rom at all.

**WHAT IS LEFT IS `tools/cport_build.sh` (9eb95fb0)**, and its own commit message names the
mechanism: the undeclared-symbol harvest matched only gcc's curly quotes, so in a non-UTF-8
shell it harvested nothing -- *"it stayed hidden for a week because
build/port/sh_src/portfull/altbeast_port.c was never regenerated; the failure only appears on
a CLEAN port build."* If the port unit had been building from a stale generated source and
started regenerating inside this range, the port's CODE changed under us and a pace move is
exactly what that looks like. THE DECISIVE TEST, one build: check out `c4d02904` into a
worktree, clean the port dir, rebuild, run check-demo. If it reads 4 of 20 / x1.018 then
there was never a regression -- the 2 of 20 was measured on a rom built from stale source,
and that would put a week of numbers in question. I did not run it.

## 0c. The A 0140 regression is NOT the clear_tileram hand file (2026-10-11)

Control built at the same HEAD (fc18affa) with `hand/clear_tileram.c` taken out of the swap,
so the generated body goes back in. Plain flags, both roms named by build_id:

| | rom | port clock | check-demo | A 0140 |
|---|---|---|---|---|
| with the hand file | d334f8b0 | x1.018 | 4 of 20 | frame 807, aw_A_784.bin, **96 bytes** |
| CONTROL, no hand file | 2a9b11cc | x1.018 | 4 of 20 | frame 807, aw_A_784.bin, **96 bytes** |
| before, for reference | f6f2f68a | x1.031 | 2 of 20 | frame 811, aw_A_784.bin, 0 bytes |

Identical: same clock, same count, same port frame, same byte count, same slot list
(`0:1 1:3 e1:4 2:3 e2:4 3:3 e3:4 4:3 e4:4 5:3 e5:3 8:13 e8:1 17:4 18:1 19:4 20:1 40:1 62:8
e62:8 63:12 e63:8`). **The hand file is cleared.** The regression and the x1.031 -> x1.018
clock move are both upstream of it, somewhere in f6f2f68a..fc18affa.

WHAT `port clock` MEASURES, since it is the other half of this: `check_demo.py:calibrate`,
`STRETCH = 400.0 / (tick@1100 - tick@700)` -- emulator frames per GAME TICK, read from the
game's own counter at two frames inside demo A. It is the pace itself, not a proxy, so it
cannot move without the port advancing a different number of game frames. x1.031 -> x1.018 is
about five fewer game frames per 400 emulator frames.

EVERY CODE CHANGE IN THAT RANGE IS SUPPOSED TO BE INERT IN A PLAIN BUILD: the `pp_*` counters
and the `FUN_0000258a` increment are inside `#if defined(PORT_TILEDIRTY)`; the Makefile `+=`
expands to nothing with PORTTRACE unset; the port_l2 watch folds away because `L2M_WATCH`
defaults to 0. SUSPECT, NOT A CONCLUSION: `f1dc1397` adds a new `static byte
watch_live_armed` to port_l2.c and moves the `#ifndef L2M_WATCH` block, which shifts the port
unit's layout -- and this repo has already measured that 64 bytes of dead `.data` moves the
level-1 ladder 18 points. A layout lottery would explain a pace move with no logic change.
Bisecting f6f2f68a..fc18affa is the card; I did not start it.

## 000. marks2 FIXES IT AND KEEPS THE SPEED (2026-10-12)

`rom/port/marks2.32x`, sha1 f868b509, commit `0c8955d4`, built AFTER the commit so the stamp
names the code.

| | clean1 | marks1 | **marks2** |
|---|---|---|---|
| check-demo | 4 of 20 | 2 of 20 | **2 of 20** |
| flips per 100 vints | 12 / 32 | 45 / 48.5 | **46 / 48.5** |
| updates per 60 | 11 / 29 | 29 / 31 | **29 / 31** |
| longest hold | 8 / 6 | 3 / 3 | **3 / 3** |
| tile-RAM parity | 20,480/20,480 | 20,480/20,480 | **20,480/20,480** |
| dirty census | 0/0/0 | 0/0/0 | **0/0/0** |
| the cut at tick 0016 | clean | **BLACK+PURPLE** | **clean** |

No differing live record on any of the three. Speed columns matched on the GAME's own
(step, tick) -- marks2 f680 tick 00D6 against clean1 f700 tick 00D6, and f1768 against f1800
both tick 008E -- never on emu frame.

**WHAT I GOT WRONG AND HOW IT WAS CAUGHT.** I reported "not the marks and not the page set"
because a 5x superset cannot under-ship a page. That test was wrong: the superset does not
delay the PAGE, it spends the ART budget. The mirror was identical on both roms in the bottom
band -- plane A rows 24-27 read 39-40 stale and plane B 38/19/11/7 blank on BOTH, and neither
converges -- so the tags were never the difference. **61 of the 69 VRAM slots those rows name
held different art.** Same tags, different pixels. Comparing the two roms' VRAM is what found
it, after comparing their mirrors had said "identical" and I had already published a
conclusion from the mirror alone.

## 00. THE GLITCH FRAME, BY EYE (2026-10-12)

**marks1 emu 486, step 0C key 0947 tick 0016** -- the first demo-A frame after the cut. Black
and purple blocks across the BOTTOM tile rows where the grass and ledge belong. clean1 at the
same tick (emu 499) is clean there: grass present, one isolated black cell, nothing else.

`nt_audit` at that exact frame names the layer:

| plane | row 24 | row 25 | row 26 | row 27 |
|---|---|---|---|---|
| B (BG) | ok 2, **blank 38** | ok 21, blank 19 | ok 29, blank 11 | ok 33, blank 7 |
| **A (FG cat-0)** | ok 1, **stale 39** | ok 0, **stale 40** | ok 0, **stale 40** | ok 0, **stale 40** |

**PLANE A'S BOTTOM FOUR ROWS ARE ENTIRELY STALE, and the BG under them is BLANKED.** That is
the PURPLE BACKSTOP at `m_main.c:18596` doing exactly what it is written to do: for
`row >= 24 && fgcov` it blanks the BG cell so a miss falls through to the backdrop. It blanks
on the assumption that the FG covers -- and here the FG is stale, so what reaches the screen is
wrong FG tiles over a blanked BG. The backstop's own comment already names the failure:
*"an opaque FG cell that was REFUSED a slot is not on the MD, so do not blank the BG beneath
it -- that is the black tile."* The purple is that comment's own tile 0x12D/pal1.

**SO THE VISIBLE DEFECT IS PLANE A, ROWS 24-27, CLASS STALE** -- the same class as the +43 at
the steady-scroll tick, but concentrated and total in the bottom band at the cut.

AND THE 43 AT THE STEADY TICK ARE NOT VISIBLE: marks1 f1756 against clean1 f1800, both exactly
tick 008E, differ by 9.94% of pixels and every one of those differences is the logo's red/white
blink phase, INSERT COIN blinking, and one frame of sprite motion. Both pictures are clean.
A tag-stale cell is not automatically a seen cell, which is what the baseline ~500 have always
shown.

## 0a. marks1's tile glitches: STALE on plane A, and NEITHER proposed mechanism (2026-10-12)

Mike's pass on marks1: *"some tile glitches, but its miles faster"*. Identified before
theorising, both roms read at MATCHED (step, tick) -- marks1 f680 tick 00D8 against clean1
f700 tick 00D6.

**`nt_audit`, the only trustworthy cell->tilemap mapping:**

| | plane B | plane A |
|---|---|---|
| clean1 | ok 1046, blank 74, **stale 0** | ok 611, **stale 509** |
| marks1 | ok 1046, blank 74, **stale 0** | ok 568, **stale 552** |

**Plane B is IDENTICAL. Plane A carries 43 MORE STALE CELLS on marks1.** The class is STALE --
a cell naming a slot whose tag no longer matches what it wants -- not TORN and not FOREIGN.
`shipped == wanted` (493 of 493) on both, so the art is present either way.

**BOTH PROPOSED MECHANISMS ARE RULED OUT BY MEASUREMENT.**

1. *Early marks tearing a page.* Staging against truth per page at the cut: marks1 lags one
   frame across all 13 pages at emu 461 and has CONVERGED by 464. clean1 at its own cut is
   still showing 11-13 pages at +6 frames. **marks1 converges FASTER, not slower**, and no page
   matched neither the old nor the new content once page 12 -- the documented blank, ~780 words
   every frame on both roms -- is excluded. There is no tearing.
2. *Re-upload flood starving the real pages.* The over-marking is REAL and large: pages MARKED
   against pages whose content actually MOVED is **78 against 14 at the cut (5.6x)** and 78
   against 16 over demo B (4.9x); steady scroll is 0 against 0. But a 5x SUPERSET cannot
   under-ship a page, and the staging census above shows it does not arrive later.

So the regression is **downstream of the page set**, in whatever consumes `pg_pending` to decide
which CELLS to re-ship -- not in which pages are marked. That is where the next read goes, and
it is a different question from the one this card started with.

## 0b. The page-2 mark (2026-10-11)

**Rom 53ab3fbd**, the `PORTTILEDIRTY=1` probe. Addresses from THAT build's own
`build/port/rom/s16.lst` (`sdram_map` does not know `pp_*` or these text symbols).

PC PROFILE, `ares-headless --profile`, instructions at emu 480 minus at emu 440:

| fn | lst range | at 440 | at 480 | delta | ran? |
|---|---|---|---|---|---|
| `FUN_00002572` | 0238F034-0238F05C | 16 | 16 | **0** | no |
| `FUN_0000258a` (hand) | 0238EDDC-0238EFE8 | 21,966 | 21,966 | **0** | no |
| `FUN_00002552` | 0238EFE8-0238F014 | 0 | 0 | 0 | no |
| `FUN_00002564` | 0238F014-0238F034 | 12 | 12 | 0 | no |
| `FUN_00002580` | 0238F05C-0238F07C | 0 | 0 | 0 | no |
| `tilemap_unpack_hi` | 0238E7A8-0238E904 | 0 | 196,653 | **196,653** | **YES** |
| `tilemap_unpack_lo` | 0238E904-0238EA80 | 0 | 267,247 | **267,247** | **YES** |

**Outcome (c): none of the four rectangle-list functions ran in the window; BOTH unpackers
did.** The write that changed page 2 at the E -> A cut is the scene load, not a blit.

`FUN_00002572`'s four call targets, `objdump -d` of the probe ELF: all four are
`jsr @r8` with `r8 = 0x0238EDDC = _FUN_0000258a`, the hand body. There is no second copy of
the blit and no wrong body holding the mark.

**THIS OVERTURNS THE ELIMINATION RESULT ABOVE.** `FUN_00002572`'s geometry (two 20x20 at
0x2400/0x2428 = 800 cells) matched the measured 800-word change exactly, and its two pages (2
and 7) are exactly the never-marked pair -- and it still did not run. The 800-word diff was
the SPARSE DIFFERENCE of a full-tilemap rewrite, which is the alternative I raised and then
leaned away from because the geometry was such a clean match. A geometry match is not an
execution proof; the profile is.

**WHAT IS NOW OPEN, and it is a contradiction, not a lead:** both unpackers ran, and both mark
pages 0-9 unconditionally at their `done:` label (`tilemap_unpack_hi.c:44`, `_lo.c:54`:
`pg <= (0x4FFF*2)>>12` = 0..9), which includes 2 and 7. Yet the census reports 2 and 7 as
NEVER marked while `mark_d` rose 6 -> 19 over the same window. Either the unpack's mark does
not land, or the census reads it at a moment it cannot see. `pp_marks_p2` says nothing about
this: it counts only `FUN_0000258a`'s mark, and that function did not run.

## 1. What Mike sees

His play pass on `44d1673f`: **the eye is correct, every demo and title scene draws junk tile
planes**, on ares and on the rig. The control draws the same junk AND no eye, so the landing
stands as an improvement and the LINE still fails.

## 2. What the junk is, MEASURED

At frame 700 on static1 (`44d1673f`), via `tools/nt_audit.py`'s replay (its own loop,
imported, totals reconciled: my MATCH 255 == its ok 255, my STALE+FOLDED+OTHER 865 == its
stale 865):

| plane | cells | MATCH | STALE | FOLDED | BLANK | OTHER |
|---|---|---|---|---|---|---|
| A (FG) | 1120 | 255 | 208 | **0** | 0 | **657** |
| B (BG) | 1120 | 17 | 0 | **0** | 51 | **1052** |

- **Sprites are perfect; the planes are the junk.** The FB draws the men, the enemies, the
  text. See `screenshots/tiles-junk/`.
- **Every one of the 1,649 OTHER cells is class (b): the slot is validly tagged, for a
  DIFFERENT tile.** Class (a) — tag wiped, never re-shipped — is **zero**.
- Of those, **1,550 differ in BOTH code and cset**; 99 in code only; **`isfg` never differs**.
- The wanted triple is self-consistent (`cset == code>>6`) in all 1,649. The cells are not
  handed a mangled version of their own triple.
- On screen: **340 distinct 8x8 tiles against the arcade's 726** at frame 700 (376 vs 675 at
  2000). That is the gap to close.

## 3. What is RULED OUT, each by a measurement

- **Way pressure / `MD_TWO_CHOICE`.** 1,216 distinct triples at frame 700 hash to all 128
  sets, histogram `6:2 7:5 8:21 9:30 10:40 11:29 12:1`; 100 sets over 8 ways but only **201
  codes unfittable**, and greedy two-choice recovers **8** of them. Not the mechanism, and
  the ~20 FPGA lines two-choice costs buy 0.7% of the shortfall.
- **The key.** `MD_KEY(code, cset) = (cset<<16)|code`, no mask. Both lookups (`:8151`,
  `:18690`) build it from the full `w & 0x1FFF`. Tag writes (`:18732`, `:18786`, `:8174`) all
  store the full key.
- **The six-bit field in the two-word record.** Of 449 tags at frame 700, all 64 values of
  `code & 63` appear, 426 have a non-zero index, 434 distinct codes, only 19 multiples of 64.
  The low bits are not lost. (The "set bases" claim that sent us here was MY six-sample
  artifact plus the bank fold at `:8143`, which folds `code & 0x1000` by `bank1`.)
- **The fetch.** Occupancy plateaus by frame 490: **1,009 of 1,024 cache slots filled**, and
  demand is 1,151 codes. The art is loaded. (Slots 1024-1407 are `MDSPR_TILE0`'s sprite art,
  not cache — that is where my earlier "1,242 loaded" came from.)
- **The missing dirty mark.** `mdp_wipe_set_tags` (`:3523`) is the ONLY tag-changing site
  that does not `MD_MARK` — real, and NOT the fix: adding it (rom `3044d0bf`) took plane A's
  MATCH **255 -> 189** and on-screen tiles to 356. Committed as `3dfd4e07`, one edit to
  revert.

## 4. The open question

**1,009 slots hold art, 449 carry a tag, 572 are filled with `tag == 0xFFFFFFFF`.** Art is
loaded into slots nothing claims, while 1,649 cells point at slots reclaimed under them. The
tag table and VRAM disagree about what is loaded. Nobody has explained that gap, and it is
where I would start.

## 5. The instruments, all proven in use

| tool | what it gives |
|---|---|
| `tools/nt_audit.py` + `tools/nt_dump_ares.py ROM FRAME DIR` | the walk replay: per-cell wanted triple, shipped slot, tag. The ONLY trustworthy cell->tilemap mapping; do not write a second walk |
| `tools/anchor_nine.py A.32x B.32x` | nine anchors at matched GAME frames. Retired as a demo verdict (mean luma 65 is junk the same as 83) but still the static-scene gate |
| `tools/anchor_align.py ROM --anchor N` | one anchor. Eye pan (1360) is filed BROKEN for every rom — our pan leads the arcade's at the matched game frame |
| distinct on-screen 8x8 tiles | the honest picture metric here: arcade 726 at 700, 675 at 2000 |
| `make check-demo PICTURE=all` | the gate. 2 of 20 is a PASS when both are named NOT REACHED classes (`tests/demo-b/README`), never on a differing record |

## 6. Gate for any candidate

measurement 3.1 MATCH per plane at 700 and 2000 (target: on-screen cells minus the ~201
pressure overflow), distinct on-screen tiles against 726 / 675, `make check-demo PICTURE=all`,
the 4:3 frames beside the arcade in `screenshots/tiles-junk/`, and v/gf at the cut and demo 2
for the price. Both roms: the control carries the same junk, so a fix must be tried on it too.
