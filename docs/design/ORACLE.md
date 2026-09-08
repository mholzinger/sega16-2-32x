# ORACLE — the frame-true arcade reference and what it measures

2026-08-31. Mike captured a full level-1 arcade playthrough with
`tools/ref_dump.lua` (interactive MAME, `screen:snapshot()` per
frame — pre-filter, native 320x224, frame-indexed from boot at 60Hz).
This is the first frame-for-frame grading target the project has had:
every prior comparison was our build against our build.

## The corpus

`ref_arcade/ref_000001..017528.png` — 17,528 contiguous frames,
coin-in through the boss kill and the crystal-ball round clear.
(`mamecap/` holds a screen-recording decode of the same session —
filtered and scaled; use the ref_arcade set for measurement.)

Scene map (luma-transition index, tools/oracle_grade.py era):

    ~3901          level 1 gameplay starts
    3901-10681     graveyard walk (wolf packs, gravestones)
    ~10681-10861   transformation-region transition
    ~13450-13900   NEFF ENTRANCE SMOKE  <- the acceptance scene
    13900-15300    boss fight (red silhouette phases, flying heads)
    ~15500         ROUND clear (golden pillar)
    16321+         crystal-ball cutscene

## Measured arcade truths (tools/oracle_grade.py)

    scene            change/frame (R0/R1/R2)   frozen pairs
    walk @9900       29 / 43 / 48 %            22 /  6 / 24 %
    smoke @13560      0 /  2 /  2 %            71 / 27 / 29 %
    boss  @14500     18 / 17 / 13 %             0 /  2 /  6 %

    walk scroll: 0.5 px/frame (1px every SECOND frame)

**The reframe these numbers force:** the arcade's own walking cadence
is ~30Hz-stepped (scroll advances every other frame; a fifth of frame
pairs are identical). NATIVE's 32.6fps whole-frame generation is
already AT the arcade's walking cadence — the walk is frame-for-frame
now. The smoke scene is slow on the arcade too (the bar there was
coherence, which whole-frame shipping wins by construction). **The
60Hz gap is one scene class: the boss fight (0-6% frozen pairs) and
fast sprite action.** "Match the arcade frame for frame" has a
measured, scene-shaped meaning now, and most of it is already met.

## Grading method

`tools/oracle_grade.py` — cadence (per-band change + frozen pairs),
scroll (x-correlation velocity), match (content-anchor search).
Anchor-match FIRST: build timelines shift (identity law — NATIVE runs
the attract +224 frames ahead of canonical at the same index), so
index-vs-index pixel diffs between builds measure the offset, not the
rom. Same instrument runs over ares screenshot bursts, so the arcade
and our build are graded by one ruler.

## The 60Hz arc — first negative, banked

**Master row rebalance (BANDSHIFT sweep): DEAD, measured 4 points.**
NATIVE gained a master row-compose program (mtask stage 1: clear/
sprites/cat1/text on the master's rows, row-partitioned ownership —
kept in-tree, no-op at BANDSHIFT=36). The sweep on the 1900f battery:

    BANDSHIFT/RG2   ships   wall      bad1
    36/40 (all-slave)  978   1.23v     87
    28/24              838   1.48v     64
    20/16              705   1.74v     62
    12/ 8              629   2.01v     55

Master rows cost MORE wall than they relieve: two CPUs fetching cart
art over one shared adapter bus lose to one warm CPU — the
parallel-bus law measured from the other side. (Side-signal worth
keeping: bad1 FALLS as the slave lightens — slave idle is what drains
landings clean, re-confirmed.) BANDSHIFT=36 stays canonical; do not
re-sweep without changing the bus economy itself.

**Where the 60Hz for the boss fight actually lives:** taking compose
work OFF the shared bus entirely — the P3 MD-sprite offload. Current
claim rate on NATIVE: 0.57 records/gen of ~3.6 (16%). The limiter is
not art budget, it is the ONE MD CRAM line (bake_mdspr.py v2 note:
other sets claim only on exact palette equality). The unlock is the
pivot's per-scene static palette directive — retire the BG pen
allocator's dynamic lines, and MD sprite palette lines multiply.
That is the next real arc: static tables -> more CRAM lines -> more
claimed classes -> slave compose shrinks -> the wall crosses ~0.65v
and the boss fight runs 60.

## Open items from this session

- NATIVE-timeline boss input script: play_level1.csv dies at the
  last life ~frame 10800 on NATIVE (RNG divergence; it collected 1
  orb). Re-derive against this build, or grade the boss scene from
  Mike's next ares capture.
- The zsh trap struck again: `make $CANON` with an unquoted scalar
  passed ONE argument and built a flag chimera (compile error in a
  nonsense flag combo, plus a stale .build_flags). Arrays only:
  `CANON=(...); make "${CANON[@]}"`.
