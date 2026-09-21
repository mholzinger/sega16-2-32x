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

### I matched a known defect's SHAPE and skipped identifying the sprite

At f4000 the candidate frame has a large solid-black shape over a
creature and a red mass where a wolf is. I reported both as the SILH
silhouette-fallback defect and escalated them to Mike as the COST side
of a trade. They are neither: the black shape is an enemy in its melting
death animation and the red mass is a red hellhound. Both are correct
art. Mike: "good colors on all sprites and backgrounds."

The rule that would have caught it was already in this file, written two
ticks earlier — locate the visual and say what it IS before proposing a
mechanism. Knowing the rule and citing the rule is not applying it.
A known defect's SHAPE is the weakest possible evidence, because that is
exactly what legitimate art in the same genre looks like.

*Consequence:* a clean improvement was escalated as a trade, which is
the kind of framing that gets a good build shelved.

*Cost:* would have been a shelved build; caught because the frames went
to Mike rather than only the numbers.

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

### FMGATE fits SHORT, RARE writers. Check the caller and the volume.

2026-09-17, cost: a rig launch and a red screen. `0x9052` was FM-gated
because LOOP-DECOMPILE 131 called it "the transformation-only playfield
text clear ... in nobody's marked set", which reads like a rare
one-shot. It is not. It has two callers and the live one is in the MAIN
LOOP:

    988: tstw 0xfffff148     ; object marker
    98c: bnes 0x996
    98e: jsr  0x3aae         ; credit line (already a TXTWRAM writer)
    996: jsr  0x9052         ; the clear -- every pass an object holds

and the routine writes 20 rows x 20 longs = 1,600 bytes. Gating it holds
FM=0 across that clear on every one of those frames, the SH-2 never gets
the framebuffer, the 32X layer never composes, and the bare MD backdrop
shows through: Mike got a solid red screen.

The precedents I copied are all short and rare — the round-clear
typewriter is one glyph per 5 frames, round-clear-all runs once at a
round end. **Volume x frequency is the thing that matters, and neither
is in the description of the routine.**

*Rules:*
1. Before gating a site, find its CALLERS (scan for bsr/bra/jsr to it)
   and count the bytes it writes. A per-frame kilobyte is not a
   candidate for an inline gate.
2. A recorded description tells you what a routine IS, not how often it
   runs. Read the caller.
3. For a long or frequent writer the mechanism is staging
   (`TXT_WRAM_WRITERS`), not gating — but note TXTWRAM has its own
   recorded cost (LOOP29 237: halves the rig frame rate), so neither
   tool is free.

*If it is retried:* granularity, not mechanism. `0x3A9A` is the
precedent — "moveb copy loop head (dbf re-enters gate)". Gating the
per-row setup at `0x905E` (`movew #19,%d2`, 4 bytes) gives 20 windows of
80 bytes with FM released between rows, instead of one 1,600-byte hold.
UNVERIFIED.

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

### map[0] MUST stay 0: MD colour index 0 is transparent

2026-09-17, cost: a build that turned the attract sky PURPLE.

md_emit_art's BG variant maps pixel value 0 through map[0], and I read
that as "BG needs a real colour at pen 0" because the FG variant forces
pixel 0 to transparent explicitly. Backwards. On the MD VDP, **colour
index 0 in a tile is transparent** -- so map[0] = 0 means pixel 0 renders
as backdrop, which is what the BG wants and what the bake already did.
Pointing map[0] at a real slot makes those pixels OPAQUE, and the sky's
transparent pixels became that slot's colour.

The record already said it and I read past it: "Colours 0 and 15 of every
slot are NEVER written by the game -- anything there is our own
initialisation and is not evidence." I used that never-written colour 0
as the TARGET to find a nearest match to, so the match was against
garbage and then painted on screen.

*The real finding, inverted:* the 5-byte divergence between the baked pen
map and the live one is the RUNTIME claiming a pen for index 0 on sets
92/93/95/96/97 -- the sky and trees. The bake is right; the runtime is
wrong there, and it is a candidate cause of colour defects in exactly
those sets rather than something to reproduce.

*Rule:* before "fixing" a difference between a bake and the runtime,
establish which side is CORRECT. I assumed the runtime was the reference
because it was the observed behaviour.

### The palette and pen math is CORRECT. Stop re-measuring it.

Established 2026-09-17, all against the arcade at a MATCHED scene (the
gated captures in discover/cram/wide vs discover/cram/arcade):

    game palette vs arcade      2047 / 2048 entries exact
    baked CRAM reaching MD CRAM   47 / 48 slots exact
    tile conversion vs live VRAM 941 / 941 slots exact

The one palette entry that differs is palette 3 entry 6 (arcade 100F,
ours 7FFF) -- an entry we never write.

**The colour cyclers are correct, including their quirks.** Palette 9
was reported as "two steps out of phase" from a SINGLE sample. Measured
over time, ours and the arcade both advance exactly 2 positions per 20
frames, same direction, and BOTH show the same anomaly at position 6
(last entry 4C00 instead of continuing the ramp). A one-frame offset
between two dumps says nothing about phase; only a time series does.

*Consequence, and it is the useful part:* a wrong picture is NOT a
palette problem. Colour, pen maps and tile conversion are all verified
against the arcade or against live VRAM. Look at which tiles land in
which cells, or at the sprite layer -- the two worst attract scenes
(face, eye) are sprite-driven, and the transformation face is a zoomed
SPRITE, not tiles.

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

### An `.incbin` blob needs `.section name, "a"` or objcopy drops it

`.section .tilesmd` without the `"a"` flag makes a section with
`CONTENTS, READONLY` and **no ALLOC and no LOAD**. `ld` keeps it in the
ELF and places it exactly where the linker script says -- every
placement ASSERT passes, the map looks right, `_end` is right -- and
then `objcopy -O binary`, which is what actually builds the cart, SKIPS
it. 425 KB never reached the ROM.

`sprbake_data.s:13` and `md_sprart_data.s:7` both carry the `"a"`.
Ours did not, and nothing in the build said so.

*What made it invisible, and this is the real lesson:* `md_emit_art`
falls through to the converter when a set has no baked block, so that
the bake "can never render less than the converter". With the blob
absent it read 0xFF gap fill, every index was `0xFFFF` = not baked, and
it fell through **every time**. The feature was perfectly inert. It
therefore PASSED every gate that compared it to the line -- VRAM
identical, pixels identical -- because it WAS the line.

*Cost:* an evening. Two rig builds handed over and played, a rig verdict
taken on them ("slightly, but nothing significant"), a halfword
transport optimisation gated at 0 VRAM / 0 pixels against dead code, a
claim to Mike that 4 VRAM slots differed "where the bake is deliberately
correct", and a cart-DMA probe aimed at 0xFF filler. All void.

**A GATE THAT COMPARES A FEATURE TO THE BASELINE CANNOT DETECT A
FEATURE THAT DOES NOTHING.** Any build flag that adds data to the cart
needs a gate that asserts THE DATA IS IN THE CART IMAGE -- find the
blob's bytes in the `.32x` file -- before any behavioural gate runs.
That is now GATE 0 in the tilesmd script. The same hole exists for
every other `.incbin` blob and for any flag whose failure mode is
"quietly does nothing".

Related: free cart space is `0xFF` gap fill, not zeros, so an absent
blob reads as `0xFFFF` -- which is exactly the "not baked" sentinel.
A sentinel that collides with erased-flash is a sentinel that cannot
report its own absence.

### The MD VDP cannot DMA from cart ROM at RV=1 (CONFIRMED ON HARDWARE)

Measured 2026-09-18 with a control, which is the only reason it counts.
One vint, two identical 16-word VDP DMAs, same destination page:

    source cart 0x264140 (the baked tile blob)  -> VRAM 0xF800 : ALL ZERO
    source FB   0x85EE00 (the SAT staging)      -> VRAM 0xF820 : LANDED

So it is not the probe, the length, the timing or the destination. The
cart is not a legal DMA source for the VDP while the 32X adapter is
mapping it.

**Corroboration already in the tree:** `mdspr_upload` (md_main.c:449)
reads cart sprite art on the 68K and pushes it to VRAM with a MANUAL
WRITE LOOP through the 0x900000 bank window, not a DMA -- 512 port
writes at a time. Someone hit this before and coded round it without
recording why, which is why it cost a probe to rediscover.

*What it costs us:* the "runtime becomes a DMA from cart to VRAM" design
does not work as stated. A tile record cannot collapse to 2 words
(slot + code) with the 68K DMAing straight from the baked blob, because
the DMA has no legal way to read the blob. Any path still has to stage
the bytes into a DMA-able region first -- which is what the SH-2 does
today into the FB window, and is exactly the copy the bake was trying to
delete.

*What still stands:* the bake itself (the arithmetic is gone, the
transport is a 16-halfword copy) and the 68K's own cart reads, which
demonstrably work by CPU access even though DMA does not.

**CONFIRMED ON THE FPGA, 2026-09-19, WITH AN ON-SCREEN CONTROL.** The
harness that finally settles it runs BOTH sources in the SAME VINT into
DIFFERENT HALVES of CRAM, so there is no comparison across time and
nothing to film:

    entries  0-31  <- 68K WRAM 0xFFA300, filled GREEN  (positive control,
                      WRAM is an indisputably legal DMA source)
    entries 32-63  <- cart 0x200000, 64 identical words (the test)

Digital captures off the rig, three frames:

    green (control) 100.0% / 60.0% / 56.6%   -> THE HARNESS WORKS HERE
    purple (test)     0.0% /  0.0% /  0.0%   -> CART IS BLOCKED

Everything before this was weaker than it looked. Cut 5 flooded from
cart alone and saw no purple, and that was called dead -- but with NO
CONTROL ON HARDWARE, "no purple" could equally have meant the flood does
not work on the FPGA at all. The control also caught a bug in my own
setup: `0x9700 | 0x80` sets BIT 7 OF REG 23, which is the DMA MODE bit
(1 = VRAM fill), so the control landed nothing until it used the same
`(src >> 16) & 0x7F` form as every other DMA in the file. Without a
control that bug would have read as a second confirmation.

So on hardware the transfer EXECUTES and the cart data never arrives; it
picks up garbage that varies per frame, where ares deterministically
reads zero. Same conclusion, different failure signature.

FIVE PROBE CUTS DIED GETTING HERE and every one of them failed in the
READOUT, never in the thing being measured:
  1. reported into WRAM that a ring buffer at 0xFFA200 overwrote
  2. source address pointed at 0xFF gap fill (the blob was not in the
     cart at all -- the .section "a" bug)
  3. read VRAM back immediately after the DMA; ares completes a DMA
     atomically, the FPGA does not, so the readback raced and returned
     undefined data (blue/white flashing)
  4. wrote ONE CRAM entry; entry 63 is contested on hardware, so the
     colour flickered between values the probe cannot even produce
  5. flooded CRAM from an all-0xFFFF source; white is a plausible game
     colour, so a white frame could not be told from the game winning
     the vint

What finally worked: a source whose CORRECT result is both UNMISTAKABLE
and STABLE (64 identical words of a colour the game never floods), and
no readback anywhere. **Design the probe so that the right answer looks
different from every wrong answer, including every way the probe itself
can fail.**

### The 68K CAN read cart on hardware -- BOTH routes -- and ares is wrong about it

Measured 2026-09-19 with a same-frame control (immediate GREEN into CRAM
0-31, cart-read words into 32-63):

    read 0x200000 directly (RV=1 identity map) :  0/32  nothing
    read 0x900000 with bank register = 2       : 32/32  the cart value

Convention, derived from MDSPR_CART_BANK=2 + MDSPR_CART_WINOFF=0xF9100
landing exactly on .mdsprart at cart 0x2F9100:

    cart address = bank * 0x100000 + winoff
    read it at     0x900000 + winoff      (bank -> 0xA15104)
    bank 3 is the resting value every site restores

`mdspr_upload` (md_main.c:449) has always used this window and nobody
recorded why. This is why: the identity map does not serve the 68K for
bulk cart reads, the window does.

**THE ABOVE IS ares ONLY AND THE RIG SAYS OTHERWISE.** Measured on the
FPGA 2026-09-19 with the same-frame control, digital captures, six
frames each:

    bank window 0x900000 + bank 2 : purple 25-27% on EVERY frame
    identity map 0x200000 at RV=1 : purple 9% on one frame of four
    control (immediate green)     : 58-63% throughout

**BOTH ROUTES READ CART ON HARDWARE.** ares reports the exact inverse
(identity 0/32, window 32/32), so ares cannot be trusted on how the 68K
reaches cart, and this class of question must be gated on the rig.

**AND I MUST RETRACT THE JUSTIFICATION.** I wrote here that this needed
no rig test because `mdspr_upload` is on the line and sprite art
renders. That was an inference from code EXISTING, not from code
RUNNING. Instrumented 2026-09-19: `mdspr_upload_pump` executes **ZERO
times in 3000 frames**. It is dead code on the line and proves nothing.
Sprite art arrives by some other path.

Same failure as the wolf transformation: reasoning from what the source
looks like instead of measuring what it does. It cost a black-screened
rig and three builds.

*Why it matters:* with VDP DMA from cart ruled out, this is what makes
the slim pipeline possible at all --

    today    cart -> SH-2 -> FB packet -> VRAM   3 payload copies, 17-word record
    next     cart -> 68K -> VRAM                 2 payload copies,  2-word record
    ideal    cart -> VRAM                        BLOCKED, the VDP will not read cart

Two copies is the floor the hardware allows, and it takes the SH-2 out
of the payload path completely.



### The 68K boot stack sat 184 bytes above the FM-gate thunk table (FIXED 2026-09-20)

Measured with a MAME write-watchpoint and a 68K trace, not inferred.
`md_start.s` put the boot/shim stack at 0xFFBFF0; the generated FM-gate
thunk table ends at 0xFFBF38 (`FMGATE_THUNK_ADDR 0xBCF4 + 290 words`).
The vint handler runs on that stack during boot, ~200 bytes deep:

    working line (lineT)     deepest boot SP 0xFFBF46   14 bytes clear
    any vint path +32 bytes  deepest boot SP 0xFFBF26   thunk tail overwritten

The overwritten words were the shared gate spin's `beq` target; the
game's first FM-gated writer (the screen clear at 0x369C, called from
its boot at 0x900556) branched into zeros, ran off through WRAM and the
void, and the vint kept servicing a dead main thread: black or red
screen, `game_running` set, shim counters advancing, scene timer 0.

**Every slim build, DELIVTEST methods 2/3 and the "one instruction flips
it" bisects were this one defect.** It looked hardware-only because the
slim builds that happened to be even-sized rendered in ares. The stack is
at 0xFF3FF0 now, in the audited free gap, and `md.ld` asserts `.bss`
stays below 0xFF3800.

*Rules:* nothing else goes in the thunk page's tail; a black screen with
the shim's counters still moving means the GAME thread is dead, so trace
the 68K in MAME (`trace file,maincpu,noloop`) before touching the pipeline.

### A 68K read at an address computed from packet data must be bounded (2026-09-20)

The slim fetch computed a cart address from the record's block field.
When the SH-2's converter fallthrough (an UNBAKED set) wrote pixel words
where the 68K expected records, the "block" was pixel data and the read
landed past the 1MB bank window -- in 0xA00000+ I/O and the
0xC00000-0xDFFFFF VDP/PSG mirrors. On silicon a read there locks the
68K; ares and MAME return garbage and carry on. The FPGA showed a total
black wedge (0.0%, one colour), ares rendered correctly.

*Rules:* bound every computed bus address before the access (the fetch
now skips anything outside the blob and counts it); a "hardware-only
wedge" with a computed address in the path is this until proven
otherwise; and a packet with two record formats needs a flag bit, not
two strides (bit 14 of the slot word marks a 17-word inline record).

### Rig captures at wall-clock offsets compare attract PHASES, not health (2026-09-20)

`non-black %` of a capture 37/50/63 s after launch depends on where the
attract is. A build whose 68K vint is a few lines longer paces the
game differently and shows the TITLE SCREEN (7-12% non-black, 40-90
colours, logo and sprites intact) where the line shows the level demo
(99%). Eight "collapse" results in one night were healthy title screens.
A pure delay of ~20 lines per vint still ran the demo.

*Rules:* only a single-colour frame (distinct colours = 1) is a failure
signature; LOOK at the frame; anchor comparisons on the game's own scene
timer (0xFFF02A), never on seconds since launch.

### A probe that never ran reads as a pass: verify the define in `.build_flags` (2026-09-20)

Three "position" results (reads after the game's IRQ4 = fine) were
no-ops: the Makefile block carrying the define had been deleted with
unrelated scaffolding, and once the hook was in a function the line
never calls (`r60_late_post` exists only under POST_LATE). The rig
showed a healthy picture because nothing had changed.

*Rules:* every probe build prints and greps its define out of
`.build_flags` before it is pushed; a probe's counter must be non-zero
in ares before its rig result counts; hook code through a symbol the
LINE's path actually reaches (md_start.s `fmgate_partb`, not a
flag-gated C function).


### The BG palette DMA is vblank-gated at the END of the consume: whatever runs before it can defer the palette for a whole level (2026-09-20)

`md_consume` DMAs the packet's 48 palette words to CRAM 16-63 only if the
V counter still reads >= 0xE0 when it gets there; otherwise it parks
them in a WRAM hold for the next vint's top. The slim pipeline's first
cut ran up to 40 WRAM->VRAM DMAs at the consume's TOP. On the FPGA,
where every adapter access costs more than ares charges, that pushed the
palette gate past vblank on every load vint: the tiles landed (rig value
readout: thousands staged and DMA'd, 0 stray) and the level's pens
stayed black until the Neff cut re-published the palette. Mike called
it from the symptom: "the palette swap at Neff is what triggers the
background". Moving the slim DMA to partb_hook restored the background.

*What is proven and what is not:* moving the DMA out of the consume
restored the background (rig, attract demo, 99.6%). But forcing the
gate on the LINE with a ~20-line delay before it (CARTREADAT=22) only
raised ares' deferrals from 9 to 25 of 1200 vints and the rig kept its
background, so the deferral/replay path itself works on hardware, and
the exact way slim18-20 lost the palette (a burst of WRAM->VRAM DMAs at
the consume top) is NOT established. Do not quote "the gate" as the
mechanism; quote the fix and the readout.

*Rules:* new work in the vint goes AFTER the game's IRQ4 (partb_hook),
not into the consume; "art present, background black" means PENS, not
tiles -- read the rig value instrument (SLIMVALUE) and 0xFFA162 before
touching the tile path.


### Three text/priority defects, one afternoon, all from the arcade's own data (2026-09-20)

- **Zeus message** (partial, never erased): the typewriter at 0x56E8 was
  FM-gated but never MARKED its rows for TEXTCAPMASK. Found by the glyph
  words in STATE (0x200|ASCII), the string in the ROM at 0x7043, and the
  one `lea` that references it.
- **Wolf transform** (character under the flames): a MAME census showed
  the cutscene's FG page 10 has priority on all 800 cells; our punch
  classified an UNBAKED page's priority cell as a whole-cell hole. Per-
  pixel masks from the art now. MDSPROFF=1 was the control that cleared
  the sprite-offload theory first.
- **High-score table** ("broken the entire time"): the writer at 0x4540
  had no gate and no mark. Gated at each STORE (LOOP29 228), not the
  routine: its delay helper sits in the game's frame wait, so a span over
  the routine would have deferred every FM raise for the whole screen.

*Rules:* a text defect is a WRITER SITE question -- census text-RAM
references (tools/patch_report.txt) against the gate table; a "wrong
layer" defect is a PRIORITY question -- read the tile words and the
sprite field in MAME before touching the compose; and run the one-flag
control (MDSPROFF, CARTREADAT) before building a mechanism.


### The FPGA's level-start background follows BUILD LAYOUT, not the feature under test (2026-09-20, evening)

Measured on the rig with the attract demo as the scene (a 99% non-black
frame with the line's colour mix at 37-50 s after launch = background
present; a sky-only blue frame and dark frames = absent), each rom
launched twice where it mattered:

    slim22                         present
    slim22 + Zeus thunk bset #2    ABSENT     (slim23, slim25)
    slim22 + Zeus thunk 1 NOP      present    (twice)
    slim22 + Zeus thunk 3 NOPs     present
    slim22 + Zeus thunk st.b       present    (slim30, the line)
    slim22 + Zeus thunk 5 NOPs     ABSENT     (twice)

A NOP pad flips it, and relaunches repeat the verdict, so the outcome is
a deterministic function of the build's layout, invisible in ares (which
charges instruction cycles only and models no cache), and unrelated to
what the changed bytes DO. Mike's slim25 capture in gameplay (background
black, sprites and gravestone row present) is the same defect.

**Everything blamed on a feature today between 14:00 and 19:30 is
therefore UNPROVEN**: same-vint art landing (slim26/27/28), the
high-score gates (every variant), the palette gate, the DMA position.
Each was one layout. They may all be fine; they may all be luck.

What is measured and stands: DELIVTEST 0 is GREEN on the rig (immediate
WRAM write then DMA lands); SLIMVERIFY is GREEN (same-vint slim art lands
with the right bytes); the SH-2 reports round 0, tables installed,
display on, on both a passing and a failing pad (MDSVALUE); on a failing
build the name table and the tile art read back non-zero (BGVALUE).
What has not been read on a failing build is the MD BG palette itself,
and the Neff observation ("the palette swap is what brings the
background") still points there.

*Rules:*
1. Before attributing an FPGA-only level-start failure to a change,
   build the same change with a 1-, 3- and 5-NOP pad (ZEUSPAD=N) and
   see whether the verdict follows the pad. If it does, stop.
2. A rig instrument that floods CRAM destroys the BG palette for the
   rest of the run (the game re-sends it only on change): read values
   in a window the picture does not need, or through 32X CRAM.
3. The next instrument is a readback of MD CRAM 16-63 on a failing pad,
   without a flood, plus the SH-2's palette landing count -- then find
   the layout-sensitive step (candidates: the master's palette-landing
   timing vs the 68K's DMA, SH-2 instruction-cache alignment).


### The palette is what the failing layouts never land; the FB persists across a warm relaunch (2026-09-21)

Rig readback without a flood (CRAMPROBE: the 68K shadows every BG
palette block it lands, floods CRAM for 64 of 512 vints, restores from
the shadow): on three padded builds that lose the level background the
shadow held ZERO palettes after 60 s -- no packet with the palette flag
was ever consumed. The SH-2 sets that flag only when the block differs
from the slot's previous content, and on the FPGA the framebuffer
survives a warm relaunch, so a first palette equal to the previous
run's last one is never flagged; ares boots from zeroed DRAM. Forcing
the first 16 publishes (slim31) is correct and passes three launches,
but did NOT rescue the failing pads or the high-score build. So the
flag path is where the loss shows, and the layout-sensitive cause is
still upstream of it. Next: the same shadow readout with the SH-2
posting, per window, whether it published a tile/cell packet, a stub
(hs_stub carries no palette), or nothing.

*Rule:* a rig instrument's picture windows are only valid if the
instrument restores what it floods; the CRAMPROBE pattern does.
