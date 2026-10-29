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

### Re-analysing an analysed Ghidra project destroys work (2026-09-28, third time)

Enabling the "Decompiler Parameter ID" analyzer and re-analysing the altbeast
program took it from **788 functions to 746** and from **73,314 bytes of 68K code
inside a function to 61,118** -- 12 KB of decompiled code gone -- while gaining
**zero** prototype parameters. It also deleted two function entries a script had
created. The option cannot help: the analyzer infers parameters against the
processor's calling convention, the 68000 language module passes arguments on the
STACK, and this program passes everything in registers, so there is nothing to
find.

**The repo already recorded this class of failure.** `tools/ghidra/f1_rebuild.sh`
header: *"two attempts to adjust the project in place (declaring a calling
convention, then reverting it) left signatures altered and the SH-2 compile rate
DOWN from 415/623 to 303/623. In-place edits to an analysed project are not
reliably reversible; re-importing is."*

Cost: a full `f1_rebuild.sh` cycle, and a near-loss of the corpus --
`docs/proof/altbeast.c` is **gitignored** (correctly: Sega-derived), so
"the corpus is committed, the project is only a means" was said out loud and was
false. `docs/proof/CORPUS-PROVENANCE.md` carries the backup rule;
`docs/audit/corpus_sha256.txt` the count and hash to check against.
`tools/ghidra/param_id.py` now raises instead of running.

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


### CORRECTION 01:30: it is a per-launch RACE, biased by layout, not deterministic per rom (2026-09-21)

slim31, the line, passed two launches and lost the background on the
third with the same bytes. Tally, launches as pass/total: slim31 2/3,
1-word pad 2/2, 3-word pad 1/1, st.b (slim30) 1/1, slim22 1/1; 5-word
pad 0/3, bset (slim23/25) 0/2, every build that adds a gate site 0/6,
slim26-28 0/4. Read that as a level-start race whose odds a build's
layout shifts between ~90% and ~10%, on the FPGA only. The "deterministic
per build" wording of the previous two entries is withdrawn; the pad
table stands as data.

*Rules:* three launches per rom before any rig verdict on the level
background; report pass/total; a fix for this card must move a 0/N
build to N/N, nothing less.

### The two CPUs name the COMM registers differently; CENSUS2 read the 68K's own stamp (2026-09-21)

`sh_src/mars.h` defines `MARS_SYS_COMM6` as 0x20004026, whose 68K
address is 0xA15126 (`md_src/common.h` `MARS_COMM6`). The CENSUS2 relay
in `partb_hook` read 0xA1512C, which is COMM12 — the register the 68K
itself stamps with `0xD000 | late_V` in `fmgate_partb`. So every
"packets/64 = 34" of the 2026-09-21 census was the 68K decoding its own
late-V byte (0xD000 has bit 15 set; (w>>8)&3 = 0 = field 0; w & 63 = 33,
+1 = 34), and the SH-2's post never reached a capture at all. Two
launches and 24 captures were spent on it.

**Rule:** name a COMM register by its raw address on BOTH sides in the
same comment when a probe opens a channel, and prove the channel with a
known constant (post 0x8055, read 0x8055) before reading a counter
through it. The barcode readout (`RIGBARCODE=1`) carries the SH-2 counts
inside the packet header instead — words 4 and 6 high bytes, stamped by
the ISR after its SDRAM->FB copy (the publish-site stamp is overwritten
by that copy) — and ares showed the 68K consume count equal to the SH-2
publish count on four frames before the rig saw it.

### The level-start "race" is a lost boot-storm packet: framebuffer packet 2 sat in the bank the master does not read, and the two-deep re-mark belt had rotated past it (2026-09-21, 04:30-05:30)

Found in ares, which DOES reproduce it once the build layout is one that
loses (rom/night/barcode6.32x: the line's layout plus a byte-7 change in
the barcode probe; deterministic, every run). The frame-by-frame dumps
(scratch bootsweep, `docs/design/RIG-READOUT.md`):

    f26  68K blasts FB packet 1: palette blocks 0-14 raw.  Master lifts it (seen 1).
    f27  68K blasts packet 2: blocks 15-29 (the level-1 palette the game
         PRELOADS at boot).  pub 2.  The master's lift finds sequence 1
         for the next SEVEN vints: a flip put packet 2 in the bank the
         master does not read, and FBXPEND blasts a packet exactly once.
    f34  68K blasts packet 3: blocks 30-44.  Master lifts it (seen 1->3),
         sees the sequence gap, posts BAD1 at f36.
    f36  68K has already pushed packet 4 (blocks 45-55) BEFORE reading
         the echo; the belt re-marks the last TWO lists = packets 4 and 3.
         Packet 2 is out of the belt.  Its blocks' shadow == mirror, so no
         compare ever re-ships them; nothing re-dirties them until the
         Neff cut rewrites the palette.  Level 1's BG pens are 0x0000.

So: not the palette flag, not the display gate, not timing of the raise
(DMAWAIT/DMADELAY dead), not a tile. The 68K->SH-2 palette transport
loses one packet per unlucky flip and its recovery is one packet too
shallow when the gap echo arrives a vint late. Every layout shifts the
blast-vs-flip phase at boot, which is the whole "layout lottery"; the
barcode's extra DMA happened to shift it into the winning phase on
every layout tried (12/12).

**Fix (FBXECHO=1):** the master echoes the sequence it last lifted in
its own packet header (word 5 bits 12-15; the count uses 9), and the 68K
re-blasts the kept packet, same sequence, when two vints after a blast
the echo still has not caught up. Structural: no dependence on flip
phase, one extra ~2-line copy only when a packet was actually missed.

**Also found on the way:** the slim diag counters (0xFF3400-0xFF347F) and
the palette shadow (0xFF3500-0xFF3561) were hard-coded WRAM addresses
that, since .bss grew past 0xFF3200, sat INSIDE `slim_art` and
`slim_va`: every diag increment corrupted fetched tile art (a candidate
for the level-1 black-tile pop-in) and the shadow copy overwrote 96
bytes of it. They are named statics now. **Rule:** no hard-coded WRAM
scratch below `__bss_end`; grep `rom/md_start.lst` before choosing an
address, and prefer a static.

**Correction 06:30, from the two-bank dump of the losing layout
(`rom/night/lay_bc2.32x`):** the packet was not "in the other bank after
a flip" -- the master's ISR lifts BEFORE it flips, and the 68K's blast
was still in progress at that lift: the master read the old publish
word, flipped, and the finished packet 2 sat in the now-displayed bank
0, unreadable by either CPU, until the master flipped back and its scene
fill erased it (bank 0 publish word 0xB602 at f28-29, 0x0000 at f30).
Also: the belt's first home, the pre-post slot, is NOT on the boot-time
vint path (age never advanced); it lives in `partb_hook` now, every
vint, FM=0-guarded. With that, the same layout re-blasts at f29 and f31,
the master lifts packet 2 at f32, and PAL_SH is complete at f300.

### The attract's level-2 demo had no background because nothing ever installed round 1's table (2026-09-21, 09:30)

Mike's rig capture: dragons over a black plane B, foreground rocks
present. ares reproduces it (echo2 and slim31 alike, so pre-existing).
Chain: the per-round MD pen tables exist for all five rounds
(`pal_rounds_md.h`), but the two paths that install one both missed --
the display-on-edge install reads the published round at the edge, and
the game writes its round variable AFTER that edge in this demo (ares
COMM14 trace: round 0 -> 1 at f3563, no edge after); the other install
is gated behind PALSTATIC's palette detector, which cannot see level 2
(2026-09-02: zero stable discriminators, level 2 reuses level 1's
palette words -- baking a level-2 scene was tried and withdrawn). With
MDSREFUSE, every level-2 background set outside round 0's table renders
as backdrop. Fix: install the published round's table when it differs
from the installed one while on screen and past the load gap
(m_main.c, the display-edge block). ares: level-2 demo 0.22 -> 1.00
non-black at the round-anchored frame; rig 3/3.

**Rule:** compare attract scenes at frames anchored on the game's own
round/step from the COMM14 trace (`anchors.py`), never at fixed frame
numbers -- a build's vint length shifts the attract by tens of frames,
and a fixed frame lands inside a legitimate display hold (the black
frame that looked like a crash).

### Attract cards, 2026-09-21 09:30-10:45: what each one was

1. **Level-2 demo background black** — CLOSED. The round's MD table was
   never installed (LESSONS 09:30 above). Rig 3/3.
2. **Score table unpopulated** — CLOSED: the high-score writer's stores
   were never FM-gated (parked as HSGATE=1 during the race). With the
   echo belt closed the gates are on by default (`HSNOGATE=1` off). The
   table types in over ~8 s in ares and fills on the rig.
3. **Black cells around the level-2 rocks** — CLOSED: the 2026-08-25
   "purple backstop" blanks every BG cell in view rows 24-27 wherever
   the FG has a cell there, because level 1's FG covers that band
   solid. Level 2's FG has holes there and the arcade shows the floor
   through them. `BGBOTTOM=1` (on the line): blank only under a fully
   OPAQUE FG tile (standalone art scan, 256-slot cache). Measured
   against `mame altbeast` frames: the floor band now runs to y=215
   (arcade 213), it stopped at 193 before.
4. **The echo belt had a hole** — CLOSED: rig att1 launch 2/3 lost the
   level-1 background with the belt on. A lost packet whose successor
   is built before the master's echo arrives is never re-blasted (the
   belt compares against the newer sequence). Second half: before the
   stage is overwritten, an unechoed two-vint-old push has its palette
   ids re-marked (dirty + force raw) so they ship in the new packet.
   Rig 4/4 after.
5. **The eye picture loads in full view for ~150 frames** — OPEN. Not
   the display hold (it honours the game's blank and releases when the
   walk has gone round twice, 32-37 vints); not lost packets (no skipped
   consumes, no deferred publishes); not the slim buffer (0 records
   dropped, counted now). The eye's tiles are UNBAKED: 1133 tiles went
   through the inline 17-word records in 300 frames (baked +19), at 12-40
   per packet, and the dynamic allocator churned the picture's colour
   sets throughout (~540 slot-tag wipes in 90 frames, all from the
   off-screen assignment rule at m_main.c "222/223"). Re-shipping
   instead of wiping (MDSREMARK, OFFSCREENREMARK) and an exact settle
   test (HOLDCOMPLETE) each helped a little and none cured it. THE FIX
   IS TO BAKE THE ATTRACT PICTURES (eye, the two other pictures, the
   score backdrop) into `.tilesmd` and a static table, as the rounds
   are: baked tiles ship 40/vint through the slim route (2400/s), inline
   ones ~24/window, and a static table ends the churn. Until then the
   eye shows its load.

**Rules:** compare an attract scene against `mame altbeast` frames from
a no-coin run (scratch `arc_attract.lua`), anchored on the game's own
round/step; a fixed frame number lands in a different scene per build.
And when a probe flag is guarded by another flag, grep `.build_flags`
for the guard before believing a null result (C1RTALL and NTWIPEGEN were
both compiled out on the line: C1_MASKTAB and NT_SKIP are not on it).

**Belt correction 11:20 (ares boot trace of a losing layout, scratch
bsweep2):** the re-blast lands in the SAME bank as the original blast
(same vint phase relative to the master's deferred flip at vcounter
~490-506), so it cannot rescue a packet by itself; what rescues it is the
second half, the re-mark of the lost push's palette ids into the next
packet -- and that never fired, because the re-blast reset the age the
re-mark tests. Re-blast spacing has its own counter now; the re-mark
counts from the ORIGINAL blast. The layout that lost in ares
(rom/night/attbc.32x) passes the palette gate with it (attbc2), and the
line build shows 2 re-marks by frame 300. Rig verdict pending (rig18).
The one-line truth: a blast lands in whichever bank is draw at that
instant, the master lifts from whichever is draw at its pre-flip lift,
and the flip sits between them in the vblank -- so every packet can be
lost once, and recovery must be by CONTENT (re-mark and re-ship), not
by re-writing the same bytes at the same phase.

### The attract bake (2026-09-21, 11:00-14:00): what it took, and the traps

Scenes 5-9 (title splash, eye, second picture, score table, the round-0
transformation) are harvested from ares at frames anchored on the state
word (tools/attract_harvest.py: palette RAM + the colour sets whose tiles
were on screen, via VRAM slot -> md_tag), packed by bake_tilecram.py as
extra "rounds" (seeded from round 0 for 7 and 9 so shared sets keep their
pen maps and the blob dedups), and baked into .tilesmd (510 KB against
the 576 KB gap). The runtime keys the tables by SCENE from the state word
(MDSCENES=1, md_scene_raw/md_scene_get). The eye now settles at +50 where
it used to show churn garbage for 150 frames; the splash backdrop, the
score backdrop and the transformation's blue zigzag are right.

Traps, each one build and one gate:
1. **Step 1 is not the title.** At boot steps 0-2 are the SEGA card and
   the splash; mid-attract "round 1 step 1" is level 2's demo. Only step
   2 is the splash. Level 2 went black on the first build.
2. **Do not make baked pictures "on screen".** The refuse rule then
   blanks every set a harvest missed (the splash's relief, the score
   backdrop). Pictures stay off screen; their baked sets are PINNED
   (mdp_free_set honours pins for scenes >= 5) and anything else goes
   dynamic.
3. **Pictures have no "on" flag, so the scene-change install must not
   require it** -- the mid-attract splash sat on level 2's table.
4. **A pin the free refuses must not be an eviction victim.** The
   victim loop picks a pinned off-screen set (age forced to 255), the
   free refuses it, nothing changes, the loop never exits: the master
   hangs holding FM, the game spins in the FM-gate thunk, the vint's
   part B defers its post forever, the 68K vint counter freezes. Found
   by the COMM trace (both CPUs silent within ten frames) and the
   68K's recorded interrupted PC = FMGATE_SPIN_ADDR. The first
   harvested scene with a set outside its table (the transformation)
   was the first to need an eviction.
5. **The wipe on a scene change belongs under the game's blank only**
   (`!r60_disp_on`); the transformation cut has none.

What remains: a scene's own load (1120 cells per plane + its art at 40
tiles a vint) holds the display ~40 vints after the game turns it on --
the arcade shows it at once. HOLD_FROM_BLANK counts the settle from the
blank and asks one rotation of a baked scene; the floor is the transport.
Next lever: a larger per-vint slim budget while blanked.

**md_pending (2026-09-21 14:40):** the master's "claimed slots not yet
shipped" counter was a LOCAL of its main function; the emitter is only
called while it is non-zero. Every re-mark path added today (install
re-ship, off-screen re-ship) set dirty bits without bumping it, so their
slots were orphaned: the transformation's red field (set 20, 57 slots)
stayed dirty and black for 100 frames while set 21, marked by the normal
path, drained. File scope now, bumped per newly dirtied slot. Rule: a
dirty bit and the pending count are one fact; never set one without the
other.

### The transport floor, measured (2026-09-21, 15:00-16:30)

A scene load = packets per vint x items per packet, and both were
measured on the eye's blank in ares (scratch loadprobe/fmprof):

    line (attract4)  37-vint blank, 25 packets, one item each, two walk
                     rotations + 416 baked tiles: hold 23 vints after
                     display-on
    + ART_TAIL joined to the slim route (tile records ride every cell
      chunk's tail, mixed 2/17-word records, the 68K walks batch and
      tail with one slim_walk): hold 9 vints (eye), 19 (picture), 30
      (mid splash), 17 (cut). Rig 3/3. ON THE LINE (attract5).
    + BLANKNOCOMPOSE (skip the compose while blanked): NO measurable
      gain -- the master's FM-held span is still 1.2 vints per window
      with nothing to compose, and it gets only 11 windows in 37 vints,
      idle 65% of the blank waiting for the 68K's announce.

What the 68K is doing meanwhile (the game-PC ring): parked in the
FM-gate SPIN thunk on 10 of 36 vints, i.e. the game's own tilemap load
waits out our windows one gated store at a time, and the vint handler
enters late (V outside the DF-E8 gate) on 5 of 36. The arcade shows the
transformation cut COMPLETE on its first frame (MAME 4440->4441): the
game pre-writes the cut's pages during the level and only switches page
pointers; our walk re-walks both planes after the switch = 8 windows.

Rules: (1) a record the 68K cannot stage is lost art -- the emitter
caps at SLIM_CAP per packet and the 68K buffer holds two packets;
(2) `md_pending` and a dirty bit are one fact; (3) profile before
building: the compose skip was the obvious lever and it was dead.

**Two more dead levers (2026-09-21 17:00, ares holds eye/pic/mid/cut):**
BLANKNOFLIP cut the master's FM-held span per window from 8192 to 2889
ticks and changed nothing (11/20/32/22 vs 9/19/30/17); the window count
stayed 20 in 37 vints. BLANKBUDGET=3 "shortened" the mid splash's hold
to 8 by releasing it UNLOADED (0.00 non-black at +50: the settle test
saw less dirt, not a faster load); 7 = 13. So: FM, the compose, the flip
and the page budget are all off the critical path of a scene load. What
is on it: the game's own tilemap writes (one gated store per FM window)
and the walk's rotation. The arcade side-steps both -- it writes the
next scene's pages during the current one and switches pointers in one
frame. The matching design: walk a scene's pages into a SECOND name
table bank before the switch (a design card, not a knob).

### The black sky on the second level-1 demo was a wrong scene map, and the barcode read it in one capture (2026-09-22)

Mike's rig capture on the readout twin, 47 s in: level 1 with a black
sky. Decoded: attract step 5, 10 live palette entries, 0 stale cells.
So: palette, not cells -- and step 5 is not a picture. NOTES-FROM-
DECOMPILE 23 already says "1/3/5 = the level's tilemap on screen";
the attract bake mapped step 5 to "the second picture" from an ares
trace read too fast (the gate's "pic_150 nonblack 0.52" WAS this
black-sky frame and was read as a picture still loading). Scene 7's
harvest was that demo's ground sets without the sky's, so its install
left the sky pens unassigned. It looked like the hardware race because
the first level-1 demo (step 3) is fine and the second (step 5) comes
only after the eye -- past the 12-capture windows, and never in the
ares gate's anchor list by that name.

**Rules:** name the game's state from the decompile notes, not from what
a scene looks like at one frame; and a readout that carries BOTH the
palette count and the stale-cell count turns "black" into one of two
words in a single capture. Step 5 now maps to the round's table.

### The splash's red logo drew a red outline over dark fill, and its fade to blue went black: pixels merged onto one pen (2026-09-22)

Read in ares at boot frames 340/400 with the pen owners: the logo's ten
sets (37-46) hold one palette, so sharing pens between the SETS is
right. But the bake also merged PIXELS of a set onto one pen when their
colours were equal in the harvest's majority frames (pixels 0, 3 and 4
of set 37 are the same blue in the blue phase), and the game's red phase
changes pixel 3 alone: a pen has one owner, so the fill pixel showed the
outline's blue. The fade-out of the relief is the same thing per pixel
(the arcade fades to plain blue; ours followed one owner to dark).
Fix: `bake_tilecram.py` packs harvested scenes by per-PIXEL colour
history -- a key is every colour a pixel showed across the dumps, and
two pixels share a pen only with identical keys. Requires the harvest to
cover every palette phase (21 dumps of the splash: boot and mid-attract,
blue, red and white).

**Rules:** a shared pen is a shared HISTORY, not a shared colour; a
palette-animated scene must be harvested across its whole animation;
and read palette RAM at 16 bytes per set (an analysis with a 32-byte
stride spent an hour reading the wrong sets).

### The relief-to-logo "fade to black" is the page clear shown in view (2026-09-22)

Measured on the diagnostic build across the switch (allocator counters,
the walker's mirror, the page copy, the layer snapshot): no refusals, no
evictions, no cat-1 pressure, snapshot unchanged; the walker blanked the
800 relief cells because it read w == 0 for them -- the game's scene
change (routine 0x36b0) CLEARS the page and refills it with set 11's
solid blue tiles within one frame, and our copy-and-walk pipeline (a
page capture per window, a 4-chunk rotation) shipped the cleared state,
then needed a rotation to replace it: ~20-30 black frames where the
arcade shows the refilled page at once. Same class as the transformation
cut. Not a palette bug: set 11's colour 0 is the blue and its pens are
right once the cells arrive. Fix is the PRE-SWITCH / atomic-walk design
card (STATE), not a bake or table change.

### The eye's "loose tiles" were three CRAM entries for a set of eight colours: the harvest read plane B from an empty address (2026-09-22)

Traced in ares at eye+280 against the arcade's frame 4780 with the
name tables, the slot tags and CRAM: plane B behind the eye is right in
tiles and sets (0 of 1120 visible cells point at a slot of the wrong
set), and the FG's pen-0 pixels are transparent as on the silicon
(jts16_prio.v). What shows through them is BG set 35 with the pen map
[13,12,11,11,15,15,14,14] -- three entries for eight colours, two of
them unassigned -- because `tools/attract_harvest.py` dumped 8 KB from
0xC000 and read "plane B" at its second half, 0xD000. Plane B's table is
at 0xE000 (md_main.c:153). Every harvested scene (5-9) had counted its
BG cells as slot 0 / set 0 and got no BG sets at all; the eye's 33-36,
and the FG sets 25-32, 19-21, were packed by whatever the seed round
left.

Two more findings on the way:

- **The "arcade shows the planes 8 rows lower" measurement was a crop
  error.** ares-headless writes 1415x243 frames; the active display is
  rows 11-234 (rows 0-10 and 235-242 are border), and the scratch
  scripts cropped at row 19. jts16_tilemap.v's vtimer (VB_START 0xDF,
  262 lines) and jts16_scr.v (vpos = vscan + vscr, vscan <= vrender)
  put plane line y+vscr on screen line y with no offset, and with the
  crop fixed the eye's cells align at shift 0. Crop at (65,11), never
  (65,19); scenegate.py is corrected.
- **Counting every plane-B cell over-harvests.** The first corrected
  harvest pulled the level ground behind the scores table into scene 8
  (21 sets) and the blob grew 510 -> 694 KB, past the 576 KB gap (the
  cart is full: .sprbake ends at 0x400000). Skipping BG cells under an
  opaque FG tile (the silicon's rule, jts16_prio.v) removed almost
  nothing: the ground sat in the scores' BLACK bands, where plane A is
  empty and plane B is stale content the walker never re-marked. The
  harvest now reads the walker's own per-view-cell mirror (md_dbg_nt,
  m_main.c:653, SDRAM 0x0603D200: [BG][FG] x 28 x 40 name-table words,
  blank and never-written marked), which is the S16's truth per screen
  cell, and keeps the opaque-FG rule on top.

- **The blob budget forced a layout change, not a set cut.** With the
  BG sets in, every harvested scene's blob grew and the total passed
  the gap even after seeding the level-set scenes from round 0 (610 KB).
  The mirrors say most sets live on ONE layer (scene 6: 17 of 19,
  rounds 0-4: 72 of 89 by the audit), and a both-variant block spends
  2 KB on the layer that never reads it. `bake_tiles_md.py` now emits a
  2 KB single-variant block for those, flagged in bit 15 of the index
  entry with the base in 2 KB units; the SH-2 folds the entry into the
  record word (bit 15 flag, bits 6-14 base, bits 0-5 code; m_main.c
  TILE_SLIM emitter) and the 68K decodes it in slim_fetch. Rounds-only
  masks took the blob 610 -> 564 KB; the harvested masks come from the
  third column of s<id>_sets.txt; with them the blob is 414 KB (206 x
  2 KB, 100 single-variant).

Outcome (eye2, the line): ares 19/19 anchors clean, refusals 37,000 ->
0 over the attract, palgate PASS, rig 3/3; the eye phase-matched
against a dense arcade capture (every 8 frames, 4540-4980): yellow
phase 239 -> 223 cells, blue ~105 in both builds. Of the 9,297 pixels
still differing at eye+290, 5,549 are sets 24-27 colour 5 displayed one
MD level bright: the quantiser (`mdp_quant`, +2 >> 2) is nearest in
LINEAR 5-bit space, the MD ramp is not linear. PALQUANT=1 is the
ramp-nearest table; it moves 16 of 32 inputs one level down on every
screen, so it waits for Mike's A/B.

**Rules:** a harvest reads the VDP's tables at the addresses md_main.c
programs, and it says which; the blob budget is a gate on every
harvest change (make line fails loudly at .tilesmd); a "hardware
offset" measured from screenshots is a crop until the RTL agrees; and
when a blob outgrows its gap, measure what the layout wastes before
cutting content.

### The eye's pupil notch was the cat-1 mask cache thrashing; the transformation's black flames were a bake overflow keyed one step early (2026-09-22, evening)

Mike's red lines (rig capture 180046): three cells at the pupil's
top-right edge show iris where the pupil should be. The framebuffer
dump at that frame has the pupil sprite's pixels under those cells
(32X palette 0x81 = black), so the sprite exists and is hidden: the
S16 draws it between BG and FG, and our FB layer sits below both MD
planes. That case is cat-1 (a priority FG cell whose transparent pixels
must show the sprite) and the punch handles it per pixel from a mask --
but on a harvested scene the mask comes from the runtime classifier's
64-slot cache indexed by tile code & 63, shared by every priority code
of the pass (the eye has ~500), so by the time the slave read the cell's
mask through cat1code another tile's mask sat in the slot, and the
sprite was punched out of the whole cell. Fix: a runtime class-2 cell
owns a mask slot while it stays class 2 (256 slots; the eye needs 46,
the cut 163), written together with cat1code. On the way: C1_MASKTAB
was OFF the line although C1_STAMP assumes it (the stamp indexes the
mask table with whatever cat1code holds; without the table that is the
ART code, an out-of-bounds read); it is on LINE_FLAGS now.

The transformation cut (rig capture 180039; ares f1550-1630 against the
arcade's 4448-4552): the arcade's solid blue around the flames is BG set
19, colour-cycling; scene 9's bake reported `OVERFLOW ... [0, 19]` --
seeded from round 0 (44 of 45 pens taken by the level's ground) because
the scene was keyed on the state word's cut bit, which rises ~15 frames
before the game switches to the cut pages, so the harvest saw the level
too. Now scene 9 keys on the page word (TEXT_C[0x740] == 0xAAAA), is
harvested after the switch, packs alone, and gets per-pixel keys (set
20's flame blues cycle as well).

Outcome (eye3): ares eye yellow phase 223 -> 210 cells vs the arcade,
pupil round with its glint (scratch notch_ab.png), 18/19 anchors clean
(cut+10 carries 12 dirty cells, the switch), palgate PASS, rig 3/3.

**Rules:** a shared cache between the master's classification and the
slave's read is only safe if nothing can rewrite the slot in between --
size it by the WORST scene's distinct keys or key it by cell; a knob one
flag "implies" must be on the line with it (grep .build_flags for both);
and a scene keyed on a state bit that leads the picture by frames
harvests the wrong picture.

### The pan to the beast eye: the edge pair's one-column lead against a 9-12 px/frame pan, then art (2026-09-22, late)

Mike's capture 180548 (and ares eye2 f1850-1870, columns 0-13 stale
against the arcade's 4720-4728): the picture pans 9-12 px a frame
(TEXT_C xs 0x164 -> 0x1e8 over 12 frames) while a row is revisited
every 9 windows, so the EDGE42 pair's one-column lead left 10-13
columns entering the view with whatever the name table held. Change:
when a plane's scroll moves >= 3 px per window -- and always on a
harvested scene -- every row of the window ships as the WHOLE 64-cell
plane row in plane order (row header bit 15; the 68K plays it as one
64-word DMA): 24 columns of lead. Measured (pan2): the stale tiles are
gone; what remains is BLACK cells whose art is not resident yet: the
68K fetched 22-28 tiles a frame in bursts with md_pending mostly 0, and
the packet sequence ran 1.00 windows a frame (0.50-0.88 across the
game's page switch at ~f1860). The remaining lag is structural: a cell
entering the 64-column window has its art requested at its row's next
visit (<= 9 windows) and is rewritten at the visit after (<= 9 more)
against 16 windows of lead at this speed. It is the transport floor
(rotation length), not a bake or a table. Note the game maps page 1
(the beast eye) only when the pan starts (pages 0000 -> 0101), so
nothing can be prefetched before it, and snaps back to page 0 with the
new picture at the pan's end (a second full rewrite, the switch-in-view
class).

The transformation cut with scene 9 keyed on the cut pages and packed
alone (eye4): f1580 299 -> 14 cells against the arcade, the flames'
blue and the cycling set 19 right; the ~30-frame transition at the page
switch (f1540-1575) stays, same class.

**Rules:** size a scroll lead from the scroll SPEED and the rotation
length, not from "one column"; and measure the fetch pipeline (records
fetched per frame, pending, windows per frame) before raising a cap.

### Harvest frames drift with the build: scene 5's "mid splash" frames were level 2's demo (2026-09-22, late)

`bake` reported scene 5 overflowing sets 65/66 on every build since the
attract bake; the mirrors say those cells sit on FG rows 18-26 at
frames 4530-4600 -- level 2's ground, because the mid-splash anchor
moved from ~4885 (attract2) to 4570 (eye4) as loads got faster, and the
harvest's absolute frame list did not move with it. Sets 64-73 rode
into the splash's bake for nothing. Now 4650-4950. The eye's
harvest had the same drift (f1640 was still the cut; f1980 still the
eye) and the cut's (f1503-1535 still the level).

**Rules:** a harvest frame list is only valid against the anchors of
the build it was taken on (tools/scenegate: `anchors ...`); re-check
the anchors whenever loads get faster, and prefer frames deep inside a
scene to frames near its edges.

### The single-variant blob drew purple through the FG at the level-1 boss gate: round masks came from a three-scene audit (2026-09-22, 19:30)

Mike's gameplay captures on eye6 (score 8000, the boss area with the
short grass): the temple gate's transparent pixels show purple. The
audit (docs/audit/mdpen_scene_sets.txt) lists sets 81, 83, 84 as BG-only
for round 0 because it sampled three scenes and never the boss gate,
where they draw on the FG. Their single-variant block holds the BG
pattern, whose pixel 0 is the set's colour 0 -- (4,0,7), purple --
where the FG variant has transparent. Rounds 0-4 now bake both
variants regardless (~560 KB, inside the gap); only harvested scenes
take single-variant blocks, from what the mirror saw.

**Rules:** a bake that drops a variant needs proof the layer never uses
it -- the ROM's page tables for a level, the mirror for a harvested
scene -- never a sampled audit; and a gameplay regression check belongs
in the gate (the level-1 play script exists: discover/inputs/
play_level1.csv).

### cut+10 is already the switch (2026-09-22, 19:45)

Three builds chased "the cut bit re-marks the level in view" (md_state_on,
bm_scan_baked_ok, the glow -- each keyed on the cut pages instead; all
harmless, none changed the count). Measured with dumps at cut+0 and
cut+10 (eye9): at cut+10 the page words are already AAAA/BBBB, xs/ys 0,
every FG view cell's word changed (sets 20/21/0), 823 dirty slots. The
bit leads the switch by ~10 frames, so the scene gate's cut+10 anchor
sits INSIDE the transition and its dirty count measures the new pages'
art loading (~800 tiles at SLIM_CAP a vint), which is the switch-in-view
class. The pre-switch window is ~10 frames: that is the budget for
prefetching the cut's art (sets 19-21, pinned) before the switch.

**Rules:** before attributing a dirty count to a cause, dump the page
words at that frame; an anchor named "cut+N" is only pre-switch if the
pages say so.

### The cut's transition is the scene install's re-mark, not the cut's tiles (2026-09-22, 20:30)

Anchor-relative measurement (scratch cutcmp.py: the state word's cut
bit from --trace-comm, then black 8x8 cells in rows 4-23 per frame):
eye9 shows ~350-600 black cells from the switch (+10) to +55. A cut-art
prefetch (CUTPREFETCH: on the cut bit, claim ways for pages 10/11's
tiles from the staging RAM and ship them under scene 9's table via a
bit-30 tag marker) changed nothing, and the trace says why: the level
leaves 42-108 free ways, the pre-switch window is 4-6 frames on this
build, and at the switch the dirty count jumps to 965 while the cut's
sets hold 150 slots -- mds_install(9) re-marks EVERY resident slot
whose set's mapping changed, which is every slot (the level's sets fall
out of the table, the cut's come in), and the walker blanks a cell
whose slot is dirty. ~965 re-ships at SLIM_CAP a vint is the black.
cutpf4 keeps prefetched slots clean and skips the re-mark for sets with
no line in the new table (left resident, not freed: LESSONS 2026-09-21
on wiped tags).

Outcome (cutpf4): dirty at the switch 965 -> 16; black cells at
+10/+15/+20/+25 = 232/36/19/0 against eye9's 357/454/496/595 (clear at
+55); 0 cells differ from the arcade at +25. The ~15 frames left are
the row rotation (9 windows) plus the pre-switch scan finishing.

**Rules:** measure a transition as black cells against the scene's own
anchor, never as a diff against a sparsely sampled arcade; and when a
switch is slow, count what is DIRTY at the switch before speeding up
the fetch.

### The 60 Hz floor on the line, measured (2026-09-22 evening, ares level 1, eye9)

The game runs at exactly 50% (scene timer: 1300 game frames in 2600
vints) with ZERO IRQ4 frame misses, and the transport posts a window
EVERY vint (60 posts in 60, no entry rejects, no defers). So neither the
game's own pass nor the packet transport misses; the game's frame is
two vints long by construction of the per-vint sequence (frame_timeline,
play f3000-3011, lines from vblank):

    0-17    68K: consume, blast, FM raise (16), post (17)
    17-60   68K waits for the master's ack (odd frames: the ack comes
            at 124-135 and the wait breaks at its cap)
    55-90   the game's IRQ4 entry (71 odd / 83-90 even)
    100-126 odd frames: the game's pass spins 22-26 lines in a gated
            store while FM is still up (drop 124-135)
    ~166    even frames: the pass ends (idle); the game then waits a
            whole vint for its next flag

The pass itself is ~184 lines (heavy); with the 68K's 55-90 lines
before IRQ4, IRQ4's ~35 and the spin, a frame is ~327 lines. The
master's FM-held window is 45-55 lines on even frames and 108-118 on
odd (the frames whose sprites changed). Cut of one vint needs the
window under ~25 lines AND no spin, or the game's FB writers taken off
the gate.

Dead on this question: TEXTCAPOFF (the ISR's 3.7 KB text snapshot) --
FM drop lines identical with it off. The ISR's own flip path is 6-10
lines (STAMPCENSUS). Windows/frame stays 1.00 on the eye's pan too.

**Rules:** speed on this port is the LENGTH OF THE FM-HELD WINDOW as
seen by the game's gated writers, not any per-vint counter; measure it
with frame_timeline's raise/drop/irq4 columns before and after every
change.

Addendum 1 (sequence; times below were narrated, git is the clock): the master's window BODY -- from
the post pickup to the ack -- is 24 lines: launch 3, master half 10-12,
apply_cram 3.5-3.8, publish+P3 5.5, slave waits ~0. The FM-held window
the game sees is 45-110 lines, so 20-85 lines of it are the master
ARRIVING at the post: its post-ack tail (the walker's slice, the art
emit, the packet build for the next window) overruns into the next
vint. The lever is the tail, or raising FM only when the master is at
the post.

Addendum 2 -- CALIBRATION: the SH-2 FRT runs at 45.8 ticks per line
here (12,000 ticks per 262-line vint, measured pickup-to-pickup with
60 windows in 60 vints), NOT the 128 per line the NOTES-47 census
assumed (its "1650 ticks = 12.9 lines"). Every FRT-derived line count
in the record before this addendum is 2.8x too small. The master's
window in real lines (ares, level 1 play, mean over 60 windows):
launch 9.4, master half (compose) 32.2, apply_cram 9.9, publish+P3 to
the ack 15.5 -- the body is ~67 lines; ack to the walker slice end
57.8; then 137.6 lines IDLE waiting for the next post. So the FM-held
window the game sees is the body itself (45 lines on frames whose
sprites did not change, ~110 where they did), and the master has more
than half the vint spare. Levers, in order: the compose's master
half, the publish, apply_cram (32X CRAM writes need no FM: move them
after the ack), the launch.

Addendum 3 -- the blit's group skip WORKS on the line: FBCLEAR's
per-bank zero masks hold 1392 of 2240 groups (62%, 70 rows fully
zero) and the per-row audit found 0 lies in 60 vints (DIAG[42]). The
NOTES-78 "0.004% skipped" is stale. The master half's cost is the
examine loop over the 154 partly-live rows, the stores, and the audit's
8 uncached FB reads per skipping row.

Addendum 4: CRAMISR (the 32X CRAM paint at the flip, in
vblank) takes apply_cram out of the window (10 lines); FM drop on the
compose frames 124-135 -> 117. A `make line BLITSHIFT=48` is NOT a
split change: the ship list carries BLITSHIFT=$(SHIPBLITSHIFT) and the
sub-make's literal wins -- use SHIPBLITSHIFT=N. The 66-line "master
half" did not move with the row count, which says it is not the row
copy; the compose (slave in-window, master in the tail) is the pole.

Addendum 5: BLITAUDITDIV=4 + CRAMISR=1: the
compose frames' master half 66 -> 58, apply_cram 10 -> 0 in the window,
FM drop on compose frames 124-135 -> 112-120, the game's gate spin
22-26 -> 10-18 lines. IRQ4 entry 71/90 unchanged. Candidate for the
line (spd1) pending the ares gates and the rig.

Addendum 6: the SLAVE composes the whole
frame in-window (7-12 twelve-row bands a window, 20-27K of its FRT
ticks -- its prescaler is not the master's, so raw) and the master's
nat_mtask compose runs ZERO bands; the master's in-window "half" is the
chase: SHIPBLITSHIFT=48 took it 66 -> 42 but the master then waited 46
lines for the slave's half. The wall of a compose frame is the slave's
compose + the master's blit of the rows behind it. Levers: the
compose's per-row clears (ROWLIVE knows which rows carry pixels), then
composing on both CPUs.

Addendum 7 -- ROWGRP built (knob): the slave clears ~58 KB of sbuf a
generation because ROWLIVE is per ROW and ~180 of 224 rows carry a
sprite pixel somewhere; the baked sprite runs know their [lo,hi) per
row, so a 10-bit per-row group mask now says which 32-byte groups were
drawn and the clear zeroes only those (other drawers mark all ten).
Probe10 measures it against the base (sbuf cleared bytes, the slave's
compose ticks, FM drop, pixel identity at f999).

Addendum 8 -- the game's IRQ4 handler is SHORT: the ISR-exit V ring
(0xFFA300, the V counter at fmgate_ret every vint) reads 79-91 lines
from vblank start while frame_timeline puts the IRQ4 ENTRY at 71-90, so
the handler itself is 1-11 lines, not the ~35 the 2026-09-07 note
assumed. The game's frame is: the 68K's window wait (ends at the
master's ack, 60-135), IRQ4 (~10), then the pass (~184 heavy / ~147
light) from the loop top. One vint needs wait + 10 + pass <= 262: the
window must end by ~line 68 on heavy frames (105 on light ones). On the
line it ends at 112-135 on compose frames.

Addendum 9 -- the 2-vint frame is a THRESHOLD, not a slope: spd1
(window ~18 lines shorter on compose frames) still measures exactly
50.0% game-frames per vint, because the game's frame-flag protocol
grants a frame only when wait + IRQ4 + pass fit a vint; every cut short
of that line reads as no change on the speed instrument. Judge window
cuts by frame_timeline's FM drop / IRQ4-entry lines until the threshold
(~68 on heavy frames) is crossed; then gameplay_speed jumps.

Addendum 10: PUBNORB takes packet A's read-back 3.9 -> 1.4
lines when the packet was copied this window (the unconsumed-defer path
keeps the FB read-back: 4.4); B similar. ~3 lines on compose windows.
With CRAMISR + BLITAUDITDIV=4 + PUBNORB the compose frames' FM drop is
107-120, even frames 64-71; IRQ4 entry 67-71 / 83-90.

Addendum 11 -- second knob trap of the day: `make line BANDSHIFT=4
RG2SHIFT=0` built with 36/40 (.build_flags), the ship list's literals;
the MTASKINWIN probe measured nothing (master compose 0 bands). They
are SHIPBANDSHIFT / SHIPRG2SHIFT now. ALWAYS read .build_flags for the
value a probe was meant to change, not just for the define's presence.

Addendum 12: ROWGRP is DEAD as built -- the compose-frame
FM drop went 108-112 -> 121-123 (the per-run uncached read-modify-write
of the row's group mask, thousands a frame, costs more than the
full-width row clears it saves; FBB[2] counts rows, so the "cleared
bytes" did not even move). Knob kept off. If the clear is ever cut, mark
with a cached per-CPU mask and publish once per band, or clear through
the DMAC.

### An ifdef count is not a measure of game coupling

`grep -c GAME_ALTBEAST sh_src/m_main.c` returns **2**, which reads as
"the engine is already game-agnostic". Both hits are actually
`GAME_ALTBEASTJ` — region variants, not the game gate.

The real coupling is **12 distinct Altered Beast WRAM addresses
hardcoded across ~47 engine sites** (0xFFF142 round, 0xFFF148
object/cut marker, 0xFFF031 attract step, 0xFFF026 credited play, and
eight more). Measured 2026-09-23.

*Rule:* measure coupling by the **constants that would have to change**,
not by the conditional-compilation surface. The ifdef count answers a
different question and flatters the answer.

*Cost:* nearly quoted as evidence that the toolkit goal was already met.


Addendum (probe13, MTASKINWIN with the master owning rows, SHIPBANDSHIFT=4
SHIPRG2SHIFT=0): DEAD. The master's stage-1 strip took ~176 lines in
the window (its chunk runs from cart ROM -- nat_mchunk is not RAMCODE
and there is no .ramtext room -- and it serialises before the blit),
FM drop 221-234, game speed 41.9%. A both-CPU compose needs the master's
half in RAM code and overlapped with the slave's, not queued ahead of
the blit. Knob kept off.

### The window's floor is the framebuffer write rate (2026-09-23, BLITPROF on the line, ares level 1)

Per compose window the master's blit stores ~393 32-byte groups (12.3
KB) in ~58 lines while the slave stores ~513 (16 KB) concurrently: the
two together push ~28 KB through the FB in ~60 lines, ~470 bytes a
line, which is the ares FB stall floor (LOOP.md 20: 13.6 SH-2 cycles a
longword, 6.76 MB/s; MAME charges nothing). Zero groups are already
skipped (54-68%); the master pays ~2x per byte for cap_drain on the
same bus. So on a compose frame the FM window cannot be shorter than
FB bytes / 470 + publish (~15) + launch (~10-15): 28 KB -> ~85 lines,
against the ~68 the 2-vint threshold needs on heavy frames (105 on
light ones). The remaining levers are therefore BYTES, not scheduling:
(1) fewer FB bytes per frame -- the MD VDP offload claims ZERO sprites
on level 1 today (MDSPR_CNT: 0/60 vints; census running), and the
content-hash skip (BLITHASH, session-7 lever B) is off the line;
(2) EARLYREC so the compose no longer sits inside the window and both
CPUs blit at the floor rate the whole window; (3) the FPGA's own FB
write rate, unmeasured -- the ares figure is the only one we have.

Addendum (BLITHASH, 2026-09-23): cannot be built on the line -- its
per-bank per-group hash table (2 x 224 x 10 longs = 17.9 KB .bss) puts
.bss at 0x1C7E0 against the 0x19000 region base (3.6 KB free). A
per-row variant would fit but only skips rows whose every group is
unchanged, rare in gameplay. Parked. The MD sprite offload census is
the byte lever: of 600 live sprite records in 60 vints on level 1, 330
have NO BAKED MD KEY, 180 fail the single-palette-line anchor, 90 are
claimed (MDSPR_WHY).

Addendum (MD offload cap, 2026-09-23): of the 330 no-key records, 210
are set 0x03 -- and set 0x03's palette differs from the anchor set
0x09's at f3000, as do 0x0A and 0x0B (the 180 palette rejects, which
DO have keys). The MD VDP has one sprite palette line (lines 1-3 hold
the tile pens, all 15 used: "lines [15,14,14]"), so on level 1 only
the anchor set's sprites can ever be offloaded: ~15% of records. The
sprite bytes stay on the SH-2. The byte lever is closed on this design;
the FB write floor is the floor, and the FPGA's own rate is the number
that decides whether EARLYREC reaches one vint (ares: 28 KB / 470 B/line
= 60 lines of blit + ~8 of publish = 68, the threshold exactly).

### The FPGA's BLIT rate, read off the rig (2026-09-23, RIGBLIT; attribution corrected the same evening)

Barcode byte 5 now carries the master's last blit_half in lines (FRT
ticks / 46). ares reads 45-47 on level 1 (demo and play; the 60-window
mean was 58). The rig, second level-1 demo (step 5), four captures:
67, 71, 81, 86 lines; scores 46; level-2 demo 62-82 (low-vote rows).
The FPGA pushes framebuffer bytes ~1.6x SLOWER than ares. With ~28 KB
a compose frame that is ~100 lines of blit alone on hardware, against
the ~68-line window the 2-vint threshold allows on heavy frames. So on
the FPGA the FB-blit design cannot reach one vint per game frame at
today's byte count: it needs <= ~15 KB of framebuffer writes a frame,
i.e. a picture whose sprites mostly do not pass through the FB (the MD
VDP renders them -- blocked by the single sprite palette line -- or a
content skip against the bank's last ship, BLITHASH, which has no SDRAM
home at 18 KB). EARLYREC + a dual blit gets a compose frame's window to
~110 lines on hardware, not 68.

CORRECTION (same evening, BLASTEM.md 10): the instrument measures the
master's BLIT, not its framebuffer writes. Counting the MiSTer RTL puts
the FB store path at ~80 of the ~336 SH-2 clocks a group the rig spends
(VDP.sv write FIFO 6 system clocks a word; BSC.sv CS2 cycle ~4 clocks
with WCR1 0x0055; the core holds the pipeline through each store,
SH_core.sv 113), so the 1.6x over ares is NOT a framebuffer write rate.
The remainder is bracketed to instruction-fetch misses (the master's
working set is 5.4x the cache; halving the cache already cost the rig
27 -> 21 presented frames), DDR3 latency on the sprite-buffer line fills,
and both CPUs' fills on one ddram channel. The numbers above stand as
blit lines; the mechanism they were pinned on does not.

MEASURED the same night (BLASTEM.md 13, four probe roms on the rig):
loads are not the term (1 load a row instead of 80: 67 -> 68 lines);
the audit's uncached FB reads are ~15% (BLITNOAUDIT 67 -> 60); the rest
is stores at ~4.8 SH-2 clocks a 16-bit word (the BSC CS2 cycle, faster
than ares's model) plus the cost of VISITING the ~60% of groups the loop
then skips. Instruction-fetch misses were not needed to explain it.

Five roms (store-all added) give the split on the FPGA, level-1 demo,
67 lines: stores 23 (4.8 clk a word), the loop's own instructions 23,
loads 13 (~8.5 clk a line fill), the audit 8; ares's 46.8 is stores 33
and ~14 for everything else. BLASTEM.md 13.

**Rules:** every blit-throughput number in this record is ares's unless
it says "rig"; the rig's is 1.6x worse; on hardware the blit's first
lever is NOT VISITING empty groups (~22 of 67 lines: a row/band
emptiness word from the compose), then the audit (8), then FB bytes at
~0.5 lines per stored group; ares overcharges stores and undercharges
the rest, so rank blit cards on the rig.

### BlastEm (MESSAGES O-8): boots after a core fix, then fails the cross-check on the figure that mattered (2026-09-23)

First pass: the SH-2s never booted (master in the BIOS trap 0x13C by
frame 10, every rom). Cause, found by a per-instruction trace in the
generated core: `sh2_reset` in BlastEm's `sh2.cpu` keeps the stale
prefetch across the 68K's reset pulse, so the restart at 0x140 executes
the delay loop's `bf` and lands on 0x13C. Three lines fix it (BLASTEM.md
section 7). None of the handoff's three ranked candidates (early
interrupt, CPU exception, BIOS byte order) was it; the trace was cheaper
than any of them.

Second pass, the section-4 figure aligned on the game's vint counter
(BlastEm's `-b N` is HALF-frames: measured 30 frames of MCLK between
`-b 700` and `-b 760`): FRT ticks/window 12004 vs ares 12001, 60
windows in 60 vints on both, master half 20.0 lines vs ares 26.6, rig
67-86 (RIGBLIT). BlastEm undercharges the framebuffer write below even
ares's stall model, while the FPGA is 1.6x above it. It cannot explain
the rig; it is not a transport instrument. BLASTEM.md section 8.

**Rules:** "boots" means both CPUs past the handshake, proven by a dump;
an emulator's frame count is a unit to be measured against the game's
own counter before two runs are called the same scene; and an
instrument that agrees on the clock and the cadence can still be wrong
on the one term the axis needs -- check THAT term, not the easy ones.

### BlastEm, second pass: the mismatch was a 68K stall under FM; the fork now brackets ares and the rig (2026-09-23 evening)

The "BlastEm undercharges FB writes" verdict was measured on a frozen
game. BlastEm spun the 68K until FM dropped on every 68K access to the
VDP windows; our shim polls FS and reads FB staging under FM by design,
so the 68K starved and the arcade program's RAM sat frozen while the shim
posted empty packets. Five hypotheses died first (DREQ landings, cache,
dropped FB writes, an FM race, the FIFO); the 68K program counter at exit
settled it in one run. The adapter acknowledges an MD cycle at once
under FM with no VDP transaction (IF.sv), as ares does. Fixed in the
fork. After the fix the picture matches ares (groups within 3%), and the
FB-write knob calibrates: wait 3 = ares, wait 11 = the rig's 1.6x, held on
three attract spans. The RTL says the FPGA's FB write path itself is
faster than ares's model, so the rig's 1.6x is a BLIT-rate figure whose
cycles are not yet located (SDRAM reads, core stalls). BLASTEM.md 9.

**Rules:** when two emulators disagree on a timing figure, prove they
are running the same picture first (group counts, game RAM changing,
the other CPU's program counter) -- every timing number taken before
that here was void; read the emulator's OTHER CPU's stall paths before
its bus models; and name the instrument's quantity by what it measures
(blit lines), not by the mechanism assumed behind it (FB write rate).

### Two tile palette lines do not hold level 1 (2026-09-23, measured, closes the second-sprite-line lever)

Per-frame census from the walker's mirror + tags + the live pen tables
(scratch pens/census.py), 6 play frames and 6 attract frames: level 1
references 34 pens holding 33 DISTINCT colours at every frame (the
2026-09-?? BGPACK2 note's "29-30" counted CRAM entries, not the pens
the visible tiles use). A best-fit of the visible sets into two lines
with the bake's rule (a set lives whole in one line) overflows 3-4 sets
-- 85/86/96/97/98/101, 6-9 colours each -- = 208-250 of 2080 cells at
every level-1 frame, which the refuse rule would draw in the
framebuffer (MORE FB bytes) or the bake would merge colours (fidelity).
Level 2's demo fits (26 colours, 0 overflow); the attract pictures fit
easily. And an S16 sprite set is 14 pens, so one MD line holds exactly
one sprite set: a second line buys one more set (0x0A or 0x0B), not
all three.

**Rules:** the byte-budget path through the MD VDP is closed for level
1 on a static bake; left: a home for BLITHASH's 18 KB, EARLYREC (mixed
cadence), or a different sprite route.

Addendum (BLITHASH potential from framebuffer dumps, 2026-09-23): the
share of drawn groups a bank already holds is 86-100% when the picture
is still (play f3000-3008), 34-79% on the attract demo, 7-13% while the
player walks (play f1000-1008: the scroll moves every sprite's screen
bytes). A content skip pays where speed is not the problem. Dead as a
60 Hz lever. Also from the same dumps: the drawn bytes per frame are
~6.5 KB walking (200 groups) against ~28 KB on the crowded static
segment -- the byte budget is scene-dependent and the threshold must be
priced per regime.

### Level 1 already runs at one vint per frame in the light regime; the 50% is the crowded regime (2026-09-23)

frame_timeline on the walking segment (play f1000-1011, spd2): FM drop
97-110, IRQ4 entry 55-66, gate spin 9-20, and the scene timer advances
60 game frames in 60 vints. The 2-vint frame measured at f3000-3011 is
the crowded segment (five zombies + the player, the heavy pass ~184
lines). So the threshold is per REGIME: light pass ~147 fits with the
window ending at ~105; heavy pass ~184 needs ~68. The blit's cost is
not simply bytes either: walking, the master stores 3.7 KB per window
in 34 lines (the examine loop over 2240 groups and the chase behind the
slave), against 12 KB in 58 on the crowded frames. Correct the earlier
"FB-write-bound at ~470 B/line" to: the window on a compose frame =
launch ~15 + the slave's compose (in-window, ~30-50) + the master's
examine+store loop (~30-60) + publish ~15; bytes matter on crowded
frames, the fixed loop matters on all.

**Rules:** price a cadence question per regime (a regime map from the
scene timer, 100-vint bins, is one ares loop); a single play window
is not the game.

Regime map (2026-09-23, spd2, level-1 play script, scene timer per
100-vint bin): 100% (one vint per frame) f600-1100 and f4400-5900;
50% f1200-4100 (the zombie fight) with 57-73% at its edges; 71.3%
overall f600-6000. The crowded regime is ~half the play run and the
whole of the 30 Hz complaint.

### EARLYREC, first build (2026-09-23): records over the FIFO at IRQ4 exit, compose off the window

Built as a knob (EARLYREC=1; md_main.c earlyrec_push at the fmgate_ret
hook, m_main.c erec_arm in the V-ISR, erec_take + the early launch in
the master's idle poll, the post's LAUNCH_EARLY suppressed when the
early one ran). First ares run on the fight (f3000-3060): 60 arms, 54
landings, 54 early launches; game frames per vint 50.0 -> 58.6% over
f1500-4100; half the compose frames drop FM at 62-70 (the target), the
other half at 128-179 (worse than the line's 108); the picture is sane.
The 68K push counters at 0xFFA0BA/BC collide with something (negative
deltas) -- do not read them.

Addendum (EARLYREC iterations 2-8, 2026-09-23): what killed the
transport in every EARLYREC build was the 68K's vint ENTRY going late:
the shim refuses to post when it enters outside V=DF-E8, and the entry
was 14-36 lines late every vint (the FM reads sit at V=0x004-0x00B in
the access trace). The push at the IRQ4 exit runs with interrupts
masked and the DREQ FIFO costs ~5 us a word on ares (the FIFO-route
push measured 596 words in 45 lines), so 276 words is ~20 lines and a
variable-length push under a fixed 278-word arm never completes (68S
stays set, the FIFO stays full, the 68K spins its poll budget). Rules:
(1) a FIFO transfer's 68K length and the SH-2's TCR must be the same
constant; (2) nothing with interrupts masked may run in the vint path
beyond a few lines -- the post's entry window is nine lines wide; (3)
"the transport died" is diagnosed from the FM-read V in the access
trace (entry late) before anything on the SH-2 is suspected.

Addendum (erec9-11): the idle-loop push (thunk at 0x3982 via the vector
at 0xFFA0E8) runs and completes on the 68K (erec10: +100 pushes per 100
vints, ~2,500 polls each = ~24 lines) but the SH-2 saw ONE landing:
re-arming at every vblank reset the DMA under a transfer in flight.
With the push unmasked and the re-arm conditional on consumption
(erec11) the transport lives (posts every vint, the game starts at the
script's press) but the channel deadlocks after the first transfer
(arms 0, landings 0), speed 40.3%, palgate CRAM 16-47 nz 9. Without an
arm/echo handshake the 68K cannot know whether its push will drain; the
FIFO route solved this with ARMGATE (COMM4 0xA001 echo). EARLYREC
needs the same: the master's arm sequence in the FB packet header the
68K already consumes, push only when it advanced.

Addendum (erec12, the FIFO residue): with the arm-sequence handshake the
transport is healthy (50.5%, gates pass) and every push completes on
the 68K while the SH-2's DMA also completes (TCR 0, TE set) -- yet the
tag is never at word 0: the landed buffer starts with the PREVIOUS
push's tail (5AA5 A55A at words 6-7) and the tag sits at word 8. One
aborted push left 8 words -- the FIFO's depth -- in the FIFO, and since
each DMA counts exactly the pushed length, those 8 ride at the head of
every later landing forever. The 68K must reset the FIFO before each
push: 68S 0 -> 4 clears the pointers (md_main.c's own mdspr note).
**Rule:** a FIFO transport needs a reset at the start of every
transfer, not just an exact length; a residue offsets every landing
that follows and no length check sees it.

Addendum (erec13, the lag is not a residue): with 68S reset before every
push the landing is STILL the previous transfer's last 8 words then the
new one -- on ares the DREQ FIFO path delivers 8 words (its depth) late
and a transfer's tail stays in the pipe until the next push. No length
or reset fixes that; the protocol must: every push ends with 8 zeros,
the payload is 268 words, and the receiver finds the tag at word 0 (no
lag: hardware, possibly) or word 8 (ares) and reads fields relative to
it. The old FIFO route's 512-word pushes hid this behind their padding.

### EARLYREC works end to end on ares and gains nothing there (erec14, 2026-09-23)

30 pushes, 30 landings, 30 early launches per 60 vints on the fight;
scene gate 19/19, palgate PASS, the game starts on the script's press;
speed 50.7% (line 50.0%). The launch fires at line 196 on average (max
259): the idle-loop push happens when the game's pass ENDS, which on
heavy frames is line 170-250, so the compose is still open at the next
post and the window chases it as before. The records are final at the
IRQ4 exit (~90), but a push there costs ~24 lines of 68K time on ares
(the FIFO at ~5 us a word) and delays the pass by the same amount: the
frame stays over 262 either way. So on ares the FIFO's own cost is the
floor of this design; the FPGA's FIFO rate is unmeasured and decides
whether an IRQ4-exit push wins there. Knob kept (EARLYREC=1, off the
line); protocol proven: fixed 276-word transfer, arm sequence in the
packet header, conditional re-arm with staleness, FIFO reset, 8-word
lag tolerated.

**Rules:** an early-delivery design is priced by the DELIVERY cost on
the sending CPU, not by the receiver's idle time; and measure a
transport's per-word cost on both machines before building on it.

### EARLYREC's push at the IRQ4 exit is a red screen on the FPGA (2026-09-23, rig)

rom/night/rigerec.32x (EARLYREC + ERECIRQ4 + the barcode) shows a red
screen on the rig from boot, and in ares it shows no barcode at f799
either. The control rom (line + barcode + the game timer) ran. On real
silicon a 68K write into a DREQ FIFO that no DMA drains STALLS the 68K
(the lost-push notes: "unarmed pushes wedge"); ares drops the words and
moves on. A push that can run before the master has armed -- boot, the
attract, any vint the arm sequence is stale -- is therefore a hang on
hardware, not a lost frame. Any FIFO transport needs the arm-echo gate
on every push, and the rig is the only place that proves it.
The rig game-rate readout (RIGGAME) also needs the timer's high byte:
captures land ~390 vints apart and an 8-bit timer wraps (rigctl read
44/32/22/11% -- meaningless). Byte 6 now carries it.

### EARLYREC runs on the FPGA and is slower there (2026-09-23, rig)

With the IRQ4-exit hook kept masked (no SR drop: the drop re-entered
the ISR, ares vc stopped at 6, the FPGA showed the red screen),
rom/night/rigerec2.32x boots on the rig and its barcode reads. The
game-rate readout (RIGGAME, 16-bit scene timer on barcode bytes 5-6,
same-scene capture pairs ~437 vints apart) gives, in game frames per
vint:

    level-1 demo (step 5)   control rigctl2 42, 38   rigerec2 35, 33, 31
    attract step 1          control        45, 37, 38   rigerec2 41, 30, 30

The early push is a net loss of ~7 points on hardware, as on ares: the
68K pays the 276-word FIFO push in its own frame, and that push is
larger than the compose time it moves out of the window. EARLYREC=1
stays a knob, off the line; it is dead as a speed lever on both
machines.

**Rule:** the sending CPU's transport cost is the price of any early
delivery, and the 68K's FIFO write is expensive on both machines.
Measure it before building the receiver.

### Speculative compose: the fight does not predict (2026-09-23, study)

Mike's idea: predict the next frame's sprite records from the last
two, compose them in the master's idle time, and ship if the real
records match at the post. Measured on spd2's per-frame sprite-table
dumps (spr_1000-1040 walking, spr_3000-3040 zombie fight; consecutive
DISTINCT game frames by the scene timer; velocity predictor = x and
top/bottom y extrapolated linearly, all other words held):

    walking  39 steps, 1.2 records/frame: identical 77%, whole-frame
             velocity hit 74%, per-record 82%
    fight    19 steps, 20 records/frame: identical 11%, whole-frame
             velocity hit 0%, per-record 83%

Walking already runs one vint per frame (regime map), so the 74% buys
nothing. The fight is where the 2-vint frames are, and no whole frame
predicts there: one of twenty records changes animation phase or
appears/disappears every frame. A per-record scheme would need a
repair path for the ~17% misses, and sprite compose is order-dependent
(a missed record can be overlapped by hits), so the repair is a
partial recompose plus a re-blit of the touched cells on the FB-write-
bound side. Not built; the numbers are the answer until Mike wants the
per-record variant priced.

**Rule:** price a prediction on the regime that is slow, not on the
one that is easy to predict.

Addendum (per-record speculation priced, 2026-09-23): with the same
predictor, the records that must be recomposed at the post (misses plus
any hit whose box overlaps a miss's old or new box, 32 px assumed
width) are 71 of 380 in the fight (19%) and 18 of 47 walking (38%). So
per-record speculation could take ~80% of the fight's compose out of
the window on ares. It still cannot reach one vint on the FPGA: the
blit ALONE measures 67-86 lines there (RIGBLIT), over the 68-line
threshold with zero compose, and the repaired records must be re-blitted
on that same FB-write-bound path. Not built: the FPGA's window is bytes,
and speculation moves no bytes.

### What the fight's framebuffer bytes are (2026-09-23, DRAM dumps of spd2 under the level-1 play script)

One ares run per frame, bank A of the 32X DRAM (0x20000 bytes; a bank
is 128 KB, the picture is 0x200 + 224 x 320), consecutive ships into
the SAME bank compared group by group (32 B):

    crowded fight f3000-3011 (timer 1643-1648): live groups 867 (27 KB,
      154 live rows); ship-to-ship change 126 groups (3.9 KB) then 18
      (0.6 KB); erase 0.2 KB; live rows unchanged 59% then 88%.
    walking f1000-1005: live 228-260 groups (7-8 KB, 82-86 rows);
      change 221-226 groups (7 KB) a ship; rows unchanged 10-16%.

So the ~28 KB a compose window stores in the fight is the live picture
(the grave platforms are SPRITES, seven static records, plus the actors
and the text); the ERASE share is under 1 KB and the auto-fill lever is
dead before it is built. What changes between ships in the crowded
regime is 1-4 KB: the 2-vint frames there are spent re-storing content
the bank already holds. While walking everything changes and every
byte must go, but that regime is already one vint. A content-aware row
skip pays exactly where the 50% regime is. Bank B was not dumped (the
first pass split bank A in two by mistake: "bank 1" with a zero line
table was bank A's upper half, the packet staging area).

**Rule:** a 32X framebuffer bank is 0x20000 bytes; dump both banks
before naming one.

Addendum (the 50% regime sampled, 8 points f1300-4000, bank A ship to
ship): live rows 85-166 (8-30 KB live), unchanged live rows 28 / 52 /
41 / 15 / 88 / 31 / 48 / 77% -- the fight is not still, it is
half-still: on a typical crowded frame about half the live rows carry
the same bytes the bank already holds. ROWGEN (write-tracked, sprite
spans of the last two snapshots) skipped 184,977 rows in 4,100 frames
under the play script with 0 mismatches against sbuf (ROWGENVERIFY,
every skipped row read back uncached), ~45 rows a vint.

### ROWGEN on the FPGA: 16 blit lines off the master, zero game frames gained (2026-09-24, rig, 3 launches each)

Line + RIGBARCODE + RIGBLIT (+BODYPROF for .ramtext) against the same
with ROWGEN=1 (write-tracked row skip, exact on ares: 0/184,977). The
master's blit_half in lines, deduped by vint, same attract positions:

    scene              control (3 launches)        ROWGEN (3 launches)
    level-1 demo (5)   69 92 70 / 69 68 69 / 69 79 69   53 76 56 / 54 72 / 53 ..
    attract step 1     84 86 91 / 92 71 70            79 86 85 / 85 81 68
    title (0)          44 44                          7 7

Game frames per vint (RIGGAME pair, the 68K scene timer, same-scene
capture pairs ~437 vints apart):

    level-1 demo (5)   control 42 38 / 41 38 / 40 39 35   ROWGEN 41 39 / 42 38 / 41 39
    attract step 1     control 37 37 / 53 38 38          ROWGEN 44 37 38 / 44 37 38 / 53 38 38

Sixteen lines off the master's blit on the demo and the game rate did
not move by a point. So the master's blit is NOT the window's pole on
the FPGA: the window ends when something else ends -- the slave's half
(its compose plus its blit), the chase, or the publish -- and the
threshold is not crossed by shortening the master alone. The FPGA
window-end line is the missing number; RIGWIN (barcode bytes 5-6 = the
68K's IRQ4-exit V and vblank-entry V) measures it next, control vs
ROWGEN.

**Rule:** a blit-line reduction is not a window reduction until the
window's end moves; instrument the end, not the part.

Addendum (RIGWIN, 2026-09-24, 3 launches each): barcode byte 5 = the
raw V counter at the game's IRQ4 rte (the previous vint), byte 6 = the
V at the shim's vblank entry (225 on every capture, the sanity value).
On the level-1 demo the frame's rte lands at V 32-47 of the SECOND
vint on both roms (control 35 39 44 / 47 42 36 / 44 32 39; ROWGEN 33 61
42 / 43 32 66 / 36 37 41), i.e. a demo frame is ~262+40 = ~300 lines
on the FPGA, ~40 over the one-vint threshold, and ROWGEN's 16 blit
lines do not move that end at all. Whatever ends the window on the
FPGA, it is not the master's blit_half. RIGSLAVE (byte 6 = the slave's
blit_half in lines) is the next reading.

### The FPGA window body is the MASTER path, ~106 lines on the level-1 demo, and it does not wait for the slave (2026-09-24, RIGBODY, 3 launches each)

RIGBODY (BODYPROF's per-window stage ticks on the barcode: byte 5 =
post pickup -> ack in lines, byte 6 = stage 3, the master's wait for
the slave's half). Level-1 demo, same attract positions:

    control  113 133 106 / 103 101 98 / 90 113 101    slave wait 0 on every capture
    ROWGEN    71  91  99 /  71  73 73 / 103 93  99    slave wait 0
    step 1   control 81-134, ROWGEN 84-115; title control 75-77, ROWGEN 39-40

So on the FPGA: (1) the master never waits for the slave -- the slave's
half (its compose and blit) finishes inside the master's; the master
path IS the window; (2) the body is ~106 lines on the demo against the
~68 the heavy-frame threshold needs; ROWGEN takes ~19 off (matching its
16 blit lines) and lands at ~87, still over, which is why its game rate
did not move; (3) the body minus the master's blit (67-92) leaves ~35-45
lines of non-blit master stages (launch, pickup wait, ph_ship, rearm,
publish) -- the stage split is the next reading (RIGBODYA/B).

**Rule:** on the FPGA the window = the master's own path. Price every
lever against the master's stages, not against the slave or the
combined FB bytes.

Addendum (stage split, RIGBODYA/B, 2 launches per pair): level-1 demo,
lines per window: launch (stage 0) 5-8 with spikes to 27; master half
(1) 67-90 = the blit exactly; slave pickup wait (2) 0-10; ship promote
(4) 0; rearm (6) 0; publish to the ack (7) 0. Step 1: launch 7-31,
master half 78-86, pickup 0-15. The FPGA window is the master's blit
plus ~10-15 lines of launch and pickup; the publish that costs ares 16
lines costs the FPGA under one. (The RIGBODY totals of 90-133 run above
the stage sum because bplast[] keeps a stale value for any stage a
window does not mark; read the stages, not the sum.)
CORRECTION (same night): barcode BYTE 6 rode only the STUB publish; on
full publishes d[3]'s high byte carried the stale-cell count, so every
byte-6 reading above (stages 3, 4, 6, 7 = "0", and RIGBLIT's groups/8)
is unverified. Fixed (both full-publish sites stamp RIGB6 under
RIG_BLIT); stages 3 and 7 re-read on the repaired bytes: slave wait (3)
= 0 (it was on byte 5, valid all along), publish to the ack (7) = 7-13
lines (NOT under one). Demo window on the FPGA, control: launch ~7 +
blit ~68 + pickup ~5 + publish ~11 = ~91; ROWGEN ~77.

Addendum (ROWGRP re-tried as a candidate, 2026-09-24): line + ROWGEN +
ROWGRP + BLITAUDITDIV=16 builds (three RAM trims) and runs the play
script at 50.5% on ares, but the f3000 picture shows purple stale cells
in the upper right (cells the group mask never marked, so never cleared
and never shipped). ROWGRP is not exact as built; its rig body number
is a bound on what a compose-side group mask could buy, not a
candidate.

### The 68K's window end on the FPGA is ~190 lines after the vint; the SH-2's window is only half of it (2026-09-24, RIGWIN byte 6 = V at the first gate that waited on FM)

The patcher's shared gate spin stamps the raw V counter the first time a
gate finds FM=1 (the moment the 68K may write the FB again = the window
end as the game sees it). Level-1 demo, 2 launches each:

    control  V 164 147 154 / 155 221 165     (step 1: 141 158 148 / 168 145 156)
    ROWGEN   V 169 141 141 / 161 186 198     (step 1: 141 150 147 / 184 151)

The vint fires at V ~224, so V 155 is ~190 lines after it.
RETRACTED the same hour: the post is NOT late. RIGWIN3 (byte 5 = the
live post's own V stamp, 0xFFA0A0) reads V 237-245 on the FPGA demo =
13-21 lines after the vint, the same as ares. So the FPGA window runs
from ~line 20 to the 68K's release at ~V 125-165 (~160-200 after the
vint): 110-150 lines from post to release, against the SH-2 stage sum
of ~91-106 from its pickup. The gap (~20-40) is the master's pickup
latency after the post plus the 68K's latency from the FM drop to its
first gate. One vint on the demo needs the release by ~line 106 (262 -
IRQ4 10 - pass ~146), i.e. a window of ~85 from the post; it is
110-150 today and ROWGEN's 16 do not show above the spread.

**Rule:** the window is post -> the 68K's release, not the SH-2's body;
instrument both ends on hardware before pricing a lever.

### ROWGEN is on the line: rg1 (2026-09-24, Mike's call, rig 3/3)

spd2 + ROWGEN=1 = rg1. Gates: ares boot (778 vints at f800), palette
gate PASS, play-script rate 50.4%, ROWGENVERIFY 0 mismatches over
184,977 skipped rows; rig 3 launches, level-1 and level-2 demos and the
scores clean. It buys 16 master blit lines on hardware and no game
frames yet; it goes on the line because it is exact and every further
lever stacks on it. The three .ramtext trims (bm_reset, rowgen_palsig,
cache_fill to ROM) apply only to barcode probes and ROWGRP builds; the
line itself fits without them (ends 0x06018368).

### The master's post-ack walker slice runs 50-255 lines on the FPGA and overruns the next post (2026-09-24, RIGBODYA=9 RIGBODYB=8, 2 launches)

Stage 8 (ack -> the walker slice's end: the NT/tile chunk builder, one
tile + 4 B + 4 A chunks a slice, cart reads) reads 50-255 lines on the
level-1 demo (81 178 50 / 212 106 85 104 89 ...); stage 9 (slice end ->
the next post pickup) reads either 3-11 (the post was already waiting)
or 84-255 (an idle vint). The ack lands ~150 lines after the vint, so a
100-250 line slice ends 250-400 lines after it, past the NEXT post at
~17: the master picks the post up when the slice lets it. That is the
hand-off latency the SH-2 stages could not see. On ares the slice is
short (cart reads cost nothing) and the pickup is prompt. Lever:
bound the slice by a deadline (FRT since the V-ISR) or poll the post
between chunks; the direct pickup-line reading (RIGPICK) prices it.

### The master picks the post up 15-70 lines late on the FPGA (2026-09-24, RIGPICK)

Byte 5 = (t_vint - visr_t0)/46, the master's pickup line after its own
V-ISR; byte 6 = the post-ack walker slice in lines. Level-1 demo, launch
1: pickup at 57 52 89 31 62 28 221 59 with the post at ~17 (RIGWIN3),
slice 255 119 83 78 112 90 213 46. The pickup lags the post by the
remainder of whatever idle chunk (tile batch or NT rows, cart reads)
the master was in: 15-70 lines, once 200. On ares the same chunks are
short and the lag is ~0, which is why every SH-2 stage sum came up
20-40 lines short of the 68K's post->release span. POSTPOLL (built
next) makes the chunk builders check COMM0 per tile / per NT row and
stop at a waiting post; the deferred rows ship as "nothing changed" and
go next visit.

**Rule:** on hardware the idle work between windows is not free -- it
holds the pickup. Every idle task must be preemptible at the post.

### POSTPOLL: the chunk builders yield to the post, the pickup improves in half the samples, the game rate does not move (2026-09-24, rig)

pp1 = rg1 + POSTPOLL (tile batch stops per tile, NT rows ship "nothing
changed" when COMM0 is set). Ares: boots (782), palette PASS, 50.5%,
f3000/f4100 pictures clean. Rig RIGPICK, 2 launches: pickup 48 19 29 /
28 125 88 (demo), 80 143 / 71 29 110 29 (step 1) against 52 89 31 / 29
54 58 without it: the demo's pickup reaches ~20-30 (post ~17) on half
the captures and stays at 88-143 on the rest -- idle work the two
checks do not cover (cache_fill, the art tail, cut prefetch, the
compose strips' own atomicity) still holds the poll. Game rate (RIGGAME
pair, 3 launches): POSTPOLL demo 40 38 35 / 41 38 35 / (L3 same) vs
control 42 38 / 41 38 / 40 39 35: no change. Dead as a lever on its
own; kept as a knob (POSTPOLL=1) because it is correct and cheap.
Next: name the idle task at the pickup (byte 6 = the last idle task id
before the post was seen) and make every one of them yield.

Addendum (RIGTASK, 2026-09-24): byte 6 = the id of the idle task begun
last before the pickup, with POSTPOLL v1 on. Every late pickup (47 54
59 77 78 88 175 lines) was task 10, the name-table chunk builder; the
early ones (25 27 28 38) were the maps drain (5) or a LUT chunk (2).
POSTPOLL v1's row check sat AFTER the row's 40-cell compute (the cart
reads), so it cut only the diff emit. v2 checks at the top of the row
and skips the compute: header + "nothing changed", mirror untouched.

Addendum (POSTPOLL v2 on the rig, 2026-09-24): pickup 93 67 73 / 21 183
41 (demo), 38 86 98 65 / 46 67 34 22 (step 1), tasks 10 and 5 -- the
row skip did not shorten the NT task, so its time is outside the row
loop (sub-tags 11/12/13 next). Game rate, 3 launches: demo 44 37 / 44 37
/ 36, step 1 40 37 40 / 40 37 40 / 40 37 41, control 42 38 / 41 38 and
37 37 / 53 38 38: +2-3 points on step 1, none on the demo. Not a
threshold crossing; kept as a knob.

### The hand-off is the master's V-ISR: 41-82 lines on the FPGA (2026-09-24, RIGISR)

Byte 6 = the master's V-ISR, entry to exit, in lines (stamped at all 11
exits); byte 5 = the pickup line. Level-1 demo, launch 1: ISR 82 78 76
66 41, pickup 136 88 92 97 71; step 1: ISR 23 28 29, pickup 24 33 29.
The pickup is the ISR's end plus a few lines. The post arrives ~17
lines after the vint, inside the ISR, and waits for it. On ares the
ISR is a few lines. So the "idle task holding the poll" was the ISR
all along (the task tags name what ran BEFORE the ISR). RIGISR2 splits
the ISR into the packet lift (FB reads) and the CRAM paint next.

Addendum (RIGISR2): inside the ISR the packet lift is 0-2 lines and
the CRAM paint 7-22 (demo 14-17, step 1 7-19). The ISR's other 40-60
demo lines are elsewhere in flip_span: the text-plane capture from the
FB (up to 928 longs of FB reads), the page merge, the truth drain or
the flip itself. VB_SPAN's marks (RIGVBS) split those next.

Addendum (RIGISR3): the text capture is 0-10 lines and the text end ->
the FS write 0-1. So before the flip write the ISR is lift 0-2 + text
0-10 + drain/merge ~1 = under 15 lines; the CRAM paint (after the
flip) is 7-22; the ISR measures 41-82 on the demo. The remaining 30-50
lines are AFTER the FS write and outside the CRAM paint -- RIGISR4
reads that span.

Addendum (the ISR's shape on the FPGA, from the code and the splits so
far): entry -> lift 0-2 -> text capture 0-10 -> FS write at ~+15 ->
CRAM paint 7-22 (PEN-safe, in vblank) -> SPIN on the FS latch (the
FPGA takes the flip at vblank END, ~38 lines after the vint; the ISR
waits for it, 18000-tick backstop; the comment at the spin says the
wait is for the staging pages) -> restore_pages into the new back
bank (up to 13 pages x 2 KB of FB stores, ~3.4 lines a page at 4.8
clocks a word) -> the publish copies (2 x 368 longs + MDSPR) -> exit
at +41..82. The post lands at +17 inside that. Everything after the
latch is FB writes that cannot precede the flip; the lever is their
SIZE (dirty pages a vint) and whether the master's window launch can
start before them (compose is SDRAM-only).

Addendum (RIGISR4/5): after the FS write the ISR spends 28-48 lines on
the vints that flip (byte 5 of RIGISR4; the 255s are stale stamps from
declined flips): the latch wait to vblank end (~10-20) plus the copies
into the new back bank -- restore_pages 0 (43 pages in 2,432 vints on
ares), the publish copies A/B (2 x 368 longs), the MDSPR tables and
the 4 KB text restore: 19-30 lines of FB stores at that point (RIGISR5
byte 6). The pickup follows at the ISR's exit. ISRSPLIT=1 (built
2026-09-24 05:20): the ISR returns at the FS write; flip_finish (latch
wait, restores, copies) runs in the window body after the launch, so
the slave composes while the master waits for the latch; blit_half on
both CPUs spins on FLIP_PEND_U before its first store; the ISR entry
finishes a pending flip first as a belt. ares gates and the rig next.

Addendum (ISRSPLIT on ares and the rig, 2026-09-24 ~06:00): is1 (rg1 +
ISRSPLIT, flip_finish AFTER the launch) boots, palette gate PASS, 50.7%,
and the rig's pickup moves to +22..40 on the demo (was +55..90) -- but
the wolf composes RED from f3000 on, with CRAM identical to rg1's (the
pens differ, not the palette). is3 (ISRSPLITPRE: flip_finish BEFORE
the launch, i.e. pickup -> latch wait -> copies -> launch) composes the
wolf white. So something the launch or the slave's compose reads must
follow the post-latch copies; not yet named. is3 keeps the pickup gain
and the CRAM-under-the-wait overlap but delays the launch to ~+55;
its rig pickup and game rate are queued.

Addendum (is1 game rate, launch 1): demo 41 41 35, step 1 36 41 --
the control's numbers. Moving the pickup to +22..40 did not cross the
threshold because the latch wait (dead until +38) and the ~25 lines of
copies still sit between the pickup and the blit. Copies census on
ares (rg1, 4,075 vints): packet A is never carried across the swap,
packet B on 1,986 of 2,398 flips (the 68K's plane-packet slot), the
MDSPR tables every flip, the text restore every flip (4 KB, the
largest). TEXTRESTMASK (built) restores only the text groups the game
wrote since the bank last held them.

Addendum (TEXTRESTMASK on ares): tr1 = rg1 + TEXTRESTMASK boots (778),
palette gate PASS (29 at f2004, rg1's value), 50.4%, and both banks'
text pages equal TEXT_U word for word at f1500, f3000 and f4100 (0 of
1,856 differ, either bank). Exact. The rig prices its ISR saving next.

Addendum (is3 on the rig): pickup 44 115 29 / 46 84 77 (demo), 53 28 22
/ 133 106 37 (step 1) -- no longer pinned to the ISR's end but held by
body work before the poll; game rate demo 41 40 36 = the control. Both
split orderings are rate-neutral: with the launch after the copies the
slave starts at ~+45 and the release lands where it did. The form that
could cross (launch at +25, copies at +38 overlapping the slave's
compose) is the one with the red wolf; its hazard is not yet named.

### The 4 KB text restore cost the FPGA ~40 lines a flip; masked it is ~2 (2026-09-24, RIGISR on tr1)

tr1 = rg1 + TEXTRESTMASK. Level-1 demo, launch 1: V-ISR 24 27 29 lines
(rg1: 66-82), pickup 51 27 29 (rg1: 55-90); step 1: ISR 24-61, pickup
45-65. The restore wrote 1,024 longs through the cached alias of the
FB every flip; on the FPGA that path cost ~40 lines, not the 7 the
word count predicts at 4.8 clocks a word -- the cached-alias store path
is far slower than the blit's. Restoring only the groups the game
touched (typically 1-2 of 8) removes ~40 lines from the ISR, i.e.
from the pickup. Exact on ares (both banks equal TEXT_U). Game rate
pair queued.

**Rule:** on the FPGA, price every FB store path separately; the
cached-alias write path and the blit's path are not the same speed.

CORRECTION (tr1 on the rig): with the masked restore the whole ISR is
22-29 lines on the demo, so the FS latch is NOT a wait to vblank end
on the FPGA -- it takes in vblank as on ares; the 28-48 post-flip
lines were the copies, and the 4 KB text restore through the cached
alias was ~40 of them. And tr1's game rate (launch 1: demo 42 38,
step 1 45 38 37) is the control's: the pickup at +20..30 instead of
+55..90 buys no frames. The release line and the 68K's frame end on
tr1 (RIGWIN) and its stage split (RIGBODYA=1,7) are queued; until they
read, the threshold model (release by ~+106) is unverified on hardware.

### Every hand-off cut lands at the same game rate on the FPGA; the frame is not bound by the window end (2026-09-24 ~07:00)

Three launches each, RIGGAME pairs against the control (spd2 + the
barcode): ROWGEN (-16 blit lines) 41 39 / 42 38; POSTPOLL v1 40 38 35;
v2 44 37 / 44 37 / 36; ISRSPLIT (pickup +22..40) 41 41 35; is3 41 40
36; TEXTRESTMASK (ISR 66-82 -> 22-29, pickup +20..30) 42 38 / 42 38;
+ audit 1-in-16 41 38. Control 42 38 / 41 38 / 40 39 35 every time.
Cuts of 16-50 lines at three different places in the window changed
nothing, so on the FPGA the game's frame length is set by something
other than when the window ends. The 68K's pass itself is the unmeasured
term (it runs from the cart under contention with two SH-2s); RIGWIN4
(byte 6 = vints spanned from the shim's vblank entry to the game's
rte, byte 5 = V at the rte) reads the frame's span directly.

Addendum (RIGWIN4 on the rig): the game's IRQ4 rte lands at raw V
35-61 = +73..99 lines after the vint on the FPGA, the same band as
ares's 79-91 (Addendum 8), with 0 vints spanned from the shim's
vblank entry (byte 6 = 0 modulo a flag bit). So the 68K's vint-side
work runs at ares speed on the FPGA. And the RIGWIN gate-exit stamp
(byte 6 = V at the first gate that waited) is written ONLY on vints
where a gate actually spun; on vints where the drop came before the
game's first FB writer the value is stale from an earlier vint, so
its 141-221 cannot be read as "the drop moved or did not". The clean
window-end number is the master's own ack line (RIGACK, queued for
rg1 and tr1).

### The window's ack on the FPGA, read directly, and why an earlier pickup did not move it (2026-09-24, RIGACK)

Byte 5 = the ack line after the master's V-ISR, byte 6 = the pickup.
rg1, level-1 demo: ack 131 172 144 / 117 115 141 with pickups 43 140
50 / 29 56 53 -- the body from pickup to ack is ~88 lines. tr1 (masked
text restore, pickup ~+28): ack 49 145 195 with pickups 28 29 (228
stale); step 1: ack 155-213 with pickups 56-71 -- the body GREW to
115-165 lines when the window starts inside the 68K's IRQ4 span
(+15..+95, which runs at ares speed on the FPGA, RIGWIN4). So on the
FPGA the master's FB work is slowed by the 68K's concurrent vint-side
traffic (its IRQ4 uploads and its tight FM-gate spin, a 32X register
read every ~4 us), and the ack floor sits at ~+115-145 whatever the
pickup. That is why ROWGEN, POSTPOLL, ISRSPLIT and TEXTRESTMASK were
all rate-neutral. GATESLOW (built): 24 nops between the gate's FM
polls, ~17 us a read instead of ~4; its ack line is queued.

**Rule:** on the FPGA the two CPUs' bus traffic is not independent;
a window that starts earlier can take longer. Measure the ack, not
the parts.

Addendum (GATESLOW on the rig, launch 1): acks 83 154 127 (demo,
pickups 39 65 40), 110 189 188 (step 1): no better than tr1. The
68K's tight FM poll is not what slows the master's body. Left: the
game's IRQ4 uploads (VDP DMA from the cart while the master stores to
the FB) or the FPGA core's own arbitration between the two ports.
Knob kept (patcher, GATESLOW=1), off the line.

Addendum (MDQUIET A/B, variant 2 on the rig): the 68K's shim DMAs and
fetches off (slim_dma, slim_fetch, mdspr_consume return at once), the
game's IRQ4 still running: acks 167 156 113 (demo, pickups 20 94 27),
128 206 56 (step 1) -- the same band as rg1. The shim's own DMA
traffic is not what slows the master's body. Variant 1 (no game IRQ4
at all) showed no barcode because the display gate never opened;
DISPFORCE (the master never blanks the 32X layer) makes it readable
and is queued.

Addendum (MDQUIET variant 1 on the rig, display forced): the 68K posts
and nothing else (no game IRQ4, no shim DMA): acks 79-126, pickups
33-67, body ~45-68 lines from pickup -- the ares-like number, against
~88 on rg1 and 115-165 on tr1. Caveat: the game never runs, so the
picture is frozen at a lighter scene (palette empty); the shorter body
may be content, not quiet. Same-content test queued: RIGPICKBLIT
(byte 5 pickup, byte 6 the master's blit_half lines) on tr1, whose
pickups spread 20-140 -- if blits that start inside the 68K's IRQ4
span (+15..95) run longer, the contention is the game's own vint work.

### The contention reading does not survive same-position pairs, and the ack-minus-pickup arithmetic was wrong (2026-09-24 11:30 ET)

RIGPICKBLIT on tr1 (byte 5 pickup, byte 6 the master's blit_half
lines), two launches at matching attract positions: ~3280 early start
88 vs late 76; ~3718 early 71 vs late 54; ~4157 52 vs 54; ~5394 33 vs
35; ~5832 66 vs 68; ~6269 late 80 vs early 74; ~6706 late 84 vs early
64; ~7111 26 vs 27. Blit length follows the scene position, not the
start line. The "blit runs longer inside the 68K's IRQ4 span" reading
is not supported.
And the RIGACK arithmetic: the ack byte a packet carries is the
PREVIOUS window's (the publish precedes the ack), the pickup byte is
this window's, so "body = ack - pickup" paired different windows; every
body figure derived that way (88, 115-165, 45-68) is void. What
stands: the ack lines themselves -- rg1 115-172, tr1 110-195, shim
DMAs off 113-167, gate spin slowed 83-154, 68K silent 79-126 (lighter
picture) on the level-1 demo -- one band. Nothing measured tonight
names why the FPGA window ends ~50 lines later than ares's.

**Rule:** a barcode byte is one window's value; two bytes are the same
window only if both are stamped before the publish. Pair by design,
not by subtraction.

Addendum (UPLOADSKIP A/B, ares): skipping the game's IRQ4 uploads on
odd vints (jump from 0x2AC6 to the handler's tail at 0x2C06, frame flag
still advanced) breaks the plane pipeline -- garbage plane rows at
f4100 -- and the game's rate on ares falls 50.8% -> 36.2%: the skipped
block also carries state the main loop paces on. The "same picture,
same logic" premise fails; the rig's ack-by-parity reading is kept as
a per-window number with that caveat.

Addendum (UPLOADSKIP on the rig, 3 launches, ack by the parity of the
window's vint): level-1 demo, windows after an UPLOAD vint ack at 138
139 150 151 165 (mean 149), after a SKIP vint at 107 112 149 (mean
123); step 1 the other way (112 vs 136, n=7/3); steps 0/2/3/4 mixed on
1-4 samples. A ~25-line earlier ack on the demo when the 68K's IRQ4
uploads did not run alongside, on a rom whose game pacing is broken
(36% on ares). SPRSKIP (sprite copy only, pacing intact at 50.0%) is
the clean read.

### The 68K's vint traffic does not slow the FPGA window: closed on three probes (2026-09-24 11:40 ET)

SPRSKIP (the game's IRQ4 skips only its sprite RAM copy on odd vints;
pacing intact, 50.0% on ares), 3 launches, ack by the parity of the
window's vint: demo after an UPLOAD vint 55 133 159, after a SKIP vint
72 113 168 173 203; step 1 100 (n=6) vs 126 (n=6); steps 0/2/3/4 the
other way on 1-3 samples. No effect above the spread. With GATESLOW
(polls) and MDQUIET=2 (shim DMAs) also flat, the 68K side is cleared.
The ack band 110-195 is the stage sum with the pickup's spread in it:
pickup 20-140 + launch ~7 + blit 53-90 + publish ~11. The FPGA's
excess over ares is the blit's own rate (BLASTEM.md 13: stores 23 /
instructions 23 / loads 13 / audit 8) and the pickup's variance.

**Rule:** when a window's end will not move, sum the stages you have
measured on that machine before hunting a hidden term; here there was
none.

### The four fixes stacked do not cross the threshold (2026-09-24 12:15 ET, rig 3 launches)

all1 = rg1 + TEXTRESTMASK + POSTPOLL + BLITAUDITDIV=16 (ROWGEN on the
line). Ares: boots, palette PASS, 50.4%. Rig, game frames per vint:
demo 43 36 / 43 36 / 43 .., step 1 35 32 33 -- the control's 42 38 /
37 37, step 1 a few points LOWER. The stage sum (~+100) predicted the
threshold; the frame did not follow. So the one-vint threshold on the
FPGA is not where the ares-derived arithmetic puts it, or the stage
sum misses a term that only the game's own frame sees. The knobs stay
knobs; rg1 stays the line.

### The unread term: on the FPGA about half the demo frames are THREE vints, and the third is the next window's spin (2026-09-24 12:50 ET, RIGSPAN, 3 rig launches)

RIGSPAN: the patcher's idle-loop thunk (0x3982) calls span_hook on
every arrival; a pass is armed when the arrival finds the frame flag
set and stamped at its next arrival: byte 5 = raw V at the pass end,
byte 6 = vints elapsed (0 = a one-vint frame, 1 = two, 2 = three; the
shim's counter 0xFFB0F0 ticks once per vint). Samples are one per
screenshot, deduped by vint across the three launches (the first
rigspan2 rom stamped byte 5 = pkt; those rows are dropped).
    ares: title 0 (V 185); attract demo f3400/f3900 1 (V 217, 190);
          play f1500/f3000 2 (V 151)
    FPGA: demo (step 5, 11 samples): span 1 x6 (V 195-222),
                                     span 2 x5 (V 156-202)
          step 1 (13): span 2 x5, span 1 x7, span 0 x1
          step 3 (4): span 2 x4;  step 2 (5): span 0 x4, span 1 x1
          title (3): span 1, 0, 0
Cross-check: demo 6x2 + 5x3 = 27 vints / 11 frames = 41% -- the
frame-rate counter reads 42% on the same scene. Step 1: 30/13 = 43%
vs 37%. The instrument agrees with the rate counter.
So the level-1 demo is all two-vint frames on ares (50%) and a
half-and-half mix of two- and three-vint frames on the FPGA (41%).
The two-vint frames end at V 195-222, 40-67 lines before the vint
ends; the three-vint frames end at V 156-202 of the third vint,
i.e. just after a window ack band (+110-195) plus a short tail. A
frame goes to three vints when its pass tail straddles the NEXT
vint's window raise (+17) and spins on that window too. That second
spin is the term no window-side stage sum contained, and it is why
every ack cut short of the straddle changed nothing: the frame is
2 or 3 vints by whether ack + tail clears 262, and the FPGA's late
acks (+150-195, the late pickups) do not.
The lever: an ack that lands early enough on EVERY window, not on
average -- the pickup spread is the tail, the body ~90 is fixed.
That buys 2-vint frames consistently = 50% like ares, +8-10 points
on the FPGA. 60 Hz is a different bar (ack + tail <= 262 from the
SAME vint) and this instrument now measures it directly.

**Rule:** measure the frame, not the window. A frame spans vints by
crossing raise points, and each crossing can cost a whole window.

### On ares the game's pass does not wait on the window at all (2026-09-24 13:50 ET, RIGWIN / RIGSPAN=2 on ares)

RIGSPAN=2 (byte 5 = the highest raw V at which one of the pass's FM
gates was released, folded per vint in shim_vblank, 0 = the pass never
waited) reads 0 on ares at play f1500/f3000 and attract f3400/f3900.
RIGWIN's raw stamp (never cleared) reads 194 at both play frames, in
two separate runs: one wait long before, none since. So ares' 2- and
3-vint frames are the 68K's own work -- game pass + IRQ4 + the shim's
vint (consumes, DMAs) -- past 262 lines, with no window spin in them.
The window matters on ares only as the shim's consume work. Whether the
FPGA's passes wait (RIGWIN stamped V 141-221 on the rig, so some do) is
what rigspk on the rig reads next; a pass that waits there and not on
ares is the FPGA-only term.

**Rule:** "the game spins on the FM window" was an ares-era inference
from the frame timeline, never a direct read; the direct read says no.

### The span instrument lost ~40% of passes to a two-read race; fixed, ares' demo is 2-vint passes at 50% and NOTHING waits on the window (2026-09-24 15:50 ET)

The idle-loop thunk read the frame flag twice: once for span_hook
(arm) and again for the game's beq. A release landing between the two
started a pass the hook never armed; the pass ran uncounted and its
vints read as "waiting". RIGSPAN=5 (vints with a pass armed / vints
waiting, counted in shim_vblank) exposed it on ares: 116 pass vints
and 288 waiting per 400, with the suspend latch 0xFFF01E clear at nine
samples and the shim's FM belt (fmgate_belt) constant at 162 -- the
game was not waiting, the hook was blind. Fix: the thunk reads
0xFFF01C once, saves the truth (sne.b -> 0xFFA0EE), the hook arms on
that byte and the caller's CCR is rebuilt from it.
    ares attract demo (step 5), fixed hook, per 400 vints:
      f2700->3100: span 1 x188, span 2 x8   (376 + 24 = 400)
      f3100->3500: span 1 x182, span 2 x12  (364 + 36 = 400)
      RIGSPAN=5: 200 pass vints / 200 release vints per 400
So ares' level-1 demo is two-vint passes (94-96%), a few three-vint,
no one-vint: 50% exactly, one release vint per pass. Every earlier
RIGSPAN number (mode 1's samples, the first histograms, the "half
2-, half 3-vint FPGA demo" and "ares all 2-vint" lines) carried the
race and is superseded by the fixed-hook reads.
Also established today, all direct reads:
  - the game's pass never waits on the FM window: RIGSPAN=2 (max
    release V per pass) is 0 on ares at four frames and on the FPGA in
    22 of 24 samples, including every 3-vint pass;
  - the shim does not wait at vint entry (belt count constant);
  - the scene timer 0xFFF02A is NOT a pass counter (280 per 397 vints
    on ares' demo vs 200 passes): retire it as a rate instrument;
  - MDQUIET=2 (shim DMAs off) on the FPGA, old hook: 3-vint passes
    165 -> 132 per 876 demo vints -- the shim's vint DMAs are ~20% of
    the FPGA's excess, not the term.

**Rule:** an instrument that reads a shared flag must read it ONCE
and share that read with the code it observes; two reads of a value
an interrupt can change is a race, and the missing events look like
idle time.

### The rig barcode itself cost the 68K ~48 lines a vint and made the demo's passes straddle; every barcode-era frame-rate number is a handicapped read (2026-09-24 17:40 ET)

The interrupted-PC ring (0xFFA200, 64 vints, stamped by the IRQ4 entry;
idle = PC in the game's wait loop 0x903980-90) on ares at demo f3100,
f3300, f3500:
    rg1 (the line, no instrument):       idle 64 / 64 at all three
    rigbc (line + RIGBARCODE only):      idle 31, game 31, ours 2
    rigsp3b (line + barcode + span hook): idle 1, ours 23 (the hook
        thunk = the wait), game 40; flag/PC at 12 consecutive frame
        ends alternate: mid-pass with the flag set, then waiting
So on ares the line's level-1 demo passes finish INSIDE their vint
(the 68K is at the wait at every vint; the miss counter 0xFFF144
reads 0 at 16 consecutive frames), and the barcode alone turns them
into two-vint passes. The cost: bc_emit's 384-word WRAM->VRAM DMA
runs from partb at ~+90, in active display, where the VDP takes ~16
bytes a line -- ~48 lines with the 68K halted, every vint. A pass that
ends within ~48 lines of the vint tips. The FPGA reads carry the same
handicap plus whatever the FPGA adds.
Consequences: every FPGA frame-rate number since the barcode went in
(RIGGAME's 42/38, the span histograms' 73-86% three-vint, the MDQUIET
and IDLEOFF A/Bs) was measured on a 68K carrying ~48 extra lines a
vint; the ares "50% demo" of the instrument roms is the instrument.
The line's own demo rate on the FPGA is UNMEASURED. RIGBCEVERY=8
(built) DMAs the barcode every 8th vint (~6 lines a vint averaged;
the plane keeps the last one); its ares ring and the FPGA histogram
are the re-read.
Kept as true: the pass never waits on the FM window (RIGSPAN=2), the
shim does not wait at vint entry, the hook's two-read race and its
fix, the ares fight (play input) being 3-vint-heavy is unverified
either way until re-read with the light barcode.

**Rule:** an instrument that runs on the CPU it measures must be
priced on that CPU first. A VRAM DMA in active display is 68K time;
a 68K frame that fits its vint by a few lines will not survive one.

### Re-read with the light barcode: the level-1 demo is 71% on ares and 50% on the FPGA, and the mass is one-vint passes turning into two (2026-09-24 18:45 ET, RIGBCEVERY=8, race-free hook)

RIGBCEVERY=8 (barcode built and DMA'd every 8th vint): ares' ring
reads idle 64/64 at f3100-3103 again, i.e. the light barcode no longer
perturbs. Span histograms per attract step (h0 one-vint, h1 two-vint,
h2 three-vint passes), the level-1 demo (step 5):
    ares  566 vints: h0 242  h1 150  h2 8    -> 400 passes = 71%
          (the game's own scene timer on the line reads 70%: the
           timer IS a pass counter; the 16:20 "not a pass counter"
           note was the handicapped rom's number)
    FPGA  440 vints: h0 34   h1 ~170 h2 16   -> ~220 passes = 50%
          (two launches: h2 16 / 22, h1 166 / 176)
    title (step 0): both machines one-vint passes (60 Hz).
So the FPGA loses ~20 points on the demo, and it loses them by turning
ares' ONE-vint passes into two-vint ones (188 -> 34 per 440), not by
three-vint passes (6 -> 16). Every 68K line saved per pass on the
FPGA converts two-vint passes back at the margin; the window is not in
the pass (RIGSPAN=2), so the lines are the game's pass + the IRQ4
chain (shim vblank + game IRQ4 + partb) + our gate/tile thunks. The
heavy-barcode A/Bs (MDQUIET=2 shim DMAs off: 3-vint 79% -> 46%;
IDLEOFF master idle off: 73-86% -> 35-63%) showed both terms move
the FPGA's passes; they need re-reading with the light barcode and
h0/h1 as the columns.

**Rule:** report the whole span distribution with the one-vint column
in it; the column that moves on the FPGA is one-vint -> two-vint.

### Light-barcode A/Bs on the FPGA: shim DMAs off and master idle off both leave the demo at ~50% passes; ares gains from either (2026-09-24 20:10 ET)

Per ~440 demo vints on the FPGA (RIGBCEVERY=8, race-free hook, two
launches each; the step's phase differs a little per rom, the pass
count per window is the comparable figure):
    control (rigsp3c/4c)     one-vint 34   two-vint ~170  three-vint 16    ~220 passes
    MDQUIET=2 (shim DMAs off) one-vint 24-28 two-vint 184  three-vint 18-22 ~230
    IDLEOFF+NOMAPS (no packets) one-vint 10 two-vint ~212 three-vint 2     ~224
On ares the same roms: control 71%; MDQUIET=2 346 one-vint / 4
three-vint by f2700; IDLEOFF 370 one-vint / 6 three-vint -- i.e.
ares goes towards 60 Hz when either term is removed, the FPGA stays
at 50%. So on the FPGA the 68K's pass is long for a reason that is
neither the shim's vint DMAs nor the master's cart-reading idle work;
what is left is the 68K's own execution speed on the FPGA core (cart
fetch through the 32X path, WRAM). RIGSPAN=8 (wait-loop arrivals per
fully idle vint: the game's jsr/beq from cart + the thunk from WRAM)
is the gauge, run on both machines.

**Rule:** an A/B whose ares result moves and whose FPGA result does
not is telling you the FPGA term is somewhere else; stop stacking.

### The FPGA's 68K is ~5% slower than ares' on both cart reads and WRAM loops (2026-09-24 21:25 ET, RIGSPAN=9)

RIGSPAN=9: every 64th vint, at the top of shim_vblank and past the V
counter's vblank jump, the shim times 256 word reads from the cart
window (0x900000, the game's code) and a 256-iteration WRAM loop on
the V/H counter, in quarter-lines. Light barcode (RIGBCEVERY=8).
    ares  cart 74  WRAM 108   (f1500, f2700, f3500: identical)
    FPGA  cart 77-80  WRAM 112-115  (11 samples, two values each)
i.e. +4-8% on cart reads and +4-6% on WRAM code, about +5% on both.
(The first cut used 1024 iterations and saturated: the raw V counter
jumps 0xEA -> 0xE5 inside vblank; the gauge now waits for V >= 0xF0.)
So the FPGA term is the 68K itself running ~5% slower -- ~12 lines on
a ~250-line pass -- and the level-1 demo's passes on ares end within
that of the vint (188 one-vint : 150 two-vint), so the FPGA loses the
marginal ones (34 : ~170). Not the window, not the shim's DMAs, not
the master's cart work: a platform constant. The lever we own is
every 68K line in the pass -- the IRQ4 chain (shim vblank + game IRQ4
+ partb), the gate/tile thunks per write, the barcode (fixed) -- and a
saved line converts the passes that sit within it of the vint.

**Rule:** before attributing a hardware deficit to a subsystem, gauge
the CPU itself on both machines; five percent is invisible in any
stage sum and decisive at a deadline.

### The 68K-line ranking of the pass on ares, level-1 demo (2026-09-24 23:40 ET, dump-based; cut nothing yet)

Instruments and their own cost: the line's existing V stamps (0xFFA0xx,
0xFFA17x, the entry/rte rings) = 0 added; CHAINPRICE (two stamps) ~0.1
line; TAILCENSUS stamps ~0.2 line; the arcade trace (MAME, the game's
own execution) = 0 on our 68K; the rig barcode = 48 lines (excluded).
Executions per pass from the arcade trace (frames 1500/3000 attract =
the level-1 demo; 4500/6000 quieter scenes; play 3000): a pass runs the
IRQ4 chain once per vint it spans (1-2), the thunks once.

The IRQ4 chain, lines after the vint (ares, line rom + CHAINPRICE,
12 + 4 consecutive frames, steady-state):
    +3    shim entry
    +13   consumes A/B done (A ~1, B ~7)
    +17   sprite pal/SAT + upload pump done
    +18   POST (COMM0 = 0x2020), r60_push entry
    +59   palette rotor + compares done   <- 41 lines
    +64   rowscroll compare done          <-  5
    +67   selection/compare done          <-  3
    +85   stage shipped (regs 2, rowscroll+pal 6-10, records+tail 2) <- 18
    +86   push/hold exit, shim end
    +112  game IRQ4 + part B done         <- 25 (the game's own ~20)
So the chain is ~110 lines a vint, of which r60_push is ~68 and the
consumes/pump ~15: about 85 lines a vint are OURS, ~25 the game's.

The per-write thunks (patched image jsr sites x arcade executions):
    site    kind                     exec/frame  ~lines/frame
    0x3A9A  FM gate + TXTWRAM mark   21-26       ~10   (a 16-byte text
            copy loop: move.b (A0)+,(A1)+ / addq / dbra -- gated PER
            ITERATION, ~230 cycles a call against 12 for the move)
    0x39A8  object-dispatcher thunk  15-19       ~2
    0x4D88  FM gate                  4-5         ~1.5
    others (8 sites)                 ~8          ~2
    109 jsr-to-thunk sites exist; 10 execute in the demo.
Thunks total ~12-14 lines a pass, three quarters of it at ONE site.

RANKING, lines x executions per pass (2-vint passes pay the chain
twice):
    1. r60_push palette rotor + compares      41 x (1-2) = 41-82
    2. r60_push stage ship (R60P words)       18 x (1-2) = 18-36
    3. shim consumes + sprite pal + pump      15 x (1-2) = 15-30
    4. 0x3A9A text-copy gate, per iteration   10 x 1
    5. r60_push rowscroll/selection            8 x (1-2)
    6. part B (post, FM raise, blast)         ~5 x (1-2)
    7. the other thunks                       ~5 x 1
    (the game's own IRQ4 ~20 x (1-2) is the arcade's; not ours)
The deficit is ~12 lines; item 1 alone is 3-7x it, item 4 alone
covers it. Conversions to report: N passes moved two-vint -> one-vint
on the demo per ~440 vints (FPGA 34 -> ?, ares 188 baseline).

**Rule:** rank by lines x executions from the machine's own stamps and
the game's own trace before touching anything; the site count (109)
said nothing -- ten sites run, one of them is the cost.

### BURSTGATE: the 0x3A9A text copy gated once per loop; exact on ares by the game's own clock (2026-09-25 01:10 ET)

The change (patch_game.py BURST GATE, knob BURSTGATE=1): the entry
thunk at 0x3A9A runs the whole  move.b (a0)+,(a1)+ / addq.l #1,a1 /
dbra  loop itself -- spin until FM=0 once, save d0/a0/a1, mark the
start 4-row group, copy, mark the end group (a burst may straddle a
512-byte group), re-read FM: if the master raised it during the copy
the bytes after the raise were dropped, so restore the registers and
redo from the spin. D0.w is left 0 so the site's own dbra falls
through as the original loop left it. The twin loop at 0x3AA4 keeps
its per-byte gate but calls the burst thunk's mark subroutine (the
thunk region is 52 bytes from the boot-stack guard; the pair is now 4
bytes larger than before, the histogram roms fit again).
Exactness on ares: a same-FRAME pixel diff is meaningless once the
game runs faster (frame 800 is a different game moment: 46,828 pixels
differ, the logo's colour cycle and the sprites moved). Aligned by the
game's scene timer 0xFFF02A (147 at the line's f800 = the burst rom's
f803/f804), the 4 KB text page (32X DRAM 0x1F000 / 0x3F000) is
identical in one bank and differs by 2 bytes in the other: glyphs
0xE99/0xE9B read 0x8C in the line's older bank and 0x8D in its newer
one; the burst rom has 0x8D in both -- one write ahead, no content
difference. Text intact on the title (50000 / INSERT COIN / SEGA 1988)
by eye.

**Rule:** compare builds of different speed at the same GAME time
(the scene timer), never at the same emulator frame.

### Item 4 converted nothing; the site runs a third as often in OUR demo as in the arcade's, and 2-vint passes do not pile up at the line (2026-09-25 03:00 ET)

BURSTGATE on the FPGA (light barcode, race-free hook, two launches each,
per ~440 level-1 demo vints): one-vint +34, two-vint ~+170, three-vint
+16 -- the control's numbers to the pass. Zero conversions. On ares
the mean pass length (RIGSPAN=11: span*262 + end line - start line,
per step) reads 246 lines over 236 passes at f2400 and 232 over 432 at
f2700 on BOTH the control and the burst rom: the cut is under the
instrument's 2-line resolution.
Why: the arcade trace's 21-26 calls a frame at 0x3A9A were the ARCADE
demo's; per-site call counters in our own rom (COUNTSITES, 0xFFA1F0
block, ~10 cycles a call, ares dumps f2400 -> f2700 = 300 vints, ~200
passes): 0x3A9A +1540, 0x3AA4 +1349, 0x4D88 +727, 0x3AAE +211, 0x3716
+211 -- 0x3A9A is ~7.7 a pass here, not 22, so the burst is worth ~3
lines a pass, the gate thunks together ~7 (arcade-derived: 8.7 for
0x3A9A alone). Execution counts must come from OUR rom; the arcade's
text traffic differs (coin state, demo phase).
And the "154 passes per 12 lines" density was wrong: RIGSPAN=10 on
ares splits span-1 passes by where they end in their second vint,
lines 0-130 vs 131-261: +30 vs +34 per 296 vints (control), +46 vs
+46 (burst) -- spread across the vint, not stacked just past the line.
A 3-line cut converts ~2 passes in 300 vints. The FPGA's 34 vs ares'
188 one-vint passes is therefore NOT a 12-line shift of a piled-up
distribution; RIGSPAN=11 on the rig (mean pass length, FPGA vs ares'
232-246) is the direct read of how many lines the FPGA adds per pass.
Instrument notes: the COUNTBURST word 0xFFA3C4 and the CHAINPRICE
stamps 0xFFA3C0/C2 sit inside the entry ring 0xFFA380-0xFFA3FF (slots
32-34, rewritten once per 64 vints); the COUNTBURST count was garbage,
the CHAINPRICE reads were consistent across 16 frames and stand, but
those words must move before reuse.

**Rule:** price a cut by measuring the pass length before and after
on the same machine; a predicted saving is not a saving.

### The FPGA's demo pass is ~130-150 lines longer than ares', the chain is not, and single 68K accesses are not (2026-09-25 04:40 ET)

Direct reads on the FPGA, light barcode, level-1 demo, two launches:
    RIGSPAN=11 mean pass length: 400 / 418 lines at ~190 passes into
      the step, 362 / 358 at ~410 -- ares: 246 at 236 passes, 232 at 432.
    RIGSPAN=10 span-1 passes by end line in their second vint per 440
      vints: 0-130 lines +30, 131-261 lines +140 -- ares: +30 / +34.
    RIGSPAN=12 chain split (CHAINPRICE stamps on the barcode): entry ->
      shim end 63-65 lines, entry -> part B end 69-72 -- ares 62-68 /
      69-89. The IRQ4 chain is the same length on both machines.
    RIGSPAN=13 68K -> 32X framebuffer, 256 word writes / 256 reads:
      65-68 / 80 quarter-lines -- ares 61-64 / 77 (+5%, like cart and
      WRAM in RIGSPAN=9).
So the FPGA adds a large term to the game's own pass (the part between
the release and the idle loop), not to the chain and not to single
accesses. Every gauge so far ran at the top of the vblank; the pass
runs while the master's window blit is in flight, and no A/B removed
the blit (IDLEOFF stopped the packet building, MDQUIET the shim's
DMAs). RIGSPAN=14 = the cart/WRAM gauge run at +102 inside the window.

Addendum (RIGSPAN=14, 05:10 ET): the cart/WRAM gauge run at +102
inside the window reads 78-80 / 112-115 on the FPGA (ares 74 / 108,
the same as at vblank on both): the master's blit does not slow the
68K. Chain equal, single accesses +5% at vblank and inside the window,
IDLEOFF and MDQUIET flat -- the ~130 lines are inside the game's pass
and not in any 68K access class gauged so far. Next read: where the
68K IS at the vint on the FPGA (RIGSPAN=7 raw interrupted PC, 28
samples; =6 the ours/idle/game split).

### The FPGA term is instruction fetch from the cart: the same loop runs 18-32% slower from the cart than from WRAM, and only on the FPGA (2026-09-25 06:20 ET, RIGSPAN=15 + CARTBENCH)

A 16-byte loop (512 x tst.w WRAM / nop / dbra) planted in the game
image's 0xFF padding at 0x3FFE0 (68K 0x93FFE0), run from the cart and
then from a WRAM copy, timed in quarter-lines every 64th vint:
    ares  cart 115   WRAM 112   (f1500, f2700)
    FPGA  cart 132-148 (11 samples, varying)   WRAM 112
The 68K's WRAM code and single data accesses are +5% on the FPGA
(RIGSPAN=9/13/14: cart data reads, FB reads and writes, at vblank and
inside the window); INSTRUCTION FETCH through the cart path is
+18-32% on a tight loop and varies, i.e. a latency-plus-contention
term in the core's ROM path. The game's whole program runs from that
path, its pass is branchy code with cart-resident tables, and the
measured pass is +55-65% (RIGSPAN=11: 362-418 lines vs 232-246).
The interrupted-PC samples (RIGSPAN=6/7) put the FPGA's 68K in game
code at 40-44 of 64 vints (ares 18), scattered over the object loop
(0x39BA), 0x0B54, 0xD5D8-0xD8C8 -- no spin site, uniformly slower.
What this closes: the window (RIGSPAN=2), the shim's DMAs (MDQUIET),
the master's idle work (IDLEOFF), the chain length (RIGSPAN=12), the
blit alongside (RIGSPAN=14), single access speed (9/13), the text-copy
gate (BURSTGATE, 0 conversions). What it opens: the game's code must
fetch less from the cart on the FPGA -- hot routines in WRAM (the
object dispatcher and the scans the samples hit), or fewer instructions
per pass -- and the FPGA core's ROM path itself (srcref/S32X_MiSTer) is
where the +18-32% lives; the real cartridge is 0-wait mask ROM.

**Rule:** gauge instruction fetch separately from data access; a loop
that runs from RAM measures the CPU, not the memory the program lives in.

### The FPGA's cart-fetch penalty is the core's ROM path, not hardware: +18% fixed from the SDRAM read wait, up to +32% with SH-2 ROM arbitration (2026-09-25 07:30 ET, srcref/S32X_MiSTer + RIGSPAN=15)

The core (srcref/S32X_MiSTer): the cart ROM lives in the board's SDRAM
as channel 1 of rtl/sdram.sv (clk_ram 107.39 MHz; one access = START
+ tRCD 2 + CAS 2 + 1 = ~7 cycles = ~65 ns); the 32X's own SDRAM is in
DDR3 (S32X.sv: use_sdr is never set), so the ROM channel is not shared
with SH-2 SDRAM traffic. An MD ROM read through the adapter
(rtl/32X/IF.sv, USE_ROM_WAIT=1 in S32X.sv) goes RS_IDLE -> RS_MD_WAIT
(waits for ROM_WAIT_SYNC = sdr_busy[1] to rise) -> RS_MD_READ (waits
for it to fall) -> DTACK: the 68K's DTACK is held for the whole SDRAM
access plus two clock-domain syncs at clk_sys 53.69 MHz (7 per 68K
clock) -- about one 68K clock per ROM access. An SH-2 ROM request
pending at RS_IDLE is granted first (SH_ROM_WAIT) and the MD waits.
Measured (RIGSPAN=15, the same loop from the cart / from WRAM,
quarter-lines): FPGA 132-148 / 112 with the master's art fetches on,
130-133 (one 141) / 112 with them off (IDLEOFF+NOMAPS); ares 115 /
112 both ways. So: +18% is the core's fixed DTACK wait on every MD
ROM read, and 0-15% on top is SH-2 ROM arbitration.
A real 32X: the MD reads a mask-ROM cartridge with no added wait
(the adapter passes the access through), and only the SH-2
arbitration part exists. So the FPGA rig has been PESSIMISTIC on the
68K by ~18% of every cart cycle since the first frame-rate probe;
ares (which charges no arbitration either) is optimistic by the
arbitration part only. The game runs entirely from the cart: that
+18% (more on branchy, table-reading code) is the ~130 lines a pass
(RIGSPAN=11) the last day chased, and the week's FPGA-vs-ares rate
gap was mostly the emulator. A core build with USE_ROM_WAIT(0)
(S32X.sv) would drop the fixed wait (the SDRAM read still lands well
inside the 68K's bus cycle); that is a Quartus rebuild, Mike's call.

**Rule:** when a rig disagrees with an emulator by a constant, read
the rig's RTL for that path before believing either; the FPGA is an
emulator too.

### The dual-chain count: 48% of the FPGA's demo chains (32% of ares') enter with the game mid-pass, and the shim pushes a full window for each (2026-09-25 09:30 ET, RIGSPAN=16)

RIGSPAN=16 counts, at every IRQ4 chain entry (shim_vblank, before the
game's handler), whether the frame flag 0xFFF01C is set (the game is
still inside a pass -- its own overrun; its IRQ4 takes the short path
and uploads nothing) or clear (a fresh frame). Level-1 demo:
    ares  per 304 vints: mid-pass 98, fresh 208   (32%)
    FPGA  per 440 vints: mid-pass 212, fresh 228  (48%)
On every mid-pass chain the shim still runs r60_push (~68 lines: the
palette rotor + compares, the stage ship) and posts a window the
master composes and flips -- for a frame whose sprites and scroll the
game did not update. The master harvests one NEW frame per pass; we
send it one push per vint. Skipping the push + post on mid-pass chains
is worth ~68 lines on 48% of FPGA vints (~33 lines a vint averaged,
~22 on ares) and survives the tiering (real on hardware).
Accuracy note before cutting it: the arcade's palette RAM is written
directly by the game, so during an overrun vint the arcade shows the
previous sprites/scroll WITH any palette writes the pass has made so
far. Skipping the mid-pass push delays those palette writes by one
vint on overrun vints only. That is a fidelity trade to put to Mike;
the alternative that keeps it exact is to make the mid-pass push
palette-only (skip the stage ship and the post when the game's IRQ4
took the short path).

### WRAMCODE, first cut: the entry jump must be abs.l (2026-09-25 10:40 ET)

tools/wram_reloc.py re-lays 12 blocks (2968 bytes, 24 entries) of the
demo's hottest game code into WRAM 0xFF5000 and turns each ROM entry
into a jump. The first build hung at boot after 3 vints with the 68K
at 0x556: the entry was `jmp (abs.w)`, and abs.w sign-extends only
from 0x8000 up -- 0xFF5000 as .w is 0x005000, low ROM. (The tile
thunks sit at 0xFFB820 for exactly this reason; the comment was there.)
The entry is now `jmp (abs.l)`, six bytes, with the overwrite check
widened to e+2 and e+4; one block (0x30B2) lost its entry to that
check and is dropped. Two more tool faults fixed on the way: btst/bset
#n and movem carry an extension word BEFORE the PC-relative
displacement, and the re-disassembly check must compare against
objdump's resolved target, not the tool's own arithmetic. The 68K side
grew 3 KB, which pushed the SH-2 RAM-code image into the tile blob;
the blob moved 0x268000 -> 0x26A000 (mars.ld + md_main.c, the check
script agrees).

**Rule:** an abs.w operand reaches 0xFF8000-0xFFFFFF only; anything
lower in WRAM needs abs.l, and a boot hang after a handful of vints
with the PC in early game code is the first thing to suspect.

### WRAMCODE on the FPGA: the demo pass shortens by 30-45 lines, 8 passes per 440 vints convert; exact by the game's own clock (2026-09-25 11:45 ET)

wramc8 = light barcode + WRAMCODE (2968 bytes, 12 blocks, 24 entries).
Exactness on ares, aligned by the scene timer 0xFFF02A with the frame
flag clear on both (the game idle at the snapshot): the 16 KB of game
WRAM below the stack differs from the light-barcode line by 3 bytes at
timer 147 (title) and 5 at timer 596, all inside the palette-cycling
slot table at 0xFFF300 (LOOP-DECOMPILE: 8-byte slots, a timer byte
each) -- those timers advance per VINT, and a rom that reaches the
same pass count in fewer vints must read differently there (it is the
arcade-faithful direction). Both 4 KB text pages identical. Inside the
demo no frame edge finds both roms idle (the WRAM rom is mid-pass at
every sampled edge); at timer 477 the 16 differing bytes are object
slots mid-update, the phase, not the code. The stack area itself
differs (stale frames hold our return addresses) and is excluded.
FPGA, level-1 demo, two launches each, per ~440 vints:
    mean pass (RIGSPAN=11)  control 400/418 -> 358/362 lines as the
                            step ages; WRAM 356 -> 332/328
    one-vint / two-vint / three-vint (RIGSPAN=4)
                            control 34 / ~170 / 16;  WRAM 42 / ~170 / 12
    passes                  ~220 -> ~224 (50% -> 52%)
So 30-45 lines a pass came off, 8 passes per 440 vints converted
two-vint -> one-vint, 4 three-vint -> two-vint: a quarter to a third of
the FPGA's ~130-line excess. The block list was chosen from the ARCADE
demo's instruction profile (~82% of its pass), which the text-copy
lesson already showed mis-weights OUR demo; the remaining cart-fetch
time is in routines that profile did not rank. Next: an execution
profile of our own rom (per-entry counters, or the interrupted-PC ring
on the WRAM rom) to choose the second block set; 616 bytes of the
0xFF5000 region remain, and 0xFF3C30-0xFF3EFF is a further ~700.

Addendum (our-rom profile, 12:10 ET): the interrupted-PC ring on the
WRAM rom on ares, 12 demo dumps = 768 vints: 572 idle, 93 in WRAM
code, 66 mid-pass in cart code. Those 66 (a thin sample) sit in
0x3352, 0xEE80, 0xD6D8, 0xD608, 0xDABC, 0x674E, 0xD65C, 0xE2C2,
0x12EC, 0x103E0, 0xD5A6 -- mostly pointer-called object handlers the
arcade-derived list did not rank or the tool rejected (0xD5A6's entry
takes a branch at +2). ~1.3 KB of WRAM remains (616 B at 0xFF5BA0,
~700 B at 0xFF3C30) for a second block set once the profile has a
few thousand samples; a longer ring dump series on the FPGA rom
(mid-pass 65% of vints there) is the cheaper source.

### The rig's launch API no-ops on a rom that is already running: three "launches" were one (2026-09-24 18:05 ET)

Three consecutive `tools/mister_push.sh rom/night/wc1.32x` calls, meant
as three launches of the line candidate, produced ONE: the game kept
running from the first, /tmp/remote.log had no `game stopped:
Sega32X/wc1.32x` line between them, and mister_push.sh's `ok` check
matched the FIRST launch's `game started` line (it greps the rom name
in the log's recent tail). The second and third shot sets were the
first launch's attract continuing.

**Rule:** a launch of a build is proven by its own `game stopped` /
`game started` pair in /tmp/remote.log, never by the push tool's `ok`.
For repeated launches of one build push under distinct names
(`mister_push.sh rom X.32x wc1b.32x`; the screenshots stay rom-named
per launch) or launch another rom in between.

### Palette-only push on mid-pass chains, priced: ~5 lines a vint on the FPGA, half of BURSTGATE (2026-09-24 18:10 ET)

The decompile settled the mid-pass palette question from the rom
(HANDOFF-FROM-DECOMPILE §1: 0x3BEC queues 8-byte palette entries during
the pass, 0x2DBC in the vblank handler drains the whole queue every
vint, overrun vints included), so the exact form is a palette-only push
on chains entered mid-pass. Priced from the chain stamps (LESSONS "the
68K-line ranking"): the push still runs the rotor + compares (41 lines)
and ships the regs + palette blocks (the regs and the palette ids are
the packet prefix, PKT_PAL_ID0 = 82); what it skips is the rowscroll
compare (5), the selection (3) and the records + tail ship (2), ~10
lines per mid-pass chain. Post B still posts (the master paints CRAM at
the flip). At 48% mid-pass chains on the FPGA that is ~5 lines a vint,
~3 on ares (32%), against ~33 for the full skip -- and BURSTGATE's ~10
a pass converted 0 of 440. Not built.

**Rule:** price a cut as (lines it removes) x (fraction of chains it
applies to) before building; the full skip's number is not the exact
variant's number.

### Our own rom's PC profile: the cart residue after WRAMCODE is 13% of the vint; the FPGA's remaining ~100-line excess is NOT cart fetch (2026-09-24 18:45 ET, 16-line PC sampler on ares)

The PC_SAMP probe (md_start.s, 2026-09-06) sampled the 68K's PC at three
fixed lines a vint (55/111/167); at fixed lines the pass's fixed order
aliases (0x3F04 alone took 11 of 174 cart samples). `PCSAMPN=15` makes
the H-int counter a knob: 16-line grid, 13 samples a vint, ring of 64 at
0xFFA200 read on ares every 4 frames (216 runs over the level-1 demo,
wcp2 = line + WRAMCODE + PCSAMP; tools/pc_profile.py attributes them to
docs/audit/function_map.md entries). 8805 samples:
    WRAM-resident game code   38.5%
    the game's idle loop      34.3%
    our shim + thunks         14.1%
    cart game code            13.2%   (1160 samples)
So the first block set already holds 74% of the game's working time;
what is left in the cart is 13% of the vint (~35 lines), and the core's
ROM wait on it (+18-32% of fetch-bound time) is at most ~8-10 lines a
vint. The FPGA's demo pass after WRAMCODE is still ~90-100 lines longer
than ares' (332 vs 240) -- that excess is NOT instruction fetch from the
cart. The next instrument has to find where the FPGA spends it: the
same sampler's class histogram read through the barcode on the rig
(a RIGSPAN mode rotating 5 counters over 5 vints), FPGA vs ares on the
same rom, would say whether the extra time is in WRAM game code (a 68K
instruction mix the loop gauges missed), in the shim, or in waits.

The residue's top ranges (samples of 1160): 0x674E animation_frame_fetch
98; 0xD5A6-0xD776 walk_player_slots + collision helpers 0xD608/0xD65C/
0xD6D8/0xD73C 285; the main loop 0x97C-0xB1E 74 (misbounded under
0x5BE in the map); 0x65AA-0x6654 47; 0x103E0 and 0xEE80 (1 KB and 450 B
handlers) 37 each; 0x3F72 clamp_y 34; 0x3F04/0x3F20/0x3F24/0x3F32 ~75 --
those last are ON THE ENTRY STUBS (the 6-byte jmp planted at a
relocated routine's ROM entry: 0x3F04 is jsr'd from 92 sites, the stub
costs three cart fetches a call).

**Rule:** profile OUR rom with a sampler whose grid does not alias the
pass (a fixed-line sampler recurs on the same PCs); the profile's class
split says what a fetch cut can buy BEFORE the cut is built.

### WRAMCODE second block set + call-site rewrite: exact on ares, ~15 lines a pass on the FPGA (2026-09-24 18:45 ET, wc2, rig 2 launches each)

tools/wram_reloc.py now runs two passes: the first lays the blocks out
to learn every entry's WRAM address, then every `jsr`/`jmp (abs.l)` in
the image whose target is an entry is re-pointed at the WRAM copy (same
6-byte instruction, new target -- 578 sites), and the second pass
relocates the rewritten image so copies inside blocks carry the direct
targets too. A stub is planted only where an outside branch still
reaches the ROM entry; an entry that cannot take a stub keeps its ROM
code for those callers. Blocks added (tools/game_altbeast.py): 0x6654
block extended to 0x6794; 0xD5A8-0xD776 (0xD5A6's moveq stays in ROM and
falls into the stub, because 0xD5A4 branches to 0xD5A8); 0x3F72-0x3FA4.
3536 of the 3584 bytes at 0xFF5000 used, 18 stubs.
Exactness (same game time, scene timer 0xFFF02A + attract step + frame
flag 0 on both): title timers 426 and 619, demo timer 479 -- WRAM
0xFFC000-0xFFFCFF differs from wc1 only in the palette-cycle slot
timers at 0xFFF307-0xFFF317 (per-vint counters, the arcade-faithful
direction), both text pages identical (one 2-byte cell at timer 426 is
the staged bank's animation phase: wc1's bank 0 vs wc2's bank 1 match).
FPGA, RIGSPAN=11, level-1 demo, control = wram11 (first set):
    mean pass at ~vint 2900: control 342/348, wc2 316/330
    mean pass at ~vint 3350: control 330/334, wc2 318/316
~15 lines a pass, as the profile priced (36% of a 35-line residue at
the core's +25%, plus the stubs). Span histogram (RIGSPAN=4, raw counts,
control = wram4, two launches each), level-1 demo:
    ~460 vints in:  one-vint  control 68 / 82   wc2 78
                    three-vint control 16 / 18  wc2 12
    ~900 vints in:  one-vint  control 94 / 96   wc2 118 / 114
                    three-vint control 32 / 36  wc2 22 / 26
About 20 two-vint passes per 900 vints become one-vint and 10 three-vint
become two-vint. rom/night/wc2.32x = `make line WRAMCODE=1` with the new
table; it needs its own play pass (new code paths in WRAM) and carries
its own three rig launches.

### The core's ROM wait covers data reads too, and RIGSPAN=9 already measured that half: ~1 68K cycle a read, a few lines a pass (2026-09-24 19:45 ET, from the captured bytes)

Decompile's point (IF.sv:780): MD_ROM_SEL is a pure address decode
(VA[23:20] < 4 & ADEN), no function-code lines, so RS_MD_WAIT holds
DTACK for operand reads as well as fetches; a PC sampler books a cart
table walk done by WRAM-resident code under "WRAM game code", so the
13% cart share bounds the residual FETCH wait only. Correct. The data
half is already in the bytes: RIGSPAN=9 times 256 word reads from the
cart window against a 256-iteration WRAM loop on the same machine:
    ares  cart 74   WRAM 108   quarter-lines
    FPGA  cart 77-80  WRAM 112-115
The WRAM loop, which touches no cart, is +4-6% on the FPGA (the 68K's
general ~5%); the cart-read loop is +4-8%. The read-specific excess is
0-2% of a ~35-cycle read iteration = 0-1 cycle a read, which is the
RTL's own number (an SDRAM access plus two syncs ~ 100 ns ~ 0.8 of a
7.67 MHz clock, one wait state). CARTBENCH's +18-32% on fetch-bound
code is the same one wait state per WORD, paid on every fetched word;
a data read pays it once per read. The count of cart data reads a pass
is not countable statically (the relocated blocks' 23 cart operands are
all lea; the reads go through the pointers), but the arithmetic bounds
it: even one cart read per instruction over the pass's ~10k
instructions is ~10k cycles = ~20 lines; a 68K mix's 0.1-0.3 cart
reads an instruction is 2-6 lines a pass. Against the FPGA's remaining
~80-90 lines a pass over ares (wc2 316-330 vs 232-246), the accounted
terms are: general 68K +5% ~12 lines, residual fetch ~8, data reads
~2-6. ~60 lines a pass are still unexplained by any per-access gauge.
Relocating data is not a lever (no room, and the share is small).

**Rule:** the per-access gauges (RIGSPAN=9 data, CARTBENCH fetch) are
run and consistent with the RTL; do not re-run them. The next
instrument is a WHERE, not a HOW MUCH: the class histogram of the PC
sampler on the FPGA against ares on one rom.

### RIGSPAN=14 answers the burst-tenure question at one phase: cart data reads inside the window cost the same as at vblank; the fetch gauge never ran inside the window (2026-09-24 19:55 ET, from the rigc14/rigc15/rigc15i shots on disk)

Decompile's mechanism: IF.sv RS_SH_CONT loops back to RS_SH_WAIT while
the SH-2 keeps asserting, and CACHE.sv:488 fills 16-byte lines in
four-longword bursts (8 word transactions on the cart port per SH-2
cache miss), so a 68K cart access arriving mid-burst waits for the
rest of the tenure, not one wait state -- and every per-access gauge
ran in vblank (mode 9 and 15 wait for V >= 0xF0). The bytes on disk:
    RIGSPAN=9   vblank        cart reads 77-80   WRAM loop 112-115
    RIGSPAN=14  +102 in window cart reads 78-80  WRAM loop 112-115
                (12 shots, steps 0-5, rigc14)
    RIGSPAN=15  vblank        cart FETCH loop 132-148 varying (rigc15),
                              130-133 flat with the master's cart idle
                              work off (rigc15i); WRAM 112 both
Data reads at +102 do not move (the table's third row: not per-access
at that phase). The fetch loop shows contention of up to +12% even in
vblank -- the master reads cart art for its idle work there -- and it
has NEVER been timed inside the window, where the blit's line fills
are densest. Tenure blocking hits a fetch and a data read alike, so
mode 14's null covers one phase, not the window. The one remaining
per-access test is CARTBENCH at window phases (+40, +102, +160): a
mode-15 loop behind mode 14's wait. Built as RIGSPAN=17 with
RIGSPANV=<raw V>; runs when the rig is free. If it does not move
either, the excess is not per-access and the class histogram is the
instrument.

### The contended phase is the cache_fill slot, which is where CARTBENCH already ran; the in-window c17 phases sample the other side (2026-09-24 22:30 ET, decompile's reconciliation)

The master's cart streamer is cache_fill (sh_src/m_main.c): 64 bytes a
tile as 16 consecutive longwords from altbeast_tiles (a plain linked
cart symbol, read through the cached window), budget 384 tiles a
drain -- ~24 KB, ~1,536 four-longword IBBURST line fills, each an
uninterruptible tenure on the cart port. It runs in the early-vint
idle slot (dt = frt() - t_vint <= 8000), outside the window. So mode
15's wait (V >= 0xF0) lands IN that slot: its 132-148 spread, flat at
130-133 with the master's cart idle work off, is the tenure signature;
and mode 14's null at +102 is the expected result, because at +102 the
master composes from the SDRAM tile cache, not from the cart. The c17
roms at 0x10/0x40/0x80 all sample active display; three flat readings
there would mean "confined to a phase none sampled", not "no
contention". Fourth rom: c17_0xF0 (raw V == 0xF0, the same wait
mechanism as c17_0x40, one phase apart). The pair to read is
c17@0xF0 against c17@0x40.

The lever if it confirms (not built): read altbeast_tiles in cache_fill
through the uncached mirror (0x22000000 | addr, the idiom already in
m_main.c), so each longword is its own 2-word transaction and the port
returns to RS_IDLE between them -- 16 short tenures a tile instead of
4 long ones, the 68K's worst-case wait 8 words -> 2. Uncached is slower
per word for the SH-2, so the drain budget would need re-pricing.
Share of the ~60 lines: unknown until c17@0xF0 reads.

### c17 at four phases: the fetch loop reads 132 in the level-1 demo at 0xF0, 0x40, 0x10 and 0x80 alike; tenure contention is sporadic spikes, not a phase (2026-09-24 22:50 ET, rig, two launches per phase)

RIGSPAN=17 = CARTBENCH's 512-iteration loop fetched from the cart,
timed in quarter-lines after a wait for raw V == RIGSPANV, every 64th
vint. Level-1 demo readings (WRAM copy 112 throughout):
    0xF0 (the cache_fill slot)  131 129 132 132
    0x40 (+102, compose)        132 132 132 141
    0x10 (+54)                  136 132 150 132
    0x80 (+166)                 135 132
Across the other attract steps 0xF0 shows spikes to 148/144/140/138 at
the round-1 load and the title, where the master's drain is busy; in
the demo the spikes are as likely at 0x10 or 0x40 as at 0xF0. So the
tenure mechanism (IF.sv RS_SH_CONT + CACHE.sv line fills) is real and
visible (a spike = +6-14% on a 33-line loop), but in the level-1 demo
it is sporadic and phase-free, mean ~134 vs the 130-133 floor: about
+1-2% of fetch-bound time, a couple of lines a pass at most. It is not
the ~60 unexplained lines, and the uncached-mirror cache_fill lever
would buy at most that. Ares for the same loop: 115.

**Rule:** a contention term is sized by its mean over the measured
scene, not by its worst spike; the spikes here live at loads and the
title, which are not on the speed axis.

### The wc2 purple band did not reproduce: rg1 0/80, wc2 0/60 across three launches each (2026-09-24 23:25 ET, rig)

Alternating launches, 20 shots at 9 s each from 14 s after launch,
scanned for the BGBOTTOM band colour (146,0,255) over the bottom 44
rows: rg1 0/20 0/20 0/20 (plus 0/20 from the aborted first run), wc2
0/20 0/20 0/20. The band remains a 1-in-6-launches event on wc2 (two
consecutive shots, view rows 24-27 exactly, 19:03 ET) and unseen on
the line in 80 shots tonight and ~600 in the corpora. Too rare to
attribute by counting; the mechanism reading stands (the backstop's
opacity cache verdict under a shorter pass), and the next evidence is
either Mike's eyes in play or a verdict-audit probe on the SH-2 side
(log a BG cell blanked under a non-opaque FG tile). wc2's picture gate:
five clean launches of six.

### The class histogram, FPGA vs ares: only cart game code moves (8% -> 12-17% of sampled time); the "60 unexplained lines" were the second chain of tipped passes, not a hidden term (2026-09-24 23:35 ET, RIGSPAN=18, c18 on both machines)

RIGSPAN=18 = the 16-line PC sampler (PCSAMP=1 PCSAMPN=15) classified
once a vint and read as shares through the barcode (byte 5 = class
share x255, byte 6 = class id | samples/256), per attract step, same
rom on ares (screenshots at frames 8 apart, one per class) and on the
rig (two launches). Level-1 demo:
                      ares          FPGA
    idle (thunk+hook) 59-61%        61 / 55 / 57%
    WRAM game code    25-29%        25.5%
    cart game code    7-9%          17.3 / 11.8%
    shim (ours)       3.9%          3.1 / 4.7%
Caveat: the H-int sampler loses the IRQ4 chain (masked, ~110 lines; the
pending H-ints collapse to one at the mask lift), so shares are of the
NON-chain time and the shim class is mostly the chain's exit sample.
The chain is equal on both machines (RIGSPAN=12).

What it says: WRAM-resident code runs at the same share on both
machines (the +5% general term is inside it, invisible at this
resolution); the cart residue takes roughly TWICE the time on the FPGA
(the ROM wait plus tenure on fetch-bound code: the main loop
0x97C-0xB1E is jsr-after-jsr, the handlers 0xEE80/0x103E0 are branchy).
In lines: ares ~12 of the ~152 sampled lines a vint, FPGA ~22 -- about
+10 lines a vint, +13 a pass. Add the 5% general term (~8 a pass):
~20 lines a pass of real machine difference in the game's work.

Why the mean pass reads +80: a pass that misses the vint line by any
margin gets a SECOND IRQ4 chain (~110 lines, 85 of them ours) before
it can end. Ares' mean pass 232-246 = ~1.3 chains x 110 + ~90 of game
work; the FPGA's 316-330 = ~1.6 chains x 110 + ~110 of game work. The
mean-pass difference is mostly chain count, and chain count is the
span distribution the 20-line difference tips. There is no hidden
per-access term left to find; every gauge agrees.

The levers, in order of lines a pass: (1) the chain -- 85 lines of ours
per vint spanned, the palette rotor 41 of them (held on Mike's word);
(2) the cart residue -- moving the main loop and the two big handlers
to the 0xFF3C30 region would take ~10 lines a vint off the FPGA, twice
what the +25% pricing said, because cart code costs 2x there, not
1.25x; (3) nothing else measurable.

**Rule:** compare a mean pass across machines only together with the
span distribution; a longer mean can be more chains, not slower work.
The class histogram (RIGSPAN=18) is the instrument that separates them.

### The third block set (WRAM region 2, 1482 bytes) converts nothing: the cart-fetch axis is spent (2026-09-24 23:50 ET, rig, two launches each with interleaved controls)

wc3 = wc2 + region 2 at 0xFF4600-0xFF4BFF (the gap above .bss, md.ld
fence at 0xFF4600; the boot stack keeps 0xFF4C00-0xFF4FF0): the main
loop 0x97C-0xB1E, 0x65AA-0x6654, 0xEE80-0xF00A, 0xF8C0, 0x144A, 0xDABC
-- 19% of the remaining cart samples. Exact on ares at three game
times (title 426/619, demo 479). Level-1 demo on the FPGA:
    RIGSPAN=11 mean pass   control (wc2) 328/314      wc3 334/320, 328/318
    RIGSPAN=4  one-vint    control 80 / 120           wc3 84 / 112, 82 / 112
               three-vint  control 12 / 26            wc3 16 / 30, 8 / 18
Nothing outside the noise floor (mode 11 +-5 lines, mode 4 +-10). The
class histogram priced the whole cart residue at ~10 lines a vint on
the FPGA; a fifth of it is ~2 lines, under the floor. wc3 stays a
knob (WRAM_BLOCKS2 in tools/game_altbeast.py, region 2 machinery kept);
wc2 remains the candidate.

**Rule:** the block-set lever is finished at wc2. Further WRAM
relocation of game code cannot show on the rig's instruments; the
remaining 68K levers are the chain's own lines (the palette rotor 41,
the stage ship 18, the consumes 15), all ours.

### wc3 picture launches: 3/3 without the purple band, one level-start shot with the BG's left half showing grey pillar tiles over the sky (2026-09-25 00:10 ET, rig)

wc3a/b/c, 60 shots: purple band 0/60. One shot -- wc3b's first, 14 s
after launch, the first level-1 demo just after boot -- shows the
background's left half (cells 0-11 of the sky and tree rows) drawn
with the grey stone-pillar tile pattern instead of the sky, statue
missing, sprites and text right; the next shot 9 s later is clean.
Signature (146,174,146)+(146,174,174) mass over (0-96, 16-128) found
in 1 of ~1000 rig shots on disk, none on rg1/wc1/wc2. Level-start
class (LESSONS 2026-09-21 "the FPGA layout lottery": the master's
pre-flip lift against a 68K blast at level start), on the fastest 68K
rom. wc3 is a knob, so this is a note, not a gate; if wc2 ever shows
it, the level-start race is the place to look, not the relocated code.

Addendum (overnight census, 01:10 ET): wc2 launch 6 of the census
showed the same grey-tile background at the level-1 demo's RESTART
after the level-2 demo (+113 s), full width this time, clean 14 s
later; rg1 0 in 6 launches so far, wc2 1 in 6, wc3 1 in 3. Level-start
class on the faster roms; the census runs to 05:45 for the rates.

Addendum (census, 02:50 ET): the purple band's SECOND event on wc2
(launch 16 of the census): again the first two shots after boot (+27 s,
+41 s), the whole first level-1 demo, gone at the eye cut; band rows
24-27, columns 0-~25. wc2: 2 events in 22 launches, both the boot's
first demo; rg1: 0 in 20. The grey-tile event (wc2n6, wc3b) is also a
level start. Both are install-time races on the faster 68K: a verdict
or a tilemap read before its art has landed, cached until the next
scene install.

### The purple band's mechanism: the backstop's opacity cache was keyed by tile code while the art depends on the bank, and never invalidated (2026-09-25 03:15 ET, from the source; fix built as wc4 / rg2)

fg_tile_opaque() (sh_src/m_main.c, BG_BOTTOM_HOLES) decides whether a
bottom-band BG cell is blanked under an FG tile by scanning the FG
tile's art for a pen-0 pixel, cached in a 256-slot table keyed by the
raw 13-bit tile code. But the art a code names is `remap(code12 +
bank1 * 0x1000)` when bit 12 is set, with bank1 = COMM2 & 7, the game's
tile bank as relayed by the 68K -- and the table was never cleared. A
verdict taken while COMM2 still held the previous scene's or boot's
bank was wrong and stayed cached until a colliding code evicted it:
exactly the observed band (rows 24-27, the first level-1 demo after
boot, gone at the cut, 2 of 22 wc2 launches, 0 of 20 rg1 -- the faster
68K moves the bank write against the master's first pass). Fix: key the
cache by the art index, which already includes the bank; opacity is a
pure function of the art, so no entry can be stale. Every correct
verdict is unchanged; only wrong ones disappear. wc4 = wc2 + fix,
rg2 = `make line` + fix (the line carries the same latent race).
Census 03:00 tally before the fix (20-shot launches): rg1 0 purple 0
grey in 16 launches; wc2 2 purple (launches 16 and the original wc2c)
and 1 grey in 16. Boot-window census of wc4 vs wc2 (5 shots over the
first demo) runs to 05:45.

**Rule:** a cache keyed by a name must include everything the name is
resolved through; here the tile bank. Stale-but-tagged is the worst
kind of wrong, because it survives until eviction.

Addendum (boot-window census, 05:10 ET): wc2's THIRD purple event
(launch 27 of the boot census, shots at +27 s and +41 s, the boot's
first demo again); wc4 (the art-keyed cache) 0 events in 54 launches
over the same window. wc2: 3 in 49 launches (~1 in 16); at that rate
54 unfixed launches would show ~3.3 events, P(0) ~ 3%. The fix holds
at that confidence; the census runs to 05:45.

Final (boot-window census, 05:50 ET): wc4 0 events in 72 launches;
wc2 1 in 36 (3 in 58 tonight overall, all the boot's first demo). At
wc2's rate 72 unfixed launches expect ~3.7 events; P(0) ~ 2%. The
art-keyed opacity cache is the fix. The rig ends on rg1.

### The ares frame's active area is rows 11-234, not 19-242: every ares crop at y=19 was eight lines low (2026-09-25 10:40 ET)

Measured on a 1415x243 ares-headless screenshot: the first non-black
row is 11, the last 234 (224 rows), columns 65-1344. tools/rig_barcode.py
cropped at y=19, so it read view rows 23-28 as "22-27" (row 28 = the
border, always blank) and decoded by vote with one row short; fixed to
11. A same-day bottom-band census read "row 27 all black" on every
frame from the same offset -- the border. Rig shots (320x224) are
unaffected.

**Rule:** measure a frame's active area before cropping it; a crop
that decodes by vote hides an off-by-one-row for weeks.

### Defect 1 (black tiles in level 1) reproduces on ares in the attract demo, deterministically, in two classes; ROWGEN is not the cause (2026-09-25 10:50 ET, bottom-band black-cell census)

Instrument: fully-black 8x8 cells in view rows 24-27 of a 320x224 frame
(scratchpad bandcells.py; ares frames cropped at the measured active
area). Arcade (MAME attract, frames 1600-2000): 0 in every frame. Ours
(wc4, level-1 demo, frames 2100-2900 every 25):
  (a) ONE cell in row 27 black for ~150 frames, moving with the scroll
      (27:6 -> 27:5 -> 27:3): a specific map cell of the HUD/floor row
      drawn black where the arcade draws the floor.
  (b) FIVE cells in row 25 black for a frame at a time (cells 2,5,8,10,
      28, then 3,6,9,11,29 one scroll step later): the same five map
      cells, transient.
  (c) the cut at 2825+ blanks the whole band (a transition, expected).
The build without ROWGEN shows (a) and (b) alike (1 cell at 27:6 over
the same span, the row-25 quintet at 2650/2800), so the row skip is not
the mechanism. Both are FB-composed rows: black = the FB has nothing
there (pen 0 -> MD planes -> backstop-blanked BG -> backdrop) or a pen
painted black. The FB dump at 2750 decides which.

### Defect 1's mechanism on ares: a priority (cat-1) FG cell whose hash set is full of young tiles is REFUSED a VRAM slot and left blank, and the backstop blanks the floor under it (2026-09-25 11:45 ET, tables read at the hit frame)

At frame 2700 of the level-1 demo (wc5), the five black cells in view
row 25 all hold FG word 0xB303: priority bit set, tile 0x303 -> art
0x1303 with bank1 = 1 (COMM2), colour set 0x4C. The art is fully
opaque (0 pen-0 pixels of 64), so the cover test is right to blank the
BG under it. The FG cell itself is blank because the residency table
has NO tag for (FG, 0x1303, 0x4C): its hash set (MD_SET = 37) holds
eight tiles with refs 209-217 against window 217, and the claim rule
for a cat-1 cell refuses to evict a victim younger than 12 windows
(m_main.c "HOT evict", MDA(4)): the slot stays MD_BLANK_SLOT. The
allocator census confirms it: MDA(4) advances by 1-7 a window while
the cells are black (+151 over the 150 frames the row-27 cell was
black, +7 in the window the row-25 quintet appeared); MDA(5/6/7) do
not move. Neighbours 0x1302/0x1304 (sets 36/34) are resident.
Table: 955 of 1024 slots valid. tools: scratchpad dec/*.py (offline
decode of layer regs, tilemap, md_tag/md_ref/md_dirty from ares dumps).

The refusal exists to stop thrash (evicting a hot tile blanks ITS
cells next window). The exact fix is therefore not a rule change: it
is capacity (more ways where the hash collides, or a hash that spreads
the demo's working set) or an FB fallback that composes the refused
cat-1 cell from the cart art. Sizing next.

**Rule:** a "black tile" is one of four blank reasons (MDA 4-8:
young-victim refusal, cut-dirty, dirty, w==0, backstop); read the
census before choosing a lever, and read the tables at the frame.

### Defect 1 FIXED on ares: the MD slot table re-laid as 64 sets x 16 ways with a hash over code, colour set and plane; band census 0 black cells in the demo (2026-09-25 12:30 ET, wc6)

The frame at 2700 needs 614 distinct (tile, colour set, plane) of the
1024 slots, mean 4.8 a set, yet three sets held more than 8 because
MD_SET hashed the CODE alone: a tile's FG and BG variants always
shared a set, and the floor's tiles are on both planes. NSETS 64,
MD_NWAYS 16 (same 1024 slots, same md_tag/md_ref/md_dirty sizes),
MD_SET(code, cset, isfg) = (code ^ code>>7 ^ cset<<1 ^ (fg ? 0x2B : 0))
& 63. Poisson at 9.6 a set puts P(>16) at ~2%. The render (art)
cache keeps its own NWAYS = 8. latch_layer_regs (670 B, once a
window) went to ROM on every build for the .ramtext room (4 bytes were
left). Ares, level-1 demo, frames 2100-2900 every 25: 0 black band
cells (wc5: a persistent row-27 cell over ~200 frames and a 5-cell
row-25 quintet). Same-game-time diff against wc5 at scene timer 292:
every game layer identical; the logo/score cells and the wolf's
animation phase differ because they run on per-vint timers -- and wc6
reaches timer 292 at frame 2364 against wc5's 2400 (~7% faster demo
on ares, the claim loop's misses gone). Rig gates next: 3 picture
launches, RIGSPAN=11/4 against wc2s11/wc2s4.

### 16 ways cost the FPGA; two-choice sets keep the capacity at the old cost (2026-09-25 11:10 ET, wc6 -> wc7)

wc6 (64 x 16) on the rig: RIGSPAN=11 336/316 against the wc2s11
control's 320 at the reliable read; RIGSPAN=4 launch 1 one-vint 86 vs
108 and three-vint 48 vs 26, launch 2 the opposite (110/152 vs
74/108). The two launches disagree by 30%, so the rig cannot rank a
change this size in one pair -- but the mechanism is real: a hit
scans the set until the tag matches, 16 longs = four SH-2 cache lines
against two, and the FPGA pays for every SDRAM line where ares pays
nothing. A slot HINT (check the cell's previous slot first) was tried
and withdrawn: the per-cell mirror it needs (md_dbg_nt) is read by
four dormant "is this slot on screen" checks that the wrap build had
left stale; keeping it live changed the allocator and blanked a whole
grass row, and there is no free SDRAM for a private copy (.bss would
cross 0x19000). wc7: two-choice sets on the original 128 x 8 -- a tile
may live in MD_SET or MD_SET2 (two folds of code, colour set, plane);
the claim scans the primary then the alternate, a free primary way
wins, the victim is the oldest of both. Capacity behaves like 16 ways
(a set overflows only when both choices are full); a primary hit costs
two lines as before. Ares: 0 black band cells in the demo (2100-2900),
scene timer 295 at frame 2400 (wc5 292: the same speed; wc6's 312 was
the 16-way build's own artefact). Rig gates queued behind wc6's.

**Rule:** on the FPGA a lookup's cost is its cache lines, not its
instructions; widen a set by giving a key two homes, not by making the
scan longer.

Addendum (wc7 on the rig, 11:35 ET): RIGSPAN=11 336 / 336 lines,
passes 400 / 412 against the wc2s11 control's 316 and 456 -- the same
slowdown as wc6, which shares the ROM-resident latch with it and
nothing else of note (wc8 puts the latch back). RIGSPAN=4 (one-vint /
three-vint at ~vint 3290): control 102 / 24, wc7 130 / 34 and 126 /
30 -- more of both, i.e. the two launches' step timing differs; the
histogram cannot rank it. Pictures: 60 shots, 0 band black cells in
59; one level-start shot with black sprite silhouettes and a black
logo (the palettes not landed yet -- the lost-push class), healed by
the next shot. Rare-event tally in 20-shot launches, boot and demo:
grey-tile level start wc2 1, wc3 1, wc6 1, rg1 0/4; palette-late start
wc7 1; purple band 0 everywhere since the art-keyed cache.

### The FG/BG-variant collision was the whole demo defect: the cset+plane hash alone on 128 x 8 gives 0 black band cells at wc4's cost (2026-09-25 12:20 ET, wc9)

The rig bisect: wc4s11 and wc5s11 read 310/320 lines against the
control's 316-328 (at or better), so the family's slowdown began with
the slot-table change (wc6 64x16: 336/316; wc7 two-choice: 336/336;
wc8 two-choice + latch back in RAM: 342/316). With the table 93% full,
about half the tiles end up in their alternate set, and a hit there
costs four cache lines. wc9 keeps the original single 8-way set and
only the hash (code ^ code>>7 ^ cset<<1 ^ plane): ares demo census 0
black band cells, lookup cost unchanged from wc4. The random overflow
(Poisson 4.8 a set, ~6% of sets > 8) did not show in the demo; if it
does in another round, MDTWOCHOICE=1 is the knob. Rig gates for wc9
queued.

**Rule:** fix the systematic collision before buying capacity; the
capacity's price on the FPGA is paid on every lookup.

Addendum (wc9 on the rig, 12:50 ET): pictures 3/3, 60 shots, 0 band
black cells, 0 purple, 0 grey; RIGSPAN=11 356/324 and 312 against the
control's 322/314 (the day's per-rom launch spread is ~15 lines);
RIGSPAN=4 one-vint 120 / 126 against 104, three-vint 30 / 20 against
24. Defect 1 is fixed on both machines at no measurable cost.

### The arcade oracle for play-only scenes: record a route, key it to the GAME's frame count, replay it on our rom (2026-09-25 14:50 ET)

Six of Mike's seven play-pass defects are in scenes no input script reaches.
An input script keyed to EMULATOR frames cannot get there: our rom runs the
game at 50-70% of 60 Hz and drifts out of the route in seconds. The game
samples input once per GAME frame, so key the route to that instead.
`md_main.c` `shim_vblank` now counts game frames (vints whose frame flag
0xFFF01C was clear at entry) into the u16 at 0xFFA1D0, zeroed at boot; the
ares fork gained `--input-gameframe ADDR` (key playback to that counter) and
`--gameframe-log FILE` (the per-frame map, so alignment is two runs instead of
a bisection of runs -- the bisection was 56 full runs and timed out).
tools/record_setup.lua records in MAME with the port's DIP bytes;
tools/inp_dump.lua replays headlessly and writes the arcade's per-frame state,
its picture at every scene edge, a snapshot-to-game-frame map, and the route
re-keyed to game frames; tools/gf_align.py puts ours beside it.
Verified: at game frames 469/1469/2469 the arcade and our rom are in play at
the same point of level 1, same enemies, same score. Full recipe in
docs/design/ORACLE-PLAYTHROUGH.md.

TWO TRAPS, both cost a run:
  - a press must survive the keying. The counter STALLS while the game is
    mid-pass, so the arcade's 10-frame start press at a scene change spanned
    ZERO game frames; press and release landed on the same key and cancelled,
    and our rom sat in the attract with one credit. inp_dump.lua now keeps each
    button's keys strictly increasing (a one-game-frame minimum hold).
  - the counter's address must be zeroed at boot, and an uninitialised read
    (0xFFFF) must not be taken as a game frame, or every event fires at once
    on frame 0.
Divergence is real and bounded: by game frame 2869 the recording had 1400
points and two lives, ours 300 and none. The oracle REACHES a scene; it is not
frame-for-frame identity for ever. Read the state columns before calling a
late-run pixel difference a port defect.

**Rule:** to reach a play-only scene, replay the arcade's own route keyed to
the game's frame count, not the emulator's.

### The transformation screen's blue field: the arcade draws it in FOUR blues, our CRAM holds TWO, so the chevron pattern flattens (2026-09-25 17:10 ET, the oracle's first finding)

Mike's full-game recording (103,171 game frames, 1,764 arcade snapshots, all
five rounds; the oracle rig of LESSONS 14:50) put our transformation screen
beside the arcade's for the first time on ares -- until today it needed his
eyes on the rig. Our rom's own cut windows come out of the alignment log
(`--gameframe-watch` on the FG page word): game frames 7157-7265 and
10474-10582 of that route, the arcade's at 3169-3345. Compared at the same
animation phase (the shouting face):

    differing cells   477 of 1120, EVERY ONE in rows 4-20
    border rows       0-3 and 21-27 identical, 160/160 black on both
    distinct colours  arcade 25, ours 16
    the blue field    arcade four shades -- 5-bit blue 25/23/21/19
                      ours ONE, 19,417 px of blue 25

Our 32X CRAM at that frame holds blue 25 and blue 21 but NOT 23 or 19 (it
carries 30 and 31 instead), and the MD CRAM has no blue at all on any of its
four lines -- the field is framebuffer-composed, so the loss is in the 32X
CRAM's pens for that scene, not in the tiles and not in the MD planes. The
tile art IS arriving: the field is full. With two of the ramp's four shades
missing the pattern's pens collapse onto one colour and the chevron becomes
invisible. That RETIRES the older reading ("the zigzag tiles were never wrong,
they were not ARRIVING -- residency, fixed by throughput", STATE's
transformation entry): throughput is not the lever here, the pen bake for the
cut scene is.

Mike's item (3), garbage on the cut's top and bottom rows, does NOT reproduce
on ares: those rows are 160/160 black on both machines through the whole
window. It is rig-only, like the grey-tile level start, and needs a rig
capture to mechanise.

Addendum (the oracle re-dumped with the page fix, 17:40 ET): the arcade's
LAYER page words are 0101/5656 in level-1 play and AAAA/BBBB in the cut --
exactly what our rom reports. The apparent disagreement in the first
alignment (ours 0101/5656 against "the arcade's" 0101/0000) was entirely the
logging bug: 0x742 is the FG's ALTERNATE page set, the BG is 0x741. Our page
selection is right; 75 scene events in the corrected dump, 1,776 snapshots.

**Rule:** count the DISTINCT COLOURS of a scene on both machines before
calling a flat area a missing-tile problem; a collapsed pen ramp and absent
art look identical in a still. And when our value disagrees with "the
arcade's", check that both readers name the same address before reading
anything into it.

CORRECTED, 18:20 ET, and the correction is the finding. Three claims of mine
above were wrong and the measurements that killed them are worth more than
they were:

  1. "our CRAM holds two of the four blues" -- ARTEFACT. My S16-to-32X
     conversion had the red and blue low bits swapped (the code takes bit 12
     as red's LSB, 13 green, 14 blue; I had 14/13/12) and I compared exact
     conversions against CRAM entries the live path never writes exactly.
  2. "0 of 48 of the cut's pens are in CRAM" -- also an artefact of that
     test, but it pointed at the real mechanism. Under CAT1_MD (on the line)
     cram_paint_tile paints a set's FRAMEBUFFER pens from the MD line's
     3-BIT colour (mdp_s_qc) whenever the set has an MD line, so the
     arcade's 5-bit values are not in CRAM BY DESIGN (LOOP29 151: so a cat-1
     boundary separates identical pixels). Measured at the cut:
     mdp_s_line[19] = mdp_s_line[20] = mdp_s_line[21] = 1 (MD line 0), while
     every play set sampled reads 0; and set 19's eight FB pens are white
     plus SEVEN COPIES OF ONE BLUE (0,0,31). That IS the flat field.
  3. "so paint the arcade colour while the cut pages are up" -- BUILT, and it
     made the picture MUCH worse: the red field became pale sage, the face
     vanished, 35 distinct colours against the arcade's 27. Reverted.
     MEASURED WORSE, CAUSE UNKNOWN. The mechanism first filed here ("PAL_SH
     holds a cyan ramp the scene never displays") was the STRIDE ERROR below
     and is withdrawn; the run that produced it was reading 19 groups past
     the data. The bypass is NOT closed: it is the only path to an exact
     field, because 5-bit 25/23/21/19 into the MD's 3-bit space is 6/5/5/4 --
     three distinct shades, not four, under the real MD level spacing and not
     merely under a shift. Re-run it once with the stride error out of the
     way before concluding anything about it (decompile, 2026-09-25 19:00).

So item 2's chain is: the cut's cycling sets (19/20/21, a ring the game
rotates every frame; the decompile's queue-and-drain ruling) do not reach
PAL_SH correctly, the FB then follows the MD line's 3-bit approximation, and
seven of set 19's eight pens land on one blue. The lever is upstream, in the
palette delivery for those sets, NOT in cram_paint_tile. Do not re-try the
cut-pages bypass: it is measured and it is worse.

SETTLED, 18:50 ET, with the decompile's correction (HANDOFF-FROM-DECOMPILE
§10): the rotation is NOT queued -- the cycler is 0x30B2, called from IRQ4 at
0x2C16, writing palette base + slot*16 (four move.l = 8 colours) on a
per-slot countdown from the ring table at 0xFFF300 -- and patch_game.py:1021
already special-cases that site with a RUNTIME dirty mask. Both halves of my
"stale PAL_SH" reading were wrong:

    our 68K palette during the cut  holds the blue ramp EXACTLY: 5-bit blue
                                    19, 21, 23, 25 at word index 153-157
    PAL_SH vs the 68K palette       2036-2039 of 2048 words identical (the
                                    rest is the rotation in flight)

So the cycler's writes arrive, the mask ships them, and the SH-2's shadow is
in sync. DELIVERY IS NOT THE BUG. My "cyan ramp" was the wrong STRIDE: the
SH-2 addresses the palette in 8-WORD groups (cram_paint_tile takes
PAL_SH + c * 8) while I read 16-word sets, so I was looking 19 groups past
the data -- exactly the set-id / slot-index confusion the decompile flagged.
Word 152-159 IS group 19, and it holds the ramp.

What remains is the painter, and it is worse than the arithmetic floor. The
floor: 5-bit 25/23/21/19 quantised to the MD's 3 bits is 6/5/5/4, so three
distinct shades are available. Measured: group 19's eight framebuffer pens
are white plus SEVEN COPIES of one blue. The MD line's quantised assignment
for that group collapses a ramp that 3-bit space could still separate.

The granularity AGREES on both sides and that is worth keeping: the cycler
writes slot*16 bytes = 8 words, and the painter groups in 8.

CEILING, so nobody over-promises the assignment fix: repairing group 19's
assignment takes the field from ONE blue to THREE, which is most of the
visible defect. Four needs the arcade's 5-bit values, i.e. the bypass.

**Rules:** the SH-2 addresses the palette in 8-word groups, the game's sets
are 16 words, and the tilemap's cset field is a SET -- reconcile all three
before reading a palette by index. And a negative result must be filed with
its CAUSE or as "cause unknown" -- never with a mechanism that later gets
withdrawn, or a live option is closed for good by a dead reason.

### Item 2's full chain: group 19's seven blue pens share ONE MD line pen, and shared-pen drift is TOLERATED, so the ramp never re-splits (2026-09-25 19:30 ET)

Measured on our rom through the transformation, all four reads at the same
scene, offsets from m_main.c's own defines:

    PAL_SH group 19          rotates every frame, SEVEN distinct blues
                             (5-bit 19,21,23,25,27,29,31) -- frame 9400
                             [31,21,23,25,27,29,31,19], 9430
                             [31,27,29,31,19,21,23,25], 9460 [31,19,...]
    mdp_s_map group 19       [14, 7, 7, 7, 7, 7, 7, 7]  <-- pens 1-7 ALL on
                             MD line-0 pen 7
    mdp_s_map group 20       [6, 13, 12, 11, 10, 9, 8, 8]  six distinct pens
    mdp_line_c line 0        pens 8-13 = 7,31,39,47,55,63 (group 20's flame
                             ramp, spread correctly)

So the allocator CAN spread a cycling ramp -- group 20's flames prove it in
the same scene, the same frame, the same line. Group 19 cannot, because its
seven pens were assigned to one pen and nothing re-splits them:

  - NEAR_MERGE is already OFF by default (the 2026-09-04 note records this
    very symptom: "Mike's transformation chevrons went flat"), so the merge
    is NOT there.
  - the assignment-time collapse is mdp_claim_pen reusing a line pen whose
    colour equals q -- correct when a set's colours really are equal, which
    they are at the cut's fade-in, and never revisited.
  - the drift path (m_main.c ~18135) has three cases: equal (continue),
    SOLE-OWNER drift (recolour the pen in place), and SHARED-PEN drift
    "TOLERATED WITHIN A DISTANCE BOUND". Group 19's pen has refcount 7, so
    every frame of the rotation lands in the tolerated case and the merge
    survives the whole scene.

PAL_QUANT_RAMP is not on the line, so the live quantiser is (5bit+2)>>2
clamped: blues 19,21 -> 5; 23,25 -> 6; 27,29,31 -> 7. THREE levels are
available, which matches the decompile's floor. The fix is therefore worth
1 blue -> 3, not 4; four needs the arcade's 5-bit values (the bypass, which
is measured worse for an unknown cause and still open).

THE FIX, located but NOT built: in the shared-pen drift case, a set whose
current quantised colours span more levels than the pens it holds must be
freed and re-assigned rather than tolerated. The site already has
mdp_free_set(s2) for the volatile case. The risk the source warns about is
churn -- "the old unconditional free+reassign invalidated the whole set's
slots ~1/sec (stage-2 statue-in-floor)" -- so the re-split must be
conditional on the span, not on any drift, and it wants the black-cell and
slot-invalidation counters watched on the same run.

**Rule:** when one set's ramp is flat and another's is not IN THE SAME FRAME,
the allocator is not short of pens -- read the pen MAP before the colours.

### Item 2, two fixes tried and both measured dead (2026-09-25 20:10 ET)

With the chain established (group 19's seven blue pens on one MD line pen,
merged at the cut's fade-in, kept by the tolerated-drift rule), two attempts:

1. **SPAN RE-SPLIT** (PENSPLIT=1, mine): at the drift site, free a set whose
   live quantised colours need more distinct pens than it holds. Built.
   Result: the pen map for group 19 came back UNCHANGED ([14,7,7,...]) while
   the picture went to 74 distinct colours against the arcade's 27 and the
   blue field all but vanished (204 px). That is the churn this file warns
   about: mdp_claim_pen re-merges the freed set onto the same equal-colour
   pens, the span condition is true again next window, and the set is freed
   every window. A span condition alone cannot work while the CLAIM merges
   within a set. REVERTED.
2. **CHEVFIX=1** (CARD CHEVPEN, already in the tree and written for exactly
   this: its comment records sets 20/21 holding 0x0007 on five distinct pixel
   indices "because they were claimed by sharing", and forces an exclusive
   pen for sets 19-21 when one is free). Built on the current line. Result:
   group 19 AND group 20's maps both read all-zero at the cut, and the field
   is still one blue (19,468 px). So on this line the flag does not reach the
   assignment it was written for -- its gate is `glow_chev && s >= 19 && s <=
   21 && freepen`, and either glow_chev is not up at that moment or the sets
   are not being assigned at all. REVERTED, and NOT dead: it worked when it
   was written ("zigzag + ornaments DRAW under CHEVFIX=1"), so what changed
   since is the question, not whether the idea is right.

ALL THREE READS, 20:40 ET -- and my CHEVFIX result above was an ALIGNMENT
ARTEFACT, the same error a second time: I screenshot the flag build at the
LINE build's emulator frames instead of its own. Its cut is at frames
9371-9489 (game frames 7157-7266), not 9482. Aligned properly:

    glow_chev at its own cut frame     1   (the gate IS up)
    mdp_s_line[19..21]                 1, 1, 1   (all assigned, all LINE 0)
    mdp_s_map group 19                 [14, 7, 7, 7, 7, 7, 7, 7]  STILL merged
    mdp_s_map group 20                 [6, 13, 12, 11, 10, 9, 8, 8]
    MD line 0's 16 pens                ONE free (pen 15); pen 0 reserved

So it is neither a gate repair nor an assignment repair. Both work. It is
CAPACITY: **the three chevron sets share ONE 16-pen MD line.** Branch (a)
forces an exclusive pen only `&& freepen`, and with one free pen the second
set onward must share -- and at claim time, a frame before the ring drains
(comment (c)), their colours are all equal, so sharing merges them. Group 20
took six pens, group 19 got two, and 19's other seven pixels landed on one.
MDP_LINES is 3, so 48 pens exist; three 8-pen cycling sets on one line need
24 and cannot fit, while spread over three lines each would have 15 to
itself.

THE LEVER, named but not built: give sets 19/20/21 different MD lines while
the chevron screen is up. Everything else in CARD CHEVPEN already works --
(b) holds them against eviction, (c) re-claims when the ring stops being
uniform, and (a) prefers an exclusive pen -- all three are defeated by one
line's pen budget.

**Rule (learned twice today):** never compare two builds at the same EMULATOR
frame. Our roms run the game at different speeds, so the same frame is a
different game moment; align with tools/gf_align.py or the game-frame log
every time. Both of today's wrong conclusions about this defect came from
skipping that.

**Rule:** before writing a new fix for a scene defect, grep the tree for a
flag already named after it -- CHEV_FIX's comment was a better diagnosis than
my first three hypotheses, and I wrote a churning duplicate of it instead.

### Item 2: the pen ownership table, and four fixes that did not fire (2026-09-25 21:30 ET)

MD line 0 at the transformation, read from mdp_pen_own / mdp_pen_rc /
mdp_line_c on the line build, aligned to its own cut frame:

    pen 1-5    colour 0 (black)   rc 2,1,1,1,3   owner set 0
    pen 6      320                rc 2           set 20 pixel 0
    pen 7      384                rc 7           set 19 pixel 1   <-- 19's
                                                 pixels 1-7, one pen
    pen 8-13   7 (all six)        rc 2 each      set 20 pixels 6..1
    pen 14     511                rc 1           set 19 pixel 0
    pen 0, 15  free

So the decompile's §13 is confirmed from our side too: sets 20 and 21 ALREADY
share losslessly (every one of 20's pens has rc 2). The waste is the other
way round -- set 20 holds SIX pens carrying the IDENTICAL colour 7, while set
19's seven pixels are crammed onto one. Both maps are frozen first claims.

WHERE THEY FREEZE: TAGKEEP's "land where you were" (mdp_assign_set) restores
mdp_pend_map pixel by pixel with no colour dedupe, so whatever map a set got
on its first claim survives every later visit -- which is the POINT of that
path (it is what keeps the tile tags valid) and is exactly wrong for a cycler
whose first claim happened while its ring was still uniform.

FOUR ATTEMPTS, all measured, none moved mdp_s_map group 19:

  1. span re-split at the DRIFT site (free a set needing more pens than it
     holds): picture wrecked (74 colours vs the arcade's 27), map unchanged.
     mdp_free_set REFUSES these sets -- CHEV_FIX (b) returns early for 19-21,
     and mds_pin guards a harvested scene's sets -- so the free hit other
     sets instead. The churn was collateral.
  2. CHEVFIX=1 alone: no change (and my first reading of it was an ALIGNMENT
     artefact; aligned, its gate is up and its sets are assigned).
  3. PENSPAN=1, a span REJECTION inside the landing path: never fires. At
     landing the ring is still uniform, so the remembered map looks adequate
     (1 distinct colour, 2 pens). The test is only true later, by which time
     the drift path tolerates it.
  4. CHEVFIX=1 PENSPAN=1 together, on the theory that (c)'s one-shot re-claim
     would supply the re-assignment for the rejection to act on: no change.

NEXT READ, named: instrument CARD CHEVPEN (c) itself -- does chev_done ever
advance, does chev_force get set, does the re-claim reach mdp_assign_set? (c)
is written to fire exactly once per set "at the moment its ring stops being
uniform", which is the only moment a correct map can be built, and nothing in
four builds suggests it runs. That is one probe, not another guess.

**Rule:** a fix that does not change the table it targets has not been
tested -- read the table, not the picture, before believing a flag did
anything. Three of tonight's four attempts looked plausible in the picture
and changed nothing in mdp_s_map.
### TAGKEEP is NOT what freezes the cut's pen map: the family-off build's table is byte-identical (2026-09-25 22:10 ET)

The decompile's test, run as specified -- today's line minus the TAGKEEP
family (TAGKEEP, PENHOLD, PENREPAINT together; the rom notag1.32x is DEAD and
was not touched), aligned to its OWN cut frame, table read not picture:

    mdp_s_map group 19   [14, 7, 7, 7, 7, 7, 7, 7]   IDENTICAL to the line
    mdp_s_map group 20   [6, 13, 12, 11, 10, 9, 8, 8] IDENTICAL
    mdp_s_map group 21   [6, 13, 12, 11, 10, 9, 8, 8] (shares 20's pens, rc 2)

So my indictment of "land where you were" is FALSIFIED. Five configurations
now produce the same table -- the line, CHEVFIX, PENSPAN, CHEVFIX+PENSPAN,
and the family off. And mds_pin reads 0 for every set at that moment (total
pinned: 0), so the scene table is not holding them either.

What that leaves: the cut's sets are assigned ONCE, while the ring is still
uniform, and NOTHING ever re-assigns them. The drift rules correctly decline
(d^2 = 4 on blue, against DRIFT_TOL 18); no free fires; no landing is
involved because there is no second assignment to land. The only mechanism
designed to re-assign at the right moment is CARD CHEVPEN (c), the one-shot
re-claim "at the moment its ring stops being uniform" -- and the CHEVFIX
build's table is identical too, so it is not firing.

NEXT READ, unchanged and now the only candidate: instrument (c) itself --
does chev_done advance, does chev_force get set, does its re-claim reach
mdp_assign_set. The family-off test has removed the alternative explanation
(that TAGKEEP's restore was undoing (c)'s work), which is exactly what it was
run to do.

SEPARATELY, and worth a play pass on its own merits: the family-off build
compiles clean on today's line (first time since 2026-09-16), TAGKEEP is
FALSIFIED in MEASURED DEAD, and dropping it is recorded as restoring the
gameplay HUD the line drops. It is a candidate for Mike, not for this
defect.

**Rule:** the index answers this faster than a build. `grep -i <keyword>
FACTS.md` before hypothesising -- it named TAGKEEP falsified, named the
family's three flags, and warned that the old rom predates six flags on the
line, all before I compiled anything.
### The TAGKEEP family off: rig 3/3 clean, and FEWER multi-vint passes than the line (2026-09-25 20:15 ET)

nt1 = today's line minus TAGKEEP + PENHOLD + PENREPAINT (the family goes
together; the rom notag1.32x is DEAD and was not touched -- a rom older than
the line is evidence, not a candidate). Mike's play pass PASSED it: "looks
great no regressions... still things to fix, like errant tiles. but no
regressions", and separately "it runs smoother... this feels better".

PICTURE GATE, 3 launches, 60 shots: 0 purple band, 0 grey level start, 0
black band cells. The only unrun gate it carried is now run.

SPEED, light barcode (RIGBCEVERY=8), RIGSPAN=4, control = wc9 interleaved in
the SAME session, two launches each. Mode 4's bytes are span>=2 count/2 and
span-0 count/2 (NOT one-vint/three-vint -- I mislabelled them at first; the
emitter is md_main.c:967). Today's rows only, the rig keeps older shots under
the same rom name and the first pull mixed 2026-09-24 frames in:

    sample        control (wc9)   family off (nt1)
    ~vint 2850      6, 6             4, 7            no separation
    ~vint 3290     14, 13            9, 11           NO OVERLAP

So at the later sample -- more passes accumulated, better statistics -- the
family off has ~26% fewer passes spanning 2+ vints, and the two builds'
ranges do not overlap, which almost nothing else has managed on this rig
today. The span-0 (same-vint) column is FLAT (control 50/59, nt1 54/51), so
the passes that stopped spanning two vints became span-1, not span-0.

Mike's impression is therefore SUPPORTED, not proven: one sample point
separates and the other does not, on two launches each. It is not a ranking.
Do not read a line-move out of it; do read it as the family being real work
on the MD-side path, not dead weight in the speed sense -- TAGKEEP is already
FALSIFIED for its own purpose ([22]=0 [23]=26, 100% moved).

**Rule:** check the emitter for what a barcode byte carries before reporting
it, and filter rig pulls by DATE -- the rig keeps every older shot of the
same rom name and they decode fine, so a stale session silently joins the
sample.
### Item 2: the allocator watch cannot see the claim, and the pen map I have been quoting may be STALE (2026-09-25 20:40 ET)

Ran MDALLOCWHY=1 MDAWATCH=19 on the family-off build, aligned to its own cut.

  - mdalloc_id captured NOTHING: that capture lives inside mdp_free_set and
    fires only for the watched set, and this set is not freed there. The
    instrument watches the free path, not the claim path, so it cannot answer
    "what did the claim do". A claim-time capture (in mdp_assign_set /
    mdp_claim_pen, same MDA_WATCH gate) is the probe that would.
  - counters over the run: hit 2,171,772; claim-into-a-free-way 5,988;
    evict 803; young-victim refusal (MDA 4) 984; free_set 8; free REFUSED by
    the pin 7; drift free 2. So frees are RARE and the pin does refuse some.
  - and the contradiction that stops me quoting the map: at the sampled frame
    mdp_s_used[19] = mdp_s_used[20] = mdp_s_used[21] = 0 while mdp_s_map[19]
    still reads [14,7,7,7,7,7,7,7] and mdp_s_line[19] is set. `used` = 0 means
    NO pixel currently holds a pen, so the map bytes are LEFTOVER from an
    earlier assignment, not a live mapping.

So the table I have quoted all evening -- and built five configurations
against -- is not necessarily what is painting the field at that instant. It
is consistent across builds because it is stale in all of them. The live
question is what `used` and `map` do across CONSECUTIVE frames of the cut: a
set that is freed and re-assigned repeatedly paints from stale mdp_s_qc in
between, which is churn, not the freeze I have been describing.

NEXT, in order, and none of it is a fix:
  1. sample mdp_s_used + mdp_s_map + mdp_s_qc for 19/20/21 on EVERY frame of
     the cut window, not one frame. That distinguishes frozen from churning.
  2. add a claim-time capture under MDA_WATCH so the claim can be observed at
     all.
Only then is a fix worth writing.

**Rule:** read the VALIDITY field before the data field. mdp_s_used says
which pen-map bytes mean anything, and I quoted the map for a whole evening
without checking it.
### Item 2 SOLVED: the cut's pen map is BAKED INTO THE ROM from an anchor captured before the cycler's first write (2026-09-25 22:10 ET)

The decompile's prediction was right and the proof is static data, not a
measurement. Read straight out of rom/s16.32x:

    baked round 9 (the cut), set 19:  line 1, used 0xFF,
                                      map [14, 7, 7, 7, 7, 7, 7, 7]
    baked round 9,          set 20:   map [6, 13, 12, 11, 10, 9, 8, 8]

Byte-for-byte the table I have been reading in live memory all evening. It
is not a runtime claim at all. tools/mdpen_bake.py computes it offline from
a single scene ANCHOR image and mds_install writes it in when the round
loads -- which is why five configurations produced identical tables, why
TAGKEEP-off changed nothing, and why CHEVPEN (a)/(c) cannot help: the
runtime never chooses this map.

WHY THE BAKE IS WRONG, from the anchor images in the rom (pscene_pal, read
as [scene][1024 words], group 19 = words 152-159):

    scene 10 anchor   blues [16, 15, 15, 15, 15, 15, 15, 15]
                      quantised [4,4,4,4,4,4,4,4] -> ONE pen for pixels 1-7
    the LIVE ring     blues 19,21,23,25,27,29,31 rotating every vint
                      quantised {5,5,6,6,7,7,7}  -> THREE pens needed

The anchor caught set 19 BEFORE the cycler's first write -- the base palette
load, the window of a vint or two the decompile named -- so the baker saw a
flat set and assigned it one pen. The live scene needs three. Seven of eight
pixels therefore share one pen for the screen's life, and the chevron is a
flat field.

THE FIX SITE: tools/mdpen_bake.py. The cycler's slots (19/20/21, installer
0x1A6BA, ring script 0x1A70E: 7 steps x 8 colours, static ROM data) must be
baked from the UNION of the ring's colours, not from one anchor frame. The
demand is derivable at bake time with no observation and no re-claim, which
is exactly what the decompile said. The ceiling stays 1 blue -> 3 on the
line's quantiser, 1 -> 4 with PALQUANT, and the prize is MOTION: a pen that
is one of three tracks the ring, a pen shared by seven cannot.

ALSO RETRACTED: my "the map may be stale in every build" note of 20:40.
mdp_s_used[19] reads 0xFF through the cut and 0 only at its very end, where
I happened to sample. The map was live; I sampled the release.

**Rule:** when a table is identical across every build you can make, stop
testing builds and look for it in the ROM. Five configurations and four
fixes cost an evening; `grep`ing the baked table would have cost a minute.
### The cycler's pen demand is STATIC DATA: extract it from the ring script, never from an anchor frame (2026-09-25 22:40 ET)

The builder found the transformation cut's pen map is BAKED into the rom, not
claimed at runtime: the baked table for the cut's round holds set 19 at line 1,
used mask 0xFF, map [14,7,7,7,7,7,7,7] -- byte for byte what live memory reads.
Five configurations gave one table because all five install the same bytes.

The bake is wrong because mdpen_bake.py anchors on ONE frame and the anchor
caught set 19 before the cycler's first write, while it was still flat.

`tools/ring_script.py` (new) reads the demand out of the program ROM instead.
Verified byte-for-byte against altbeast US set 8, and it reproduces the
builder's independently measured live ring exactly:

    installer 0x1A6BA   scene setup; zeroes 32 longwords at 0xFFF300
    script    0x1A70C   count word = 3, then 3 x (slot word, ring ptr long)
                        at 0x1A70E -- NOT the rings themselves, see below
    slot 19 -> ring 0x01A720   7 steps, timer 1 EVERY step
    slot 20 -> ring 0x01A7A0   6 steps, timers 4/2/2/2/2/4
    slot 21 -> ring 0x01A7A0   THE SAME POINTER as slot 20
    ring format: step-count word, then N x 18 bytes (2-byte timer + 8 colour
                 words = 8 pens)

PEN DEMAND, union over every step, quantised with mdpen_bake.quant() itself:

CORRECTED 23:05: the union count is NOT the pen count. Two pixels may share
an MD pen only if they carry the same colour at EVERY step -- a shared pen
holds one colour at a time. The demand is the number of distinct per-pixel
colour SEQUENCES, and for a rotation the two numbers diverge sharply because
every pixel visits the same colours in a different phase:

    slot 19   union = only 4 distinct MD colours, but 8 DISTINCT PIXEL
              SEQUENCES -> 8 PENS. pixel -> pen class = [0,1,2,3,4,5,6,7],
              the identity map; pixel 0 alone is constant (7FFF white).
              (4900..4F00 = blues 19,21,23,25,27,29,31; (v+2)>>2 clamp 7 ->
              5,5,6,6,7,7,7.) THE BAKE GAVE IT ONE. That is the flat field.
    slot 20   7 PENS. pixel -> pen class = [0,1,2,3,4,5,6,6]; pixels 0,6,7
              constant, and 6 and 7 agree so they share.
    slot 21   same ring pointer -> identical demand, and 20/21 can share one
              7-pen allocation losslessly (live refcounts already showed 2).

TOTAL for the cut: 8 + 7 shared = 15 pens = exactly one full MD line
(NPENS=15), against the 1 + 6 the bake packs today. STATIC-SCENE.md records 9
spare on the normal 15/15/6 partition, so it fits with 1 spare -- tight, and
partition() fails loudly rather than degrading.

The spec and a patch sketch are in docs/design/RING-BAKE.md (mdpen_bake.py was
NOT edited -- the builder may have it open). A cycled set's pens are PRIVATE:
their colours change over time, so they can never merge with another set's pen
that happens to match at step 0. Giving each sequence class an opaque token
gets that through the existing partition() unchanged.

FIX SITE (CORRECTED 23:30): tools/bake_tilecram.py, NOT mdpen_bake.py which
this thread named -- six tools write a *_s_map and `grep -ln 'mds_s_map' 
tools/*.py` lists them all. bake_tilecram.py was already keying per pixel; its
key was the SET of colours a pixel had held, and under a rotation every pixel
visits the same set in a different phase, so all seven keys matched. The key is
now the pixel's ORDERED quantised sequence read from the cycler script. One comparison per title at bake time, no observation, no
runtime re-claim, and it generalises: the script address is the only
title-specific input (`--script`).

**Rule:** when a table is identical across every build you can make, stop
testing builds and look for it in the rom. And a pointer TABLE is not the
thing it points at -- this thread recorded 0x1A70E as slot 19's ring in
HANDOFF-DECOMPILE-4/5 when it is the address of the (slot, pointer) pairs;
the rings are at 0x1A720 and 0x1A7A0. Read the bytes before writing the
offset down.
---

### RING BAKE APPLIED: check which generated table the build installs before fixing the generator

2026-09-25 22:35 (commit 765de95a). The entry above named the wrong file, and
the correction is the lesson.

### The cut's pen table is written by bake_tilecram.py, NOT mdpen_bake.py

The fix site is NOT tools/mdpen_bake.py. That writes
sh_src/pal_scenes_md.h, the per-SCENE tables -- and m_main.c:1907 overrides
every one of those names with the per-ROUND tables from sh_src/pal_rounds_md.h
under MDROUND=1, which is on the line. The clue was in the evidence all along:
the table that was quoted back was "baked ROUND 9", and rounds are
pal_rounds_md.h's index space. Two files with the same four array names, one
aliased over the other by five #defines, and the dead one is the one whose
docstring describes the bug. Half an hour went into the wrong file.

### A sampled colour SET cannot describe a rotation

The live baker
(tools/bake_tilecram.py) was ALREADY keying cycled scenes per pixel --
PIXKEY_SCENES = {5, 6, 9}, added 2026-09-22 for exactly this set. It still
produced one pen, because the key is tuple(sorted(<colours the pixel was seen
holding>)): the SET. Under a rotation every pixel visits the SAME set in a
different phase, so all seven keys are identical and dedupe to one pen. The
per-pixel keying was right; the key was the wrong function of the observation.
The ordered SEQUENCE is the right key, it comes free from the rom's cycler
script, and it needs no sampling at all.

### Make the pen key the correctness condition itself

Two pixels may share an MD pen iff they hold the same colour at every ring step. Make the pen key the
ordered sequence and that rule becomes the key's identity: phases never merge,
and palettes driven by one ring (altbeast 20/21) produce identical keys and
share one allocation without any special case. The handoff's proposed
('cyc', slot, class) token would instead have cost 8+7+7 = 22 pens against a
15-pen line -- a per-slot token cannot express "these two slots are the same
ring".

### A shared pen does not track a ring, so sharing needs a certificate

mds_install
refcounts a pen once per owning pixel, and the in-place recolour fires only at
refcount 1, so the 7 pens shared by sets 20/21 would have frozen at their
install colour. This is the CHEVPEN freeze (m_main.c:3197) and it explains its
own logged symptom -- "every one of them holds 0x0007" is step 0 of their ring,
which is uniform. The bake now emits mdr_pen_ring, a per-pen flag asserting
that every co-owner agrees at every step, and the runtime lets the owner drive
the recolour whatever the refcount. Rounds 0-8 emit zeros, so the relaxation
is live only where the cycler is.

MEASURED, each rom screenshot at ITS OWN cut (control 1456-1568, new
1459-1571; the bots never transform, so the cut is the ATTRACT demo's):
shipped tables put all 78,176 field pixels on ONE blue per frame, the whole
field strobing between three levels; the ring bake carries three blues at once
with their pixel counts redistributing every frame. Three is the quantiser's
ceiling with PAL_QUANT_RAMP off, not a shortfall. Rounds 0-8's tables are
byte-identical, so gameplay is untouched.

### The ring bake: three corrections to the decompile thread's sketch, each load-bearing (2026-09-25 23:30 ET)

FIXED (rom ring1.32x, on the rig): control shows 1 blue shade over all 78,176
px and the field strobes; the ring bake shows 3 coexisting shades whose counts
redistribute every frame. Three is the quantiser ceiling with PAL_QUANT_RAMP
off, corroborated by two hardware captures of the title screen. Rounds 0-8
byte-identical, zero ring flags -- gameplay untouched.

The decompile thread's derivation held (the hardware sharing rule, the
sequence metric, slot 19 = 8 pens). Its engineering did not:

  1. WRONG GENERATOR: it named mdpen_bake.py; the table comes from
     bake_tilecram.py. Derived instead of grepped.
  2. TOKEN KEYED PER SLOT would have cost 22 pens, not 15. The shareable unit
     is a RING, not a slot -- slots 20 and 21 share a ring pointer. Keying on
     the sequence itself gets the sharing for free and makes the key's identity
     the correctness condition.
  3. THE SHARED PENS WOULD HAVE FROZEN, and that IS the CHEVPEN freeze:
     mds_install refcounts once per owning pixel, so a shared pen sits at
     refcount 2 and the in-place recolour only fires at refcount 1. It explains
     CHEVPEN's own logged symptom -- those pens read 0x0007, step 0 of their
     ring. The bake now emits a per-pen certificate that co-owners agree at
     every step and the runtime honours it.

Also settled: the cycler declares EXACTLY THREE slots, 19/20/21, which bounds
O-14's open ask. And the cut is reachable ONLY through the attract demo -- the
bots never transform, and replaying arcade inputs into our rom diverges so the
round never advances.

**Rule:** a refcount is a sharing rule. Before proposing that two consumers
share a resource, read what the owner does at refcount > 1 -- the freeze here
was already in the code, logged, under another card's name.
### The ring bake is COMPLETE for Altered Beast: one cycler script in the whole program (2026-09-25 23:15 ET)

Whole-ROM census of the 256 KB program (altbeast US set 8), not a sample.
Signature: a count word 1-8, then that many (slot word < 128, even ROM
pointer) pairs whose target opens with a plausible step count 2-32 and whose
18-byte steps fit in the image.

    EXACTLY ONE MATCH: script 0x01A70C, 3 slots
       slot 19  ring 0x01A720  7 steps  -> 8 pens, 7 of 8 pixels move
       slot 20  ring 0x01A7A0  6 steps  -> 7 pens, 5 of 8 pixels move
       slot 21  ring 0x01A7A0  (same ring) 7 pens, 5 of 8 pixels move

So the ring bake covers EVERY cycled slot in the game. No other scene can
break the same way, and O-14's slot census is closed at three.

LIMITS, stated: the scan finds a contiguous count+pairs structure pointing at
in-image rings of >= 2 steps. A cycler driven by a different data shape, or
built in code rather than read from a table, would not match. It also does not
prove 0x1A6BA is the only consumer -- that routine has NO direct jsr/bsr in
the program, so it is reached through one of the 249 pointer-table targets the
census recovered, and the call graph was not walked.

bake_tilecram.py's PIXKEY_SCENES is {5,6,9}. Round 9 is the cut. Scenes 5 and
6 are per-pixel keyed for some OTHER reason, not a cycler -- whether their
colour-SET key is sound is a separate question this census does not answer.
### Mike's "errant tiles" are DEFECT 1's family, and the gate that reads 0 measures a narrower window than where he looks (2026-09-25 23:20 ET)

Mike, asked for a where and a when: "The errant BLACK tiles appear
consistently through level 1 gameplay. There hasn't been a where or when they
just happen through level 1."

So: not O-6 (leftover text glyphs), which is what a pattern-match would have
reached for. It is black tiles in level 1 = defect 1's family.

THE TWO REPORTS ARE NOT IN CONFLICT, and the reason matters more than the
defect:

  - defect 1 was reproduced and fixed against the BOTTOM-BAND black-cell
    census (view rows 24-27) in the ATTRACT DEMO. "Band census 0 black cells"
    is a true statement about that window (wc6 12:30, wc9 12:20).
  - Mike sees it WHOLE-SCREEN, in GAMEPLAY, consistently.

A gate reading 0 over rows 24-27 of a scripted demo cannot falsify a
whole-screen report from a human playing. Neither measurement is wrong; the
instrument is narrower than the claim. Before this was asked, the record had
the defect marked FIXED on the strength of the narrow gate.

THE RECORDED MECHANISM PREDICTS THE GAP: a priority (cat-1) FG cell whose hash
set is full of YOUNG tiles is refused a VRAM slot and left blank, and the
backstop blanks the floor under it (11:45, tables read at the hit frame).
Gameplay churns more distinct tiles than a scripted demo, so the sets fill
faster than any attract run can demonstrate. The cset+plane hash relieved the
pressure; nothing measured says it eliminated it under a human's tile load.

WHAT IS MISSING: a whole-screen black-cell census over a GAMEPLAY window, with
the slot-refusal counter read at the hit frame. black_pct is NOT it (LESSONS
185: a whole-frame black fraction swamped by legitimate black art).

**Rule:** when a gate reads 0 and the user still sees the defect, compare the
gate's WINDOW to the report's window before doubting either. Two of tonight's
dead ends were an instrument narrower than the claim it was trusted to settle.
### "No rhyme or reason" IS the signature: stop analysing the black-tile screenshots and read the allocator state (2026-09-25 23:30 ET)

Mike, on the level-1 black tiles: "every single time I've handed you
screenshots of the level 1 black tiles, there is no rhyme no reason."

Several threads have taken that as a failure to find a pattern. It is not. It
is the diagnostic, and it agrees with the mechanism already on record.

A cat-1 FG cell is left blank when its HASH SET IS FULL OF YOUNG TILES
(LESSONS 11:45, tables read at the hit frame). Which set is full depends on the
CHURN HISTORY of everything drawn before that frame -- not on screen position,
not on elapsed time, not on which art is involved. So a capacity/eviction
defect produces patternless blanks BY CONSTRUCTION. A defect that did show a
spatial or temporal pattern would point somewhere else entirely: at specific
art, a specific page, or a specific scroll position.

So the pattern is not in the pixels and no amount of screenshot analysis will
find it. It is in the slot table's occupancy at the hit frame. The instrument
is the one already used to fix defect 1's attract case -- read the tables at
the hit -- pointed at a GAMEPLAY window instead of the demo.

THE INSTRUMENT GAP, stated honestly:
  - the bottom-band black-cell census reads rows 24-27 of the ATTRACT demo,
    where it honestly reads 0. Mike sees it whole-screen in GAMEPLAY.
  - inp/ab_full (2026-09-25 16:09, untracked) is a MAME .inp of `altbeast`,
    MAME 0.288 -- the ARCADE. It is a long level-1 gameplay ORACLE and worth
    having, but arcade inputs do not drive our rom (they diverge), so it does
    not reproduce our defect.
  - headless ares is driven by oracle/inputs_gf.csv, which reaches the attract
    path. Driving OUR rom through real level-1 gameplay long enough to
    accumulate the churn is the missing capability, and it is probably a
    build, not a measurement.

**Rule:** when a defect has no spatial or temporal pattern, that FACT selects
the class of cause. Patternless means state-dependent -- read the state at the
hit, do not keep looking at the output.
### PENS and SLOTS are different resources, and the ring bake is INERT for level 1 (2026-09-25 23:40 ET)

Mike, on the ring bake: "I was REALLY hoping to see the pens reduced since we
figured out the bake." Recording why that cannot help the level-1 black tiles,
because the confusion is easy and costly:

  1. THE RING BAKE RAISED THE PEN COUNT, it did not lower it. Set 19 went
     1 -> 8 pens and round 9's line 0 filled to exactly 15 of 15. The
     under-allocation WAS the defect; more pens is the fix.
  2. IT TOUCHED ROUND 9 ONLY. Rounds 0-8 are byte-identical with zero ring
     flags, and LEVEL 1 IS ROUND 0 (STATE 671: "Level 1 (round 0) keeps all
     30"). So the bake is inert exactly where Mike sees the black tiles. It
     could not have moved them in either direction, by construction.
  3. THEY ARE NOT THE SAME RESOURCE. The black tiles are a VRAM SLOT refusal
     -- a cat-1 FG cell whose hash set is full of young tiles is refused a
     slot and left blank. Pens are MD CRAM colour entries, 15 per line over 3
     lines. Freeing pens does not free slots.

WHAT DOES TRANSFER, and it is specific rather than an analogy: a tile's slot
identity INCLUDES its colour set. TAGKEEP's own note records that "a tile's
shipped pattern depends on its colour set's (line, pixel -> pen map) and NOT on
the pen colours", and defect 1's fix folded cset+plane into the slot hash for
exactly that reason (FG and BG variants of one tile were colliding). So pen-map
DISTINCTNESS drives slot pressure: identical pen maps let tiles share a slot,
distinct ones force separate slots.

That makes the open question a sharp one -- is the slot hash still coarser, or
now finer, than the sharing rule actually requires? Defect 1's fix took one
step of exactly that kind. It is the same reasoning the ring bake needed (key
on precisely what the hardware rule cares about, nothing coarser and nothing
finer), applied to the other resource.

**Rule:** name the resource before proposing relief. Pens (CRAM, 15 x 3) and
slots (VRAM tile patterns, the hash table) have separate capacities, separate
failure pictures and separate fixes.
---

### The attract demo runs the renderer at HALF the slot load, so it cannot gate a gameplay defect (2026-09-25 23:25 ET, ares, ring1+MDALLOCWHY)

Mike's black tiles and the passing bottom-band gate are both correct, and the
reason is not the screen region. It is the LOAD.

Same rom, same emulator frame (10800), the only difference being whether a
route is driving it:

    ATTRACT       slots  499/1024   sets FULL (8/8)   0/128   cat-1 declines  223
    GAMEPLAY      slots 1018/1024   sets FULL (8/8) 123/128   cat-1 declines 2760

The attract demo never fills a single one of the 128 slot hash sets. Gameplay
fills 123 of them and holds the table at 1010-1023 of 1024 slots for the whole
run. Declines run ~1 per frame in play against ~20 per 1000 in attract -- a
50x difference in the exact event that produces the defect.

So "band census 0 black cells" was never evidence about gameplay. A census run
in attract exercises half the renderer.

### The level-1 black tiles are cat-1 slot-pressure declines, and the patternlessness is the hash set, not the art (2026-09-25 23:30 ET)

REPRODUCED in gameplay on ares, which no earlier route could do: 6 of 44
sampled level frames carry fully-black 8x8 cells, in view rows 24-27, at
frames where the decline counter is climbing ~1150/1000 frames.

The mechanism is already in the code with Mike's name on it (m_main.c 17604,
"SLOT PRESSURE (2026-09-03, Mike's black cells"): a cat-1 tile whose cache set
is full of ways younger than 12 windows does NOT evict -- `done = 1; slot
stays BLANK` -- because the evicted BG tile would re-claim at its next visit
and the two would ping-pong. The framebuffer is supposed to keep the cell
(CAT1_PEND). When it does not, the cell falls through to the backdrop: black.

WHY IT LOOKS PATTERNLESS, which is the part worth keeping. The refusal is
keyed on MD_SET(code, cset, isfg) -- a HASH of the tile code, colour set and
plane -- and on the AGE of the ways already in that set. It is not keyed on
screen position, on the art, on the colour set alone, or on elapsed time.
Which cell loses depends on the churn history of its hash bucket, so
"no rhyme no reason" is the signature of the mechanism, not an absence of one.
Two cells of identical art, one bucket apart, differ.

NOT the other black-cell paths, both ruled out by the same dumps: "refused:
not in the scene's table" is 1 for the whole run (so it is not a set missing
from the round's baked table), and the round-9 ring bake is inert here (level
1 is round 0).

CAPACITY IS THE FRAME, NOT A BUG: 1024 slots, and sustained level-1 play wants
essentially all of them. At frame 20000 the table reads 1023/1024 with 127/128
sets full. NO FIX TONIGHT -- this entry is the identification.

### ring1's rig gate and speed, measured rather than inherited (2026-09-25 23:10-23:20 ET)

PICTURE GATE 3/3. Three launches under distinct names (the launch API no-ops
on a path already running), 20 shots each at 9 s from 14 s after launch, pulled
date-filtered. 39 demo frames carry 0 black cells and 0 purple-band cells;
60/60 shots are purple-free. The one outlier is the attract EYE screen caught
mid-draw-in, whose gaps are the known transition latency.

SPEED: NO RESOLVABLE DIFFERENCE against nt1. RIGSPAN=4, light barcode
(RIGBCEVERY=8), four interleaved launches r/n/r/n, 14 shots each. Per attract
step, span>=2 counts were ring1 {50,46,54,44,40,20} against nt1
{26,46,62,44,16,28} -- fully overlapping, with nt1 holding both the highest
and the lowest reading. One-vint counts overlap the same way.

This replaces STATE's "nt1's speed by construction" with a measurement, but it
is a NULL result, not an equality proof: at 14 shots a launch the per-step
counters are sampled once or twice each and the spread swamps any small
difference. And ring1's `_end` sits 8 bytes above nt1's, which the resolution
floor entry says is enough to move a ladder on its own. A difference this
instrument cannot see is all that is claimed.

### SH-2 cycles per instruction on the FPGA: 1.125 cache-resident, 5.1-8.75 all-miss (2026-09-26, SH2BENCH)

PORT-CENSUS.md priced the whole 68K->SH-2 port on an ASSUMED SH-2 figure and
said so. It is now measured, the way CARTBENCH measures the 68K's:
`make line WRAMCODE=1 SH2BENCH=1 RIGBARCODE=1 RIGBCEVERY=8`, two rig launches,
16 decoded barcodes.

    variant 0  register ALU, I-cache resident   1.125   (byte 5 = 9 in ALL 16
                                                         samples, both launches)
    variant 1  cached SDRAM loads, stride 128   5.125-8.75  (41,42,52,55,59,
                                                         61,65,69,70 eighths)

THE ASSUMPTION WAS WRONG AT BOTH ENDS: assumed 1.5 / 4.0, measured 1.125 /
up to 8.75. The port's floor is BETTER (25x the 68K, not 18.8x) and its
ceiling WORSE (3.2x, not 7.1x). The recommendation survives -- even all-miss
is 3.2x -- but the spread between the ends is 8x, not 2.7x, so WHICH END a
routine lands on is the design question, and it is a cache question.

The miss loop is not an arbitrary stress: it is exactly the access pattern
PORT-CENSUS flags for the object table (512 reads at stride 128 touch 8 cache
sets and contend for 32 lines). Its 4.6-7.8x penalty over the same loop shape
running cache-resident is the measured price of the arcade's 128-byte stride,
and it makes the re-stride-to-144 note the cheapest item in that document.

ARES AS THE CONTROL, same rom: 1.000 and 4.000. Near-exact on the
cache-resident loop, ~2x OPTIMISTIC on the miss-heavy one -- "ares charges
INSTRUCTION COUNT" with a number on it. Note the miss figure MOVES with bus
contention on the rig and does not on ares; a single miss-heavy reading is not
a constant and should not be quoted as one.

TWO THINGS THE PROBE HAD TO LEARN, both worth keeping for the next one:
  - A VALUE PUBLISHED ON COMM6 IS LOST. COMM6 is the k1 announce register;
    the 68K overwrites it before reading. Byte 5 never latched. The barcode's
    counters ride the FB PACKET HEADER (words 4/6 high bytes) and that is the
    channel to use -- RIGB5 / RIGB6 exist for exactly this.
  - A DIAG SLOT WRITTEN ONCE PER 64 VINTS READS 0. DIAG is cleared wholesale
    (m_main.c 12543/12569), so a periodic result must be latched in a static
    and REPUBLISHED every vint. The probe's first reading was a confident
    "ticks 0" from this alone.

### KILLED ON PAPER: BURSTGATE + the mid-pass push is not worth a build (2026-09-26)

The standing claim was that the two together come to ~8 lines once WRAMCODE is
on the candidates, which is below the rig's ~15-line ranking floor. Checked,
and it is worse than that claim -- one of the two terms is already MEASURED at
zero on the rig, so no arithmetic is needed to kill it:

  - BURSTGATE CONVERTED NOTHING ON THE FPGA (LESSONS "Item 4 converted
    nothing", two launches, per ~440 level-1 demo vints: one-vint +34,
    two-vint ~+170, three-vint +16 -- the control's own numbers). Its ~10
    68K lines a pass are real and still buy no frame.
  - the palette-only mid-pass push is priced at ~5 lines a vint, half of
    BURSTGATE (2026-09-24 18:10).
  - the rig cannot rank a cut under ~15 lines a vint (O-14's premise), and
    the 2-vint frame is a THRESHOLD not a slope -- a cut short of the line
    reads as no conversion at all.

Mind the UNITS, which is where the ~8 came from: 10 lines a PASS is not 10
lines a VINT. A pass that spans two vints makes BURSTGATE ~5 lines a vint, so
the honest sum is ~10 lines a vint -- still under the floor, and still owed to
a term that measured zero.

AND THERE IS NO OVERLAP TO REMOVE: WRAMCODE does not cover BURSTGATE's site.
The text-copy loops are 0x3A9A and 0x3AA4; WRAM_BLOCKS holds (0x398E,0x39C8)
and (0x3AAE,0x3AFE), which bracket them without containing them. So "once
WRAMCODE is on the candidates" was never a double-counting argument, and
correcting it does not rescue the stack.

### The palette rotor does NOT price high on the FPGA: removing it entirely moves neither rig instrument (2026-09-26, PALROTOROFF on the rig)

O-13's 94.7% against 82.3% is an ARES number and the record said it would not
transfer. It does not. PALROTOROFF=1 is the CEILING -- the 68K ships no
palette blocks at all, rotor and compare and selection skipped -- so whatever
the rotor is worth on hardware, this is the most of it.

THE FLAG DEMONSTRABLY TOOK, which is the first thing to establish and the
easiest to skip: the barcode's own palette counter (byte 4, blocks flagged and
consumed) falls from 179-234 on the control to 50-67 with the flag, in all
four launches. The rotor really is off.

    RIGSPAN=11, mean pass length in lines, 4 launches interleaved c/o/c/o
      step 1   control 264-308      rotor off 268-306
      step 3   control 360-484      rotor off 358-410
      step 5   control 318-374      rotor off 322-360
    Overlapping at every step, with the control holding the extremes.

    RIGSPAN=4, ONE-VINT frames (the metric cadence actually rests on)
      step 1   control 44-110       rotor off 66-154
      step 3   control 10-42        rotor off 16-44
      step 4   control 24, 24       rotor off 40, 46
      step 5   control 54-110       rotor off 64-112
      step 2   control 358-380      rotor off 120-132   (contradicts)

So there is a HINT of more one-vint frames without the rotor at four steps of
five, and step 2 points the other way. It is not resolvable at this sampling:
the RIGSPAN counters accumulate within an attract step and reset when it
changes, so one to three shots a step samples an accumulator at an arbitrary
point, and the spread swamps the effect.

WHAT THIS SETTLES, which is the point: a 12-point conversion gain -- what the
ares ceiling predicts -- would be unmissable in these numbers. It is not
there. **The rotor is not the 41-line item on the FPGA that the cadence plan
is built on**, and a per-slot split of it is not worth writing until the
whole-rotor price is resolvable.

WHAT THE NEXT INSTRUMENT NEEDS, and it is not a per-slot probe: a WHOLE-RUN
total rather than a per-step accumulator. Every rig speed reading in the
record is a per-step counter read once or twice per launch, which is why the
per-launch spread (~15 lines) has repeatedly swamped the thing being measured.
A barcode mode that latches a running total across the whole attract loop
would turn four launches into four numbers instead of twenty-four samples of
six accumulators.

### THE CUTS PATH CANNOT REACH 60Hz, AND THE FULL PORT FITS AT 3.1% — both measured overnight (2026-09-26 ~06:00 ET)

Two results from the same night settle the cadence architecture, and they point
opposite ways. Recorded together because either alone would mislead.

**1. The rotor's 41 lines do not survive contact with the rig.** PALROTOROFF is
the CEILING (no palette visits at all, colours freeze) and it demonstrably TOOK
-- palette blocks flagged fell 179-234 -> 50-67 across all four launches. Yet
mean pass length overlaps the control at every step. The ares ceiling predicts a
12-point gain; a gain that size would be unmissable and it is not there.

So the 68K axis's inventory, after the cart-fetch axis was already SPENT at wc2:

    palette rotor      41  ->  DEAD ON HARDWARE (this measurement)
    stage ship         18      at the rig's ~15-line ranking floor
    consumes           15      MDQUIET=2 already showed no FPGA gain
    rowscroll/select    8      below the floor
    part B              5      below the floor

Against a ~68-line threshold, what remains is ~46 lines of which none is
individually rankable and one is already measured worthless. **Cuts cannot
cross.** 60Hz does not come from 68K micro-optimisation, and the cadence stack
should stop being planned.

**2. The full port's performance question is CLOSED, favourably.** SH-2
cycles per instruction, measured on the rig (the figure PORT-CENSUS named as
"assumed, and it is the whole ratio"):

    cache-resident   1.125 cyc/instr, IDENTICAL in all 16 samples, two launches
    all-miss         5.125 to 8.75 cyc/instr, both loops disasm-verified

Recomputed against PORT-CENSUS's own terms (383,333 SH-2 cycles a vint, 8,184
68K work instructions, the measured 1.27x executed translation ratio):

    cache-resident        port needs  3.1% of the vint
    all-miss, best case              13.9%
    all-miss, WORST case             23.7%

**Even if every single access misses cache, the port fits in under a quarter of
the vint.** The budget allowed 46.8 SH-2 cycles per 68K instruction = 41.6
SH-2 instructions at the measured rate, against 1.27 needed. So the risk is no
longer "is the SH-2 fast enough" and not really "does the data stay in cache"
either -- cache failure is a 7.6x cost on a 33x margin. PORT-CENSUS's verdict
stands and narrows: **the only real risk is FIDELITY across 25,284
instructions.**

And the cheapest item in that document is now measured: the all-miss loop IS
the object table's 128-byte stride, carrying a 4.6-7.8x penalty. Re-striding to
144 gives every slot its own cache set for 1 KB of padding. Only a port can
take it; the 68K has no cache.

**Rule:** an ares ceiling is a hypothesis about the rig, not a measurement of
it. O-13's 94.7/82.3 was carried for weeks as the rotor's prize and the rig
says the prize is not there. Price a ceiling on the hardware BEFORE planning a
stack around it.

### The attract demo runs the renderer at HALF the slot load, which is why the black-tile gate honestly read 0 (2026-09-26 ~06:00 ET)

Mike's level-1 black tiles reproduce, and the instrument gap is now a number.
Same rom, same frame, via the new gameplay route (discover/inputs/
gameplay_level1.csv -- holds our rom in play for 90% of 20,000 frames):

                    slots used    hash sets full    cat-1 declines
    attract           499/1024         0/128             223
    gameplay         1018/1024       123/128            2760

The attract demo never creates the condition. 0 of 128 sets full against 123 of
128. So "band census 0 black cells" was true and irrelevant at the same time --
the gate was measuring a machine state the defect cannot occur in.

The refusal is the SLOT PRESSURE rule at sh_src/m_main.c:17700, which has
carried Mike's name since 2026-09-03. It keys on the hash of code, colour set
and plane and on the AGE of that bucket's ways -- never on position, never on
art. **That is why there is "no rhyme no reason": patternlessness is the
mechanism's signature, not a failure to find the pattern.**

What defeated every earlier attempt at a gameplay route was SURVIVAL, not the
coin: coins register at every frame tested, but an idle player dies in a few
hundred frames and the return to attract looks exactly like a coin that never
landed. The credit counter and in-play flag came from a differential work-RAM
dump and are in the route file's header.

**Rule:** before trusting a gate that reads 0, measure whether its window
CONTAINS the defect's precondition. Two nights were spent on a gate whose
machine state made the defect impossible.
### The image is part of the fact: a wrong-SET read gives the right answer at the wrong address (2026-09-26 13:00 ET)

altbeast.zip holds NINE program sets. The port ships SET 8 — the parent pair
epr-11907.a7 (high) + epr-11906.a5 (low) — which patch_game.py targets and
which roms/altbeast/prog68k.bin has contained since 2026-08-20.

Yesterday I derived the cycler script from an image I interleaved myself out of
the zip, picked altbeast4 (SET 4) without noticing, and used it to "correct"
addresses that had been right:

    set 8 (SHIPPED)   script 0x01A6FA   rings 0x01A70E  0x01A78E
    set 4 (my error)  script 0x01A70C   rings 0x01A720  0x01A7A0

**The two sets carry the same cycler data shifted by exactly 0x12 — one ring
step.** So the wrong set produces the RIGHT pen demand (steps, pen classes,
step-0 colours and full sequences all verified identical) at the WRONG
addresses, and nothing anywhere complains. That is the worst shape a data bug
can have: self-consistent and silently mis-sourced.

CONSEQUENCES, bounded by measurement rather than assumed:
  - ring1.32x and its baked tables are CORRECT. Only the informational `ring`
    field differed; every field the bake consumes is identical. No rebuild.
  - the live defect was PROVENANCE in the build path: ring_script.py's
    program_from_zip returned altbeast4 before the parent (sorted order), and
    both it and bake_tilecram.py defaulted to set 4's 0x1A70C. Consistent with
    each other and wrong about the shipped rom. It would have broken silently
    on the next title or any set whose cycler data moved.
  - FIXED: ring_script.py defaults to roms/<game>/prog68k.bin, --zip prefers
    the parent set, and both default addresses are 0x1A6FA.

**Rule:** cite the IMAGE with the offset, and prefer the toolchain's existing
image over one you build yourself. A rom set is not a rom. `cmp` against
prog68k.bin was available the whole time and would have caught this before I
wrote the wrong numbers into LESSONS, RING-BAKE and two handoffs.
### F0(d): the 68K program is a DISPATCH MACHINE, and every dispatch is table-driven (2026-09-26 13:30 ET)

The item flagged as most likely to kill the full-port proposal. It does not.

    reached instructions (2 entries + 249 pointer roots)   25,284
    indirect control transfers among them                      62  = 0.245%
    reachable from the two entry points ALONE                  846

239 of the 249 pointer targets are reachable ONLY through dispatch. The direct
call graph is 846 instructions; the program is 25,284. **An AOT must treat
dispatch as the main mechanism, not an edge case.**

All 62 classified (40-instruction backward walk to the routine top):

    11  word-offset jump table, pc-relative base    statically resolvable
     3  pointer table via lea pc-relative           statically resolvable
     1  lea absolute, single target                 statically resolvable
    12  handler pointer from an object record       resolvable via the 249
    35  register set outside the routine            lookup-returns-a0, below

The dominant form is the friendliest indirect there is: `lea %pc@(TABLE),%a0` /
byte index / `addw` / `addaw %d0,%a0` / `addaw %a0@,%a0` / `jmp %a0@` — a
16-bit-offset jump table with a LITERAL pc-relative base and a BYTE index, so
at most 256 entries and the table reads straight out of the image. 15 distinct
bases, all literals: 0x346A 0x88EC 0xD0EC 0xD5A2 0x100F6 0x10104 0x11E34
0x128DA 0x12FF2 0x12FFC 0x13926 0x141B8 0x148A2 0x15E60 0x16938.

The 35 "unresolved" are NOT computed targets: each is `bsrw <lookup>` / `bccs
<skip>` / `jmp %a0@` — a lookup subroutine returns the handler in a0 and signals
validity in carry. Their universe is what those lookups read, and they read ROM
tables.

So all 62 reduce to "fetch a routine pointer from a ROM table and branch". **No
fallback interpreter is needed** (which matters because an interpreter measured
~1.07x the vint and does not fit). The translator needs one address -> entry map
and an indexed call.

Residual risk and its answer: pointer_targets() is heuristic, so 249 is not
proven complete. The translator can TRAP on an unmapped dispatch, so a missing
target halts loudly at a named frame under the L2 differential instead of
running wrong. Static-completeness becomes test coverage.

**F0's remaining blocker is (a): 3.1 KB of SDRAM headroom under the region guard
against 15.6 KB of state needed.** That is now the item that can kill it.
### F0(a): the port frees the room it needs — the 52 KB tilemap shadow is an artefact of the split (2026-09-26 14:00 ET)

I posed this as "does the BURNS list free 11 KB below the region guard". Wrong
question. Below the guard 94.4 KB of 102.4 KB is committed and 76 KB of it is
`sbuf`, the 336x232 compose buffer, which BURNS does not name. Headroom 4,616 B
against 15,974 B needed.

The answer is what the guard protects. `0x06019000` is TILEMAP_C, the SDRAM
TILEMAP SHADOW (m_main.c:77; the Makefile guard says "tilemap shadow and friends
live there"). Size agreed three ways: TILEMAP_C to TEXT_C = 53,248 B; NPAGES 12
plus the blank page 12 = 13 x 4096 = 53,248 B; REBUILD.md §1's own "Tile RAM
(~52KB live)".

**The shadow exists only because the game runs on the 68K.** REBUILD §1: the
game reads back its tile staging, so its video state must live where a 68K can
read it coherently. The shadow is the SH-2's readable copy. Port the game and it
stages tiles where it lives; there is nothing to shadow and nothing to copy.

    port state needed        15,974 B
    tilemap shadow released  53,248 B
    margin                   37,274 B

Honest form of the accounting: the ~52 KB of staging does not vanish, the COPY
does — the original moves into SDRAM, where 52 KB of room already exists for
exactly it. Either reading clears the obstacle.

**Rule:** when a budget looks short, check whether the thing occupying it is an
artefact of the architecture you are proposing to change. I nearly reported an
11.1 KB shortfall by counting inside a boundary that the proposal deletes.
### F1 step 1: the existing disassembly cannot be the translator's front end (2026-09-26 14:30 ET)

Censused what an emitter must cover, and what it can trust. The second is the
problem.

COVERAGE is tractable: 162 distinct mnemonic forms in the reached set, 58 cover
95%, 98 cover 99%; `move` in all sizes is 27%. And the FLAG strategy is already
proven in docs/design/collision_sh2.s -- it does not materialise a 68K CCR, it
FUSES each flag-setter with its consuming branch (tst/bt). That fusion is why
1.27x executed is achievable; an unfused flag consumer must be the exceptional
path, not the rule.

TRUST is not. roms/altbeast/prog68k.asm is a LINEAR disassembly:

                          reached instrs   undecoded .short
    below 0x20000              17,405           109  =  0.6%
    at/above 0x20000            7,879         2,850  = 36%

The high region is genuinely code -- 559 DIRECT call/branch targets land above
0x20000, against 2 pointer-table roots. The 2,959 undecoded words form 989 runs
(longest 127 words, 544 of them a single word) and only 15 coincide with F0(d)'s
15 jump-table bases. That is a linear disassembler losing ALIGNMENT on embedded
data and resyncing at the wrong parity, not data mistaken for code.

SO: the translator's front end must be a RECURSIVE-DESCENT decoder rooted at the
two entries plus F0(d)'s resolved jump tables. A linear listing cannot be
repaired into one -- code/data separation is the whole difficulty and only
reachability answers it. It also replaces pointer_targets()'s heuristic root
guess with a derivation.

AND A CORRECTION TO A NUMBER THE RECORD CARRIES: "25,284 reached instructions"
includes ~2,959 undecoded words, so the true reached-code count is nearer 22,300
and ratios dividing by it are ~12% optimistic. The budget is unthreatened (3.1%
of a vint cache-resident, 23.7% all-miss) but PORT-CENSUS's count should be
re-derived once the decoder exists.

**Rule:** before trusting a disassembly as a tool's input, measure its undecoded
fraction BY REGION. The aggregate 11.7% hid a clean 0.6% and a broken 36%.
### The 0x20000+ region is DATA: I built an argument on a broken listing's own output (2026-09-26 15:00 ET)

An hour after recording that the high region was "genuinely code, 559 direct
call/branch targets land there", Ghidra's independent recursive descent
falsified it:

    linear-listing edges landing above 0x20000                    573
      originating at an instruction GHIDRA ALSO DECODED             0
      originating at bytes Ghidra does not consider code          573

Zero of 573. Those edges are garbage decodes in a data region whose displacement
fields happen to read as branches. **I used the broken listing's output as
evidence that the broken region was code** -- circular, and the 0.6%-vs-36%
undecoded split I had just measured was the clue I failed to follow.

Corroboration: Ghidra's highest decoded instruction is 0x1AF74, just past the
cycler script at 0x1A6FA. Code occupies ~0x400-0x1B000, about 107 KB of the
256 KB image; the remaining ~148 KB is art, tables and level data. An entirely
ordinary S16B layout, and it should have been the first hypothesis.

CONSEQUENCE, and it is favourable: the port is SMALLER. PORT-CENSUS's 25,284
reached instructions is inflated by data-region garbage well beyond the ~12% I
estimated from the .short count alone. Fewer instructions = less fidelity
surface, which is the full-port proposal's only remaining risk. Re-derive the
count from the decoder; do not adjust it by a factor.

AND THE RIG ALREADY HAS THE ENGINE. tools/ghidra/ carries a seeding fixpoint
(seed_harvest -> seed_apply) built for exactly this. One seeding round took
Ghidra from 8,822 instructions / 234 functions to 12,456 / 536. Two gaps:
seed_harvest reads the OBJDUMP listing to find seeds for Ghidra (using the
unreliable listing to repair itself -- point it at Ghidra's own listing), and it
knows two dispatch shapes but not F0(d)'s dominant WORD-OFFSET table shape
(11 sites, 15 bases). Also measured: P-code is available at 5.6 ops per
instruction, and dead-flag elimination over it is the fusion pass
collision_sh2.s does by hand.

**Rule:** never accept a tool's output as evidence about the region where that
tool is failing. The undecoded-fraction split was already measured and I argued
past it.
### F1.1: the jump tables are NEGATIVE offsets with an 0x8000 sentinel, and gating on prior decoding cannot find them (2026-09-26 15:45 ET)

Ghidra, driven from the rig: 8,822 instructions unseeded -> 12,456 with
seed_harvest's 303 seeds plus the two vectors -> 13,037 with F0(d)'s jump tables
expanded correctly. New Ghidra-native scripts f1_export / f1_seeds / f1_tables /
f1_gap (they match instruction OBJECTS, never disassembly text).

THE STRUCTURE, derived from the raw words: each table sits at the END of its code
block and its entries are int16 offsets RELATIVE TO THE BASE, so negative,
pointing back into the preceding block. 0x8000 TERMINATES. Entry counts over the
15 bases: 2 4 0 0 14 7 7 5 10 5 7 5 11 6 8 = 90 targets, against 19 from the
first attempt.

WHY THE FIRST ATTEMPT GOT 19: it gated each target on being an already-decoded
instruction start, using the linear listing, which does not decode those blocks.
**The targets are undecoded precisely BECAUSE they are only reachable through the
table**, so that gate can only ever return the first entry. This circularity bit
this work TWICE in one day -- the other time was concluding 0x20000+ was code
from the broken listing's own branch edges.

STILL OPEN AND IT IS THE BULK: 76,870 undefined bytes in 0x400-0x20000, 72% of it
in 38 gaps of >=256 B. Expanding the tables barely moved them (largest in-code gap
4,600 -> 4,078 B), so the targets land at gap EDGES but the bodies do not decode
onward. Two hypotheses, NOT to be guessed between: data interleaved in the code
region (normal for S16B -- animation scripts, sprite tables), or code reached by
shape B (a routine pointer stored into an object field), which is only partly
covered because seed_harvest derives it by regex over the linear listing. The
test is structural: do those bytes look like instruction streams or like tables.

**Rule:** never gate the discovery of undecoded code on it being already decoded.
If a target is only reachable through the mechanism you are modelling, prior
decoding is evidence of nothing.
### RIGRATE: a rig speed number that CONVERGES, so four launches are four numbers (2026-09-26)

Every other rig speed reading in the record is a per-attract-step accumulator
sampled once or twice a launch, so the ~15-line per-launch spread has
repeatedly swamped the effect being measured (the rotor, ring1 vs nt1, and
BURSTGATE all died on this). RIGRATE=1 counts game frames and vints from boot
and NEVER resets them: barcode byte 5 = game frames per vint x 255, byte 6 =
vints/256 so a reader can see it has settled.

Measured spread across a run: 176-188 on ares (ONE band, +-3%), against the
old instrument's step-to-step swings of 2-3x on the same rom. On the rig it
separated an 8-point effect cleanly on two launches a side. 255 = 60 Hz,
128 = every other vint; the level-1 demo control reads 147-150 (58%).

### F0(c) PART 1: the MASTER cannot host the game's work for free -- 8.3 points of cadence (2026-09-26)

PORTLOAD=1: 10,400 SH-2 instructions a vint (the translated game's count)
against a 16 KB working set walked one cache line at a time -- 4x the SH-2's
4 KB cache, so it exercises RESIDENCY, which FULL-PORT (b) names as the
planning variable. Exactly 5 instructions an iteration, verified against the
disassembly.

    control       147-150   (58.2%)      two launches
    MASTER load   123-132   (49.9%)      two launches

Non-overlapping bands, 8.3 points, a 14% relative loss -- and consistent with
the cycle budget: 10,400 instructions at the measured 5.1-8.75 all-miss
cyc/instr is 13-22% of a vint. Confirmed directionally on ares (166-176
against the control's 179-188), where the load also runs.

So the master, which is the renderer, has no free 10,400 instructions.

### F0(c) SUPERSEDED BELOW -- both arms answered once the picture gate existed (see "F0(c) ANSWERED")

### F0(c) PART 2 as it stood before the gate (2026-09-26)

TWO TRAPS, both caught, both worth keeping.

**The first slave measurement was a false null.** The hook sat in the slave
loop's `(cmd & 0xF000) == 0x3000` branch (CMD_BLIT). THIS LINE NEVER ISSUES
CMD_BLIT -- every slave_cmd call passes tile_cmd (CMD_TILE, 0x2000). The load
never ran, and the reading was "slave load costs nothing", which is exactly
the answer the intuition wanted. It was caught only by a 10x CONTROL: ten
times the load moved the rate not at all, which no real load can do.
**Run the 10x control before believing any null.**

**With the hook fixed, the result is paradoxical and monotonic:**

    control          145-150   (58.0%)
    SLAVE load 1x    149-160   (59.9%)
    SLAVE load 10x   158-169   (63.9%)   band does NOT overlap control

More load on the slave makes the GAME FRAME RATE go UP. The likely mechanism
is that delaying the slave's echo shortens or drops compose work, which frees
the 68K -- i.e. the number is bought by rendering less. That is the FBXPORT
failure exactly (a logic metric improved while the picture froze; the lesson
there is to rank on presented MOTION, not on the counter).

**So RIGRATE alone cannot rank slave-side work**, and the slave arm of F0(c)
needs a picture gate beside the rate before it can be answered. Do not read
these numbers as "the slave has headroom" -- they are equally consistent with
"the slave's delay is degrading the frame we ship". ares cannot settle it
either: there the same builds are NON-MONOTONIC (1x below control, 10x above),
which is the standing rule that memory-traffic cards rank on the rig.

### The oracle's own execution is the only non-circular seed source (2026-09-26 16:15 ET)

From the prior Kyocera decompile (~/src/kyocera-2235, Mike's pointer): that
project traced the real ROM under Unicorn for ground truth instead of inferring
it. Unicorn is not installed here, but THIS repo already has the same instrument
and it is the sanctioned oracle -- tools/arcade_trace.lua runs `mame altbeast`
with AT_PLAY=1 driving a real level-1 playthrough, and MAME's instruction trace
is exact.

    distinct executed PCs, 40 frames of gameplay        1,677
    range                                         0x00404-0x1AF74
    executed PCs at or above 0x20000                        0
    executed PCs Ghidra already decoded                 1,474
    executed PCs Ghidra did NOT decode                    203   <- real code
    seeded and decoded                                    200

ZERO executed PCs above 0x20000 is a third independent confirmation that the
region is DATA (after Ghidra's recursive descent and the phantom-edge analysis).
Instructions 13,037 -> 13,526; low-region code 39.3% -> 40.9%.

**THE ASYMMETRY, and it bounds the method:** an executed PC PROVES code; the
ABSENCE of executed PCs proves NOTHING, because one playthrough does not reach
every routine. The oracle trace closes the set from BELOW only. 75,726 bytes of
the low region remain unclassified, and F1.1 must close with an explicit list of
never-executed gaps the translator will translate blind or trap on -- the trap
being the design already (F0(d)).

Executed code also needs no heuristic and resolves every dispatch by
construction, which is why it is the one seed source that cannot be circular --
the failure mode that bit this work twice in one day.

ALSO TRANSFERABLE: ~/src/kyocera-2235/analysis/coverage.md is a per-function
coverage table with PORTED / ANALYZED / UNTOUCHED status, a guessed purpose and
the evidence per function; that project drove 12 -> 83 PORTED of 93 functions
against it overnight. That is exactly the instrument F2 needs, proven rather than
invented. F2 opens by generating it from Ghidra's 536 functions with a fourth
column for the L1 differential verdict.

**Rule:** to discover code, ask what the machine EXECUTED, not what a listing
says. Every other source here has been circular or heuristic.
### F1.1 closes: execution tracing saturates at 15% of decoded code, so the never-executed set IS the deliverable (2026-09-26 16:45 ET)

Four arcade traces (boot, attract, title, level-1 play) merged by the new
tools/f1_coverage.py:

    union 2,055 distinct PCs, range 0x00404-0x1AF74
    marginal gain: boot +105, title +62  -- SATURATED
    executed at/above 0x20000: 0  (fourth independent confirmation of DATA)
    Ghidra decodes 13,526 instructions; only 2,055 ever executed
      => 11,476 decoded instructions (85%) never reached by any playthrough

AND EVERY ONE OF THE TEN LARGEST GAPS CONTAINS ZERO EXECUTED PCs (the biggest
four: 0x1AF76-0x20000 20,618 B, 0x14844-0x15832 4,078 B, 0x15B1E-0x16878
3,418 B, 0x0E2F0-0x0EF2C 3,132 B). So tracing cannot classify them and more
tracing will not -- absence is not evidence (the recorded asymmetry) and the
method has visibly plateaued. A 60-frame level-1 window cannot exercise five
rounds, bosses, deaths and continues.

SO F1.1's deliverable is the LIST, not a classification: 59,438 bytes in 39 gaps
of >=256 B, none containing an executed PC, plus 11,476 decoded-but-unexecuted
instructions. F0(d) already designed for this -- the translator TRAPS on an
unmapped dispatch and on entry to any address it did not translate, so under the
L2 differential over the 20,000-frame gameplay route anything the static analysis
missed halts at a named frame instead of running wrong. The unclassified bytes
become a test-coverage question, which is what the verification ladder already is.

**Rule:** when a discovery method's marginal yield collapses (105 and 62 PCs from
whole new game phases), stop gathering and make the residue an explicit,
trapped-on list. An open-ended search for completeness has no termination
condition; a trap does.
### The undecoded gaps are 59% DATA with ZERO flow references, and full coverage is ~13 KB away (2026-09-26 17:15 ET)

Two corrections, both Mike's, and the second raises the bar.

FIRST: my "85% of code is never reached" was not a finding and conflated two
populations. The 11,478 instructions Ghidra DECODED but no trace executed are
FINE -- known code, known extents, they translate normally, no trap needed. They
are rounds 2-5, other enemies' AI, bosses, death and continue. Mike: "These are
just instructions for enemy AI and level layout." Only the 59,438 bytes Ghidra has
NOT decoded were ever the open question.

SECOND: "translate blind or trap" is too low a bar. Mike, on the prior Kyocera
work: we did not have Cavern Crawl until the entire game was decoded, and that
project's coverage.md reached 0 UNTOUCHED of 93 functions. Standing rule 1 is
fidelity over everything. The trap stays a SAFETY NET, not the plan.

THE TEST MIKE'S FRAMING SELECTED: AI is code reached by dispatch, level layout is
data reached by a pointer load, so the REFERENCES into a gap classify it --
no further tracing needed (tools/ghidra/f1_gapref.py):

    39 gaps >= 256 B, 59,438 B
      DATA          35,156 B   data references only
      CODE               0 B   NOT ONE GAP HAS A FLOW REFERENCE
      UNREFERENCED  24,282 B   no reference of any kind

Zero flow references means no gap is an unreached callable routine, so 59% of the
gap space is provably not code. And 8 of the 19 unreferenced gaps END within 1 KB
below a jump-table base (10,942 B), which is the shape already derived: each table
sits at the end of its code block and dispatches BACKWARDS into it. Those gaps are
those blocks -- code, undecoded only because the table expansion is incomplete.

    59,438 - 35,156 data - 10,942 table bodies = 13,340 B in 11 gaps

**~13 KB of a 256 KB image stands between here and full coverage** -- a
terminating target, not an open-ended search, which is exactly why the trap should
not have been the plan.

**Rule:** classify by what REFERENCES a thing before trying to reach it. One
reference pass split a 59 KB unknown into 35 KB of data, 11 KB of known-shape code
and 13 KB genuinely open; four emulator traces had split it not at all.
### Use docs/audit before re-deriving it: the saved seeds restored 60.9 KB of coverage in one command (2026-09-26 17:45 ET)

Mike: "I shouldn't have to coach you on using ghidra to fully exhaust and decode
the instructions. PLUS we already have a huge portion of the assembly in FRONT of
us." Correct, and the cost was most of an afternoon.

WHAT WAS ALREADY IN THE REPO and went unused while I seeded one shape at a time:

    docs/audit/function_map.md     560 annotated functions, 60.9 KB, span
                                   0x00404-0x1BB28, with a class and an
                                   evidence column per function
    docs/audit/altbeast_seeds.json 4 dispatch tables, 275 object routine
                                   pointers, 433 reference function starts,
                                   692 function seeds, 19,137 repair seeds
    docs/audit/function_map2.md, bound_repairs.md, timing_census.json

Applying the saved seeds took the fresh import from 13,533 instructions / 536
functions to 20,355 / 720, and low-region code from 40.9% to 60.8% -- matching
function_map.md's 60.9 KB exactly. One command. My exhaustive-disassembly pass
over the whole code band (13,732 attempts at every even offset) produced ZERO new
instructions, which was the signal that the missing code needed SEEDS, not brute
force.

AND I REPRODUCED A DOCUMENTED MISTAKE ON THE WAY: passing all 2,229 seeds as
function seeds gave 2,121 functions. LOOP-DECOMPILE 19 records exactly that
number as the failure -- "it took 433 real functions to 2121 and made the call
graph useless." seed_apply.py's own docstring warns about it. The correct split
(all as disassembly seeds, only the 823 genuine entries as function seeds) gives
720 functions.

**Rule:** grep docs/audit/ before building an instrument for a question about the
68K program. Three artifacts there answered what I spent the afternoon
re-deriving, and one of them contains the answer to the next step too.
### F0(c): the master cannot host the game ALONGSIDE THE PIPELINE, which is not the question (2026-09-26 18:00 ET)

The builder's rig measurement, and my objection to what it settles.

MEASURED (new instrument RIGRATE=1: game frames and vints counted from boot and
never reset, so the figure CONVERGES -- one band of +-3% across a run instead of
the per-step accumulators' 2-3x swings, and byte 6 reports vints elapsed so a
reader can see it has settled. It separated an 8-point effect on two launches a
side, which the old instrument could not have done).

Load: 10,400 instructions a vint over a 16 KB working set walked one cache line at
a time -- 4x the SH-2's cache, so it tests RESIDENCY not issue rate. Five
instructions an iteration, disassembly-verified first.

    control       rate 147-150   58.2%
    master load   rate 123-132   49.9%

Non-overlapping, 8.3 points, 14% relative. It agrees with the arithmetic: 10,400
instructions at the measured 5.1-8.75 all-miss figure is 13-22% of a vint.

**MY OBJECTION: the control is TODAY'S master, which carries the pipeline the
full port DELETES.** CLAUDE.md puts the pipeline at 2,882 instructions/vint, "as
much as the game itself", and the master owns a share of it -- packet building,
the tile batch, the NT chunk, and the cart reads that feed them. After a port
there is no second CPU's state to fetch and translate. So the honest reading of
this measurement is "the master cannot host the game ALONGSIDE the pipeline",
which is not the configuration the decision is about.

The proxy already exists and has never been used for this: IDLEOFF=1 NOMAPS=1
makes "the master's idle loop nearly empty" -- no MD packets, tile batch and NT
chunk skipped, their cart reads with them (Makefile:2323). Re-run the master load
against THAT control. It is not exact (a ported master still composes and blits)
but it brackets the answer from the right side.

SLAVE: NOT ANSWERED, and the builder is right to refuse the number. More slave
load RAISES the game frame rate monotonically -- 145-150 at zero, 149-160 at 1x,
158-169 at 10x, the 10x band clear of control. The likely mechanism is that
delaying the slave's echo drops compose work and frees the 68K, i.e. the number
is bought by RENDERING LESS. That is the FBXPORT failure exactly, and the rule
from it is to rank on presented motion. Ares is non-monotonic on the same builds.
A picture gate beside the rate is required before another load measurement means
anything.

THE FALSE NULL, the builder's, and the rule is theirs: their first slave
measurement was a clean null and it was WRONG. The hook sat in the slave loop's
CMD_BLIT branch and this line only ever issues CMD_TILE -- every slave_cmd call
passes tile_cmd -- so the load never ran, and the null said exactly what the
intuition wanted to hear. It was caught only because a TENFOLD load also moved
nothing, which no real load can do.

**Rule (the builder's): run the SCALED control before believing a null.** A null
result and a disconnected instrument are indistinguishable from one reading; a
10x load separates them in one more.
### The function count sanity-checked: 720 total, 622 real, and 73 fragments sitting on zero words (2026-09-26 18:15 ET)

Mike: "no one wrote 2,121 assembly functions unless those are ALL jump routines"
-- and 2,121 was indeed the fragmentation artifact of seeding a function at every
address. He accepts 720. The distribution supports it and exposes the residue.

    SIZE DISTRIBUTION of 720 functions
      1-2 B        10   1.4%
      3-6 B        88  12.2%
      7-16 B      138  19.2%
      17-64 B     226  31.4%
      65-256 B    209  29.0%
      257-1K       42   5.8%
      >1K           7   1.0%

    functions > 6 bytes: 622, holding 16,153 of 16,259 in-function instructions
    mean of those: 102 bytes, 25 instructions

**622 independently corroborates PORT-CENSUS's 590 state-touching routines** and
the prior map's 560, from a different direction.

THE TAIL IS DIAGNOSTIC: of the 98 sub-7-byte "functions", **73 start with
`ori.b`** -- and 0x0000 disassembles as `ori.b #0,d0`, so those sit on zero-fill
or table data, not code. They are not functions and must be culled before the
coverage table is generated, or they will read as 73 untranslated routines
forever.

CONTAMINATION MEASURED, and it is low: across all 20,355 decoded instructions,
only 368 are `ori.b` on a 0x0000 word -- 154 runs, ~736 bytes, **0.92% of the
79,676-byte code figure**. So the decoded set is ~99% sound. The one large run,
0x06DCA-0x0702E at 154 words, sits immediately after the dispatch tables
seed_harvest found at 0x06D70 and 0x06DA0, so it is that table's data, decoded as
code because I seeded the region.

Also: 16,259 of 20,355 decoded instructions lie inside a function, so ~4,100 sit
outside any -- fragments and dispatch bodies whose bounds the repair pass
(docs/audit/bound_repairs.md, fix_bounds.py, still unrun here) exists to fix.

**Rule:** sanity-check a derived count against what a human would have written.
2,121 routines for a 1988 arcade game is not a number anyone types; 622 at 102
bytes each is. The implausible count was a tool artifact and the plausible one
still carried 73 fragments.
### The behavioural core is 35 object handlers = 26% of code; the rest is tile/sprite plumbing (2026-09-26 18:40 ET)

Mike's domain claim, and it predicted the structure the data shows: "SO many of
those are jump routines to fill tiles in from the rom, and animate sprites.
that's 90% of what altered beast is even doing. enemy AI boils down to MAYBE 5
enemy types at best, all using different sprites... the enemy routines NEVER
change."

Aggregating docs/audit/function_map.md's class column by TAG (560 functions,
62,385 bytes; a function may carry several tags):

    animation      111 funcs  29,680 B  47.6%
    motion          81        27,482    44.1%
    sound           77        27,036    43.3%
    palette        135        26,118    41.9%
    claim/lock      85        20,162    32.3%
    despawns        73        17,194    27.6%
    draws           54        16,516    26.5%
    collision       33        14,648    23.5%
    state change    73        10,988    17.6%
    hardware       117         7,688    12.3%
    leaf/helper    150         5,063     8.1%

And the named singletons are exactly what he described: unpack_level_tilemap,
tilemap_unpack_hi/lo, tilemap_block_04, tilemap_block_a5, sprite_frame_lookup,
zoom_scale_lookup, sprite_build_depth_banded, sprite_build_and_cull, animate,
animate_variant, animation_peek_next, animation_frame_fetch, integrate_position,
clamp_x_velocity, clamp_y_velocity, hitbox_a/b/c_expand, floor_collide, rng_next.

**THE BEHAVIOURAL CORE ISOLATED.** Functions carrying 5+ behaviour tags at once
are the object handlers:

    35 functions, 16,208 bytes = 26.0% of mapped code
    everything else            46,177 bytes = 74.0%

    0x0E474 1494 B   0 callers   collision+animation+claim/lock+draws+despawns+motion+palette
    0x15434 1422     0           ... + sound
    0x04870 1222     2           ... + state change
    0x13374 1124     0
    0x103E0 1092     0
    0x124BC 1008     0
    0x1650A  972     0
    0x11886  880     0

**ALMOST ALL HAVE ZERO STATIC CALLERS** -- the signature of dispatch-reached
handlers, called through the object dispatcher's `jsr (a0)`. Six or seven at
roughly 1 KB each is exactly "maybe 5 enemy types plus the player".

AND THIS EXPLAINS THE GAPS. 0x0E474, 0x15434, 0x13374, 0x124BC, 0x1650A,
0x11886, 0x15B1E, 0x1211A, 0x0EE80, 0x11CE8 all sit inside the undecoded gaps I
could not classify. The gaps ARE the object handlers, which is why they had ZERO
flow references (dispatched, never called) and ZERO executed PCs in a level-1
trace (different enemies appear in different rounds). Three separate anomalies,
one cause.

**PORT IMPLICATION, and it is the biggest yet for FULL-PORT §3.** The concession
was "correct by construction becomes correct by verification across 25,284
instructions". It is really: verify ~16 KB / 35 object handlers CAREFULLY, and
translate ~46 KB of tile-fill, sprite-animation, palette and sound plumbing
mechanically. The fidelity surface that needs human judgement is a quarter of
what §3 assumed.

CAVEAT, stated: 514 of the 560 classifications are SIGNATURE matches -- a
hypothesis from which fields and callees a function touches -- and only 46 are
READ. So the 26/74 split is a well-founded hypothesis, not a reading.

**Rule:** ask the person who has played the game. Mike's model of Altered Beast
predicted the zero-caller dispatch structure, the handful of ~1 KB handlers and
the plumbing majority before any of it was measured, and it explains three
anomalies that had each been chased separately.

### F0(c) ANSWERED: both SH-2s can host the game, on different terms (2026-09-27)

TWO CORRECTIONS to the 2026-09-26 reading, both from adding the PICTURE GATE:
byte 6 = PRESENTED frames a vint (the master counts THE FS WRITE, the only
thing that changes the displayed framebuffer), converging from boot beside
byte 5's game frames.

**The master result was the PIPELINE, not the load.** The first control was
today's master, which carries the packet building, tile batch, NT chunk and
their cart reads -- 2,882 instructions a vint, "as much as the game itself"
(CLAUDE.md) -- all of which a full port deletes. Re-run against
IDLEOFF=1 NOMAPS=1:

    normal control          game 58.4%   presented 33.5%
    normal + master load    game 50.7%   presented 25.8%   -7.7 BOTH
    pipeline-off control    game 62.4%   presented 67.4%
    pipeline-off + load     game 58.0%   presented 65.4%   -2.0, bands OVERLAP

So the master cannot host the game ALONGSIDE the pipeline, which is not the
configuration the decision is about. **"Whole-game-on-master is out" is
withdrawn.** (NOMAPS renders wrong by construction, so only the within-pair
comparison is valid -- which is the one being made.)

**The slave paradox was a rendering-less artefact, and the gate caught it:**

    control          game 59.1%   presented 33.7%
    SLAVE load 1x    game 59.3%   presented 32.9%   both FLAT, all bands overlap
    SLAVE load 10x   game 63.0%   presented 23.4%   OPPOSITE directions

At 10x, game frames rise 3.9 points while presented frames fall 10.3 -- the
FBXPORT signature, caught in the act. At the REAL load both metrics are flat:
**the slave absorbs 10,400 instructions a vint at no measurable cost to
cadence or to the picture.** The 10x arm doubles as the control proving the
hook fires, so the flat 1x is headroom and not another false null.

**Why this does not contradict SELFCHAIN.** SELFCHAIN filled the slave's idle
with COUPLED work -- chain and compose -- which the master waits on, and it
rejected 10-14%. This load is INDEPENDENT read-only traffic sharing no state
and no sequence with the compose. The idle is not free for work the master
must wait on; it does absorb work the master never waits on.

**Rule:** a cadence number without a presented-frame number beside it cannot
be trusted in either direction. Both of this card's wrong answers -- "the
master is out" and "the slave improves things" -- were single-metric readings.

### F0(c): both SH-2s can host the game; the master answer flipped once the pipeline was removed from the control (2026-09-26 19:15 ET)

THE INSTRUMENT FIRST, because nothing here reads without it: barcode byte 6 now
carries PRESENTED frames per vint -- the master counts the FS write, the only
thing that changes the displayed framebuffer -- converging from boot beside byte
5's game frames. A build that raises one while lowering the other is buying its
number by rendering less.

MASTER, and the first answer was wrong because of its CONTROL:

                            game frames   presented
    normal control              58.4%       33.5%
    normal + load               50.7%       25.8%     -7.7 on both
    pipeline-off control        62.4%       67.4%
    pipeline-off + load         58.0%       65.4%     -2.0, bands OVERLAP

With IDLEOFF=1 NOMAPS=1 the same 10,400-instruction load is nearly free.
"Whole-game-on-the-master is out" is WITHDRAWN: what the first run measured was
the master failing to host the game alongside work a full port DELETES. (NOMAPS
renders wrong by construction, so only the within-pair comparison is valid, which
is the one made.)

SLAVE, and the paradox resolved itself:

                    game frames   presented
    control             59.1%       33.7%
    slave 1x            59.3%       32.9%
    slave 10x           63.0%       23.4%    opposite directions, no overlap

At 10x the metrics diverge -- game frames +3.9, presented -10.3 -- which is the
FBXPORT signature caught in the act and explains why the earlier monotonic rise
was not headroom. At the real load both are flat with every band overlapping, and
the 10x arm doubles as the scaled control proving the hook fires, so the flat
reading is not another false null.

SELFCHAIN RECONCILED: it filled the slave's idle with COUPLED work the master
waits on; this load is independent read-only traffic. The slave's idle is not free
for work the master must WAIT on, but it absorbs work the master never waits on.

THE NEW QUESTION IS DESIGN, NOT CYCLES, and it argues for the MASTER: if the game
runs on the slave, the master must read its output every frame -- sprite lists,
tilemap decisions, palette -- which is a pipeline again, smaller but the same
shape, with the same arbitration and coherence questions. If the game runs on the
master, the decider and the drawer are one CPU on one SDRAM and no cross-CPU read
exists at all. So the measurement that flipped the master arm also removes the
main reason to prefer the slave. The discriminator left is cache residency: the
object table's 128-byte stride, its 4.6-7.8x penalty, and the 144-byte re-stride
only a port can take.

**Rule:** when a load test says a host has no room, check whether the CONTROL
includes work the proposed change removes. The master was charged for the
pipeline the port deletes, and that one substitution reversed the verdict.
### Cross-title code sharing measured: 8.3% structural, 0.6% byte-identical (2026-09-26 19:30 ET)

Mike's framing: 1989 arcade code under time pressure, largely reusing older
assembly routines, so much of it boilerplate. Measured against Golden Axe -- a
different game on the same board, same year:

    byte-identical runs >= 32 B          1,551 B = 0.6%   (largest = the 1,008 B
                                                           vector table)
    game_align structural match          7,689 of 92,681 insns = 8.3%, 531 blocks
    BASELINE, altbeast US vs JP          91,544 of 92,681 = 98.8%, 210 blocks

So there IS a shared house-code layer and 531 matched blocks is a lot of common
idioms -- but it is 8.3%, not the bulk. The titles were re-assembled with
different memory maps (game_goldnaxe.py's MEMMAP differs throughout), which is
why byte matching finds only the vector table.

FOR THE KIT this settles which asset is reusable: not translated OUTPUT, but the
TRANSLATOR and the tooling. FULL-PORT §9 already named that as the fork and this
is the number behind it. 8.3% of a title comes free; the rest needs the same
machinery run again, which is cheap if the machinery is good.

**Rule:** distinguish "the game spends 90% of its time doing X" from "90% of the
code is shared with other titles". The first is true of the tile/sprite plumbing
and drives the port's risk profile (26/74 behavioural/plumbing split); the second
is false at 8.3% and drives the kit's.
### The port's language is C, and 415 of 623 functions already compile for SH-2 (2026-09-26 21:30 ET)

Measured, not chosen. `tools/cport_prep.py` + `sh-elf-gcc -m2 -ffreestanding`:
**415 of 623 decompiled functions compile for the target CPU today (66.6%).**

THE ENABLING FACT: the 32X SH-2 build is elf32-sh / -m2 = BIG-ENDIAN, the same as
the System 16 68000. Struct layout, byte order and word packing carry over with no
rewrite. Without that, decompiled C would not be portable at all.

Also: sh_src/ is already C on marsdev's sh-elf-gcc, so translated functions drop
into the existing build with no new toolchain, and F0's budget (3.1% of a vint
cache-resident, 23.7% all-miss) affords gcc output even at 2x hand-assembly cost.
So F1.3's emitter -- 58 mnemonic forms or P-code with dead-flag elimination -- is
NOT NEEDED for the 525 plumbing functions. Hand assembly stays available for
whatever measurement later shows needs it.

Generated, nothing hand-written: types, the CONCAT/SUB/ZEXT/SEXT intrinsics (plain
shifts, both sides big-endian), 614 forward declarations, and declarations for all
499 absolute globals -- 83 program ROM, 27 S16 hardware, 362 68K WORK RAM (F0(a)'s
15.6 KB of ported state), 27 other. Whole-corpus errors fell 1,701 -> 589.

**THE REMAINING 208 FAILURES HAVE ONE UPSTREAM CAUSE.** 45 distinct pseudo-
registers, lopsided: unaff_A6 alone has 9,821 references, then unaff_D3w 771,
unaff_D6w 462, in_D0b 297, extraout_A1 281. Ghidra has NO CALLING CONVENTION
declared for this program, so a register the caller sets and the callee reads
becomes `unaff_`, and a result returned in D0 becomes a void function whose value
is used -- which is the residual profile exactly (211 "invalid use of void
expression"). The fix is one declaration in Ghidra, not postprocessing, and the
convention is already written down: collision_sh2.s fixes r14 = state base and
r4 = a4 = current object slot. A6 is UNAFFECTED -- passed through, never modified
-- so in a single-threaded game loop it is semantically a global cur_obj pointer.

**Rule:** when postprocessing errors cluster on one symbol (9,821 of them on
unaff_A6), the fix is upstream of the postprocessor. I wrote three header
generators before noticing that one calling-convention declaration outranks all of
them.
### The recorded playthrough is the trace driver: 2,055 -> 6,930 PCs, 101 -> 244 proven functions (2026-09-27)

The trace campaign saturated because it was driven by a level-1 BOT inside a
single window. Two changes, both in tools/arcade_trace_multi.lua:

**Inputs come from Mike's recorded full playthrough** (inp/ab_full, 103,300
frames, all five rounds, every boss and transformation) through MAME's
-playback, not from a bot that cannot leave level 1.

**One pass takes MANY windows.** Reaching frame 100,000 is paid once instead of
once per window, and the whole playthrough replays at ~1300% speed, so 120
windows cost about two minutes.

    2,055 -> 6,930 distinct PCs
    101 -> 244 of 623 functions with a proven-executed instruction (66% BY BYTES)
    315 executed PCs Ghidra does not decode  -> docs/proof/f1_seeds_trace.json

AND THE OLD FAILURE HAS A CAUSE: the late windows (AT_START 7100/9000/12000)
produced no trace because -bench 160 ends the run at frame 9600. A window past
the -bench limit never arrives, silently. Size -bench above the last window.

Diminishing returns are informative: round 2-5 windows added almost nothing
(rounds share code), while GAMEPLAY VARIETY added a great deal. Sample densely
across the run rather than aiming at rounds.

### The L1 differential harness runs on MAME's debugger, not on lua breakpoints (2026-09-27)

A lua-driven breakpoint loop -- bpset, poll execution_state, read registers,
set it back to "run" -- SEGFAULTED MAME (rc 139). The supported path is the
debugger's own action strings, and all three needed mechanisms work:

    trace <f>,maincpu,,{tracelog "R %08X...",d0,...}   registers per instruction
    wpset ff0000,10000,w,1,{tracelog "W %08X %08X",wpaddr,wpdata; g}   every write
    bpset <site>,1,{tracelog "...",a0,a6,w@(a6),...; g}    state at a call

**MAME NAMES A7 "SP".** Writing `a7` in an expression silently parses as the
HEX CONSTANT 0xA7, and since the call detection keys on the stack pointer, the
whole extraction returned zero calls with no error. The register list is
A0-A6, SP, D0-D7, PC, SR.

Result: tools/l1_oracle.lua + tools/l1_vectors.py, 30 windows over the
playthrough -> 9,126 calls, 182 distinct callees, 590 reference vectors
(docs/proof/l1_vectors.jsonl). Each is entry registers, exit registers and the
exact WRAM the call wrote -- what a translated routine must reproduce.

Calls are detected FROM THE STACK POINTER, not from the disassembly text, so
`jsr (a0)` dispatches are caught like any other -- which is what makes the
object-handler work possible.

### The object dispatch that reaches the handlers is DISPATCHERS[0], and 58 handlers are round-specific (2026-09-27)

I nearly filed this as a correction to game_altbeast.py and it would have been
WRONG. The handoff described DISPATCHERS as holding 0x03CEA and 0x03D20; the
FILE holds 0x39A8 (`movea.l (2,A6),A0 ; jsr (A0)`) and 0xD842. I observed every
dispatch coming from 0x039AC, which is that first entry's `jsr (A0)`, four
bytes into it -- so the file was right and the description of it was not.
**Read the table before correcting it.**

Measured over the playthrough: of the eight largest handlers, seven were
observed being called and every call came from 0x039AC (455 calls, 57 distinct
targets). Calls from within 0x40 of 0x03CEA/0x03D20 reached only three small
helpers (0x03C84/0x03CA6/0x03CD4). **DISPATCHERS[1] (0xD842) fired zero times**
in 30 windows across the whole playthrough -- it is real but rare, and naming
its targets needs a capture aimed at it.

1,613 dispatches, **83 distinct handlers** -- more than the 35 expected -- and
**58 seen in exactly one round**, which is the naming lever: a handler that
runs only in round 2 is a round-2 enemy. docs/proof/obj_handlers.md.

The object record's layout fell out of the same capture: +0 status, +2 the
handler pointer as a long, +6 type, +10 sprite attribute -- and A6 walks the
table in 0x80 steps, so PORT-CENSUS's 128-byte stride is visible in the raw
addresses.

### The oracle says 83 object handlers, not 35; and my DISPATCHERS citation was Golden Axe's (2026-09-26 22:00 ET)

The builder's three tasks, delivered, with two corrections against me.

**1. THE TRACE CAMPAIGN, and the right driver was Mike's own recording.**
`inp/ab_full` is a MAME `.inp` of the arcade -- replayed with `-playback`, it
drives many windows in one pass at ~1300%, so 120 windows cost two minutes.

    distinct PCs                     2,055 -> 6,930
    functions with a proven instr      101 ->   244
    by bytes                             -- ->   66%
    executed PCs Ghidra missed                  315  (docs/proof/f1_seeds_trace.json)

TWO THINGS I HAD WRONG. My dead late windows had a mechanical cause: **a window
past the `-bench` limit never fires, silently** -- bench 160 ends the run at
frame 9600, so AT_START 12000 never arrived. And **rounds 2-5 added almost
nothing because the rounds SHARE CODE**; the gains came from gameplay VARIETY.
So sample densely, do not aim at rounds -- which is the opposite of the advice I
put in the builder's task list.

**2. THE L1 HARNESS EXISTS: 9,126 calls, 182 distinct callees, 590 reference
vectors** (entry registers, exit registers, and the exact WRAM the call wrote) in
docs/proof/l1_vectors.jsonl. Calls are detected from the STACK POINTER rather
than the disassembly, so `jsr (a0)` dispatches are caught like any other call.
Their traps: a lua-driven breakpoint loop segfaults MAME (use the debugger's own
trace/watchpoint/breakpoint actions), and **MAME names A7 "SP", so `a7` in an
expression parses as the hex constant 0xA7** and yields zero calls with no error.

**3. OBJECT HANDLERS: 1,613 dispatches, 83 DISTINCT HANDLERS -- not the 35 I
estimated -- and 58 appear in exactly ONE round**, which is the lever a name
reads off. My 35 came from the map's tag classification (5+ behaviour tags); the
oracle counts what is actually dispatched. Both numbers are real and they measure
different things, but **83 is the one the port must translate faithfully.**

THE OBJECT RECORD, read off the same capture and now generated into
`obj_record.h` by tools/cport_prep.py:

    +0x00  status/flags (observed 0x88xx / 0x89xx)
    +0x02  handler pointer, long -- the target of jsr (A0)
    +0x06  type
    +0x0A  sprite attribute
    stride 0x80, visible in the raw addresses -- the 128-byte stride
           PORT-CENSUS flags as cache-hostile

**MY ERROR, caught before it cost a build:** I told the builder the DISPATCHERS
were 0x03CEA and 0x03D20. Those are **GOLDEN AXE's** (tools/game_goldnaxe.py).
Altered Beast's are **0x39A8** (`movea.l (2,A6),A0 ; jsr (A0)`) and 0xD842, and
every observed dispatch came from **0x039AC**, the first entry's jsr four bytes
in. I took the numbers from a FACTS.md grep hit without checking which file the
citation pointed at. The table was right; my description of it was not.
Separately, DISPATCHERS[1] at 0xD842 fired ZERO times in 30 windows across the
whole playthrough -- real but rare, and naming its targets needs a capture aimed
at it.

**Rule:** FACTS.md lines carry their source `file:line`. Read it. A grep for
DISPATCHERS matched the Golden Axe table and I quoted it as Altered Beast's.
### 504 of 623 functions compile for SH-2 (80.9%), and the biggest win was one type fix (2026-09-26 22:30 ET)

Progression, each step generated by tools/cport_prep.py, never hand-edited:

    types + intrinsics + 614 protos + 499 globals          415   66.6%
    + halt_* intrinsics, xRam globals, void->uint retyping  477   76.6%
    + bare return; -> return 0;, func_0x declaration        504   80.9%

**The dominant fix was retyping void functions whose result is used.** 302 of the
589 whole-corpus errors were "invalid use of void expression", all from one cause:
the 68K returns in D0 and Ghidra did not model it. A non-void function with no
return statement is legal C and closer to the hardware truth than void.

REMAINING 119, all bounded: 91 more void-result forms the regex misses, 49
assignment-to-byte (a global read at several widths needs its widest declaration),
31 CROSS-FUNCTION `goto LAB_xxxx` -- the only class needing a human, and named in
failing.txt -- and 9 unresolved call targets.

AND A NEGATIVE WORTH KEEPING: declaring the 68K calling convention IN GHIDRA
(A6/A0/D0 as custom-storage parameters plus a forced D0 return on 451 functions)
dropped the compile rate 415 -> 253 and raised errors 589 -> 812. It half-worked
-- pseudo-registers fell 45 -> 29 -- but forcing a return on functions that return
nothing, and adding parameters call sites do not pass, costs more than it buys.

**AND THE REVERT DID NOT RESTORE THE ORIGINAL STATE**: reverting to
DYNAMIC_STORAGE left signatures altered and the baseline re-measured 303, not 415.
Hence tools/ghidra/f1_rebuild.sh. In-place edits to an analysed Ghidra project are
not reliably reversible; re-importing is.

**Rule:** before experimenting on an analysed Ghidra project, make the rebuild
reproducible. I spent two cycles establishing that my revert had not reverted, and
the measurement that looked like a regression in the new headers was a regression
in the project state.
### SHAPE D: routine pointers installed by `lea pc@(H),An ; move.l An,(d,A6)` -- 108 targets the harvester never saw (2026-09-26 23:00 ET)

Chasing what looked like a shared tail found a THIRD install idiom and 89
functions of new code.

THE CHASE: 14 functions failed to compile on cross-function `goto LAB_xxxx`, and
seven jumped into the SAME label set 0x228C..0x2490. Those addresses were in NO
FUNCTION and their only references were DATA, from 0x1FCC-0x21C2. Reading the
site settled it:

    2110: move.w #-32768,(0,A6)    ; object +0  = flags 0x8000
    2116: lea     (0x228c,PC),A0   ; A0 = the handler
    211a: move.l  A0,(2,A6)        ; object +2  = HANDLER POINTER
    211e: move.w  #0x01DD,(6,A6)   ; object +6  = type

Not a tail at all -- an OBJECT HANDLER BEING INSTALLED, into exactly the record
layout the oracle capture reports (docs/proof/obj_handlers.md: +0 flags, +2
handler, +6 type, +10 sprite, stride 0x80). seed_harvest.py knows a pc-relative
table of longs and `move.l #ADDR,(d,An)` with a literal; it does not know the
address being formed by a pc-relative `lea` first.

MEASURED over the program (tools/shape_d_seeds.py, new):

    118 install sites, 108 distinct targets
      +0x24   103 sites   <- a SECOND per-object routine pointer, unremarked
      +0x02    13 sites   <- the dispatch handler the object loop calls
      +0x6C     2 sites

**103 of the 118 install at +0x24, not at the handler slot.** That is a second
routine pointer per object record that nothing in the record had named.

RESULT of seeding them: 89 newly disassembled, 104 functions created.

    functions      623 ->   680
    instructions 20,956 -> 21,407
    low-region code 62.7% -> 64.0%  (82,228 -> 83,840 B)
    COMPILING FOR SH-2  504/623 (80.9%) -> 560/680 (82.4%)

**And the cross-function goto class is GONE** -- 31 errors to zero -- because
those labels are function entries now. The residual is three mechanical classes:
91 void-expression forms, 83 assignment-to-byte (a global assigned a pointer
needs a wider declaration), 9 Ghidra adjust intrinsics, 9 unresolved calls.

Shape D is now in tools/ghidra/f1_rebuild.sh so it is part of the reproducible
pipeline rather than a one-off.

**Rule:** a compile error can be a decompilation finding. "Cross-function goto"
was not a code-quality problem to paper over -- it was 108 unnamed routine
pointers and 89 functions of undiscovered code, and the error was the only thing
pointing at them.
### Half the functions are DERIVABLE, so compile rate is the wrong headline (2026-09-26 23:45 ET)

Mike: "Half of the functions we can just derive. we understand the hardware
platform it's written to intimately." Measured against docs/audit/function_map.md:

    NAMED behaviours (name + hw spec)   35 fns   4,600 B
    hardware glue (S16 memory map)     117 fns   7,688 B
    leaf/helper (small, read it)       150 fns   5,063 B
    DERIVABLE                          302 of 560 = 54% of FUNCTIONS
                                    17,351 of 62,385 = 28% of BYTES

He is right by count and the byte figure explains why: the derivable ones are the
small ones. THREE DIFFERENT 100%s were being conflated:

  A COMPILE RATE, 578/680 = 85.0% -- how much of Ghidra's literal transcription
    survives gcc. NOT the goal: ~15% of the residue is transcription noise on
    functions we would hand-write anyway.
  B CODE/DATA CLASSIFICATION, 64.0% code with 45,577 B undecoded -- and "100%" is
    the wrong word, because some of the band IS data (59% of big gaps are
    data-referenced with zero flow references).
  C FUNCTIONS WITH A CORRECT VERIFIED SH-2 IMPLEMENTATION -- the finish line, and
    the STATUS column of docs/proof/coverage.md. Cavern Crawl's definition is the
    one to copy: PORTED = behaviour reconstructed, "matched by cited FUN_ address
    OR CLEAR BEHAVIOURAL EQUIVALENCE". Reconstructed, not transcribed.

So the route splits: 302 functions DERIVED from the spec and verified against the
L1 vectors, 258 TRANSLATED from the decompiled C and verified. The derivable half
is where the port gets BETTER than a transcription -- integrate_position is 4 bytes
of 68K that becomes one inlinable line, and the 128-byte object stride can become
144 in the same pass for the measured 4.6-7.8x cache win only a port can take.

**Rule:** when a metric is easy to measure it becomes the headline whether or not
it is the goal. I pushed compile rate 415 -> 578 over six iterations while the
actual finish line -- functions verified correct -- had moved not at all.
### The port is the GAME LOOP: 27% of the 68K is display code we already replaced (2026-09-27 00:15 ET)

Mike: "we are looking for the game loop. So much of the other instructions are
assembly for fetching and displaying sprites and tiles to race the beam. So we
already have our own code to talk to the VDP. So much of the 68K code is for
reference."

FULL-PORT §4 said the port MOVES all 25,284 reached instructions. That was the
document's largest over-statement. Measured against function_map.md:

    MIXED logic + display              161 fns  36,414 B  58.4%
    display only (we already do it)     79       9,216    14.8%
    hardware glue (our own VDP path)   117       7,688    12.3%
    leaf/helper                        150       5,063     8.1%
    GAME LOGIC only                     37       2,434     3.9%

Pure display + hardware glue = 27% of bytes, DELETED from scope: we have MDBGALL
for the tile planes, TILESMD/CAT1MD for the MD path, MDSPR/SPRLIST for sprites,
TEXTCAP* for text, the V-ISR discipline for the beam. The 68K's version is the
SPEC WE ALREADY IMPLEMENTED AGAINST.

But 58.4% of bytes DO BOTH -- the handlers decide and poke video in one routine --
so the separation is not clean per function. **WHICH IS WHY THE PATCHER'S TABLES
ARE THE REWIRING MAP.** game_altbeast.py already enumerates, hand-derived and
byte-verified, every video touch: PAL_DIRTY_SITES 42, TILE_DIRTY_SITES 25,
TEXT_IDIOM 8, HARVESTED_HANDLERS 43, FMGATE_ENTRIES/SPANS 34/18, 249 entries over
38 tables. patch_game.py already does this substitution at machine-code level
(S16 video addresses -> 32X); the port does it one level up in C.

**You rewire ~75 video sites, not 161 functions.**

AND A DISCREPANCY WORTH CHASING: HARVESTED_HANDLERS holds 43; the oracle observed
83 distinct handlers. Either the patcher's list is a deliberate subset or 40
handlers are unpatched, and those are functions the port must carry.

**Rule:** ask what the port must REPRODUCE, not what the original CONTAINS. I
costed a translation of the whole program for a day before Mike pointed out that a
quarter of it is a renderer we already wrote, and more than half of the rest only
needs its video calls redirected to it.
### The L1 vector set widened to 269 callees, and an interrupt is not a call (2026-09-27)

150 windows x 2 frames over the recorded playthrough: 47,122 calls, **269
distinct callees, 943 vectors** (from 182 / 590). 191 of the export's functions
have a vector at their entry, 40% by bytes.

TWO THINGS THE FIRST EXTRACTOR MISSED, both structural:

**An exception pushes SIX bytes, not four.** A jsr-only rule cannot see the
game's per-frame driver at all, because IRQ4 is entered by interrupt. Detecting
a 6-byte push as well puts it in the set.

**And the IRQ lands on the THUNK at 0x000404, not on the handler body at
0x2AAC**, which is reached by a jump. A jump-entered body has no entry/exit
pair of its own; it inherits its caller's vector. That is worth knowing before
anyone reports 0x2AAC as unprovable.

### The second dispatcher is a scroll-triggered spawn, and 47 of them exist in the whole game (2026-09-27)

DISPATCHERS[1] (0xD842) fired ZERO times in 360 sampled single frames, which
looked like dead code and was not. `FUN_0000D80E` indexes a per-round table at
0x1D32A by the round at 0xFFF142 and calls the initialiser at entry+8 only when
the camera at 0xFFF132 passes the entry's trigger.

**The whole game holds 47 spawn entries.** A one-frame window has ~0.05% chance
of landing on one. Re-sampled at 30 frames a window it fired twice, and those
hits confirmed the layout against the ROM, after which every target enumerates
statically: six initialisers in 0x0DA0C-0x0DA6A, reachable ONLY through this
table (docs/proof/spawn_table.md).

**Rule:** before calling a never-observed path dead, compute how often the
sampling could have seen it. Zero hits at 0.35% coverage of a 47-event
population is the expected result, not a finding.

### Two tools disagreed on the same captures, and the filename was the reason (2026-09-27)

Merging capture directories renamed the logs `od2_o000700`, and one analyser
took the frame number by stripping non-digits from the WHOLE stem -- folding
the '2' of 'od2' into the frame and reporting 44 round-exclusive handlers where
the other reported 60. Caught only because two paths computed the same quantity
and disagreed. Parse the field, not the string.

### 100% coverage is a COMPLETENESS PROOF for the port's scope, and the ledger says where to look (2026-09-27 00:45 ET)

Mike: "We still have a NEED to identify 100% of the decompiled code coverage, but
as a way to ensure we only have what we need to port the game logic." That is a
different and better reason than the one I was working to: full classification is
not a porting target, it is how the §4 scope reduction becomes trustworthy. The
argument is negative -- "we have all the game logic" is only a claim once nothing
is unaccounted for.

THE LEDGER, code band 0x400-0x1F000 = 125,952 B:

    decoded CODE                83,840   66.6%
    data (defined + referenced)  1,842    1.4%
    UNCLASSIFIED                40,270   32.0%   in 520 gaps

    by gap size:  1-8 B   97 gaps    321 B   (alignment)
                  9-32   112       2,081
                 33-128  251      18,515   <- 46% of the unknown
                129-256   37       6,173
                257-1024  21       8,592
               1025+       2       4,588

251 gaps of 33-128 B hold 46% of it and the mean real function is 102 B, so that
is FUNCTION-shaped. The reference pass agrees from the other side: over the big
gaps, 19,112 B data-referenced, 5,802 B unreferenced, and CODE/MIXED still ZERO --
no gap anywhere has a flow reference into it. Nothing CALLS them; they are reached
by a pointer, like every discovery in this file.

TWO IDIOM HUNTS: broadening shape D to any destination register was marginal (122
targets vs 108; destinations are only A6 x112 and A5 x10, so A6 was the story) and
scanning for TABLES OF ABSOLUTE LONGS FRAGMENTED the call graph -- 227 targets gave
25 new instructions but took functions 687 -> 859, with 257-1K functions falling
42 -> 30 and 17-64 B rising 226 -> 329. Reverted by rebuild.

Its two dominant false positives are identifiable: 0x00004 with 251 entries is the
68K VECTOR TABLE (all targets 0x400-0x40C) and 0x06DCA with 154 entries is the
zero-word data beside the dispatch tables at 0x06D70/0x06DA0. Filter tables whose
targets span under ~0x20 and whose base sits in known data, then retry the other 12.

**Rule:** a seeding scan must be judged on the FUNCTION SIZE DISTRIBUTION, not the
instruction count. 25 new instructions looked like a win while 12 large functions
were being split into 103 small ones.
### Pointer tables have STRIDES, not consecutive longs -- and my table filter judged the wrong end (2026-09-27 01:30 ET)

The builder's spawn-table finding exposed both errors at once.

THEIR FINDING (docs/proof/spawn_table.md): DISPATCHERS[1] at 0xD842 is not dead.
`FUN_0000D80E` reads the round from 0xFFF142, indexes a pointer table at
**0x1D32A** by round x 4, walks that round's list of **12-BYTE ENTRIES**, and for
each entry whose trigger the camera (0xFFF132) has passed calls the initialiser
held as a long at **entry+8**. 47 spawn entries in the whole game; six
initialisers in 0x0DA0C-0x0DA6A reachable ONLY through this table. Round 3's list
is EMPTY, which is worth an eye.

MY FIRST ERROR: my filtered long-table scan REJECTED that region -- "base 0x1D2EA
is not decoded code (class 3)". **A data table of code pointers is the most common
kind of jump table in this program, and its base being data is EXPECTED.** The
filter must judge the TARGETS, not the base. I had inverted the test.

MY SECOND ERROR, and the bigger one: the scan looked for runs of CONSECUTIVE
longs. **The spawn table's pointer is every 12 bytes at offset +8.** A
consecutive-longs scan cannot see it, and could not have seen the cycler's 8-byte
slot entries either. Generalising to a STRIDED scan -- >=4 entries at a fixed
stride whose pointer field lands in the code band, strides 4..32, all offsets:

    183 strided pointer tables, 347 distinct code targets, 76 not yet decoded
      0x06DD2 stride 24  26 entries      0x1D9FE stride  8  15 entries
      0x25600 stride 24  25 entries      0x1D9AE stride 16  14 entries
      0x06DCA stride 32  20 entries      0x17D8A stride  6   8 entries

RESULT, judged on the FUNCTION SIZE DISTRIBUTION as the rule now requires:

    71 newly disassembled, functions 682 -> 734, code 64.0% -> 64.9% (85,016 B)
    LARGE FUNCTIONS PRESERVED: 257-1K = 43 (orig 42, fragmented run 30)
                               >1K     =  7 (orig  7, fragmented run  6)

So this scan added real code without the fragmentation the unfiltered version
caused. That is the difference a stride makes over a consecutive run.

### The builder's L1 harness at 943 vectors, and two structural misses worth knowing (2026-09-27 01:30 ET)

150 windows over Mike's recording: **47,122 calls, 269 distinct callees, 943
vectors** (was 182 and 590). 191 functions have a vector at their entry, 40% by
bytes. Two structural fixes they found, both of which change how coverage reads:

  - **A 68K EXCEPTION PUSHES SIX BYTES, NOT FOUR.** A jsr-only call detector
    cannot see IRQ4 at all -- which means the game's PER-FRAME DRIVER was
    invisible to the harness until this was fixed.
  - **The IRQ lands on the thunk at 0x000404, not on the handler body at
    0x2AAC.** That body is JUMP-ENTERED and inherits its caller's vector, so an
    absent vector at 0x2AAC is not evidence that it is unprovable.

HANDLERS: 14,078 dispatches, **112 distinct handlers** (was 83), 60 seen in
exactly one round. **Slot 0 is the player, nine handlers run on it, and one is
0x02490** -- inside the shared tail whose bounds I am fixing, so that work touches
the player's state machine directly. And 0x049BA shows **145 distinct sprite ids
in one routine**, which is Mike's "same routine, different sprites" measured.

TWO RULES FROM THEM, both general:

  - **Before calling an unobserved path dead, compute whether the sampling could
    have seen it.** 47 spawn entries over a whole game gave a one-frame window
    ~0.05% odds; 360 single frames finding zero was the EXPECTED result, not
    evidence. Their earlier "fired zero times in 30 windows" was a sampling
    artefact, not a fact about the game.
  - **Two independent paths computing the same quantity is how a parse bug gets
    caught.** Their round-exclusivity analysers disagreed 44 against 60; the cause
    was a filename (`od2_o000700`) whose non-digit strip folded the `2` into the
    frame number. Published figure is 60.
### THE DECOMPILED GAME COMPILES FOR SH-2: 734 functions, 132,220 bytes, zero errors (2026-09-26 23:35 ET)

`tools/cport_build.sh` -> port.o, elf32-sh, architecture sh2, .text 0x2047C =
132,220 bytes, 734 T symbols.

**132,220 SH-2 bytes from 85,016 68K bytes = 1.56x**, inside the hand
translation's predicted 1.08-1.18x idiomatic / 1.84x literal (collision_sh2.s).
Independent corroboration of that ratio by a different route.

ERROR COUNT 721 -> 0, every fix GENERATED and none hand-edited:

    721 start  -> 246  downgrade GCC 15's pointer/integer conversions
               -> 88   typedef bool/true/false (freestanding C99 has none)
               -> 72   extras.h: declare what gcc calls undeclared, by feedback
               -> 34   :: before normalisation; BADSPACEBASE as typedef;
                       dereferenced DATs as pointers; X._2_2_ -> a cast
               -> 15   sanitise symbols carrying string content
               -> 5    subscripted function names via byte pointer; (code)->(byte)
               -> 2    `code *` locals subscripted rather than called, retyped
               -> 0

**TWO DIALECT FACTS DID MOST OF THE DAMAGE AND NEITHER IS A BUG IN THE CODE.**
GCC 15.2 promotes 68K-normal pointer/integer punning to errors -- and
`_DAT_fffff0ec = &DAT_00410e80` is literally what the 68K does -- so two
-Wno-error flags removed 475. And C23 makes `f()` mean `f(void)`, turning
Ghidra's inconsistent arities into 57 "too many arguments"; -std=gnu99 restores
the unprototyped meaning, which is what an un-analysed 68K function IS.

THE STRUCTURAL DECISION: every function is `uint NAME()`. Ghidra has no calling
convention here, so its parameter lists are invention -- the bodies read inputs
from unaff_A6 / in_D0b globals. A uniform unprototyped declaration is both what
compiles and what is honest; real signatures get declared per function against
the L1 vectors.

CORROBORATION: the naming pass produced `irq4_handler` at 0x00404 (4 B) AND at
0x02AAC (784 B). That is exactly the builder's harness finding -- the IRQ lands on
the thunk at 0x000404, not the jump-entered body at 0x2AAC. Two routes, one
structure.

WHAT IT IS NOT: not correct, not meant to run, nothing verified against a vector
yet, and per Mike's reframing 54% of these will be REPLACED by spec-derived code.
Compiling is the floor that makes per-function verification possible, not the goal.

**Rule:** when a generated corpus will not compile, separate DIALECT from DEFECT
before fixing anything. Two compiler-dialect flags removed 532 of 721 errors here,
and I had started hand-classifying the C instead.
### COVERAGE CLOSED: the gap-edge heuristic, 42 of 42, and code accounted for to 280 bytes (2026-09-27 00:10 ET)

THE HEURISTIC: classify each undecoded gap by the instruction immediately BEFORE
it. A gap after rts/rte/jmp/bra is a CODE BOUNDARY -- the previous function ended
and something unreached begins. A gap starting MID-FLOW, after an ordinary
instruction, is what an embedded table looks like: the disassembler walked up to
it and stopped.

First pass was 100% predictive: **42 of 42 terminator-adjacent gaps decoded**, no
fragmentation (large-function counts held at 43 and 7). Iterated to a fixpoint it
converged at 2 gaps / 280 B that will not decode.

FINAL LEDGER, code band 0x400-0x1F000 = 125,952 B:

    decoded CODE          86,246   68.5%
    data (defined + ref)   1,797    1.4%
    unclassified          37,909   30.1%
      of which, by gap edge:  2 gaps /    280 B  after a terminator (code?)
                            367 gaps / 39,306 B  MID-FLOW (embedded data)

And independently: over the big gaps CODE/MIXED remains ZERO -- not one gap
anywhere has a flow reference into it.

**So the CODE is closed to within 280 bytes across 2 gaps**, which is the
completeness proof the scope reduction needed. Caveat kept: mid-flow position is
strong evidence, not proof; 367 gaps all being mis-decoded function heads is
implausible but the audit trail is the 2 resisters and the mid-flow population.

PROGRESSION TONIGHT, each step a different IDIOM and none brute force:

    functions   623 -> 680 -> 687 -> 734 -> 771
    code band  60.8% -> 64.0% -> 64.9% -> 65.8% -> 68.5%

    the builder's 315 trace-proven PCs                +593 instructions
    shape D (lea pc-rel then store into a record)      +89 functions
    strided pointer tables, strides 4-32, all offsets  +71 instructions
    the gap-edge heuristic iterated                    +42 functions, 42/42

**Rule:** where a gap BEGINS classifies it. Four idiom hunts each found real code
by asking how it is REACHED; the gap edge found the rest by asking what it
FOLLOWS, and cost one script.
### The corpus LINKS, and its symbol span is a fourth confirmation of F0(a)'s 15.6 KB (2026-09-27 11:15 ET)

cport_prep now emits defs.c (one definition per extern), so the decompiled game
links into verify.elf -- elf32-sh, sh2, 181,012 B, .text 0x210F8 = 135,416 B.

**The only unresolved symbols were four libgcc helpers** -- __sdivsi3,
__ashlsi3_r0, __ashiftrt_r4_7/8 -- which SH-2 needs because it has no barrel
shifter and no divide. Nothing in the game itself was missing.

FOURTH CONFIRMATION OF THE STATE BUDGET. Classifying the 588 absolute symbols:
401 are WRAM, 68K span 0xFFC000-0xFFFF82 = **16,258 bytes**, against F0(a)'s
15,974:

    vint_census.py reachability                14.8 KB
    the builder's WRAMCODE exactness test      ~16   KB
    FULL-PORT F0(a)'s working figure           15,974 B
    the decompiled corpus's symbol span        16,258 B

Four independent routes, 1.8% spread. The 52 KB tilemap shadow the port frees
covers it three times over.

WHAT BLOCKS A VECTOR RUN: the linker places WRAM symbols wherever it likes, so
pointer arithmetic CROSSING symbols (`*(undefined2 *)(&DAT_x + 2)`) does not reach
the next one. They must be placed at their true 68K offsets -- which is the layout
the port needs anyway. Plus two small things: the SH ABI prefixes C symbols with
`_` so the entry is `__start`, and qemu's r2d board boots to its own reset vector
rather than the ELF entry.

**Nothing is verified against a vector yet. The image links and is structurally
complete; correctness work has not started.**

**Rule:** a link failure lists exactly what a translation unit assumes about the
world. Four libgcc helpers was the entire external surface of a 771-function game,
which is a stronger completeness signal than any coverage percentage.
### The corpus links at TRUE 68K addresses; the execution route is the open piece (2026-09-27 11:25 ET)

tools/cport_layout.py places every absolute symbol at its real address, which was
the structural blocker: the linker had scattered work-RAM symbols, so any access
CROSSING one -- `*(undefined2 *)(&DAT_x + 2)`, which the game does constantly --
landed nowhere.

    wram  401 syms  68K 0xFFC000-0xFFFF82 = 16,259 B, one contiguous block
    rom   101 syms  the REAL prog68k.bin linked in, so constant reads work
    mmio   56 syms  zeroed stubs

VERIFIED BY THE PLACEMENT: __DAT_fffff014 links at 0x0C043014, and
0xFFF014 - 0xFFC000 = 0x3014 against a 0x0C040000 base. Exact. verify.elf is
442,992 B with .romimg carrying the real 256 KB program.

EXECUTION IS NOT SOLVED and I stopped rather than sink more time in. qemu-system-sh4
-M r2d accepts the image and -device loader sets pc=0x0C010000 correctly, but the
CPU does not execute; -kernel does not load a bare ELF at all. That is board
bring-up, and qemu is an SH-4 SYSTEM emulator being asked to be an SH-2 unit-test
harness.

**THE BETTER ROUTE IS THE PROJECT'S OWN INSTRUMENT.** ares-headless already runs 32X
roms exactly and is what this project trusts for SH-2 exactness; state_health.py and
ares_diag_at.py already read SH-2 state from savestates. A test rom that calls one
ported function with a vector's entry state and dumps the result is the same shape
as every probe here -- and it tests the function ON THE TARGET, so a pass means
something about the 32X rather than about qemu.

**Rule:** prefer the instrument the project already characterised. I reached for a
third emulator whose fidelity nobody here has measured, when the exactness rig was
already in the repo.
### I invented a capability boundary between the threads, and it idled the builder for a day (2026-09-27 11:40 ET)

I told Mike: *"The division was never decompile-vs-build; it's what needs
hardware. I can't push to the rig, build a rom, or run ares -- those are hard
boundaries."* **Every part of that is false and I never checked.**

    rule forbidding it        NONE in CLAUDE.md, LOOP-PROTOCOL.md, MESSAGES.md
                              or either decompile handoff
    ares-headless             present and executable
    tools/mister_push.sh      present and executable
    mame                      on PATH -- I USED IT tonight for the arcade traces
    make line                 expands and runs

So I asserted a negative about my OWN capabilities without the grep, which is the
rule CLAUDE.md states outright and which I spent this session recording other
people's versions of.

**THE COST IS MEASURABLE.** The last 14 commits were all port work and not one
touched the shipping renderer, while `ring1` carried the one visible defect Mike
named -- the level-1 black tiles -- with its mechanism fully identified since
2026-09-25 (slot-pressure refusal at m_main.c:17700; gameplay runs 1018/1024 slots
and 123/128 hash sets full against attract's 499 and 0) and ZERO fix attempts.
The builder went idle not because there was nothing for them, but because I
stopped generating asks that needed a machine -- having decided, wrongly, that
machine work was theirs by definition.

**THE REAL DIVISION IS CONCURRENCY AND CONTEXT, NOT PERMISSION.** Either thread
can do anything. Two threads exist for parallelism and for separate context
windows. So the question to ask when splitting work is "who already has the
conventions in context, and what can run at the same time" -- never "who is
allowed".

**Rule:** check your own capabilities before declaring a boundary. An invented
constraint about yourself is harder to catch than one about the code, because
nothing fails -- the work simply does not get handed over.
### ares does NOT reproduce Mike's level-1 black tiles: 40 clean gameplay frames (2026-09-27 12:00 ET)

First fix attempt at the defect, and the measurement says the INSTRUMENT is wrong,
not that the fix works.

THE FIX (C1REFUSEBG=1, commit 2b44b054) completes the diagnosis the 09-25 work
stopped at. The mechanism was known -- a cat-1 cell refused a VRAM slot when its
hash set is full of hot ways (MDA(4), vage<12) -- but nobody had asked why that
yields BLACK rather than a merely missing FG tile. Reading both sites answers it:

    if (!isfg && row >= 24 && fgcov)      /* blank the BG: the FG covers it */

`fgcov` asks whether the FG tile's ART is fully opaque (BG_BOTTOM_HOLES /
fg_tile_opaque) and NEVER whether that FG cell got an MD slot. A refused cell is
blank on the MD, its art is opaque, so the BG beneath is blanked too -- BOTH layers
blank = black. The fix records the refusal per cell (140-byte bitmap) and the
backstop consults it; BG composes before FG so it reads one frame late, in time for
a defect that lasts seconds.

THE MEASUREMENT, control only, dense: 41 shots at 200-frame intervals across
2400-10400 of discover/inputs/gameplay_level1.csv, whole-screen 8x8 census
(tools/black_cells.py, new -- the instrument the old bottom-band/attract censuses
lacked):

    40 of 41 frames: ZERO black cells
     1 of 41 frames: 1088 of 1120 cells black = a BLANK frame (a cut or load),
                     not the defect

**So ares does not reproduce it on this route.** An earlier six-frame run read 0 on
BOTH control and candidate, and a zero on the control is a null instrument, not a
passing fix -- the builder's own rule, applied to my own work.

This matches recorded precedent: STATE has defects 2 and 4 as RIG-ONLY, needing
plain-rom captures. Mike sees these in play and on the FPGA. So the instrument for
this defect is the rig, and the ares route cannot gate it either way.

**The fix therefore has NO evidence for or against it.** Its reasoning stands on
reading both sites; that is not the same as working.

**Rule:** before reporting a fix, check that the control exhibits the defect. I
built, flagged, measured and nearly reported a pass on a route where the defect does
not occur -- 40 clean frames on the control.
### The L1 vectors could not be replayed at all, because this game's functions take no register arguments (2026-09-27)

The harness plan was to map a vector's entry registers onto the pseudo-register
globals and call the function. Built it, then read two of the functions it was
meant to test:

    FUN_000063b4:  if (((DAT_fffff028 & 4) == 0) && ((DAT_fffff029 & 4) == 0))
                     DAT_fffff026 = DAT_fffff026 | 0x20;
    FUN_00002e50:  if (DAT_fffff03c != '\0') { ... _DAT_fffff03e ... }

**Neither reads a register.** They read ABSOLUTE WORK RAM, which is what 68K
code compiled from C does, so the entry state that decides the outcome is
MEMORY -- and the capture recorded registers and writes but not reads. Every
vector was unreplayable and nobody had noticed, because nothing had tried to
run one.

FIXED in tools/l1_oracle.lua: a read watchpoint over WRAM, mirroring the write
one, logging (address, data, WIDTH in bits -- a seeded byte and word are
otherwise indistinguishable; `wpsize` gives it). The FIRST read of an address
is the input; later reads may see the function's own writes. The 4 bytes at the
entry SP are the rts popping the return address, not an input, and are filtered.

A vector is now: reads in, writes out, registers either side. 0x02E50's reads
to `[['FFF03C', 0, 1]]` and writes to nothing -- which is exactly the C's early
return when that byte is zero, and the first differential this project has run.

COST OF THE READ WATCHPOINT, stated: call detection drops from 268 callees /
943 vectors to 192 / 669, because the watchpoint's stop-and-resume breaks the
consecutive-instruction pairing the stack-based call detector relies on. Keep
both captures -- the reads-free one for breadth, the reads one for replay.

### A differential can pass on nothing: report vacuity or the gate is worthless (2026-09-27)

First run of tools/l1_verify.py: 8 PASS. One of them was `sprite_frame_lookup`,
whose decompiled body is `return;` for 34 bytes of real code. It passed 4/4
because every one of its vectors recorded ZERO WRITES, and an empty function
reproduces an empty write set perfectly.

So the harness now separates them:

    PASS                 writes reproduced AND at least one vector constrains output
    PASS-UNCONSTRAINED   no vector writes anything: proves nothing, never count it

That reclassified 7 of the 8. **Exactly one function is genuinely verified so
far** -- collide_box_b, 4/4 vectors, 2 of them constraining. Seven FAIL against
the oracle and are real decompilation defects, rng_next among them.

**Rule:** a green result whose test could not have been red is not evidence.
Before reporting a pass rate, count how many cases could have failed.

### What actually gates the L1 harness is the interface, not the harness (2026-09-27)

tools/l1_triage.py over 771 functions and the vector set:

    TESTABLE-NOW          7 functions     510 bytes
    NEEDS-WRITE-DIFF     14 functions   1,298 bytes
    SHADOWED            165 functions  24,550 bytes
    EMPTY-BODY            7 functions     304 bytes
    NO-VECTOR           578 functions  40,846 bytes

**SHADOWED is the blocker: 165 of the 193 vectored functions (85%).** The body
re-declares `in_D0w` / `unaff_A6` / `extraout_*` as a LOCAL, which shadows the
global the harness sets, so the function reads an uninitialised stack slot.
`textram_stride_write` does it with all three of its inputs -- and it writes
through those uninitialised pointers, so CALLING it is unsafe, which makes
every caller unsafe too. The fix is prep-side, one transform: drop the local
declaration so the name binds to the global.

NO-VECTOR (578) is the other gate and it is a capture problem, not a prep one.

### CONFIRMED: the level-1 black tiles are ONLY in rows 24-27, which is exactly the backstop's domain (2026-09-27 12:15 ET)

Mike's own captures were in `screenshots/` the whole time -- he said he had handed
them over "every single time", and 60 loose PNGs were sitting there uncensused.
Running the whole-screen 8x8 census over them separates three populations:

    5 shots   ~1120 cells   essentially all black: a blank/cut frame, NOT the defect
    11 shots    1-53 cells  THE DEFECT
    44 shots         0

And the defect's spatial signature is decisive. `20260925_104044-x4d.png`, 53
cells:

    view row 24: 14 cells
    view row 25: 13
    view row 26: 12
    view row 27: 14
    NOTHING above row 24

**The bottom-band backstop's guard is `row >= 24`.** The black cells occupy that
band and only that band. `20260925_135530-s16.png` adds rows 23-27 (21 cells) and
five more captures are a single cell at row 27.

So the diagnosis is confirmed from a second, independent direction: reading the
code said the backstop blanks a BG cell wherever the FG art is opaque without
checking that the FG cell got an MD slot; the captures say the damage is confined
to precisely the rows that code governs.

WHAT THAT MAKES C1REFUSEBG: a fix aimed at the right site, still unproven. ares
gives 40 clean control frames on the level-1 bot route, so it cannot gate it --
the captures are 320x224 native, from the rig and Mike's play, matching STATE's
"defects 2/4 rig-only".

**Rule:** census the corpus you already have before building a route to reproduce
something. Mike had handed over the evidence repeatedly; 60 screenshots sat
unmeasured while I built a bot route that does not exhibit the defect at all.
### The shadow-drop transform: 1,743 declarators, the builder's whole gate (2026-09-27 12:30 ET)

The builder's harness found the interface broken before it found anything wrong
with the corpus, and the fix was on my side.

THEIR FINDING: Ghidra declares `short in_D0w;` / `byte *unaff_A6;` as a LOCAL in
the function that reads it, SHADOWING the global of the same name. A differential
harness sets the globals; the body then reads uninitialised stack. **165 of 193
vectored functions shadowed, 24,550 bytes, all behind one prep-side transform.**

FIXED: `tools/cport_prep.py` drops a declarator whose name is a pseudo-register,
keeping the other names in a mixed declaration and leaving assignments alone (a
function that writes `extraout_var` is writing that register, and the global is the
right destination). **1,743 declarators dropped, zero remaining, and the corpus
still compiles to an SH-2 object.**

One follow-on: with the locals gone, four sites broke where an A-register is
MASKED (`unaff_A2 & 0xfffffff0` -- the alignment round-down the 68K instruction
performs) because the global is `byte *` and C rejects `&` on a pointer. My first
regex for that over-matched and produced 132 errors against the 4 it fixed;
narrowing it to a parenthesised mask expression with the register as the left
operand fixed it at 0.

TWO OF THEIR FINDINGS WORTH KEEPING AS RULES:

  - **The vectors were unreplayable and nobody noticed because nothing had run
    one.** The game reads ABSOLUTE WORK RAM, not registers, so the entry state that
    decides an outcome is memory -- and the capture recorded registers and writes
    but not READS. All 943 were unusable. Fixed with a read watchpoint; cost is
    callees 268 -> 192, because stop-and-resume breaks the consecutive-instruction
    pairing that detects calls.
  - **A differential can pass on nothing.** Their first run reported 8 PASS; one was
    `sprite_frame_lookup`, whose body is `return;` for 34 bytes, passing 4/4
    because all its vectors record zero writes and an empty function reproduces an
    empty write set perfectly. 7 of the 8 reclassified as PASS-UNCONSTRAINED.

CURRENT SCORE, theirs: PASS 1 (`collide_box_b`, 4/4 with 2 constraining),
PASS-UNCONSTRAINED 7, FAIL 7 (real decompilation defects, `rng_next` among them),
CRASH 31, WONT-COMPILE 75. And `textram_stride_write` shadows all three of its
inputs and writes through them, so calling it is unsafe and every caller inherits
that.

**Rule:** a verification harness needs a PASS that could have failed. Count the
constraining outputs, or an empty function scores perfectly.
### THE BLACK TILES ARE A MISSING FB FALLBACK: C1REFUSEBG converts them to PURPLE, which names the real bug (2026-09-27 12:20 ET)

Mike played bt1 (line + WRAMCODE + C1REFUSEBG) on the rig: "the occurrence of the
black tiles is fewer and not 100% resolved, but far less... mostly reduced to POPIN
instead of static. When the grass transitions to the boss entrance, PURPLE tiles
appear instead of the black tiles."

CENSUS OF HIS 21 bt1 CAPTURES against his 09-25 control:

    control (20260925_104044-x4d)   53 black cells, rows 24-27 ONLY
    bt1                              0 black cells in rows 24-27
                                    13 PURPLE cells, rows 24/25/27
                                    19 black cells elsewhere (rows 7,8,11,17,20)
                                       -- a DIFFERENT population, one shot

**So the fix did exactly what it was designed to do and the band changed colour
rather than clearing.** That is the informative outcome.

THE PURPLE IS ALREADY NAMED IN THE RECORD. REBUILD.md "PASS 11 forensics: the
purple has a NAME in CRAM": cram_mirror holds 0x680E = RGB(112,0,208) at entries
24/32/40/48/88/96/104/112 -- **pen 0 of eight tile groups**. S16 convention makes
colour 0 arcade-invisible, so the game parks garbage there and Altered Beast's
garbage IS this purple. "the defect is whatever DISPLAYS pen-0 pixels." My measured
(146,0,255) is that colour through MD quantisation.

SO BOTH SYMPTOMS ARE ONE ROOT CAUSE, and it is documented at m_main.c:575:

    "cat-1 cells have the FB fallback (CAT1_PEND); cat-0 cells have nothing"
    "under C1_NOFB there is no FB fallback, so the SLOT PRESSURE rule leaves a
     BLACK cell"

**C1NOFB=1 IS ON THE LINE.** So a cat-1 cell refused a slot has NOTHING drawing
it. The backstop was masking that with black; removing the mask shows the BG's
pen-0 garbage instead. Not blanking the BG was never the fix -- the cell needs a
DRAWER.

AND THE REAL FIX'S PRIOR VERDICT WAS MEASURED THROUGH THE MASK. `C1EVICT=1` makes
`C1_SOFT` 0, which disables the slot-pressure refusal entirely so cat-1 tiles
evict normally -- no refusal, no undrawn cell. Its recorded verdict is "changed
nothing on the rig's picture (vi91 = vi90)", but that was measured while the
backstop still painted the result black, so the picture could not have changed.
**bt2 = line + WRAMCODE + C1REFUSEBG + C1EVICT is the first build where that
combination is visible**, on the rig 12:17:17, _end unchanged.

Its risk is the one the 2026-09-03 rule was written to prevent: the ping-pong
between a cat-1 and the BG tile it evicts, measured then at cut-blanks 3.9/gen
against shipping 0.8/gen. So the thing to watch on bt2 is FLICKER AT SCENE CUTS,
not the bottom band.

**Rule:** when a fix changes a defect's COLOUR rather than removing it, the new
colour is evidence. Black said "nothing is drawn here"; purple said "pen 0 is being
displayed", which named the missing drawer and pointed at a flag already on the
line.
### C1NOFB is a "not a ship" flag ON THE LINE, and it is the black tiles' root cause (2026-09-27 12:35 ET)

Parked on Mike's call -- "chasing this bug will be much more worthwhile having the
port running on the SH-2" -- with the chain fully established. Recording it so none
of it is re-derived.

THE CHAIN, all measured or quoted:

  1. Gameplay runs 1018/1024 VRAM slots and 123/128 hash sets full (attract: 499
     and 0), so the SLOT PRESSURE rule at m_main.c:17700 refuses a cat-1 cell whose
     set is full of hot ways.
  2. That rule's own comment says "The FB keeps the cell instead (CAT1_PEND)".
  3. **But C1NOFB=1 IS IN LINE_FLAGS, and its own Makefile comment reads: "Removes
     the FB cat-1 pass entirely... so this is the step-3 MEASUREMENT and not a
     ship."** So the fallback the refusal relies on does not exist on the line.
  4. Nothing draws the cell. The bottom-band backstop then blanks the BG beneath it
     (fgcov tests the FG ART's opacity, never whether the cell got a slot) = both
     layers blank = BLACK.

THREE BUILDS, all measured against Mike's 09-25 control (53 black cells, rows 24-27
only):

    bt1  + C1REFUSEBG           band black 53 -> 0, but 13 PURPLE appear
                                (pen 0 displayed -- REBUILD PASS 11 named it
                                0x680E RGB(112,0,208)). Mike: "far less... mostly
                                popin instead of static". NOT A SHIP: it unmasks
                                pen-0 garbage instead of drawing the cell.
    bt2  + C1EVICT              REGRESSION. 167 band black and whole-row blanking
                                (one shot: complete rows 0-3 and 24-27, 320 cells).
                                C1EVICT sets C1_SOFT to 0, disabling the refusal,
                                and the 2026-09-03 ping-pong it prevented is real:
                                "cut-blanks 3.9/gen vs shipping 0.8/gen".
                                Its old verdict "changed nothing on the rig's
                                picture" was measured while black masked it.
    bt3  C1FB=1 (FB pass back)  WILL NOT LINK: .ramtext overflows its
                                0x31000-0x38000 slot. The line has **416 bytes**
                                of .ramtext headroom (__ramtext_end 0x06037E60) and
                                the FB cat-1 pass needs more.

THE REAL FIX AND ITS PRICE: restore the FB cat-1 pass (C1FB=1, the override added
today so this never needs LINE_FLAGS reconstructed by hand). It needs >416 bytes of
.ramtext, and membership is opt-in per function via
`__attribute__((section(".ramtext")))` -- so the pass can stay in ROM instead,
since it runs only for refused cells. That is the route if this is picked up before
the port.

WHY PARKING IT IS RIGHT: C1NOFB exists for PLAN-TILES-TO-VDP, i.e. to cut
FRAMEBUFFER WRITES. The port deletes the pipeline, so FB write pressure stops being
the binding constraint -- which is the trade that made a "not a ship" flag look
acceptable on the line in the first place. Fixing it now means engineering around a
416-byte slot to preserve a constraint the port dissolves.

**Rule:** a flag whose own comment says "not a ship" does not belong in LINE_FLAGS.
Nobody put it there dishonestly -- it earned its place on speed -- but the comment
was the warning and it went unread for weeks while the defect it causes was chased
as a mystery.

**AND A TRAP I WALKED INTO:** I hand-reconstructed LINE_FLAGS with sed to drop one
flag. CLAUDE.md warns that bare `make ship-us` "is NOT the line... regenerates
.build_flags with a REDUCED -D set" and fails with an undeclared symbol; it failed
with `md_round undeclared`, exactly as written. Worse, the failed build left
rom/night/bt3.32x as a COPY OF bt2 with bt2's build stamp -- a mislabelled rom I
nearly pushed. Use a Makefile override, and check the build id of every rom before
handing it over.
### rng_next's decompilation is CORRECT -- the FAIL was my declaration width (2026-09-27 12:55 ET)

The builder reported 7 FAIL as "real decompilation defects, rng_next among them".
At least one of those is mine.

EVALUATING rng_next's C faithfully against its two oracle vectors, in Python, with
no hardware in the way:

    vector 1: in 0xCF89035A -> oracle 0xC65B896A, decompiled 0xC65B896A   PASS
    vector 2: in 0xC65B896A -> oracle 0xC6A301FA, decompiled 0xC6A301FA   PASS

**The C is exactly right.** What was wrong is the type I generated for its state:

    extern byte _DAT_fffff014;        <- cport_prep, guessing from the name

The function does `_DAT_fffff014 * 0x29` and `>> 0x10`. At 8 bits that cannot
work. And 89 symbols were declared `byte` while used in wide arithmetic, because
cport_prep inferred width from the SYMBOL'S FIRST LETTER -- a guess, in a repo
whose rule is derive, don't guess.

FIXED by asking Ghidra, which already inferred a type at every address
(tools/ghidra/f1_datwidth.py, new): every defined datum's own length, plus the
ACCESS width at every reference from the instruction suffix (.b/.w/.l), widest
wins. 715 addresses; 229 byte, 205 word, 205 long, the rest arrays.

    declarations now: 228 byte, 212 ushort, 145 uint   (derived)
    byte-declared-but-used-wide: 89 -> 6
    corpus still compiles: 0 errors

TWO THINGS FOR THE BUILDER, both likely to move their score:

  - **Re-run the 7 FAILs.** If rng_next was a width artefact, others will be too.
  - **Their PASS may be softer than it looks.** `collide_box_b`'s recorded writes
    are at FFFEF0/FFFEF2 with A7 = 0xFFFFFEF4 -- that is the CALL FRAME, not data.
    So its "4/4 with 2 constraining" may be constrained by stack traffic. A
    harness should filter writes at or just below the entry stack pointer, the way
    this analysis filtered `int(addr,16) < 0xFFFE00` to find the real outputs.

**Rule:** a differential failure indicts the whole chain, not just the code under
test. Mine was the type declarations, and the first thing I did that found it was
evaluating the function by hand instead of trusting either end of the harness.
### FIRST DECOMPILED 68K FUNCTION EXECUTING ON THE SH-2, VERIFIED AGAINST THE ARCADE (2026-09-27 12:45 ET)

    port_test_out[]:
      [0] magic    0x50525431   = 'PRT1', it ran
      [1] vectors  2
      [2] passes   2
      vector 1: got 0xC65B896A  want 0xC65B896A   PASS
      vector 2: got 0xC6A301FA  want 0xC6A301FA   PASS

`make line WRAMCODE=1 PORTTEST=1` -> rom/night/pt1.32x. sh_src/port_selftest.c
carries rng_next (68K 0x03FBE) EXACTLY as Ghidra emitted it -- the only additions
are the type prologue the port generates -- and checks it against two vectors
captured from `mame altbeast`, the oracle CLAUDE.md calls always valid. Called from
m_main, which runs exactly once, so the counters are unambiguous. Result read with
`ares-headless --dump sdram:0x49C:0x40`; the address comes from rom/s16.elf, so
there is no magic constant.

**This is the first time ported game code has executed on the target and been
verified against the arcade.** Cost: _end 0x06017DF8 -> 0x06017E40 = +72 bytes,
flagged off by default, and the file compiles to nothing without PORTTEST.

WHY IT WORKED FIRST TRY, and the honest version: rng_next was chosen BECAUSE it had
already been proved correct on the host that morning -- the builder's harness called
it a decompilation defect, and it turned out to be a declaration-width bug in
cport_prep (LESSONS 12:55). So this run confirms the SH-2 agrees with the 68K and
with the host; it did not discover anything new about the function. What it proves is
the PATH: Ghidra C -> cport type derivation -> sh-elf-gcc -> our rom -> SH-2 -> a
result that matches the arcade.

WHAT IT IS NOT. One function of 771, self-contained, no work-RAM layout, no
dispatch, no cross-CPU state. The 52 KB tilemap shadow, the object table, the
handlers and the pipeline deletion are all untouched. But the chain that was
theoretical this morning is now executed and measured end to end.

**Rule:** pick the first thing to run on new hardware from what is already proved
elsewhere. A first-run failure would have had four candidate causes (the C, the
types, the toolchain, the target); starting from a host-verified function left only
one.
### DECOMPILED 68K CODE VERIFIED ON REAL 32X SILICON: 4 of 4 (2026-09-27 13:05 ET)

    rig, rom/night/pt4.32x, two independent screenshots
      20260927_130313-pt4.png   vint  819   sh_pub 4
      20260927_130327-pt4.png   vint 1615   sh_pub 4

Barcode byte 5 carries the self-test's PASS COUNT, so the verdict on decompiled
code is readable off the FPGA from one screenshot. ares agrees: run 4, passed 4.

TWO FUNCTIONS, both EXACTLY as Ghidra emitted them (only the generated type
prologue added):

    rng_next          68K 0x03FBE   0xCF89035A -> 0xC65B896A   PASS
                                    0xC65B896A -> 0xC6A301FA   PASS
    clamp_x_velocity  68K 0x03F40   pos 376 vel -8 lim -256 -> 368   PASS
                                    pos 0 vel 0 lim 0 -> 0           PASS

`clamp_x_velocity` is the first ported function to exercise the work-RAM LAYOUT
rather than one isolated cell: it reaches +0x14 / +0x16 / +0x18 through A6 inside a
record of the real 0x80 stride. Vectors are the arcade's, captured from
`mame altbeast` by the L1 harness.

THE BUG THAT COST THE MOST, and it is a good one to know about this machine:
the publish went through two statics at 0x06001B3E/3F, and they read **4/4 at
frame 40 and 0 by frame 400**. Something reclaims that SDRAM once the game is
running; `port_test_out` at 0x0600051C survives indefinitely. The rig's first
screenshot lands near frame 1200, so the statics were always going to read zero
there -- and the disassembly showed a correct store the whole time. Fixed by
pointing RIGB5/RIGB6 at port_test_out[] directly.

Two smaller ones: the self-test first ran at the TOP of m_main, before
`Hw32xInit`, which zeroes memory and wiped the result; and a failed build leaves
rom/night/*.32x as a COPY OF THE PREVIOUS ROM carrying its build stamp -- I nearly
pushed three mislabelled roms and caught each only by checking `build_id.py show`.

Cost: PORTTEST is off by default and the file compiles to nothing without it.

**Rule:** when a value reads zero, sample it EARLY as well as late. A correct store
and a zero cell look identical at one sample point, and the disassembly cannot tell
you the difference.
### WITHDRAWN -- rng_next's decompilation is CORRECT; the one-bit claim below was MY ARITHMETIC (2026-09-27, same day)

Retracted in full. The raw watchpoint lines say:

    W 00FFF014 0000C65B 16
    W 00FFF016 0000896A 16

**35178 is 0x896A. I converted it by hand to 0x894A and built a
"demonstration" on top of that.** 0x894A is 35146, a value the oracle never
wrote. The decompiled C computes 0xC65B896A, the oracle wrote 0xC65B896A, and
rng_next agrees with the 68K on this vector.

And the failure my harness reported is ENDIANNESS, not the corpus -- see the
next entry. Two errors of my own, stacked, and the shape is the one I had spent
the day pointing at elsewhere: a hand-computed intermediate treated as
measurement. The instrument said FAIL, I did not ask why before deciding what
it meant.

The original entry follows, wrong, for the record.

### (WITHDRAWN) rng_next's decompilation is WRONG, and the differential caught it: one bit, demonstrated (2026-09-27)

`tools/l1_verify.py` reported rng_next FAIL. The port self-test
(sh_src/port_selftest.c) reports it PASS. Computed by hand, from the same
input, the disagreement resolves and it is not a harness artefact:

    input 0xCF89035A
      the decompiled C computes   0xC65B896A
      THE ORACLE WROTE            0xC65B894A     <- one bit, 0x20 of the low byte

The oracle value is what the 68K wrote at that call, captured by a write
watchpoint under `mame altbeast`, which CLAUDE.md calls always valid. So the
decompilation does not reproduce the function.

Curiously the two chains RECONVERGE: the oracle's next state from 0xC65B894A is
0xC6A301FA, and the C's next state from ITS 0xC65B896A is also 0xC6A301FA. A
one-bit divergence that washes out in one step looks like a carry or rotate the
decompiler modelled as an add -- worth a look at the real instruction.

### A test whose expected value comes from the implementation cannot fail (2026-09-27) -- PRINCIPLE STANDS, MY TIMING ARGUMENT WAS FALSE

The port self-test's `RNG_WANT[0]` is 0xC65B896A -- exactly what the decompiled
C computes, and NOT what the oracle wrote. Its header cites
docs/proof/l1_vectors.jsonl as the source, but that file carried no input
values until commit 257d5be5 (2026-09-27 11:49), which is AFTER the self-test
was written: a vector was exit registers and writes only, with no way to know
the entry state. So the expected values cannot have come from it.

This is the vacuity rule from the same day in a different dress. There, a
function passed because its vectors constrained nothing. Here, a function
passes because the expected value was derived from the code under test. Both
produce a green result that could not have been red. THE PRINCIPLE STANDS and
the decompile thread has since generated every comparison from the vector file
(tools/gen_selftest.py).

**BUT MY SUPPORTING ARGUMENT WAS FALSE AND IS WITHDRAWN.** I claimed the vector
file carried no inputs until 11:49, after the self-test was written, so the
constants could not have come from it. Both halves are wrong: the file's mtime
is 11:42 (11:49 was the COMMIT, not the write), and the self-test's first
commit is 12:43 -- an HOUR LATER. The constants could have come from the file,
and the author says they were read from it by hand. A `git log -S` timestamp is
when a change was COMMITTED; do not use it to date a file's contents, and do
not build an impossibility argument on it.

**Rule: state where an expected value came from, in the test.** "From the
oracle at frame N" and "computed from the C" are both legitimate provenances
for different purposes, and only the first can falsify a port.

### Say FPGA when you mean FPGA: real 32X hardware has never run this code (2026-09-27)

Four comments in the shipping sources said a behaviour happened "on silicon" --
a burst that LOCKS THE MACHINE, a DMA placement that loses the background, a
deferred palette that blacks the level, a bank-window overrun that locks the
68K. All four were measured on the MiSTer FPGA core. Relabelled ON THE FPGA
(md_main.c 748/1120/1408, m_main.c 11246).

This is not pedantry. The FPGA core is an implementation with known departures
from hardware -- it holds the 68K's DTACK for a full SDRAM access on every MD
ROM read, which a mask-ROM cartridge does not do at all -- so "it locks on
silicon" and "it locks on the MiSTer core" are different claims with different
consequences. A real 32X was in transit on 2026-09-27 and had never run this
code; a commit message on the same day claimed "verified on REAL 32X silicon",
which cannot have happened yet.

### CORRECTION: "REAL 32X silicon" was the MiSTer core, and my self-test's constants were transcribed (2026-09-27 13:40 ET)

The builder is right on both counts and the first is a factual error in a commit
title.

**1. THE MiSTer IS NOT REAL 32X SILICON.** Commit d8d3e1d0 is titled "Decompiled
68K code verified on REAL 32X silicon: 4 of 4". It was verified on the **MiSTer
FPGA core**. Mike's 32X hardware is still in the post. The title cannot be
rewritten but it is wrong, and every claim resting on it should read "on the
FPGA". This is the same relabelling the builder just applied to four shipping
comments (md_main.c:748, 1120, 1408; m_main.c:11246), and I committed a fresh
instance of it the same day.

**2. THE CONSTANTS WERE TRANSCRIBED, WHICH IS THE WEAKER CLAIM I THOUGHT IT WAS
NOT.** The self-test's expected values were hardcoded after I read them out of the
vector file by hand. That is not the same as a harness that SOURCES them, and it
cannot fail for the reason a sourced one can. Fixed: tools/gen_selftest.py now
generates the comparisons directly from docs/proof/l1_vectors.jsonl, so the
expected values are the oracle's by construction.

FIRST ORACLE-SOURCED RESULT, and it is not 4 of 4:

    run 3, passed 1     (rng_next x2, palette_queue_drain x1)

**So something does fail, which supports the builder's direction even where we
disagree on the value.** Per-case attribution is NOT yet known -- my per-case
reporting patch silently failed to apply and I misread the case indices in
port_test_out[] as pass/fail flags. Do not quote a per-function verdict from this
run.

WHERE WE DISAGREE, stated so it can be settled rather than argued. On the vector
file as published (docs/proof/l1_vectors.jsonl, mtime 11:42, which DOES carry
reads):

    reads   FFF014 53129 (0xCF89), FFF016   858 (0x035A)  -> in  0xCF89035A
    writes  FFF014 50779 (0xC65B), FFF016 35178 (0x896A)  -> out 0xC65B896A

The builder reports the oracle wrote 0xC65B**894A** -- one bit, 0x20 of the low
byte -- and concludes the decompilation is wrong. 35178 is 0x896A; 0x894A is
35146. So either their corrected capture differs from the file they published, or
one of us has mis-decoded it. **That one bit decides whether rng_next's
decompilation is wrong, so it needs the raw watchpoint line, not another
derivation.**

Their claim that the file "carried no input values until 11:49, after the
self-test was written" does not match the copy on disk: mtime 11:42, and it has
reads. Worth reconciling, because if their file moved under me the disagreement is
explained.

**Rule:** label the machine in the claim. "On the FPGA" and "on silicon" are
different measurements, and a commit title is exactly where that distinction gets
lost.
### ares and the MiSTer core are PIXEL-IDENTICAL on the 240p Test Suite, and the 9.6% I nearly reported was my own resampler (2026-09-27)

First pixel-exact agreement ever measured between our two emulating machines,
on an instrument neither thread wrote: Artemio's 240p Test Suite for 32X
(tools/240ptestsuite/240pSuite_32X_1.0.32x, a binary whose every version was
"developed and tested on real hardware"). Its menu screen, ares against the
MiSTer core: **0.00% of pixels differ.**

I first measured 9.6% and was one step from filing it as an FPGA divergence.
Two controls caught it:

  the FPGA's three captures, 8 s apart   BYTE-IDENTICAL to each other
  ares's two frames                      byte-identical to each other

so the screen is static and any difference had to be real -- or mine. It was
mine. **ares hands back a 4:1 pixel-doubled frame, and downscaling it with
BILINEAR blurs every edge:**

    x=63 NEAREST    0.00% differ      x=63 BILINEAR   11.71%
    x=65 NEAREST    0.00% differ      x=65 BILINEAR    9.56%

Fixed in tools/rig_barcode.py's frame(). Rig screenshots arrive at 320x224
already, so that path only ever affected ares frames; the barcode still decoded
because it thresholds 4x4 cell centres hard, but every pixel-exact ares-vs-rig
comparison through that helper carried ~10% of manufactured difference.

AND A SELF-CATCH WORTH KEEPING: my alignment sweep reported the crop x offset
was wrong (63, not the tool's 65). It is not. Both offsets give 0.00% with
NEAREST -- the sweep's minimum was DEGENERATE and I read the first of many
equal winners as the answer. A parameter sweep that finds several exact minima
has not identified a parameter; check whether the minimum is unique before
changing a constant on its authority.


### The host cannot verify this corpus: the 68K and SH-2 are big-endian and the host is not (2026-09-27)

The real reason tools/l1_verify.py reported FAIL on rng_next, and on every
other function that touches a multi-byte cell:

    bytes CF 89 03 5A  read big-endian    = 0xCF89035A -> rng = 0xC65B896A
                       read LITTLE-endian = 0x5A0389CF -> rng = 0x7CB81227
    stored back, memory reads  27 12 B8 7C
    the vector expects         C6 5B 89 6A

The decompiled C reaches memory through typed pointers (`*(uint *)(&DAT_x)`),
so the host's byte order is applied to 68K data and every word or long access
is wrong. **A hosted differential is sound ONLY for byte-wise functions.**

This is not a fixable detail of the harness: the corpus casts and does pointer
arithmetic freely, so there is no chokepoint to byte-swap at. It is a property
of the route.

**And it independently justifies running on the target.** The SH-2 is
big-endian like the 68K, so the same source is correct there by construction --
which is what the decompile thread's on-SH-2 harness already does. The division
that follows: the vectors, the read capture and the triage are worth having
from this side; EXECUTION belongs on the SH-2.

### The shadow-drop transform took the testable set from 23 functions to 148 (2026-09-27)

Triaged against the PREPPED corpus (`altbeast_port.c`, what actually compiles)
rather than the source corpus, and counting only INPUT declarators:

                        source    prepped
    TESTABLE-NOW             4         45   (4,444 B)
    NEEDS-WRITE-DIFF        19        103   (14,810 B)
    SHADOWED               118          0
    NO-VECTOR              623        623   (48,254 B)

So the decompile thread's transform (dcf9b5e0) is the whole gate it claimed to
be: 23 testable functions to 148, 19,254 bytes.

Two reconciliations worth keeping, because both of us had a true statement that
looked like a contradiction:

**"Zero shadowing declarators remain" and "73 remain" are both right.** 1,810
in the source, 73 in the prepped output -- and ALL 73 are `extraout_*` with
numeric suffixes (`extraout_A0_00`, `extraout_D1w_01`), which Ghidra emits when
one function takes several distinct register values back from different calls.
**`extraout_*` is an OUTPUT of an inner call, not an input**, so shadowing it
does not stop a harness driving the function. Zero `in_*`/`unaff_*` remain,
which is the number that gates anything.

**Triage the artefact that compiles.** My earlier figures came from
docs/proof/altbeast.c and were stale the moment a transform ran downstream of
it. The gate is a property of the prepped output.

**The remaining blocker is now mine, not theirs: 623 of 771 functions have no
vector at all** -- 48,254 bytes, 74% of the corpus. That is a capture problem.

### A shipped 32X cart clocks its FM off the PWM DMA, in stereo (2026-10-19)

32X Live Mixer Vol. 1 (srcref, static disassembly, `srcref/32X-Live-Mixer-Vol1/MAP.md`):
SH-2 DMA1 with CHCR1 `0x18E5` moves LONGWORDS to `0x20004034`, so one DREQ transfer
writes PWM L and R together (ROM `0x11544-0x11558`). Each DMA buffer is one sequencer
tick; the DMA-end ISR bumps a counter on COMM10 and the 68K runs FM ticks until it
matches (ROM `0x1B56-0x1BA0`). That is a working recipe for FM/PWM lockstep without
vblank. Our lane's own `speech_census[2]` "underruns" has no writer
(`sh_src/r60/r60_speech.inc:46`), see docs/sound/HANDOFF-SOUND.md top.

## Decompiled 68K code verified on the SH-2 (2026-09-27)

Three arcade-oracle vectors over two decompiled functions (`rng_next` 68K
0x03FBE, `palette_queue_drain` 68K 0x02DBC) run on the 32X's SH-2 and match
the oracle: `run 3 / passed 3` in ares at frames 40 and 500, and on the
MiSTer FPGA core at vint 712 and 910 (`pt12`/`pt14`, byte 5 = 51 = run 3,
passed 3). THE FPGA CORE IS NOT REAL SILICON -- Mike's 32X hardware has not
run this. Falsified both ways: one bit cleared in one expected value gives
`passed 2` in ares AND on the rig (`pt13`, byte 5 = 50). A PASS that cannot
fail is not a measurement.

Five traps, each measured, each having cost a wrong verdict first:

1. **Casting `volatile byte *` to `uint *` DISCARDS volatile.** The generated
   accessors did `*(uint *)WP(addr)`, so GCC cached the cell in a register
   across the call and the comparison read a stale copy while MEMORY HELD THE
   RIGHT ANSWER -- `wram[4..7]` dumped as `0xC6A301FA`, exactly the oracle
   value, while the test reported FAIL. Every access `tools/gen_selftest.py`
   emits is now `volatile`. A FAIL whose read-back equals the case's own INPUT
   is this bug, not a bad port.
2. **A "this BSS address survives" note is true for exactly one link.**
   `port_test_out` moved `0x0600051C` -> `0x060021d8` across one edit to
   `sh_src/m_main.c`, which silently voided the note that put the readout
   there.
3. **`port_test_out` is RECLAIMED between frame 40 and frame 200** -- magic
   word included. A one-shot self-test after `Hw32xInit` reads correct at
   frame 40 and all-zero by 200, so the rig, whose screenshots land at vint
   500-900, read 0 and reported nothing. Fix: re-run per vint.
4. **`hs_stub` is not the live per-vint barcode publisher; `m_main`'s publish
   loop is.** A hook in `hs_stub` left frame 500 reading zero while frame 40
   still showed the stale one-shot -- which looks exactly like a working test.
   `port_test_out[3]` is now a heartbeat, so "it ran" is never inferred from
   the verdict.
5. **Barcode byte 6 does not survive to the readout under the line's flags;
   byte 5 does.** Byte 5 carried `passed` while byte 6 read 0 with
   `port_test_out[1]` = 3 in the same build's ares dump (a second publisher
   owns byte 6 -- the 2026-09-24 note at the DREQ header writes). Both
   counters now ride byte 5 as nibbles: `run << 4 | passed`, so `run !=
   passed` is visible on its face. Byte 5 reading a plausible small number is
   NOT evidence the test ran -- its default meaning is packets published, and
   an early `sh_pub 3` was read as "passed 3" when PORT_TEST's counter was 0.

**The disputed oracle value is settled against the builder's `0xC65B894A`:
the correct value is `0xC65B896A`.** It is not my copy of the file against
theirs -- the two recorded vectors CHAIN. `rng(0xCF89035A)` = `0xC65B896A`,
and `rng(0xC65B896A)` = `0xC6A301FA`, which is the oracle's own recorded
output for the NEXT call. From `0xC65B894A` the next call gives
`0xC182FCDA`, contradicting that record. Both values also sit in the oracle
register captures directly (`l1_vectors.jsonl`, callee `003FBE`, exit d1).
One bit (0x20); a transcription slip.

### Verify the WHOLE frame, not the active area, when the readout is a colour (2026-09-27)

I reported the test cartridge showing "full screen green" on ares and the
MiSTer core. Mike's own ares window showed a green centre inside a **CYAN
BORDER**, and he was right.

Two layers, and the suite was only driving one. The 32X layer covers the active
area; the border is the MD BACKDROP, and main()'s boot tracer leaves its last
stage colour there -- cyan for "slave alive". My headless checks cropped to the
active area (rows 11-234), which is correct for pixel comparisons and exactly
wrong for reading a colour verdict, so the crop hid the very thing a human sees
first.

It is not cosmetic. On hardware a boot stage that ends RED would frame a GREEN
pass, and the screen is the whole readout channel there -- no memory dump
exists. The suite now drives both layers from the same verdict and they always
agree: 32X CRAM 0xA15200 in 15-bit BGR, MD backdrop CRAM entry 0 in 9-bit BGR.

**Rule:** a screenshot check whose answer is "what colour is the screen" must
look at the frame the operator looks at. Crop for diffs, never for verdicts.

## Scaling the SH-2 self-test: 11 functions, 39 vectors (2026-09-27)

`tools/gen_selftest.py` now emits CALL CLOSURES over a MULTI-WINDOW work-RAM
mirror. 11 functions / 39 oracle vectors, **27 cases pass**, identical in ares
(frames 300 and 500) and on the MiSTer FPGA core (`pt15`, vint 431/577/720).
Falsified on the rig: one bit flipped in one expected value reads 26, not 27.
Seven functions pass every vector they have -- `rng_next`, `clamp_x_velocity`,
`clamp_y_velocity`, `FUN_000012ec`, `FUN_00003f24`, `FUN_00006646`,
`FUN_0000673e`. THE FPGA CORE IS NOT REAL SILICON.

The verdict is reported in THREE buckets, not two, because two of them are
about the scaffolding and only one is about the port:

  * PASS (27)
  * MIRROR ARTEFACT (9) -- the case tripped a window guard, so the body wrote
    outside the mirrored region. Says nothing about the decompiled code.
  * GENUINE MISMATCH (3) -- guards intact, value wrong. Only these are
    candidate port defects. `FUN_00003f32` is 1 pass / 3 mismatch and is the
    one worth reading next.

Collapsing artefacts into failures would have reported 12 broken functions
when the count of possibly-broken ones is one.

What the funnel costs, over 192 vectored functions: 86 have an incomplete call
closure, 44 have no decompiled body, 20 have no constraining write, 12 reach
hardware, 9 dereference a pointer whose VALUE came from memory, 1 dispatches
through a code-pointer table. **Rejecting every function that CALLS something
cost 130 of 192** -- emitting the closure is what recovered them.

Four scaffolding bugs, each of which produced confident false failures:

1. **A shadowing pseudo-register local.** Ghidra declares `int unaff_A6;`
   INSIDE the body, shadowing the global the case setup assigns, so the
   function reads an uninitialised base pointer. This alone was 17 of the
   mismatches: `clamp_x_velocity` returned its input unchanged when its
   arithmetic is right by hand (0x178 + -8 = 0x170). `tools/cport_prep.py`
   already drops 1,743 of these; the self-test had to do the same.
2. **A mirror offset must be CONGRUENT MOD 4 to the address it stands for.**
   Guard bytes pushed windows to odd offsets (0x00FF, 0x02D7), so every word
   access through a base pointer was misaligned on the SH-2 and
   `clamp_x_velocity` read back 0xFF78. Align the window base down to 4 and
   the offset up to 4.
3. **An A-register is a `byte *`, not an `int`.** Bodies use it both as
   `unaff_A6 + 0x16` and `unaff_A6[0x21]`; only the pointer type compiles for
   both, with the byte offsets the 68K used.
4. **A MISSING INPUT MUST STOP THE GENERATOR.** `/tmp/datwidth.json` (Ghidra's
   inferred widths) was loaded through a silent `except: pass`, so an absent
   file declared EVERY symbol 1 byte -- including the 32-bit LCG seed -- and
   produced 23 "genuine mismatches" from working ports. It now exits with the
   command to regenerate it. The oracle's own widths are NOT a substitute:
   they are ACCESS widths, and the oracle writes the 32-bit seed as two word
   halves, reporting width 2 for a 4-byte symbol.

Also: entry D-registers are arguments and are now set from the oracle state,
and a case whose base register cannot be mapped is SKIPPED rather than run
with a zero base -- such a case cannot pass and cannot fail informatively.

**THE CEILING IS ROM LAYOUT, NOT THE METHOD.** Adding functions grows `.text`,
which pushes the `.ramtext` ROM image into `.tilesmd` ("move the blob --
mars.ld + md_main.c, make tilesmd-addr"). 13 candidates is the most that links;
the last attempt overflowed by 259 bytes. 21 functions / 2,333 B of mirror FIT
the region guard and are blocked only by that collision, so moving the blob --
Mike's call, it changes the line's layout -- is what lifts the cap. Nine more
functions need a runtime address-translation helper instead of a wider window,
because they dereference pointers read from memory (`object_dispatcher` is one).

## The barcode readout was not self-identifying (2026-09-27)

`m_main.c` defines `RIGB5` **sixteen** different ways depending on the build's
flags. `tools/rig_barcode.py` printed byte 5 as `sh_pub` unconditionally, and
byte 0's `0xA5` is a fixed magic, not a mode tag -- so a readout carried no
evidence of which of the sixteen meanings applied.

What that cost, measured the same day: a PORTTEST rom whose byte 5 was the
self-test pass count read `sh_pub 3`, and 3 was accepted as "3 cases passed"
when PORT_TEST's counter was actually 0 and the 3 was an unrelated packet
count. The reading was wrong and nothing in the output could contradict it.

The decoder now REFUSES TO NAME bytes 5 and 6 unless told the mode: default
prints `b5`/`b6`, and `--mode porttest|rate|bench|blit|pick|isr|task` labels
them. Add a mode entry whenever you add a `RIGB5` arm.

**The general rule: an instrument whose output cannot say what it measured will
eventually be read as measuring something else.** Every FPGA speed and verdict
number this project has is a byte 5 or byte 6 reading. Prefer a self-describing
readout; where the wire format cannot carry a tag, make the decoder decline to
guess rather than print the most common label.

Related fragility, found in the same audit and NOT yet bitten: both
`tools/cport_prep.py` and `tools/gen_selftest.py` load Ghidra's width table from
`/tmp/datwidth.json` -- a path that does not survive a reboot. `gen_selftest.py`
now exits when it is missing; `cport_prep.py` still falls back to `{}`, which
would silently declare all 771 functions' globals 1 byte wide. Verified
2026-09-27 that the current generated port declares `uint _DAT_fffff014`, so the
table WAS present for the run that produced it. Move the table into the repo (it
is Ghidra-derived, not ROM-derived) rather than relying on /tmp.

## -fno-strict-aliasing is REQUIRED for decompiled code (2026-09-27)

Every Ghidra body type-puns: the self-test's case setup stores a cell through
`volatile ushort *` and the body loads the same cell through `int *`. That is a
strict-aliasing violation, so at `-O2` gcc may assume the two accesses cannot
alias and fold the load to a value it believes still holds -- the zero that
`wram_arm()` wrote.

Measured: `FUN_00003f32` (68K 0x03F32, one line:
`*(int *)(A6+0x10) = *(short *)(A6+0x1a) * 0x100 + *(int *)(A6+0x10)`) is
CORRECT -- all four oracle vectors verified by hand -- and reported 3 of 4 FAIL,
each read-back consistent with its base having read 0. Its FIRST vector passed:
the same code, folded at one call site and not another, which is what makes this
class of bug look like a per-vector data problem instead of a compiler
assumption. Adding the flag took the self-test 27 -> 30 of 39 and left **zero
genuine mismatches**: every remaining non-pass is a mirror artefact.

Now on the PORTTEST probe (Makefile) AND on `tools/cport_build.sh`, which
compiles all 771 functions. It had been building the whole port at `-O2` without
it, so any correctness number taken from that object before this commit is
suspect.

**Rule: decompiled C is not ordinary C.** It reads one address at several
widths by construction, which is precisely what strict aliasing licenses the
compiler to disbelieve. Never rank or verify a decompiled body without the flag.

## A probe rom can be overwritten between the measurement and the deploy

`rom/s16.32x` changed between the ares measurement and the `cp` to a probe name:
a plain `make` (the auto-commit hook) replaced it, so `rom/night/pt17.32x` was
the LINE, not the self-test build, and went to the rig under a probe name. It
was caught only because the barcode read `NO BARCODE` and `_end` had moved
0x060189e0 -> 0x06017df8.

Gate every probe deploy on the rom carrying the probe, not on having asked for
it -- `grep -q -- -DPORT_TEST .build_flags`, plus the symbol in the elf
(`sh-elf-nm rom/s16.elf | grep port_test_out`), BEFORE `mister_push.sh`. Copy to
the probe name in the same command as the build.

### FMDL: the transport costs 56-57% of the frame, and that derives the cadence we have always measured (2026-09-27)

The harness has a name now -- **framebuffer-marshalled display list**, every
word an existing term, because the mechanism is a beam-raced cross-ISA command
buffer and only the SUBSTRATE is unusual: the transport medium is the display
surface itself, time-shared by the same FM bit that governs display access.
Forced, not chosen -- the 68K cannot address SH-2 SDRAM, so the framebuffer is
the only large shared window. docs/design/FMDL.md.

MEASURED by the test cartridge, 736 real words through the real packet base
0x851A00, in lines of a 262-line frame:

                        ares   MiSTer
    assemble             55      57
    blast                92      94
    assemble + blast    147     151
    lift                 68      71
    share of the frame   56%     57%

**And it derives the cadence.** The frame timeline puts the game at 147 lines
light and 185 heavy; add the transport and a frame wants 294-336 lines against
262 available, i.e. 1.12-1.28 vints, i.e. 47-53% of 60 Hz. That is the 50-70%
this project has measured all along, reproduced by an instrument built for
something else and never fitted to it.

**So the ceiling is the transport's SHAPE, not the game's side.** No amount of
shaving game work buys a single-vint frame while half the frame goes to moving
the list. Fewer, denser records is the lever -- which is what TILESLIM's 2-word
record was for, and why the 688-word packet body being the wall mattered.

## The offline L1 harness: 1 verified -> 4, and 74 results reclassified (2026-09-27)

`tools/l1_verify.py --all` over the 122 functions with replayable vectors:

    BEFORE  CRASH=28 FAIL=10 HANGS=1 PASS=1 PASS-UNCONSTRAINED=7 WONT-COMPILE=75
    AFTER   CRASH=7  FAIL=20 HANGS=1 PASS=4 PASS-UNCONSTRAINED=14
            UNMAPPED-POINTER=24  UNMODELLED-RETURN=2  WONT-COMPILE=50

Verified: `FUN_00003f24`, `restore_position`, `animation_frame_fetch`,
`collide_box_b`. The fixes came from the SH-2 self-test built the same day --
the two harnesses were hitting the same bugs from opposite ends.

What moved, and why each was NOT a decompilation fault:

1. **The forward-declaration heuristic was one bug wearing two hats** (25 of 75).
   The rule was "uint if `uint ` appears in the first 40 chars, else void", which
   declared `static void zoom_scale_lookup();` over a definition returning
   `ulonglong`. That is the conflicting-types error directly AND
   `uVar1 = FUN_00003cb6();` "assigning from incompatible type 'void'" as its
   knock-on. Take the signature from the definition; never from a heuristic.
2. **Nothing seeded the pseudo-registers.** The driver set no A- or D-register
   from the oracle's entry state, so every function taking its input in a
   register ran on garbage -- and `unaff_A6` was additionally SHADOWED by
   Ghidra's own `int unaff_A6;` local. A seeded A-register points into `MEM`,
   which is what stops it faulting.
3. **`-fno-strict-aliasing`** -- see the entry above; a correct function reports
   FAIL without it.
4. **`UNMAPPED-POINTER` (24) IS NOT A CRASH AND NOT A DEFECT.** These dereference
   a pointer whose VALUE came out of memory: a raw 68K address used as a host
   pointer. macOS reserves the low 4 GB (`__PAGEZERO`), so no mapping makes a
   bare 68K address valid. **This is FULL-PORT.md 6(a), state placement, and the
   count is the measurement of that question's size: 24 of 122.** The target has
   the same problem (SDRAM is at 0x06000000), so it is a PORT design question,
   not a harness one. Calling it CRASH hid what it was.
5. **`UNMODELLED-RETURN` (2) cannot be scored either way.** Ghidra typed a callee
   `void` while a caller uses the value the 68K left in D0. Promoting the
   signature makes the closure compile, but the value is not recoverable from the
   decompilation -- a PASS would be luck and a FAIL would be our own missing
   model.

**PASS-UNCONSTRAINED went 7 -> 14, and that is a warning, not progress.** More
functions compile, so more reach the vacuity check. A vector set with no writes
cannot tell a correct function from an empty one. Those 14 need richer capture,
not a verdict.

## The offline L1 harness was byte-swapping every multi-byte cell (2026-09-27)

`tools/l1_verify.py` runs on the HOST, which is little-endian arm64. It seeded
`MEM` byte by byte in 68K (big-endian) order -- correctly, as the oracle recorded
-- and the decompiled bodies then read those cells AT NATIVE WIDTH. Measured
directly: bytes `CF 89 03 5A` read through `*(uint *)` give **0x5A0389CF**, not
0xCF89035A. **Every FAIL involving a 16- or 32-bit cell was an artefact of the
harness.**

**This is the whole of the "rng_next's decompilation is wrong by one bit"
report.** rng_next now PASSES 2/2 offline and 2/2 on the SH-2. There was never a
carry or rotate modelled as an add.

The fix is not a byte-swap -- it is to never go through bytes. Seed and compare
at the CELL'S OWN WIDTH and the host's byte order cancels out entirely, because
the same width is used writing and reading. Three widths have to agree for that
to work, and each one cost a regression to learn:

1. **The width the BODY uses wins, not the oracle's access width.** They differ:
   rng_next's state is ONE 32-bit cell at 0xFFF014 and the 68K writes it as TWO
   WORDS. Seeding two native 16-bit values and letting the body read 32 bits
   composes them in the HOST's order.
2. **A cell reached through a BASE POINTER has no named symbol**, so the
   declared-width table knows nothing about it. Defaulting those to 1 byte
   regressed `clamp_x_velocity` and `clamp_y_velocity` from PASS to FAIL. Parse
   the width out of the body's own `*(short *)(unaff_A6 + 0x14)` and fall back to
   the oracle's width, never to 1.
3. **`FUN_00003f32` needed both** -- the oracle wrote its cell as two words while
   the body reads one int, AND it is reached off a base register.

**The SH-2 self-test never had this bug, for two independent reasons: the SH-2 is
big-endian, and it seeds at width.** That is why it was right when the host
harness was wrong, and it is the argument for keeping both: a single harness has
no way to discover its own byte order is wrong.

## Two independent harnesses now agree, which is the point

    tools/l1_verify.py --all   PASS 1 -> 15 over 124 functions
    BEFORE  CRASH=28 FAIL=10 HANGS=1 PASS=1  PASS-UNCONSTRAINED=7  WONT-COMPILE=75
    AFTER   CRASH=7  FAIL=12 HANGS=1 PASS=15 PASS-UNCONSTRAINED=14
            UNMAPPED-POINTER=23 UNMODELLED-RETURN=2 WONT-COMPILE=50

All EIGHT functions with an SH-2 hardware verdict now get the same verdict from
the host harness. Zero disagreements. Before today the two instruments
contradicted each other on five of them and the host one was wrong every time.

`--all` also no longer requires a recorded memory READ. That filter was right
when nothing seeded the registers -- a vector with no reads had no input. Now the
entry A- and D-registers are seeded, so a function taking its input in registers
is fully testable: `FUN_00006646` and `FUN_0000673e` both PASS 4/4 and neither
was in the sweep.

FAIL is now 12, not the 20 an inflated run reported. Those 12 are the first
candidate decompilation defects in this project that are not explained by a
known harness fault -- triage them before quoting them.

## Triaging the L1 FAIL bucket: two more harness faults, two real defects (2026-09-27)

    CRASH=7 FAIL=11 HANGS=1 HOST-ENDIAN-LIMIT=1 PASS=15 PASS-UNCONSTRAINED=14
    UNMAPPED-POINTER=23 UNMODELLED-RETURN=2 WONT-COMPILE=50      (124 functions)

**8 of 8 functions with an SH-2 hardware verdict agree with the host harness.**

Found while triaging, each having produced 0-of-N FAILs on correct code:

1. **THE PROGRAM ROM IS PART OF THE INPUT STATE.** The oracle's watchpoints cover
   work RAM, so a read of a TABLE IN ROM is never recorded and never seeded --
   while the entry registers point straight at those tables
   (`animation_peek_next` enters with A0 = 0x004DCE, program ROM). With MEM
   zeroed, such a function reads zeros and fails every vector, which looks exactly
   like a bad decompilation. `roms/altbeast/prog68k.bin` now loads at 0x000000.
2. **`extraout_AN` IS WHAT A CALLEE LEFT IN AN, not the outer function's entry
   value** -- 7 of the 12 FAILs used one. `animation_peek_next` enters with
   A0 = 0x004DCE while the `FUN_00006742` it calls EXITS with A0 = 0x0075B4.
   The oracle records those inner calls, so the exit value is available; taken
   only when every recorded inner call agrees, since nothing pairs a specific
   inner record with a specific outer vector.
3. **`HOST-ENDIAN-LIMIT` is IRREDUCIBLE on this machine and it is not a defect.**
   The ROM image is loaded as raw 68K big-endian bytes while every seeded cell is
   written at its own width, so MEM carries TWO byte-order conventions. A 16- or
   32-bit copy out of ROM comes out swapped: `animation_peek_next` computes
   0xFFC050 = 0xF808 correctly and 0xFFC052 = 0x00C4 where the oracle has
   0xC400, from `*(undefined2 *)(A6+0x52) = *(undefined2 *)(extraout_A0+0xc)`.
   **No host-side care fixes this; the SH-2 harness does not have it, because the
   SH-2 is big-endian and ROM and cells share one convention.** These functions
   must be verified on the target. That is the second independent reason to keep
   both instruments.
4. **A regex alternation that omitted `extraout_` hid item 3 entirely.** The
   check never fired and the width of every callee-register-based cell went
   uninferred. The statement that exposed it reads through exactly that register.

**Two of the 11 remaining FAILs are REAL: `FUN_000039c8` and `FUN_00003a00`, and
Ghidra says so itself** -- both carry "Globals starting with '_' overlap smaller
symbols at the same address". In `FUN_000039c8`, `_DAT_fffff138 =
_DAT_fffff132` needs the WORD at 0xFFF132 (0x102E) while the next statement needs
the LONG (0x102EC800). One C macro cannot be both widths, so the emitted C is
wrong whatever the harness does. These need a decompiler-level fix -- split the
overlapping symbol -- not a harness one. They are the first confirmed
decompilation defects in this project.

## L1 fully classified: 124 functions, no opaque bucket (2026-09-27)

    PASS=15                  verified against the oracle
    PASS-UNCONSTRAINED=13    vectors contain no writes -- NOT a port
    FAIL=9                   candidate defects (1 confirmed, see below)
    UNMAPPED-POINTER=22      FULL-PORT 6(a) state placement
    NEEDS-POINTER-TABLE=16   FULL-PORT 6(d) indirect dispatch
    CLOSURE-INCOMPLETE=20    a callee Ghidra never decompiled
    WONT-COMPILE=14          residual dialect
    CRASH=6  UNRESOLVED-REGISTER=3  UNMODELLED-RETURN=2
    NEEDS-STRING-DATA=2  NEEDS-CODE-LABEL=1  HANGS=1

`WONT-COMPILE=75` at the start of the day was one bucket that said nothing
actionable. It is now 14, and the rest name what the PORT needs rather than what
the compiler complained about. Two of the classifications are direct
measurements of FULL-PORT.md's own open questions:

**NEEDS-POINTER-TABLE = 16 of 124 (13%).** These reference `PTR_FUN_*` /
`PTR_DAT_*` dispatch tables the harness does not emit. **Read this carefully
before treating it as the 6(d) risk**: 6(d)'s danger is a fallback INTERPRETER
for dispatch targets that are not statically known, and F0(d) answered that
favourably -- the census recovered all 249 targets. So 16 functions needing the
tables MATERIALISED is emission work, not an interpreter. The number to re-cost
on is unknown targets, and that is still zero.

**UNMAPPED-POINTER = 22 of 124 (18%)** is the size of 6(a), state placement.

Two cheap fixes worth recording because both were silent:

1. **Ghidra disambiguates overlapping symbols with a `_N` suffix**
   (`DAT_fffff032_1`) and the symbol regex required the hex at the END, so those
   were never declared at all. The regex now accepts the suffix and `sym_addr`
   strips it -- and returns None rather than throwing for a symbol that carries no
   address, which the widened regex started matching.
2. **clang 15+ makes `-Wint-conversion` an ERROR and `-Wno-error=int-conversion`
   does not demote it here** -- `-Wno-int-conversion` does. 18 files failed on
   that flag alone, all from the `byte *` pseudo-registers meeting Ghidra's freely
   punned ints.

**Confirmed decompilation defect: `FUN_000039c8`.** Ghidra warns on it itself
("Globals starting with '_' overlap smaller symbols at the same address"):
`_DAT_fffff138 = _DAT_fffff132` needs the WORD at 0xFFF132 (0x102E) while the
next statement needs the LONG (0x102EC800), and one C macro cannot be both.
`FUN_00003a00` carried the same warning but reclassified to HOST-ENDIAN-LIMIT
once the byte-swap test became empirical -- a warning is a hint, not a verdict.

**`--why` now makes a FAIL explain itself**: every mismatching cell with got vs
want, an EXACT BYTESWAP flag, and an untouched-input flag, plus a
`[n swapped, n truly wrong]` count on every FAIL line. The swapped/wrong split is
what separates the host's byte-order limit from arithmetic, and it is measured at
runtime -- the earlier static "is `want` byte-symmetric" test was meaningless and
hid the real classification completely.

## 24% of the corpus can ONLY be verified on the target, and that changes the plan

    PASS=15  PASS-UNCONSTRAINED=15  FAIL=11  UNMAPPED-POINTER=29
    NEEDS-POINTER-TABLE=19  WONT-COMPILE=14  CRASH=7  UNRESOLVED-REGISTER=6
    UNMODELLED-RETURN=3  NEEDS-STRING-DATA=2  HOST-ENDIAN-LIMIT=1
    NEEDS-CODE-LABEL=1  HANGS=1                            (124 functions)

`CLOSURE-INCOMPLETE` is now ZERO -- all 20 were in the corpus the whole time, and
two closure bugs hid them:

1. **The call regex matched EMPTY parentheses only** (`\w+\s*\(\s*\)`), so a callee
   invoked with an argument was silently dropped from the transitive closure.
   Ghidra usually emits `f()` for this program because it has no calling
   convention, which is exactly why the narrow form looked sufficient.
2. **A function referenced as a POINTER has no parentheses at all.**
   `*(code **)(unaff_A6 + 1) = FUN_0000566e;` stores a handler address -- the
   object-handler pattern, so it is common in precisely the code the port is
   about. The closure now follows bare references to known function names.
3. **`sym_addr` ate `DAT_00000001`.** Stripping Ghidra's `_\d+` overlap suffix
   FIRST removes `_00000001`, because an all-decimal address is indistinguishable
   from a `_1` suffix; 'DAT' has no hex tail, so the symbol was dropped and the
   body would not compile. Try the full tail first, the suffixed form second.

**THE STRATEGIC FINDING: 30 of 124 functions (24%) cannot be verified on this
host at any effort.**

* `UNMAPPED-POINTER` (29) dereferences a pointer whose VALUE came from memory.
  Rebasing it onto `MEM` is not merely awkward, it is IMPOSSIBLE: `MEM` lives
  above 4 GB (macOS `__PAGEZERO` reserves the low 4 GB on 64-bit), so a host
  pointer does not FIT in the 32-bit cell the program stores it in. There is no
  host-side fix on this machine. A 32-bit Linux address space would be one.
* `HOST-ENDIAN-LIMIT` (1) is the ROM-byte-order case.

Both vanish on the SH-2: it is big-endian, and it is a 32-bit machine where a
work-RAM pointer fits the cell it came out of.

**So the rom-embedded SH-2 self-test is not a redundant spot-check -- it is the
only instrument that can ever reach about a quarter of the corpus.** Its 13-
function ceiling is a ROM LAYOUT limit (`.text` growth pushes the `.ramtext`
image into `.tilesmd`), which makes moving that blob the binding constraint on
verifying that quarter, not a nice-to-have. This reverses the ranking recorded
earlier today, when the self-test looked like the slow path and the host harness
looked like the one that scales. It scales to 76% of the corpus.

## A probe that needs the bus can never read virgin memory (2026-09-27)

TESTCART's T20 claimed to read uninitialised Z80 RAM. Reaching Z80 RAM needs
the 68K to hold the Z80 bus; the grant only comes once Z80 reset is RELEASED
(T1E proves it, and both md_start.s:175 and 32x-builder's SMS harness hit it
independently); and a released Z80 executes. With SP = 0 and 0xFF in RAM it
runs RST 38h, whose pushes wrap SP to the top of the 8K space and walk the
stack down through 0x1FFE, 0x1FFC, ... over the very address the probe sampled.

ares read 0xAFFF, the FPGA 0xAF00, and it presented as a memory-init
divergence. It was the Z80's own stack, and the delta was how many instructions
each machine let it retire. Fixed by sampling 0x0800/0x1200 -- clear of a PC
walking up from the reset vector and a stack walking down from the top -- where
both machines read 0x0000.

**The rule: when access to a resource requires waking the thing that owns it,
"before" is not observable.** Say what the measurement actually is (RAM after N
microseconds of execution) or move the probe somewhere the owner cannot reach.

T20 shipped without a control, which this file already names as the way to
produce confident nonsense. The cross-machine delta caught it -- the
characterisation design working, not a substitute for the control.

## 68000 cycle counts predict the cart's timing tests (2026-09-27)

TESTCART's REF column was one ares run, which catches change but not both
machines being wrong. Three of the timing tests now have DERIVED expectations,
from the loops as they appear in the linked ELF (LTO inlines them into `main`;
`objdump -D rom/md_start.elf`, they are in .data at ~0xFF2670):

    T08 word  MOVE.W (xxx).L,Dn 16 + MOVE.W Dn,(d16,An) 12 + SUBQ.W 4
              + Bcc.B taken 10 = 42 cyc  x1024 / 488.57 = 88.0 lines (reads 90)
    T13 byte  same 42                                    = 88.0 lines (reads 90)
    T14 long  20 + 16 + 4 + 10 = 50 cyc  x1024 / 488.57  = 104.8 lines (reads 107)

The residual is CONSTANT at ~2 lines across all three rather than scaling with
the loop's cost, so it is fixed harness overhead (wait-line entry, the trailing
counter read, the `jsr`) and not a broken cycle table.

Two consequences. First, these tests are now specifications rather than
baselines, marked C in the cart's provenance column. Second: **the FPGA reads
T08 at 0x5D = 93 against ares' 90 -- +3 lines on a loop that touches no cart
and no VDP, executing from work RAM.** The recorded explanation for FPGA
slowness is the core's ROM DTACK wait (IF.sv USE_ROM_WAIT=1), a CART-read
penalty. This loop reads and executes work RAM. So the core taxes plain 68K RAM
access too, by a similar margin (+3 vs cart's +5). The ~2-line overhead is
identical on both machines and cancels in the delta, so the +3 stands
independent of the residual.

## Name the rig; there is no default (2026-09-27)

Mike, twice: "you are using the mister.office .local instead of the
mister.tv.local", then "you did it again. Stop doing that."

FOUR tools defaulted to mister.office.local -- tools/mister_push.sh,
rig_value.py, rig_barcode.py, rig_blackcells.sh -- and office belongs to the
DECOMPILE thread while mister.tv.local is the builder's. A default means every
un-flagged invocation fires at one rig, and the misfire is INVISIBLE from here:
the launch API answers "game started" whichever machine took it.

All four now REFUSE to run without an explicit rig (--name, or MISTER=) and
print both names when they do. An un-flagged run fails loudly instead of
landing on somebody else's session.

**The general rule: when two targets are equally valid and only the operator
knows which, a default is not a convenience, it is a silent wrong answer.**
Fail closed and make the caller name it.

## The SH-2 self-test: .porttest lifts the ROM cap, and two measurement bugs (2026-09-27)

13 functions / 45 vector cases, **36 pass**, up from 11/39/30. ares frames 300,
400 and 500 all read run 45 / passed 36. FPGA re-verification is PARKED at Mike's
word (2026-09-27 afternoon, laptop away from the rigs).

**`.porttest` (mars.ld) is where the generated bodies now live** -- the measured
free ROM gap between `.tilesmd`'s end and `.palscenes` at 0x2F8000, about 62 KB.
Additive: nothing on the line moves. The cap was `.text` growth pushing the
`.ramtext` ROM image into the `.tilesmd` blob, and that cap matters more than it
looks, because 24% of the corpus can ONLY be verified on the target. Proven on
hardware before the rigs were parked: pt21, the section enabled, read 30 on the
FPGA.

**A STUB THAT TURNS A LINK ERROR INTO A HARDWARE CRASH IS WORSE THAN THE LINK
ERROR.** gcc emits `abort()` for a path it proves unreachable, and freestanding
has no libc, so the link fails on `_abort`. Stubbing abort() to record and return
is undefined behaviour -- gcc treats everything after the call as dead code.
Measured: with the stub, a 16-function build ran 42 of 54 in ares and produced
**NO BARCODE AT ALL** on the FPGA (pt20, re-confirmed on an idle rig), while the
same set minus the aborting function read 30 on the rig (pt21). It also silently
dropped the trailing summary stores, so the barcode reported ZERO passes on a
working test. The stub is gone; the link error is the guard, and the response is
to lower `--max` until it links.

**A COUNTER WRITTEN AS A SWEEP RUNS IS NOT A READABLE COUNTER.** Making
`port_test_out[1]`/`[2]` incremental (so a crash mid-sweep still showed progress)
made every reader sample a partial count: the SAME rom reported **27 in an ares
dump and 36 off the rig barcode**, and for an hour that looked like an
ares-versus-FPGA disagreement worth investigating. It was two sampling points.
`[9]`/`[10]` are now written only when a sweep COMPLETES, `[8]` counts completed
sweeps, and the barcode reads `[10]`. Verified sample-independent across three
ares frames.

Rule: when an instrument is read asynchronously, publish a value that cannot
change under the reader. A live counter is for liveness, never for a verdict.

## A THIRD of the corpus is target-only, and low-mapping the host is dead (2026-09-27)

    PASS=16  PASS-UNCONSTRAINED=17  FAIL=13  UNMAPPED-POINTER=42
    WONT-COMPILE=19  CRASH=7  NEEDS-CODE-LABEL=3  UNMODELLED-RETURN=3
    NEEDS-STRING-DATA=2  HOST-ENDIAN-LIMIT=1  HANGS=1        (124 functions)

`NEEDS-POINTER-TABLE` and `UNRESOLVED-REGISTER` are now ZERO: `PTR_FUN_*`,
`PTR_DAT_*` and `s_*` are ADDRESS-NAMED like any other symbol, so they declare
the same way -- the bodies only take their address (`(int)&PTR_DAT_00001834`),
which needs an lvalue at that MEM address and nothing more. `register0x...` is a
placeholder Ghidra emits for a register it could not resolve; it only has to
exist.

**`defs.c` was never linked.** `ghidra_rt.h` DECLARES Ghidra's intrinsics
(`bcdAdjust`, `halt_baddata`, the CONCAT/CARRY family) and `cport_prep.py`'s
`defs.c` DEFINES them, so 15 files compiled and then failed at the LINK. Same
single-source rule as the width table: use what the port build already produced.

**MEASURED DEAD -- do not retry: mapping the 68K address space at its own
addresses on this host.** The idea is sound (then a 68K pointer read out of memory
IS a valid host pointer, and `UNMAPPED-POINTER` disappears), and macOS's 4 GB
`__PAGEZERO` reservation is only a linker setting. But `-Wl,-pagezero_size,0x1000`
and `0x4000` both produce a binary the kernel KILLS outright -- `mmap` at 0x1000
and at 0x4000, exit 137 (SIGKILL) both times, before any output. arm64 macOS does
not permit it. A Linux container with `vm.mmap_min_addr` lowered would be the way
if this is ever worth revisiting.

**So UNMAPPED-POINTER = 42 of 124 (34%) is irreducible on this host** -- it grew
from 24% as more files got past their compile errors. A third of the corpus can be
verified only on a 32-bit big-endian machine, which is the SH-2. The rom-embedded
self-test is therefore not the redundant instrument; it is the only one that can
ever reach that third, and its ROM-layout ceiling (now lifted by `.porttest`, and
bounded next by the `.bss` mirror budget) is the constraint that matters.

## `_abort` was a NULL base pointer, and it is an LTO artefact (2026-09-27)

**Nothing in the 68K program produces `abort()`.** It is gcc's own codegen for a
path it proves unreachable, and `_` is the SH-ELF symbol prefix. Compiling
`sh_src/port_selftest.c` with `-fno-lto` produces NO abort reference at all, so it
is specifically whole-program LTO that finds the path.

The path: `static byte *unaff_A6;` is ZERO-INITIALISED, and with `-flto` gcc sees
across the program that a body can dereference it while still NULL -- provable
undefined behaviour, which it lowers to `abort()`. Initialising the pseudo-register
pointers to the mirror (`= wram`) removes every one of them. Every case still
assigns the registers it needs before calling anything; this only makes the
pre-assignment state defined rather than undefined.

Two wrong theories were tested and discarded on the way, both cheap:
`restore_position` reaching `unaff_A6 + 0x68` past a window (the record span IS now
derived from the body's largest constant offset rather than a guessed 0x4F, which
is right on its own merits), and a provable out-of-bounds access on the fixed-size
`wram[]` array -- gcc emitted no array-bounds warning, which ruled it out.

**Result: 21 functions / 69 vector cases, 37 passing** (was 13/45/36).

Also fixed, and it is what unblocked the jump: **Ghidra's intrinsics are not
missing callees.** `CONCAT11` alone appeared in 293 closure walks and each counted
as "call closure incomplete", which is why that bucket sat at 86 and would not
move. They are MACROS in `ghidra_rt.h`, generated by `cport_prep.py` and now
single-sourced from `docs/proof/cport/ghidra_rt.h` -- inlined, never re-derived,
because a subtly wrong `CARRY1` or `SUB42` makes a CORRECT function report FAIL.
Closure-incomplete went 86 -> 41. `defs.c` stubs `halt_baddata`,
`halt_unimplemented`, `stop` and `bcdAdjust` to `return 0`, which is NOT their
semantics, so a closure reaching one is now REJECTED with that reason rather than
verified against a fiction.

**Next, and it is the same finding the host harness produced: THE PROGRAM ROM IS
PART OF THE INPUT STATE.** `animate_variant` seeds correctly and still fails
because its two recorded word reads at 0xFFC924/26 compose to 0x00010220 -- a
68K ROM address, the animation table. The mirror holds no ROM, so it reads zeros.
On the SH-2 the cart already contains the 68K program, so this is a rebase onto
the cart copy rather than a copy, unlike the host.

## A core's RTL is a derivation, not an oracle (2026-09-27)

TESTCART's COLOUR page graded T29 from the MiSTer core's Verilog:
`BMMR_MASK = 16'h80C3` (32X_PKG.sv:139), writes ANDed with it (VDP.sv:133-134),
readback substituting `~PAL` into bit 15 (VDP.sv:160). Writing 0xFFFF to the
bitmap mode register must therefore read back 0x80C3 on NTSC.

ares returns 0xB3FF, and the test FAILED on its first run.

The RTL reading may still be right -- but it is a reading of ONE
IMPLEMENTATION, and this project already established that a MiSTer-core
behaviour can be the core's rather than the machine's (the USE_ROM_WAIT
verdict: "the week's FPGA-vs-ares gap was mostly the emulator"). Grading
against it is the same conformance mistake as grading against a capture, one
step further from hardware than it looks.

**The rule: RTL earns provenance R for an EXPECTATION. It does not earn a
GRADE.** Grade only tautologies (what was written must read back) and
behaviours two independent implementations already agree on. Everything else
reports its value and lets the machines disagree on screen.

The same page's T28 shows the method working: the RTL says VDPPAL is an
unmasked 16-bit RAM, the prediction was that palette bit 15 survives a write,
and it does on ares. Four derived predictions on that page, three held, one
did not. Writing them down is what made the score visible.

## The SH-2 self-test reaches the ROM: 46 of 68 cases, 11 functions clean (2026-09-27)

`animation_peek_next` went 0/4 to 3/4 and `animation_frame_fetch` to 4/4 on one
change, because THE CART ALREADY HOLDS THE 68K PROGRAM and the test was not using
it. Three pieces:

1. **A 68K ROM address is `0x02300000 + A` from the SH-2.** mars.ld's
   `.gamehigh 0x02300000 : AT(0x00300000)`, verified by finding prog68k.bin's bytes
   at rom offset 0x310220 for 68K 0x010220. No copy is needed, only an add -- and
   cart and SH-2 are both big-endian, so the byte order that defeats the HOST
   harness is not a problem here. This is why the target instrument reaches what
   the host one cannot.
2. **`extraout_AN` was never seeded at all** -- the A-register regex matched only
   `unaff_`/`in_`, so it kept its initialiser and every body reading through it
   read the mirror's base. `animation_peek_next` enters with A0 = 0x004DCE while the
   `FUN_00006742` it calls EXITS with A0 = 0x0075B4; the oracle records those inner
   calls, and the exit value is taken when they all agree.
3. **The body's access width decides what is a POINTER, not the oracle's.**
   `animate_variant`'s pointer is ONE 4-byte cell at 0xFFC924 that the 68K wrote as
   TWO WORDS, so reading the oracle's widths literally never recognises it as a
   pointer and it is never rebased. Same coalescing lesson as
   `tools/l1_verify.py`, reached from the other side.

11 of 20 functions now pass every vector they have: `rng_next`, both velocity
clamps, `restore_position`, `hitbox_c_expand`, `animation_frame_fetch`,
`FUN_000012ec`, `FUN_00003f32`, `FUN_00006646`, `FUN_0000673e`, `FUN_00009270`.

Progression across the day, all on the same oracle vectors: 3 cases -> 30/39 ->
36/45 -> 39/69 -> **46/68**. Every increase came from fixing the harness, not the
port; the only confirmed decompilation defect so far remains `FUN_000039c8`'s
overlapping symbol.

## The baker and the runtime are two implementations; test them against each other (2026-09-27)

`tools/bake_tilecram.py:md()` generates the MD pen tables on the host;
`sh_src/m_main.c:mdp_quant` consumes them at runtime. Same function, two
languages. When they drift the symptom is not a crash, it is PINK TREES
(LOOP29 196: the baked tables carried md_word()'s field positions where
md_pack9()'s were wanted, so white (7,7,7) read back as (6,5,3)). `md()` has
carried a docstring saying it MUST match the runtime ever since -- which is a
comment, not a test.

`tools/quant_diff.py` is the test, and it does not reimplement the C in Python
-- that would risk re-deriving the same bug. It EXTRACTS the `#ifdef
PAL_QUANT_RAMP` block from m_main.c verbatim by #if/#endif depth, compiles it
twice with the host cc (once each side of the ifdef), runs both over all 65536
S16 colour words and diffs against the baker's own `md()`/`md_pack9()`. The C
under test is the repo's text.

Result: **the linear runtime quantiser agrees with the baker on all 65536
words.** The line is clean. No emulator and no rig -- `make quantcheck`.

**THERE ARE TWO RUNTIME QUANTISERS AND THE SECOND ONE IS A LOADED GUN.**
PALQUANT=1 selects a ramp-aware table (`mdp_q5`) fitted to the MD DAC's
measured output, which deliberately moves 16 of the 32 five-bit input levels
one level down. The baker stays linear. MDROUND=1 -- which IS on the line --
makes the baked per-ROUND tables override the per-SCENE ones. So PALQUANT=1
plus MDROUND=1 ships tables that disagree with the runtime on half the input
levels, and the SH-2's drift check compares a table colour against mdp_quant
of the live word, so every disagreeing pen is freed and re-claimed for ever.
That combination is now a hard `$(error)` in the Makefile with a
PALQUANT_OK=1 escape hatch for deliberate mismatch testing.

The tool also checks m_main.c:3103's own claim ("Sixteen of 32 inputs move one
level down") against the code: 16, all exactly one level down. **A comment
asserting a count is a testable claim -- test it rather than believing it.**

## Two more register-seeding gaps: 48 of 68 (2026-09-27)

1. **Ghidra SUFFIXES a second callee-produced value: `extraout_A0_00`.** The
   A-register pattern ended at the digit, and `\b` cannot match inside `A0_00`, so
   that register was never captured and never seeded -- it kept its initialiser and
   `animate_variant`'s `*unaff_A6 = *pbVar4 | *unaff_A6` OR'd in ZERO. All four of
   its vectors wanted 0x89 at 0xFFC900 and got 0x88: one bit, systematically, which
   is what an unseeded pointer into a table of flags looks like. Also: the register
   INDEX must come from after `_A`, not from the last character -- `extraout_A1_00`
   ends in '0' while its register is 1.
2. **Requiring every recorded inner call to AGREE threw away the only correct
   values.** `animate_variant` needs `extraout_A0` = 0x010223 / 0x01027B across its
   four vectors -- ROM addresses into the animation table -- and the agreement rule
   fell back to the outer function's entry A0 instead. Both lists are sequential
   captures from one run, so the n-th inner record is paired with the n-th vector
   when the counts match, and agreement is kept as the rule only when they do not.
   Falsifiable rather than assumed: a wrong pairing makes cases fail.

**A FAIL NOW REPORTS THE FIRST MISMATCHING CELL, NOT THE FIRST CELL.** The old
read-back showed the first compared cell whatever happened, so for `animate_variant`
and `FUN_0000669c` it showed **got == want on a failing case** and said nothing about
the cause. The slot is now `0xD0000000 | cond-index << 20 | value`. An instrument
that reports a value which is not the one that failed is worse than silent, because
it looks like information.

48 of 68 cases, 11 of 20 functions clean. The remaining nine split by signature:
`FUN_0000e20c` and `FUN_000171fa` never write the cell at all (0/N, cell reads its
zeroed seed); `FUN_000039c8` is the known overlapping-symbol defect;
`animation_peek_next` and `hitbox_a_expand` are 3/4 -- one vector each, the profile
of a pairing or per-vector state problem rather than arithmetic.

## The VDP grants 18 external access slots a line, and it explains the table (2026-09-27)

TESTCART's T0A (1K VRAM word writes) and T16 (1K VRAM word reads) both measure
113 lines even though their loops cost 30 and 42 68K cycles an iteration. A
loop cannot measure the same as a 40%-cheaper loop unless the 68K is not the
limiter.

Counted out of the MiSTer core's Genesis VDP, `rtl/GEN/vdp.sv:1003-1038`, H40
active display: the slot pattern repeats every 16 H_CNT counts and SLOT_CE
fires on odd H_CNT (:1001), so the candidate is H_CNT == 3 (mod 16); over
H_CNT 0..319 that is 20, less the five where H_CNT[5:4] == 2'b11 which become
ST_REFRESH (:1025), leaving 15; plus three explicit H40 externals at
H_CNT[8:1] == 0xA0, 0xA1, 0xE7 (:1033-1037). **18 slots per line.**

VRAM is byte-wide: a VRAM WORD costs two slots, a fill's BYTE costs one. That
single constant predicts five of the cart's rows -- T0A 113.78 vs 113, T16
113.78 vs 113, T0D 28.44 vs 28, T18 14.22 vs 14, T19 28.44 vs 28 -- and
explains a 49-line gap that had sat in the table unexplained: a CRAM word is
ONE slot, so T17's 16.29 accesses a line fit under the budget and it stays
instruction-bound at 62.88 lines, while T0A runs the IDENTICAL instructions and
is slot-bound at 113 because VRAM doubles the cost.

**Why this matters beyond the cart:** the pipeline is the project, and the
budget it is spending is 18 slots a line during active display, 9 of them if it
is moving VRAM words. That is a first-principles ceiling on every
feed-the-VDP scheme, available without a single probe build.

Also established: the cart's harness costs a FIXED ~2 lines (nine
instruction-bound loops all land 1.1-2.2 lines over prediction, not scaling
with the loop), and T0B/T0C landing exactly on their instruction cost is
independent confirmation that **ares charges nothing for a 32X framebuffer
access** -- the FB-write bus-stall floor, arrived at from the other direction.

## 46 of 56 (82%), and a capture limitation that needs the builder (2026-09-27)

11 of 17 functions pass every vector they have. Remaining six: `FUN_000039c8` (4
mismatches -- the confirmed overlapping-symbol DEFECT), `FUN_000056e8` (1/2),
`animation_peek_next` and `hitbox_a_expand` (3/4 each), and `FUN_000171fa` /
`palette_queue_drain` with window-guard artefacts, which are not verdicts.

**PAIRING INNER CALLS TO OUTER VECTORS BY ORDER IS FALSIFIED. It was my guess and
the data killed it.** `animate_variant` has four inner records and four vectors, so
the counts matched and the by-order pairing was used -- and it is wrong. The proof
is self-contained: the oracle read 0xFFC922 = 7 and wrote 6, so it took the same
`else` branch the port does, and therefore `pbVar4 = extraout_A0`; the expected
result has bit 0 CLEAR, which requires a ROM byte of 0x00, but the paired exit A0
(0x010223) holds 0x01. Matching counts do not make an ordering correct.

So a function whose recorded inner-call exits DISAGREE is now rejected as
`extraout value cannot be paired to a vector` -- 8 functions. **The fix belongs in
the capture, not here: `tools/l1_oracle.lua` should stamp every call record with a
sequence id, so an inner call can be attributed to the outer call it happened
inside.** That is a builder-side change and it converts 8 functions directly.

**A stale global made the check inert and it looked present.** The rejection used a
bare `sz` in a loop that has no `sz`, so the name resolved to a leftover from an
earlier loop, the caller range was wrong, and nothing was ever rejected -- while the
code read as if it were working. Third time today that a silently-wrong lookup
(`except: pass` on the width table, `-Wno-error=` not demoting, this) produced
confident wrong output. **A lookup that cannot fail is a lookup that cannot be
trusted.**

## Readback is not display; grade the pixels (2026-09-27)

TESTCART's COLOUR page proves MD CRAM holds all 512 nine-bit pens and 32X CRAM
holds all 256 entries. Neither proves a pen DISPLAYS. A palette that reads back
perfectly and reaches the DAC wrong is exactly the pink-trees failure, and this
project's acceptance gate has always been pixel truth rather than readback.

The cart cannot close that gap itself -- there is no path from the 68K to the
composited output -- so page 6 PAINTS and `tools/tc_pattern.py` GRADES from the
frame. `make pengrid` builds, runs headless ares and verifies in one command.

Design points worth reusing:

- **Sample the cell, not the centre pixel.** The flatness check reads nine
  points across each 3x2-tile block. A centre sample walks straight past a tile
  or name-table fault that leaves the block striped.
- **Make each level appear TWICE.** level = pen >> 1, so every ramp walks its
  eight levels in pairs; a dropped LSB then shows as a step that fails to
  repeat instead of as a colour nobody can name.
- **Put the expectation's source under test.** The verifier parses the DAC ramp
  out of m_main.c rather than restating it, so the comment is graded. ares
  produced 0,52,87,116,144,172,206,255 -- exactly what the source claims. That
  comment is what the whole PALQUANT feature rests on and it had never been
  checked.

Two traps the build hit, both worth the minute they cost. The first render
painted a good label BLACK ON BLACK: the tables draw glyphs in pen 1 over a
pen-0 ground, and in a ramp pen 1 is level 0 (tc_font_pen takes the pen now).
And the grid measured 32px narrower than its own geometry until I noticed pens
0 and 1 are both level 0 -- genuinely black, not missing. **Before calling a
pattern wrong, check whether the missing part is supposed to be the background
colour.**

## Ghidra's underscore prefix IS the width, and the table cannot say so (2026-09-27)

**49 of 56 cases (87%), 12 of 17 functions clean.**

Ghidra distinguishes two symbols at ONE address by a leading underscore, and its
own warning states the convention: *"Globals starting with '_' overlap smaller
symbols at the same address"* -- so `_DAT_x` is the LARGER datum and `DAT_x` the
smaller. **The width table (`f1_datwidth.py`) is keyed by ADDRESS, so it cannot
express both**, and declaring both at the table's width made
`DAT_ffffe800 = 1` a WORD store of 0x0001 where the 68K wrote the single BYTE 1 --
the byte read back 0x00 and three vectors of `FUN_000171fa` reported a mismatch on
correct code. The corpus settles which is which independently of the convention:
line 1269 compares `DAT_ffffe800 != '\0'`, a character.

The pair has to be detected across the WHOLE CORPUS, not per closure:
`FUN_000171fa` uses only `DAT_ffffe800` while `_DAT_ffffe800` appears in other
functions, so a per-closure check sees no pair and learns nothing.

Two more instrument improvements, both because a bucket that does not say WHICH is
not actionable:

* **A guard now returns the 1-BASED WINDOW INDEX** rather than a bare pass/fail.
  "A guard tripped" does not tell you which of six windows to widen; with the index
  it took one run to see `FUN_000171fa` overrunning window 5 at 0xFFE800 and
  `palette_queue_drain` window 6 at 0xFFF676.
* **Every window is padded 0x10 at the top.** The touched set is built from the
  addresses the oracle RECORDED, so a window ends exactly at the last recorded
  byte -- and a body that writes a WORD there, or walks one entry past where the
  trace stopped, lands in the guard. Guard trips went from 4 to 0.

`FUN_000039c8` remains the ONE confirmed decompilation defect, and the underscore
rule makes its shape exact rather than suspected: `_DAT_fffff138 = _DAT_fffff132`
needs the WORD at 0xFFF132 (0x102E, which is what the oracle wrote), while the next
statement needs the LONG (0x102EC800). One name, two widths, in one function -- so
no declaration can satisfy both and the emitted C is wrong whatever the harness
does.

## A third instrument adjudicates what two only disagree about (2026-09-27)

TESTCART's T29 predicted 0x80C3 for the 32X bitmap mode register from the
MiSTer core's RTL (BMMR_MASK = 16'h80C3, 32X_PKG.sv:139). ares returned 0xB3FF,
the test failed, and the entry written that morning concluded "a core's RTL is
a derivation, not an oracle" and degraded it to report-only pending silicon.

The BlastEm fork returns 0x80C3. Prediction exactly.

Two independent implementations agree and **ares is the outlier** -- it does
not mask that register on readback. The RTL reading was right the whole time;
what was missing was not hardware, it was a SECOND OPINION, which was sitting
in ~/src/blastem-0c61d0d95463 the entire session.

**The rule the morning's entry got half right:** RTL alone does not earn a
grade. RTL plus an independent implementation does -- that is exactly the
"two projects agree" bar the Z80 page already uses. And when they agree against
an emulator, the REF column becomes a SPECIFICATION and the emulator announces
its own deviation as a delta, which is what that column was built for.

Mechanics worth keeping: ares-headless and the BlastEm fork accept the
IDENTICAL `--dump wram:ADDR:LEN:FILE`, so one dump spec feeds both and
`tools/tc_compare.py` diffs them with no pixel decoding in between.
`make compare`. BlastEm needs the three 32X BIOS roms in its CWD (plain
CWD-relative fopen); without them the SH-2s never boot and the symptom is
"Unimplemented instruction: 0, pc=0" with no dump file -- which reads like a
broken switch rather than a missing file, and cost twenty minutes.

Also caught: the delta column printed two hex digits, rendering 0xB3FF against
0x80C3 as "3C". **A truncated delta is not a large difference reported badly,
it is a different number.** Four digits when needed.

And the timing rows now have a tiebreak too. BlastEm sits ONE LINE BELOW ares
on every instruction-bound loop (T08 89 vs 90, T14 106 vs 107, T15 41 vs 42,
T1A 72 vs 73) and one line CLOSER to the derivation in every case -- so the
cart's fixed harness overhead is ~1 line, not ~2, and ares adds one more. The
slot-bound rows bracket the 18-slot prediction of 113.78 from both sides:
ares 113, BlastEm 114.

## The PEN rule has two exceptions, and my test was running inside one (2026-09-27)

TESTCART's T1D wrote 32X CRAM during active scan and expected a refusal. It
LANDED on ares and on the FPGA, and I recorded that as "either the rule does
not cover the 68K's path to 0xA15200, or both emulating machines miss it --
real hardware decides".

Neither. ~/src/32XDK/docs/32x-hardware-manual.md:1240:

    "Palette access is possible only during H and V blank."
    "Palette can access whenever the bitmap mode is in the direct color mode,
     as well as during Blank."

T1B runs immediately above T1D and writes 0 to the bitmap mode register --
M1:M0 = 00 = BLANK MODE. The test was writing CRAM with the 32X blanked, which
the manual ALLOWS. The machines were right; the test was exercising the
exception.

The MiSTer core implements the manual exactly, which is how the reading is
confirmed rather than assumed -- rtl/32X/VDP.sv:402-406:

    if (H_CNT == 9'h157+3-1 || VBLK || !MODE[0]) PEN <= 1;
    else if (H_CNT == 9'h017-1)                  PEN <= 0;

`!MODE[0]` is exactly Blank (00) and Direct Color (10); palette access is
gated on PEN at :170.

T1D now sets PACKED PIXEL mode first and reads back in the following vblank
(the gate covers READS too, so reading where the write was refused would
measure the read). **Both emulators still let the write land: ares 5678,
BlastEm 5678.** Manual and RTL both say refused, so **the prediction for the
rig is T1D = 0000**, and neither emulator models this.

**The consequence is bigger than the row.** sh_src cram_paint has no PEN guard.
If the rule holds on silicon, this port's palette writes during active scan are
SILENTLY DROPPED on real hardware -- a hardware-only defect no instrument we
own can show, and one the FPGA would only have revealed once the test stopped
running inside the exception.

**The general rule: when a test agrees with every machine, check that it is
testing what you think.** A rule with exceptions needs the test placed OUTSIDE
them, and "everything agrees" is as often a mis-aimed probe as a settled
question.

## A documented bit layout can be wrong; check it against what ships (2026-09-27)

~/src/32XDK/docs/32x-hardware-manual.md:1169 tables the bitmap mode register
as PAL=15, PRI=13, 240=7, M1:M0=1:0. Every implementation disagrees, and so
does this port:

    sh_src/mars.h:81  MARS_VDP_PRIO_32X  0x0080   (bit 7)
    sh_src/mars.h:84  MARS_240_LINES     0x0040   (bit 6)
    S32X_MiSTer 32X_PKG.sv:139  BMMR_MASK = 16'h80C3  (bits 15,7,6,1,0)
    BlastEm readback of 0xFFFF: 0x80C3

The port sets MARS_VDP_PRIO_32X and the 32X layer does appear in front of the
MD on ares, the MiSTer FPGA and MAME. A transcribed table is exactly the part
of a converted manual that shifts, and the manual's PROSE is unaffected -- :358
"the remaining 1 bit is called a priority-bit (through-bit); pixels indicated
by this bit's color are displayed opposite to the MEGA Drive screen" confirms
the per-pixel composition rule independently.

**Do not "correct" mars.h to match that table.** It would break a working port
on three machines to satisfy one transcription.

## The four 32X technical bulletins, audited against this tree (2026-09-27)

~/src/32XDK/bulletins. All four are one- or two-page developer notices from
1994 and every one of them lands somewhere in this port.

**#9, 68K interrupts & RV bit = 1** -- entire body: "All 68000 interrupts must
be prohibited during the period that the RV bit = 1." The manual puts RV at bit
0 of the DREQ control register, 0xA15106/7: "RV: ROM to VRAM DMA. 0: NO
OPERATION (initial value), 1: DMA Start Allowed. The SH2 side cannot access
the ROM when RV = 1."

**SATISFIED, VACUOUSLY -- AND THAT IS THE FINDING.** Every write to 0xA15107 in
the whole tree is 0 or 4: md_start.s:124 clears it, and md_main.c's live path
writes only 4 (68S on) and 0 (68S off). Nothing sets bit 0, ever. TESTCART T25
reads the register back as 0x0000 on ares and BlastEm, confirming it.

But this tree describes running under an "RV=1 identity map" in a dozen places
-- md_main.c:100, :1464, :1501, :7165, :7517, md_start.s:220 "main sets RV=1
itself" -- and it is on the five-item real-hardware list. **The code does not
do that.** Either the comments are stale or a mechanism was planned and never
landed. Bounding the claim honestly: the cart runs INSTEAD of the game, so its
reading is the boot state; what closes the gap is the grep, not the reading.

**#14, VDP shift bit precaution** -- "the shift bit becomes invalid when the
lower byte of the base address set in the line table is $FF". Read in
2026-09 as **DOES NOT APPLY** because nothing in sh_src or md_src touched SFT,
0xA15182 or 0x20004102. **THAT IS NO LONGER TRUE, 2026-10-15: R60LTPROBE uses
the shift bit, so the precaution is now LIVE and is a constraint on the
free-scroll mechanism below.** Any line-table entry the scroll walks must
never land on a low byte of $FF while the shift bit is set; the walk is one
word a pass, so it WILL cross such a value unless the generator skips it.
Not yet hit. THE TEST IS A CLOSED FORM, NOT A SCAN -- derived against the real
constants (r60_main.c:241, lt[i] = i * (FB_W/2) + 0x100, FB_W 320, FB_H 224) and
brute-forced over every kw: the 160-word row stride makes the 224 entries take only
EIGHT distinct low bytes, {0,32,64,96,128,160,192,224} + kw, so an entry hits 0xFF
exactly when **(kw & 31) == 31**. One comparison, no 224-entry loop. It bites one
offset in every 64 PIXELS of scroll.
IT CANNOT BE DESIGNED AWAY by choosing a different line-table base or row stride: any
base b just moves the forbidden residue to 255-b mod 32, and the stride is fixed by the
320-px format, so a forbidden kw always exists once per 32. The +-1 px substitution is
the only fix.
THE SHIFT BIT MOVES THE DISPLAY THE OTHER WAY, measured 2026-10-15 (xo2/xo3: k 37 read
+1 px against the zero build, k 38 read 0). An ODD offset is therefore ONE MORE WORD with
the bit, 2*(kw+1)-1, not kw*2+1. So the word to test against bulletin 14 is the
INCREMENTED one, and in pixel terms the forbidden offsets are p = 61, 125, 189, 253 --
**p ≡ 61 (mod 64)**, not 63. The committed code keys on the incremented kw and is right
about that.
BUT THE FIRST GUARD OVERSHOT, AND THE SHAPE OF THE OVERSHOOT IS THE LESSON. r60_main.c
on advisor-bisect read `if (sh) { kw++; if ((kw & 31u) == 31u) { kw++; sh = 0; } }`. The
SECOND kw++ is unnecessary -- bulletin 14 only invalidates the SHIFT BIT, so a line entry
whose low byte is 0xFF is harmless once sh is 0 -- and it costs three pixels instead of
one: at k=61 the displayed offset is 64, and the sequence runs 59, 60, **64, 62**, 63,
i.e. a +4 step followed by a NON-MONOTONIC -2 snap back. Dropping the second kw++ gives
59, 60, **62, 62**, 63: steps of 2 then 0, monotonic, max error 1 px everywhere.
AND THE GATE CANNOT SEE IT. The step-A check compares the text's shift against the
offset the renderer ACTUALLY programmed, and the guard perturbs that same value, so the
text compensates exactly and reads 0 whatever the guard does. It is a self-consistency
check, not a correctness one. What catches this class is comparing the picture's absolute
position against the offset that was REQUESTED.
WORSE, THE PLATFORM CANNOT SEE IT EITHER: bulletin 14 is a real-hardware precaution and
there is no evidence ares models the invalidity at all, so an ares pass says nothing
about whether the guard is needed. The guard's COST (the lurch) is visible on ares and in
the code; its BENEFIT only exists on the board. Read guards of this shape, do not soak
them.
AND THE SUBSTITUTION MUST PERTURB THE DISPLAYED OFFSET ONLY, NEVER THE ACCUMULATOR. If
the scroll position is accumulated frame to frame and the guard writes its corrected
value back, the 1 px stops being one frame's error and becomes permanent drift against
the game's own scroll.

**#10, free run timer initialization** -- "Initialization of the free run timer
must be done whether or not interrupt is used." Our master sets FRT TCR=1
(m_main.c:2722, phi/32); **the slave never sets TCR at all** (s_main.c:282, our
own note: "its ticks are phi/8 = 48208/vint, 4x the master's"). Known to us as
a calibration quirk; the bulletin makes it a documented requirement. Worth a
look when the slave's timing next matters.

**#27, SH2 interrupt problems on the 32X** -- with two or more interrupt types
in use, one occurrence can be acknowledged TWICE, because clearing a source
changes the encoded external IRL level while the first is still being handled.
Ships corrective handler code ("also in the 32X Hardware Manual Supplement 2"),
and notes the problem is 32X-only, not Saturn. **THE PRECONDITION HOLDS HERE:**
mars_start.s:665 main_irq dispatches five types off the SR level -- H, PWM, V,
CMD, VRES. Unverified whether our sequence can trigger it; the bulletin's code
is the reference if a duplicated CMD ever shows up in a census.

**The general point:** a one-line 1994 notice can be worth more than a week of
probing, and three of these four were checkable by grep in minutes. Read the
bulletins before designing a probe for anything they cover.

## The SH7604 manual, mined (2026-09-27)

~/src/32XDK/docs/sh7604-hardware-manual.md. Three things that change decisions,
one that confirms a handoff, and one warning about the document itself.

### The cache cannot absorb a store, by construction

Section 8.4.2: "This cache is of the **write-through** type, and writing to
external memory is performed regardless of whether or not there is a cache
hit... **The CPU waits until the writes onto the internal bus are completed.**"

Every SH-2 store goes to external memory and the CPU stalls for it. There is no
write-back, no write buffer to hide behind, and a cache hit does not make a
store cheaper -- it only keeps the cached copy consistent.

**This is a structural ceiling on the blit**, and it matches what the rig
measured: FPGA blit = stores 35 / instructions 35 / loads 20 / audit 12
(LESSONS, "Rig blit split"). Stores being the largest single term is not a
tuning failure, it is the architecture. Any card of the form "cache the
framebuffer writes" is dead before it is costed. The levers that remain are
FEWER stores and WIDER stores, never cheaper ones.

It also explains the ares gap from the other side: ares charges instruction
cycles only, so it cannot see the one term the manual says the CPU always
waits on.

### A cache miss is four longword reads, and the fill order is rotated

Section 8.1: "Each line of cache memory consists of 16 bytes... **Four 32-bit
accesses are required to update a line.** Since the number of entries is 64,
the six bits (A9 to A4) in each address determine the entry." Section 8.4.1
adds that the fill starts so the longword containing the requested address
comes LAST, and the CPU is released when it lands.

Our recorded geometry -- 64 sets x 4 ways x 16-byte lines indexed A[9:4] -- is
confirmed verbatim by the official manual. It had been derived from the MiSTer
RTL; it is now documented.

### The OD/ID handoff to 32x-builder was correct

We sent that project an RTL-derived claim that CCR carries two replacement-
disable bits their mars.h lacked. The manual, section 8.2, states it exactly:
CCR H'FFFFFE92, bits W1 W0 - CP TW OD ID CE, with "OD: data fetched from
external memory is not written to the cache even if there is a cache miss.
Cache data is, however, read or updated during cache hits. OD is valid only
when CE is 1."

That is our wording's substance including the subtlety ("reads still hit if the
line is already resident; what stops is replacement"). Our own sh_src/mars.h
already carries CP/TW/OD/ID/CE and the W[7:6] way-select, all correct. Those
claims move from provenance R to D.

### Wait states per CS area come out of WCR

Section 7.2.3: bits 7-0 are wait control for areas 3-0, bits 15-8 are idle
cycles between accesses to different areas. BLASTEM.md:346 records the BIOS
leaving WCR = 0x0055, which decodes to one wait on CS0/CS1/CS2 and a CAS-latency
setting on CS3 -- consistent with that file's "a CS2 access is T0 -> T1 -> TW".
NOT independently confirmed here: WCR is an SH-2 on-chip register and the test
cart is 68K-side, so a readback needs an SH-2 probe. Flagged, not claimed.

### The tables in this manual are unreliable; the prose is not

Section 7.2.3's bit-name row reads `W31 W30 W21 W11 W10 W01 W00 W00` -- W20 is
missing and W00 appears twice. The pairing tables immediately below make the
true layout obvious (W31/W30, W21/W20, W11/W10, W01/W00).

That is the SECOND transcription defect found in this toolkit in one session;
the 32X hardware manual's bitmap-mode-register table puts PRI at bit 13 where
every implementation and our own working port put it at bit 7. **Treat tables
in these converted manuals as suspect and the prose as sound.** Cross-check any
bit position against an implementation before acting on it.

## A FULL contiguous work-RAM mirror: 62 of 97 cases, 17 functions clean (2026-09-27)

`--full-mirror` mirrors ALL of 68K work RAM (15,888 B) as one block in a new
`.portmirror` section, so windowing is gone: one base offset, no guard-edge
artefacts, and **no per-function window budget, which alone had been rejecting 27
functions**. 28 functions / 97 vectors selected, up from 17 / 56.

**Where the 16 KB came from, and why it is defensible rather than free.**
`.portmirror` sits at 0x0602CC00, immediately above sbuf's end (0x06019000 +
80,640 = 0x0602CB80) and as far as possible below the Master stack at 0x0603FC00.
That region IS the documented worst-case render-stack spill room, so it is NOT free
in the ordinary sense. Two things make it acceptable: PORTTEST is a probe build that
never ships, and the mirror keeps its 0xA5 guard bytes, so a stack incursion reports
a MIRROR ARTEFACT rather than a false pass. The rule being honoured is that a
DETECTED wrong answer is acceptable in a probe and a silent one never is. It also
moved 16 KB out of `.bss`: `_end` fell 0x06018768 -> 0x06017F28, freeing ~4 KB of
region-guard headroom as a side effect.

Three more classes rejected by name rather than scored, each because no value we
could supply would be the game's:

* **An indirect call through a memory pointer** (`(**(code **)(pcVar1 + 2))()`, 3
  functions) jumps to whatever 68K address a table holds, and no mirror or rebase
  turns a 68K code address into an SH-2 one -- the cart's 68K program is not SH-2
  instructions.
* **A callee typed `void` whose result is used** (13 functions). Promoting the
  signature makes it compile, but the value the 68K left in D0 is not recoverable
  from the decompilation, so a pass would be luck and a fail would be our own
  missing model. Same verdict `tools/l1_verify.py` reaches as UNMODELLED-RETURN.
* **`typedef uint code();`** must be copied from `docs/proof/cport/ghidra_types.h`,
  not invented: defining `code` as `unsigned char` made every indirect call
  "called object is not a function".

**I repeated a mistake already in this file: a C-style `/* */` comment inside the
Python generator.** It made `gen_selftest.py` a syntax error, and because I had
redirected its stderr I spent three build cycles reading a STALE generated file and
blaming the compiler. The earlier entry says exactly this. **Never redirect a
generator's stderr while iterating on it**, and check the reported function count
changed before trusting a build.

## A stale comment can steer a probe (2026-09-27)

A dozen comments in md_main.c and md_start.s described this port running under
an "RV=1 identity map"; it was on the real-hardware list and in TESTCART.md's
opening as one of three facts that justified the whole suite. **The code never
sets RV.** Every write to 0xA15107 is 0 (68S off) or 4 (68S on); TESTCART T25
reads the register back as 0x0000 on ares and BlastEm.

The current design was already written down, in md_main.c's UNPAIR MODEL block:
"RV stays 0 FOREVER... the game executes its REBASED copy through the banked
0x900000 cart window" (NOTES.md "REBASE DESIGN v2"). The code is right; the
comments are leftovers.

**The cost was a wasted probe.** The CART_DMA_PROBE comment reasons: "the
identity map is what SHOULD work here -- holding RV=1 is the whole reason the
arcade binary's ROM self-references resolve... Test that directly, no bank
switch at all." It is the REBASE that resolves those references. So the probe
tested a map that was never mapped, and the 9%-hardware / 0%-ares divergence it
produced measured nothing. That number is now withdrawn from TESTCART.md.

Nothing ever broke because of this, and there is a reason: 32X Technical
Bulletin #9 forbids taking any 68000 interrupt while RV = 1, and this port's
whole heartbeat is IRQ4. Setting RV would have broken it -- presumably why v2
dropped it in the first place.

**AND THAT REASON SURVIVES THE FULL PORT, which is worth stating here so this
entry does not decay the way the comments it is about did.** Mike, 2026-09-27:
"Aren't we retiring IRQ4 for an SH2 native codebase?" The direction is real --
"THE CUTS PATH CANNOT REACH 60Hz, AND THE FULL PORT FITS AT 3.1%" above -- but
it shuts the RV door harder, not softer. The manual: "The SH2 side cannot
access the ROM when RV = 1." An SH-2-native game reads cart ROM constantly for
code past the 256 KB of SDRAM and for all of its art, so RV=1 would starve
exactly the CPU the port depends on. Bulletin #9 still bites as well, because
the 68K keeps a vint shim and the MD-plane packets either way. Two independent
reasons, and the second is the stronger.

So: RV=1 is not a lever being held in reserve. It is excluded under the current
architecture and MORE excluded under the planned one.

**The rule: a comment describing a MECHANISM is a claim, and claims decay.**
When a mechanism is named in a dozen places and nowhere in the code, grep for
the write before designing anything around it. Superseded comments are tagged
`[RV=1: SUPERSEDED]` rather than deleted -- they still explain why the
surrounding code is shaped as it is, and deleting them would lose that.

## Two more width rules from the same root: 74 of 97, 20 functions clean (2026-09-27)

**A SYMBOL USED AS AN ARRAY BASE INDEXES IN ITS OWN ELEMENT SIZE.** Ghidra's width
table is a WHOLE-SYMBOL inference -- something else in the program reads a long at
0xFFEC80, so it reports 4 B -- but `FUN_00003cb6` does
`(&DAT_ffffec80)[iVar1] = 0xff`, where the symbol is the base of a BYTE table. With a
`uint` declaration the index scaled by 4 and every store landed four times too far
out. The ORACLE's observed access width is the authority for that use (it recorded a
1-byte write), so an array-base symbol now takes the oracle's narrowest observed
width and falls back to the table only where the oracle never touched the address.
That one change cleared `FUN_00003cb6`, `hide_object_sprite` and `FUN_000182c4`
together: **62 -> 74 of 97**.

This is the third distinct way one address can need two widths, and they are worth
keeping separate because each needs a different authority:

1. **Overlapping symbols** -- `_DAT_x` is the larger, `DAT_x` the smaller. Authority:
   Ghidra's underscore convention, stated in its own warning.
2. **An array base** -- the element size, not the symbol size. Authority: the
   oracle's observed access width.
3. **One name at two widths in one function** -- `FUN_000039c8`'s
   `_DAT_fffff138 = _DAT_fffff132` needs a WORD where the next statement needs a
   LONG. No authority can fix this; it is a decompilation defect.

**68K ABSOLUTE-SHORT ADDRESSING IS RENDERED AS A NEGATIVE CONSTANT.**
`move.b #$FF,($ECC0).w` sign-extends its 16-bit displacement to 0xFFFFECC0 -- work
RAM 0xFFECC0 -- and Ghidra writes it as `*(undefined *)(iVar1 + -0x1340)`, because
0x10000 - 0x1340 = 0xECC0. Left alone it dereferences the SH-2's on-chip register
area. Now rewritten through the mirror, which is a mechanical change to how the
address is FORMED and never to what the code computes, applied only when the
sign-extended value lands inside work RAM. `FUN_00003cb6` carries both styles in one
body: `(&DAT_ffffec80)[iVar1]` (a named symbol, already mirrored, worked) beside
three absolute-shorts (which did not).

## A 68K ADDRESSING MODE IS NOT A SEMANTIC CONSTRAINT (Mike, 2026-09-27)

"The SH-2 likely has way more functionality than the 68K and while pointer
signedness is something to care about for a 68K conversion we are using a different
toolset with the C port are we not?"

Correct, and it overturned a rejection I had just written into this file. The 68000
stored only 16 bits of a work-RAM address **because it had an addressing mode that
sign-extended them**: `iVar2 = (int)*(short *)(A6+0x3c)` with 0xC880 in that cell
means the cell at 68K 0xFFC880. I had concluded these were unseeadable "on any host
or target", reasoning that a 16-bit cell cannot hold a 32-bit mirror pointer. That
reasoning is only binding on an EMULATION. **A C port needs the same CELL, not the
same encoding, and it is free to reach it however it likes** -- so translate at the
point of use:

    #define W68(v)  (wram + WOFF(0xFF0000u | ((uint)(v) & 0xFFFFu)))
    (int)*(short *)(X)   ->   (int)W68(*(short *)(X))

**86 of 101 cases (85%), 23 of 29 functions clean** -- up from 78/97/21, with two
functions recovered from the bin I had wrongly closed.

The general rule, because I got this wrong once and will otherwise get it wrong
again: **when the decompiled C encodes a 68K ADDRESSING MODE, rewrite it; when it
encodes the game's LOGIC, never touch it.** Three rewrites now qualify, all
mechanical and all changing how an address is FORMED rather than what is computed:

* a shadowing pseudo-register declaration (drop it)
* 68K absolute-short, rendered as a negative constant (`+ -0x1340` -> `WP(0xFFECC0)`)
* a 16-bit work-RAM address (`W68`)

**THIS APPLIES TO THE FULL PORT TOO, and `tools/cport_prep.py` does none of it.**
The 771-function port contains the same idioms, so any of its functions that use
absolute-short or 16-bit addressing is currently compiling to a dereference of the
SH-2's on-chip register area. That is a correctness bug waiting in the port, not
just in the test harness, and it is worth fixing there before anyone measures the
port's behaviour.

## The port's own address rewrites, and the 234 I nearly broke (2026-09-27)

`tools/cport_prep.py` now performs the two addressing-mode rewrites the SH-2
self-test validated, so the 771-function port gets them too: **143 absolute-short
references and 52 sixteen-bit work-RAM pointers**, via a generated `w68.h` that maps
through the port's own contiguous block at `_WRAM_BASE` (`cport_layout.py`). Before
this, every one of those compiled to a dereference of `0xFFFFxxxx` -- the SH-2's
ON-CHIP REGISTER AREA. That was a live correctness bug in the port, not in a test.
The port compiles clean: 0 errors, 304,620 bytes of SH-2 object.

**The number started at 377 and the honest number is 143. The other 234 were
ordinary pointer arithmetic I would have broken**, and the compiler's type error was
the only thing that said so:

    invalid operands to binary + (have 'char *' and 'byte *')

Two distinctions had to be added, and both come from Ghidra's own naming:

* **`p<type>Var<N>` is a POINTER, `<type>Var<N>` is an integer.**
  `*(T *)(puVar8 + -0x1340)` walks backwards through a block that ALREADY points
  into work RAM.
* **An A-REGISTER is a pointer too.** `*(undefined *)(unaff_A6 + -0x78)` is the
  68K's `(-$78,A6)` -- a negative DISPLACEMENT off an address register, nothing to
  do with absolute-short addressing.

**Absolute-short addressing forms its address from an INTEGER. That is the entire
distinction**, and a broad cast is exactly how it gets lost -- Mike spotted the
`(unsigned)` casts and that is what they were hiding. A rewrite that cannot tell a
pointer from an address is not a rewrite, it is a corruption with good intentions.

Verified both ways: the port compiles at 0 errors, and the SH-2 self-test held at
**86 of 101 with no regression** after the guard narrowed its own rewrites from 8 to
4 -- so nothing real was lost by excluding the 234.

## The SIGN decides the region, and I had erased it (Mike, 2026-09-27)

"Why unsigned though? especially if we are using the SH-2 and 32bit pointers
already?"

The `(unsigned)` casts were redundant -- `& 0xFFFF` already discards the sign
extension, because `short & unsigned` promotes to int first -- but following the
question found a real bug they were hiding.

**`W68` unconditionally OR'd in `0xFF0000`, which is only correct for a NEGATIVE
value.** The 68000 SIGN-EXTENDS a 16-bit absolute address and the 24-bit bus
truncates:

    0xC880 -> 0xFFFFC880 -> 68K 0xFFC880   work RAM      (my macro: right)
    0xF45B -> 0xFFFFF45B -> 68K 0xFFF45B   work RAM      (my macro: right)
    0x0100 -> 0x00000100 -> 68K 0x000100   PROGRAM ROM   (my macro said 0xFF0100)
    0x7FFE -> 0x00007FFE -> 68K 0x007FFE   PROGRAM ROM   (my macro said 0xFF7FFE)

So every POSITIVE short was mapped into work RAM when it addresses ROM. `W68` now
branches on the sign in both the port (`w68.h`, to `_ROM_BASE`) and the self-test
(to the cart's `.gamehigh` copy at 0x02300000), exactly as `cport_layout.py`'s
`region()` already did.

**Mike said "pointer signedness is something to care about" two messages earlier and
I filed it under 68K-only concerns.** It is not: on the 68000 the sign of a 16-bit
absolute address IS the region selector, and a port that drops it silently relocates
every ROM table read into work RAM. A redundant cast is harmless on its own; a
redundant cast written INSTEAD of thinking about the sign is how the bug got in.

Port: 0 errors, 305,504 bytes (up 884 for the sign branch). Self-test: 86 of 101,
unchanged, guard trips 0 -- the fix costs nothing and the two instruments still agree.

## The SH-2 probe: cart misses cost five SDRAM misses (2026-09-27)

`make shprobe`. TESTCART is 68K code, so everything it knew about the 32X was
inferred -- from RTL, from the manual, or through the adapter. The cache, CCR,
the FRT and every access cost live on the far side of that wall and they are
what the SPEED axis is made of. sh_src/sh_probe.h runs there and publishes FRT
ticks to SDRAM 0x0603B000, which ares and the BlastEm fork dump with the same
switch they already use for the 68K side.

                                   ares   blastem
    resident access + loop          7.2      13.0   SH-2 cycles
    SDRAM miss, extra over a hit   12.1      10.2
    CART miss, extra over a hit    60.0      48.2
    cart / SDRAM miss ratio        4.96      4.73

**A CART MISS COSTS ABOUT FIVE SDRAM MISSES, and two independent
implementations agree on the ratio to within 5%.** That is the CS1 wait state
measured from the SH-2 side for the first time, and it is load-bearing for the
full port, which fetches both code beyond the 256 KB of SDRAM and all of its
art from cart. Absolute cycles differ between the machines by up to 1.8x; the
RATIO is what survives, so plan with the ratio.

**Cache clear is proved by BEHAVIOUR, not by a register read.** Walk a 2 KB
window twice (the second pass is hot), purge via CCR CP, walk again: ares
229 -> 283 ticks, BlastEm 416 -> 459. A CP bit that reads back 0 proves
nothing; a re-miss does.

**Write-through is visible on both**, contrary to what I expected. Same
resident window, loads against stores: ares 222 vs 264 (+19%), BlastEm 427 vs
452 (+6%). The SH7604 manual's "the CPU waits until the writes onto the
internal bus are completed" shows up even in an emulator that charges
instruction cycles only.

### The instrument cross-checks itself, and that is why the numbers are usable

Two independent readings must agree if the model is right. The RESIDENT walk
gives the per-hit cost directly. The stride-16 against stride-4 pair gives it
again by subtraction -- both cover the same 16 KB so both take 1024 line fills,
and the difference is purely 3072 extra cached iterations. They land on the
same number on both machines: 7.2 and 7.2 on ares, 13.0 and 13.0 on BlastEm.

**And the first cut of that pair was wrong in a way worth remembering.** It ran
both strides at 1024 iterations and read 616 against 328, which looks like a
1.9x line-fill payoff. It is not: 1024 steps at stride 4 covers 4 KB, which
FITS IN THE CACHE, so it was comparing a 16 KB footprint against a resident
one. **When a stride changes, the footprint changes with it -- hold bytes
covered constant, not iterations.**

### Three ways an include can be silently absent, in one afternoon

The probe returned all zeros twice before it ran. First, `TEST_CART` was in
MDCCFLAGS only, so the SH-2 half compiled out and the 68K sat waiting on a
handshake nobody was listening for -- COMM4 read back the 68K's own SHP_GO.
Second, the `#include` landed inside `#if defined(PORT_LOAD)`, false on the
line. Third, it had to sit after frt() is defined, not with the other includes.

Each looks identical from outside: a dead master. **A handshake that times out
should report WHAT IT READ BACK, not just that it failed** -- seeing 0x5350
(our own GO) rather than 0 is what said "nobody consumed it" instead of
"the master crashed", and that is the whole difference between the first and
second diagnosis.

## The shadow-DROP transform was corrupting address AND value (2026-09-27)

Mike, on my dismissing suppressed warnings as a flag issue: "dropping -Wno-error= is
not a great example. we want the compiler clean." He was right, and compiling the
port with NOTHING suppressed found a serious bug in its most-praised transform.

**Ghidra declares a pseudo-register's element type PER FUNCTION, and dropping the
declaration throws that type away.** `FUN_000024ec` has `undefined2 *unaff_A6;`, so
`unaff_A6[6] = 0x14a0` is the WORD at byte offset 12. Bound instead to the global
`byte *unaff_A6`, the identical text means the BYTE at offset 6 and truncates the
value to 0xA0:

    Ghidra's local  undefined2 *   unaff_A6[6] -> byte offset 12, stores 0x14A0
    after the drop  byte *         unaff_A6[6] -> byte offset  6, stores 0x00A0

**Wrong address AND wrong value, over 10,700 references.** gcc reported 78 of them as
`-Woverflow`; the address scaling was silent everywhere else. LESSONS previously
described this transform as "the highest-leverage transform available" -- it was, and
it was also silently destroying data.

The fix is to INITIALISE, not delete: the globals are renamed `_r_*`, and each
function declares every pseudo-register it references once, with **Ghidra's type where
it gave one**, initialised from the register:

    undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;

**1,463 declarations now keep Ghidra's type and 0 fall back to the global's.**

The diagnostic count, with nothing suppressed, is the measure of the whole exercise:

    before  420 errors, 128 warnings   (-Woverflow 78, int-conversion 313)
    after   147 errors,  47 warnings   (-Woverflow 0,  int-conversion 95)

`-Woverflow` to ZERO is the value-truncation class gone. Three regex faults cost a
round each on the way, all worth knowing: **the `*` binds to the NAME** in Ghidra's
output (`byte *in_A1;`), so requiring whitespace before the identifier left 136
declarations alive to collide with the injected ones; **a type cannot be a C
keyword**, or `return in_D0;` becomes `return in_D0 = (return)_r_in_D0;`; and an
injection that does not first detect what is already declared produces
"redeclaration with no linkage".

**The remaining 147 are all pointer/integer punning**, which is legitimate in a 68K
port -- the 68K stores addresses in data cells -- but each needs an EXPLICIT cast to
be reviewable. That is the worklist, and the reason to keep the compiler clean is
exactly this: the sign bug in `W68` was hiding under a suppressed cast too.

## The 68K runs 2.24% under its nominal clock (2026-09-27)

TESTCART T24/T25 make the 68K time ITSELF, in assembly so the instruction count
is a specification rather than something to be disassembled afterwards:

    1: subq.w #1,Dn    4 cycles
       bne.s 1b       10 taken

Two runs of that loop, 2048 and 6144 iterations, differ by exactly 4096 x 14 =
57,344 cycles and by nothing else -- same entry, same trailing read, same jsr,
so the harness cancels. **The delta is 120 lines on ares AND on BlastEm.**

    57,344 / 120 = 477.9 68K cycles per raster line
    nominal:      53.693175 MHz / 7 / (262 x 59.92) = 488.57

**A 2.24% derate, agreed on by two independent implementations.** And T25
prices one NOP at 4.08 cycles (ares) / 3.97 (BlastEm) at that rate -- the
68000 table's 4, confirmed on the machine.

**CORRECTED AGAIN, SAME DAY, BY THE DECOMPILE THREAD -- AND THEIR READING IS
THE RIGHT ONE. There is no residual at all.** I first called the gap a
CONSTANT ~2 lines of harness overhead, then a PROPORTIONAL 2.24% derate applied
to a correct prediction. Both kept 488.57 and invented something to absorb the
difference. At 477.9 the nine rows land within 0.3 of a line with nothing
subtracted:

         test  cycles   at 488.57   at 477.9   measured
         T08    43008        88.0       90.0         90
         T0B    30720        62.9       64.3         64
         T14    51200       104.8      107.1        107
         T15    19968        40.9       41.8         42

The cycle table was never off and there was never a cost to subtract. **A
constant discrepancy across several tests is a WRONG CONSTANT before it is an
additive overhead** -- and that reading stops an explanation rather than
starting one. Their words: "that's the second time today a plausible mechanism
absorbed a systematic error and stopped the search."

What remains open is why 477.9 and not 488.57 -- either the frame is not 262
lines at 59.92 Hz on these machines, or ~2.2% of 68K cycles are stolen (VDP
refresh is the right size). T24 settles it on the rig, and the answer decides
whether 477.9 transfers to silicon or is an emulator constant.

The superseded proportional table, kept because it shows the error's shape:

               predicted   x1.0224   measured   predicted+2
        T15        40.87      41.8         42          42.9
        T0B        62.88      64.3         64          64.9
        T14       104.80     107.1        107         106.8

So every instruction-derived prediction on that page should be multiplied by
1.0224, and the nine rows fit to within a line.

A candidate for the derate is VDP DRAM refresh stealing 68K bus cycles during
active display, which is the right size. Not asserted: the MiSTer core asserts
REFRESH only under DMA_VBUS (GEN/vdp.sv:721), so if the FPGA also reads 120 it
is not that core's refresh path. The rig settles it, and T24 says so in one
number.

**The method is the lesson.** Three loops of similar length cannot distinguish
a constant from a percentage -- the discriminating measurement was a loop
whose cost was known EXACTLY and run at two lengths far enough apart to make
the difference visible. Write the probe in assembly when the number you want
IS the instruction count.

## Why the same bug cost the port 78 truncations and the self-test nothing

`tools/gen_selftest.py` had the identical shadow-DROP fault and now initialises too.
The self-test result is **86 of 101, unchanged, 23 of 29 functions** -- and that is
explained rather than lucky:

**207 of its 221 pseudo-register declarations DO differ from the global's type**, so
the fix is doing real work. But the differences present are signedness (`short` vs
`ushort` on D-registers) and `int` vs `byte *` on A-registers, and neither changes a
BYTE OFFSET: `in_A0 + 0x10` is offset 16 either way. The class that bit the port is a
WORD pointer -- `undefined2 *unaff_A6`, where `[6]` means offset 12 -- and none of
these 29 functions has one.

So the two instruments agree for a reason: the self-test's functions were never
exposed to the scaling variant, while the port's 10,700 references were. **A negative
result that explains itself is worth as much as the fix.** Signedness still matters on
its own terms -- `short` vs `ushort` changes every signed comparison -- so keeping
Ghidra's type is right here even where these vectors do not exercise it.

Also learned, twice now: **`ld: error adding symbols: no error` means a stale object,
not a code fault.** `rm -f sh_src/m_main.o sh_src/port_selftest.o` and rebuild;
port_selftest.c compiled cleanly on its own throughout.

## Four parallel tables drift; generate them (2026-09-27)

TESTCART carries TC_NAME, TC_REF, TC_CAT and TC_PROV, all indexed by test
number, all hand-edited. Inserting the two 68K self-timing tests shifted every
later index by two and I patched three of the four by hand. The table then
showed MD CRAM 64 with the bitmap-mode register's reference (0x80C3) and four
rows reading REF 0xFFFF -- silently wrong, because a misaligned REF still
renders as a number.

Rewritten from a single list of (name, ref, cat, prov) tuples, generated into
all four arrays at once. **Parallel arrays indexed by the same key are one
table pretending to be four; edit them together or generate them together.**

Two smaller things from the same pass:

- **A timeout has to outlast the SLOWEST machine.** The 68K waited six frames
  for the SH-2 probe's handshake; ares finished, BlastEm did not -- it charges
  ~1.8x for the same SH-2 loop. The probe had published correctly and only the
  68K's verdict was wrong. Now it polls up to 40 frames with a WRAM delay
  between reads, so the whole wait costs 40 adapter accesses.

- **The cart's timing loops must live in .data**, because executing them from
  ROM would change the numbers they measure -- so the suite grows WRAM, and it
  went 22 bytes past md.ld's boot-stack fence. The fence is now TESTCART-aware
  (512-byte reserve against a measured 202-byte boot depth) the same way the
  __wramcode2 fence already was, and shipping builds keep the full 1 KB. An
  unconditional fence is not a fence when it guards against a contender that
  is not in the build.

## A sweep must name the failing cell (2026-09-27, from the decompile thread)

Their CARD-DECOMPILE-C-TRAPS: "a FAIL must report the cell that failed, not the
first cell compared -- ours showed got == want on failing cases." Audited
TESTCART against all four of their instrument-discipline traps; three were
already clean and this one was live in FIVE places.

T21, T29, T2A, T2C and T2F all swept a range and reported `bad`, a COUNT. On
ares that is merely unhelpful; on real hardware, where the screen is the whole
channel and there is no dump, "5 cells wrong" is not actionable at all while
"cell 0x2B read 0x46" is the entire diagnosis. Now:

    bits 31-24  how many failed      bits 15-8  index of the FIRST
    bits  7- 0  the low byte of what that one actually read

so the table's four digits carry index and value -- the part you act on -- and
the dump carries the count above them.

**The general form: a verdict that says HOW MANY without saying WHICH is a
verdict you cannot act on from the readout alone.** It survives only because
a dump is usually available; it fails exactly when the dump is not, which is
the case the instrument exists for.

The other three of their four traps, checked here:

- **Never stub abort()** -- clean, this suite has no stub. Their measurement:
  with one, a build ran 42/54 in ares and produced NO BARCODE AT ALL on the
  FPGA, and silently dropped its summary counters so a working test read zero
  passes. gcc treats everything after a proved-unreachable abort() as dead
  code, so the link error is the guard.
- **A counter written during a sweep is not readable** -- their rom read 27 in
  an ares dump and 36 off the barcode and it looked like an ares-vs-FPGA
  disagreement for an hour. TESTCART publishes only after tc_run_all returns,
  so it does not have the shape.
- **Never redirect a generator's stderr while iterating** -- no generator here
  does.

And one constraint their bulletin work puts on sh_src/sh_probe.h: **the probe
must never write the FRT output compare.** Bulletin #27's double CMD INT goes
live the moment TOCR/OCRH/OCRL are driven, because the FRT output participates
in the encoded external IRL level. The probe reads TCR and nothing else;
verified by grep, and worth re-checking whenever a finer SH-2 clock is wanted.

## Per-site review found 18 bad rewrites a passing regex had hidden (2026-09-27)

Mike: "inspect every line of c. we dont need a sweeping regex. lets do each line
independently." `tools/cport_review.py` prints one record per transformed site -- the
Ghidra line it came from, the line now, the derived address, and a check -- because **a
pattern matching is not evidence that a rewrite is right.** Three classes of bad site
came out, none of which any compiler would have reported:

1. **16 stack-frame locals relocated into work RAM.** All shared `register0x0000003c`
   as the index with displacements of -0xC to -0x18. That is Ghidra's placeholder for an
   UNRESOLVED register, so a small negative displacement off it is a stack local
   (`(-0xC,A7)`), and the rewrite was sending them to work RAM near 0xFFFFF4.
2. **1 displacement translated as an absolute address.**
   `*(int *)(A6+0x24) = (int)*(short *)(iVar10 + -2) + iVar10 + -2` is the 68K's
   table-relative form: the cell holds a DISPLACEMENT added to a base. W68 turned it
   into a mirror address and then added a base to that. **Adding a CONSTANT to a
   translated address is fine; adding a VARIABLE means the cell held a displacement.**
3. **1 stack-sourced value translated.** A 16-bit word in a stack slot
   (`register0x0000003c + -6`) is a saved value with no basis for being called a
   work-RAM address -- the same exclusion the absolute-short pass already had.

So the absolute-short count reads **377 -> 143 -> 127** and W68 **52 -> 51 -> 50**, each
reduction a class found by a different check rather than by the pattern getting luckier.

**A CHECK THAT CANNOT FAIL IS NOT A CHECK.** My first W68 test asked whether the LINE
contained a pointer cast, and passed all 52 sites plus a synthetic
`uVar3 = (int)W68(...) + 1` that is obviously a value use -- because `*(short *)` appears
INSIDE the W68 argument at every site, being the read of the cell itself. The test has to
look at the context OUTSIDE the call. Writing three synthetic cases and confirming one
FAILS is what exposed it.

Known blind spot, left rather than over-fitted: the checker follows an assigned variable
looking for a dereference, and misses one more indirection --
`iVar3 = (int)W68(...); puVar1 = (undefined *)(iVar3 + 7); *puVar1 = ...` is an address
use it reports as suspect. Two sites read by hand and confirmed correct. Tightening the
checker until those pass risks tightening it until real faults pass too.

## The 68K line is 477.9 cycles, and that retires the "harness overhead" (2026-09-27)

The builder measured the 68000 running **2.24% under its nominal clock**: 57,344 cycles
of assembly-known loop read as 120 raster lines on BOTH ares and BlastEm, so a line is
**477.9 cycles, not the nominal 488.57** (53.693175 MHz / 7 / (262 x 59.92)).

**That does more than shift the numbers -- it removes an explanation they had already
accepted.** Their three timing tests were each ~2 lines over prediction and the residual
was recorded as "fixed harness overhead, since it is constant rather than scaling". At
477.9 there is no residual at all:

    test  cycles   at 488.57   measured   at 477.9
    T08      42       88.0        90        90.0
    T13      42       88.0        90        90.0
    T14      50      104.8       107       107.1

All three land exactly. So the cycle table was never off and there is no harness
overhead to subtract -- **a constant discrepancy across three tests was a WRONG CONSTANT,
not an additive cost.** That is the same shape as today's other findings: a plausible
mechanism ("fixed overhead") absorbed a systematic error and stopped the search.

Earlier entries in this file quoting 488.57 (the T08/T13/T14 block, and
`docs/handoff/CARD-RIG-NO-DEFAULT.md`) are superseded: **multiply any nominal-clock
timing figure by 1.0224, or use 477.9 directly.**

Also from the same pass: **a cart miss costs about five SDRAM misses** (4.96 on ares,
4.73 on BlastEm, measured SH-2-side). That bears directly on the port, because the
decompiled bodies currently live in `.porttest` -- in CART -- and on the object-table
stride finding: with the SH7604 cache at 4 ways and 64 entries, a thrashing stride costs
5x more for cart-resident code than for SDRAM-resident code. Any port function placed in
cart wants its working set to be cache-resident far more than an SDRAM one does.

## The architecture, proved end to end (2026-09-27)

TESTCART timed the transport's three legs -- assemble, blast, lift -- and the
68K performed all three. **Nothing proved that the SH-2 lifts and executes what
the 68K staged**, which is the spine of this port. Three rows now do, and they
pass on ares and BlastEm.

The whole protocol is four steps and it is the real one, at the real packet
base:

    FM=0   the 68K owns the FB window; writes a display list at 0x851A00
    FM=1   window handed over, FMDL_GO on COMM4; the SH-2 lifts the SAME
           MEMORY at 0x24011A00, executes, posts the record count on COMM14
    FM=0   the 68K takes the window back and reads what landed

Both sides see one memory at two addresses (68K 0x840000 is SH-2 0x24000000)
and FM alone decides whose turn it is. Comm ports are adapter registers, not
the FB window, so they stay readable across the handoff -- which is what makes
a handshake over an FM boundary possible at all.

**Three rows so a failure says WHERE:** records executed (count vs count),
words landed (cell-named), round-trip cost. A short count means the list was
truncated in transit; a matching count with wrong pixels means the writes did
not land. Different failures, told apart from the table without a dump.

The executor is six lines on purpose. When the round trip fails, the failure
should be in the HANDOFF and not in an interpreter anybody has to debug.

### Two silent-drop bugs found while building it, both the same shape

**The cart dropped three tests off the end.** tc()/tc_val()/tc_meas() silently
ignore anything past TC_MAX, so the three new FMDL rows never ran -- and the
table showed the COLOUR page's verdicts under their names, because names and
results are two arrays that agree only by counting. It read as three passes.
There is now a `tc_asked` counter and a last test that FAILS if any recorder
was refused for want of room.

**Then tools/tc_compare.py did the same thing from outside.** It hardcoded the
result-block layout (0x00/0xC0/48) and went stale the moment the table grew to
56, reading tc_res from inside tc_v and again printing the wrong verdicts under
the right names. It parses TC_MAX and both base addresses out of testcart.c
now.

**A tool that duplicates a layout is a second place for that layout to be
wrong, and the failure renders as a PASS.** Parse it or share it; never restate
it.

## extraout applies to DATA registers too: 83 of 93 (89%), 23 of 27 clean

`extraout_D<n>` is what a callee left in D<n>, exactly as `extraout_A<n>` is for address
registers -- and only the A case was ever sourced from the inner call's exit state.
`FUN_0000e974` stores `extraout_D1w` into A6+0x1A, the oracle wants 0xFF00 there, and
seeded from the outer ENTRY state it was 0: all four vectors failed on that one cell.

Sourcing it from the callee exit then showed the same thing the A-register case showed --
the four recorded exits are all DIFFERENT (0x1577FF00, 0x0000FFB8, 0x15770100,
0x0000FFB8) and nothing pins which belongs to which vector. So the unpairable rejection
now covers `extraout_[AD]`, and six functions are excluded by name rather than scored.

**Ghidra's suffixed variants are the clinching evidence for the capture fix**:
`extraout_D1w_00`, `_01`, `_02` in one function are several DISTINCT callee outputs, and
a sequence id in `tools/l1_oracle.lua` is the only thing that can attribute them. That
ask now covers 6 functions rather than 8's worth of A-registers alone.

Four functions still fail, and the useful thing is that they are no longer a mixed bag:
`FUN_000039c8` (0/4) is the confirmed overlapping-width DEFECT, and `FUN_000056e8` (1/2),
`FUN_000171fa` (2/3) and `FUN_0001828e` (0/4) are the remainder.

## 84 of 93 (90%), and the second candidate REAL defect

`FUN_000056e8` fixed by the last-write rule. Three failures remain and they are now
distinct kinds rather than a mixed bag:

* **`FUN_000039c8` (0/4) -- CONFIRMED decompilation defect.** `_DAT_fffff138 =
  _DAT_fffff132` needs the WORD at 0xFFF132 while the next statement needs the LONG. One
  name, two widths, one function; no declaration satisfies both.
* **`FUN_0001828e` (0/4) -- CANDIDATE defect, reached by ELIMINATION.** `*unaff_A6` is
  seeded 0x80, the body's last write is `*unaff_A6 & 0xf7`, and we read back 0xC6 where the
  oracle has 0x80. Every alternative was checked and closed: none of the 8 absolute-short
  rewrites can reach 0xFFD580 (they target 0xFFEC80-0xFFFAB0); the branch condition
  `DAT_ffffd420` is seeded 2 and the oracle RECORDED reading 2, so both sides ran the same
  three callees. So one of `FUN_0000669c` / `hitbox_a_expand` / `walk_player_slots`
  behaves differently in the port when run with A6 = 0xFFD580 -- and `FUN_0000669c` passes
  4/4 on its OWN vectors, where A6 = 0xFFC000. **A callee that is correct on its own
  vectors and wrong inside a caller's closure is the interesting case**, because it means
  the caller's vector does not constrain some cell the callee depends on.
* **`FUN_000171fa` (2/3)** -- one vector.

**The absolute-short guard was also too broad, and the oracle is what proved the fix
safe.** A BARE register term is the address base (exclude); a SUBSCRIPTED one is a value
being read (allow) -- `((ushort)unaff_A6[8] * 0x10 + -0x800)` forms its address from an
integer expression and IS absolute-short, but the broad guard rejected it and the store
went to the SH-2's on-chip register area. Port sites 127 -> 326.

**And the review checker is now CIRCULAR**: it shares the transform's rule, so it cannot
find a fault the transform makes. Zero suspects from it means nothing. The 199 new
rewrites were verified by the oracle pass rate holding at 83/93 across the change --
**an independent authority, which a checker built from the same premise can never be.**

## FUN_000039c8 was not a declaration problem, it was ENDIANNESS: 88 of 93 (94%)

I recorded this as "the ONE confirmed decompilation defect... one name, two widths, one
function, so no declaration satisfies both". **That was wrong, and the fix is four
characters.**

`_DAT_fffff138 = _DAT_fffff132` where 0xFFF132 holds a LONG is the 68K reading the WORD AT
THAT ADDRESS -- the HIGH half on a big-endian machine. **C's implicit conversion takes the
LOW half.** The oracle settles it without argument: the long is 0x102E_C800 and the write
to 0xFFF138 is 0x102E.

    _DAT_fffff138 = _DAT_fffff132 >> 16;

**The general rule: a NARROWER DESTINATION assigned from a WIDER same-family source takes
the HIGH bits**, by `8 * (srcwidth - dstwidth)`. Only 4 sites in the corpus, 2 distinct
pairs, so it was rewritten exactly rather than by pattern-and-hope. The other pair,
`DAT_fffff149 = DAT_fffff148` (byte from word), is the same shape and was a NO-OP in C --
it assigned the byte its own current value.

**25 of 27 functions now pass every oracle vector they have; 88 of 93 cases.** Two left:
`FUN_000171fa` (2/3) and `FUN_0001828e` (0/4, the callee-inside-a-closure case).

What this cost to learn, and it is the lesson worth keeping: I had a WORKING EXPLANATION
("two widths, unsatisfiable") that fitted every observation, and it stopped me looking. The
oracle had the answer in its own recorded values the whole time -- the write was 0x102E, the
high half, sitting right there in `l1_vectors.jsonl`. **An explanation that accounts for the
evidence is not the same as the right one, and "confirmed defect" was a label I applied
three times to things that turned out to be mine.**

## Over-constrained vectors, and catching my own over-reach mid-change: 87 of 91 (95%)

A vector can record MORE than the function does. `FUN_000171fa`'s whole body is
`DAT_ffffe800 = 1`, and three of its four vectors record exactly that write -- while the
fourth records `FFE406`, `FFFEF8` and `FFFEFA` too, because the capture spanned a longer
sequence. **The function can never satisfy that vector**, so scoring it as a failure says
nothing about the port.

Dropping an expectation the closure has no statement targeting fixed it. **But the first
version dropped 149 expectations and took the case count 93 -> 79, including 28 from
`hide_object_sprite` which was passing 4/4** -- because a writable set built from named
symbols and A-register offsets cannot see a store through a computed or memory-derived
address, so everything unresolvable looked unwritable. That is precisely the
defect-masking risk the check was meant to avoid, and it was visible only because the case
count moved in the wrong direction.

Gated version: **if the closure contains ANY store whose target cannot be resolved -- through
a mirror pointer, a W68 translation, or a local pointer variable -- bound nothing and drop
nothing.** 9 expectations dropped across 3 closures whose stores are fully bounded.
`FUN_000171fa` passes.

**87 of 91 cases, 25 of 26 functions.** One failure left: `FUN_0001828e` (0/4), where a
callee correct on its own vectors diverges inside this caller's closure -- the caller's
vector does not constrain some cell the callee depends on, which is a capture gap rather
than a port defect.

The rule worth keeping from this one: **a check that removes evidence needs a bound on what
it can remove.** "Drop what the closure cannot target" is only sound while "cannot target"
is provable; where it is merely unproven, dropping is guessing with extra steps.

## A characterisation suite can name the machine it is running on (2026-09-27)

Mike: "maybe a guess at the end -- You are running this in an emulator!"

Once a suite has rows where the machines disagree, those divergences ARE a
fingerprint and the suite can classify itself. TESTCART's bottom line:

    GUESS ARES                       BMMR B3FF

Three discriminators, all already measured for other reasons: the bitmap mode
register readback (0xB3FF is ares alone; the RTL, the official manual and
BlastEm all say 0x80C3), and cart-read against work-RAM-read (the MiSTer core
holds DTACK for an SDRAM access on every MD ROM read, so cart is +2 lines
slower; both emulators read them equal). ares 5A/5A, BlastEm 59/59, FPGA 5D/5F.

**The line carries its EVIDENCE beside its verdict.** A bare "ARES" is
unfalsifiable from the screen; "ARES / BMMR B3FF" can be checked against the
row four lines above it. On hardware, where the screen is the whole channel,
a verdict you cannot audit from the same screen is a verdict you have to
trust.

**And it declines to guess what it cannot.** Real silicon is not yet separable
from the MiSTer core, so the line says FPGA OR HW rather than picking one. The
row that will split them is T1D -- a machine that refuses an active-scan 32X
CRAM write in packed-pixel mode WHILE reading cart at work-RAM speed is
hardware and not this core. Naming the future discriminator is worth more than
a confident wrong label now.

## My own publication block overflowed, and the aggregate stayed right (2026-09-27)

`volatile uint port_test_out[64]` with per-case slots starting at index 16 gives **48
slots**, and the sweep had grown to 103 cases. **Everything past case 47 was writing out of
bounds for several builds.**

**It hid because the SUMMARY counters are written by the test itself and stayed correct.**
The 87/91 and 91/103 figures were right the whole time; the per-function breakdown behind
them was partly garbage, which is how `release_palette_slot` appeared to regress from 4/4 to
2/4 and sent me looking for a shared-declaration coupling that did not exist. The array is
now sized from the case count at generation time, and `m_main.c`'s extern no longer
understates it as `[16]`.

**An instrument whose headline number is produced by a different mechanism than its detail
can be right and useless at once.** The detail is what you debug from; a correct total is not
evidence the detail is sound.

## Hardware and ROM are different regions, and only one gets a stub

22 functions were rejected for "closure reaches hardware". Since **every oracle expectation
is a work-RAM cell**, a hardware write is unobserved either way -- so an absorbing stub lets
those functions' WORK-RAM behaviour be verified, which is the part the oracle constrains.
It does not model the hardware, and a function whose only purpose is the register write
cannot pass vacuously because a scored function must still have constraining work-RAM writes.

**But ROM IS NOT HARDWARE.** `DAT_0001c0f2`, `DAT_000040f0` and `DAT_00004100` are below
0x40000 -- CONSTANT TABLES in the 68K program ROM, which the cart already carries at
GAMEHIGH. Stubbing them returned 0 instead of the real bytes. Routing by REGION -- work RAM
to the mirror, ROM to the cart's copy, the remainder to the stub -- is the same split
`cport_layout.py`'s `region()` already made.

**93 of 103 cases, 26 of 29 functions.** Three failures: `FUN_00003a00` (2/4),
`colour_cycle_streamer` (0/4, the palette writer, newly admitted) and `FUN_0001828e` (0/4).

## A font that fails silently needs a checker (2026-09-27)

TESTCART's tc_glyph knows 45 glyphs -- space, ! 0-9 A-Z : . - > | % + -- and
returns glyph 0 for anything else. Glyph 0 is a BLANK. It does not warn, and
the result reads as a spacing choice rather than a bug.

The help page went up saying "?  NO REFERENCE YET" and rendered "   NO
REFERENCE YET". Following it back: **the same '?' is the delta column's
no-reference marker and has been invisible since that column was written.**
Every row with no reference has been displaying a blank where it meant to say
"nothing to compare", for as long as the column has existed.

`make glyphcheck` (tools/tc_glyphcheck.py) parses the glyph set out of
tc_glyph itself and checks every string the cart actually prints. The marker is
"---" now, which the font has.

**Two passes to make the tool worth reading.** The first scanned the raw file
and reported thirty missing glyphs -- all lowercase letters inside explanatory
comments. The second stripped comments and still flagged `#include "common.h"`,
`".rodata"` and the asm constraint `"+d"`. Only the third, which looks at
tc_puts/tc_frame arguments and the four on-screen tables, reports just the real
one. **A checker whose output is mostly false positives is a checker nobody
reads twice**, and the work of narrowing it is the work of making it useful.

## Two build failures that read as one (2026-09-27)

Adding the splash and help pages produced a black screen and a work-RAM dump
with zero tests recorded -- which looks like the cart hanging. It was not
running at all: `rom/s16.32x` was a NON-TESTCART build, because the link had
been failing and I had read `rc=0` from a compound command where the code came
from a later statement.

The link error underneath: **.ramtext overlapped .tilesmd by 548 bytes.** The
SH-2 probe and the FMDL executor had gone into m_main's hot SDRAM-resident
region, which mars.ld:112 guards precisely against. They belong in .porttest --
the measured free ROM gap above the blob, "empty and harmless when PORTTEST is
off" -- because a suite that runs once a boot has no business in the renderer's
residency budget.

Two rules out of it. **Check the artefact, not the exit code**: `strings -a rom
| grep -c <a-string-only-this-build-has>` settles in one line what a stamp and
an `rc` both got wrong. And **moving code between residency regions changes any
timing that code takes of itself** -- sh_probe's absolute per-access figures
were measured SDRAM-resident and are not comparable with post-move ones; its
DIFFERENTIAL results are, by construction, because the loop appears on both
sides of every subtraction.

## 95 of 99 (95%), 27 of 28 functions: three name/scope faults in one chain

`FUN_0001828e` and `FUN_00003a00` both resolved, and the causes were all mine:

1. **A ROM symbol used as an array base indexes in BYTES.** `FUN_00003a00` reads
   `*(undefined2 *)(&DAT_000040f0 + (short)uVar1)`, a word table at 68K 0x40F0. Declared
   `uint`, `&sym + n` scaled the index by 4 while the 68K's offset is a byte offset -- it read
   the wrong entry and got 0x4352 where the oracle has 0x101. The cast that follows supplies
   the access width, so the base only has to make the arithmetic come out in bytes. Same rule
   as work-RAM array bases.
2. **`extraout_A0_00` was never initialised**, because the pseudo-register name class
   `[A-Za-z0-9]+` stops at the underscore in Ghidra's suffix. The declaration never matched,
   the local stayed garbage, and `unaff_A6[0x22] = *extraout_A0_00` read an uninitialised
   pointer. **Third bug today from that character class** -- it also cost the A-register
   seeding and the D-register seeding.
3. **AN INNER CALL CAN BE MADE BY ANY FUNCTION IN THE CLOSURE, not just the outer one.**
   `FUN_0001828e`'s `extraout_A0_00` is what A0 holds after `FUN_0000673e` returns -- and that
   call is made by `FUN_0000669c`, so its caller address lies inside FUN_0000669c's range.
   Searching only the outer function's range matched the WRONG records; they happened to
   AGREE, so the unpairable check passed, and the agreed value was 0x01828E -- the outer
   function's own address, making `*extraout_A0_00` read its own code bytes as data.

**With the wider search the records genuinely disagree, so FUN_0001828e is now excluded as
unpairable rather than scored.** That is the right answer: it was never a port defect, and
the earlier "candidate defect reached by elimination" verdict was wrong because my
elimination rested on a search that was looking in the wrong place.

**One failure left: `colour_cycle_streamer`**, parked with its reason -- seeding its pointer
correctly does not bound its loop, because the terminator lives in data the vector never
captured.

The arc for the day, same oracle vectors throughout: **3 cases -> 95 of 99**, 27 of 28
functions verified against the arcade. Not one of the failures traced to the decompiled code.

## The 2.2% is a STEAL, not a miscount -- the frame really is 262 lines (2026-09-27)

The open question behind T24: 57,344 known cycles measure as 120 raster lines,
so a line is 477.9 68K cycles, while the nominal arithmetic (53.693175 MHz / 7
/ (262 x 59.92)) says 488.57. Two explanations fitted -- the frame is not 262
lines, or cycles are being stolen -- and they are not the same thing.

T30 counts them. **262 on ares and on BlastEm.** Counting V TRANSITIONS, not V
values: the MD's V counter is a byte that JUMPS (NTSC runs 0x00-0xEA then
repeats 0xE5-0xFF), so distinct values are not lines and the maximum is not the
count. A transition is a line however the counter is numbered.

So the frame geometry is exactly right and the gap is real time the 68K does
not get:

    262 lines x 477.9 cycles available  = 125,210 cycles a frame
    7,670,453 Hz / 59.92 fps            = 128,012 cycles a frame elapsed
                                          -------
                                           2,802 cycles a frame, 2.19%, gone

**~2,800 68K cycles a frame are unavailable**, and VDP DRAM refresh during
active display is the right mechanism and the right magnitude. Not asserted:
the MiSTer core asserts REFRESH only under DMA_VBUS (GEN/vdp.sv:721), so if
the FPGA reads 477.9 too it is not that core's refresh path and something else
takes them. T24 and T30 together answer it in two numbers at the rig.

**The method: when two explanations fit one number, find the measurement that
separates them rather than the argument that picks one.** Line count and cycle
rate are independent, and measuring the first settled the second.

## The Z80 executes instructions (2026-09-27)

The Z80 page proved the BUS -- reset, grant, the 0xA00000 window, byte and word
access. **Every one of those tests would pass against a socket with no CPU in
it.** T32 uploads seven bytes and lets it work:

    F3        DI
    3E 20     LD A,0x20
    C6 3A     ADD A,0x3A      -- must FETCH, DECODE and compute
    32 00 1F  LD (0x1F00),A
    76        HALT

**ADD rather than a second LD on purpose.** A machine that merely moved the
byte through would leave 0x20, so the answer names which happened: 0x5A ran it,
0x20 copied it, 0x00 is a dead CPU. Both machines read 0x5A.

The sequence is the same ORDER IS LAW the page already established -- release
reset, take the bus, write, pulse reset so PC is 0, hand the bus back, release
reset -- and it restores md_start.s's park with the PSG re-muted by hand.

**A bus test is not a CPU test.** Anything that only ever reads and writes
memory the other processor owns proves the PATH, never the PART.

## Coverage push: VSRAM is 11 bits, and a timed DMA is not a verified DMA (2026-09-27)

Asked what the suite does NOT touch. Five tests and a new MD VDP page.

**VSRAM had never been addressed at all** -- the VDP's third memory space, 40
entries in H40. Same control encoding shape as CRAM, different code: write is
CD=5 (first word 0x4000|addr, second 0x10), read is CD=4 (0x0000|addr, 0x10).
All 40 read back. **Writing 0xFFFF reads back 0x07FF, so an entry is ELEVEN
bits** -- I had assumed ten, and the test measures it rather than masking to an
assumption.

**T18 and T19 timed DMA fill and copy and read nothing back.** A DMA that takes
exactly the right number of lines and moves nothing would pass both. The new
checks read the destination -- and failed on the first run, informatively:
`0x7F00013C` is 127 of 128 bad, first at word 1, low byte right and high byte
stale.

That is a strided fill. **A VDP VRAM fill writes BYTES, stepping by the
autoincrement**, so at the suite's usual autoinc of 2 it writes every other
byte. The fix is autoinc 1 for the fill and the copy.

**And T18's timing is unaffected, which is the part worth separating.** The VDP
still performs 256 write operations at either autoincrement, so the slot
arithmetic predicting 14.22 lines is about the OPERATIONS and not about where
they land. Timing and data are two claims; only the data one was wrong.

**The status register now cross-checks the clock.** 0xC00004's VBLANK bit must
agree with the raster counter this suite times everything with, sampled both
sides of the edge -- one sample could agree by luck. Two independent indicators
of one fact: if they ever disagree, one is lying and the timing page is built
on the other.

### The link failure that was not what it said

TESTCART stopped linking with ".ramtext ROM image reached .tilesmd". The
obvious reading -- my SH-2 test code is in the hot region -- was wrong.
**.ramtext is byte for byte the same size in both builds (0x6E60)**; what moved
was its LOAD ADDRESS, because defining TEST_CART for the SH-2 grows .text by
~12 KB underneath it. Every TEST_CART function was already in .porttest.

So the fix is not to trim the suite but to step .tilesmd and .porttest up a
page, and only when `--defsym __testcart=1` is passed, leaving the shipping
layout byte-identical. **An error that names the section it collided with does
not name the section that grew.** Measure both before trimming either.

One real thing did come out of chasing it: consolidating the SH-2 test entry
into a single `.porttest` function rather than per-function attributes on the
pieces. Under LTO a static callee carrying its own section attribute can still
be inlined into a .ramtext caller, and the symptom appears hundreds of bytes
later as a collision that names the victim, never the cause.

## The DREQ FIFO is proved end to end (2026-09-27)

The FMDL proof shows the 68K staging a list in the framebuffer and the SH-2
executing it. **That is not how this port moves per-frame data.** The DREQ FIFO
is: the 68K pushes words into 0xA15112 with 68S set and the SH-2's DMAC pulls
them out of 0x20004012 into SDRAM. Every packet the renderer eats arrives that
way, and nothing tested it end to end.

It is also the path carrying the most measured pain in this project -- partial
DREQ landings, the lost-push belt, "no packet lands in MAME at all under R60".
**A transport with that history and no test is the wrong shape.**

Two rows, and they separate the two sides: the 68K reports how many words it
actually got into the FIFO, and the SH-2 reports the SUM of what arrived. One
number over the comm port still catches a wrong value, a wrong order and a
short transfer -- 64 words of 0x1000+i sum to 0x07E0 in sixteen bits. Both PASS
on ares and BlastEm, first run.

Three design points worth keeping:

- **The handshake enforces the order, rather than a delay guessing at it.**
  The SH-2 must have armed its DMAC before the first word is pushed, so it
  posts DREQ_ARMED and the 68K waits for that. A delay here would be a race
  dressed as a test.
- **The destination is zeroed first**, so a DMA that does nothing scores 0
  instead of inheriting whatever was there. Same reason the FMDL proof clears
  its framebuffer range.
- **The push loop refuses to wedge.** It spins on the FIFO-full bit with a
  bound and stops pushing if the bound expires, so a FIFO nobody is draining
  produces a short count and a FAIL rather than a hang that looks like a dead
  machine.

The protocol was taken from the shipping code, not invented: m_main.c:12010
for the SH-2 arm (SAR0 = 0x20004012, CHCR0 = 0x44E1) and md_main.c:3821 for
the 68K side. A transport test that used its own protocol would prove only
that the test works.

## A fourth platform, and it found the masks (2026-09-27)

Mike ran the cart in OpenEmu -- Genesis Plus GX. The PLATFORM line read
UNKNOWN / NEW MACHINE, which is the classifier working: its BMMR readback is a
third value nobody had seen. 48 graded tests, all PASS.

**It agrees on everything structural.** 57,344 cycles measure as 120 lines, so
477.9 cycles a line; the frame is 262 lines; the Z80 page passes throughout
including Z80 EXECUTES; and BOTH of the port's transports -- the DREQ FIFO and
the FMDL round trip -- work. Three independent emulators now agree on the
68K clock model and on the transports.

**It is the odd one out on the VDP.** Full column in
fingerprints/genesis_plus_gx.txt; the two that matter:

- **VRAM reads are not slot-charged.** T16 reads 89 lines where ares and
  BlastEm read 113, and 88.03 is that loop's PURE INSTRUCTION COST -- so GPGX
  charges no VDP access slot for a VRAM read, while charging full price for a
  VRAM write (T0A matches at 113). The 18-slot derivation makes a VRAM word
  two slots in either direction.
- **It does not mask CRAM or VSRAM on readback.** Write 0xFFFF to MD CRAM and
  0xFFFF comes back, where ares and BlastEm return 0x0EEE for a NINE-bit
  register; VSRAM likewise returns 0xFFFF against 0x07FF for an eleven-bit
  entry. Those widths are not opinions -- the 9-bit packing is what
  bake_tilecram.py emits and the SH-2 reads back, and the pink-trees bug was
  that field layout being wrong.

**And it sharpens the one row that matters most for silicon.** T1D -- a 32X
CRAM write during active scan in packed-pixel mode -- now lands on THREE
independent emulators, against a manual that says it must be refused and a
MiSTer core that implements the refusal exactly. **Three emulators agreeing
against the documentation is not evidence the documentation is wrong; it is
three emulators sharing a simplification.** cram_paint has no PEN guard, so if
the manual is right this port drops palette writes on real hardware and no
instrument we own can show it.

**The general point: a platform the classifier cannot name is the most
valuable run you can get.** Every row it disagrees on is a place where "the
machines agree" was only ever "the two machines I had agreed".

## The reachable port compiles as ONE rom-linkable unit (2026-09-27)

`tools/gen_selftest.py --port FUN_0000040e --full-mirror` emits the 410 functions reachable
from the frame entry plus the 104 observed handlers as a single SH-2 translation unit: **0
errors, 310,656 B of text, 532 B of bss**, with every porting rule applied -- 953 shadow
initialisations, 207 absolute-short rewrites, 31 sixteen-bit pointer translations, 40
high-bit narrowing assignments, 5 sanitised string names.

Reusing the self-test generator rather than integrating `layout.ld`'s 590 PROVIDEs into
`mars.ld` was the right call: the mirror, the WP() macros, the ROM routing to the cart's own
copy at GAMEHIGH, the hardware stub and all four addressing rewrites already existed and
already agreed with the oracle.

Twelve more Ghidra forms had to be handled, each one line and each previously invisible
because no VECTORED function used it: `undefined1/3/6`, `int3`, `uint6`, the condition-flag
pseudo-registers (`in_CF`, `in_ZF`, `in_XF` -- the A/D-only pattern never collected them),
`BADSPACEBASE` as a TYPE not a variable, `stack0x*` / `ram0x*` placeholders, `PTR_*` tables
with multiple suffixes, `LAB_*` code labels, string symbols carrying the string's own
characters, and `code *` that is subscripted rather than called.

Three of those are worth keeping as rules:

* **A `LAB_x` used as a VALUE and a `LAB_x:` used as a GOTO LABEL are the same name.**
  `#define`-ing the label form to a number broke every one and took the error count from 21
  to 670. A name followed by a colon is a label; one that is not is a value.
* **A string symbol is an ARRAY and a `DAT_` base is not.** Bodies subscript `s_X[i]` but
  reach a data base through `&sym + n`, so the string needs a pointer and the base needs a
  dereference.
* **No vector cases in port mode**, and clearing them at SELECTION rather than afterwards:
  the case list is built from the candidates, so emptying `vec` later changes nothing. 203
  cases of scaffolding put 597 KB where the port is 303.

**And a process failure that cost three rounds: `str.replace` with no match is a SILENT
no-op.** Three of my patch scripts this session printed success while changing nothing --
including one whose `assert` covered a different replacement in the same script, and one
where an assert failure discarded an earlier good edit because the write came last. Every
replacement now asserts, and the assertion reads the FILE BACK rather than trusting the
in-memory string.

## The DREQ FIFO loses the first word, on the FPGA, reproducibly (2026-09-27)

First full rig run of the suite. 31 graded tests, 29 pass, and the two that
fail are the same transport:

    T36 DREQ SH2 SUM     F7E0  against 07E0 -- short by exactly 0x1000
    T37 DREQ FIRST WORD  0000  against 1000

The 68K pushed all 64 words (T35 passes). The SH-2's DMAC received 63 of them
**in their correct slots**, and slot 0 was never written -- it still holds the
zero the receiver put there. That distinguishes the two candidates: a SHIFT
would leave 0x1001 in slot 0 and produce the same short sum. **The first word
of the transfer is lost.** Twice, on two launches. ares, BlastEm and Genesis
Plus GX all pass.

**CAUSE FOUND THE SAME EVENING -- see "The lost DREQ word was the missing FIFO
pointer reset" below. This entry stands as the capture; that one is the
diagnosis.**

**This is the first deterministic capture of "partial DREQ landings" and "the
lost-push belt".** That failure has been inferred from pixels, packet censuses
and FIFO counters for months. It is now two numbers on a screen, reproducible,
on the transport every frame of the shipping game goes through.

The second row is what made it a diagnosis instead of a symptom. The sum alone
says how much content is missing and never says what happened -- three
different faults produce "short by 0x1000". **Reporting the first word turns
"something is missing" into which thing.** Cost: one extra word over a comm
port.

### Five RTL-derived rows land on the machine that runs that RTL

T0A 113, T16 114, T0D 28, T18 14, T19 28 -- against predictions of 113.78,
113.78, 28.44, 14.22, 28.44 from counting 18 external access slots a line out
of GEN/vdp.sv. The derivation holds where it should.

And T2E reads 80C3, exactly the core's own BMMR_MASK. **Two of the three
emulators have that register wrong** (ares B3FF, Genesis Plus GX 80FF); only
BlastEm matched.

### The FPGA gives the 68K 473.9 cycles a line

T24 reads 121 lines for 57,344 known cycles against 120 on all three
emulators, with the frame still 262 lines. So the FPGA hands the 68K about
0.8% less than the emulators do and 3.0% less than the nominal clock. The
known penalties are all present and unchanged: WRAM +03, cart +05 (the core's
ROM DTACK wait), framebuffer +03 to +06.

### And it refutes something I asserted this morning

**T1D 32X CRAM ACTIVE reads 5678 on the FPGA**, the same as all three
emulators. I read GEN/32X VDP.sv:402-406 and :170 as implementing the manual's
PEN refusal, called the MiSTer core "the manual implemented exactly", and
predicted 0000 here. The machine that runs that RTL lands the write.

My reading was wrong about which path the gate covers -- the 68K's route to
0xA15200 is evidently not the one PEN guards. **Reading RTL is still
derivation, and a derivation the hardware disagrees with is just wrong.** Four
machines now land a write the manual says must be refused; real Sega silicon
is the only authority left and the weight has moved a long way from my
prediction.

## The lost DREQ word was the missing FIFO pointer reset (2026-09-27)

Three candidates for the FPGA losing the first word of a DREQ push, run as
three variants of one test in ONE rom so they share a boot, an SH-2 and a
FIFO -- and so the rig answers in one screenshot instead of three launches:

    A  no reset      first word 0000   LOST      (the control, as shipped in
                                                  the suite until now)
    B  with reset    first word 1000   CORRECT
    C  reset+dummy   first word DEAD   the dummy landed

**The cause is the missing 68S-off pointer reset and nothing else.** The
shipping code has it -- md_main.c:3855, `*ctrl = 0` before `*ctrl = 4`, with
the comment "68S off: resets the FIFO pointers (erec12: an aborted push left 8
words in the FIFO and every later landing was offset by them)". The suite's
first DREQ test did not copy it. Add it and the transport is exact.

**And C refutes the other hypothesis.** A dummy word pushed ahead of the
payload arrives in slot 0 as 0xDEAD, so a write issued immediately after 68S
is NOT being eaten -- the FIFO simply starts at the wrong place without the
reset. One test killed a plausible workaround that would otherwise have been
adopted on faith and cost a word of bandwidth on every push for ever.

### Three emulators pass the broken variant

ares, BlastEm and Genesis Plus GX all return 0x07E0 and 0x1000 for variant A.
**None of them models the FIFO pointer state that makes the reset necessary**,
which is exactly why this class of bug has only ever shown up on hardware, and
why erec12 had to be found by someone staring at offset landings rather than
by a test.

### What the suite does with it now

B is GRADED -- correct usage must hold on every machine. A and C REPORT, with
the correct answer as their reference, so the FPGA shows `+F000` and `-1000`
against a row that reads `.` on the emulators. **A permanent FAIL for a
variant that exists to document a quirk is noise; a delta against the right
answer is the finding.** The FPGA now reads 30-30 PASS with the whole
diagnosis visible on one page.

### The general shape

**When the shipping code carries an undocumented-looking workaround, copy it
into the test or test its absence deliberately -- do not quietly omit it.** The
suite reproduced a months-old production bug purely by leaving out a line
someone had already paid to discover. That was luck this time: the omission
found the cause. It could as easily have sent me hunting the core's RTL.

## A HOUSE UNIT IS NOT A JUSTIFIED SAMPLE SIZE (2026-09-30)

**The multi-frame L2 gate was built to run 30 frames, and nobody derived 30.** Mike
asked "why 30?" and the honest answer was "I picked it". Mining the corpus for what
primed it found the mechanism, and it is worth knowing because it will happen again.

**"A 30-frame window" is this repo's house unit for a sampling window.**
`MESSAGES.md:852` ("23 later windows of 30 frames each"), `LESSONS.md:5520`
("re-sampled at 30 frames a window"), `docs/design/ORACLE-PLAYTHROUGH.md:71`
("every 30 frames"), `docs/design/BOSSFIGHT.md:799` ("the 30-frame build") -- and it
is promoted into **`FACTS.md:655` and `:658`**, the file this project's CLAUDE.md
instructs you to grep BEFORE deriving anything. So the number arrives pre-approved.

**The record also carried the caveat, and only the unit was inherited.**
`MESSAGES.md:882`, in the same paragraph: *"23 windows x 30 frames is 690 of 103,000
frames, 0.67%. That cannot exclude a brief appearance somewhere I did not look."*

**The unit was CORRECT for its original purpose and WRONG for the new one.** There
it detects a handler that runs 100% of frames while active -- 30 frames is ample and
the reasoning is explicit. Transplanted to "does the port SUSTAIN the game", coverage
is the entire question, and 30 frames is half a second.

**The project's own bar for this was already written down**:
`docs/design/STATIC-SCENE.md:130` -- "a milestone is a long ares run plus a
verifier". Run lengths elsewhere in the record are 900 vints (8 uses), 3900 vints,
103,000 frames. 30 is one to three orders of magnitude below standard.

**THE RULE.** A sample size carried over from the record is a borrowed unit, not a
derived one. Say which it is in the same sentence you first use it, and when it is
borrowed, go and find what the record says the bar is -- here that cost one grep and
the answer was in a file called STATIC-SCENE.md under the word "milestone".

## FACTS.md could not see 56 of 67 lesson headings, and that is how a rule got re-derived (2026-09-30)

**I re-derived "a symbol used as an array base indexes in its own element size".**
LESSONS has carried it since 2026-09-27 under the `##` heading "Two more width rules
from the same root", and `tools/gen_selftest.py:665` has implemented it since --
`if nm2 in _arraybase and addr in _orw: w = _orw[addr]`, the oracle's NARROWEST
observed access. I rebuilt the principle from scratch today as if it were new.

**The lookup could not have found it.** `tools/build_facts.py`'s `lessons_facts()`
matched `^## ` only to set the section NAME and never emitted it; only `###`
headings and `Addendum` blocks were indexed. Measured: **56 of the 67 `##` headings
in LESSONS.md were absent from FACTS.md**, and `grep -i width FACTS.md` returned
seven lines, none of them this rule. CLAUDE.md's instruction is "BEFORE DERIVING
ANYTHING: grep FACTS.md"; the grep was sound and the index was not.

The function's own docstring carried the assumption: *"Every ### heading in LESSONS
is a claim"*. For 67 lessons the claim IS the `##` heading and the rule is in its
body. Fixed; FACTS.md went 870 -> 937 facts.

**Both failures are real and they are different.** Mine: a question about declared
widths should have started at the file that decides them and at LESSONS, not at a
blank page -- CLAUDE.md's own rule 1, "for a question about a FEATURE, read the file
that implements it first". The tool's: an index that silently omits 84% of one
heading level is worse than no index, because it converts "I checked" into evidence
of absence.

**AND THE RULE WAS NEVER APPLIED TO THE PORT.** `gen_selftest.py` is the L1 self-test
harness; `cport_prep.py` generates the port's own `dat_syms.h` and had no array-base
rule at all. So the port still carried the exact bug the 2026-09-27 lesson
describes: `extern uint DAT_ffffec80` with the body doing `(&DAT_ffffec80)[iVar1]`,
indexing four times too far. Two generators, two different width decisions, one of
them three days out of date -- the general form is in
[[derive-dont-guess]]: single-source the decision or it drifts.

## An impossible sub-number is how an out-of-bounds write got caught (2026-10-01)

`PORTENDURE=900` free-ran 900 frames against a **256-frame** delta chain, and
`l2m_score(w, 900, ...)` walks `fcnt[]`/`fcur[]` for `j = 1..k`. `L2M_MAX` is 288, so
it read out of bounds and -- worse -- **wrote `fcur[j] = 0` for j up to 900, 612
bytes past the end of a `.bss` array.**

**Nothing crashed and the headline number looked fine.** The endpoint diff read 1,168
wrong against a control of 1,156: net +12, a plausible and even flattering result.
What gave it away was a SUB-number that cannot be true: the scorer reported
`missed 0` beside a four-figure wrong count. Missed and unchanged-but-wrong sum to
the total by construction, so `missed 0` with 1,168 wrong is arithmetically
impossible.

**The rule: publish the decomposition next to every total, and check it sums.** The
decomposition is not decoration -- it is a free internal consistency check, and here
it was the only thing standing between a memory-corrupting instrument and a number
that would have gone in a report. This project has now been saved twice by a
component reading zero when the total said otherwise (the other was
`dispatch entered 0` beside a frame that scored better than the control).

**And beyond the chain, REFUSE rather than clamp.** `l2m_score` now returns
0xFFFFFFFF when `k` exceeds the delta chain, because there is no expected image to
score against; clamping k would have produced a number computed from the wrong
frame's data, which is the same failure wearing a different hat. The host-side
endpoint diff is the valid instrument for N beyond the chain.

## A SHAPE IS NOT A DISCRIMINATOR: two false-positive tests in one session (2026-10-01)

Twice in one session a pattern match over the whole unit produced a confident number
that was entirely artifact, and both times the shape being matched was common for
reasons unrelated to the thing being looked for.

**1. "192 absolute-short misses."** The test counted every `(intvar + -0xNNNN)`
whose 16-bit two's complement lands in 0xC000-0xFFFF -- the work-RAM range -- and
called each one an unrewritten 68K absolute-short address. All 192 were arithmetic.
`sVar7 + -0xe0` is `sprite_build_depth_banded` comparing a coordinate against 224,
and **every `x - small constant` lands in 0xC000-0xFFFF as a 16-bit two's
complement**, so the test could only ever return "all of them". Absolute-short
addressing is a DEREFERENCE whose base is an INTEGER -- the record already said so
("absolute-short forms its address from an INTEGER, that is the entire
distinction"). Requiring the dereference: 17 forms, 15 of them A-register
displacements `(-d16,An)` and 2 pointer vars. **Zero misses.**

**2. "19 object fields that separate high-error frames from low-error ones."** 5
slots x 128 offsets = 640 comparisons against 28 sampled frames, and 17 of the 19
"disjoint" fields were the ODD bytes of 16-bit coordinate words -- maximum-entropy
data, which is what chance produces at that many tests.

**THE RULE. Before running a pattern test over thousands of sites, state what would
make a hit a FALSE positive, and check that the pattern excludes it.** If the answer
is "nothing", the test measures the frequency of the shape and not the presence of
the defect. A count produced that way is worse than no count, because it arrives
looking like evidence and it takes a specific counter-example to dislodge -- here,
recognising one expression I had read an hour earlier in another context.

Both were caught the same way: the hit list was READ, not just counted. A test that
reports a number and not its instances cannot be checked at all.

**A third form, 2026-10-01: a GLOBAL counter read across a call attributes the
CALLEE's behaviour to the caller.** Checking which of the frame body's 14 calls
resolved, by comparing the indirect-call MISS counter before and after each one:

    uint before = port_l2_out[13];
    port_call68_at(7, FRAME_BODY[j]);
    port_body_hit[j] = (port_l2_out[13] == before);

It reported **7 of 14 missed** -- and every one of the 14 was in the dispatch table,
verified by grep. The counter also counts misses made INSIDE the call, so any
function whose own callees missed was recorded as having missed itself.

The replacement measures the thing directly: write 1 before the call and 2 after, so
the three states that matter are distinguishable -- never reached (0), **HUNG here**
(1), completed (2). A before/after delta on shared state measures the span, not the
subject.

## .build_flags DID NOT CONTAIN ANY PORT* PROBE FLAG (2026-10-02)

`.build_flags` is written by a parse-time `$(shell)` at Makefile:3684. Every PORT*
probe flag -- PORTSEED, PORTZOOM, PORTENDURE, PORTBODY, PORTWATCH, PORTREGS -- is
appended to SHCCFLAGS about 550 lines BELOW that line, so the stamp never saw one.
Measured: with `PORTSEED=64` on the command line the stamp held `-DPORT_FULL` and
no `-DL2M_SEEDAT`.

**What it cost:** switching between PORT* flag values changed nothing make could
see, so `port_l2.o` was reused and the "new" probe measured the old object. Any
PORT*-flag-only comparison from before 2026-10-02 is two readings of the same
object unless a source file happened to change in between.

**The fix:** the stamp is now the LAST parse-time statement in the Makefile.
Anything that appends to MDCCFLAGS or SHCCFLAGS below it is invisible to make.

**The general form:** a parse-time `$(shell)` captures a variable AS IT STANDS AT
THAT LINE. A stamp built from accumulating variables has to be last, and
`.build_flags` only ever covers -D flags -- a generated source whose CONTENT
depends on a non--D variable (CPORT_ROOTS, CPORT_OPT) needs its own stamp. See
[[a-tmp-file-is-not-a-build-input]].

## A SIGNATURE PARSER THAT IS WRONG ON EXACTLY ONE ENTRY CAN HIDE FOR WEEKS

`tools/cport_dispatch.py` accumulated lines of `protos.h` until it saw a `;`. The
file's header comment and its `#ifndef`/`#define`/`#include` contain no semicolon,
so all of it was glued onto the FIRST declaration; the `^`-anchored regex rejected
the result and that one handler silently got the no-argument thunk.

It was invisible because the first declaration happened to be
`uint FUN_00000bd6();` -- which takes no arguments, so the wrong thunk was the
right thunk. Rooting `test_mode_screen` (2026-10-02) moved
`FUN_0000057e(byte *p_A1,uint p_D0)` into first place and gcc said "too few
arguments to function".

**The rule:** a generator that parses declarations must report how many it
UNDERSTOOD against how many it SAW. `719 of 719` is a check; a silent fallback to
a safe-looking default is not.

## A /tmp FILE IS NOT A BUILD INPUT

`tools/cport_l2seed.py` read `/tmp/disp_regs.json`, written by hand on 2026-09-28
and never committed -- correctly, since it is ROM-derived. When /tmp was cleared,
`make ... PORTFULL=1` died with a FileNotFoundError and produced no rom at all.

**The rule:** a build step whose input cannot be regenerated from the repo must
DEGRADE and say so, not fail. It now emits an empty seed and prints
`NO CAPTURE ... PORTREGS is a no-op in this build`. Degrading is right here only
because the experiment is superseded -- the register chain it proved is built into
`cport_prep.py`. Where the input still matters, the answer is to commit a
regenerator, not to soften the failure.

## REFUSE A CURVE YOU CANNOT MEASURE RATHER THAN PUBLISH ITS LOOKALIKE

Under PORTMAIN the frame boundary is a `__builtin_longjmp` out of the game's own
main loop, so every frame re-enters at 0x5BE and re-runs the prologue. The
per-frame curve is unaffected (the seed overwrites all 16 KB after the prologue).
FREE-RUN would have come out as an exact duplicate of the per-frame curve wearing
free-run's label -- a plausible, flat, wrong number of precisely the kind this
project has produced eight times.

The driver publishes `'NOFR'` in `port_l2m_out[195]` and zeroes the free-run slots.
A missing number is readable; a lookalike is not.

## AN OBJECT NAMED IN A LINKER SCRIPT IS A DEMAND, NOT A FILTER (2026-10-02)

`make line` -- THE SHIPPING LINE -- could not link, and had not been able to since
commit e4fb3e1e. `sh_src/mars.ld`'s `.portcode` block listed three PORTFULL objects
by name:

    sh_src/portfull/port.o(.text .text.* .rodata .rodata.*)

A filename WITHOUT wildcard characters in a SECTIONS command makes ld LOAD that
file, whether or not it is on the link line. So every non-PORTFULL build broke in
both directions: with the objects present they were pulled in without `layout.ld`
(hundreds of undefined references, plus a `.portcode`/`.sprbake` LMA overlap);
with them absent, "cannot find sh_src/portfull/port.o".

**The fix** is one pattern WITH a wildcard -- `sh_src/portfull/*.o(...)` -- which ld
matches against the files already in the link. The same file's line 41 already used
`EXCLUDE_FILE(sh_src/portfull/*.o)`, so the wildcard form was right there.

**The general rule:** a linker script must never name a conditionally-built object
without a wildcard. And a flag-gated section is a reason to rebuild THE DEFAULT
CONFIGURATION, not just the flagged one.

## PROVE THE ROM IS FRESH BEFORE MEASURING IT (2026-10-02)

Two of my own method errors in one session, both producing confident wrong readings:

1. **I read dumps that were never written.** `timeout 120 ares --dump ...` KILLS
   ares, and ares writes its dumps at exit, so the dump file does not appear. The
   reader then read a stale file from a previous run (or the shell hid the error)
   and I reported "no marker" -- which I took as "the driver never ran".
2. **I measured a stale rom.** When a link fails, `rom/s16.32x` is whatever was
   there before. I profiled it, saw 100% of both SH-2s in the 32X BIOS, and
   concluded THE LINE DOES NOT BOOT. The line builds and boots fine; the link had
   failed and I was profiling an old file.

**The rule:** `python3 tools/build_id.py show rom/X.32x` before any measurement --
it prints the commit and the build timestamp. And check the emulator's exit status;
a killed run has no output, which is not the same as an output of zero.

## A GENERATOR THAT MATCHES SYMBOL NAMES BY STRING EQUALITY WILL DROP SOME

`cport_prep.py` had a guard against fabricating a ROM table as a 64-byte ZEROED
array (the 2026-09-28 bug: 208 indirect calls a frame resolving to address 0). The
guard compared names to `docs/audit/sym_addrs.txt` with `==`, and Ghidra spells one
symbol three ways:

    PTR_LAB_00010202+1_00001cda   the listing      (`+` is not a C identifier char)
    PTR_LAB_00010202_1_00001cda   the decompiler
    PTR_DAT_00006dc0 / PTR_FUN_00006dc0            (data in one view, function in
                                                    the other, same address)
    LAB_00002686_2                                  a short numeric disambiguator,
                                                    not an address

Fifteen tables were being fabricated as zeros, six of them silently through those
holes. One was `PTR_LAB_00010202_1_00001cda` at ROM 0x1CDA -- read by the MAIN
LOOP'S PROLOGUE to set 0xFFFFF142, the mode byte the frame loop branches on.

**The fix:** match on the ADDRESS, which every one of these names carries, in ONE
shared resolver (`tools/cport_names.py`) that both tools import so they cannot
drift. 15 fabricated -> 1, and the wild-jump class (`PTR_`/`switch`/`LAB_`) now
REFUSES the build rather than dispatching to address 0.

**Measured neutral at the seed frame** (still 9 wrong / net -43): the fix is correct
and those six tables do not affect that frame's work RAM. A correct fix is allowed
to measure zero.

## A 68K POINTER ROUND-TRIPPED THROUGH 16 BITS IS WHY THE MAIN LOOP HANGS

The 68000 held any work-RAM pointer in 16 bits -- work RAM was 0xFFxxxx and the
hardware sign-extended. The decompiled C does the same: it stores a pointer in a
`short` and re-expands it with `(T *)(int)x`. On the SH-2 work RAM is at
`_WRAM_BASE` (0x0602CC00), so the truncation DESTROYS the pointer and the
re-expansion yields an address in the 32X BIOS.

`FUN_0000e16a` scans the object table with an EMPTY for-loop that stops only at a
negative sentinel, following `psVar10[0x3c]` -- a 16-bit work-RAM pointer -- with no
translation. It walks into the BIOS and never returns: 8000 ares frames without
finishing one seeded frame.

    59 sites in 40 functions   (T *)(int)x
    11 sites                   (short)<pointer>
    29 sites in  3 functions   register0x0000003c, Ghidra's STACK placeholder, used
                               as a pointer (an uninitialised global in the port)

`w68.h` has had the correct translation, `W68()`, the whole time. Nothing applied it
and no type forbade the mix. **This makes PORT-ARCH step 3 (the `a68` type) a
PRECONDITION for step 1, not a follow-up:** rooting the real main loop reaches code
the hand-rolled four-call body never executed, and that code cannot run without the
domain separation. `tools/port_guard.py` check 4 counts both forms.

## READ THE GAME'S OWN PROGRESS MARKER BEFORE READING ITS CELLS (2026-10-02)

Altered Beast stores the 68K address it last passed into **0xFFFFFEFC/FEFE**. The
decompiler treats those as dead stores. They are the most useful two cells in work
RAM, because they say WHERE IN THE PROGRAM each machine is.

Measured on the rooted main loop: arcade **0x0AE2** (inside the gameplay frame loop
at 0x970-0xADC), port **0x0094**. The port had completed a frame, cleanly, of a
DIFFERENT PART OF THE PROGRAM -- the state-initialisation pass, which legitimately
clears text/tile/sprite RAM. 868 of its 871 wrong cells in 0xFFE000 were simply
zeroed by faithful code.

A byte count cannot tell **"computed the frame wrongly"** from **"ran different
code"**, and those need opposite fixes. Reading 1163-wrong as a regression of the
9-wrong baseline would have sent the next day into arithmetic that was never wrong.

The same marker validated the OLD body: the four-call frame body reads 0x0AE2 --
exactly the arcade's position. The crude list was positioned correctly all along.

`tools/portl2_seed.py` prints it first and flags a mismatch.

## GHIDRA'S register0x0000003c IS THE 68K STACK POINTER, AND IT NEEDS REAL MEMORY

68K register space: D0-D7 at 0x00-0x1C, A0-A7 at 0x20-0x3C. So **0x3C is A7, the
SP**. Ghidra could not model `move.w d0,-(sp)` / `move.w (sp)+,d0` as locals, so
the decompiled code spills through `register0x0000003c` -- and in `FUN_0000e16a`
what it spills is the LOOP COUNTER, so the reload is the loop's only exit test.

Defined as a bare `uint` it is 0: the spill lands at address -8, the reload returns
garbage, and the loop never ends. That alone hung the rooted main loop for 8000 ares
frames.

**The value was never a guess.** `lea 0xffffff00,%sp` appears five times in the
listing (prog68k.asm:272, 326, 365, and at 0x5BE and 0x62A). 0xFFFFFF00 is inside
work RAM, so the port's SP is that cell's SH-2 address and the spills land exactly
where the 68000 put them. And because 0x62A re-loads SP once per frame -- an
instruction Ghidra dropped -- the driver re-loads it at every frame boundary.

**The general form:** a Ghidra pseudo-register named `register0x<n>` is a REAL
register of the target. Look up what offset `<n>` is before treating it as a
placeholder.

## A GUARD THAT COUNTS CORRECT CODE IS DECORATION

The pointer-round-trip check counted every `(T *)(int)x` in the unit: 59 sites, so
it baselined 51 violations. 35 of those already read `(byte *)(int)W68(...)` -- the
`(int)` is cast noise around a CORRECT translation. The guard then reported
FUN_000154C0 as a NEW violation for a line that was right.

Excluding the translated form took the baseline to **12** (8 A6 widths, 3 stack
placeholders, 1 genuine pointer). A number that cannot go to zero teaches the reader
to ignore it.

## A FRAME LOOP IS ROTATED AROUND ITS VSYNC, AND THE ENTRY POINT IS WHAT POSITIONS A PORT

Altered Beast's gameplay frame loop runs 0x97C -> 0xADC, calls `wait_frame_flag` at
**0xADC**, and then runs a TAIL (0xAE2 -> 0xB7C) before branching back to 0x97C.
The arcade's capture is suspended inside that wait, so ONE ARCADE FRAME is the
rotated span `0xAE2 -> tail -> 0x97C -> body -> 0xADC`, not `0x97C -> 0xADC`.

This is why the four direct calls work as well as they do. `FUN_00003aae` is at
0x98E (head), `object_dispatcher` at 0xA54 (body), and **`FUN_0000144a` at 0xB04 --
in the TAIL.** The list straddles the vsync exactly as the rotation requires, which
is why it lands on the arcade's own progress marker (0x0AE2) and scores net -43,
while rooting the enclosing function and entering at its top lands at 0x0094 and
scores +1111.

**POSITION COMES FROM THE ENTRY POINT; STATE COMES FROM THE SEED.** Seeding 16 KB of
work RAM gives a port the arcade's state and tells it nothing about where in the
program to resume. A coroutine does not fix that either -- primed at boot it resumes
in ATTRACT MODE, and the capture is 9,064 frames into a played session. The 68K's
own stack is in the seed, but an SH-2 C call stack cannot be synthesised from 68K
return addresses.

So the entry must BE the arcade's resume point. Here that means a Ghidra function
split at 0xAE2.

## A FRAME'S INPUTS ARE THE SEED *PLUS* THE CONTROLLER, AND THE L2 GATE HAS ONLY THE SEED

Nine of the nineteen cells the rotated real frame gets wrong at the seed frame are
not computed wrongly -- they are read out of the System 16 I/O window, which the
gate does not supply:

    FUN_00001366 (bsr 0x1366, in the loop head)
        DAT_ffffe028 = ~DAT_00c41003;      /* player 1 */
        DAT_ffffe0a8 = ~DAT_00c41007;      /* player 2 */

The arcade's 0x40 at 0xFFE028 means its port read 0xBF. The port reads a constant
from its MMIO stub and cannot know otherwise.

**AND THIS PENALISES THE MORE FAITHFUL PORT.** The four-call frame body never reads
the controller, so it never writes those cells and never gets them wrong -- it
leaves the seed values, which are already correct. Running the real frame costs 9
bytes on the gate for doing something right. A gate that rewards not executing code
will keep choosing the hand-rolled body.

The fix is to capture the I/O bytes per frame alongside the work-RAM deltas and have
the stub return the frame's own values. It is derivable from the arcade and it is
not circular: the controller state is an INPUT to the frame, declared like the seed,
not read back out of the answer.

## HAND-WRITE WHAT THE DECOMPILER CANNOT EXPRESS -- AND `movem` TO -(SP) IS ONE

`FUN_0000E16A`'s loop saves its two counters across a call with

    movem.w {D1w D0w},-(SP) ; jsr 0x3F04 ; movem.w (SP)+,{D0 D1}

Ghidra flattened the moving stack pointer into fixed offsets from a single
`register0x0000003c`, wrote the counter at SP-4, read it back at SP-6 -- two
different cells -- and decremented its model of SP by 4 per iteration with no
matching restore. The port produced `10 11 12 66 CE FF FF ...` where the arcade has
`10 11 12 13 14 15 16 ...`: three correct, two garbage out of the phantom stack,
then its own 0xFF initialisation for the remaining nineteen. 21 of 132 wrong cells.

Transcribing those 30 instructions by hand took the frame from 132 wrong to 19.
Third member of the class, after `zoom_scale_lookup` (six outputs against a
two-register return) and `port_object_dispatch` (calls a 68K address out of work
RAM). The test is the same every time: not "is this wrong", but **"is there a
shape here the decompiler has no way to say"**. `movem` to `-(SP)` is one.

## RUN THE SAME THING TWICE BEFORE CALLING A DIFFERENCE A CAUSE (2026-10-02)

I added a per-frame I/O capture to the L2 oracle, saw the work RAM shift by 12 bytes
against a run without it, and reported that reading the System 16 input ports
CONSUMES A LATCH. It does not. Two identical runs -- same script, same playback, same
flags, no change at all -- differ by **13 bytes at the same 13 addresses in every
frame** (0xFFFF02..0xFFFF95). The oracle is simply not deterministic, and every
12-to-14-byte difference measured that afternoon was that wobble.

The control cost one MAME run, about fifty seconds, and I ran it third instead of
first. Two intermediate conclusions were wrong until I did.

**The consequence that matters:** `docs/proof/l2m/wram_*.bin` is NOT byte-reproducible.
A re-capture moves the baseline by up to 13 bytes. The committed corpus is therefore
treated as the fixed artefact it is -- the I/O capture was ADDED beside it, not
regenerated over it. None of the 13 wobbling cells has ever appeared in a port
wrong-cell list.

## A COMPACTION THAT OVERFLOWS ITS REGION IS A SILENT ALIAS, NOT AN ERROR

`cport_layout.py` placed the 68K's MMIO symbols linearly from a base: 0x400000 to
0xC43007 is 8.6 MB, laid into a 256 KB SDRAM. Fourteen symbols landed past the end
and reached a cell only because the 32X mirrors SDRAM every 256 KB. Nothing warned.

Two consequences, both invisible:

  * `DAT_00440000` (S16 video) and `DAT_00840000` (S16 palette) folded onto the SAME
    CELL -- 0x40000 apart in 68K space, 0 apart after the fold.
  * `DAT_00c41003` and `DAT_00c41007`, THE TWO CONTROLLER PORTS, landed outside the
    driver's 4 KB stub fill and read uninitialised SDRAM.

Compacting one 4 KB page per distinct 68K page gives 9 pages and 36 KB, every symbol
inside a region the driver can actually fill, and the generator now REFUSES if two
addresses ever map to one cell. **A placement that cannot fit must fail, not fold.**

## A DECLARATION WIDER THAN THE ARCADE'S ACCESS IS NOT A HARMLESS OVER-READ

`_DAT_fffff132` is declared `uint` by Ghidra; the arcade only ever accesses it as a
word. `FUN_0000E052` assigns it into a `ushort`, so the port read 32 bits and the
narrowing took the LOW half -- on a big-endian machine, the word at 0xFFF134 instead
of 0xFFF132. Its loop-exit comparison then never fired and it walked a ROM table 172
entries past the end.

Every over-wide declaration is a trap of this shape: the extra bytes are not ignored,
they DISPLACE the ones you wanted. 80 of 715 symbols disagree with the observed
table and 73 are declared wider.

`cport_prep` already clamped to the observed width, but only when another declared
symbol sat inside the span -- a heuristic for FINDING candidates, applied as if it
were the authority. Nothing is declared at 0xFFF133/134/135, so the clamp never fired
on the worst instance of the class. **The oracle's observed width is the authority;
an overlap test is a search hint.**

## A 16-BIT VALUE BEING ADDED TO AN ADDRESS IS AN OFFSET, NOT AN ADDRESS

The port rewrites `(int)*(short *)(...)` to `(int)W68(...)` because the 68000 held
work-RAM pointers in 16 bits. The rule carried a lookahead to skip a value that is an
ADDEND -- and the lookahead recognised only `+ identifier`. The self-relative ROM
table idiom is written `+ (int)...`:

    p_A0 = (undefined *)((int)*(short *)(&DAT_0001d33e + idx)
                       + (int)(&DAT_0001d33e + idx));

which is `lea %pc@(table),%a0 ; adda.w (a0,d0.w),%a0`. W68 turned the offset 0x0750
into `ROM_BASE + 0x750`, and FUN_0000E052 then walked a table that is not a table.
That is also what the older "hangs in 0x0E052" was.

**Measure the blast radius of a generator rule change before and after.** Widening
the lookahead to any addend changed exactly ONE site in the whole unit: 91 rewrites
to 90. A one-site change that moves the gate by two cells is a very different thing
from a 300-site change that moves it by two, and only the count tells them apart.

## A GATE THAT CANNOT SEE PROGRESS PREFERS A PORT THAT DOES NOTHING

The L2 gate scores work RAM against the arcade's next frame. It never asked whether
the game ADVANCED. 0xFFFFF02A is the game's own frame counter, incremented by
`FUN_000012EC` at the loop head, and reading it reorders everything:

    arcade                     +1
    the four-call frame body   +0     <- it never reached the loop head
    the rotated real frame     +1

**The four-call body's net -43 -- the baseline for days -- was a score for something
that is not a frame.** And in free-run the effect is worse: a port stuck in the
no-players wait at 0xB80 scored 55 wrong against a control of 54, net +1, while
advancing ONE game frame in eight. Fixing it to advance 8 of 8 took the score to 330.
**The dead port scored 275 better.**

A cell count cannot tell "ran the frame and got cells wrong" from "did not run the
frame", and the second wins because the control is also nearly still. Both readers
now print the counter first and say outright that the cell count is not comparable
when it disagrees.

**The general form: every scoring gate needs a liveness term that is independent of
the score.** Here the game supplies one for free; it had just never been read.

## 45 BYTE ACCESSES AND GHIDRA STILL CALLED IT A WORD

`DAT_fffff028` -- the credit/player-active byte -- is declared `ushort`. Every one of
the 45 accesses to it in the program is a BYTE access: `cmpib`, `btst`, `moveb`,
`bclr`, `bset`, `tstb`, `clrb`. Not one word. The arcade's observed width agrees: 1.

So `FUN_00000DFA`'s `bclr #5,0xfffff028` came out as a 16-bit read-modify-write --
`0x0100 & 0xDF = 0x0000` -- which zeroed the credit byte and its neighbour. The player
went inactive, the frame loop fell into the no-players path at 0xB80, and the game
stopped. One declaration, and the port could not run a second frame.

**`grep` the disassembly for the access widths before believing a declaration.** It
is one command and it is the authority: the instruction stream cannot be wrong about
how wide its own operand is.

## ON BIG-ENDIAN, A `.w` READ AT THE BASE OF A LONG IS THE *HIGH* HALF

    move.w (0xfffff132).w,D0w

reads the two bytes AT that address. 0xFFFFF132 holds a 32-BIT fixed-point camera
position, so those bytes are its TOP half. Ghidra renders the instruction as a bare
narrowing assignment -- `uVar2 = _DAT_fffff132;` with uVar2 a `ushort` -- and C defines
that as the LOW-order bits, which on big-endian are the bytes at 0xFFF134. The far
half. The player's position came out 0x30EC where the arcade has 0x110C, a constant
+0x1FE0 across eight fields of the player record.

**AND NARROWING THE DECLARATION IS THE WRONG FIX.** I tried it first, on the strength
of the arcade's observed width (2). `FUN_000039C8` does
`(short)((uint)_DAT_fffff132 >> 0x10)` to take the integer part; as a word that shift
is constant zero, 0xFFF120 became the literal 0x1000, and the camera died. **An
observed width is a SAMPLE: the window saw the word reads of the top half and not the
long arithmetic.** Ghidra's `uint` was right.

**The exclusion is what makes the rewrite safe.** `rng_next` does
`(short)(X >> 0x10) + (short)X` -- both halves of one long in one expression, real
32-bit arithmetic, where the bare narrowing genuinely IS the low half. So a function
that also takes the symbol's high half is left alone. One of seven candidates in the
program; the other six were all the camera.

## ONE `int` PARAMETER HOLDING BOTH A 68K ADDRESS AND AN SH-2 POINTER

`FUN_0000D608(int p_A4)` -- the collision test -- is called four ways by
`walk_player_slots`:

    FUN_0000d608(unaff_A6);              an SH-2 pointer
    FUN_0000d608(0xffffc000);            a BARE 68K ADDRESS
    FUN_0000d608(0xffffc400);            a BARE 68K ADDRESS
    FUN_0000d608((int)(&DAT_ffffc080));  an SH-2 pointer

and the body both DEREFERENCES it and TRUNCATES it to a short. On the SH-2,
dereferencing 0xFFFFC000 reads the ON-CHIP PERIPHERAL REGISTER FILE
(0xFFFF8000-0xFFFFFFFF on the SH7604), so a collision outcome is decided by whatever
a peripheral register holds. The stored result was 0xD400 -- `WRAM_BASE + 0x800`
low-worded -- where the arcade stored nothing.

The generator translates `&DAT_ffffc080`, because it is a SYMBOL. It cannot translate
`0xffffc000`, because Ghidra rendered it as an integer constant and **nothing in the C
distinguishes a 68K address from an integer once both are `int`.**

1,238 sites in 253 functions use a bare 0xFFFFxxxx constant where it could be an
address, and some are not -- `sound_enqueue(0xffffff92)` is the sound command -110.
The callee's declaration cannot arbitrate either: this one says `int`.

**This is what a type is for, and it is why guessing has failed every time it was
tried** -- `cport_prep` still carries the note that a blanket rewrite "substituted 372
sites for the 2 that actually warn and produced 339 errors".

## A DECOMPILER CANNOT SAY "THIS GLOBAL IS TEMPORARILY SOMETHING ELSE ACROSS A CALL"

    00D5B4  exg  A4,A6
    00D5B6  bsr  0x0000d608
    00D5B8  exg  A4,A6

Ghidra renders that as `FUN_0000d608(unaff_A6)`. It gets A4's VALUE right and drops
the fact that **A6 itself is different for the duration of the call**. In the port A6
is the global `_r_unaff_A6`, so every access inside the callee used the wrong record,
and a collision result was written to the object instead of the player.

Hand-writing `walk_player_slots` with the exchange modelled took the seeded frame from
25 wrong to 18 and removed exactly the seven cells the diagnosis predicted, breaking
none. `exg %a4,%fp` occurs NINE times in the whole program; the class is ten sites.

Third member of the hand-write class after `zoom_scale_lookup` and `FUN_0000E16A`. The
test is always the same: **not "is this wrong" but "is there a shape the decompiler has
no way to say".**

## A REWRITE PASS THAT MISSES ITS TARGET MUST FAIL THE BUILD

Writing one generator pass took three attempts -- wrong position in the pipeline, then
a brace the pattern did not expect, then an insertion point that landed PAST THE
CLOSING BRACE and planted a statement at file scope. **Every one of those failed
SILENTLY**: the build succeeded, the rom measured the same, and the only evidence was
a number that did not move.

That is the most common failure mode in this toolchain -- a regex over GENERATED C
stops matching when some other pass reshapes the code -- and it had no detector.
`CPORT_EXPECT` now names the functions a pass is aimed at and `cport_prep` refuses to
emit a unit if any came out untouched.

**Checking that the pass hit the one function you are targeting costs nothing and is
the difference between a measurement and a guess.** Three build cycles were spent not
doing it.

## GHIDRA EMITS AN INTRINSIC IT DOES NOT DEFINE, AND THE PORT STUBS IT TO ZERO

The 68000 has packed-BCD arithmetic and this game keeps its scores and counters in it:

    00377C  abcd -(A1),-(A0)    x4, low byte first, carry chaining through X

Ghidra renders each one as `*p = *p + *q; bcdAdjust(*p);`. **`bcdAdjust` is an
intrinsic Ghidra names and does not define**, so `defs.c` emits
`uint bcdAdjust(){return 0;}` -- and the call's result is discarded anyway. The port
therefore did a plain BINARY add and lost BOTH the decimal adjust AND the X-flag carry
chain that links the four bytes into one number.

151 `abcd`/`sbcd`/`nbcd` instructions in the program; 33 `bcdAdjust` call sites in the
unit. Hand-writing just the score adder took the seeded frame from 18 wrong to 15.

**An intrinsic the decompiler emits but does not define is a silent hole, not a
compile error** -- the stub links, the rom runs, and the arithmetic is simply wrong.
Audit the `intr` list for anything that computes: `bcdAdjust`, `CARRY1`, `CARRY4`,
`bcdAdjust` is the one that bit. A stub is only safe for something that OBSERVES
(a flag read); never for something that COMPUTES.

## A PORT CAN BE WRONG TWICE AND MEASURE RIGHT

`FUN_0000374A` promotes a score to the high score when it beats it. The decompiled
version compared against `p_A0` -- a local Ghidra never assigns -- so the branch was
garbage and happened never to promote. Hand-writing the comparison correctly then
exposed a SECOND fault: the BCD adder's overflow saturation (0x99999999) fired, and
the now-working promote copied it into the high score. 15 wrong became 20.

**Fixing one of two cancelling faults makes the measurement worse.** That is not a
reason to keep the first fault; it is a reason to expect the number to move the wrong
way and to have the cell list ready to say why.

Reverted, because a worse number is a worse number — but the finding stands, and the
next attempt has to start from the 68000's real `abcd` carry behaviour for operands
that are not valid packed BCD. Ghidra marks the saturation block "unreachable", which
says its analysis believes the carry never happens.

## GHIDRA FOLDS A6 AT THE USES AND DROPS IT AT THE CALL

    003724  lea (-0x2000).w,A6    ; A6 = 0xFFFFE000
    00372E  bsr 0x0000374a
    003730  clr.l (0x4,A6)

Ghidra emits `_DAT_ffffe004 = 0` for the `clr.l` -- so it resolved A6 correctly -- and
`FUN_0000374a()` for the `bsr`, with no argument and no A6. **It knows the value and
does not pass it.** A6 is a global in the port (`_r_unaff_A6`), so the callee runs
against whatever the previous dispatch left behind.

Here that was object record 40, and the score adder BCD-added into an object's flags
word. The symptom was 0x99999999 in 0xFFD400..03, which looked for two rounds like the
BCD saturation being wrong -- it was not; the ARGUMENT was wrong.

**210 instructions in the program load A6** (94 `lea`, 65 `movea.l`, 51 `movea.w`).
Hand-writing ONE of them took the seeded frame from 15 wrong to 8 and emptied the
object-record region.

**When a decompiled callee reads an inherited register, check what the CALLER sets it
to before suspecting the callee's arithmetic.**

## A REGISTER THE 68000 RELOADS IS A GLOBAL THE PORT MUST RESTORE

A6 is a global in the port (`_r_unaff_A6`). On the 68000 every consumer loads A6 for
itself, so nothing depends on the value surviving a call -- the port's version
persists, and the next dispatched handler inherits it.

Restoring A6 that a caller sets, as a bare assignment before the call, left the seeded
frame at **8 wrong, unchanged and apparently harmless**, while driving the 8-frame
free-run's game counter to **-1217 of 8**. The cell count read 150 wrong, which looks
like ordinary drift. **Only the progress check saw it.**

Save and restore around the call, and the same twenty insertions are safe: 8 wrong
seeded, 31 wrong free-run, net -23, counter 8 of 8.

**A one-frame gate cannot see a leak.** The state a frame LEAVES is as much its output
as the cells it writes, and only a multi-frame run scores it.


## 2026-10-04 -- the port-alone output path (SH-2 port, NOGAMEHIGH)


### A generator pass that matches zero is a bug in the pass, never a quiet day
- **A generator pass that matches zero is a bug in the pass, never a quiet day.** Three
  passes in one session silently matched nothing because the text at that stage did not
  look the way it was assumed to: the `(uint)` casts are added LATER by cport_build.sh's
  gcc feedback loop; `CPORT_STUBS` redirects `wait_frame_flag()` with a `#define` and
  does not rewrite the source; and cport_prep's `out` is a list of CHUNKS with the whole
  corpus in one element, so per-line state never fires. cport_prep now fails the build
  on a zero match (`_pass_report`). Dump the input before writing the regex.

### Never hand-compute an SDRAM offset
- **Never hand-compute an SDRAM offset.** `0x300C0` was typed for `WRAM_BASE + 0x30C0 =
  0x2FCC0`; it read 0xFF outside work RAM and produced a false "the i8751 mailbox is 0xFF
  from boot". `tools/port_peek.py` resolves symbols and 68K addresses through the .lst.

### ares --dump "VDP CRAM" is PACKED 9-bit 0bBBBGGGRRR
- **ares --dump "VDP CRAM" is PACKED 9-bit 0bBBBGGGRRR**, not the VDP's 0000BBB0GGG0RRR0:
  MD 0x0A60 reads back as 0x158. Two conclusions were drawn from the raw bytes before
  this was noticed ("CRAM 16-63 is all 0x030"); both were wrong.

### A counter placed inside #ifdef'd code you did not check the flags for compiles
  to nothing
- **A counter placed inside `#ifdef`'d code you did not check the flags for compiles
  to nothing** and reads zero forever. `0xFFB0BA/BC` were put inside
  `CRAM_PROBE || RIG_BARCODE` and `PAL_FIRST` blocks the build does not define. Check
  the flag list before reading a zero as "the path is dead".

### Cross-run frame numbers do not align on port builds
- **Cross-run frame numbers do not align on port builds.** ticks-per-frame depends on the
  MD handshake and varies run to run, so `port_peek` at frame N in two runs can land in
  different scenes. Take every related measurement from ONE ares invocation.

### An entry count on a function with many call sites names no site
- **An entry count on a function with many call sites names no site.** `FUN_0000153E`
  (21 sites) at 3 said nothing about case 0x10; CPORT_COUNT now prints the call-site
  count beside every entry count.

### A provably correct fix can make the observable strictly worse
- **A provably correct fix can make the observable strictly worse** -- twice today: the
  32-bit ring-header width (then dereferenced into the SH7604 peripheral file) and the
  real DIP switches (then the i8751 path with no MCU). Keep the correct value; the
  regression names the next defect.

### The i8751 lives on the SH-2 in a port-alone build
- **The i8751 lives on the SH-2 in a port-alone build.** md_main's shim writes MD RAM;
  the port's work RAM is SDRAM. Without `MCU_COINS=0 / MCU_SND consumed to 0xFF` each
  tick the game spins in irq4_handler on a TEST bit nobody clears (183M iterations).

### 0xFFC014 and friends: a move.w into the HIGH half of a long
- **0xFFC014 and friends: a `move.w` into the HIGH half of a long** is CPORT_HI's class,
  and the pass must rewrite reads INSIDE the value too (`C = C + 0xc00` -> SETHI16 of a
  high-half read), or the eye's X velocity reads -1 instead of -0xC00.

### The MD listing belongs to the LAST build, not to the rom you are reading
- **The MD listing belongs to the LAST build, not to the rom you are reading.**
  `rom/md_start.lst` is rewritten by every `make`; rd1's was overwritten by the line
  rebuilt 33 s later, and every MD cell read for rd1 (slim_ready, slim_diag, slim_rec)
  was 4 bytes off -- two hours chased a "stomp" that was slim_rec itself, and a
  counter labelled STRAY=2910 was the inline-DMA count. `tools/save_build.sh NAME`
  now keeps `rom/NAME.{32x,lst,md.lst,md.elf,flags}` together; `tools/md_pc.py`
  disassembles a 68K PC from the ROM BYTES (the .elf only supplies section
  placement). Read an MD cell only through the listing saved WITH that rom.

### A -D flag reaches ONE compiler unless you put it in both
- **A `-D` flag reaches ONE compiler unless you put it in both.** `-DPORT_FULL` sat in
  SHCCFLAGS only, so md_main.c compiled the line's `TILESMD_CART 0x26A000` while the
  PORTFULL link put the blob at 0x26C000: the 68K fetched every baked tile 8 KB below
  the art (`addal #0x96AA00` in the compiled bytes). tools/check_tilesmd_addr.py reads
  the source and cannot see which branch compiled; `tools/check_tilesmd_bytes.py` now
  reads the immediate out of the rom and runs on every link.

### A write-only static is dead-store-eliminated: no symbol, no ring
- **A write-only static is dead-store-eliminated: no symbol, no ring.** `slim_ring`
  (the first 16 stray records) had no entry in the listing because nothing read it;
  `volatile` keeps a diagnostic ring alive.

### Under NOGAMEHIGH the vint ran TWICE a frame, nested
- **Under NOGAMEHIGH the vint ran TWICE a frame, nested.** The line's vint is the H-int
  at line 223 (reg 0 IE1 + reg 10 = 0xDF, level 4) with IE0 off; the port build turned
  IE0 on (the display gate's 0x8114 stops the H-int with the display) and left IE1 on.
  Measured on rd1: 840 vints in 548 frames, an exception frame SR=0x2404 PC=0xFF1C6E
  on the 68K stack (the handler interrupted at IPL 4 by the level-6 entry), the
  handler dead from frame 548, every VRAM tile slot zero after it, slim_rec full of
  one garbage pair from two consumes walking one packet. One source (IE1 off, IE0 on)
  = 1.00 vints/frame and 1252 VRAM slots of art at frame 2001 against the line's 1254.

### The arcade's own writer census is the spec for address translation
- **The arcade's own writer census is the spec for address translation.**
  `tools/video_writers.lua` (MAME watchpoints on the four S16 video ranges) +
  `tools/video_writers_hist.py` named every function that writes tile, text, sprite
  and palette RAM in a minute of attract; the port's rd3 had translated the static
  symbols and lost every store that reached its destination through an ADDRESS
  REGISTER (FUN_00002622, FUN_00003972, textram_stride_write, the unpackers, the
  palette queue). A68PTR at entry (CPORT_PARAMNORM) is exact because it is the
  identity for an SH-2 pointer. Never guess which functions write video RAM: watch.

### A 64 KB arcade walk over a 13-page shadow lands on the regions packed behind it
- **clear_tileram clears all 16 tile-RAM pages (0x4000 longs from 0x400000); the port's
  shadow is 13 pages** with TEXT_U, PAL_SH and SPR_SNAP right behind it, so the last
  0x3000 bytes of the clear hit them. Capped at 0x3400 longs by a prep pass (the
  renderer never shows pages 13-15). Every bulk walker over a translated region must
  be checked against the SHADOW's size, not the arcade's.

### The master landed the MD's empty palette pushes over the port's shadow
- **PAL_SH held 985 colour words at frame 495 and 1 at frame 499, zeroed in push order
  (sets 0-27, 28-57, 58-63), with no 68K-port palette writer running** (CPORT_COUNT
  deltas flat; CPORT_ARGLOG showed FUN_00003972 writing 0x06027000 from 0x023632a0).
  The wiper was m_main.c's R60 landing code copying K pushed sets into PAL_SH -- the
  line's palette stream, fed from the MD's 0xFF7000 mirror, which no game fills under
  NOGAMEHIGH. LANDED_PAL_K() makes the copy a no-op under PORT_VIDEO: the port owns
  PAL_SH. Found by bisecting ares dumps to the frame, then diffing the words.

### A byte copy Ghidra rendered as a shifted word
- **`moveb 0xfffff148,0xfffff149` (0x2C00) became `DAT_fffff149 = DAT_fffff148 >> 8`**,
  always 0 for a byte: the fade engine's level never reaches its target once it is
  non-zero, and the black-out branch repeats every vint. Restored by a prep pass. The
  class is CPORT_WIDTH's -- a byte move the decompiler widened -- and it was found by
  reading the engine, not by a symptom; tools/dat_width_observed.py should flag a
  `>> 8` of a byte cell.

### Ghidra folded A0 and D0 into one return and the port stored a pointer's low byte as a slot index
- **order_list_insert (0x3CA6) stores D0 = the object's slot index (fp+8) into the draw-order
  list; FUN_00003cb6 returns A0 = list + index AND leaves D0 = index.** The decompiled C
  returns one value and stores `(byte)iVar1` -- the low byte of an SH-2 pointer, bit 7 set
  -- so the vint's sprite copy (`moveb (a0)+,d0; bmi skip`) skipped every entry and
  SPR_SNAP held the terminator at record 0 (rd9/rd10 2026-10-04). Fixed by a prep pass
  that stores `*(byte *)(unaff_A6 + 8)`. When a leaf returns in A0 and the caller uses
  D0, read the 68K: the C's single return value is one of them.

### The hand-computed SDRAM offset, again: 0xFFEC80 is sdram 0x2F880, not 0x2EC80
- **Three hours of "phantom object in the draw-order lists" came from dumping 0xFFEC80 at
  sdram 0x2EC80** -- WRAM_BASE sits at SDRAM 0x2CC00, so 0xFFEC80 - 0xFFC000 + 0x2CC00 =
  0x2F880; 0x2EC80 is object slot 0xFFE080. The in-rom probe read the real lists (255 x
  0xFF, as the arcade) while every ares dump of the wrong offset read zeros. The rule
  already existed (tools/port_peek.py, "never hand-compute an SDRAM offset"); the gap was
  that ad-hoc `--dump sdram:` arguments bypass it. `tools/a68.py ADDR [len]` prints the
  dump argument; use it for every work-RAM or video address, every time.

### One address, two access widths: a per-cell width override cannot be right for both
- **0xFFF400 is a byte to the game (moveb/addqb at 0x3B44/0x3B4E) and part of an 8-byte
  long copy at init (0x3B10).** `CPORT_WIDTH FFFFF400=4`, added to make the init copy
  land, turned the counter's `< 0x3f` compare into a compare of the counter | the next
  byte | the queue pointer's high word, so request_palette_update never allocated a
  sprite-palette slot and PAL_SH sets 64-79 were absent in rd9-rd12 (2026-10-04).
  Keep the cell at the width the READERS use and rewrite the odd-width site (here the
  init, as two ST32/LD32U longs). tools/cport_widths.py should flag a cell whose
  accesses disagree in width, not pick one.

### CORRECTED: A1 from sprite_frame_lookup is the sprite ROM address the HARDWARE reads -- a value, not a pointer
- **The first reading below was wrong and measured wrong**: translating A1 to an SH-2
  ROM pointer put the SH-2 base into the sprite record's bank byte (`moveb d1,(a0)+`
  at 0x3EE0 writes `(a1>>1)>>16 | 0xF0`): rd20-22 records carried 0xFA where the
  arcade's carry 0xF0, and the hero never drew. 0x3EA4 only ADJUSTS A1 (`addaw a0@,a1`);
  nothing dereferences it. The 6-vs-30 record gap was missing OBJECTS (the table
  clear, the flag width, the installer's bare constant), not this. A 68K address
  that is stored for the hardware stays in the 68K domain. Reverted.

### (superseded) A ROM pointer returned in A1 through CONCAT44 is still a 68K address
- **sprite_frame_lookup (0x3C84) returns A0 = table entry and A1 = the ROM pointer it
  loaded (`moveal (a0),a1`); the C hands A1 back raw in the low half of CONCAT44 and
  sprite_build_and_cull walks it as an SH-2 pointer** -- the SH-2 vector area read as
  sprite data. The arcade's only 0xFFF800 writers are sprite_build_and_cull and
  hide_object_sprite (tools/wp_census.lua, frames 945-948); the port's sprite table had 6
  records to the arcade's 30 at the matched state (2026-10-04). A68ANY on the returned
  A1 in a prep pass. A value that leaves a function in an ADDRESS register is a 68K
  address until proven otherwise; `extraout_A1` readers are the places to check (109
  reads, 0 publishers in the unit).

### A vint flag loses every vint that lands while the master is mid-loop
- **pp_due was a flag: the master's frame loop ran past one frame often enough that the
  port ticked 0.83 game frames per display frame** (rd17: 420 ticks by frame 500, 1650
  by 2000), so the demo ran 18% long, the camera lagged 29 px and tools/attract_parity.py's
  constant-offset alignment fell apart (its OFFSET grew from 54 to 59 across the run).
  A bounded counter (`if (pp_due < 4) pp_due++` in the vint, `while (pp_due--)` in the
  loop) gives 0.994 ticks/frame (rd18) and the port now leads the arcade by 8 frames
  from boot. The game's own frame is cheap (dt ~0.04-0.18 of a frame); the renderer's
  is not. Count the owed vints; never flag them.

### A 16-bit flag store rendered into a 4-byte cell activates nothing
- **FUN_00016b6a activates object slot 40 with `movew #0x8000,(fp)` (fp = 0xFFD400); the
  port's `_DAT_ffffd400 = 0x8000` went into a uint, writing 00 00 80 00** -- flag byte
  zero, slot never dispatched (arcade f947: slot 40 active, handler 0x16BF0; port: flag
  0 with the same handler). CPORT_WIDTH FFFFD400=2. Same class as 0xFFF400: read the
  68K's access width before trusting a `_DAT_` cell's declared type.

### The palette queue writes through the 4 KB mirror, and the shadow had no mirror
- **The arcade's palette_queue_drain targets 0x841002 + set*32 (the MAME wp census read
  it as "sets 128-159"); the S16B decodes palette RAM modulo 0x1000, so those are sets
  0-31 -- the text and HUD colours.** The port's shadow is exactly 4 KB and A68VIDA
  mapped 0x840000-0x840FFF only, so every queued HUD palette vanished and PAL_SH sets 0-2
  carried the round table's words instead (rd18 f2200 vs the arcade's 0x840000 dump).
  The drain now wraps its destination into the shadow and A68VIDA masks the whole
  0x840000-0x84FFFF range. A region's SIZE in the shadow is not its size on the bus.

### `lea 0xffffc000,a0` as the int -0x4000, and `-0x78(a0)` as an absolute cell plus a register
- **FUN_00001338 clears the object table at every attract loop: A0 = 0xFFC000, 64 x
  (32 longs of zero, then three byte stores at -0x78/-0x51/-0x77 off A0).** The C passed
  `p_A0 = -0x4000` to the clearing helper (an SH-2 peripheral address) and the
  generator's W68PTR pass turned the displacements into `W68PTR(0xFFFF88u) + (p_A0)`.
  MEASURED 2026-10-04 (rd18 f2200): the arcade's loop point has ONE active object, the
  port 16 -- the whole demo still alive under the high-score screen, including its
  scroll object, so the page word read 0x7878 against 0x6767. Four such sites exist in
  the unit (`W68PTR(0xFFFF..u) + (reg)`): an absolute work-RAM address added to a
  register is never a 68K thing; it is a negative displacement.

### Two renderer inputs the 68K's push used to carry, and the port carried as zero
- **The 32X sprite list rebuilds on `spr_chain_id`, which only the slave's landing code
  advanced** (s_main.c 302/352). With the port as producer no chain lands, so spr_list
  stayed as built at boot -- empty -- while SPR_SNAP held valid records (rd24 f985:
  `a15d 00a9 0110 c109 f082`, bank byte 0xF0 like the arcade's) and nothing drew. The
  tick now advances the chain under PORT_VIDEO.
- **`r60_disp_on` -- the 32X layer's enable -- was read off the landed push (word 20 bit
  15)**; the port's push carries 0, so from the first landing the layer sat in
  MARS_VDP_MODE_OFF: the FB rows held the text and the hero (pens 7/2/4, white/red
  CRAM at y=60, f2200) and the screen showed the MD planes only. The game's own bit is
  0xFFF018 bit 5 (irq4_handler writes it to port 0xC40001); PORT_VIDEO reads that.
  When a build "draws the background but never a sprite or a letter", list every
  renderer input that the 68K's packet used to carry and ask what the port carries.

### A program-ROM constant cast to a data pointer is a ROM pointer; a `code *` one is a handler VALUE
- **FUN_00004178 copied the default high-score table from `(undefined4 *)0x4732`** -- the
  SH-2 vector area -- so the port's table was zero and its score row spaces (rd27 f985 vs
  arcade f993: `41 28 00 05 20 20 20` at 0xFFFC01). Ghidra's own cast, present at the
  prep stage (gcc's later casts are parenthesised: `(T *)(0x...)`; Ghidra's are not).
  Ten data-pointer sites rewritten to `ROM_BASE + k`. The 248 `(code *)0xNNNN` sites are
  NOT pointers: they are handler addresses stored into 68K records for the dispatcher
  and must stay 68K values -- the first draft of the pass rewrote them and would have
  broken every object. A cast's TYPE says what the constant is for.

### The attract gate is deterministic: a row that moves between builds moved because of the build
- **tools/attract_parity.py --port run twice on rd24 gave identical tables to the digit**
  (2026-10-04). ares-headless is frame-exact and the port has no entropy source, so
  there is no run-to-run noise to blame; rd28's eye row at 72 against rd27's 34-44 was
  the ROM-cast pass's other nine sites, not chance. Bisect builds, never re-run.

### A callee that returns in three data registers: every caller must reload all three
- **zoom_scale_lookup (0x3CD4) returns the sprite's pitch, height and width in D3/D6/D7;
  the port's stub publishes them to _port_reg[3/6/7] and only ONE of 105 call sites
  reloaded them** (sprite_build_depth_banded, by the register-plumbing pass). The other
  104 -- sprite_build_and_cull and every object handler's draw -- built records from the
  stale parameters: rd27 f1192 vs arcade f1200, same sprite data c866/f480, word 0
  0x8070 / pitch 0 against 0x935d / 0x0E. The eye's iris was a 16-line band, the title
  logo and the heroes collapsed. A generic prep pass now reloads after every call. When
  a stub publishes registers, grep every caller for the reload; the plumbing pass
  covered the one site it was written against.

### The i8751 seeds the credit count; a watchpoint on the 68K's bus cannot see it
- **On the oracle (MAME altbeast, DIPs 0x54/0x66) 0xFFF000 reads 0xFF through frame 6
  and 7 from frame 7 on, and a 68K watchpoint armed before the first frame logged NO
  write to it**: the MCU wrote it over its own bus. The arcade shows "CREDITS 7" and
  spawns the credit object; the line's shim and the port's never seeded it, so both show
  "INSERT COIN" and the eye scene lacked slot 6. The port's shim now seeds 7 once (rd34).
  tools/wp_census.lua WP_FROM=0 arms before frame 1 (the boot's own writes); a cell that
  changes with no 68K write caught is the MCU's, and the shim has to replicate it.

### The port's framebuffer has no background blit to overwrite old glyphs
- **compose_text writes glyph pixels without clearing; on the line the layer blit
  repaints the row first. Under PORT_VIDEO the MD planes carry the background and the
  FB holds only sprites and text, so a text row whose words changed kept its old pixels
  in BOTH banks** (rd34 f1192/f1193: the demo's logo rows 32-72 byte-identical under
  the eye scene). The row generator already regenerates a row whose FBCLEAR state is 0
  ("unknown, write it"); the text-row change detector now zeroes both banks' eight
  lines (rd36: the stale logo gone, eye row 41). When a layer is removed from the FB,
  every clearing it did for free has to be found and re-done.

### A handler Ghidra never made a function of: 28 bytes between two function ranges
- **The credit object's handler 0x24A4 lies between FUN_00002490 (ends 0x24A3) and
  FUN_000024c0 (starts 0x24C0); Ghidra emitted it as a label and the installer stored
  `&LAB_000024a4` -- an SH-2 address -- into the record**, so the object never
  dispatched (rd34 f1192: the arcade's slot 6 active with 0x24A4, the port without).
  The 68K is 5 instructions that fall into 0x24C0; a prep pass defines
  FUN_000024a4 from those bytes and rewrites the store to the 68K value. THE
  DISPATCHER DID NOT THUNK IT UNTIL rd42: cport_dispatch.py takes its callable set
  from docs/audit/func_ranges.txt (name -> ENTRY), not from indirect_roots.txt, so
  rd37-rd41 still logged 0x24A4 in the unknown-handler census (port_watch_out[190])
  while the LESSONS entry said "present" on the strength of the record holding the
  value. A hand-added func_ranges line carries it; the census is the proof, not the
  record.
  CPORT_HANDLERS already reports "real handler addresses that are not function
  STARTS" -- read that list; each entry is an object the port cannot run.

### The demo plays itself from a ROM script, and the script pointer was a raw 68K address
- **The attract's hero is driven by ROM bytes, not the I/O ports: FUN_00001366
  (prog68k 0x2C6A region) takes P1/P2 from `PTR_DAT_00001834[(state & 0x18) >> 1] +
  0xFFF02A * 3` whenever 0xFFF026 bit 0 is set, and Ghidra left that loaded address
  raw -- `(byte *)(counter * 3 + LD32S(...))` with no P68.** The SH-2 read 0x0000xxxx,
  the record at 0xFFE000 carried +1e/+20 = 0xFF/0xFF where the arcade carries 00/00
  (rd40 f500 vs arcade f508), FUN_00004d9a took the "jump" branch every frame (slot 0
  +20 = 9, +1a = 0xFB80, floor_collide skipped on the negative velocity), and the hero
  bounced in place at x 0x1149 while the arcade's walked to 0x1197 by f508. Nothing
  downstream of that could happen: no enemy spawn records reached, no spirit balls,
  no morph (+6b = 3 by 0xF4D0 -> table 0x6D90[2] = 0x9072), no 0xFFF148 = 0x0100, no
  FUN_00009052 text clear at the eye transition. A generator pass wraps the load in
  P68 (cport_prep.py "demo script pointer through P68"). The I/O cells themselves were
  right all along (rd40 f500: C41001..7 = FF, C42001/3 = 54/66, as the arcade reads
  at f465) -- the first hypothesis, "the pad is stuck", was wrong and ten minutes of
  dumping the cells proved it before any code moved.

### A helper that returns one register loses the other: the credit writer's A0
- **textram_stride_write returns A1; Ghidra assigned that return to `extraout_A0`
  and left `p_A0` uninitialised in FUN_00003aae**, so "CREDITS 7" came out
  `CREDIT 0xFF 0xFF 7` (rd37 TEXT_U row 26: 0043 0052 0045 0044 0049 0054 00ff 00ff
  0037). prog68k 0x3AD8-0x3AFA: A0 after the 6-char copy is the string + 6; `addq`
  skips the 'S', `subq` restores it for 2+ credits. The pass sets p_A0 from the
  source string and advances it by one after the 1-char copy (rd40: 0053 0020 0037).
  Any `extraout_An = <other register's return>` in the generated C is this bug.

### Reading the arcade's writers: tools/wp_census.lua emits `V r<n>` lines, not `W`
- The header comment says `W <range#> <pc> <addr> <data>`; the tracelog format string
  is `V r%d %06X %06X %04X`. `grep ^W` found nothing twice and the first reading was
  "the census didn't run". The MAME trace goes to WP_DIR/wp.log (hundreds of MB for
  600 frames; filter with `grep ^V`), the `F <frame> <state>` markers sit in the same
  file. Also: a failed glob (`rm -f $DIR/*` on an empty dir) makes zsh abort the WHOLE
  command line, including the MAME run that follows it -- name the file.

### MAME's 68K disassembly prints PC-relative operands RESOLVED
- `lea ($6d90,PC), A0` at 0x4A84 means A0 = 0x6D90, not 0x4A86 + 0x6D90. Computing the
  sum gave a "table" at 0xB816 full of code words. Confirm a table by finding the
  dispatched target in it (0x6D90[2] = 0x9072, the US set); roms/altbeastj/prog68k.bin
  is the JAPANESE set and its table differs (0x6D90[0] = 0x8AC4) -- the port is built
  from roms/altbeast/prog68k.bin ($(GAMEROMS) in the Makefile).

### A python pass pasted through a non-raw heredoc string loses its `\n`
- Building a generator pass as a python `'''...'''` literal that is then written into
  cport_prep.py expands `\n` inside the regex into a real newline and breaks the file
  (SyntaxError at the pass; rd40's first build ran a broken generator, exit != 0, and
  save_build.sh still "saved" the previous rom under the new name). Write the block as
  r'''...''' and `ast.parse` the file before queueing the build; check `exit=` in the
  build log before trusting a saved rdN.

### The scripted-spawn dispatch: Ghidra read the `bcc` from the wrong instruction
- **FUN_0000e052 (prog68k 0xE0AC-0xE0D2) calls the slot allocator FUN_0000e0f4 and
  branches on ITS carry (`move #1,CCR` at 0xE164 = found a slot); with every slot full
  the allocator never returns -- it jumps into the caller's own exit at 0xE0E0.**
  Ghidra attributed the `bcc` to the last flag write it could see in the caller (the
  kind-0 branch's `tas`, the other branch's `addqb`), so for every kind-0 record the
  port skipped the dispatch, and on the other kinds it indexed the installer table
  with a stale `extraout_A0` (rd41 unknown-handler census 0x00FE0001, 0x00FD0001, 0).
  The pass makes the allocator's failure path return 0 and gates the dispatch on
  that. Any `bcc`/`bcs` straight after a `bsr` in the 68K is this pattern: the
  decompiler cannot know the callee's CCR contract, and silently guesses.

### Ten installers the kind table reaches that Ghidra never made functions of
- FUN_0000e052 dispatches a spawn record's kind byte through the 21-entry long table
  at 0x1D2EA (`lea 0x1d2ea,a1; lsl.w #2,d0; movea.l (a1,d0.w),a1; jsr (a1)`). Ghidra
  followed the code but not the table, so 0xEE0A 0xEDB0 0xF74A 0x102CC 0x10CD6 0x1D348
  0x1D4D6 0x1D520 0x1D56A 0x1D56C are in no function -- kind 0 (0xEE0A) is the attract's
  demo director (installs 0xEE80 into slot 16 at arcade f513; it spawns the wolves
  whose spirit balls morph the hero). Round 1's demo uses kinds 0-5; the other rounds
  reach 6-15. tools/ghidra/make_kind_inits.py creates them (plus 0x24A4) the way
  make_spawn_inits.py did for the 0x1D32A tables, then f1_decompile.py and
  func_ranges.py re-export. A hand-written C body (the 0x24A4 route) does not scale to
  ten; a jump table in ROM is always a Ghidra "create function" list, never a
  hand-write list. The symptom to recognise: an allocated record (flags, position
  filled by the allocator) with a ZERO handler.

### A long read at a short-pointer offset is misaligned on the SH-2, and it reads EARLY
- **colour_cycle_streamer (prog68k 0x30F2): a word then four longs per 18-byte table
  entry, so the longs are 2 mod 4 for half the entries. Ghidra: `*(undefined4 *)(puVar4
  + 1)`; the SH-2 masks the low address bits of a longword load and returns the long
  one word early.** Palette set 20 read 0004 0a00 100f ... where the arcade's is 0a00
  100f 100f ... (the 0004 is the entry's leading word) and the spill landed in set 21
  (rd41 f1192 vs arcade f1200, docs: the "HUD palette one-word shift" of rd31-rd41).
  The pass routes those four reads through LD32U. The grep that finds the class:
  `\*\(undefined4 \*\)\(p\w+Var\d+ \+ [13579]\)` -- a long through a SHORT pointer at an
  odd element offset. The 68K does not care; the SH-2 silently does.

### Ghidra headless does not exit after a postScript here: wrap it
- `tools/ghidra_run.sh script X.py` ran make_kind_inits.py to "Save succeeded" and
  then sat for the full 10-minute tool timeout; f1_decompile.py likewise finished in
  10 s ("decompiled 864 ok") and never returned, so a chained func_ranges.py never
  started. Run it in the background, poll the log for "Save succeeded" and the output
  file, then `pkill -f analyzeHeadless`. The re-export itself is cheap (864 functions,
  10 s) and surgical: 11 functions added, 1 body changed (FUN_0000250a, which now
  names FUN_000024a4 instead of LAB_000024a4), 0 removed.

### .bss sat EXACTLY at the 0x06019000 limit, so the next 60 bytes failed the link
- rd41 and rd42 both reported `_end = 0x06019000` -- not a designed fit, a coincidence
  of ALIGN(8) -- so the re-export's 19 new pseudo-register globals (defs.c) tripped
  mars.ld's ".bss grew into the renderer's region block" ASSERT with no number in the
  message. The port's own .bss holds instrument rings that a play build never fills:
  port_mark_ring (768 B), port_trace (512 B), port_cur_regs (2.6 KB), pp_pgsum (3.7 KB
  in m_main.c); the marker ring went 192 -> 64 entries (one bound site, port_l2.c).
  Read `grep ' _end$' rom/rdN.lst` against 0x06019000 BEFORE adding anything to the
  unit; a value equal to the limit means zero slack, not a perfect fit.

### A stale port.o linked as if it were the build: rd44-rd46 never carried their passes
- **tools/cport_build.sh's compile loop ran five passes that each ended at 1 error, gcc
  wrote no object, and `ls -l port.o && echo OBJECT PRODUCED` blessed the previous
  build's object** (06:34, rd43's). Three roms were then measured for passes they did
  not contain: the found-slot A6 dispatch, two ARGLOG builds whose rings stayed at
  n = 0, a COUNT build whose counters stayed at 0 -- and the "why does the ring never
  fill" chase cost an hour before `python3 -c "open('rom/rd46.32x','rb').read().count(
  b'\x06\x00\x09\x3c')"` (the counter's address as a literal) returned 0 and the rom's
  own disassembly of FUN_0000e052 showed no prologue. The error was a type conflict:
  cport_prep harvests every undeclared identifier into defs.c as `uint`, so a pass that
  adds `extern byte *port_spawn_a6;` conflicts with its own harvest. The script now
  removes port.o before the loop and exits 1 with the gcc errors when none is produced.
  RULE: after a build that adds a probe, grep the ROM for the probe's address before
  reading the probe; a measurement of zero from an instrument that was never linked is
  not a measurement.

### The installer runs on the slot the allocator FOUND, not the A6 the wrapper restored
- **prog68k 0xE0F4 walks A6 to the free record and 0xE0C6's `jsr (a1)` runs the kind
  installer with THAT A6; the generated FUN_0000e052 saves/restores _r_unaff_A6 around
  the allocator (the a6_callsites wrapper), so the installer ran on the LAST DISPATCHED
  OBJECT's record** -- rd43 f490: slot 16 allocated with a zero handler, slot 51 (the
  0xDA4E zombie from the 0x1D32A table) overwritten with the director's fields
  (+24 = 0xF576, +3e = 0xFF, flags 0x88 -> 0x08). FUN_0000e0f4 now publishes the slot
  it found (port_spawn_a6, harvested as a uint) and FUN_0000e052 dispatches with it and
  tests ITS flags word, not Ghidra's propagated 0xFFC800. Plus FUN_0000edf0's ROM
  template `(undefined4 *)0xf56a` through ROM_BASE and LD32U (0xF56A is 2 mod 4).
  rd47 (the first rom that linked it): slot 16 = 0xEE80 at f650, the director's kind
  chain runs (counts ee0a 1, edb0 1, f74a 2, 102cc 5 by f1192), 0xFFF148 = 0x0101 at
  f1060 -- the morph and the text clear now fire. The wrapper rule: a callee that
  MOVES A6 for its caller (allocators, slot walkers) must publish it; the save/restore
  is only right for callees that borrow A6.

### Six installers branch on the pair allocator's carry; Ghidra made the install branch dead
- **prog68k 0xE032 returns carry SET with A5 = the free record (0xE04C `move #1,CCR`),
  clear when the 24 records from 0xFFC800 are full; FUN_0000f74a, FUN_000102cc,
  FUN_00010cd6, FUN_000143dc and FUN_00014a5a (x2) do `jsr e032; bcs install`.** The
  decompile: `bVarN = false; FUN_0000e032(); if (bVarN)` -- the install was dead code
  and the `clr.w (A6)` else-path killed the record, so the demo's zombie pairs never
  existed (rd47 f540-f630: no 0x103E0/0x10824 objects; 0xFFF154 = 0 where the arcade
  has 3). The pass makes the callee return its carry and publish A5, rewrites the six
  sites to take the return, and re-reads `unaff_A5` after the call where the caller
  models it (rd50: 0xFFF154 = 3, pairs present). Same class as FUN_0000e0f4/e052: a
  `bcc`/`bcs` straight after a `bsr` is the callee's CCR contract, which Ghidra cannot
  see and silently guesses.

### A pointer register truncated to a 68K short, then widened back
- `sVar = (short)unaff_A5;` stores the low 16 bits of a RECORD ADDRESS (0xC880 for
  0xFFC880); the SH-2 pointer gives 0xD480. P3d only covered `*(short *)(..) = (short)X`.
  rd48: FUN_000102cc cross-linked the pair through `(short)unaff_A5`, the +0x78/+0x3c
  links pointed at the wrong records, slot 61 filled with 0x07070707 and the game
  died by f560 (gate eye 98, face 88). Two passes now: the bare form goes through
  A68OF16 (9 sites, A5 and A6), and `(int)(short)unaff_A5` -- the 68K's `moveaw
  %sp@+,%fp` sign-extending the short back into an address -- is the pointer itself;
  a short that came out of A68OF16 and is later widened (`iVar = (int)sVar`) goes back
  through W68 (3 sites), or it hands the SH-2 a 68K offset as a pointer (rd52's
  f74a/14a5a/12cba).

### The hero's per-state dispatch passes D5/D6; the generated site published only D7/A5
- **prog68k 0x4A98-0x4AC2 loads D5 = record+0x1f, D6 = ~record+0x20 & record+0x1e (new
  presses), D7 = record+0x1e (held), all zero while 0xFFF14C runs, before the table
  jsr at 0x4C16 (and FUN_00004870's twin).** The decompile computed D6 into a local
  and published only D7 and A5 before port_call68_at, so the register thunk handed
  every state handler a STALE r[6]: the demo hero was in state 9 (jump) from its first
  frame with the input record reading 00 06 00 06 exactly like the arcade's, and the
  sound queue filled with 0xB6 (rd47-rd52). Counting the eight `+0x20 = 9` writers
  found none of them running -- the writers were the FORM-2 handlers (FUN_00008aae..)
  whose `unaff_A6[0x20] = 9` spelling the first grep had missed; grep both spellings.
  The pass publishes D5 and D6 at both sites (rd53: state 0 at f452, 2 at f500, the
  hero walks; gate face 24). The register file is the ABI across a table jsr: every
  D register the 68K loads before it must be published, not only the ones Ghidra
  happened to pass as parameters.

### A pair installer that moves A6 to the pair slot mid-way
- FUN_000102cc: `moveaw %sp@+,%fp` (0x10362) makes A6 the pair record before the
  second template fill; the decompile kept calling FUN_00010394 through the first
  record's A6, so rd50 had three 0x10824 records (the second template over the first)
  and no 0x103E0 partner. The pass sets _r_unaff_A6 to the pair slot before the
  second call (rd53: 17/19/21 = 0x103E0, 18/20/22 = 0x10824 like the arcade's). The
  kind-2 and kind-4 installers swap A6/A5 too but only around pointer-tracked stores,
  which Ghidra renders correctly; a CALL inside the swapped stretch is what needs
  the explicit A6.

### Object templates read as longs where the 68K reads words: the zombie's palette set
- **prog68k 0xDA74-0xDA7E copies the template at 0xDD38 as long, WORD, WORD (`movew
  (a0)+,6(fp)`; `movew (a0)+,d0; moveb d0,11(fp)`); Ghidra declared uRam0000dd3c and
  uRam0000dd3e as `uint`**, so the port stored the 32-bit value's low half into +6
  (0x0032 -- the palette set id -- where the arcade has 0x0117) and `(byte)` of the
  next long into +0xb (0x10 where the arcade has 0x32). Palette slot 0 then held set
  0x10 instead of 0x32 and the demo's logo stayed white (rd41-rd54; arcade 0x3B74 at
  f464). The same shape at 0xDCEE/0xDCF0, 0xDDD8/0xDDDA and 0x1984E; all seven are in
  CPORT_WIDTH now (rd55: slot 0 = 0x32, the slot tables identical to the arcade's at
  the same tick). The scan that finds the class in one line:
  `(undefined2 *)(..) = uRamX;` / `(byte)uRamX` / `(short)uRamX` against dat_syms.h's
  declared width -- every uRam read narrower than declared is this bug.

### A rebuilt line differs from the saved copy in 17 bytes: the build stamp, nothing else
- `make line` after a day of port work produced a rom that `cmp` called different from
  the session-start copy: 4 bytes at 0x262568 (the git-hash u32 the master stamps into
  DIAG[18]) and 13 in the trailing build-id string at 0x3FFFD4-0x3FFFF0. Nothing in the
  code or data moved -- every port edit in m_main.c/md_main.c/port_l2.c sits under
  PORT_VIDEO/NO_GAMEHIGH/PORT_FULL. "Pixel-identical" is the right claim; a byte-equal
  check must mask the stamp (tools/build_id.py shows it) or it cries wolf after any
  commit.

### The port lands a palette every vint, so the scene detector's 16-no-match drop fires in 16 frames
- **PALSTATIC drops `pscene_cur` to 0xFF after 16 consecutive no-match LANDINGS; the
  line's landings are K-vint packets, the port's are every vint**, so the eye scene --
  not an anchor, its iris cycles -- lost the scene within a quarter second of the fade
  (rd55 f1192: pscene_cur 0xFF, mds_scene_cur 0xFF, no MD tile tables installed, 627 of
  628 slot keys foreign to the line's at f1330, the eye's BG served from the demo's
  claims) while the game's tile RAM, text RAM and palette were byte-identical to the
  arcade's at the same tick. The line keeps scene 0 through the whole eye (f700/f1192/
  f1330). Under MD_ROUND the ROUND picks the table; the scene index is only the
  "palette is stable" gate, so PORT_VIDEO never drops it (PSCENE_NOMATCH_LIMIT).
  rd56: pscene_cur 0 at f1192/f1330, gate eye 35-40 (rd55: 76-91). When the game side
  measures exact and the picture is wrong, the renderer's SCENE/TABLE state is the
  first thing to read, not the cache.

### Ghidra's LAB_ data labels declared as `uint` scale every `&LAB + offset` by four
- **The eye-pan camera (FUN_000023dc) steps a script of 8-byte entries with
  `*(short *)(&LAB_00002686 + sVar5)`; extras.h declared every undeclared LAB_ as
  `extern uint`, so the pointer arithmetic stepped 32 bytes per entry** and the object
  read x velocities 0x0300/0x7333 where the arcade's are 0x01c8/0xfff8 (rd56 f1250 vs
  arcade f1268); the pan never reversed, the BG scrolled off its tiles and the gate's
  "eye pan" row sat at 43-69 while tile RAM, text RAM and palette were exact. DAT_ and
  PTR_ symbols were already byte arrays; LAB_ data labels now are too (rd57: the record
  byte-identical to the arcade's, eye pan 27-30). The HUD icon table (LAB_00006d5a_2)
  had been reading right only because its index was already a byte offset times one.
  Any `&SYMBOL + n` in the decompile assumes BYTE arithmetic: check the declaration.

### The pair's animation script is the parent's pointer plus a word stored before it
- **prog68k 0x108xx (FUN_00010824): `movea.l 0x24(a0),a1; move.w -2(a1),d0; add.l a1,d0;
  move.l d0,0x24(a6)`.** Ghidra: `psVar = (short *)(LD32S(parent + 0x24) + -2); A6+0x24
  = *psVar + (int)psVar` -- a 68K ROM address dereferenced on the SH-2, then re-stored
  as if it were a 68K value. rd57 f548: the partner carried the parent's script (0x10a58
  beside 0x10a5a; arcade 0x10c36) and its flags/x diverged from there. The word is read
  through P68 and the base re-stored as the 68K address (rd58: +0x24 = 0x10c7a, flags
  0x88, x 0x124e like the arcade's). Three spellings in the unit, one of them through
  the +0x3c link SHORT (W68 first). Any `(T *)(LD32x(..) + k)` whose load is a ROM
  pointer is this class; the grep is `\(short \*\)\(LD32S\(`.

### The 68K spends ~5 frames per tilemap unpack; the port spent none
- The attract's cut-black is the arcade's 68K unpacking two RLE tilemap passes
  (0x16C8/0x16EE, 0x5000 byte stores each, vints running in between): first writes at
  f451/f453 and f456/f459, the demo hero created at f464. The port ran both in zero
  frames and created the hero at f443, so every later scene led the arcade by ~20
  frames (gate "cut black" 88-131, "demo scene" 83-118 at lags 0-12, rd57). A frame
  yield every 0x1000 stores (wait_frame_flag, the stubbed game-loop yield) puts the
  port's demo tick 0 at f451 (rd58; arcade f464), cut black 20 at lag 20, demo 2 42-43.
  The SH-2 is not the clock the attract was timed against; where the arcade's own
  68K cost is visible on screen (a black hold, a load), the port has to pay it.

### `clr.w 0x840000` widened to a long zeroed text pen 1 -- the garbled boot card
- **prog68k 0x1F80 (and 11 more sites) clears palette set 0's word 0 with `clr.w`;
  Ghidra declared `_DAT_00840000` as `uint`, so every `_DAT_00840000 = 0` in the port
  also zeroed word 1 (pen 1 = 0x0FFF, the text colour).** rd59 f18: set 0 = 0000 0000
  0f00 ffff ... where the arcade's is 0000 0fff 0f00 ffff (copied from ROM 0x232A0 by
  FUN_00003972); the boot card's and the title's text drew black/garbled, and the
  PALSTATIC scene probe never matched scene 0 (pscene_cur 0xFF at f300 in every port
  build; the line reads 0), so no MD tile table was installed and the logo scene's
  tilemap page 2 never reached the MD. CPORT_WIDTH 00840000=2. The class is the
  "one address, two widths" census (tools/cport_widths.py): a palette word cleared
  through a long-typed symbol is the same bug as 0xFFF400/0xFFD400 before it.

### The renderer's game-state word comes from the 68K's RAM, which the port never writes
- **md_main.c posts a per-vint state word (tag E, seq, cutscene 0xFFF148, round
  0xFFF142, play ~0xFFF026, step 0xFFF031>>2) from the 68K's OWN cells; under the port
  (NO_GAMEHIGH) those cells are the boot shim's leftovers**, so the SH-2 decided every
  scene with a stale word: step 0 "picture", round 0, no cut. Measured 2026-10-04 with
  the game side byte-identical to the arcade's: the logo's upper half and the boot
  card's panels never drew, plane B carried 335 raw tilemap words where the line's
  cells are 0x03FF. Under PORT_VIDEO the word is synthesised from the port's WRAM
  shadow once per vint (same layout). rd62: the logo scene draws complete. The seq
  field must advance per POST: computing the word per reader (several times a vint)
  moved the eye scene's rows from 37-45 to 61-78 -- a consumer counts posts.
  Rule for every MD<->SH-2 channel the line fills from 68K RAM: under the port the
  SH-2's WRAM copy is the truth, the 68K's cells are not written at all.

### A `dbf` counter the CALLER loads is invisible to Ghidra -- the loop runs forever
- **prog68k 0x4612 (the typewriter string copy: one char, `bsr 0x4664` = two frames,
  `dbf d0`) takes its count in D0 from the caller (`moveq #22,d0` at 0x45C2, `moveq
  #2,d0` at 0x45D6). Ghidra gave FUN_00004612 no D0 parameter and wrote the dbf as
  `while (extraout_D0w != 0)` with extraout_D0w the POINTER FUN_00004664 hands back --
  never zero.** rd63 typed characters across text RAM for 52 s at the score table
  (arcade: 9.5 s, frames 2130-2700 of the no-coin attract); the green/grey "scene"
  after it was the same loop writing past text RAM, and the rig showed exactly what
  ares showed. The pass gives the routine `uint p_D0`, loops `while (_dbf-- != 0)`,
  and the two call sites pass the 68K's constants (rd64: table scene 2220-2790, the
  arcade's 570 frames). FUN_0001b9aa (`dbf d1`, five callers at 0x1B95C-0x1B992
  loading 5/7/7/3/3) is the same class but has no decompiled caller.
- **Rule:** `while (extraout_Dnw != 0)` in the generated C is a dbf whose counter
  the caller set. Disassemble the callers; the count is a `moveq`/`move.w` to that
  register within a few instructions of the `bsr`.

### FUN_00004540's A0/A1 thread through its row loop; Ghidra made them locals
- **The score table's rows take their rank strings from A1 where the header call
  left it (0x4790 + 23) and their scores from A0 = 0xFFFC00 advanced 8 bytes per row
  (0x45CE `lea 0xFFFC00,a0`; `addq.l #4,a0` + four `(a0)+`).** Ghidra's `p_A1`,
  `p_A0_01` are uninitialised locals, so rd64's rows read strings from a stale
  pointer and scores from whatever A0 held. The pass seeds both after the header
  call and advances them per row. Same shape as the credit writer's A0 (above).

### MAME's System 16B work RAM is NVRAM: the arcade oracle carries state across runs
- **jts16-ref segas16b.cpp 9317: `m_nvram->set_base(m_workram, ...)` -- the whole
  16 KB work RAM is battery-backed, and MAME saves it to `nvram/altbeast/nvram` under
  the cwd (this repo's root) at every exit.** The oracle's high-score table (7 x
  412800, round 5, blank names, CRC at 0xFFFC38) is a previous session's state, NOT
  the ROM default (prog68k 0x4732: 50000/40000/.../4000, names HKR UCH SAT HAG HAS
  TOS TAK, which FUN_00004178 installs when the CRC fails). Every arcade frame in the
  corpora shows 412800 in the HUD; a port that boots factory-fresh shows 50000. The
  write census found no 68K writer because NVRAM loads outside the CPU. Measured
  2026-10-04: `-noplugins` changes nothing; `-nvram_directory <empty>` restores the
  factory table.
- **Rule:** an oracle run that must be reproducible passes `-nvram_directory` at a
  fresh directory. See the BATTERY SAVE card (STATE.md) for making the port match the
  board instead.

### The rig's speed is measured by scene, not by feel; "too slow" was the typing stall
- **Method (2026-10-04): relaunch under a fresh name, `echo screenshot >
  /dev/MiSTer_cmd` at known seconds after the `game started` log line, pull the
  rig's own rom-named PNGs, and match each against an ares strip of the same rom
  (`--screenshot f:file` every 30 frames; pass the list through `${=args}` -- zsh
  does not word-split a plain `$args`).** rd63 on mister.office.local: the eye at
  +27 s (ares f1320), the score table's first row at +42 s (ares f2220, mean diff
  3.2), the scene after it at +102 s (ares >= f5340): the rig runs the port at ares'
  frame rate within the launch-offset error. What read as "too slow" was the game
  sitting in the 52-second typing loop above. Rank rig speed only after the ares
  strip says the scene lengths are the arcade's.

### A byte CLAMP on a cell Ghidra stores as a word keeps the WRONG half
- **CPORT_CLAMP narrows `_DAT_fffff142` to a byte because the program reads it with
  `move.b`/`cmpi.b` at 30+ sites; but Ghidra wrote the program's byte stores to the
  0xFFF142/0xFFF143 pair as word compositions -- `CONCAT11(tbl[d0], DAT_fffff143)` for
  0x0670 `move.b tbl(d0),0xFFF142`, `CONCAT11(FFF142, FFF143 + 1)` for 0x0A2A `addq.b
  #1,0xFFF143`, `(ushort)FFF142 << 8` for 0x0BCE `clr.b 0xFFF143` -- and a word assigned
  to a byte keeps its LOW half: the round byte took FFF143's value.** rd67: the second
  attract demo played round 1 (arcade: round 2, 0xFFF142 = 1 at f2686); the reads
  `_DAT_fffff142 >> 8` (0x08C4, the music index) came out 0 for every round. The pass
  rewrites each form to the byte the instruction stores (rd68: round 2).
- **Rule:** before clamping a cell, grep its `_DAT_` stores for CONCAT/shift forms --
  each is a byte store to the pair that the clamp will misdirect.

### Ghidra re-reads a register the callee PRESERVES as `extraout_`; the port fills it from the return
- **0x4624 (score digits) keeps the score in D0 across `bsr 0x4664` (0x4672 `movem.l
  d0/d2/d7-a3,-(sp)`) and rotates it again each pass; 0x45F8 counts the name loop in
  D0 the same way. Ghidra wrote both as `extraout_D0` after the call, and the port's
  extraout machinery fills that from the wait's packed A0:A1 return** -- every score
  printed blank and the name loop ran once (rd68). FUN_00004624 is now hand-derived
  from 0x4624-0x4662 and the name loop from 0x45F4-0x4606 (rd69: rows byte-exact).
  Same family as the dbf count above: a value that lives in a register across a call
  the callee saves/restores is invisible to the decompiler's call model.

### A link error for a symbol the build never needed is a stale object; touch the source
- **rd68 failed twice at the link with `_bake_find: undefined reference to
  _sprbake_blob` (NOSPRBAKE=1 filters sprbake_data.o out, and bake_find is under
  `#ifdef SPR_BAKE`), after an earlier rd68 attempt had aborted in cport_prep and the
  script's `make line` restore had rebuilt the line objects. `touch sh_src/m_main.c`
  and the same command linked.** The flag stamp did not catch it; the mechanism was
  not measured beyond that. Rule: a build script that aborts mid-way must `touch` the
  SH-2 sources before its next port build, and the gate must not run on a rom the
  build did not write (build_rdN.sh saved and gated the line rom after `exit=2`).

### The port's seven credits were MAME's NVRAM too (retraction of the 2026-10-04 MCU seed)
- **port_l2.c seeded 0xFFF000 = 7 at boot on a measurement that read the oracle's
  credit count at frame 7 -- from the saved nvram/altbeast/nvram, not from the MCU.**
  A fresh `-nvram_directory` reads 0 from frame 7 (cell_timeline 0xFFF000, 2026-10-04),
  and the fresh arcade's table scene has no "CREDITS 7" line where rd69 typed one on
  text row 26 (rd72, seed removed: row 26 bare, text rows 1-3 of the table exact).
  Every number taken from `mame altbeast` without a fresh NVRAM directory inherits the
  previous session's work RAM: credits, the high-score table, and whatever else the
  battery would have kept. Rule as above: oracle runs name a fresh nvram directory.

### The layout put an I/O page on top of work RAM: 0xC43001 was object slot 0's flag byte
- **tools/cport_layout.py lays the program's sparse I/O pages (0xC40xxx, 0xC41xxx,
  0xC42xxx, 0xC43xxx) at `--io-base` with a 0x100 stride; with io-base 0x0602C800 the
  fourth page ended at 0x0602CCFF, past the work-RAM base 0x0602CC00, so 0xC43001 --
  the output port irq4_handler writes 0xFFF094 (0xFF) to every vint -- became
  0x0602CC01 = 68K 0xFFC001, byte 1 of slot 0's flags word (0xC43007 = slot 0's +7,
  and the "odd" bucket at io_base + span sat on slot 2).** The hero's flags low byte
  read 0xFF from boot frame 21 (the arcade keeps 0x00; `btst #4/#5,0xFFC001` gate its
  attack states), the partner object (slot 1) never spawned in the second demo and
  the hero stood still. Found by transition watches in port_l2.c (PORT_FFWATCH: the
  byte flipped between the frame wait and the dispatcher's first handler, i.e. in
  irq4_handler) after three days of looking at game code. The Makefile now passes
  0x0602C700, the odd bucket goes below the block, and the tool refuses an overlap.
- **Rule:** a generated memory map is measured like everything else: diff the symbol
  table against the region bounds (`grep PROVIDE layout.ld` sorted by address) before
  trusting any "the port wrote a byte the arcade never writes" finding -- two
  symbols at one address is a layout bug, not a game bug.

### The renderer's phase census lived inside the port's work RAM
- **m_main.c/s_main.c `CEN` at 0x2602FF00 and `FBB` at 0x2602FE00 are SDRAM 0x2FF00 and
  0x2FE00 = the port's work RAM 0xFFF300 and 0xFFF200 (WRAM_BASE 0x0602CC00 + 16 KB).**
  0xFFF300 is the colour cycle's record table (FUN_0001a6ba fills it, colour_cycle_streamer
  walks it every frame: 8-byte records, bit 7 = active, +1 timer, +2 script pointer); the
  census counters landed there as "active" records with garbage pointers, so the streamer
  pushed 16-byte palette blocks from wherever they pointed every frame. Seen for days as
  `fff300:937b/9379 fff306 fff308 fff30e fff310 fff316 fff352` in every work-RAM diff and
  read as "RNG state". Both moved to 0x2602C000/0x2602C100 (free below the I/O block) in
  rd82. Same rule as the I/O page lesson: every fixed address in the renderer is a
  candidate collision with 0x0602CC00-0x06030BFF; `grep -n '0x2602[C-F]\|0x2603[0-9A-C]'
  sh_src/*.c` is the census.

### A 68K `movea.l (a0)+` from a 2-mod-4 ROM address is an unaligned load the SH-2 cannot do
- **FUN_0001a6ba reads the colour-cycle entries at 0x1A6FC (word kind, long script
  pointer at +2): Ghidra's `iVar1 = *piVar5` is a 32-bit load at 0x0235A6FE.** Measured
  rd82: the record held 0x00130001 -- the long at the aligned-down address 0x1A6FC --
  where the arcade holds 0x0001A70E; and `*(iVar1 + 2)` then read the script's timer
  through a raw 68K address. The pass uses LD32S for the load and P68 for the script
  (rd83). The generator's unaligned-access pass catches `*(uint *)(expr)` forms, not a
  plain `*ptr` whose pointer came from a ROM table: grep the unit for `= \*p[a-z]*Var`
  loads whose pointer is a ROM symbol plus an odd-word offset.

### A transition watch whose "previous state" lives in a shared scratch word reports a transition at every neighbour
- **rd80-rd90 (2026-10-04): the PORT_FFWATCH hook kept its previous-state bit in
  port_watch_out[176] bits 16+, and the older A/B watch blocks in the same build
  stored their own bytes into port_watch_out[176] (bits 0-15, with a full-word
  store).** Every hook after an A/B block saw "previous = 0" and reported the
  corruption as freshly written -- at the dispatcher's bookkeeping, at the wait, at
  the renderer's first hook after the game tick -- so ten builds bracketed the
  writer between hooks that had nothing between them. Give every watch its own
  state slot and print the whole ring, not the last entry.

### A caller whose prologue loads A6 owns it for its whole body, not for one call
- **prog68k 0x16B6A `lea 0xffffd400,%fp` (the demo director's init) falls through into
  the director's per-frame handler (restore_position, integrate_position,
  FUN_0000669c, hitbox_a/b_expand, animation_frame_fetch, walk_player_slots -- all on
  fp). Ghidra merged the tail into FUN_00016b6a and wrote the director's cells as
  absolute `_DAT_ffffd4xx`, so CPORT_A6's per-site restore covered only the first call
  and every later callee read the register-file A6: the last object the dispatcher
  ran, slot 48, the first zombie of the demo.** Measured 2026-10-04 (rd77-rd95, a
  transition watch with its own state word, a ring of the last eight transitions and
  the waiting caller's return address): exactly one write, at demo-B start, between
  the waits at 0x928 and 0x94C, in main_entry; the zombie's +0x22 became 0xFF (the
  anim stepper's countdown), its y ran negative (integrate_position), +0x30.. filled
  from its own anim list, it was culled by tick 0x1D and the demo diverged. rd96:
  `_r_unaff_A6 = W68PTR(0xFFD400)` at the function's entry; the zombie rises 4 px a
  tick to 0x10D8 and lives to the hero's attack like the arcade's. The generic rule
  is now a pass: any a6_callsites.json row whose load sits at the caller's entry sets
  A6 for the whole body.
- **Rule:** when a hand-restored A6 covers one call in a function, ask what the 68K's
  fp was for the REST of that function -- a `lea` into fp at the entry means every
  callee, and Ghidra's absolute-cell rendering of the body hides that nothing else
  in the function mentions A6.

### A save/restore around a pointer-wipe is dead code to Ghidra AND to gcc
- **prog68k 0x1306 (the player-record clear, called from main_entry at 0x5D2) reads
  FFE018 (lives) and FFE098 into D2/D3, wipes FFE000-FFE0FF through A0, and writes
  the two bytes back (0x1310/0x1314 -> 0x131E/0x1322). Ghidra's alias analysis does
  not connect the A0 loop to the two globals, so it folded `X = X` away and the port
  wiped the lives byte that the attract-step table (0x1ED4 -> 0x1EDA) had just set.
  FFE018 stayed 0 for the whole demo, the dispatcher's D5 button word read 0
  (`form < 3 || FFE01B` false), and the hero took 0xA140's punch (state 5) where
  the arcade takes 0xA198's kick (state 6) and spawns the partner in slot 1.**
  Measured 2026-10-04: rd98 port FFE018..1F `00 00 00 00 f0 28 02 06` vs arcade
  `01 00 00 01 f0 28 00 06`; port FFF028/FFF029 = 1 at tick 0 (the block ran) with
  FFE018/FFE098 = 0 (its stores were wiped). **A hand-derived C version with the
  save/restore (rd99) compiled to NO access of FFE018 at all** (`sh-elf-objdump -d
  rom/rd99.elf`, `_FUN_00001306`: 16 instructions, the wipe loop and the two
  pointer stores only): gcc at -Os/LTO applies the same object-based alias rule --
  `(undefined4 *)&DAT_ffffe000 + i` cannot touch `DAT_ffffe018` -- and deletes the
  restore. The pass now reads, wipes and writes through `volatile` (rd100).
- **Rule:** any hand-derived loop that writes 68K cells through a pointer and then
  touches a named cell in the same range must use volatile (or the W68PTR domain) for
  BOTH sides; check the function in `sh-elf-objdump` before trusting the C. The
  symptom class is "the block ran, its neighbours' flags are set, this one byte is 0".

### A word link used as a pointer base is a 68K address, wherever it is nested
- **`movea.w ($3c,A6),A0 ; move.b #1,($1b,A0)` (0x48BE, the beast-form flag of the
  player record) came out of Ghidra as `*(undefined *)(*(short *)(unaff_A6 + 0x3c) +
  0x1b) = 1;` -- the 16-bit link sign-extended and dereferenced RAW, 52 sites in the
  unit in the shape `*(T *)(*(short *)(BASE) + K)` (two-hop links included).** On the
  SH-2 0xFFFFE01B is the on-chip area, so the store vanished: FFE01B stayed 0, the
  dispatcher's D5 button word was 0 (`form < 3 || FFE01B`), and the demo hero punched
  where the arcade kicks. Measured 2026-10-04 rd100 (FFE018 fixed, FFE01B still 0) ->
  rd101: hero state 6 / frame 0xAA4C at tick 0x34, partner slot 1 at tick 0x3A, both
  as the arcade. The W68 pass covered the `(int)W68(*(short *)(A6 + 0x3c))` assignments
  only; the nested-deref shape is now its own pass (8/16-bit pointees; the 32-bit
  loads through a link belong to LD32S/LD32U/ST32, which also map the VALUE).
- **Rule:** grep the unit for `*(short *)(` used as an address before trusting any
  object-record field that lives behind a link (+0x3c parent, +0x4c owner, +0x78 chain).

### A bare 68K address handed to a callee is dereferenced raw unless the callee is normalised
- **Six more installers took a literal 68K address in an address register and
  dereferenced it: FUN_00011386 (0x10CCE, the partner's x-step table -- Ghidra also
  lost the `(a0)+` so its loop never advanced; the SH-2's 0x10CCE read 0 and the demo
  HUNG at tick 0x76 the moment the partner first ran it, rd101 coroutine stack
  FUN_000103e0+0x5ac -> FUN_00011386), FUN_000037d0 (0xFFF010: the "50000" top score
  drew "0"), FUN_00001192/switchD_00002ef0__caseD_f0 (0xFFF028/0xFFF004 input
  records), FUN_00004276/4330/4196 (the name-entry record; 0xFFFFFC3A is SH-2 on-chip
  RAM), FUN_000058e4/5e04 (0xFFDE00/0xFFD900 slots), FUN_0000f8c0/14b5e (ROM
  templates), FUN_000180c2 (ROM 0x186FA).** rd102 census (tools: the literal-cast grep
  in this session's notes, now the "ROM table literals" pass): every `(T *)(0x....)`
  argument or local in the unit, classified ROM / WRAM / video / SH-2. Fix = add the
  callee to CPORT_PARAMNORM (A68PTR at entry, identity for an SH-2 pointer) and map
  ROM literals assigned to a dereferenced local through P68 (8/16-bit pointees only:
  `(undefined4 *)0x1233A` and friends are 2 mod 4 and belong to the LD32U installers).
- **Rule:** a template record at an address 2 mod 4 cannot be copied with 32-bit loads
  on the SH-2 -- ares masks the address and the header lands two bytes early (rd106
  slot 18: +0 FBB8, +2 80000000 against the template 8000 0000F90A 013D ...). Byte
  copies in FUN_0000f8c0/14b5e/10394 (rd107).

### `movea aN,fp ; jsr` -- A6 moved for one call is invisible in the C
- **66 sites in prog68k move A6 and then call (19 of them 0x3B2E request_palette_update,
  9 0xE032, 4 each 0xD608/0xD65C, 3 each 0xF8C0/0x12C70/0x13324/0x153EC ...). Ghidra
  keeps the moved A6 in a LOCAL and the callee reads the register file, so the call
  runs on the dispatcher's object.** Two demo cases measured 2026-10-04: FUN_0000f74a
  (0xF7A2-0xF8BE installs its own slot, then `movea.w ($78,fp),fp` twice for the two
  children -- rd103: slot 18 had x/y and no header, 19/20 never existed; and the
  second slot search's `move.w a5,($78,fp)` was written with the FIRST search's a5);
  FUN_0000a1e0 (0xA270 `movea.l a5,fp ; jsr 3b2e`: the partner's palette request ran
  on the hero's colour set, already held -- arcade FFF480 `.. 004B 0050`, port `.. 004B
  FFFF`, partner pal 0550 vs 0050; rd104 both exact). Both are hand passes on the
  final text; the general mechanism (publish `_r_unaff_A6 = <local>` before the call
  from the disassembly's fp moves) is an OPEN CARD with this census as its size.
- **Rule:** when a callee that reads `_r_unaff_A6` is reached from a function whose 68K
  moves fp (`movea.w (sp)+,fp`, `movea.l a5,fp`, `exg`), the C call is wrong until A6
  is published; `grep -c 'moveaw .*,%fp\|moveal %a.,%fp' prog68k.dis` is the budget.

### Carry and D0 out of a helper: Ghidra drops both, and the callers test a placeholder
- **30 functions in prog68k return through `move #1/#0,CCR` (census 2026-10-04, from
  prog68k.dis + func_ranges.txt); Ghidra renders the callers as `bVarN = false; FUN();
  if (bVarN)` -- the branch is dead or always taken -- and their D0 results as
  `extraout_D0w = (uint)result` (the pointer half) or a function-entry pseudo-register.
  The enemy AI's distance helpers are four of them: 0xE266 (nearest live player, D0 =
  its slot word / 0 / 0xFFFF), 0xE2C2 (|x40 diff| or -1), 0xE2DC (type compare, carry
  = sum < 3), 0xE23C (|x40 diff| in D0, carry = facing-relative), plus 0xF77C (too
  close), 0xD776/0xDBB8/0x1953C (spawn into the first free record -- Ghidra lost the
  `rts` on found, so the port installed into EVERY free record: 14 death parts per
  call), 0x100B0, 0x154C0, 0x17BF8.** Measured rd103: slot 16 (the first zombie pair)
  took the "within 0x30 px" branch on its first tick (state 2, script 0x10A5A, dy
  0x80) with the hero 0xE8 px away, because `extraout_D0w_00 < 0x30` read a stale
  pseudo-register; rd106: slot 16 state 01020400 / script 0x109D2 like the arcade's.
  Eleven helpers are hand-derived from the 68K and publish D0 in `port_e_d0` and the
  carry in `port_carry`; the call-site pass rewrites the placeholder assigned false
  before the call (or tested after it, `in_CF` included), the `extraout_D0.. =
  (uint)x` lines, the comma forms `(x = FUN(), bVar)`, and the next D0 read. The five
  attack lookups of the other beast forms (0x5062/0x7F58/0x8D66/0xBBC6/0xC9B4, 80-100
  sites each, same shape as the hand-derived 0xA3B0) are gameplay, not attract: OPEN
  CARD.
- **Rule:** before trusting any `if (bVarN)` after a call, check whether the 68K
  callee ends in `move #n,CCR`; the census above is the list. A helper that scans a
  record table for a free slot and installs must `return` on the first hit.

### The port's object was compiled from the text BEFORE the derived casts
- **tools/cport_build.sh produced port.o inside its feedback loop (pass N reaches 0
  errors because the pointer/integer puns are warnings under -Wno-error) and only THEN
  ran tools/cport_cast.py, which rewrites the unit; every build since 2026-09-27
  linked an object whose source differed from the committed text.** Found 2026-10-04
  when cport_cast.py learned to turn `(undefined4)(&LAB_00003d60)` into the 68K value
  0x3D60: the text lost the symbol, cport_layout.py stopped PROVIDE-ing it, and the
  stale object still referenced it (rd107: `undefined reference to _LAB_00003d60`).
  The script now recompiles port.o from the final text after cport_cast.py.
- **Rule:** whatever rewrites the unit last must be followed by the compile that makes
  the object; "the compiler is clean" is a statement about the text that was compiled,
  so print which text that was.

### The object handlers' state machines are PC-relative jump tables Ghidra never recovered
- **`lea table(pc),a0 ; move.b state,d0 ; add.w d0,d0 ; adda.w d0,a0 ; adda.w (a0),a0 ;
  jmp (a0)` -- 49 sites in 20 functions (prog68k 0xFDDA and siblings), 702 "Could not
  recover jumptable ... Too many branches" warnings in the unit, and each decompiled as
  `(*(code *)((int)*psVar + (int)psVar))()`: a 68K address computed from SH-2-raw
  reads and CALLED.** rd107 (2026-10-04) hung at tick 0x51 the first time a zombie
  child (FUN_0000fd6c) reached its table (coroutine stack: FUN_0000fd6c+0x546 ->
  jsr @r0 with r0 from a word at SH-2 0x100F6). The targets were not even in the
  functions: Ghidra's bodies stopped at the jumps (fd6c = 0xFD6C-0xFE67, 0xFEAE-0xFED9,
  0xFF82-0xFFFF; the states at 0xFE68/0xFE86/0xFF7E belonged to nothing).
  tools/ghidra/make_jumptables.py derives each table from the bytes (entries while the
  target is this function's own code -- in its body or in the gap between its entry
  and the table -- or another function's ENTRY), disassembles the gaps, writes a
  JumpTable override and re-fits the body; 13 word/long tables written, the 40
  `jmp (a0)` sites after an attack lookup are the OTHER-FORM ATTACKS card. The
  decompiler then emits `switch(psVar) { case (short *)0xfdd0: ...}` (a switch on the
  68K target with pointer-typed labels, "Switch is manually overridden"); cport_prep
  makes the switch and labels integers and reads the table word through A68PTR.
  THE WORD IS RELATIVE TO ITS OWN ENTRY (`adda.w d0,a1 ; adda.w (a1),a1`), not to the
  table base: the first run computed base-relative targets and handed Ghidra 0xFE86
  and 0xFED4, both mid-instruction (a `halt_baddata()` case, and rd108 still hung at
  tick 0x51); entry-relative gives 0xFE68 0xFE88 0xFDDC 0xFEDA 0xFEDA 0xFF88 0xFDDC,
  every one an instruction boundary. A wrong override also leaves misaligned
  disassembly in the function's gaps; the script clears them before re-disassembling.
  WHAT THE DECOMPILER THEN WRITES: it normalises the switch to the register the index
  was LOADED THROUGH and folds the load away -- `move.b (0x23,a0),d0` becomes
  `switch(iVar12) { case -0x23: ... }` or `switch(unaff_A6) { case (byte *)0xffffffdd:
  ... }`, i.e. case value = state byte - 0x23 (all eight pointer-switch sites of the
  re-export; the index is +0x23 at every one). cport_prep switches on
  `*(byte *)(obj + 0x23) - 0x23` with the labels as signed ints. Only FUN_000143dc
  came out as a switch on the target address itself.
- **Rule:** `grep -c 'Could not recover jumptable' docs/proof/altbeast.c` is a count
  of garbage calls, not a cosmetic warning; every one is a hang waiting for its state.
  Derive the table from the bytes and hand it to the decompiler -- never hand-write
  the switch.

### P68 ate an SH-2 pointer: the child zombies never saw their parent die
- **FUN_0000e20c returns `W68(link)` -- an SH-2 pointer -- in A0, and its callers
  (FUN_0000fd6c, FUN_000103e0 ...) read the parent's state as `*(byte *)P68(iVar12 +
  0x23)`. P68 masked the pointer to 24 bits, called it a ROM offset, and the compare
  `parent.+0x23 == 4` was against a byte of ROM.** Measured 2026-10-04 (rd109,
  tick-aligned dumps): the trio's parent (slot 18, 0xF90A) reached hp 0 and state 4 at
  tick 0x88 like the arcade's, but its state-4 code waits for both children (+0x78
  chain) to be in state 4, and the children (0xFD6C) stayed in state 3 for the rest of
  the demo -- the arcade's parent switched to the death handler 0x1004C by tick 0x90.
  P68 now passes through any value whose top byte is neither 0x00 nor 0xFF (every 68K
  address is one of those; every SH-2 pointer is 0x02/0x06/0x20/0x26).
- **Rule:** a translation macro applied to a VALUE must be the identity for values
  already translated; a helper that returns a mapped pointer poisons every P68 site
  downstream of it. The A68ANY/A68PTR family had the guard; P68 did not.

### A callee that moves A5 must be re-read at EVERY call shape, the comma form included
- **FUN_0000f74a's first slot search is `(FUN_0000e032(), bVar5)` inside a condition;
  the pair-allocator pass re-read A5 only after statement-form calls, so `sVar1 =
  A68OF16(unaff_A5)` took the register file's A5 -- the pair spawner's, slot 17 -- and
  the trio's first child was installed OVER the pair's second zombie (rd110
  2026-10-04, per-frame dumps: slot 17 = 0x0824 at tick 0x51, 0xFD6C at 0x52; the
  arcade keeps 16/17 = the pair and puts the trio in 18/19/20).** The child then ran
  BEFORE its parent in the dispatch order (17 < 18) and saw the parent's state one tick
  late, which is where the hit/death race of the trio diverged.
- **Rule:** the slot an object lands in is a measurement, not a cosmetic difference:
  dispatch order follows slot order, and every "sees the parent's state" handler is
  one tick off when a child sits below its parent.

### Three more silent-wrong-value classes found by the tick-aligned object diff (rd112)
- **(1) `cRamfffff035` is not `DAT_fffff035`.** Ghidra names a byte it reads through a
  register-relative form `cRamXXXXXXXX`; cport_prep mapped `DAT_`/`_DAT_` cells into
  work RAM and left ten `cRam` names as zero globals in defs.c. FUN_00003706 (the
  kill's points, 0x3706: four `abcd -(a1),-(a0)` from 0xFFF032 into the player record's
  pending bonus +4..+7) added zeros -- and its `bcdAdjust()` is a stub whose result is
  discarded. MAME wp census on FFE006 (tools/wp_census.lua, WP_RANGES=FFE006:2): the
  arcade's writer is PC 0x3780/0x3782 and the cell goes 1 -> 2 -> 3 as the trio dies;
  the port's stayed 0. Hand-derived as a 4-byte BCD add (rd113).
- **(2) A script pointer that is also a function is stored as the SH-2 symbol.** 0x113F8
  is both; Ghidra writes `pcVar15 = FUN_000113f8; *(code **)(unaff_A6 + 0x24) =
  pcVar15;` and the port stored 0x0238DD40 (the function's SH-2 address) into +0x24.
  The hero's special-attack flame (slot 18, handler 0x10DD4) then ran a garbage script
  (y = 0xF77F) and never touched zombie 49, which the arcade's flame hits for 8 every
  8 ticks from tick 0x206 (49: hp 0x20 -> 0x18 at 0x20E, -> 0x10 at 0x21E, -> 0x08 at
  0x228; the port's 49 was untouched until 0x246). Nine sites; cport_prep now keeps
  the 68K value for the bare assignment, the store, the load and the
  `p - 2 + *(short *)(p - 2)` script-offset form (rd114).
- **(3) FUN_00011386's period is read once.** 0x11392 `move.b (a0)+,d1` happens before
  the loop; 0x11394 `sub.b d1,d0 ; bcs ; addq.l #1,a0 ; bra` subtracts the SAME d1 --
  the delta is table[1 + d0/period]. The first hand-derivation (rd102) re-read d1 each
  pass as (count, delta) pairs: the zombie pairs drifted 0x20 px by tick 0x1FE (slot
  20: arcade x 0x1213, port 0x11F3). Fixed in rd114.
- **Rule:** when the live set and the hero match tick-for-tick, diff EVERY live record
  (all 0x80 bytes) at one tick; the three classes above were each one object's few
  bytes, and none showed in a picture gate.

### A helper that leaves a register for its caller: 0xE266's A2 and the handler's A2 PARAMETER
- **FUN_0000e266 ends with A2 on the chosen player's chaser-count byte (0xE2A2 `lea
  0xFFF156,a2 ; ... addq.l #1,a2`), and the pair handler tests it: 0x1069A `cmpi.b
  #2,(a2) ; bcc 10700` (-> state 3, stop and wait). Ghidra gave FUN_000103e0 a
  PARAMETER `byte *p_A2` and wrote `if (1 < *p_A2) goto LAB_00010700;` -- the
  register-file A2 at dispatch, never e266's.** Measured 2026-10-04 (rd114, slots
  20/21 of the demo's second pair): the port's pair entered state 3 between ticks
  0x160 and 0x17D (`st 00010103`, script 0x109A2, dx 0) while the arcade's kept
  walking, and sat 0x1C px left of it by 0x1FE; FFF156/FFF157 are 0 on both machines,
  so only a stale pointer could read "2 or more". The hand-derived e266 publishes
  `port_e_a2`, and every caller that holds A2 as `p_A2` or `unaff_A2` re-reads it
  after the call (rd116).
- **Rule:** a parameter named after a register (`p_A2`, `p_D1`) is the value at
  ENTRY; any callee that writes that register for the caller's benefit (the pair
  allocator's A5, e266's A2, the finders' A0) needs an explicit re-read after the
  call. The 68K ABI here is "whatever the callee leaves", not "whatever came in".

### `jsr ; beq` -- the flag after a call is a bool Ghidra computed BEFORE the call
- **0x10650 `jsr e266 ; beq 1063a` tests the Z of e266's D0 (0 = the chosen player
  already has two chasers). Ghidra wrote `bVar16 = (bVar6 & 0xc0) == 0; if (bVar16)
  { uVar13 = FUN_0000e266(); if (bVar16) { ... } }` -- the inner test reuses the
  outer bool, so the port ALWAYS took the D0 == 0 path (0x1063A: e2dc, e23c, then
  0x1072E `cmpi.b #17,(0x20,a0) ; bcc 10700`).** Measured 2026-10-04 with a probe
  build (CPORT_PROBE=103e0 stamps the tick and the decision's inputs at each entry to
  LAB_00010700): at tick 0x161 the site fired with distance 0xFA, target = player 2
  (0xFFC400) in state 0x14 -- "target busy, wait" -- while the arcade, with D0 =
  0xC400 != 0, walked. Six `jsr e266 ; beq/bne` sites (0x10650 0x11C20 0x14DBA
  0x156EC 0x15832 0x166FE); the bool tested within four lines of the call is now
  `port_e_d0 == 0` (rd118: the second pair's x 0x11DB/0x11E9/0x1214 at ticks
  0x17F/0x19B/0x200 against the arcade's 0x11DB/0x11E9/0x1213). The sibling idiom
  `notw d0 ; beq` came out as `extraout_D0w == -1` -- a ushort promoted to int can
  never equal -1 -- and is now `== 0xffff`.
- **Rule:** a bool tested right after a call that returns in the flags is the
  call's flag, whatever name Ghidra gave it; the probe build (stamp the tick and the
  inputs at the branch) found this in one build where reading the C did not.

## A RIG LINE NEEDS THREE LAUNCHES, AND A ONE-COLOUR STRIP ROW IS NO BAR (2026-10-09)

47.5 ticks/second and 73% one-vint were THE LINE for two days and both were wrong: they came
from reading 2 of ONE launch (336f13c7), differenced against reading 1 of the same run. That
divides the boot out and says nothing about launch-to-launch spread. FOUR fresh launches of
the same rom (pc9rig 37cafdc6) under distinct names, log-proven, shots at 82/152/222 s, read
33.4 cumulative at 222 s and differenced to **34.8 ticks/second and 39% one-vint** -- 13
ticks/s and 34 points off. A single reading is not a line; three launches are.
And the reader's own rule, which is what let a bad shot through: **a strip row that is one
colour across all 320 px is NO BAR -- discard the shot.** A bar is a leading run against a
cleared remainder, so a row with no transition carries no length. tools/rig_bars.py:read()
enforces it and raises; its two dead helpers, one of which called a one-colour row "a full
bar", are deleted. (Mike, rig session 2026-10-09, mister.office.local, seven launches.)

A SCENE TABLE IS THE UNION OVER THE WHOLE STEP, NEVER A WINDOW. Demo A's step byte
0xFFF031 reads 0x0C from f500 to f1100; a set list taken from f680-780 missed sets 0, 19,
20 and 21, which appear only as the phase turns at f1090. The table built from it left them
with no MD line and the rom ran 37 game ticks behind by f700. The cset is also
`(md_tag >> 16) & 0x7F` (m_main.c:3676) -- a 0x7FFF mask lets a second flag bit through and
invents csets. 2026-10-13.

A CANDIDATE THAT ADDS A PALETTE TABLE SHIFTS THE CLOCK, SO ITS GATE IS TICK-MATCHED. A build
carrying a new table with its flag OFF still read tick 235 at f700 against the line's 226.
Frame-matched screenshots against the line are therefore meaningless for such a candidate,
and the first identity run of one compared it against a contaminated control. 2026-10-13.

A PALETTE TABLE INDEX ABOVE 4 EMPTIES THE BAKED CAT-1 MASKS AND COSTS ~58,000 CYCLES/VINT.
mds_install takes the table index as its scene argument (:8602, :8616) and hands it to
c1mask_install, which refuses anything >= CAT1MASK_SCENES = 5 (m_main.c:1171,
sh_src/cat1mask.h:4) and sets c1mask_n = 0. Every cat-1 tile then rebuilds its opacity mask
in c1rt_class (:1151-1167), 64 pixels through the libgcc variable shift the SH-2 needs for
want of a barrel shifter, per tile per frame. Measured 2026-10-13: a demo-A table at index
11 cost +58,039 cyc/vint of _m_main and 43 GAME TICKS by f700, identical whether its pens
were renumbered or byte-for-byte table 0's. MDR_ATTRACT_TABLE is 10, so the title is
predicted to pay the same today -- unmeasured. Two attempts were misread as a short set
list before the loop was disassembled; cs_[18] (note_tile) stays near zero and is NOT the
detector. The detector is the game tick at f700 and _m_main cyc/vint.

THE SCENE-TABLE REFUSE TESTS THE WRONG BOUND. m_main.c:4107 guards `ti < MDSTATIC_N` with
MDSTATIC_N = 2 (pal_scenes_md.h:5), but under MD_ROUND `ti` is the TABLE index, bounded by
MDROUND_N = 11 (:4100). Every installed table above index 1 therefore BYPASSES the refuse
and its non-table sets go to the dynamic allocator: table 10 at the title and gameplay
rounds 2-4 on the line today. Measured 2026-10-14 on a table-11 build: it is real but small
-- assigns +11 per 100 vints against the line's +9, evictions +77 against +10 -- so it is a
correctness defect, NOT the ~53,000 cyc/vint a second resident table costs. Filed unfixed.

A RIG READING IS A PAIR: TICKS/S AND PRESENTED FRAMES. tools/rig_bars.py measures ticks per
second and the one-vint share -- the ENGINE rate -- and says nothing about the picture. The
picture is BOOTFLIPRATE=1 read off the value channel (presented frames per 64 vints, FS bank
changes between vint tops, md_main.c:4166-4182). Measured 2026-10-14, three launches each:
clean1 0.71 frames/64, marks2 8.54, the line 8.91, c1r 8.45 -- while rig_bars read 42.2
ticks/s for both the line and c1r. A build can raise the engine rate, leave the picture at 8
of 64 and read as a WORSE slideshow, which is what Mike's pass on c1r found and what
rig_bars could not express. Report both numbers or neither.


THE MiSTer 32X CORE HAS NO IN-FLIGHT GUARD ON THE SH-SIDE VDP PATH, AND BOTH SH-2s SHARE
IT. srcref/S32X_MiSTer/rtl/32X/IF.sv:974-983: the `if (SH_VDP_WAIT && !SH_VDP_ACCESS)`
that would refuse a new access while one is pending is COMMENTED OUT, so any new bus start
on the region re-latches VDP_A / VDP_DO / the strobes over an access still waiting for the
VDP's ack, and that earlier access is never performed. The region is 0x20004100-41FF
(registers), 0x20004200-43FF (CRAM) and 0x24000000-2403FFFF (the framebuffer), IF.sv:922-924;
FM at 0x20004000 and the COMM registers are NOT in it. MEASURED 2026-10-14 ON mister.tv.local ONLY (a rig since
shown not to display this renderer's picture at all -- treat the NUMBERS as suspect; the
RTL fact above is read from the source and stands),
three launches each: with the master committing CRAM from its V-ISR while the slave drew,
the sprites came out correctly shaped and almost entirely WHITE (1,750 white pixels);
with every master VDP-region access moved into the window between the slave's DONE and the
next GO, white fell to 410, then 33, then 1 and the colours were right. DO NOT INTERLEAVE
SH-SIDE VDP ACCESSES FROM THE TWO CPUs. Nothing is dropped at the VDP itself -- VDP.sv:176
accepts only when !FEN and !FIFO_FULL and otherwise holds ACK_N high so the cycle WAITS, and
FEN is FILL_EXEC | RFRH (:411), fill or refresh, not display fetch.

A BYTE WRITE OF ZERO TO THE DIRECT FRAMEBUFFER IMAGE IS DROPPED ON ares AS WELL AS ON THE
CORE. The title of this entry used to say "by that core", which read as a MiSTer quirk one
could ignore on ares. IT IS NOT. Measured 2026-10-17: the priority punch's BYTE stores
(sw156-sw160) left every framebuffer row intact on ares while the punch's own counters
happily counted them; the same punch in LONGWORDS (sw161) zeroes the pixels. So the rule
is unconditional -- ANY framebuffer clear or punch must be word or longword, on every
platform -- and a counter that says the stores happened is not evidence that they landed.
The RTL below explains the mechanism; the ares measurement is what makes it a rule.
VDP.sv:513-514 gates each byte lane on `(|D & A[17]) | ((|D | WE[other]) & ~A[17])`: for the
OVERWRITE image (A[17]=1) each lane needs its own byte non-zero, which is correct overwrite
behaviour; for the DIRECT image a zero byte is only written when the other lane is enabled
too, i.e. when it was a word or longer write. So a per-byte clear through the direct image
erases nothing there. Word and longword clears are unaffected.

THE 32X FRAMEBUFFER IS TWO BANKS AND `--dump dram:0x200` READS ONE OF THEM. Which bank holds
the finished picture alternates with the flip, and a dump can also catch the slave MID-PASS:
measured 2026-10-14, one read gave 2,768 px and no bars while the other bank held 12,211 px
with every bar complete. Dump BOTH (dram:0x200 and dram:0x20200) and take the complete one.
A whole conclusion was built on a single-bank read that night -- a "frame split across the
banks" that did not exist -- and withdrawn the same night when four frames were dumped
properly: at f900 one bank held 4,665 image AND 420 bar pixels.

ON ares THE PICTURE IS `--screenshot`, NEVER A DUMP. `ares-headless --screenshot N:file.png`
captures the DISPLAY -- through the line table, 32X CRAM and the 32X/MD layer mix -- while
`--dump dram:...` reads the framebuffer and bypasses all three. MEASURED 2026-10-14: the r60
line's screenshot at f1099 is a black field with a green MD border and the white bar, NO
SPRITES, while a dump of the same frame counts thousands of sprite pixels in the same bank.
A whole night was spent treating a display bug as a board-only hardware mystery because
every ares check was a dump, so ares agreed with itself and disagreed with the FPGA. Control
the instrument before trusting it: the same capture of a different rom must differ (marks2
does).

THE 32X SPRITE COLOUR SETS LIVE IN THE UPPER HALF OF THE PALETTE SHADOW. PAL_SH[1024 +
set*16 + pen], not PAL_SH[set*16]. MEASURED 2026-10-14 at f1100: sets 0, 1, 2 and 3 are ALL
ZERO in the low half and hold 13-14 real colours at +1024 (set 0: 7DEE 0CDD 7ABB 09AA). The
two heroes of the opening scene are colour set 0, so reading the low half blacks out exactly
them -- which is a convincing wrong answer, and it cost a build to put back.

MARS_VDP_PRIO_32X IS NOT IN Hw32xInit. mars.c:40 writes `MARS_224_LINES | MARS_VDP_MODE_256`
with no priority bit; the old renderer set the full value itself (m_main.c:8708 and :13820)
and the 68K's copy (md_main.c:4592) is inside shim_vblank. A renderer that replaces either
one inherits a 32X layer BEHIND the Mega Drive layer and has to write DISPMODE itself. Note
the SH-side VDP access is gated on FM in the MiSTer core (IF.sv:974) and md_main drops FM at
the END of its boot (:7380), so a DISPMODE write at master init can be thrown away: assert
it late, with FM up, and read it back (0x8081).

AN SH-2 WRITE TO MARS_VDP_FBCTL IS DROPPED AT FM=0, AND IT LEAVES NO TRACE. The SH-side
VDP access is only latched with FM (MiSTer IF.sv:974, `SH_VDP_SEL && ADCR.FM && ...`), and
ares returns before its own trace hook (ares-debug io-internal.cpp:344-349) -- so a dropped
FS write does not even appear in --trace-flip. MEASURED 2026-10-14 on r60: the V-ISR flipped
on exactly the condition that had already made the master drop FM earlier in the same vint,
so EVERY flip request for 650 frames was thrown away. --trace-flip logged 220 writes whose
LAST was at frame 447 of 1101; framebuffer half 1 changed 0 bytes across f1097-f1100 while
half 0 alternated by 5,276; two consecutive screenshots were byte-identical. Write FS while
FM is still up. It is then outside vblank, which is harmless: the hardware defers it to the
next vblank start (VDP.sv:400, `if (VBLK || MODE==0) FS <= FBCR.FS`). After the move:
699 writes, 701 flips, the last at frame 1099, both halves live, and the sprites on the
display for the first time.
  A RETRACTION LIVED HERE: that ares "pins the overwrite image to bank 0". It was wrong --
ares-debug bus-internal.cpp:51-52 and :60-61 both write vdp.bbram and there is no per-image
bank. The table it rested on (sprites always in half 0, bar in both) was real; it was every
pass writing the same half because the flip never applied, and the half-1 bar a fossil.

RETRACTED, AND THE RETRACTION IS THE LESSON: "nothing written to framebuffer rows 216-223
reaches the MiSTer's screen". It was measured on mister.tv.local ALONE, and the same builds
show the bottom bars on mister.office.local. The rows were never the problem.
  WHAT WAS BROKEN WAS THE RIG. The whole r60 board history from 2026-10-13 -- speckled
quarter-density sprites, five instrument channels reading zero, ~2 pixels per several
hundred stores, and a dozen hypotheses excluded against those numbers (pen, framebuffer
image, write width, rate, order, row range, line table, display mode, bank, flip protocol,
access overlap, store interleave) -- was measured on tv, and tv is the rig that cannot show
this renderer's picture. THE SAME BUILD, r60win1 (516677c0), draws the whole picture at
full density on office: nonblack 11,864 / 8,877 / 7,019 over three captures, both
instrument bars present, correct colours.
  SO: every hardware conclusion dated 2026-10-13/14 that rests on a board capture carries
the qualifier "on mister.tv.local" and should be re-read on a rig that is known good. The
RTL facts, read from the core's source, stand on their own; so do the ares facts.
  THE METHOD LESSON, which is the durable one: A SINGLE RIG IS A SINGLE INSTRUMENT. An
instrument that disagreed with the emulator across forty-five builds should have been
suspected long before the code was, and the cheap test existed the whole time -- the same
rom on the other rig.

A 16-BIT FRT DELTA WRAPS AT 356 SLAVE LINES, AND THE r60 PASS IS LONGER THAN THAT. The
slave's FRT runs phi/8 = 48,208 ticks a vint over 262 lines = 184 ticks a line, so a
uint16_t delta -- `(uint16_t)(r60_frt() - t0)`, which is how r60 timed its draw pass --
rolls over at 65,536 ticks = 356 lines and reports X for a pass of X+356. MEASURED
2026-10-14 by a master-clock event ring: the pass that the 16-bit delta called ~150 lines
actually ran 419 (line 9 of one frame to 166 of the next) plus 38 more for the CRAM
commit, every write of which waits on PEN. That is why every variant of the beat cost two
vints while the counter said there was room. Every "lines per pass" figure r60 reported on
2026-10-13/14 is ambiguous by 356 and none should be quoted; extend the accumulator to 32
bits or sample a coarser clock. The DIRECTION of the per-row clear's saving is still
corroborated (flips 293 -> 310 at the same cadence), the MAGNITUDE is not.

CHECK WHICH ARM OF THE #IFDEF YOU ARE READING BEFORE YOU BUILD TO IT. The r60 planes'
first build emitted a TYPE 0 tile-art packet against the contract at md_main.c:2326-2340,
which is inside the `#else /* legacy staged consume */` at :2323 and is COMPILED OUT under
R60. The live R60 consume is the DIRECT path and takes art only as the ART TAIL of the
name-table packet -- sc[1] bit 14, then [n], then inline records slot|0x4000 plus 16 words
(:2027, slim_walk :735). MEASURED 2026-10-14: VRAM tile memory changed by ZERO bytes
between frames while 555 batches counted as "consumed", because the counter counts the
packet, not the reader. The contract was quoted into a handoff with its line numbers and
cost a build; grep for the enclosing #ifdef, or compile a deliberate syntax error into the
branch you think you are reading.

A SCREENSHOT DIFF BETWEEN TWO ROMS AT ONE FRAME IS NOT A PIXEL GATE ONCE CADENCE MOVES.
They are showing different GENERATIONS, not different pixels. MEASURED 2026-10-15 on the
zoom rewrite: sw70 and sw73 draw the eye pan's logo IDENTICALLY -- 2,194 of 2,194 pixels
of record e5 0x129 (vz 9 hz 9, x 119, 38 rows) match the exact per-pixel simulation from
sprites.bin, 0 extra logo pixels, at the record's own x, in both roms (sw70_fb_1400.bin /
sw73_fb_1400.bin) -- while their whole-screen screenshots at frame 1400 DIFFER, because
sw70 had flipped 1010 times by vint 1386 and sw73 1103. The faster rom is simply further
through the attract. To gate pixels when cadence differs, compare ONE RECORD against a
simulation of that record, or align on the game's own clock; never two roms' frames.
(The same trap in the other direction: 45 board builds were compared against a fixed ares
frame while the attract moved underneath -- see the tv-rig retraction above.)

THE 32X CRAM LAYOUT OFFSETS SPRITE SETS: the eye logo's pens sit at FB byte 0x80 + pix for
colour set 7, NOT cset * 16. Any sprite plot that derives its pen base as (cset & 15) << 4
is wrong for these records; r60's first plot did exactly that (sh_src/r60/r60_main.c,
rebuild60-p1) and the layout is superseded by the sw7x line's.

A VDP DMA SOURCED FROM THE 32X FRAMEBUFFER MOVES ~18 WORDS PER 68K LINE, display on or
off. MEASURED 2026-10-15 against the r60 fill packet: 1,628 words (588 of name table for
14 full rows, 976 of inline art, 64 of palette) takes ~90 68K lines, which is the span
actually observed -- the consume runs from line 226 to line 53-59 of the NEXT frame, so
the FM=0 span crosses the vint and windows run at 0.5/vint through a fill.
SLIM-PIPELINE.md's proven 512 words/vint is the same rate seen from the other end.
WHAT IT BOUNDS: the lag at a scene cut (30-45 frames behind the arcade) is a function of
the packet's WORD COUNT and nothing else, until the packet carries fewer words. Two ways
round it were measured and both are dead. CAPPING RECORDS PER WINDOW DOES NOT HELP AND
HURTS: at 12 and 20 records a plane the windows still run 0.5/vint and the scene becomes
whole LATER (sw83 at 522, sw82 not by 525, against sw80's 500), because the ROWS are the
bulk and the cap only touches art. STAGING TO WRAM AND DMA-ING FROM THERE IS SLOWER THAN
THE DMA IT REPLACES: the 68K's own copy loop out of the framebuffer pushed the consume
end to line 107/72 of the next frame against 53, and cost 36 flips by f553 (483 against
519) because every normal frame pays the copy too.

BOTH ROADS OUT OF THE SCENE CUT ARE RATE-BOUND, AND THE TWO RATES ARE NOW BOTH MEASURED.
2026-10-15. The lag at a cut is 30-45 frames; there are only two parties who can close it
and neither can at full width.
THE MD SIDE is bounded by the DMA: ~18 words a 68K line out of the 32X framebuffer, so a
1,628-word fill packet is ~90 lines (above).
THE 32X SIDE is bounded by the stores: a full-screen paint of 2,436 tiles costs 2.1-2.5
VINTS a call -- 156k pixels of reads and stores -- after the per-pixel map reads have
already been hoisted into a per-tile table (that hoist alone took it from ~15 frames a
draw to ~2 vints, so the easy factor is spent). Measured sw85/86/87, R60_GATE_FILLV 39
calls / 82 vints by f470.
AND THE 32X SHAPE FAILS ON THE PROTOCOL BEFORE IT FAILS ON THE CLOCK: at 2+ vints a pass
the slave outruns GO/DONE, the master declines every vint (DIAG[8] +1/vint), flips fall
to one per ten frames, and only part of one bank is painted (sw86 f470: bank 0 zero fill
pixels, bank 1 16,456). Any future full-width-redraw card has to answer the protocol
first, not the arithmetic.
WHAT THIS LEAVES: incremental only, on either side -- fewer words in the packet, or a 32X
paint that touches the scroll delta and the cells under last frame's sprites and nothing
else. A card that redraws the whole screen at a cut, by either party, is dead on both
rates and should be killed at the top without a costing exercise.


THE 32X CAN SCROLL ITS WHOLE LAYER FOR FREE, AND THAT IS NOW PROVEN (on ares).
2026-10-15, R60LTPROBE, rom build/port/rom/lt2.32x on advisor-bisect. Advance the DRAW
bank's line table by one word every second slave pass and toggle the VDP shift register
at 0x20004102 bit 0 on odd passes: the displayed picture moves and NO PIXEL IS COPIED.
Verified by cross-correlating the text band's shift against the displayed bank's lt[0] --
0x101 gives 2 px, 0x102 gives 4 px, the shift bit adds the odd pixel (lt2_lt0_N.bin /
lt2_N.png, frames 615-625). Word granularity is 2 px and the shift bit buys the 1 px
between them, so the pair addresses every pixel of horizontal scroll at zero draw cost.
BOARD UNVERIFIED as of this entry -- the MiSTer is the gate for anything that reaches the
display, and this has only been read on ares. The board check is lt2 on the office rig,
32X layer walking left ~1 px/frame from ~frame 615.
CONSTRAINT, from Sega bulletin #14 above, which this makes live for the first time: the
shift bit is invalid when the line table base address's low byte is $FF.
THE TRAP THAT COST THE FIRST PROBE: it keyed on r60_frame and NEVER FIRED IN THE SLAVE.
r60_frame is cached SDRAM and the slave reads a stale copy. **Slave-side timing keys on
R60_DIAG[0]**, which lives in the uncached control block. This is the same class as the
0x26028EC0 clobber: anything the two CPUs share has to be in the uncached block, and
"the probe did nothing" is its symptom, not a dead idea.

THE COLOUR SIDE OF A 32X-DRAWN PLANE IS NOT AN ALLOCATOR PROBLEM, AND SAYING IT IS
RESURRECTS A PIVOT THAT WAS MADE ON PURPOSE. Mike's correction, 2026-10-15, against the
advisor's "the 256-entry CRAM is the wall". It is not a wall and it is not a runtime
question. The arcade shows <= 112 colours at once, so the slots are not squeezed
(palette-fits-in-cram); the 2026-08-31 pivot made the palettes per-scene and STATIC on
purpose; and bake_tilecram.py already bakes the per-round/scene pen tables, which passed
the CAT1MD play pass. A 32X-drawn plane therefore takes its colours from a BUILD-TIME
table for the scene it is in. Any card that proposes allocating, LRU-ing, memoising or
re-packing pens at runtime for this is re-opening a closed pivot and should say so out
loud before it is costed.

ares --profile CYCLE SHARES ARE NOT WALL TIME FOR FRAMEBUFFER-BOUND CODE. 2026-10-15,
step B: the --profile run put the plane fill at 3% OF CYCLES while the FRT phase timers
read the same fill at 0.4-0.9 VINT a pass. Both are correct and they measure different
things -- ares charges the SH-2 instruction cycles only (no SDRAM or uncached waits, no
data cache), so a routine whose time is in the bus shows up as almost nothing in the
profile. The profile ranks COMPUTE; only an on-rom timer ranks a routine that writes the
framebuffer. This sits beside the denominator trap (--profile forces the interpreter, so
its generation count differs from the recompiler's): the profile has now been wrong about
this code in two independent ways, and neither is a bug in it.
THE INSTRUMENT THAT IS RIGHT HERE is the FRT phase timer block, R60_PH at 0x26028FC0.
Same caution as always: the FRT is 16-bit and wraps at 356 lines.

AN ares STORE-COST ARGUMENT DOES NOT TRANSFER TO THE PLANE FILL WITHOUT CHECKING THE
LOADS. The record's "ares overcharges stores and undercharges the rest" was measured on
the BLIT (above, ~0.5 lines a stored group), which is store-dominated. The plane fill is
156k pixels of READS AND STORES, so the two errors pull opposite ways and the net sign is
not known. "ares overcharges stores, so the rig will run the fill faster" is a reasonable
hypothesis and NOT an inference from the existing measurement. The rig decides it.

md_main.c IS MOSTLY DEAD UNDER THE PORT/R60 BUILD, AND IT HAS NOW COST A SESSION IN BOTH
DIRECTIONS. The 68K that actually runs is md_src/r60_shim.h; md_main.c's k1/k2 block is
not compiled (see the port-68K note). Both mistakes this makes possible have now been
made for real:
  READING a contract out of the dead path -- 2026-10 earlier, the art contract was taken
  from md_main.c:2326-2340, which sits inside `#else /* legacy staged consume */`. The
  tell was VRAM changing by 0 bytes while 555 batches counted "consumed".
  WRITING a change into the dead path -- 2026-10-15, commit 2a9f6347. Packet A was moved
  from 0x11A00 to 0x1E000 and TEN 68K constants in md_main.c were rewritten, but
  **r60_shim.h:77 still said md_consume(0x851A00uL)** while the SH-2 side
  (r60_main.c:730) published at 0x1E000. The live 68K consumed ZERO A packets, the
  0xB6B6 magic sat unread in both banks, name table A had 0 cells, the FG plane never
  drew, and the eye scene -- whose skin IS the FG -- read luma 44-75 against 9-14.
  Reverted in d817b767.
THE RULE: a 68K-side address or protocol change is made in r60_shim.h FIRST, and a grep
for the old literal has to come back clean across BOTH md_src and sh_src before the build
is measured. Ten correct edits in a file nobody compiles look exactly like a finished job.
THE GATE IS ONE READ, NOT AN OVERNIGHT. The 68K consume census at 0xFFA076 distinguishes
these in a single frame (f1268: good build 1022, broken build 0). Parity found this one
overnight and an ares bisect was needed to localise it; the census would have failed the
build on the first probe run. Any packet-address or packet-format change gates on the
census before anything else is read.

AN EXCHANGE RATE, MEASURED 2026-10-16: THE LARGEST PARITY GAIN WE HAVE EVER PRODUCED LOST
TO SEVEN POINTS OF CADENCE, ON MIKE'S EYE. The 32X fill (R60FILL32, sw109) moved the boot
card row from 55 through lag 90 -- i.e. never matching the arcade inside the window at
all -- to collapsing at lag 20-30, and brought first content ~60 frames earlier at every
cut. It cost 0.72 flips/vint against 0.79. Mike's first pass chose the OTHER rom: "sw108
is the more polished rom, with cleaner transitions ... and consistent sprite movement".
The record already says his pass overrules the metrics; this is the size of the override.
A row crossing from never-matching to matching did not outweigh motion that is 9% less
even. CONSISTENT MOTION IS WORTH MORE THAN ANY SINGLE-SCENE PARITY WIN, and a card that
buys parity with cadence should be costed on that basis before it is built, not after.

A FAILED PING TO A RIG IS NOT EVIDENCE THE RIG IS DOWN. CHECK THE SUBNET FIRST: ONE
COMMAND. 2026-10-16, and it cost Mike most of a working morning. Both rigs failed to
ping, I reported them as down, wrote it into STATE, told the advisor "nothing runs until
it answers", and treated the board gate as blocked. We were simply not on the home LAN.
    ipconfig getifaddr en0
HOME IS 10.0.4.x. Anything else -- this machine read 10.11.241.233 -- means the rigs are
not reachable from HERE and says nothing whatever about the rigs. The distinction is not
pedantic: "the rig is down" stops work and waits for Mike, "we are off the LAN" means
carry on with ares and BlastEm and queue the board items. The first is a blocker nobody
can clear from the keyboard; the second is a location.
AND THE BOARD-ONLY CLAIM WAS WRONG TOO, which is what made the stall expensive. The
question held for the rig was whether hardware charges the 32X plane fill less than ares.
BLASTEM_FB_WAIT answers it from here: 3 is ares's model, 11 is the RIG'S measured blit
rate, held on three spans. Before parking anything as board-only, check whether the
scoped third instrument already covers it.

OUR INSTRUMENTS MEASURE SINGLE FRAMES AND AGGREGATE RATES. MIKE JUDGES INTER-FRAME
CONSISTENCY. NOTHING WE OWN MEASURES THAT, AND IT HAS NOW DECIDED TWO BUILDS IN A ROW IN
OPPOSITE DIRECTIONS. 2026-10-16.
  sw109 (the 32X plane fill) was REJECTED for "consistent sprite movement" after
  producing the largest parity gain in the project's history -- a scene going from never
  matching inside the window to matching at lag 20-30.
  sw108 was PROMOTED for "cleaner presentation, less tile flicker" while attract_parity
  and the consume census read it EQUAL to sw81 and the soak read it ONE POINT WORSE. The
  builder had recorded it in STATE as "not a line candidate on its own merits"; the eye
  overruled that, correctly.
WHAT WE HAVE: attract_parity (one frame against one arcade frame), the soak (flips per
vint, an average), the censuses (counts at a frame), R60_PH (time inside one pass).
Every one of them is a still or a mean. FLICKER AND MOTION EVENNESS ARE NEITHER -- they
live in the DIFFERENCE BETWEEN CONSECUTIVE PRESENTED FRAMES, and no tool in tools/
reports that.
SO: when a card's payoff or cost is temporal, STOP PREDICTING IT FROM THE NUMBERS. Say
in the handover that the instruments cannot see it and that the pass decides. Two
sessions have now been spent reasoning about builds on metrics that were structurally
blind to the thing being judged.
THE INSTRUMENT THAT WOULD CLOSE THIS is a per-pixel change rate between CONSECUTIVE
presented frames (how many pixels flip, and how evenly the flip count is distributed
over time), not another comparison against the arcade. It does not exist. Writing it is
a card.

S16B SPRITE WORD 4 IS 6 BITS OF PALETTE, 2 OF PRIORITY, 4 OF BANK -- AND OUR RENDERER
READS ONLY 4 OF THE 6 PALETTE BITS. Derived 2026-10-16 from the authoritative RTL,
srcref/jtcores/cores/s16/hdl/jts16_obj_scan.v:183-186, the MODEL (S16B) arm:
    pal  <= tbl_dout[5:0];      6 BITS, 64 colour sets
    prio <= tbl_dout[7:6];
    bank <= tbl_dout[11:8];
sh_src/r60/r60_main.c's r60_sprite takes `base = (e[4] & 0x0F) << 4` -- FOUR bits, 16
sets of 16 pens, because the 32X framebuffer is 8-bit indexed and 256 entries is all
there is. So sets 16-63 ALIAS onto 0-15.
LATENT, NOT ACTIVE, FOR THIS TITLE: no object in either sampled scene uses a palette
above 15 (cave byte 0x0A values 00 01 02 04 05 06 09 0A 0C 0D; demo A 0x118 values 00
01 02 03 04 06 07 08 09). So it is not the cause of any current defect. IT IS A TOOLKIT
PROBLEM, THOUGH: the next S16B title that uses more than 16 sprite colour sets will
alias silently, and the symptom will be wrong colours on some sprites with no crash and
nothing in any counter. Check a title's palette range before porting it.
AND IT RENAMES A FIELD WE HAD UNLABELLED. The staging store
`puVar9[9] = unaff_A6[10] | unaff_A6[9] << 6` is palette | priority<<6: object byte
0x0A is the 6-bit PALETTE and byte 0x09 is the 2-bit PRIORITY. So "bits 6-7 are correct
and only the low nibble is wrong" means THE PRIORITY LANDS AND THE PALETTE DOES NOT,
which is a sharper statement of the same measurement.

THE HIGH-SCORE PALETTE SHIFT IS ONE CONTIGUOUS 40-WORD BLOCK READ TWO BYTES EARLY, AND
THE OBVIOUS WRITER IS PROVABLY INNOCENT. 2026-10-16, measured at our frame 2600 against
the arcade's 2640 (PAL_SH 0x06027000 / palette RAM 0x840000).
THE SHAPE, sharper than "pals 1-5 are each one word off": entries 8..47 are shifted RIGHT
by exactly one word, ours[k] = arcade[k-1]. Pal 0 is identical, pal 6 and 7 are identical,
and ENTRY 48 IS CORRECT -- which bounds the block at 40 words, 8..47. It is one copy gone
wrong, not five.
THE INTRUDER NAMES THE CAUSE: our entry 8 holds 0x9801, which appears NOWHERE in the
arcade's palette, and ROM 0x46E0 -- the word immediately before the palette block at
0x46E2 -- IS 0x9801. Our pal1 is byte-exact ROM[0x46E0..0x46EF]; the arcade's is
ROM[0x46E2..0x46F1]. So the data was read one word early, not written one word late.
AND YET THE ONLY WRITER IS CORRECT AT EVERY LEVEL. FUN_00004540 (68K 0x4540) is the sole
port site targeting palette entry 8:
  68K        lea pc@(0x46e2),a0 / lea 0x840010,a1 / movew #19,d0 / movel (a0)+,(a1)+ / dbf
  C          altbeast_port.c:7443-7451, same source, same dest, same 20 longwords
  symbols    _DAT_000046e2 -> 0x023446E2 (ROM_BASE 0x02340000 + 0x46E2), and
             _DAT_00840010 -> 0x06027010, which IS PAL_SH + 0x10 = entry 8
  cart data  the bytes AT 0x023446E2 in the built rom are 0000 0FFF 0DDD ... (correct)
  COMPILED   sh-elf-objdump of _FUN_00004540: `mov.l @r3+` with r3 = 0x23446e2, dest
             r2 = 0x6027010, `mov #20,r1`. Right source, right dest, right count.
ALSO EXCLUDED: the renderer never writes PAL_SH, it only reads it (r60_main.c:295). And
the palette queue drain matches its 68K exactly -- 7 longwords an entry, 68K 0x2DBC
`moveal (a2)+,a1 / moveal (a2)+,a0 / movel (a0)+,(a1)+ x6 / movel (a0),(a1)`.
WHERE IT APPEARS: between our frames 1800 and 2400, i.e. the high-score load. Before
1800 pal1 reads 0000 000F 30CF FFFF...
WHAT IS LEFT, for whoever picks this up: the palette queue's ENQUEUE side. The drain
takes its destination from the queue (`moveal (a2)+,a1`), so an enqueuer that writes
0x840012 where it means 0x840010 produces exactly this and the drain is blameless. Note
the 68K has literal 0x840012 references at 0x1A960 and 0x1B112, eight bytes after its
0x840010 ones. Start there, and do not re-walk FUN_00004540.

THREE MEASUREMENTS FROM THE 2026-10-16 OVERNIGHT (advisor, ares, advisor-bisect):

AN FB LONGWORD STORE COSTS ~18 CYCLES ON ares: 4,688 longwords measured at 59 68K lines.
So the row clear and the sprite plot are STORE-BOUND, not instruction-bound, and a card
that removes arithmetic from either buys nothing. Count stores first.

THE SLAVE WINDOW IS VBLANK-SYNCHRONOUS, not free-running: the pass starts after the
68K's span, so there is a HARD ONE-VINT BUDGET and a pass that misses it falls to every
second vint whatever its average says. Rank passes against the budget, not against each
other.
THE BUDGET IS NOT A CONSTANT -- IT IS WHATEVER THE 68K LEAVES. Measured 2026-10-16 at
vint+20, giving ~240 lines. Measured again 2026-10-17 after R60EMPTYPKT moved the slave
ring's SPANCL from vint+20 to VINT+3: ~259 of the 262-line frame. Any budget number in
this record is only valid for the 68K span that was current when it was taken; re-read
the span before using one.

tools/temporal_metric.py IS A GATE, NOT A DIAGNOSTIC. It alone caught 30 Hz ghosts from
un-cleared zoomed rows -- flicker 90k against 8k (sw138/139) -- which luma and
attract_parity never saw at all. Run it on every candidate.
AND ON A SCROLLING SPAN READ ITS FLICKER AGAINST THE ARCADE'S OWN NUMBER AT THAT SPAN
(demo 19.8k, wolf 27.8k): a period-2 texture scrolling at 60 Hz aliases as a revert, so
a high flicker count there is the CONTENT, not a defect. The arcade column is not a
nicety on these spans, it is the only thing that makes the number mean anything.

THE FORCED TEXT CLEAR SKIPPED PENDING BLANK CELLS, AND IT WAS ON EVERY BUILD INCLUDING
THE LINE. 2026-10-17, fixed in sw147 (fa4ab4da). When "2 CREDITS 1 PLAY" gave way to
"INSERT COIN" the trailing glyphs of the longer string stayed in BOTH banks, so the title
read "2 CINSERT COINAY". Title luma against the arcade at frame 300: 57 -> 19.4.
HOW IT WAS FOUND IS THE REUSABLE PART. It was not found by looking at the title. It fell
out of a span check demanded of a SPEED candidate: sw146 raised the presented-frame rate
everywhere by construction, so the title span had to be re-read against the arcade (which
presents 6 distinct frames in 59 there) to prove the candidate had not carried the title
up with it. The candidate was fine -- 58/59 on the LINE against 31/59 on the candidate --
and the check instead exposed that most of the line's own count was a 178-pixel
per-frame text alternation that had been there all along.
THE RULE: when a change raises a rate everywhere, re-read the span where the arcade's
rate is LOW. That is where a regression hides, and it is also where a standing defect
shows up, because nothing else on a nearly-static screen is moving to mask it.


THE 68K PAID 13 LINES A FRAME TO CONSUME PACKETS THAT CONTAINED NOTHING. Measured
2026-10-17 by V-stamping the shim's vint (R60SPANPROBE in r60_shim.h): entry V225,
before A 226, AFTER A 234, after B 239, DONE 239. An EMPTY packet A -- seven rows of
nothing, no art -- still cost md_consume EIGHT 68K lines, and an empty B five, EVERY
FRAME. The DMA runs whether or not the packet says anything.
FIXED by having the master flag an empty packet in the header (bit 12; for B also
palette and scroll unchanged) and the shim take it with the hold bit, the consumed mark
and the census only (R60EMPTYPKT, 90d843ae). The slave ring's span close moved vint+20
-> vint+3 and the demo went 71 -> 74 passes per 100 vints.
THE GENERAL FORM: a transport that always pays its fixed cost is worth auditing for the
EMPTY case before it is worth optimising for the full one. Nothing in any counter said
"these packets are empty" -- the census counted them as consumed, which is what they
were.
AND WHAT IS LEFT ON THE HEAVY FRAMES IS STORES, NOT WORK. The remaining two-vint passes
break down as clear 32 + sprites ~210 + 20 against the 262-line frame, so the sprite
plot alone is four fifths of it, and the plot is STORE-BOUND at ~18 cycles an FB
longword. The lever on a heavy frame is therefore FEWER STORES -- fewer sprite pixels
touched, or fewer stores per pixel -- and not cheaper arithmetic, which the SH-2's
missing barrel shift has already punished twice.

**A 68K `movel` FROM A 2-MOD-4 ADDRESS IS LEGAL ON THE 68000 AND SILENTLY WRONG ON THE
SH-2. THIS IS A DEFECT CLASS, NOT A BUG.** Found 2026-10-17 as the cause of the
high-score palette shift, after the obvious writer had been cleared five ways.
THE 68000 allows a longword access at ANY EVEN address. THE SH-2 requires 4-byte
alignment for `mov.l`, and a 2-mod-4 source reads THE ALIGNED LONGWORD BELOW -- i.e. one
16-bit word early. The decompiler turns every 68K `movel` into an `undefined4 *` copy,
so any such site inherits the fault with no warning from anything.
THE WORKED CASE, FUN_00004540 (68K 0x4540), the attract title step's palette load:
    68K     lea pc@(0x46e2),a0 / lea 0x840010,a1 / movew #19,d0 / movel (a0)+,(a1)+ / dbf
    dest    _DAT_00840010 -> 0x06027010,  mod 4 = 0, FINE
    source  _DAT_000046e2 -> 0x023446E2,  MOD 4 = 2, BROKEN
    result  the SH-2 reads 0x023446E0, so all 40 words arrive one word early
MEASURED, not inferred: the palette footprint at the frame of the scene boundary
(2073 -> 2074, tick 0x254 -> 0x0B4) is a contiguous run of EXACTLY entries 8..47, right
destination and right length, with entry 8 holding ROM[0x46E0] = 0x9801 and 9..47 holding
ROM[0x46E2..]. Mike sees it as garbled scoreboard glyphs.
WHY IT SURVIVED SO LONG: every check that can pass, passes. The decompiled C matches the
68K instruction for instruction; the symbols resolve to the right addresses; the cart
bytes at the source symbol are correct; the compiled SH-2 loads the right literal. The
fault is in the ADDRESS'S LOW TWO BITS and nothing in the pipeline looks at them.
THE SWEEP THIS IMPLIES: tools/cport_widths.py ranks declared-type against real 68K access
WIDTH. It does not check ALIGNMENT. Every `undefined4 *` / `ST32` / `LD32U` site whose
68K address is 2 mod 4 is a candidate, and the port has many. That audit is a tool, not a
grep, and it is a TOOLKIT-level item: every S16 title ported this way inherits the class.

**A 68K `bpl`/`clr` CLAMP BECOMES DEAD CODE WHEN THE DECOMPILER DECLARES THE BYTE
UNSIGNED. FIVE SITES, AND THE COMPILER ALREADY WARNS ABOUT EVERY ONE.** Found 2026-10-17
as the cause of the boot card snapping back to full colour after its fade.
THE 68K clamps a counter at zero with the N flag, i.e. a SIGNED read of the byte:
    25c8: subq.b #1,0xfffff02d
    25cc: bpl.s  0x25d2
    25ce: clr.b  0xfffff02d
THE PORT renders that faithfully in shape and fatally in type:
    DAT_fffff02d = DAT_fffff02d + -1;
    if (DAT_fffff02d < '\0') { DAT_fffff02d = '\0'; }     <-- ALWAYS FALSE
because dat_syms.h:486 declares `extern byte DAT_fffff02d` -- UNSIGNED. The clamp never
fires, the level wraps 0 -> 0xFF, the fade restarts at full brightness and rewrites the
card's ORIGINAL colours over the faded ones. Measured in PAL_SH: entries 912+ go
0A60 0A60 0A60... back to 0FB0 0D90 0C80..., between our frames 181 and 185, where the
arcade holds the faded values until the title load.
EVERY SITE, from a scan of the GENERATED unit against the GENERATED dat_syms.h:
    altbeast_port.c:3514   DAT_fffff02d   (the boot-card fade -- the visible one)
    altbeast_port.c:598    DAT_fffff15d
    altbeast_port.c:824    DAT_fffff15d
    altbeast_port.c:1553   DAT_fffff15d
    altbeast_port.c:2104   DAT_fffff15d
THE FAITHFUL FORM is a signed test on the byte: `if (x & 0x80) x = 0;` or
`if ((signed char)x < 0) x = 0;`. Do not "fix" it by clamping before the decrement --
the 68K decrements first and the wrapped value is observable in between.
**AND THIS CLASS IS COMPILER-DETECTABLE, WHICH IS THE PART WORTH KEEPING.** -Wextra
enables -Wtype-limits, so gcc is ALREADY emitting "comparison is always false due to
limited range of data type" for all five, on every build, and nothing reads the
warnings. A port that greps its own build log for -Wtype-limits finds this class for
free, in any title, before it ever reaches a screen. That is a TOOLKIT item, not an
Altered Beast one.
SISTER CLASS, same day, same root: the ALIGNMENT entry above (a 68K movel from a 2-mod-4
address). Both are the decompiler producing C that is structurally right and
type-wrong, and both are silent.

**A 68K bpl/clr CLAMP BECOMES DEAD CODE WHEN THE DECOMPILER DECLARES THE BYTE UNSIGNED.
FIVE SITES, AND THE COMPILER ALREADY WARNS ABOUT EVERY ONE.** Found 2026-10-17 as the
cause of the boot card snapping back to full colour after its fade.
THE 68K clamps a counter at zero with the N flag, i.e. a SIGNED read of the byte:
    25c8: subq.b #1,0xfffff02d
    25cc: bpl.s  0x25d2
    25ce: clr.b  0xfffff02d
THE PORT renders that faithfully in shape and fatally in type:
    DAT_fffff02d = DAT_fffff02d + -1;
    if (DAT_fffff02d < 0) { DAT_fffff02d = 0; }      <-- ALWAYS FALSE
because dat_syms.h:486 declares "extern byte DAT_fffff02d" -- UNSIGNED. The clamp never
fires, the level wraps 0 -> 0xFF, the fade restarts at full brightness and rewrites the
card's ORIGINAL colours over the faded ones. Measured in PAL_SH: entries 912+ go
0A60 0A60 0A60 back to 0FB0 0D90 0C80, between our frames 181 and 185, where the arcade
holds the faded values until the title load.
EVERY SITE, scanning the GENERATED unit against the GENERATED dat_syms.h:
    altbeast_port.c:3514   DAT_fffff02d   (the boot-card fade -- the visible one)
    altbeast_port.c:598    DAT_fffff15d
    altbeast_port.c:824    DAT_fffff15d
    altbeast_port.c:1553   DAT_fffff15d
    altbeast_port.c:2104   DAT_fffff15d
THE FAITHFUL FORM is a signed test on the byte: "if (x & 0x80) x = 0;" or
"if ((signed char)x < 0) x = 0;". Do NOT clamp before the decrement -- the 68K
decrements first and the wrapped value is observable in between.
**AND THIS CLASS IS COMPILER-DETECTABLE, WHICH IS THE PART WORTH KEEPING.** -Wextra
enables -Wtype-limits, so gcc is ALREADY emitting "comparison is always false due to
limited range of data type" for all five, on every build, and nothing reads the
warnings. A port that greps its own build log for -Wtype-limits finds this class for
free, in any title, before it reaches a screen. That is a TOOLKIT item, not an Altered
Beast one.
SISTER CLASS, same day, same root: the ALIGNMENT entry above, a 68K movel from a
2-mod-4 address. Both are the decompiler producing C that is structurally right and
type-wrong, and both are silent.

THE ALIGNMENT CLASS IS SEVEN SITES ACROSS AT LEAST TWO FUNCTIONS, SO IT IS A PASS AND
NOT A HAND FILE. Swept 2026-10-17 against the GENERATED unit and the GENERATED symbol
addresses (nm on build/port/rom/s16.elf), for symbols reached by a 32-bit access whose
linked address is 2 mod 4:
    DAT_000046e2  0x023446E2   FUN_00004540    the boot/title palette load (PROVEN BAD)
    DAT_0001c302  0x0235C302   FUN_0001a52c
    DAT_0001c272  0x0235C272   FUN_0001a52c
    DAT_0001d2ea  0x0235D2EA
    DAT_0000f556  0x0234F556
    DAT_0001c0f2  0x0235C0F2
    DAT_0001c622  0x0235C622
TWO SCANS WERE NEEDED AND THE FIRST ONE MISSED THE KNOWN SITE -- worth knowing before
anyone trusts a single sweep. An inline-cast scan (`(undefined4 *)&SYM`, `ST32`,
`LD32U`) finds five of them and NOT FUN_00004540, because that function assigns the
symbol to a pointer declared earlier (`puVar3 = &DAT_000046e2;`). A declaration-tracking
scan finds FUN_00004540 and two others. The union is seven. A sweep for this class has
to follow pointer declarations, not just casts.
THE FIX SHAPE: where a 32-bit access targets a 2-mod-4 address, emit TWO 16-BIT
accesses. That is byte-identical to what the 68000 does and is safe whether or not the
original was really a movel. It is a mechanical rewrite across functions, which by the
port's own rule (CLAUDE.md) belongs in cport_prep as a PASS; a hand file is for one
function.
NOT YET BUILT. Only FUN_00004540 is proven to misbehave; the other six are candidates
by address arithmetic alone and each wants its 68K checked before a pass is trusted to
rewrite it.

FRAMEBUFFER STORES ARE DROPPED WHILE THE VDP FILL ENGINE IS RUNNING. Advisor, 2026-10-17
(sw154, 75925c63): a short span's CPU clear issued during an auto-fill lost its stores,
and that was the uncleared sprite rows. The fix is to WAIT FOR THE FILL ENGINE before a
CPU clear. This is the same FEN gate the RTL describes (VDP.sv:411, FEN = FILL_EXEC |
RFRH): a fill or a refresh blocks the write, and nothing reports the loss.


A BUDGET THAT FORGOT ONE RECORD LET A LATER WRITE EAT THE LAST ART RECORD OF EVERY FULL
WINDOW. Advisor, 2026-10-17, fixed in sw166 (8b987bc6). r60_planes.c's r60_art_fit sized
packet B's art budget without counting the BLANK record, so a full tail (count word +
blank + 25 art records) ran 17 WORDS PAST 688. The palette block is written after the
tail, so on every full window it overwrote the last art record. The 68K then read that
record as "slot 0" with palette words where art should be, and the real slot's cells
stayed BLACK until some later window happened to re-ship them.
WHY IT WAS HARD TO SEE, and why it is worth a line: the symptom appeared far from the
cause and intermittently. A slot could have its tag set and its dirty flag clear and
still never appear in any published tail (slot 895 at the title cut). It predates
everything added on 2026-10-16/17, SO SOME OLDER "BLACK CELLS AT A CUT" SIGHTINGS WERE
THIS and were attributed elsewhere at the time. When a buffer overruns into whatever is
written next, the corruption lands on the NEXT writer's data, not the overrunning one's,
and the search starts in the wrong file.

THE GREEN LIGHTNING IS ONE BYTE, AND IT IS WRONG FOR THE BOLT'S WHOLE LIFETIME. Measured
2026-10-17 on sw166, the cave dragon, our tick 0x00D1 matched to the arcade's frame 2910
(same tick) -- objects n9-n12 at 0xFFC000 + n*0x80 are the four bolt records.
    live flag     arcade 88/89/8A/8B   ours 88/89/8A/8B   SAME
    byte 0x0A     arcade 09            ours 00            THE DEFECT
    16 bytes differ in each record and FIFTEEN OF THEM ARE SMALL POSITIONAL DELTAS --
    0x07 80/7F, 0x0D A0/A4, 0x11 54/57, 0x21 02/01, 0x23 06/07, 0x35 0x37 0x39 0x3B
    0x43 0x47 0x5C-0x5F all +-1 to +-4. That is the bolt mid-flight one step apart,
    i.e. the TIMING LEAD. Byte 0x0A is the only CATEGORICAL difference.
IT IS NEVER RIGHT, NOT EVEN BRIEFLY: sampled across the bolt's whole life (live at our
2870/2876/2882, dead by 2890) byte 0x0A reads 00 at every sample. So it is set wrong at
spawn, or never set, and nothing corrects it. Set 0 is the yellow dragon's own palette,
which is why the bolt draws greenish.
WHAT THIS RULES OUT. The per-object palette table at 0x6C-0x6F is ALL ZEROS ON BOTH
MACHINES, so any routine of the shape `A6[10] = A6[0x6c + (timer & 3)]`
(altbeast_port.c:19893, FUN_00005cfe) would write 0 on the arcade too -- the arcade has
9, so that routine is not how the arcade sets it. And the same routine also writes the
ZOOM byte 0x4E, which MATCHES on both sides (00), so it did not run on these objects in
our port either. Do not start there.
WHAT IS LEFT: find what writes 9 into byte 0x0A on the arcade -- most likely a spawn
template copy, since the value is set once and never touched again. If the template is
read through a 32-bit access at a 2-mod-4 ROM address, this is the ALIGNMENT CLASS
again and byte 0x0A would receive the template's byte 0x08; that is a hypothesis with a
cheap test and it is NOT yet checked.
SEPARATELY STILL OPEN, the P2 bolt: the arcade carries bank 4 / pal 10 records (addr
31f0/35b3/3a09, x 210-227) at every sample 2920-3060 and WE NEVER CREATE ONE anywhere in
2700-3400; instead we carry small bank-0 pal-9 records (addr d9da, 6 rows, x 272-320)
that the arcade never has. Different object or a mangled one; unknown which.

**THE ARCADE GIVES ITSELF 21 FRAMES OF BLANK AT A SCENE LOAD AND WE TAKE 4. FIXING THAT
PORT DEFECT HANDS THE RENDERER THE COVER IT HAS SPENT A MONTH TRYING TO EARN.**
Measured 2026-10-17 (advisor, sw180-sw184) by reading VIDEO_EN at 0x2602C801 bit 5 per
frame against the arc6k corpus' black runs:
    title -> demo    arcade black 444-464   = 21 frames | ours ven=0 at 450, 1 by 454 = 4
    eye -> demo 2    arcade black 1478-1498 = 21        | ours 1484-1488, 1 at 1490 = 4-6
    face -> eye      arcade black 1165-1168 = 4         | ours 1168..1172         = 4  SAME
The two 21-frame blanks are the game's SCENE LOAD. Ours is 4 because the 68K's load loop
is WORK-PACED, not frame-paced, and the work runs ~5x faster on the SH-2 than on a
10 MHz 68000. face->eye matches because it is not a load.
THIS IS THE SAME DEFECT as the load-pacing entry (our scene timer counts 44 ticks through
a load the arcade holds parked for ~16 frames) seen from the display side instead of the
timer side, and it is the same root as the ~10-frames-a-boundary lead.
**WHY IT IS THE HIGHEST-VALUE PORT ITEM AND NOT JUST A FIDELITY ONE.** Everything the
renderer draws after the enable is visible catch-up; on the arcade the picture is already
complete at enable+0 because the load took 21 frames behind a blank screen. The current
r60 fill needs ~25 frames after enable. PACE THE LOAD AT THE ARCADE'S RATE AND THE
RENDERER GAINS 17 FRAMES OF FREE COVER AT BOTH CUTS -- which is most of what a month of
cut-lag work has been trying to buy by making the renderer faster. The cut is not
primarily a renderer problem. It is a port problem wearing a renderer's clothes.

THE RENDERER LOSES GAME TICKS WHEN A PASS OVERRUNS A FRAME, AND THE TWO CLOCK ERRORS RUN
IN OPPOSITE DIRECTIONS. Advisor, 2026-10-17: pp_due is SET TO 1 per vint rather than
INCREMENTED, so a master pass longer than one frame silently drops a game tick. The fat
direct builds under the hold are ~1.3 frames each. R60_DIAG[11] (game ticks) against
R60_DIAG[0] (vints) at frame 1560: sw166 1537/1544, seven lost; sw184 1522/1544,
TWENTY-TWO lost. Under HOLDDIRECT the game timeline slips ~13 extra frames per attract
loop.
SO THE PORT RUNS THE GAME TOO FAST AT A LOAD (above) AND THE RENDERER RUNS IT TOO SLOW
WHEN A PASS OVERRUNS. Both corrupt the game timeline, neither is in any gate, and they
partly mask each other in any end-to-end number -- which is exactly why both were found
by instruments aimed at something else.

UNDER PORT_BOOT THE PORT HAD NO INPUT PATH AT ALL, AND NOTHING SAID SO. Advisor,
2026-10-18, found while wiring live control (R60LIVEPAD): port_play_tick never called
l2m_io_apply under PORT_BOOT, so the nine I/O cells were never fed from anything --
not the capture blob, not a pad. The attract ran regardless, because the attract does
not read a control, which is exactly why a whole month of attract work never noticed.
A FEATURE NOTHING EXERCISES IS NOT TESTED BY A GREEN GATE; check-demo passes object
records and never presses a button.
THE LIVE PATH, for the record: the 68K shim posts COMM2 = P1<<8|P2 (md_to_arcade
bytes) and COMM8 = 0xA5<<8|service every vint; port_l2.c's l2m_io_live() applies them
to DAT_00c41003 and DAT_00c41007 (the two joysticks) and DAT_00c41001 (the system
byte), and the i8751 block posts pressed bits in MCU_COINS, which is where the game
actually reads coins and starts (altbeast_port.c:4072).
THAT MATCHES THE RTL INDEPENDENTLY. Derived the same day from
srcref/jtcores/cores/s16b/hdl/jts16b_cabinet.v without seeing the wiring: 0xC41001 is
sys_inputs = { 2'b11, cab_1p[1:0], service, dip_test, coin[1:0] } -- coins bits 0-1,
dip_test 2, service 3, P1 start 4, P2 start 5, ACTIVE LOW -- with the joysticks at
0xC41003 and 0xC41007 and sort_joy = { joy[1:0], joy[3:2], joy[7], joy[5:4], joy[6] }.
Two derivations, one from the hardware spec and one from the running port, agreeing.
VERIFIED BY PLAYING IT: headless ares with --input (two coins, START, right, A) starts
the game and the player walks and punches. That is the first input the port has ever
taken.

THE START TRANSITION: WE NEVER LEAVE THE DEMO, AND THE ATTRACT LOOP HAS NO START TEST
IN THE 68K EITHER. 2026-10-18, from the advisor's matched play dumps (arcade START at
frame 1000, ours at 800; work RAM 0xFFC000 x 0x4000 at START-10 and START+50).
WHAT THE DIFF SAYS, and it is the opposite of what "the start does not run" suggests.
Of the 215 bytes the ARCADE changes across START and we do not, most are places where
OUR VALUE ALREADY EQUALS THE ARCADE'S POST-START VALUE and is steady:
    0xFFC002  arcade 00002490 -> 000049BA   ours 000049BA steady
    0xFFD802  arcade 00000000 -> 0000DABC   ours 0000DABC steady
    0xFFD824  arcade 00000000 -> 0000DD48   ours 0000DD48 steady
So the arcade TEARS DOWN AND REBUILDS at START and we have nothing to rebuild, because
our attract demo already has the player objects live and initialised. We are not failing
to start the game; WE NEVER EXIT THE DEMO.
THE ATTRACT LOOP DOES NOT POLL THE START BUTTON. Disassembled, 0x1F5A-0x1F78:
    1f5a: jsr 0x3aae / jsr 0x144a / jsr 0x398e      the per-frame work
    1f6c: moveq #0,d0 ; jsr 0x397e                  wait one frame
    1f74: subq.w #1,0xfffff02a
    1f78: bcc 0x1f5a                                loop until the timer borrows
    1f7a: jmp 0x1e54
There is no test of the start flag anywhere in it, and the decompiled C matches the 68K
exactly here -- this is NOT a lost-structure case. The loop ends ONLY when the scene
timer runs out.
SO THE HANDOFF IS THE TIMER ITSELF. The advisor measured 0xFFF02A RESETTING on the
arcade at START (0x24 -> 0x1F -> 0x119, a new scene) while ours keeps counting
(0x149 -> 0x185 -> 0x27F). On the arcade something forces the attract scene to END when
start is taken; the loop then falls through to 0x1E54 and 0x1E54 chooses the game.
**CORRECTED THE SAME DAY, AND THE CORRECTION IS THE ALIGNMENT RULE AGAIN.** Reading the
game's OWN state out of the same dumps, rather than diffing bytes, the two machines are
IN DIFFERENT ATTRACT STEPS when START is pressed:
    arcade  0xFFF031 = 08   breadcrumb 0xFEFE 2044 -> 0928 -> 0928
    ours    0xFFF031 = 0C   breadcrumb 0xFEFE 0A54 -> 0A5A -> 0A5A
0xFFF031 is the attract step selector (`f031 = f031 + 4 & 0x1c; switch`). Different
step, different code, and different exit conditions -- some of those loops count the
timer DOWN while ours was counting UP, which is the whole "the timer never resets"
observation. So the conclusion below is NOT established: the evidence cannot separate
"start is broken" from "start pressed in a different scene", and the state where our
value already equals the arcade's post-START value is as consistent with being further
along a different step as with never leaving the demo.
THIRD TIME THIS WEEK THE SAME MISTAKE HAS BEEN MADE IN A NEW PLACE: attract_parity's
late rows, temporal_metric's moving spans, and now a play capture -- each matched on
FRAME NUMBERS when the game's own state was the thing to match. The rule is general and
it keeps being rediscovered locally: MATCH ON THE GAME'S STATE, and for this corpus that
means 0xFFF031 and the breadcrumb, not a chosen frame.

NEXT, and do not look where I looked: not the start acceptance (altbeast_port.c:4072 is
correct -- credits drop, 0xFFF029 is set), not the attract loop (no test to lose), and
not the score digits or the tilemap pages at 0xFFF0F4 (both downstream -- two player
score fields reading 0x3F is the DEMO HUD, and the pages follow the camera, which
follows the scene). The question is exactly: WHAT ON THE ARCADE ENDS THE ATTRACT SCENE
WHEN START IS TAKEN, and it is a write to 0xFFF02A or to whatever the loop's borrow
depends on.

THE CREDIT-TO-TITLE TRANSITION IS MISSING CODE, NOT WRONG CODE: GHIDRA MADE NO FUNCTION
OVER 0x4FE-0x57E AND THE PORT HAS NO ENTRY AT 0x1E4E. Found 2026-10-18, and it is why a
coin never cuts the attract.
THE ARCADE'S PATH, disassembled:
    0x524..0x572   the credit handler: sets 0xFFF0F0, four jsr setups (0x3B08, 0x4178),
                   0xFFF094 = 0xFF, then `lea 0xffffff00,sp` -- IT RESETS THE STACK --
                   and `move.w #0x2300,sr`
    0x574          jmp 0x1E4E            the ONLY jump to that address in the program
    0x1E4E         move.b #4,0xFFF031    <-- set the step to FOUR
    0x1E54         move.b #-1,0xFFF094 ; lea 0xffffff00,sp ; ... the sequencer
    0x1EBC         addq.b #4,0xFFF031 ; andi.b #28 ; jump table at 0x26DC
    So 4 + 4 = STEP 8, which is the title. That is the whole mechanism: "go to the
    title" is spelled "enter at 0x1E4E so the step lands on 8".
WHAT THE PORT HAS INSTEAD. Its function list runs 0x0040E (240 bytes, ending 0x4FE)
straight to 0x0057E: THERE IS NO FUNCTION OVER 0x4FE-0x57E, so the credit handler was
never decompiled and nothing can call it. And FUN_00001e54 begins at 0x1E54 -- its first
statement is the 0xFFF094 write -- so the `move.b #4,0xFFF031` at 0x1E4E is not in the
port anywhere. The only `DAT_fffff031 = 4` in the whole unit is at altbeast_port.c:140,
in the boot path, unrelated.
SO THE PORT CANNOT REQUEST THE TITLE AT ALL. Its step advances normally, 0x0C -> 0x10,
and the demo runs on, which is exactly what was measured: with two coins the arcade cuts
from step 0x0C to step 0x08 within ~40 frames and we walk to 0x10 as if no coin existed.
The coin DOES reach the credit counter (Mike sees the CREDIT text change) -- the
transition that consumes it is the missing piece.
THE CLASS IS ALREADY NAMED IN THIS TREE, which is the part worth keeping: the comment on
hand/AT_FUN_00003d60.c says "Ghidra made no function here, so the dispatcher counted the
call as unknown and the trail never drew". Same cause, a far more visible symptom. A
decompiler gap is invisible to every gate we own -- check-demo diffs object records and
presses no buttons, and the attract never needed this path.
**RETRACTED BY THE BUILDER, SAME SESSION, BEFORE ANYTHING WAS BUILT. THE CODE IS ALL
THERE.** I read the function HEADER ("0x0040E FUN_0000040e (240 B)") and the gap to the
next header at 0x0057E and concluded 0x4FE-0x57E was never decompiled. The header's
byte count is Ghidra's idea of the function, not the extent of the C: the decompiler
INLINED the jump-table targets, so the C for FUN_0000040e runs to ~line 400 and contains
the whole tail. altbeast_port.c:129-141 has it exactly --
    FUN_00004178();                    the 68K's jsr 0x4178 at 0x542
    DAT_fffff094 = 0xff;               0x548
    clear_textram / clear_tileram / clear_spriteram_and_pool   0x55C/562/568
    set_level_palettes();              0x56E
    DAT_fffff031 = '';             <-- THE 0x1E4E WRITE, PRESENT AND CORRECT
    ... then the 0x1E54 body inline
So the credit-to-title path exists and is faithful, including the step-4 write I said
was absent. The "only DAT_fffff031 = 4 is in the boot path" line below is the same
error: line 140 IS this path.
WHAT IS ACTUALLY TRUE, and it is all the measurement supports: the path is NEVER
REACHED. The advisor measured f031 going 0x0C -> 0x10 with two coins and never 8, so
nothing calls into it. THE OPEN QUESTION IS THE CALLER, not the body.
TWO WRONG STRUCTURAL CALLS ON ONE DEFECT, both from reading the decompiled unit's
LAYOUT instead of measuring: first "the steps are mis-aligned" (they were in lockstep
without coins), then "the handler is missing" (it is inlined). A decompiled unit's
function headers, byte counts and gaps describe GHIDRA'S MODEL, not the program. Read
the C body, or better, instrument what runs.

THE FIX SHAPE, IF THE BODY HAD BEEN MISSING: a hand file (the project's rule is
sh_src/portfull/hand/FUN_<addr>.c, not a regex pass, for one function), ending with
DAT_fffff031 = 4 and a call to FUN_00001e54; plus whatever dispatches to 0x524, which
has to be found the same way -- by looking for the gap rather than for wrong code.

THE ATTRACT STATE MACHINE, MAPPED (2026-10-18). Written down because two wrong
diagnoses came out of guessing at this structure, and the map is cheap to read and
expensive to re-derive.
    0xFFF031 is the step selector. The sequencer at 0x1EBC does
        addq.b #4,0xFFF031 ; andi.b #28 ; jump through the table at 0x26DC
    THE TABLE AT 0x26DC:
        step 0x00 -> 0x01EF2      step 0x10 -> 0x020A0
        step 0x04 -> 0x01ED4      step 0x14 -> 0x01ED4
        step 0x08 -> 0x01F80      step 0x18 -> 0x02282
        step 0x0C -> 0x01ED4      step 0x1C -> 0x02282
    0x01ED4 IS THE GAME ENTRY, not a demo screen: it sets 0xFFF028/0xFFE018/0xFFF029/
    0xFFE098 to 1 and jmp 0x5BE (main_entry). So steps 4, 0x0C and 0x14 are the three
    DEMO PLAYS and they run the real game; 0, 8, 0x10, 0x18, 0x1C are the title, logo
    and high-score screens.
    EVERY WRITER OF 0xFFF031 IN THE PROGRAM:
        0x01E4E  move.b #4   -- reached ONLY by jmp from 0x574, which is the tail of
                 the COLD BOOT path (0x4A0: lea 0xffffff00,sp ; move.w #0x2700,sr).
                 4 + 4 = step 8 = the title, which is why boot shows the title.
        0x00BA6  move.b #28  then bra 0x1E54   (0x1C + 4 masked = step 0)
        0x00D08  move.b #28  then bra 0x1E54
        0x01EBC  the sequencer's addq #4
    THERE IS NO WRITER THAT JUMPS THE STEP TO 8 ON A CREDIT. So the arcade's measured
    cut from step 0x0C to step 0x08 within ~40 frames of the second coin is NOT a
    direct write: either the demo ABORTS and the sequencer cascades 0x0C -> 0x10 ->
    ... -> 0x08 with the intervening steps bailing out immediately, or main_entry's
    game loop returns early. THAT IS THE OPEN QUESTION.
    main_entry (0x5BE) branches on 0xFFF026 BIT 0 at 0x604: set means a credit game
    (lives = 1 via 0xFFF030), clear means the demo (lives from DIP 0xFFF01B through
    the table at 0x1844). So f026 bit 0 is the credit-vs-demo discriminator and is
    worth watching on both machines across a coin.
    MEASURED ON OUR SIDE (advisor, sw240, coins at 600/800, per-frame dumps 795-875):
    the boot tail's breadcrumbs 0x548/0x562/0x568/0x56E NEVER appear, step stays 0x0C,
    the timer counts up with no reset. Correct and expected -- that tail is boot-only.
    The port's demo simply never ends.

THE INTRO IS NOT A WRONG TIMER -- OURS IS EXACT. THE ARCADE EXITS EARLY. 2026-10-19,
after the IRQ-redirect fix made START work.
TWO READINGS CORRECTED, both cheap to get wrong:
  (a) THE BREADCRUMB DIFFERENCE IS SAMPLING PHASE, NOT DIVERGENCE. 0xFFFEFE reading
      0x0920 on ours and 0x0928 on the arcade looks like two different places. It is
      ONE LOOP, 0x90A-0x928:
          90e: jsr 0x3AAE
          914: subq.w #1,0xFFF14C
          918: beq 0x92A           the only exit
          91a: jsr 0x398E          object_dispatcher   -> breadcrumb 0x0920
          922: jsr 0x397E          wait_frame_flag     -> breadcrumb 0x0928
          928: bra 0x90A
      0x0920 and 0x0928 are the RETURN ADDRESSES of two consecutive calls in that
      loop. Both machines are in the intro; they were sampled at different points of
      the same iteration.
  (b) OUR INTRO COUNTDOWN IS CORRECT. Measured on irqred.32x with coins and START:
      0xFFF14C reads 0x02B6 at frame 949 and 0x0090 at 1499 -- it is set to 0x2D0
      (720) exactly as the arcade sets it (the arcade read 0x02B1 at its START+50),
      and it decrements EXACTLY ONCE PER FRAME (694 -> 144 over 550 frames). The
      choice between 120 and 720 is also faithful: the port's
      `(_DAT_fffff14e == 0) && ((DAT_fffff142 | DAT_fffff143) == 0)` is the 68K's
      two `tst.w` at 0x746/0x74C, byte-OR standing in for the word test.
SO THE OPEN QUESTION IS THE ARCADE'S EARLY EXIT. Its 720 would expire around frame
1739 and it is in gameplay (breadcrumb 0x0AE2) by 1600, so it leaves the loop before
the countdown ends. The loop's ONLY exit is the `beq` at 0x918, so something must
zero 0xFFF14C -- or take another fake-return redirect out of the handler, which is
the same class we just fixed and would be the first place I look.
DO NOT chase the timer value, the 120-vs-720 choice, or the breadcrumb: all three are
measured correct.

THE LIGHTNING PALETTE: WHERE ITS DATA IS, AND WHY A STATIC SEARCH WILL NOT FIND THE
LOADER. 2026-10-19. Sprite sets 148 and 149 read ALL ZERO on ours and hold the bolt's
white-to-yellow-to-green ramp on the arcade, which is why the bolt draws as a
white/blue strip.
    THE DATA IS AT ROM 0x247E0 (prog68k.bin), sixteen words:
        0777 7FFF 4FFF 4FEE 4FDD 4FCC 7FAA 4F99 7F77 7F66 2BF0 49E0 28C0 46B0 2590 7DEF
    which is sets 148-149 (68K palette 0x840940..0x84095F) with the arcade's first
    entry forced to 0000.
    IT IS NOT REACHED BY ANY DIRECT REFERENCE. Searched prog68k.bin for the absolute
    longwords 0x247DE and 0x247E0: zero hits. Searched every pc-relative lea/pea in
    the program reaching either: zero hits. Searched for a POINTER TABLE -- any
    longword in the image pointing into 0x24700-0x24900: zero hits. So the address is
    COMPUTED at runtime (a base plus an index), and base + set*16 would put the base
    at 0x23EA0, which is also never referenced. 0x24000 has 13 absolute references
    and is the most likely base, but that is inference, not evidence.
    THE RIGHT INSTRUMENT IS A WRITE WATCHPOINT, NOT A SEARCH: in MAME, watch 68K
    writes to 0x840940 and the handler names the loader in one run. The same debugger
    harness that found the IRQ fake-returns (b1/start/trace.lua) does this directly.
    Three wrong structural calls were made earlier in this same session by inferring
    from static layout; this is where to stop inferring.
    RELATED AND PROBABLY THE SAME LOADER: sets 38, 40 and 42 have only their FIRST
    LONGWORD wrong, carrying what look like entries of sets 19/20/21 -- and 38 = 19*2,
    40 = 20*2, 42 = 21*2, so a destination computed at TWICE the intended set index
    fits every one of them. That is a width or shift error in the same address
    arithmetic, and the same watchpoint would catch it.

THE MISSING PLAYER SPRITE IS SLOT 0, LIVE BUT NEVER DRAWN. 2026-10-19, on spawn2.32x
(the +0x3C fix) with coins and START, frame 1599.
    OUR LIVE OBJECTS:  slot 0 flag 80 handler 49BA idx 00 cset 02
                       slots 16-22 flag 88 ...        slot 40 flag 80 handler 6BF0
    SLOT 0 IS THE PLAYER and its cset 02 matches the sprite record the arcade has and we
    do not (arcade entry 0, word 4 = F082 -> bank 0, cset 2). Everything else in the list
    matches the arcade one frame apart.
    THE FLAG IS THE TELL: every drawn object reads 0x88 and the player reads 0x80. BIT 3
    IS "DREW THIS FRAME", set by sprite_build_and_cull at LAB_00003EB2
    (`*unaff_A6 = *unaff_A6 | 8`). So the player is alive and its draw never runs -- this
    is not the renderer dropping it, and the 32X framebuffer has no player pixels either.
    TWO FIELDS WORTH THE NEXT LOOK, both measurable against the arcade in one dump:
      +0x6C = 0x0000.  The trail's draw is reached by `jmp (a0)` with A0 taken from +0x6C
      (hand/AT_FUN_00003d60.c's note). A null there means no draw target at all.
      +0x3C = 0xE000.  We JUST fixed FUN_00005776 storing an SH-2 pointer's low word into
      this same field (0x0602E400 -> 0xE400 instead of 0xD800). 0xE000 has exactly that
      shape, so a SECOND site may truncate the same way. The hand file for 0x5776 already
      asks for a census of "a pointer parameter normalised at entry and later truncated to
      a word store into a 68K field"; this would be its first customer.
    NOT the dispatch table: handler 0x49BA is present in objdispatch.c.
    DO NOT re-check the intro player at 0xFFDE00. It is dead at 1599 BY DESIGN -- 0x5854
    ends with `clr.w (a6)` after setting 0xFFF14C = 30, so the intro placeholder despawns
    once it has cut the intro. I spent a measurement there before noticing.

ZEUS'S INTRO LINE: THE TEXT OBJECT NEVER SPAWNS, AND THE SPAWN CODE IS FAITHFUL.
2026-10-19. Our text rows 9/10 are blank where the arcade shows "I COMMAND YOU TO RISE
FROM YOUR GRAVE...".
    THE STRING is at ROM 0x07032 and is referenced from exactly one place, the
    pc-relative lea at 0x05722 inside FUN_000056e8 -- the per-character writer, which
    walks the string by the object's +0x22 index and stores each cell through the text
    pointer in the object's +0x24.
    THAT WRITER IS NOT THE PROBLEM AND NEITHER IS ITS ADDRESSING. The port renders the
    store as `*(undefined2 *)((int)P68(LD32S((unaff_A6 + 0x12))) + uVar2) = uVar3`, so
    the 68K pointer is already mapped -- by the P68 pass, not the LD32S one. (My LD32S
    pass would have missed it anyway: its rule is `LD32S(..) + <literal>` and this
    offset is a variable. Worth widening if another site turns up, but this one is
    covered.)
    THE SPAWN IS FAITHFUL TOO. FUN_000056c2 sets 0xFFD580 = 0x8000 (live), +0x02 =
    0x56E8 (the writer), +0x21 = 0, +0x22 = 0 and +0x24 = the text address, matching the
    68K at 0x56C2-0x56E6 field for field.
    WHAT IS ACTUALLY WRONG: THE OBJECT IS NEVER CREATED. At frame 1099 our live set is
    only slot 0 (the player, 0x49BA), slot 48 (0xDABC) and slot 59 (0x5618, the intro
    parent). No 0x56E8 anywhere.
    THE GATE, altbeast_port.c:17945, inside the intro parent's handler:
        +0x21 += 1 ; bVar2 = +0x21 & 0xF ; +0x21 = bVar2
        if (bVar2 == 0 && (--word[+0x22]) < 2) { ... sound 0x4A ; FUN_000056c2() ... }
    so it fires when the low nibble of +0x21 wraps AND the word at +0x22 counts below 2.
    OUR slot 59 reads +0x21 = 00 and +0x22 = 00 at frame 1099. Decrementing 0 gives
    0xFFFF, which is not < 2, SO THE GATE CAN NEVER FIRE. Something upstream should have
    left +0x22 counting down; find what sets it and why ours is zero. The same handler
    reloads it to 0x100 inside the gate, so the initial value comes from elsewhere.

THE INTRO LIGHTNING: THE ZOOM PLUMBING IS INTACT, SO IT IS AN OBJECT THAT IS MISSING OR
UNZOOMED. 2026-10-19, measured on score.32x, six coins, START 901, frame 1249.
    WE EMIT NO ZOOMED RECORD ANYWHERE: two entries and a terminator, zoom word 0000 on
    both, where the arcade has three and its entry 1 is the bolt with zoom 0x7021.
    RULED OUT, the register-file gap I expected: port_zoom_scale publishes d0/d1/d3/d6/
    d7 and A0 into _port_reg[0,1,3,6,7,8], and ALL FOUR callers read them straight back
    (`p_D3 = _port_reg[3]; p_D6 = _port_reg[6]; p_D7 = _port_reg[7];` at
    altbeast_port.c 6120, 6226, 7521, 7891). The plumbing is complete; do not re-walk
    it.
    THE ZOOM WORD COMES FROM OBJECT BYTE 0x4E. The staging write is
    `uVar5 = A6[0x4e]; if (A6[0x4e]) uVar5 = (uVar5 & 0x1f) * 0x21; record word 5 =
    uVar5` (hand/AT_FUN_00003d60.c's tail). EVERY LIVE OBJECT OF OURS HAS +0x4E = 0 at
    that frame -- slots 0 (player, 0x49BA), 48 (0xDABC), 59 (0x5618, the intro parent)
    and 60 (0x57BC) -- so zoom 0 everywhere is CONSISTENT with our object state rather
    than a bug in the record build.
    SO THE QUESTION IS WHICH OBJECT CARRIES THE ZOOM ON THE ARCADE and whether we have
    it at all: our four live objects produce two records (slots 48 and 60 have the draw
    bit; 0 and 59 do not). Needs the arcade's object table at 1250 to answer, which is
    one MAME dump.
    THE SECOND HALF, the mis-assembled strike sprite, is a separate shape and still
    open: ours 7800 0111 01FC 01E7 F084 against the arcade's 7800 0111 00FC 01E4 F384 --
    the pitch BYTE and the palette nibble are identical and only bit 8 of word 2 (the
    FLIP bit) and the bank nibble of word 4 differ, which is a field OR'd from the wrong
    source rather than a wrong object.

**NEVER REGENERATE altbeast_port.c WHOLESALE. A REBUILD FROM THE COMMITTED SEEDS LOSES
MORE THAN HALF OF IT.** Measured 2026-10-20, and the instruction that led here ("seed
the function starts, regenerate") is exactly what would have destroyed the port.
    THE COMMITTED UNIT      91,500 lines, 755 function definitions
    A FRESH rebuild.sh + f1_decompile.py   40,331 lines, 692 definitions
tools/ghidra/rebuild.sh does what it says -- it rebuilds to the SEEDED state (import,
seed_apply, coverage) -- and the seeded state is NOT the worked state. The committed
unit is the accumulation of weeks of project-side operations (the f1_* scripts,
set_param_storage, fix_bounds, split_frame_body and the rest), and seed_apply alone
reproduces neither the extra 63 functions nor whatever made the bodies twice as long.
The Ghidra project lives in the scratch directory and is GONE, so there is no way back
to the worked state except the committed C itself. THE COMMITTED altbeast_port.c IS THE
ARTIFACT OF RECORD. Treat it as source, not as output.
WHAT THE REBUILD IS STILL GOOD FOR, and this is why the exercise was not wasted: it is
a way to OBTAIN A FUNCTION GHIDRA NEVER MADE, one at a time, without touching the unit.
Running it put all eleven kind-init targets into the project (seed_apply: 2098 seeds,
692 entry points, 481 functions created, 13 failed and none of them these), and
f1_decompile then produced bodies for nine of them -- 0xE316 0x132A0 0x12B08 0x12422
0x1208E 0x1176A 0x16484 0x13AA6 0x1435C. Two (0x14A78, 0x152E2) are functions in the
project but did not come out of the export.
SO THE FIX FOR A MISSING ROUTINE IS A HAND FILE, NOT A REGENERATION: lift the body out
of a scratch export and commit it as sh_src/portfull/hand/FUN_<addr>.c, which is the
mechanism this tree already uses for exactly this (hand/AT_FUN_00003d60.c: "an entry
Ghidra never made a function of"). The dispatch table is built from the functions in
the unit, and a hand file is included into it.
THE SEEDS WERE NEVER THE PROBLEM. All eleven addresses were already present in
docs/audit/altbeast_seeds.json (in function_seeds, repair_seeds and seeds) before any
of this. The committed export is simply older than the seeding.
