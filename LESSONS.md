# LESSONS — findings that must never be re-derived

**Each entry is a rule, the measurement that established it, and what it
cost when it was violated.** If you are about to propose something, check
this file against it first.

Add to this file only when a lesson cost real work. Do not add
speculation.

---

## About the instruments

### ares and the rig disagree about WHICH CPU the wall is on

**ares is SLAVE-gated. The rig is MASTER-gated.** On ares the wall is
`max(echo, mtask)` and echo wins, so removing master instructions cannot
move the ares number by construction.

*Cost:* every card sized from LOOP29 275's phase split aimed at the wrong
processor. NT_SKIP was shelved on an ares ranking (LOOP29 177) three days
before the correction (LOOP29 294) that changed its value.

### ares charges INSTRUCTION COUNT, not memory or fetch

No SDRAM waits, no uncached waits, no data cache, **no instruction
fetch**. So every size-for-count trade in this renderer's history
measured as a win on ares and may have been a loss on hardware.

*Corollary:* memory-traffic cards rank on the rig's frame probe. ares
keeps exactness.

### PROFILE GAMEPLAY, NOT ATTRACT

`bm_scan_baked_ok` requires `MD_STATE_PLAY` (or attract steps 1/3/5 with
bit 8). Attract fails it, so the live scan runs there **and only there**.

*Cost:* every number produced in the week of 2026-09-16 came from an
attract window. Two cards were costed against work that does not run in
gameplay. `bm_scan_rows` is 27.6% of `_m_main` on attract and **0.00%**
in play.

### A union over N frames is not a working set

The cache sees ONE generation. A 200-frame union reads 21,856 B; a
2-frame window reads 14,256 B; ten frames adds 1 KB.

*Cost:* a "1% cold tail" of 13,168 B was proposed for relocation. At a
2-frame window essentially all of it is already touched — there is no
cold region.

### A documented counter index may have NO WRITER AT ALL

`mdalloc_id[3]` is documented at its declaration as "on-screen cells
naming its slots" and **nothing ever assigned it**, from LOOP29 158 until
2026-09-17. It reads 0 because `.bss` is zeroed. Every reading of it was
a measurement of nothing, and one of those readings was used on
2026-09-17 to "falsify" a hypothesis — a conclusion drawn from a counter
that did not exist.

This is the ninth instrument in this project caught lying and the second
FOUND BY READING THE SOURCE rather than by a contradictory number. A
collision at least produces a wrong value you can catch with the subset
rule; an unwritten index produces a plausible zero that nothing catches.

*Rule:* before quoting a counter, `grep` for an ASSIGNMENT to that exact
index, not for the index. `mdalloc_id\[3\]` appears twice in the file —
once in a comment promising it, once now writing it.

*Cost:* one build cycle and one wrong conclusion, caught the same tick.

### Counter indices collide. THERE ARE NOW REGISTRIES — READ THEM

    m_main.c:72     DIAG. 64 slots and the block is FULL
                    (0x28000..0x280FF; BM starts at 0x28100).
    m_main.c:718    mdalloc_ctr. 48 slots.

Written 2026-09-16 from a preprocessor-aware census: each site's guard
evaluated against the flag set, so "LIVE" means live ON THE LINE.
(First run used `.build_flags`, which turned out to be a PROBE's stamp —
see the `.build_flags` entry below. Re-run against the true line stamp
after `make line`: identical verdicts, because no counter site is
guarded by any of the four flags that differed.)
**No DIAG slot is free in every build.** A new cumulative counter goes
in `.bss` the way `mdalloc_ctr` does — not into a slot you picked.

What the census found: `DIAG[36]` is the nearest-colour fallback counter
AND `r60_pkt_flip`, which XORs it every gap — plus NINE more live
collisions ([35] [37] [38] [39] [42] [50] [51] [52] [53]), two of them
four-way. On the MDA side the collision was **six slots wide**, not two:
the NOTES 51 batch census took [16]..[21] from the allocator, including
`mdp_claim_pen`'s pen-starvation trio. Fixed — census moved to
[32]..[37].

*The detection rule that DID NOT work:* "a subset counter may never
exceed its parent". `MDA[20] = 23,038` against `MDA[19] = 218` was read
as structurally impossible. It is not: both belong to the shipper, and
[20] is a WORD count against [19]'s CHUNK count — ~105 words per chunk.
The collision was real and the rule pointed at it, but by luck. **A
ratio is only a subset violation once you have checked that the two
counters count the same UNIT.**

*Cost:* two debugging cycles before the registries existed, and one
false "mdp_claim_pen writes no MDA slot" during the census itself.

### Before blaming trajectory divergence, READ THE SCENE TIMER

`LAYOUTPROBE` established that 64 B of dead `.data` moves the speed
ladder 18 points, and the memo explains it as *"a build that falls
behind is measured on different content, because the input script is
frame-indexed."* True — and on 2026-09-16 it was applied to a case where
it did not hold, which nearly buried a real result.

Dropping the TAGKEEP family removes 1408 B of `.bss` (`_end` 0x6016e38
-> 0x60168b8), 22x the LAYOUTPROBE step, and the f2000/f3000/f4000
captures differed a lot. That was written up as trajectory divergence
and the card was parked. **It was not divergence.** The 68K scene timer
(WRAM `0xFFF02A`, one tick per GAME frame) reads 874 / 1224 / 1574 /
1924 / 2174 at f1500..f4100 for BOTH roms — identical at every point,
0 game-frames apart at every shot — and `irq4_miss_pct` was 0.0 for
both. The flags are SH-2 RENDERER flags; they never touched the 68K
timeline, so nothing could diverge.

*The test, and it is free:* `night_run` already records `0xFFF02A` at
all five window frames. Interpolate it to the shot frame for each rom
and compare. Same game frame = comparable captures, whatever the image
sizes are. Different = do not compare the pictures.

*Rule:* rom size predicts NOTHING about comparability on its own. The
scene timer measures it directly. Read the timer.

*Also check phase before believing a frame pair:* shoot N, N+1, N+2,
N+3. Here both roms were stable across four consecutive frames (the
lives-region held 90 distinct colours on the line and 335 on the
candidate at every one), so a one-vint render-phase offset was excluded
too.

*Cost:* one tick spent parking a card that had actually produced a
result, plus a LESSONS entry that had to be withdrawn.

### The HUD is NOT on the colour-set allocator — stop taking it there

Two ticks on 2026-09-16/17 went into the MD residency allocator looking
for why the line drops the gameplay HUD. It cannot be there:

    m_main.c:4132  "HUD text class: fixed home in group 0. The boot pin
                    is PERMANENT -- no allocator/steal/evict loop ever
                    touches index 0 (they all start at 1 or bound)"

Measured, and agreeing: at f3000 (gameplay) every candidate colour set
(40, 41, 44, 45, 46) is UNASSIGNED with an identical `mdp_s_line`,
`mdp_s_used`, pen map and line-0 colour table on BOTH builds. The
allocator state does not differ at all between the build that draws the
HUD and the build that does not.

*Where it must be instead:* the text path — `text_grp[par][0]`, and
group 0's special paint at `m_main.c:4830` ("entry 0 IS the through bit
-- paint pens 1-7 only, never the base entry") — and, for the lives
portrait, the MD sprite path.

*Rule:* a pinned resource is exempt from the allocator that manages the
unpinned ones. Check for the pin before costing an allocator theory.

### black_pct does not resolve the black-tile defect

At f4000 the candidate frame carries a large solid-black silhouette blob
over the creature that the line frame does not, and `black_pct` moved
from 4.1 to 4.2. A whole-frame black fraction is swamped by legitimate
black art; the defect is a share of CELLS.

*Rule:* `black_pct` is a guard, as `night_run`'s own header says
("Guards only. LOOK AT THEM"). It is not the instrument for a black-tile
card. Look at the frame, and if a number is wanted, count cells.

### A flag you have never built OFF is not a flag, it is an assumption

`mdp_wipe_set_tags` was DEFINED inside `#ifdef TAGKEEP` and CALLED from
`mdp_assign_set` under `MD_STATIC && MD_ROUND && !ASSIGN_NOWIPE` — none
of which imply TAGKEEP. So every TAGKEEP-off build from LOOP29 155 until
2026-09-16 died with "implicit declaration of `mdp_wipe_set_tags`".

**"Drop TAGKEEP" sat in the card list as READY TO BUILD the whole time,
and it had never once compiled.** A card is not ready to build until the
build has been attempted; "it is one flag" is a claim about the
Makefile, not about the source.

Two more things hid in the same card, both found by reading guards
before building:

- `PENHOLD` needs `TAGKEEP` and `PENREPAINT` needs both — the Makefile
  SAYS SO IN A COMMENT and does not enforce it. `PEN_REPAINT` and
  `PEN_HOLD`'s second site are nested inside `TAGKEEP` and die with it,
  but `PEN_HOLD` at `m_main.c:2622` is guarded by `PEN_HOLD` alone and
  SURVIVES — leaving the pen-release skip with nothing to serve and no
  release timeout. `TAGKEEP=0` alone builds "drop TAGKEEP and keep the
  pen leak".
- The falsifying counters `MDA[22]`/`[23]` are outside the `[16]..[21]`
  collision range, so the falsification is clean. Worth checking, since
  six neighbouring slots were double-booked.

*Rule:* before costing a flag removal, (1) find the flag's DEFINITIONS
as well as its uses — a definition inside the block and a call outside
it is a compile break waiting, and (2) walk the guard stack of every
site, because a family's members do not all die together.

*Cost:* one build. Cheap only because the failure was immediate; a
partial family that COMPILES would have produced a measured number for
a configuration nobody designed.

### A grep for an array index misses the macro form

`grep 'MDA\[\|MDA_ADD'` finds `MDA_ADD(20, ...)` and misses `MDA(19)`
entirely. That single blind spot is why the `mdalloc_ctr[16..21]`
collision survived: the batch census (db8d834) took six slots the
allocator (eea4cf8) already owned, and a census run on 2026-09-16
initially reported "mdp_claim_pen writes no MDA slot" — the exact
opposite of the truth, and it was one keystroke from being written into
a registry as fact.

**Search for the ACCESSOR, not the array.** `MDA(`, `MDA_ADD(`,
`mdalloc_ctr[` — all three. Same for any counter reached through a
macro.

*Cost:* caught before publication only because the claim contradicted
`STATE.md` and the contradiction was chased instead of assumed.

### `.build_flags` is the LAST BUILD, not the line

At the start of 2026-09-16 it carried `NT_SKIP`, `NT_KEY8`, `BOOT_VALUE`
and `BOOT_FLIPRATE`. None are in the Makefile's `LINE_FLAGS`. It was the
stamp of somebody's probe, and `STATE.md` said "the authority for what
is on the line", so a whole session's premise checks were run against a
probe's flag set.

    the line          `LINE_FLAGS` (Makefile:2890) / `make line`
    what is built     `.build_flags`
    are they equal?   check before trusting any measurement taken
                      against rom/s16.32x

`STATE.md` also claimed `rom/s16.32x` was "bldS-equivalent (stamp bytes
only)" at session start; it was not verified, and `make line` had to be
run to make it true.

*Cost:* nothing this time — no counter site was guarded by any of the
four — but only because it was checked. Corrected in `STATE.md`,
`CLAUDE.md` and `LOOP-PROTOCOL.md`.

### `make -n` REWRITES .build_flags

The `FLAGSTAMP` rule is a `$(shell ...)` at Makefile PARSE time
(`Makefile:2823`), so it fires on `make -n`, on a `make` that builds
nothing, and on any `make` whose goal is unrelated. `make -n ship-us
FBXPORT=1` silently replaced the line's stamp with a reduced `-D` set —
the stamp no longer described `rom/s16.32x`, and the next real build
would have rebuilt every object against it.

**To read flags without touching the build: save the file, run, restore.**

    cp .build_flags /tmp/bf.save; make -n <goal> ... ; cp /tmp/bf.save .build_flags

`.build_flags` is gitignored, so there is no `git checkout` to recover
it. If it is lost, the only authority left is the Makefile's
`LINE_FLAGS` (`Makefile:2890`) and a rebuild.

### An LTO object is not a byte-diff instrument

`-flto` stores GIMPLE, and GIMPLE carries line numbers, so inserting a
COMMENT changes the object bytes. Proving "this edit changed no code"
needs `-S` with `-flto` stripped and the `! <n> "file"` inline-asm
markers filtered out. Done that way, a 200-line comment insertion
produced identical SH-2 assembly.

### grep is not a free-space test

Live code reaches scratch through base pointers. `0x26028DE0` and
`0x26028DA0` both had zero literal references and both were live.

**The authority is the memory map comment at `m_main.c:1523`.**

*Cost:* two builds.

### Stale state reads as live state

`mdp_free_set` clears `mdp_line_c` and does NOT clear `mdp_pen_own`.
**Never read `mdp_pen_own` without masking on `mdp_line_c != 0xFFFF`.**

*Cost:* a regression was reported as an improvement.

### A metric can measure its own failure

"Non-field centre 83.3%" was a classifier keyed on the arcade's palette.
Once our palette was wrong, every pixel read non-field **by
construction** — the number measured the classifier failing, not
coverage.

### Eight instruments lied in one arc

Raw-DRAM histogram read as a decoded framebuffer; `DIAG[36]`; a corrupted
scratch block; two dead scratch addresses; `.bs1` vs `--dump`; a wrong
base address; the classifier above; `MDA[19]/[20]`.

**Assume a new number is wrong until its instrument is checked.**

## About reasoning

### A single counter-example is a reason to ask WHICH CASE, not to withdraw

The claim "cyclers own no pens and are never repainted" was withdrawn
because one pen (14) tracked correctly. That pen was **sole-owned**, and
`m_main.c:16281` gates the in-place recolour on `mdp_pen_rc == 1`. The
counter-example was the clue.

*Cost:* a correct mechanism was abandoned and a build was spent.

### The census answers the question you asked; the picture answers the one you didn't

Ten exchanges of pen/owner/colour/mask censuses on one screen. **Nobody
looked at the frame.** One look at f1575 closed the entire palette path.

### Check the premise, not the arithmetic

Six mechanisms died in one week. **Every one was arithmetically correct
and built on an unchecked premise** — a plane label, an occlusion
direction, an array's length, an attract profile.

*Rule:* before a card, state what the record already says and read BOTH
`.build_flags` (what is built right now) and `LINE_FLAGS` in the Makefile
(what the line is). **They are not the same thing.** Both times that was
done, it killed a card before it cost anything.

### Docs written as proposals get re-proposed

`ARCHITECTURE.md §4` says "Adopt Model B". It shipped weeks ago. A full
costing exercise was one message from being spent on it.

*Rule:* state lives in `STATE.md`. Narrative docs are history.

## Hardware and game facts (derived from ROM/RTL, will not change)

### The bar is a threshold

60 Hz is 100% single-vint. Ships are vint-quantised, so 1.2 v/gen still
costs 2 and flips at 30. Nothing is paid until the wall crosses below
1.00 — then it all arrives (15% → 98% in one ablation).

### The camera moves at most 0.5 px/frame

Table at rom `0x1878`, 8 words, indexed by `active_enemies` clamped to 7,
max entry `0x0080`; math at `0x39CE` gives px/frame = v/256. **The only
non-zero writer of `FFF158` in the binary** — the other three write zero.

So a new 8-pixel tile column enters the viewport **at most once every 16
frames**, and with enemies on screen once every 32-512, or never.

### Vertical scroll never changes

`world_yposition` has four writers and all four write `#$1020`.
`unk_FFF0E6` has one writer, `#0`. Y scroll is the constant `0x20` on
both planes for the entire game.

### Tile RAM has no in-play writer

`m_main.c:3393` / LOOP-DECOMPILE 99. The tilemap is static during play,
which is what makes caching the scan **safe**, not merely fast.

### The transformation screen

`sub_3A00`'s cutscene branch (`0x3A0C-0x3A22`) zeroes all four scroll
registers and selects pages 10/11 (`#$AAAA`/`#$BBBB`). Static by
construction. `glow_chev` (`m_main.c:14814-14844`) is the exact gate —
"any page nibble >= 10". **Do not write another.**

The face itself is a four-palette **zoomed sprite** built by `sub_90F4`
(`0x90F4`), not tiles. Pages 10/11 are the backdrop behind it.

`0xFFF148` is an **object marker, value = slot + 1** (dispatcher
`0x398E`) — **not** a scene flag, despite `m_main.c:6805` calling it
"the game's own cutscene byte". That contradiction is unresolved and is
load-bearing for `mds_onscreen`.

### Palettes are QUEUED, and the arcade queue has no budget

`RequestPaletteUpdate` (`0x3B2E`) does not write a palette; it queues an
entry and `0x3C5A` drains **the entire queue every vblank** with no cap.
**Any partial landing on our side is ours.**

Each entry writes **14 colours / 28 bytes** to `dest + 2`. **Colours 0
and 15 of every slot are NEVER written by the game** — anything there is
our own initialisation and is not evidence.

### Cat-1 is on the FOREGROUND plane and is never tile-occluded

Pages 0-4 are the foreground (`snap[0]`, `m_main.c:3103`/`:15042`,
`pagesel_census.txt`). `jts16_prio.v:83-95` tests the foreground first,
so no tile plane is above cat-1. **Tile-occlusion of cat-1 is zero.**

*Cost of the inverted label:* six entries of occlusion percentages, all
void.
