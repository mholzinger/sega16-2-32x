# The _m_main split: measured baseline (2026-09-16)

`make line` (bldS-equivalent). ares `--profile`, master SH-2, steady
state. All figures are DIFFERENCES between two runs, so boot and
scene-load code are excluded.

## The per-generation working set, which is the number that matters

The cache does not care about a 200-frame union; it cares what one
generation touches. Windowed from frame 1800:

    window        frames   lines     bytes   vs 4 KB cache
    1800-1802          2     891    14,256       3.5x
    1800-1804          4     924    14,784       3.6x
    1800-1810         10     963    15,408       3.8x
    1800-2000        200   1,366    21,856       5.3x

**Two frames already touch 14,256 B and ten frames add only 1 KB.** So
~14 KB is the PER-GENERATION working set, not an artefact of unioning.

**THIS CORRECTS MY OWN EARLIER READING.** I reported a "1% tail" of
13,168 B from the 200-frame union and proposed moving it out of the hot
path. There is no such tail: at a 2-frame window essentially all of it is
already touched, so every branch runs at least once per generation. There
is no cold region to separate. That is why the footprint card as written
(separate hot from cold) has little room.

## Where the 14,304 B goes

    function              bytes    %WS   instr%
    _m_main               9,808  68.6%   80.66%
    _flip_span              816   5.7%    2.10%
    _mdspr_claim            624   4.4%    0.35%
    _blit_half              496   3.5%    3.37%
    _visr_vbi               400   2.8%    9.67%
    _disp_gate              288   2.0%    0.03%
    _cram_paint             192   1.3%    0.35%
    _bm_scan_baked_ok       192   1.3%    0.37%

`_m_main` is 68.6% of the working set and 80.7% of the instructions. Any
split that does not cut into _m_main cuts into 31% of the problem.

Three functions are cheap in work and expensive in footprint:
_flip_span, _mdspr_claim and _disp_gate are 12.1% of the working set for
2.5% of the instructions. They are already out of line; they are resident
every generation because they are CALLED every generation.

## What this says the lever is

Not relocation -- `.text` is the cached cart view, so moving code out of
`.ramtext` changes no cache behaviour (measured: touched bytes went
25,552 -> 25,568 across three such moves). Not hot/cold separation --
there is no cold. The only thing that reduces a 3.5x oversubscription is
EXECUTING LESS DISTINCT CODE PER GENERATION, which is an algorithmic
change to what the generation does, not a layout change.

ARCHITECTURE.md's S4 pivot is the one thing on the books that does that:
it removes the work AND the code that does it.

## What the 14 KB IS: tile/plane work, not sprites (2026-09-16)

The decompile thread's three-way test, run. Bucketing _m_main's
per-generation PCs by source line against a line-mapped build:

    src lines      bytes    %WS   instr%   what
    3200-3249        400   3.0%   27.63%   bm_scan_rows -- "scan nr
                                           TILEMAP ROWS of (which, aset)"
    15300-15849    1,872  14.2%   ~18%     the NAME-TABLE pass in m_main
    3497-4100      1,200   9.1%    ~9%     bm_tail_body, the band tail
    4507+            256   1.9%    0.53%   apply_cram
    2004/2061/3107   ~1.5K 11%     ~2%     frt, diag_add, decode_pages

**And the sprite compositor is not in the master's per-generation set at
all:**

    _compose_pass         no
    _slave_concurrent_k   no
    _compose_sprites      no
    _spr_draw             no

Because compose runs on the SLAVE. The master's 14 KB is tilemap
scanning, map building, the name-table walk and shipping.

**So the answer is the thread's second branch: still tile/plane work, and
there IS room.** S4 moved the PLANES to the MD VDP -- and the master
still walks the tilemap every generation to decide what to ship. The
single hottest thing in the whole master is bm_scan_rows at 27.6% of
_m_main's instructions in 400 bytes, and its own comment says it scans
tilemap rows.

That is not a contradiction of S4 being on the line. S4 moved where the
pixels are DRAWN. It did not move the per-generation decision work that
feeds the MD: which sets are needed, which cells changed, what to ship.
That work is proportional to the tilemap, it runs every generation, and
it is most of the master's footprint.

**The named target is therefore the tilemap scan/ship path, not a layout
change and not the sprite path.** Whether it can be made incremental --
scanning only what changed rather than the viewport every generation --
is the question a card should ask. bm_scan_memo (3177) and the TAGKEEP
"land where you were" path (2770-2790) suggest that idea already has
partial machinery here.
