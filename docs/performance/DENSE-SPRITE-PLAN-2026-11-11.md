# Dense sprite compositor: rejected live experiments

## Hardware verdict (2026-11-12)

Neither tiled compositor belongs on the player line.  The exact 32x32 version
already lost badly in ares and dropped most of the plane in its first forced
fixture.  The replacement eight-row compositor passed the complete host oracle,
then lost on the real 32X path under the same frozen native dense-sprite table:

| renderer | V-ints/s | native clock/s | tick calls/s | applied flip acks/s |
| --- | ---: | ---: | ---: | ---: |
| accepted scalar path | 59.8696 | 59.8621 | 59.8621 | **40.0084** |
| sparse eight-row bands | 59.7357 | 59.7239 | 59.7239 | **35.5695** |

The band path produced only 88.91% of the scalar path's applied acknowledgments,
an 11.09% loss.  Each line has six valid mode-4 samples over 96 seconds after
cold launch.  The clocks were decoded from the rendered marker words, not
inferred from thumbnails.  Full samples and hashes are in
`build/codex-band-audit/rig/hardware-ab.json`; the scalar and band screenshots
are beside it.  The rig was restored to `devmerge02.32x`, confirmed by both
`ACTIVEGAME` and the fresh launch log at 04:41:47.

The band core remains default-off as an exact research oracle, but is rejected
for live promotion.  Its scratch clearing, packet replay, coverage construction,
foreground repair and sparse flush cost more than the framebuffer stores it
eliminates.  The next renderer experiment must remain in the scalar painter and
coalesce work locally, without a frame/band/block scratch compositor.

### Scalar tilemap-cell cache: also rejected

A 512-entry direct-mapped cache then targeted the foreground tilemap reads made
by overlapping sprite punches.  The dense fixture visits 1,597 foreground cells
but only 316 are unique, so the trace predicts an 80.2% cache hit rate.  The live
candidate reused the existing priority arena and preserved the scalar framebuffer
writer.  It passed the host context/collision/wrap test and remained exact in
ares, but the same frozen fixture lost on MiSTer:

| renderer | V-ints/s | native clock/s | tick calls/s | applied flip acks/s |
| --- | ---: | ---: | ---: | ---: |
| accepted scalar path | 59.8696 | 59.8621 | 59.8621 | **40.0084** |
| scalar + tilemap-cell cache | 59.9298 | 59.9318 | 59.9318 | **39.5320** |

The cache is 1.19% slower in the output metric despite the predicted hit rate.
That means these repeated uncached tilemap loads are not the dense-smoke limit;
the added hash/tag/branch work costs more than the reads it removes.  The live
flag and implementation were removed after the verdict.  Six synchronized
samples spanning 153 seconds are in `build/codex-prcell/rig/pcc02-health.json`
with their source screenshots.  The rig was restored to `devmerge02.32x`; its
local and remote SHA-256 both read
`dd8d66187e8118b6baa0ff89203bbdcab93cdd4d4c9d854cdc9aa855520e97b6`.

### Scalar row-store occlusion: rejected in ares

The next experiment stayed inside the scalar baked-row copy and skipped only
stores whose complete physical word span was covered by a later fully opaque
row.  Generated AB1/ZB1 sidecars preserved every established packet byte and
address.  A count-only path retained row-pixel, span, box and priority metadata.
The host word differential passed, and the frozen MAME table predicted 1,712 of
11,876 stores removable with two row ranges per record (14.42%).

That opportunity did not survive the live snapshot.  With conservative barriers
the planner executed 649 times, admitted 1,872 records and skipped zero rows.
Retaining later opaque coverage across declined records was also safe by painter
order and raised the offline estimate to 4,128 stores (34.76%), but the live
fixture still skipped only 63 rows / 205 estimated longword stores over 644
plans.  The added planning reduced ares applied flips from 4,030 to 3,971 over
5,000 frames.  The implementation, metadata format and flag were removed.  The
paired traces and counters are under `build/codex-rowocc/ares-{control,live3}`.

Do not retry per-frame whole-list analysis for this problem.  Any next scalar
experiment must make its decision from information already present in the hot
row loop, or precompute a complete plan outside the frame budget.

### Scalar baked opaque intervals: headless gain, hardware pending

`R60ZOOMRUN=1` is the first follow-up that satisfies that restriction.  It does
not compose a frame or change the ZB1 packet.  The working-set baker appends an
OR1 sidecar containing one prevalidated longest opaque interval for each atlas
row.  The existing scalar row copier then executes a generic prefix, an opaque
middle, and a generic suffix.  Inside the opaque middle it omits the per-group
palette mask and zero tests; painter order, shifted carry, spans, row-pixel
counts and stores are unchanged.  Clipped records retain the original copier.

The smoke corpus contains 1,151,965 four-pixel groups in 81,578 rows.  Of its
1,037,738 nonempty groups, 900,287 (86.75%) are fully opaque.  A row's longest
opaque interval covers 98.68% of all opaque groups and averages 10.25 groups;
71,937 rows have an interval of at least four groups.  The complete 45-pose OR1
sidecar costs 3,124 bytes and leaves 13,648 bytes before the established page
guard.  These figures come from
`build/codex-rowocc/sprite-macro-census-v9.json` (SHA-256
`2c162ee3e58e898e81134ce45102d19802b6239b135eced843cd00372d6be1c3`).

The fixed dense-scene A/B has zero differences across all 71,680 shown pens and
all 343,845 screenshot pixels.  Its independent native-scene audit fails for
both arms because that old fixture/reference has foreground-through-pen and
missing-pixel errors; it is not a native-parity proof.  The exact control versus
candidate framebuffer equality is valid.  SH-2 interpreter profiling, normalized
per row-copier call, reports 3.51% fewer instructions and 1.25% fewer modeled
cycles in `_r60_bk_rows`.  The surrounding `_r60_bk_sprite` setup grows, so this
is a local kernel result, not a whole-frame percentage.

The controlled 16,001-frame smoke route gives the first visible result.  Every
boss-handler transition is on the identical emulator frame in both arms:

| phase | control presented frames | OR1 presented frames | worst gap control / OR1 |
| --- | ---: | ---: | ---: |
| Neff entrance (`16D7E`) | 195 | 195 | 4 / 4 |
| materialization (`1794C`) | 41 | **43** | 4 / 4 |
| main death smoke (`17B54`) | 44 | 44 | 4 / 4 |
| death follow-up (`17B88`) | 90 | 90 | 3 / 3 |

Thus the optimization makes materialization fractionally more presentable
without changing native event duration, but is neutral on the measured entrance
and death smoke.  An earlier layout showed one extra entrance presentation;
that did not survive hardening and is treated as noise.  This is enough to keep
the experiment, not enough to promote it or call it the final smoke solution.
Hardware remains the promotion gate: the office rig refused SSH during this
run.  Evidence is under `build/codex-zoomrun/`; the hardened normal candidate is
`build/port/rom/zoomrunline02.32x` and its same-source flag-off control is
`zoomrunlinecontrol01.32x`.

A 2026-11-12 rebuild, `zoomrunline03.32x` (SHA-256
`06fc229025a56dc8c9738179fb8b1ddcb8d4840cafe4610b59d5a9b850faf751`),
differs from `zoomrunline02.32x` in only three build-stamp time digits.  All
earlier headless evidence therefore applies to the fresh ROM byte for byte
outside the stamp; it was not reclassified as a new performance result.

The matched 65,001-frame broad health run is also neutral-to-slightly-negative:
both arms record 64,989 VInts and 64,991 ticks; control presents 54,812 flips
and OR1 presents 54,790, a difference of 22 (0.04%).  Attract scenes can phase
shift, so this is not a standalone regression claim.  It is enough to reject
headless promotion: OR1 remains exact, reusable and default-off until a paired
MiSTer smoke measurement shows a material event gain.

### Per-sprite row-constant hoist: exact, rejected headless

`R60BKHOIST=1` tests a narrower scalar change.  The baked-row copier normally
derives the palette-bank transform constants and saves/restores four high
registers for every sprite row.  The experiment derives those constants once
per sprite and keeps them live across its row loop.  It does not change painter
order, row packets, spans, pixel counts or framebuffer-store decisions.

The exact row checker reports 68,735 checked rows with zero byte, row-end or
pixel-count errors.  The MAME-derived gameplay gate has identical object/game
records at every reached target.  Its two early picture warnings reproduce on
the same-source flag-off control, whose dumped framebuffer and picture files
are byte-identical to the candidate at those frames.  The later H #13 target is
unreached by both arms.  Those inherited staging/baseline results are therefore
not attributed to this experiment.

Interpreter profiling does show the intended local reduction.  Normalized per
`_r60_bk_rows` entry, the candidate uses 1.07% fewer instructions and 0.77%
fewer modeled cycles.  That local win does not translate into the controlled
16,001-frame smoke route:

| fixed route window | control presented frames | hoist presented frames | worst gap control / hoist |
| --- | ---: | ---: | ---: |
| Neff entrance | 240 | 238 | 2 / 2 |
| materialization | 121 | 120 | 2 / 2 |
| main death smoke | 63 | 63 | 2 / 2 |
| death follow-up | 62 | 62 | 2 / 2 |

Both arms finish at the same native game frame, but the full route presents
13,034 frames on the control and 12,915 on the candidate: 119 fewer (0.91%).
The isolated attract run is effectively neutral (+1 presentation), which does
not outweigh the controlled smoke-route loss.  This is an exact implementation
and a useful warning that removing per-row SH-2 bookkeeping is not automatically
a visible win.  It is rejected for headless promotion and remains default-off
only as a future hardware A/B probe; it is not a player-ROM candidate.

The normal probe is `build/port/rom/bkhoist01.32x` (SHA-256
`b11f34ea301b44489a7a38ade4154db0174b9bc49a9b6a5a8345d5af75ac260e`);
the controlled smoke probe is `build/port/rom/bkhoistsmoke02.32x` (SHA-256
`2a490b898afdc3312f689b5152c3c2239e5190d5907ae11fa6c04150047065d7`).
Evidence is under `build/codex-bkhoist/`.  With the option absent, the rebuilt
line differs from the earlier accepted control only in five build-stamp bytes.

### Ready-byte trail register: exact, hardware probe only

`R60RAWTRAIL=1` targets the already accepted ready-byte loop used by palette
bank zero and prepared sprite packets.  In the 1,801-frame interpreter sample,
45,436 of 92,879 row-copy calls take that path.  Its only saved high register
held the one-bit trailing-carry predicate.  After the palette-add-zero branch,
`r7` is dead, so the experiment moves the predicate into `r7` in the branch
delay slot and removes the per-row stack store/load.

The exact checker reports 68,720 checked rows with zero byte, row-end or
pixel-count errors.  Normalized per ready-byte call, the interpreter reports
1.32% fewer instructions and 0.45% fewer modeled cycles.  Across the complete
baked-row region the modeled cycle reduction is only 0.23%.

The full MAME-derived gameplay differential reaches 19 of 20 targets with zero
live-record differences at every reached target.  H #13 is not reached by the
flag-off control either.  The two early picture warnings and stale staging
diagnostics are likewise inherited: the candidate's T/E framebuffer and picture
artifacts are byte-identical to the flag-off control at those frames.  The gate's
reported 1/20 result is therefore the shared H #13 reachability issue, not a
candidate record or pixel difference.

The controlled route does not support headless promotion.  It ends on the same
native game frame as its byte-matched flag-off control, but presents 12,938
frames versus 13,034, a loss of 96 (0.74%).  Fixed event windows report 240/240
entrance presentations, 122/121 materialization presentations, 63/63 main
death-smoke presentations and 62/62 follow-up presentations (candidate/control).
Thus the target materialization window gains one presentation, while entrance
and death smoke are neutral and the route as a whole is worse.  The isolated
attract profile similarly presents four fewer frames.

This remains default-off as a small hardware A/B probe because it removes two
real SDRAM stack accesses from nearly half the sampled rows, which an emulator
cycle model may not price like the 32X bus.  It is not a player-ROM candidate.
Mike's 2026-11-12 local playthrough of the normal probe found no visible
regressions.  That accepts its rendering behavior for further testing; it does
not establish a cadence improvement or override the negative headless count.
The normal probe is `build/port/rom/rawtrail02.32x` (SHA-256
`5995f7b24bf29b1fc74404d2615b675f68c212838e53200ae921020c1ee3ae84`);
the controlled probe is `build/port/rom/rawtrailsmoke01.32x` (SHA-256
`b62c28f189647c9971ce41208fb10c23f5a8368370baa24ce316bf5230dd64cb`).
Evidence is under `build/codex-rawtrail/`.

### Scaled prepared-pose cache: rejected

A default-off experiment allowed the accepted two-slot prepared-pose cache to
consume the already-scaled ZB1 row format.  Zoom was added to cache identity so
two scaled versions of the same source art could not alias.  Painter order and
the row copier were unchanged, and clipped packets retained the scalar path.

The isolated 5,000-frame attract run was healthy but gained only two large-slot
hits.  The controlled 16,001-frame FORM3/TICK120/ENDCLEAR/GOD smoke route was
decisive: scaled admission added 1,130 total cache lookups but only 29 net hits.
The large slot declined 1,913 oversized poses.  The candidate produced 12,917
applied framebuffer flips versus 13,000 for the same-source flag-off control,
83 fewer (0.64%), while their final 512-byte health/clock dumps were identical.

The experiment was removed rather than retained behind a flag.  The reusable
lesson is that the current 768/256-word arena is appropriate for repeated small
unscaled packets, not the entrance cloud's large scaled poses.  Do not spend
slave critical-path time scanning those packets for admission.  A future scaled
cache must be pre-sized from the ZB1 corpus and prepared off the slave render
path; simply sharing the accepted two slots is a measured loss.  Evidence is
`build/codex-zoomrun/sprprepz{01,smoke01,smokectl01}-*`.

### Solid priority-cell shortcut: rejected headless

The level-one white-wolf observation was tested against a fresh MAME oracle and
paired ares runs.  In the cemetery window (arcade frames 3750..3900), the arcade
shows exactly 38 one-frame red flashes: one every four native ticks.  The port
also generates that one-in-four palette state.  Its control shows the same 38
red frames, with three missed framebuffer flips shifting two flashes and holding
one for a second displayed frame.  The wolf logic and palette cadence are not
the defect; presentation pressure determines which one-frame state is seen.

The foreground-priority cache at that scene held 137 masks: 85 fully opaque,
51 partial and one empty.  Of the active tilemap references that were resident,
1,262 were solid versus 314 partial.  A default-off experiment marked solid
masks in the spare cache-tag bit and replaced their eight repeated mask tests
with an inline constant-store loop.  It preserved the guarded code regions only
by moving an unchanged clear helper between the two existing SH-2 SDRAM code
regions; neither region was enlarged.

The result is too small.  Normalized per `r60_punch2` call, the cemetery
interpreter sample uses 1.47% fewer instructions but only 0.18% fewer modeled
cycles.  Its exact wolf window presents 146 of 151 host frames versus 148 for
the control, and the full 3,901-frame replay presents 3,150 versus 3,157.  A
first out-of-line version was worse because the per-cell call erased the local
gain.  The source experiment was removed; ROMs and profiles remain under
`build/codex-punchsolid/` as negative evidence.

Deferring and merging the boxes is not the escape hatch.  A per-frame census of
both framebuffer banks over the same 151-frame window found only 0.35% and
0.52% rectangle-union overlap.  The gravestones cost real through-pen stores;
they are not repeatedly punching a large shared area.  Do not retry tag-based
solid-mask or whole-pass box-union variants without a new store-removal design.

### Two-bank transition patches: exact corpus and admission gate

`tools/sprite_transition_census.py` measures the next store-removal idea in the
32X's actual alternating-bank model.  It composes the target run in painter
order and compares native tick N with N-2, the image normally resident in the
physical bank being reused.  A patch entry records the changed four-pixel word
and which bytes must restore background.  It is game-independent; event ranges
and the target sprite key are inputs.

The first report was invalidated before integration.  It carried sparse MAME
event captures forward across uncaptured native ticks, so repeated tables were
incorrectly priced as exact N/N-2 transitions.  The old 69.59% reduction and
its storage-coverage figures are retained only as rejected evidence in
`build/codex-rowocc/sprite-transition-census-v8.json`; they are not performance
claims.  Both tools now require a real capture at N and N-2 by default.  The
old behavior is available only through the explicitly diagnostic
`--carry-forward` option.

A new cold-start MAME run records all 33,750 native sprite tables from the
level-one tape.  Its 34,560,000-byte stream has SHA-256
`6195ff8f321f6af906bb21ecc248e7d8a8aee358be469d0c95216609b609741b`.
`tools/sprite_transition_oracle.py` models the accepted row-extent clear with
the target smoke run deliberately carried as a kept object, draws every other
native record in painter order, applies the target transition, and compares the
complete 320x224 logical sprite layer to an ordinary full redraw.

Across the 284 ticks where both banks contain the target run, 146 pass that
sprite-layer oracle and 138 reject.  The passing set writes 607,918 transition
words instead of 1,228,920 scalar stores, a measured 50.53% opportunity.  This
is still not a live speed result.  Forty-eight passing ticks require 17,949
nonzero lower-sprite restore pixels.  Until the packet format carries those
values, the stricter zero-restore set contains 98 ticks and reduces its scalar
stores by 51.33%.  The packet wire values use a page-level table of the five
native smoke palettes and encode palette selector plus pen in one byte; the
earlier `palette | pen` identity was ambiguous and is rejected.

The mapper audit is narrower than the first estimate.  Slot 7 must repeat the
394,652-byte sound blob at identical offsets, and the accepted page also keeps
45 ZB1 poses plus the OR1 sidecar.  Rebuilding its AB1 index for only the smoke
phase retains all 72 global references, the complete 62,528-byte ZB1 atlas and
the 3,124-byte OR1 sidecar while raising usable tail space from 18,764 to 63,324
bytes.  On the strict zero-restore set, shared-dictionary selection fits five
packets / 23 ticks in 63,170 bytes and retains 27.29% of admitted store savings;
that unconstrained choice favors death smoke.  A balanced roughly 30 KiB per
event price retains five entrance ticks and ten death ticks before cross-event
dictionary sharing and packet-key overhead.  No accepted atlas pose is evicted.

Two executable facts now replace the former assumptions:

1. the normal per-bank span clear does erase the previous cloud; a live path
   must carry the target spans as a deliberate keep before emitting any patch;
2. the host oracle rejects painter-order and clear collisions at zero pixels,
   and the strict selector rejects every tick whose vacated target bytes need a
   nonzero lower-sprite restore.

A foreground context audit is favorable but not sufficient: every selected
tick has scroll `0x18C0` and the same tilemap SHA-256
`a7d8e8d1144ccfe394c7885b997cea0604134e9360441a7faaef3c3d8c2563657`.
Palette snapshots change throughout, which is compatible with logical pen
patches; text changes between events and must retain its independent later
composition.  The remaining gate is foreground priority: a live patch must
replay the original target records' punch metadata and prove its spans, boxes
and punched rows equal to the scalar path.  Text remains the independent later
composition pass.

Exact evidence: `build/codex-temporal-exact/oracle-v2.json`, SHA-256
`1ec068025315e423d249c2a5ec3cce5243a189316a0485d404d5abf771be000a`;
`build/codex-temporal-exact/census-palette-v1.json`, SHA-256
`92906ca7ac58eba2b57679673299ef1c2d344602f3e3f96aa229208c497fe1c3`;
minimal-AB1/full-atlas page proof
`build/codex-temporal-exact/page/smoke.json`, SHA-256
`659e263cfb47c2b16df40b6f1d471c1de28f91e30e8252edfa290f062abe49a3`.

A failed admission must use the scalar path before any patch word is emitted.
There is no safe mid-patch fallback.

### Precomposed macro-sprite direction

`tools/sprite_macro_census.py` prices that precomputed alternative without a
live renderer.  It recognizes normalized arrangements of any requested sprite
key, composes the native records in painter order, and splits the final visible
pixels into disjoint logical-palette layers.  Because those layers never own the
same final pixel, their draw order cannot alter the composed image.  The tool is
corpus-driven and reusable for later Sega16 ports; it contains no Altered Beast
addresses or smoke timing.

For key `031401d0` over native event ticks 30660:30810 and 33550:33690,
the complete AB1/ZB1 census (including the globally stored unscaled pose) finds:

- all 105 captured frames contain exactly one contiguous target run, so no
  arrangement crosses an intervening sprite record;
- those runs produce 67 normalized arrangements, with the full cloud reaching
  16 original records;
- the scalar records emit an estimated 931,799 physical nonzero longwords;
- disjoint macro layers emit 476,866, a 48.82% reduction;
- a 512 KiB selection occupies 516,488 bytes, covers 52 captures, and retains
  58.46% of the measured store-saving opportunity;
- a 1.5 MiB selection occupies 1,569,632 bytes, covers 93 captures, and retains
  98.53% of the opportunity.
- the 21,888 bytes actually free in the current page fit only one dense, sparse,
  or 32x32 pattern, retaining less than 1% of the store-saving opportunity;
- raw-deflate storage fits three patterns / ten captures in that space and
  retains 11.15%, but runtime decompression would add exactly the per-frame work
  this direction is meant to remove, so it was screened out before integration.

Evidence: `build/codex-rowocc/sprite-macro-census-v9.json`.  The JSON is
deterministic across independent runs (SHA-256
`2c162ee3e58e898e81134ce45102d19802b6239b135eced843cd00372d6be1c3`).
The tool round-trips every generated layer packet and fails if any owned pen
changes.  This is a materially
larger ceiling than the rejected runtime planners, but it is not yet a live
candidate.  Integration needs a bounded arrangement hash, a mapper/page budget,
and an exact target differential against the original records.  Do not claim a
speed gain from this census.

The tempting framebuffer-tail packet cache was also screened out before live
implementation.  Ares microbenchmarks read cartridge-to-framebuffer at 19.0
cycles/group; an uncached framebuffer source read alone costs 17.0 and the
framebuffer destination write another 17.0.  Evidence screenshots are under
`build/codex-rowocc/fbbench{1,2}`.

The smoke slowdown tracks overlapping sprite area: boss-death smoke accelerates
as the cloud shrinks even though the animation cadence is unchanged.  The saved
dense frame performs 10,660 sprite/punch word stores for 4,530 unique physical
words.  That store census motivated the experiments below; it did not predict
their bookkeeping cost on the SH-2.

## Proven components

- `r60_sprite_blocks.h` passes 1,430 host differential cases, including the
  real 22-record cloud capture.  The complete 71,684-byte framebuffer, ordered
  spans, boxes, priority-punch metadata and arena guards match the scalar oracle.
- `r60_sprite_block_plan.h` splits painter order into maximal contiguous
  DRAW/KEEP ranges without crossing RAW/REPUNCH barriers.  Every record is
  classified once.  Exhaustive inputs through ten records and the 64-record,
  21-range bound pass ASan and UBSan.
- `tools/sprite_block_census.py` finds profitable ranges in 162/187 saved smoke
  captures.  It admits 1,904/2,025 eligible draws (94%), up to 19 in one frame.
  This is coverage evidence, not a speed result.

## Live memory ownership

Do not revive the old natural adapter's 9,216-byte retirement of both accepted
caches.  Its page-7 guard admitted zero of 8,968 observed passes and its ROM did
not exercise the compositor.

The live adapter instead borrows `0x0603C400..0x0603D3EF` only after admission.
That is 4,080 bytes inside the 4 KiB priority-mask byte array.  It does not touch
the two-slot prepared-sprite cache at `0x0603D800..0x0603E800` or the priority
tags at `0x0603D400..0x0603D800`.

Before borrowing, freeze the exact 41x29 foreground identities and scroll/bank
context into the compositor arena.  The compositor's opacity callback reads
decoded tile art directly, not the overwritten mask bytes.  After the run set
`r60_prio_init=0`; any later scalar punch must rebuild tags/masks normally.
Never leave old tags apparently valid over borrowed mask storage.

`0x06038000..0x06038FFF` is not an arena: under `R60_HOLDDIRECT` the master uses
it for both plane art lists while the slave can be drawing.  Likewise, the
framebuffer packet tail and staging buffers have concurrent owners.

## Dispatch contract

1. Retain the existing snapshot lease, clear planning, palette assignment and
   foreground context capture.
2. Classify all records before the first compositor output.  Packet lookup must
   return an exact bounded extent; malformed packets reject the complete plan.
3. Execute records in original order.  Each planned interval calls the exact
   compositor with absolute slot numbers.  Every barrier and unprofitable range
   executes the unchanged scalar body.  Never fall back after a compositor
   interval has emitted pixels.
4. Share the existing span capacity/full flag, rectangle list, KEEP counters,
   text-touch state and final bank publication across all intervals.
5. First live flag is default-off.  Build a control and candidate from the same
   accepted line, then gate MAME and ares on route-matched inputs before a ROM is
   offered for hardware speed judgment.

The first candidate may preserve the current renderer's frozen-native foreground
semantics.  It must not claim to solve the separate MD-presented-plane epoch
problem; that requires the acknowledged-plane protocol described in the earlier
foreground design.
