# The old MD plane pipeline's rules, inventoried for the r60 feed

Written 2026-10-15 by the advisor thread after Mike's correction ("you ALREADY had
backgrounds working on the 68k pipeline ... why?"). An Explore agent read the old
implementation and the record; every rule below carries the file:line that holds it
and the measurement that established it. Section 6 says which rules r60_planes.c had
at the time and which it lacked; the pen pack (commit c12f0151), the plane bit in the
hash and the rounding quantiser were adopted from this list the same day.

Read this as EVIDENCE, not as a checklist. It protects against one failure class, a
fact the old tree stated and the new feed never read (the cached window at
m_main.c:3119-3124, the opaque background). It can make the other class worse: a value
carried faithfully from a context where it no longer means the same thing
(MD_BLANK_SLOT=1023 in r60, 2026-10-14). Before carrying a value across, re-ask what it
means in the new pipeline; before skipping a rule, find the measurement that retires it.

Files read: sh_src/m_main.c (the old builder; `sh_src/native1/m_main.c` does not exist,
r60's citations to "native1 m_main.c" point here), tools/bake_tiles_md.py,
tools/bake_tilecram.py, md_src/md_main.c, docs/design/SLIM-PIPELINE.md, LESSONS.md,
FACTS.md, STATE.md, Makefile.

Flags the old pipeline ran with: ship flags Makefile:4069-4074 (TILESLIM=1 SLIMCAP=80
SLIMWORDCAP=1 MDBATCHBLANK=80 BGBOTTOM=1 ARTTAIL=1 CUTBLANK=1 NTWRAP=1 MDSCENES=1
CUTPREFETCH=1); line flags Makefile:4109-4115 (CAT1MD=1 C1NOFB=1 MDBATCH=24 MDROUND=1
MDSREFUSE=1 TILESMD=1); the port adds MDLINES4=1 MDSPROFF=1 MDATTRACTSPLIT=1
(Makefile:4199-4201).

## 1. Art baking and pens

- FG pixel 0 always goes to pen 0 (transparent). BG pixel 0 goes through the set's map,
  `map[0]`. m_main.c:12344-12357; bake_tiles_md.py:52-65 ("the EXACT body of
  md_emit_art"), docstring :27-29. Evidence: "941 of 941 slots byte-identical" against
  live VRAM (bake_tiles_md.py:31-34; LESSONS.md:497-499).
- FG claims never spend a pen on pixel 0: `mask &= 0xFE` m_main.c:4059-4060, reason
  :4013-4017 and :768-775. S16 art leaves garbage in arcade-invisible entries ("set 76
  pixel 0 is literal magenta"); claiming all 8 pixels overflowed the lines and gave real
  sky colours "nearest-fallback DARK pens".
- Both FG and BG variants are baked for every set in rounds 0-4; single-variant blocks
  only for harvested scenes. bake_tiles_md.py:88-95, layout :149-157. Incident
  LESSONS.md:1384-1399: the boss gate drew sets 81/83/84 with the BG variant on the FG,
  so colour 0 (4,0,7) showed as purple. "A bake that drops a variant needs proof the layer
  never uses it."
- The sky fix: BG colour index 0 gets a real pen; MD pen 0 stays reserved and unused.
  bake_tilecram.py:271-280 (harvest `range(0, 8)`), :600-605. Incident STATE.md:2054-2055
  "BG sets 92/93/95/96/97 resolve pixel 0 to 0x01EC instead of the backdrop", commit
  3c4cd053; before it "every index-0 pixel of a BG set drew the backdrop: the black sky
  band". LESSONS.md:466-490 (2026-09-17) said the opposite ("map[0] MUST stay 0"); the fix
  was reverted once (baa33165: pen tables without runtime refcounting gave black tiles,
  broke the wolf) and re-landed as tilesmd4. baa33165 also measured that pixel 0 in the
  backdrop register cannot work: "6 distinct index-0 colours across the 30 pinned sets,
  and there is one backdrop."
- S16 colour 0 is arcade-invisible garbage in some sets (0x680E purple): LESSONS.md:
  6047-6055. Whatever displays pen-0 pixels shows it.
- Two sets per line was a census comment only (m_main.c:4019-4025); the shipped
  allocator packed per COLOUR: exact-match share, then a free pen, then nearest
  (`mdp_claim_pen`, m_main.c:3855-3917), with an 8-entry pixel-to-pen map per set
  (:622-633). Pen 8 as a set's colour 0: not found in the old pipeline.
- Near-merge of dither pairs is OFF; merging at assignment time breaks fades
  (m_main.c:3977-3995: "transformation chevrons went flat, the level load-in never
  reached its palette").

## 2. Palette lines and the backdrop

- Three tile lines (MDP_LINES 3, lines 1-3, 15 usable pens each), line 0 the grey/text
  ramp; the port uses four (MDP_LINES4) once sprites leave the MD. m_main.c:643-658;
  Makefile:4201; md_main.c:2282-2291. Worst visible BG window: "at most 21 distinct S16
  colour sets but only 36 distinct MD-quantised colours" (m_main.c:623-626). "Level 1
  (round 0) keeps all 30" pinned sets; rounds 1/2/4 lose 12 to overflow (STATE.md:
  2067-2068).
- Two lines do not hold level 1: LESSONS.md:1743-1759 ("34 pens holding 33 DISTINCT
  colours at every frame"; two lines overflow by 3-4 sets = 208-250 of 2080 cells).
- Pen colours re-send from live PAL_SH every window; only the grouping is static, so
  fades track (m_main.c:629-632).
- The quantiser ROUNDS (+2 then >>2, clamp 7); truncation is wrong. m_main.c:3595-3602,
  3625-3634: "the stage-1 sky is a 5/6 checker ... truncation renders a straddling pair
  at 4x its arcade contrast", plus global darkening. The bake must use the same function
  (bake_tilecram.py:99-111, LOOP29 196: a truncating table "disagrees with the runtime
  on 412 of the map-referenced pens" = the pink trees; gate `make quantcheck`,
  Makefile:4116-4129). PALQUANT (DAC-ramp) rejected on the rig: "darker than the
  cabinet on the FPGA" (FACTS.md:122; m_main.c:3603-3623).
- The 9-bit pack and the CRAM word are different encodings; CRAM is `0000BBB0GGG0RRR0`
  (bake_tilecram.py:114-125; white (7,7,7) read back as (6,5,3) = the pink trees).
- CRAM entry 0 (the backdrop) is never written by the tile palette block: 48 words to
  CRAM 16-63 (`0x9330`, `0xC000|32`); line 0's 15 words go to entries 1-15
  (md_main.c:2282-2291). The backdrop is black on purpose (m_main.c:19052-19054;
  FACTS.md:436).
- The palette DMA runs in vblank only (V >= 0xE0), else held in WRAM for the next vint
  top (md_main.c:2276, 2317-2320, 4983-4995). PALFIRST puts it before any tile work
  (md_main.c:1469-1476): "a deferred palette ON THE FPGA showed as a black level
  background".
- The fourth line's palette field wraps (`& 3`): `4 << 13` sets the VDP PRIORITY bit
  (m_main.c:19285-19289).
- How sets were assigned: per-round/per-scene baked tables (MDROUND/MDSTATIC,
  mds_install); a set outside the scene's table is REFUSED a line and drawn as backdrop
  (MDSREFUSE, m_main.c:4067-4112). The table must cover every page the level walks
  ("player standing on a ledge drawn as background", bake_tilecram.py:145-156). The
  attract needs two tables: demos and static screens use disjoint set families
  (m_main.c:2333-2352; "2,126 live plane cells contributing half a point of picture
  while the arcade draws 49.1%"). A wrong scene map gave a black sky on the second
  level-1 demo (m_main.c:2289-2294; LESSONS.md:1170).

## 3. The slot allocator

- Geometry 128 sets x 8 ways = 1024 slots; md_tag [1024] longs at 0x0603B400, md_ref
  [1024] bytes at 0x0603B000, md_dirty at 0x06028EC0 (m_main.c:531-545, 570, 581, 619).
  2-way thrashed the round-1 sky gradient into "black placeholders" (:522-525); 4-way
  churned three animation-frame code families (:526-530).
- The key includes the colour set and an FG bit: `(cset<<16)|code | isfg<<31`
  (m_main.c:597, 19176-19177); the pattern is pen-remapped per set (:578-580).
  LESSONS.md:4171-4180: slot pressure = number of distinct pen maps; "identical pen maps
  let tiles share a slot".
- The set hash must include colour set AND plane: `MD_SET = code ^ code>>7 ^ cset<<1 ^
  (isfg?0x2B:0)` (m_main.c:3563). LESSONS.md:3330-3352, 3404-3420: hashing the code
  alone put a tile's FG and BG variants in one set; the demo frame needed 614 distinct
  keys; the plane-aware hash gave "0 black band cells at wc4's cost". "Fix the
  systematic collision before buying capacity."
- Two-choice sets (MD_SET2) are OFF on the line (m_main.c:3565-3569, :3183-3190): on the
  FPGA a hit in the alternate set costs four cache lines; wc6/wc7 read 336 lines against
  the control's 316 (LESSONS.md:3364-3402).
- Eviction picks the oldest way by md_ref; on-screen slots are re-stamped by the
  name-table pass "at least every 5 windows" (m_main.c:612-618, 19232-19234,
  19256-19283). "First-come, no eviction" failed: the title's ~1120 cycling codes filled
  all 1024 slots and pinned dead codes (:612-616).
- Slot-pressure refusal: a cat-1 FG cell does not evict a victim younger than 12 windows
  (`C1_SOFT && vage < 12`, m_main.c:19236-19254; DIAG[39]): "cut-blanks 3.9/gen vs
  shipping 0.8/gen". Pressure: "gameplay runs 1018/1024 slots and 123/128 sets full,
  attract 499 and 0" (m_main.c:1108-1109; FACTS.md:115; LESSONS.md:4193-4206,
  4432-4447).
- The blank slot is reserved and never claimable (1023); its dirty bit can never be set
  (m_main.c:3047, 19147-19150, 19201-19204, 19313-19315).
- A claimed-but-dirty slot ships the BLANK, not its stale art (CUT_BLANK,
  m_main.c:19308-19344): "foreign tiles: the red logo's litter, ref frames 294-310";
  keeping the previous entry gave "the dark logo".
- Pending honesty: an already-dirty slot counts once (m_main.c:19214-19224): "permanent
  pending=162 ... cell rotation at 1/3 speed = the banded reveals".
- Wiping tags leaves stale cells; re-mark instead of wipe, or bump the walk generation
  (m_main.c:3664-3687): "a wiped tag leaves every cell naming the slot showing the next
  claimant's art".
- Flush the slot map at the scene display gate; MDS_NOFLUSH was falsified: "41% of
  pixels wrong" (FACTS.md:207, 366). Cut install re-mark: "dirty at the switch 965 ->
  16" with cutpf4 (LESSONS.md:1420-1436).

## 4. Art throughput budget

- The packet body is 688 words with the palette at 688; at 17 words a record about 40
  fit (SLIM-PIPELINE.md:151-153; m_main.c:12199-12200). The MDBATCH sweep (24 -> 8.82
  tiles/vint, 48 -> 18.61, 96 -> 4.93) is VOID/RETRACTED (SLIM-PIPELINE.md:105-124): a
  clobbered, wrapping counter.
- Clean counter: "PEAK 40 tiles in one vint", mean ~1.07 tiles/vint; "black tiles arrive
  in BURSTS" (SLIM-PIPELINE.md:114-118; md_main.c:569-575).
- Batch size: MD_BATCH 12 default, 24 on the line, MD_BATCH_OFF 24 (m_main.c:3023-3041;
  Makefile:4113). Above the vint-top window "the DMA overruns vblank ... the load-in
  tear/purple-band cluster" (:3010-3014); "24 clears level 2's top-band tear that 40
  showed" (:3032-3037). At 12/vint a 1120-tile scene takes 93 vints (:3016-3022).
- 512 words/vint is the only proven 68K cart-to-VRAM rate (mdspr_upload_pump,
  md_main.c:504, 521) = 32 tiles/vint, "a BURST, not a steady state"
  (SLIM-PIPELINE.md:178-197).
- SLIM_CAP 8 by default (md_main.c:576-578), 80 on ship (Makefile:4073); SLIMCAP=40
  joined the line 2026-09-20 (Makefile:4075). The SH-2 emitter caps at the 68K's per-vint
  fetch: "a record it cannot stage is LOST art" (m_main.c:19476-19478; LESSONS.md:
  1152-1153). SLIM_WORDCAP stops on words, not records (m_main.c:12203-12225).
- Where the DMA sits: records copied in md_consume at the vblank top (FM=0); cart art
  fetched to WRAM; slim_dma "FIRST THING in partb_hook (after the game's IRQ4, before the
  FM raise) ... The only placement the FPGA accepts" (md_main.c:666-680, 1153-1195).
  Inline 17-word records (bit 14) are DMA'd from the FB at once in the consume
  (md_main.c:730-753). VRAM bound `va + 32 <= 0xB000` (md_main.c:741, 784).
- Art rides every cell chunk's tail (ART_TAIL): before it "a new column's slots showed
  their OLD art for up to 9 windows" (m_main.c:19456-19463).

## 5. Known defects and their fixes

- Level-1 black tiles, rows 24-27: the cat-1 slot-pressure refusal with C1NOFB=1 (no FB
  fallback); the backstop then blanks the BG, so both layers are blank (LESSONS.md:
  3316-3342, 4212-4240, 6025-6110). C1REFUSE_BG turned black into PURPLE (53 -> 0 black,
  13 purple; LESSONS.md:6033-6040). C1EVICT regressed (167 band black; :6095-6100).
  Parked; "the cell needs a drawer" (:6084-6086).
- Purple band: the backstop's opacity cache was keyed by tile code but the art depends
  on the bank; fixed by keying on the art index (m_main.c:1031-1058; "wc4 0 events in 72
  launches; wc2 1 in 36", LESSONS.md:3246-3282).
- Bottom-band backstop, BG rows 24-27 (m_main.c:18996-19066): blanks BG junk there
  ("solid-purple wall (tile 0x12D/pal1)", Mike's corpus 797-809); only where the FG word
  is nonzero (crystal ball); resolve the FG cell with the row's own rowscroll and page
  set; only where the FG tile is fully OPAQUE (BGBOTTOM, level-2 holes).
- Purple through the FG at the boss gate: single-variant bake (section 1). Sky checker
  and darkening: truncating quantiser (section 2). Black sky band: colour 0 not
  harvested (section 1).

## 6. r60_planes.c against this list (as of 2026-10-15 morning, before c12f0151)

Had: 128x8 slots at the carried addresses, the blank slot reserved (moved to 0 with a
reason); a key folding the colour set; oldest-way eviction plus never-age-0 and pins;
blank cell for unshipped art (CUT_BLANK); art on the packet tail (ART_TAIL) bounded by
packet free space; live PAL_SH re-send every B packet; CRAM word layout; FG pixel 0
transparent; a flush on plan change; a dynamic set table from page content.

Lacked (adopted the same day in c12f0151 unless noted):
1. The set hash ignored the plane (the LESSONS.md:3404-3420 collision). Adopted.
2. Two-choice always on; the old line kept it off for FPGA cost. NOT changed; rank on
   the rig.
3. The quantiser truncated. Adopted (rounds).
4. CRAM entry 0 was written with a set's colour 0; the old rule never wrote it and kept
   the backdrop black; one backdrop cannot carry six index-0 colours. Adopted (pen 0
   black, colour 0 a pen for BG sets).
5. At most 8 colour sets a scene (two halves a line) where level 1 needs 33 distinct
   colours; no per-colour sharing. Adopted (the pen pack).
6. Comments disagreed with the code (48 words/lines 1-3 vs 64 words/4 lines); the 68K's
   deferred out-of-vblank palette path still copies 48 words to CRAM 16
   (md_main.c:2323, 4983-4995): if a palette is ever deferred, r60's layout lands one
   line off. OPEN.
7. No young-victim refusal for priority cells, no VDP priority bit. By design (REBUILD
   drops cat-1); the arcade priority behaviour is not present. OPEN.
8. None of the bottom-band backstop rules; under an opaque BG the 0x680E garbage
   colour 0 can display. OPEN; watch for it.
9. No FB fallback for refused/blank cells (not applicable in r60).
10. The 68K per-vint fetch cap does not apply (inline records); the vblank-overrun rule
    still bounds it. Measured 2026-10-15: at 40 records a plane the consume ends by
    line 231-239 of 262.
11. Pins were not cleared on the plan flush (fixed the same day).

Instrument rule the builder asked to add here: a counter that counts the packet does
not count the reader. 9cf65304 (368 longs copied against a palette block ending at word
751) read zero in every dump and no counter saw it; the builder's night bug (an art
contract read out of an #else branch) counted 555 consumed batches. Verify a transport
with a dump of what the READER holds (VRAM, CRAM, the FB packet), never with the writer's
count.

And say what each gate is BLIND to. The luma spot check at five frames and the cell
census (VRAM + md_tag + tilemap) both read identical between sw60 and sw70 while the
30,000-frame soak read 0.86 -> 0.73 flips/vint: picture gates cannot see cadence, only
the soak can, as check-demo cannot see palette RAM and a consumed-batch counter cannot
see VRAM. A gate that is green for a thing it does not measure is the week's commonest
failure (three cases, 2026-10-14/15).
