# LOOP 19 — SPRITE COLOUR. One root cause under most of the punch list.

Read this, then `CLAUDE.md` ("What MAME is for now"), then `TOOLKIT.md`.
`docs/log/LOOP18.md` is the full log of the day this came out of.

## WHERE THIS STANDS (2026-08-19)

**Canonical build:**

    make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
         BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16

`_end 0x06018658`. **CUTBLANK is OUT** — see below.

                      LOOP18 open   J, best measured
      handler mean         91.3          82.5
      game gets            65%           69%   (~53% of the arcade)
      band drops/cycle     ~1.18         0.52
      flip/blit skips      0.3-4.6%      0.1%  (1 in 1670 cycles)
      pen drift            2166-3775     215

**9.6% of the 68K's stall reclaimed across the loop**, and 3-17x less
palette churn once CUTBLANK came out (215-782 without it against
2166-3775 with).

**THE SPLIT HAS FLIPPED: window/ack 36.3 vs own-tail 46.2.** The
master's FM work is no longer the bigger half — the 68K's OWN work is
(consume + DREQ push + glue). Every remaining lever on the SH-2 side is
chasing the smaller number now. SPRTRUNC took the easy half of the tail;
the parked sprite-list DELTA encoding (73% of decode jobs repeat frame
to frame) is what is left there, and it is the honest next speed item
once the colour work lands.

**LOOP18 landed four things:** BLITSKIP (group-level blit skip),
DIRTYROW (row-level skip without reading), the sbuf region-guard
purchase (88 -> 2,776 bytes, which unparked SPRBAKE), and BANDSHIFT
(master/slave compose rebalance — the tearing lever).

**CUTBLANK removed.** It punched 10,000+ black pixels into the MD plane
at every level start (MAME-confirmed frame by frame) because its "blank
cells under the fade" premise does not hold at a level start, and it was
driving the palette churn: pen drift fell 2.8-4.8x without it.
**STILL OWED: check a CUTSCENE cut** (round end, transformation) — that
is the case CUTBLANK was added for in LOOP14 and nothing has re-tested
it.

## THE THESIS: MOST OF THE PUNCH LIST IS ONE BUG

Mike's 11-item list from the J play pass is dominated by sprites in the
WRONG COLOUR — items 3, 5, 7, 8, 9, 10, and probably 4. They have one
plausible root, and the code says it plainly.

`spr_pair[par][64]` maps each of the 64 S16 sprite colour sets to one of
15 MD-side pairs. Every cycle it is cleared to 0xFF and rebuilt from
`sused[]` — the sets seen by that cycle's scan. At draw time:

    uint8_t pr = spr_pair[par][d4 & 0x3F];
    base = (uint8_t)((pr == 0xFF ? 15 : pr) << 4);

**An unassigned set draws in PAIR 15 — which is the shadow ramp.** So a
sprite whose colour set was not in the last scan renders in the shadow
palette for one cycle.

**One cycle is three vints. Mike's flashes are 1-3 frames.** Item 5
(gravestone black for 3 frames, yellow for 5, yellow for 3), item 9
(gravestone flashes yellow, 3 frames) — that is exactly one cycle of
pair-15 fallback as a sprite enters.

The second failure mode is in the same allocator: when no pair is free
and none is old enough to steal, the set is given `shared_pair` — an
ALREADY-OWNED pair. That is persistent wrong colour rather than a
flash, and it fits items 8 (white wolf wrong shade) and 10 (enemy wrong
palette). Capacity: 15 pairs against a measured worst of 11 distinct
sprite sets per cycle (LOOP18 SPRLINE probe), so this should be rare —
**re-measure it during a cutscene and a boss**, which the probe run did
not cover well.

### CONFIRMED IN CODE, not inferred

`build_maps(par, bank1)` is what fills `sused[]` from `SPR_SNAP` and
rebuilds `spr_pair[par][]`. Its only caller:

    if (b->rg == 2)
        build_maps(b->bpar ^ 1, b->bank);     /* m_main.c, band terminator */

**It builds the map for the OPPOSITE parity — the NEXT cycle — from the
CURRENT cycle's sprite list.** So the pair map is always one cycle
behind the sprites it has to colour:

  - cycle N-1's terminator builds parity N's map from N-1's SPR_SNAP;
  - a sprite whose colour set first appears in cycle N is therefore
    NOT in `spr_pair[parN]`, reads 0xFF, and draws with base 15 — the
    shadow ramp;
  - cycle N's terminator includes it, so cycle N+1 is correct.

**Exactly one cycle wrong on a sprite's first appearance. One cycle is
three vints. Mike's flashes are 1-3 frames.** The signature matches
without any remaining assumption.

The reason it is a cycle ahead is cost: build_maps is ~4ms and
uninterruptible (its own comment), which is why it runs in the gap and
not in a window.

### The fix: a LATE-CLAIM pass at k1

Do NOT try to move build_maps — it cannot afford to run inside a window.
Instead, after the sprite-list snapshot at k1 and BEFORE `apply_cram(par)`
(m_main.c:4762, already in that same block), scan the freshly
snapshotted list for sets whose `spr_pair[par]` is still 0xFF and assign
them from the free pairs. Capacity is there: 15 pairs against a measured
worst of 11 distinct sprite sets per cycle (LOOP18 SPRLINE probe).

Cost is one 64-record scan plus a handful of assignments — nothing like
build_maps' full rebuild. Ordering works because apply_cram runs later in
the same k1 block, so the newly claimed pairs get painted in the same
window they were claimed.

Gate it the way LOOP18 gated everything: a counter of late claims and of
sprites that STILL drew with pr==0xFF, and falsify the counter before
believing a zero.

### The two fixes to evaluate

1. **Claim the set when the sprite list is SNAPSHOTTED, not a cycle
   later.** The snapshot already happens at W1; the pair rebuild appears
   to run off the previous scan. Closing that one-cycle gap kills the
   flash class outright.
2. **If a set is still unassigned at draw time, SKIP the sprite for that
   cycle** rather than drawing it in the shadow ramp. One frame absent
   beats one frame luminous yellow — and note item 4 ("Zeus invisible
   during the title text") suggests something already behaves this way
   somewhere, so check before adding a second policy.

Do 1 first. 2 is the fallback if the gap cannot be closed.

## MEASURED: THE LATE CLAIM IS NOT ENOUGH. THIS IS A CAPACITY PROBLEM.

The late-claim pass is built (`make ... SPRLATE=1`) and the instrument
finally works. It does NOT fix the flashes, and the reason is worth more
than the fix would have been.

      map missed 647 sets   claimed 28   no pair available 619
      sprites that still drew in the SHADOW RAMP: 31,004

**The root cause is confirmed** — the pre-built map really does miss
hundreds of sets, exactly as the one-cycle-behind reading predicted.
**But the fix cannot land**, because at the moment a missed set needs a
pair there is almost never one to give it:

  - a genuinely FREE pair (`pr_key` unowned AND both `grp_key` halves
    unowned) existed for 10 of 633 misses;
  - adding build_maps' own STEAL rule (take the pair whose owner has
    been absent longest, age >= 3) took that to 28 of 647.

**The pairs are owned by TILE GROUPS, not by absent sprite sets.**
`bound = 32 - 2*need` splits the 32 groups between tiles and sprites by
tile demand, so when tiles are hungry the sprite side is starved. The
premise this fix was scoped on — "15 pairs against a measured worst of
11 live sprite sets, capacity exists" — is **wrong**: 11 is the number
of sets that WANT a pair, not the number of pairs available to give.

**So item 3/5/7/8/9/10 is a CAPACITY problem, not a latency one.** The
one-cycle gap is real but it is the smaller half. Closing it properly
means changing how the 32 groups are divided between tiles and sprites,
or how long a departed set holds its pair (`pr_age > 90` is a long time
to hold a pair against a starved neighbour) — a real design item, not a
patch at k1.

**BEFORE SIZING THAT WORK, RE-MEASURE ON ARES.** This run is a
NON-SPRTRUNC build (MAME cannot render SPRTRUNC — see the trap below),
which is not the canonical configuration, and 31,004 shadow-ramp draws
does not match Mike's report of brief 1-3 frame flashes. Either the
non-canonical build inflates it or his eye is under-counting; find out
which before designing to this number.

### THE TRAP THAT ATE THIS SESSION — third time in two days

Every SPRLATE counter read ZERO through four rounds of debugging. The
instrument was fine. **The build carried SPRTRUNC and was being measured
in MAME**, where the sprite list never lands, so `SPR_SNAP[0]` reads as
the list terminator and `compose_sprites` loops over nothing — 55,474
calls, zero sprites reached. It is written in CLAUDE.md, in the Makefile
next to SPRTRUNC, and in LOOP18's own trap list, and it still cost a
full debugging round.

**Make it mechanical, not a rule to remember: if a probe reads zero,
FIRST check whether the build carries SPRTRUNC and the measurement is in
MAME.** Bisect from the outside in — function entry, then loop body,
then the branch — which is what finally found it.

A second, smaller reader trap alongside it: rom code writes fixed-block
counters through the UNCACHED 0x26 alias, but MAME's SH-2 debugger space
serves ONLY 0x06 and returns a clean zero for 0x26. Proven with a boot
sentinel. SPRLATE now uses the cached alias deliberately.

## THE RESOURCE IS THERE. IT IS BEING SPENT ON DUPLICATION.

Mike: *"we have SO much compute power and methods to resolve other than
using a dumb buffer"* and *"this is a FIXED gated system — we control
everything, we have the game end to end."* Both are right, and the
measurements now say exactly where the slack is.

### The census

      live sprite sets wanting a pair (peak)        9
      sets that got their own pair                  6
      sets FORCED TO SHARE an owned pair            4
      pairs reserved for sprites (tightest bound)   9
      live TILE singles squatting in that zone      6   = 3 pairs blocked
      free groups below bound when squatted (worst) 1

**The reserved pairs exist — 9 of them for 9 sets — and 3 are blocked by
live tile singles sitting inside the sprite zone.** The eviction pass
above `bound` only clears IDLE singles (`grp_age > 0`), so a live one
squats indefinitely. We are short exactly the 3 that are blocked.

### What does NOT work

**Exact-palette dedup is dead: 8,314 sets -> 8,313 distinct (1.00x).**
No two live sets carry the same 16 colours. Merging whole sets buys
nothing, and that was the obvious idea.

**Eviction of live squatters was already tried** and is recorded in the
source as the cause of the "yellow sprite-ghost" (group 14 flipping
between text yellow and sprite pair 7 every cycle). Do not retry it.

### What DOES work — the method §11 already proves, on the wrong layer

The MD side does not pack by SET, it packs by COLOUR: *"21 distinct S16
colour sets but only 36 distinct MD-quantised colours"*. Nothing does
that for 32X sprites — every live set is handed its own 16-entry pair
even though the sets overlap heavily.

Measured at the peak cycle, transparent pixel 0 excluded:

      9 live sets occupy      135 palette entries
      they contain only        97 DISTINCT COLOURS      = 1.39x
      97 colours fit in         7 pairs, not 9

**Colour-level packing recovers 2 of the 3 missing pairs, and squatter
RELOCATION (move the live tile single down to a free low group rather
than evicting it — free groups below bound never hit zero) recovers the
rest.** Neither buys a byte of memory.

### AND IT SHOULD NOT BE A RUNTIME ALLOCATOR AT ALL

The 1.39x above is what a greedy allocator discovers at runtime from one
cycle's live set. **We have the whole game.** The sprite palettes are in
the ROM, the scenes are fixed, and `tools/bake_sprites.py` already
establishes the pattern: compute it offline, verify it at build time,
ship a table.

An offline pack can see every scene at once and solve for a packing a
per-cycle greedy allocator cannot reach — and it costs zero runtime
compute, which is the opposite of what the current code spends. The
runtime job shrinks to "look up this set's pen mapping", with no
`pr_key`/`grp_key`/`pr_age` arbitration and none of the artifacts that
arbitration keeps producing.

**JOB 1 IS NOW: bake the sprite palette pack offline.** Order:
  1. Offline: extract every sprite colour set from the ROM, group the
     sets that co-occur (the sprite lists are deterministic per scene),
     and solve the colour-level packing per scene.
  2. Verify at build time the way the sprite bake does — every set's
     every pixel must map to the same colour it has today, or fail.
  3. Runtime becomes a table lookup; keep the existing allocator behind
     a flag until the bake is proven on ares.

Squatter relocation stays as a cheap independent win if the bake is
slower to land than expected.

## THE OFFLINE PACKER: WHAT THE HARVEST SAYS, AND THE ONE INPUT MISSING

`tools/palharvest.lua` dumps, per cycle, which sprite colour sets are
live and the 16 colours each holds — 870 cycles over a scripted play
through gameplay and the boss. Palettes are NOT a static ROM table (the
game writes palette RAM for fades and cycling), so the pack has to be
solved over observed demand. Three sharing strategies tested against it:

      exact whole-palette dedup      8,314 sets -> 8,313 distinct  DEAD
      position-wise sharing (all 15) 1 compatible pair in 10,849   DEAD
      colour-level packing           97 colours -> 7 pairs (1.39x) COSTS

**Colour-level packing works but is not free.** The sprite draw is
`base + pixel` — the pixel value IS the pen index — so merging by colour
requires a per-set pixel->pen REMAP, i.e. a table lookup in the hottest
per-pixel loop in the program. That cost has to be priced before it is
chosen; it is not the obvious win it looked like.

### The strategy that IS free, and the measurement that points at it

`mdp_s_used` on the tile side already records the insight: *"S16 art
leaves garbage in arcade-invisible entries"* — a set's palette has 15
slots but the art may use only a handful. **Two sets whose USED pens do
not collide can share one pair with no remap and no runtime cost at
all.**

First read of live pen usage (partial — see below):

      4 sets use all 14 pens (the main actors)
      6 sets use only 4-8 pens   (masks 0x1428, 0x1429, 0x15A8, 0x15AA,
                                  0x15A9, 0x15AB)

The sparse sets overlap each other heavily, so disjointness alone will
not merge them — but the correct test is compatibility on **pens both
actually use**, which is exactly what has never been measured.

### THE MISSING INPUT, and it should come from the ROM not a probe

The pen-usage numbers above are from a runtime tap and are NOT
trustworthy: only one of the draw macros was tapped in that run, and a
follow-up that tapped all of them harvested nothing (SPRPEN reads zero —
unresolved; the taps are now correctly macro-guarded and both the
shipping and probe builds verified, `_end 0x06018130` / `0x06018B28`).

**Do not fix the probe — get the input offline instead.** Mike's point:
we have the game end to end. `tools/bake_sprites.py` already decodes
every sprite frame's pixels, so pen usage per colour SET is derivable
without touching the running rom:

  1. Extend `palharvest.lua` to record, per live sprite record, its ART
     KEY (`e[3]` addr, `d2` pitch/flip, bank) alongside the colour set —
     the same key `bake_find` uses.
  2. Offline, decode each art key with the bake machinery and union its
     pens per colour set. That is exact, complete, and needs no probe.
  3. Solve used-pen-aware compatibility per cycle, then across cycles,
     for a static assignment.
  4. Verify at build time the way the sprite bake does: every set's every
     used pen must resolve to the colour it has today, or fail the build.

Only after step 3 is the answer known: if used-pen packing fits the
worst cycle into the 6 pairs actually available, the whole allocator —
`pr_key`, `pr_age`, `grp_key`, the steal, the shared_pair fallback —
becomes a lookup table, and with it go the artifacts it generates.

## THE OFFLINE PACKER IS BUILT, AND IT SAYS DO NOT PACK

`tools/palpack.py`. It takes pen usage from the ROM (decoding
`discover/*.csv`'s 963 jobs with `bake_sprites.py`'s own decoder — the
colour set is `w4 & 0x3F`, already in that CSV, so no runtime probe) and
the observed palettes from `tools/palharvest.lua`, then solves
used-pen-aware sharing across 869 cycles.

      pens used per set:  14 pens -> 18 sets     7 pens -> 1 set

**18 of 19 sprite colour sets use ALL 14 pens.** The `mdp_s_used`
insight that motivated this — *"S16 art leaves garbage in
arcade-invisible entries"* — is true of TILES and false of SPRITES in
this game. So:

      WORST naive (one pair per set)   8 pairs
      WORST used-pen-aware packing     8 pairs      <- buys NOTHING

All three free strategies are now measured dead: exact-palette dedup
(8,314 -> 8,313), position-wise sharing (1 in 10,849), and used-pen
sharing (18/19 sets full). Only colour-level packing compresses at all
(97 colours -> 7 pairs) and it needs a per-pixel remap in the hottest
loop.

### WHICH MEANS THE PACKING WAS THE WRONG PROBLEM

      pairs the worst cycle NEEDS        8
      pairs RESERVED for sprites         9   (bound 14 -> need 9)
      pairs actually USABLE              6   (3 blocked by tile squatters)

**The budget is already sufficient. Nine are reserved, eight are needed,
and six are reachable — because six live tile singles sit inside the
reserved sprite zone and block three pairs.** Nothing needs compressing,
nothing needs a remap, and nothing needs to be baked. The reservation
simply is not enforced.

**JOB 1 IS THEREFORE: make the sprite pair reservation real.** The
eviction pass above `bound` clears only IDLE singles (`grp_age > 0`), so
a live one squats indefinitely. Blanket eviction was tried and produced
the yellow sprite-ghost — but RELOCATION is not eviction: move the live
single to a free group below `bound` and its colour survives, the pair
frees, and nothing flips. Free groups below bound were never observed at
zero (worst case 1), so relocation has somewhere to go.

Keep `palpack.py`: it is the instrument that proved the budget is
adequate, and it is a per-TITLE question — another S16 game with sparser
sprite art would answer differently and could pack.

## RELOCATION MEASURED: DEAD. IT IS A GENUINE SHORTAGE.

`make GRPRELOC=1` is built and A/B'd. It changes nothing:

                              reloc OFF    reloc ON
      sets wanting a pair         9            9
      got their own               6            6
      forced to share             4            4
      squatters relocated         -            1
      NOWHERE TO MOVE THEM        -        1,914

**The tile zone below `bound` is full.** Tiles occupy 19-20 groups in a
14-group zone, so a squatter has nowhere to go and relocation fires once
in ~1,900 attempts.

**RETRACTION.** The previous section concluded "the budget is already
sufficient, the reservation is simply not enforced". That was wrong, and
the error was comparing "9 pairs reserved" against "8 pairs needed"
without checking whether the 9 could ever be free. They cannot: the
reservation is nominal, because the tile side already needs more than
its own share and overflows by construction.

The real arithmetic:

      tiles need           ~19-20 single groups
      sprites need           8 pairs = 16 groups
      total                 ~36 groups
      available                32

**It is a real over-subscription of about 4 groups.** Not a latency bug,
not a blocked reservation, not a packing failure. Three theories, three
measurements, three retractions — each one narrowed it correctly, and
each one was declared solved a step early.

### The one side never measured

Every compression test so far has been aimed at SPRITES, and all of them
came back dead (exact dedup, position-wise, used-pen). **The TILE side
has never been tested at all**, and it is the bigger consumer — 19-20
groups against the sprites' 16.

§11 already proves tiles compress on the MD side: *"21 distinct S16
colour sets but only 36 distinct MD-quantised colours"*. The 32X
`tile_grp` side gets no such treatment — every tile colour gets its own
8-entry group.

**NEXT: point `tools/palpack.py` at tiles.** It already has the shape;
it needs the tile colour source (`PAL_SH` regions 0..7 rather than the
sprite block at +1024) and tile pen usage (`tcount`/the tile art). If
tiles compress 20 -> 14, everything fits and nothing else has to change.
If they do not, the honest options are colour-level sprite packing with
a per-pixel remap (97 colours -> 7 pairs, priced in the hot loop) or
accepting the sharing and choosing WHICH sets share by visual similarity
rather than by allocation order.

`GRPRELOC` stays as a flag, OFF, documented as measured-ineffective so
nobody rebuilds it.

## THE GRAVESTONE YELLOW FLASH IS THE ARCADE. NOT A BUG.

Mike: *"I haven't captured the MAME source. I don't know if the black and
yellow flashes are actually written in intentionally."* He was right to
ask, and the answer is that at least half of item 6 is not ours.

`mame altbeast` coined up to round 1 (arcade ports are `Coin 1` and
`1 Player Start`, NOT the 32X build's `P1 Start`), sampling the
gravestone rect x 80-115, y 120-175 every third frame:

      g_00564  SOLID YELLOW (247,247,0)  86.7% uniform
      g_00573  SOLID YELLOW              86.5%
      g_00588  SOLID YELLOW              86.7%
      g_00591  SOLID YELLOW              87.7%
      g_00597  SOLID YELLOW              86.5%
      g_00612  SOLID YELLOW              86.7%
      g_00621  SOLID YELLOW              86.5%

**Seven solid-yellow gravestone frames in a 70-frame window on the REAL
MACHINE**, strobing with the lightning during "I COMMAND YOU TO RISE
FROM YOUR GRAVE". Our yellow flashes are correct.

**The black flashes are NOT confirmed either way** — none appeared in
this window, but the sample is every third frame over 70 frames. Needs a
denser capture before being called ours.

### WHAT THIS COSTS THE REST OF THE ANALYSIS

**The oracle was available the entire loop and never asked.** CLAUDE.md
opens with `mame altbeast` being the ORACLE and "always valid", and this
job spent four attempted fixes on a palette system partly to remove an
artifact the arcade also produces.

**Before treating any remaining colour item as a defect, capture the
arcade at the same moment.** That applies directly to:
  - item 8, white wolf "wrong sprite shade"
  - item 9, gravestone purple during vertical movement
  - item 10, right-hand enemy "wrong colour palette"

Each is a colour judgement against remembered arcade appearance, and the
gravestone shows how unreliable that is when the arcade itself does
something startling. `tools/parity_run.sh` already captures both
machines scene-anchored; the gap was never tooling, it was not looking.

**This does not exonerate the palette system.** `pen drift` is real,
TILEDEDUP genuinely made it 17x worse, and the one-cycle-late pair map
is confirmed in code. But the SIZE of the visible problem it causes is
now unknown, because some of what was attributed to it is the game.

## TILEDEDUP IS A REGRESSION ON ARES. DROPPED.

**Mike's pass on K_tilededup, against J:**

                          J (canonical)   K_tilededup
      pen drift                215           3,760     17x WORSE
      flip/blit skips           0.1%           4.8%    48x WORSE
      handler mean              82.5           85.5
      band drops/cycle          0.52           0.75

**The verdict rests on those counters ALONE.** A first draft of this
section claimed Mike's punch list corroborated it — item 6 going from
three flash episodes on J to fourteen on K. **That was wrong and is
retracted: he simply logged more thoroughly the second time.** A change
in REPORTING granularity is not a change in artifact RATE, and treating
one as the other is the same class of error as reading a MAME structural
win as a hardware win. Two independent-looking signals, one of them
manufactured.

**Two costs that exist only on hardware, and MAME showed neither:**

  1. **Sharing is unstable under fades.** Two sets share a group while
     their palettes match; a fade separates them and the group must
     split — every cycle, in a game that fades constantly. That churn IS
     the pen drift. The static analysis counted 10 distinct palettes at
     one instant and never asked how long they STAY identical.
  2. **The comparison is not free.** Up to 20 groups x 8 words per
     colour needing a group, inside `build_maps` — already ~4ms and
     uninterruptible. Band drops 0.52 -> 0.75, skips followed.

**The structural finding stands and the fix does not.** Tiles really do
hold only ten distinct palettes at an instant; sharing by that identity
really does clear the squatters. It just costs more than it frees, on
the machine that counts.

**TILEDEDUP joins GRPRELOC, PAIR_HOLD and the late claim: flag kept,
OFF, measured-ineffective.** Four for four in this job. J_shift16 stays
canonical.

**And the lesson is one this loop keeps re-teaching:** MAME ranked the
STRUCTURE correctly (squatters 6 -> 1 was real) and could not price the
DYNAMICS. A palette scheme has to be judged over time, not at an
instant — the harvest already had 899 cycles of it and the analysis only
ever looked at one cycle at a time.

## (SUPERSEDED — see above) TILEDEDUP LANDS. The other two do not.

`tools/palpack.py` pointed at TILES — the side never tested, and the
bigger consumer — found the slack immediately, over 899 harvested cycles:

      exact 8-entry dedup                 17,536 -> 5,298   (3.31x)
      position-wise sharing (all 8 agree) worst 10 groups
      distinct colours at peak            62 -> 8 groups with a remap

**The live tile colours hold only TEN distinct palettes at the worst
cycle, and the allocator was spending 19-20 groups on them** — because
it claims a group per COLOUR SET, not per palette. `make TILEDEDUP=1`
points a colour at a group that already holds a byte-identical palette
and claims nothing. No remap (the colours ARE the same), no
approximation (unlike the `shared_tile` fallback), nothing in the draw
path.

                                  OFF        ON
      tile zone groups used        19        15
      squatters in the pair zone    6         1
      colours sharing a palette     0    23,727

**Squatters are essentially gone** — the thing three previous theories
were aimed at. Renders correctly in MAME; shipping unaffected
(`_end 0x06018138`).

### What did NOT work, measured

  - **`GRPRELOC`** (relocate squatters below bound): 1 relocation in
    1,915 attempts — the tile zone was full, so there was nowhere to go.
    TILEDEDUP is what actually emptied the pair zone.
  - **`PAIR_HOLD`** (release a departed set's pair after 2 cycles
    instead of 90 once demand reaches the reserve): no measured change.
  - **The late claim** (`SPR_LATE`): 35 of 583 missed sets claimed.

All three stay as flags, OFF, documented as measured-ineffective.

### Where it still stands, honestly

      sets wanting a pair    9
      got their own          6
      forced to share        5
      pairs reserved         9
      held by ABSENT sets    6

Sprite capacity did NOT move. With the squatters cleared the binding
constraint is now the reserve SIZE itself: 9 pairs against 9 sets with
any churn at all starves somebody, and 6 of the 9 are held by sets not
currently on screen.

**The next lever is the same one, tuned further.** The offline analysis
says tiles need TEN groups; the runtime achieves fifteen, because dedup
only matches against groups that already have an owner while the
sticky/steal paths keep spreading claims. Closing 15 -> 10 frees five
more groups, which is 2-3 more sprite pairs — and `need`'s clamp
(`nspr < 6 ? 6 : nspr > 10 ? 10 : nspr`) can then afford to reserve
them. That is arithmetic with headroom for the first time in this job:

      tiles 10 + sprites 20 (10 pairs) = 30 groups against 32

## THE REST OF THE LIST

  - **Items 1, 2 — "slow load in" for the splash and the first level.**
    **NOT a CUTBLANK regression — Mike: "it's always been visible, I
    just decided to finally start sharing it."** An independent,
    long-standing defect, and CUTBLANK was never hiding it. This is the
    cut drain itself: cells hold the previous scene's art while uploads
    land. The drain is transport-
    bound (LOOP14: 792 claims in 28 windows, MD_BATCH=40, the 17-word-
    per-tile payload fills the packet). Making it faster means a bigger
    SH-2 -> MD tile packet — a real design item, not a tuning knob.
  - **Item 3 — Zeus during scale-up: missing sections AND inverted
    colour.** Two defects in one: the colour half is the pair-15 class
    above; the missing sections are the zoomed-sprite path, which cannot
    use SPRBAKE and is the most expensive thing the compose does.
  - **Item 6 — tearing on the player and scaled enemies.** BANDSHIFT
    took band drops 0.97 -> 0.53/cycle, not to zero. Shift 32 reached
    0.26 but starved the slave (skips 4.6%, V-gate rejects 5.7x). The
    next move is not more shift: it is moving a MASTER-ONLY duty to the
    slave — `build_maps` is the candidate (already chunked under
    BQCHUNK, pure computation, touches neither the FB window nor CRAM).
  - **Item 11 — right-seam panel. LOCALISED, frame 2701 of the J
    capture: screen columns 309-318, densest 312-313, rows 72-102.**
    That is the LAST VISIBLE TILE COLUMN (39), and the mechanism is the
    tile MISS POLICY, not overdraw geometry.
    The FG pass draws 41 columns (c = 0..40) from `drow + (8 - xf)`, so
    column 40 lands at sbuf 328-xf .. 335-xf — fully in the pad only when
    xf == 0. At any other scroll offset part of it falls INSIDE the
    visible area, at screen 313-319. And that column is the one scrolling
    IN, so it is the likeliest cache miss. On a miss:

        if (tpx == blank_tile)
            continue;            /* miss: keep last frame */

    **"Keep last frame" is right mid-screen and wrong at the leading
    edge**, where last frame's content is guaranteed to be a different
    tile. It persists because fresh tiles keep arriving there.
    Fix candidates: at the incoming edge draw the MD-through value (0)
    on a miss instead of keeping stale pixels — a blank column reads as
    "not loaded yet", stale art reads as corruption — or prefetch the
    incoming column a frame early. Cheap either way; independent of the
    palette work.

## WHAT LOOP18 SETTLED — do not re-run

  - The blit is **throughput-bound**; partial removal returns
    sub-linearly. Only whole rows pay. BLITSKIP and DIRTYROW have taken
    what that road offers.
  - LOOP 9's "80% of the blit is an FB-write bus-stall floor" and its
    "~5x cheaper SDRAM read" are **RETIRED** — measured false.
  - **MD hardware sprites are dead on PALETTE, not geometry.** 92% of
    scanlines fit the sprite chip; those sprites would need MD palette
    lines a failing background pack already owns.
  - **Raster palette swapping does not help this title.** 8 swaps take
    the worst span from 11 sets to 8 — a brawler puts every actor on the
    same ground line, so the demand is horizontal.
  - **SPRBAKE is neutral** (three measurements). It fits and is
    pixel-gated, so it stays; it is not a lever.
  - The tear is at COMPOSE BAND boundaries (rows 72/144), which are the
    master/slave row boundaries — not the k1/k2 blit boundary (112).
    **Flip-after-blit is dead as the tearing fix.**

## TRAPS CARRIED FORWARD

  - **Build the control before reading the picture.** A SPRTRUNC build
    cannot be pixel-judged in MAME, and LOOP18 attributed a known
    emulator artifact to a new change hours after writing that rule down.
  - **A defect metric that will not move under speedups is an
    asymmetry**, not a throughput deficit.
  - **Check what a constant was calibrated against.** MD_BATCH=40,
    §11's "36 colours in 45 pens", DIAG[25]'s row counts and
    `rowslot()`'s ranges were all measured against a configuration that
    later changed underneath them.
  - **Verifiers must be falsified before their zero means anything.**
  - Region guard: 2,776 bytes spare at LOOP18 close. Do not spend it
    without measuring what is actually in `.bss` first.
