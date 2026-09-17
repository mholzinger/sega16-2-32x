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

## CORRECTION (2026-09-16): note 115 measured ATTRACT, not gameplay

Re-run with `--input discover/inputs/play2.csv`, 2-frame window at f3000:

    gameplay working set   946 lines = 15,136 B   3.7x cache
    bm_scan_rows           0.00% of instructions  -- IT DOES NOT RUN
    bm_scan_baked_ok       present, eligibility PASSES

**SET_COLS already replaces the tilemap colour-set scan in gameplay, and
it fires exactly where it matters.** The 27.6% I attributed to
bm_scan_rows was an ATTRACT artefact: bm_scan_baked_ok requires
MD_STATE_PLAY, or step 1/3/5 with bit 8, and attract fails that, so the
live scan runs there and only there.

So the decompile thread's "make the scan incremental" card is ALREADY
BUILT, ships on the line, and works. Costing it would have been the
second card spent twice in two messages.

## The real gameplay hot path

    src lines      bytes    %WS   instr%   what
    11350-11399      224   1.6%   16.90%   the maps drain --
                                           build_maps_chunk call site
    15450-15499      496   3.6%   10.84%   NAME-TABLE pass: per-row
                                           tilemap page pointers
    15600-15649      336   2.4%    6.96%   NAME-TABLE cell walk: code
                                           extract, bank fold, cset,
                                           allocator entry
    15300-15849    1,792  13.0%    ~21%    the name-table pass entire

The master's gameplay cost is the NAME-TABLE WALK plus the MAPS DRAIN --
walking every viewport cell each generation to decide what to ship to the
MD. That is tilemap-derived decision work and SET_COLS does not cover it;
SET_COLS answers "which colour sets does this viewport hold", a different
question from "which cells changed and what must ship".

**And the thread's camera arithmetic applies to it unchanged**: at most
0.5 px/frame, so at most 1/16 of one column of new cells per generation,
against a full 40-column walk. The partial machinery is visible in the
same block -- nt_key / nt_gen / nt_win at 15455-15459 already memoise per
row index.

That is the target, and it is a different function from the one note 115
named.

## NT_SKIP: BUILT, MEASURED, KILLED -- ON ARES, BEFORE THE CRITICAL-PATH
## CORRECTION (2026-09-16)

The memo the thread pointed at is `NT_SKIP`, and it is NOT on the line.
It is not an omission: LOOP29 177 (2026-09-11 18:20) killed it.

That entry is worth reading in full. Its own "91.4% skipped" was a
counter collision -- NTS at 0x28FD0 overlapped the band-deferral block --
and read correctly the flag was doing NOTHING (0 skips in 23,800 walks),
for two reasons since fixed in the source: tm_gen was global, and
NT_MAXAGE could never be satisfied. With both fixed:

    NTMAXAGE=64    63.9% skipped     21% single-vint
    NTMAXAGE=200   67.5% skipped     21% single-vint
    (zero skips)    0.0% skipped     20% single-vint

"Skipping two thirds of the name-table walk buys ONE POINT."

**But measured in gameplay, the name-table walk is 34.45% of the master's
instructions** (2-frame window at f3000, 186,886 of 542,462), and
build_maps_chunk -- which 177 names as "the real 0.44" -- is **0.03%**.
So 177's closing attribution is backwards for gameplay.

**And the ranking that killed it was an ARES ranking.** On ares the wall
is max(echo, mtask) and echo wins, so the SLAVE reads as the critical
path and removing master instructions cannot move the number. On the rig
the master never waits for the slave, so the MASTER is the critical path.
That correction is LOOP29 294, dated 2026-09-14 -- THREE DAYS AFTER 177
killed NTSKIP on the earlier understanding.

So NTSKIP was ranked on the machine where its target is off the critical
path, and buried at one point. It deserves re-ranking on the rig, which
is the only instrument that prices master instructions at all.

Everything it needs is already in place: the flag exists, both of 177's
bugs are fixed in the source, NT_MAXAGE is bounded (which addresses the
unbounded-skip corruption Mike saw on vi20), and NBUILD1 -- the brake for
the transport flood NTSKIP causes -- is already ON the line.

    make line NTSKIP=1 NTMAXAGE=64 BOOTFLIPRATE=1

against the same flag set without NTSKIP, ranked on the rig's presented
frames per 64 vints. That is the card.

## NTSKIP RUN (2026-09-16): the key fix is real, the skip is NOT CORRECT

Gameplay (`--input play2.csv`), NTS read from the verified 0x28FD8:

    rom                          skipped   walked   skip rate
    NTSKIP,        MAXAGE=64      12,096    8,036      60.1%
    NTSKIP+KEY8,   MAXAGE=64      17,080    3,080      84.7%

**The decompile thread's ~26 points were there and the cause was the key
itself, not allocator churn.** It folded the FULL 10-bit `vxr`, so a
ONE-PIXEL camera move re-keyed a row whose cells are identical -- the
cells a row reads are a function of `vxr >> 3` only. Keyed on `vxr>>3`:
60.1% -> 84.7%, against the 93.75% the 8-pixel camera bound allows.

**And then the pixel gate failed.** Black share against the line's
4.8% / 4.4% at f2800 / f3100:

    MAXAGE   skip rate   black f2800   black f3100
      4          0.0%          4.9%          4.7%
      8          0.0%          4.9%          4.7%
     16         47.6%         14.0%         13.7%
     64         84.7%         10.3%          --
     64 (no KEY8) 60.1%       --            20.3%

**Black tiles scale with the skip rate.** At 0% skip the picture matches
the line; at any nonzero rate it drops tiles, which is exactly the
corruption Mike saw on vi20. MAXAGE <= 8 giving 0.0% also confirms
LOOP29 177's age arithmetic exactly: a row is revisited once per
eight-phase walk and win_no advances per window, so the age at revisit is
8-16 and a bound of 8 can never be satisfied.

**Cause, and it is the gap the decompile thread named:** m_main.c:2309 --
a cell's shipped pattern depends on its set's (line, pixel->pen map). The
row key folds scroll, pages and per-page CONTENT generations. It does NOT
fold allocator state. When a set is re-assigned or its pen map changes,
every skipped row keeps a stale slot reference and those cells go black.
The thread's hypothesis was that allocator churn RE-KEYS static rows; the
measurement says the opposite and worse -- allocator churn SHOULD re-key
those rows and does not.

**So NTSKIP cannot be ranked on the rig yet.** A flip rate from a rom
that drops tiles prices a machine doing less work than a correct one.
The key must cover the allocator first, and doing so will cost back some
of the 84.7%.

## THE TWO COUNTERS (2026-09-16), MDALLOCWHY=1, gameplay f2600->f3400

    [ 0] cells reaching allocator     59,556
    [13] assign_set entered              665
    [14] wipe_set_tags                 3,699
    [19] burned set claiming a pen       218
    [20]  ... with NO exclusive pen   23,038
    [21]  ... and a shareable match        0
    [22] TAGKEEP deferred wipe SAME        0
    [23] TAGKEEP deferred wipe MOVED      26
    [24] free_set                      1,129

**TAGKEEP NEVER WINS. [22] = 0, [23] = 26. One hundred per cent MOVED.**
Its whole premise is that a re-assigned set usually lands back on the
same (line, pen map) so the wipe can be skipped. It never does. That
reproduces LOOP29 155's own "0 of 59" on a different build and a longer
window -- so TAGKEEP defers a wipe that is ALWAYS needed, and pays the
deferral's staleness window for nothing.

**Set re-assign rate: 665 in 800 frames**, ~0.83/frame. That sizes the
NTSKIP key: only rows holding a re-assigned set lose their skip, and at
0.83 assigns a frame against 56 rows, most of the 84.7% should survive a
per-set generation fold. The decompile thread's push design is the right
shape and the number supports it.

**And the pen supply is starved, which is new.** [20] says a burned set
found NO exclusive pen 23,038 times in 800 frames -- 28.8 per frame --
and [21] says a shareable match was found ZERO times. Every one of those
falls to nearest-colour. 3,699 tag wipes in the same window is 4.6 a
frame, and a tag wipe is tile destruction.

**That is a named, measured cause for the line's 4.8% black**, and it is
the same shape as the NTSKIP corruption: a slot reference outliving the
set's pen map. NTSKIP makes it worse by adding skipped rows; it does not
create it.
