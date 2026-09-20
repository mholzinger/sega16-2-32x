# STATE — what is true right now

**Volatile document. PRUNE IT, do not append to it.**
Last updated: 2026-09-20.

If a line here is stale, fix the line. Do not add a newer line below it.
That is how the logs became unreadable.

---

## THE LINE

    rom/night/lineV.32x         THE LINE. Mike's play pass 2026-09-20:
                                "lineV.32x is playable! we have our
                                regular set of defects to solve for".
                                = tilesmd4/lineT + the boot-stack move
                                (md_start.s 0xFFBFF0 -> 0xFF3FF0, md.ld
                                guard) + two hook call sites that
                                assemble only under PARTB_HOOK. ares:
                                identical to lineT (718, 0 px, 0 VRAM).
    rom/s16.32x                 line-equivalent (stamp bytes only)
    rig (MiSTer)                lineV
    rom/night/slim18.32x        the slim pipeline candidate
                                (TILESLIM=1 SLIMCAP=40): runs on ares
                                and the FPGA (SLIM-PIPELINE.md 1b).
                                Play pass pending. 73% of its tiles are
                                unbaked and go inline.

    rom/night/lineT.32x         PREVIOUS line (== tilesmd4, Mike
    (== tilesmd4.32x)           2026-09-18). Superseded by lineV.
    rom/night/bldS.32x          the line before that (2026-09-14). Keep
                                for "was this always broken" questions.

INVARIANT CHECK: rom/s16.32x differs from lineV in the build string only:

    cmp -l rom/s16.32x rom/night/lineV.32x \
      | awk '{printf "%x\n",$1-1}' | cut -c1-4 | sort -u
    -> 3fff              (the 14 B build string at 0x3FFFD4)

The old `25f1 3fff` does NOT hold against lineV: the 68K image moved when
the boot stack did. `rom/s16.32x` must be left holding the LINE build at
the end of any session.

WHAT THE LINE CARRIES THAT bldS DID NOT
    - baked MD tiles (TILESMD=1 in LINE_FLAGS). 377 KB at cart
      0x263C00; md_emit_art copies 16 halfwords instead of 64 ROM
      reads + 64 pen lookups + 32 writes.
    - the sky fix: colour index 0 is harvested and packed, so BG sets
      92/93/95/96/97 resolve pixel 0 to 0x01EC instead of the backdrop.
    - Mike on the rig: sky correct, "the framerate feels more
      consistent". Consistency, not mean speed, is the expected shape
      of this change -- it cuts the TAIL (see the transport card).

KNOWN DEFECTS ON THE LINE, accepted as the price of the baseline
    - wolf transformation: one frame of the character, then flames and
      chevron only. PRE-EXISTING -- confirmed missing on bldS too
      (2026-09-18 screenshots). Not caused by the tile or pen work.
    - black tile pop-in, worst during the flame wipe (residency).
    - leftover text glyphs (four cells).
    - rounds 1/2/4 lose 12 pinned sets to overflow vs bldS; they fall
      to the framebuffer path. Level 1 (round 0) keeps all 30.

## THE AXIS — DEFECTS (Mike's call, 2026-09-17)

**Direction changed. Speed is PARKED; defects are the work.**

Mike: "we had a trajectory of moving the build to solve for the frame
updates, and that was useful. but since we have stalled this build for
over a week with no movement updates, might as well solve defects now."

The visual defects were being let through ON PURPOSE while the frame
rate was moving. It stopped moving, so the reason to let them through
expired.

**What this parks (not kills):** the transport ablation, O-1, and the
whole 60 Hz push. The findings below stay true and stay on the shelf:

    master compute ablated    84.7% of the name-table walk removed
                              -> FLAT on the rig
    slave compute ablated     ares' 58.3 fps was SLAVE work on a
                              slave-gated instrument
    transport, NBUILD1 off    0.33 fps against the line's 9.3 = 28x
                              -- a BRAKE REMOVED, not load reduced.
                              Whether reducing transport LOAD has a
                              slope is still the open question, and it
                              still needs rig time.

**The bar has NOT changed.** INTENT still says 60. This is a change of
what we work on now, not of what done means.

## MEASURED DEAD — do not re-propose without new evidence

    CACHELOCK             2 KB against a 14 KB working set
    footprint split       no cold region exists; a 2-frame window
                          already touches 14,256 B of 15,408
    relocation            .text is the cached cart view; three moves
                          changed touched bytes 25,552 -> 25,568
    NT_SKIP as a SPEED card   84.7% skip -> flat on the rig
    TAGKEEP               FALSIFIED: [22]=0 [23]=26, 100% MOVED, over
                          two independent windows. It defers a wipe
                          that is always needed. STILL ON THE LINE —
                          removing it is an open card.
    MDCHEV                packing the chevron sets changed no pixel
    static bake for runtime-allocated sets   structurally no path
    triple buffering      the pipeline is already full
    re-timing generally
    SH2_CCTL_TW (card T2)
    MDS_NOFLUSH           FALSIFIED 2026-09-17 same day. Dropping
                          mds_flush at the display gate: claims
                          2765->2144 but evictions 499->1022 (no net
                          win) and 41% of pixels wrong. The evidence
                          was an ORDERING ARTIFACT -- the flush runs
                          first and empties the tags, so install's
                          changed[] "wiping only 2" measured nothing.
                          Without the flush it wipes 1023/1024. The
                          flush also clears md_ref/md_dirty, which the
                          install path does not.
    NT ship-skip (NTHASH)  FALSIFIED 2026-09-17 the day it was written.
                          The NT chunk payload encodes ALLOCATOR SLOT
                          INDICES, not tile identity, so a visually
                          static scene still emits a payload that is
                          almost never byte-identical: 3 chunks skipped
                          of 2384. The 280-word hash also put master
                          compute on the window critical path and moved
                          4900 px at f3000 off three skips. Dedup of
                          this stream needs a STABLE ENCODING first --
                          same root as the black tiles.

## SHIPPED — do not cost these as if they were proposals

**ARCHITECTURE.md §4 reads like a proposal because it was written as
one. It shipped.** Both scroll planes are on the MD VDP:

    -DMD_BG -DMD_BG_FG0     both planes on the MD VDP
    -DC1_NOFB               framebuffer cat-1 pass DELETED
    -DC1_PUNCH -DC1_STAMP   sprite suppression under cat-1, per-pixel
    -DMD_SPR -DMDSPR_TOP    MD sprite path
    -DMD_STATIC -DPAL_STATIC -DPEN_MATCH
    -DSET_COLS              replaces bm_scan_rows in GAMEPLAY

**`.build_flags` is NOT the authority for the line.** It is the stamp of
whatever was BUILT LAST, which is usually a probe. At the start of
2026-09-16 it carried `NT_SKIP`, `NT_KEY8`, `BOOT_VALUE` and
`BOOT_FLIPRATE` — none of which are in the line.

    the line's flags   Makefile `LINE_FLAGS` (Makefile:2890), i.e.
                       whatever `make line` passes. That target exists
                       precisely because `make ship-us` alone is not it.
    what is built now  `.build_flags`. Check it MATCHES the line before
                       trusting a measurement against rom/s16.32x.

**Read both before costing anything.**

## OPEN DEFECTS Mike can see

    gameplay HUD dropout          FIXED by dropping the TAGKEEP family
                                  (Mike's G-1 verdict 2026-09-17).
                                  Still on the LINE. NEW 2026-09-17, not in any
                                  earlier list though frame_grade.py has
                                  had a detector for it since
                                  2026-08-26. The line drops the lives
                                  portrait, the gold "x2" and the 50000
                                  high score at f2000/f3000/f4000 under
                                  the level-1 play script. "400" and
                                  "INSERT COIN" -- same blue, same row --
                                  ARE drawn, so it is per-cell, not a
                                  layer. Dropping the TAGKEEP family
                                  restores all three (G-1, with Mike).
                                  MECHANISM: NOT the colour-set
                                  allocator -- ruled out by census and
                                  by the permanent group-0 pin at
                                  m_main.c:4132. Look at the text path.
    black tiles, 4.8% of cells    MECHANISM IS RESIDENCY, not stale
                                  tags (rig census 2026-09-17, 20/20
                                  frames decoded, 20 distinct md5s):
                                    noslot  mean 1.20  no way claimed
                                    dirty   mean 1.20  claimed, art
                                                       not shipped yet
                                    cut     mean 0.15  dead code
                                  Both live paths mean THE ART IS NOT
                                  IN VRAM YET when the cell is drawn.
                                  NOT palette, NOT pens, NOT the
                                  allocator, NOT stale tags -- the
                                  earlier "slot reference outliving
                                  its pen map" reading is RETIRED.
                                  So this defect is DOWNSTREAM OF EMIT
                                  THROUGHPUT: it is the same card as
                                  BAKED TILE TRANSPORT below, and the
                                  black-cell count is the honest
                                  instrument for that card.
    transformation screen         zigzag + ornaments DRAW under
                                  CHEVFIX=1 (not on the line).
                                  Colours wrong: 3 distinct against
                                  an expected 7.
                                  MOVED 2026-09-19 on mdb48 (MDBATCH=48,
                                  NO CHEVFIX): 3 -> 4 distinct. The new
                                  one is a second blue (0,0,206 beside
                                  0,0,174) and it is the ZIGZAG, which
                                  had only ever drawn under CHEVFIX.
                                  So the zigzag tiles were never wrong,
                                  they were not ARRIVING -- residency,
                                  fixed by throughput, not by a colour
                                  card. Still 3 short of 7.
                                  2026-09-18 (Mike, rig, tilesmd6/7):
                                  the animating character shows for
                                  exactly ONE FRAME, then only flames
                                  and chevron. NOT caused by the pen
                                  bake -- MDP_LINES is 3, so the tile
                                  tables own MD CRAM lines 1-3 and
                                  MDSPR owns the fourth (m_main.c:595);
                                  no pen the bake allocates can reach a
                                  sprite. Reserving 6 free pens changed
                                  nothing, which is the same evidence.
                                  UNTESTED ON THE LINE -- run bldS and
                                  trigger a transformation. That single
                                  test says regression vs pre-existing
                                  and nothing else should be built for
                                  it until it is done.
                                  BLACK TILES during the flame wipe are
                                  a SEPARATE and now-explained thing:
                                  md_state_on() reports the round OFF
                                  SCREEN through the transformation
                                  (m_main.c:1738, deliberate), so sets
                                  assigned there get their tags wiped
                                  (:6954) and the art must re-ship.
                                  Mike's 02:24:04 frame is full of
                                  black 8x8 holes; 02:24:05 has them
                                  filled. Residency, caught in the act.
    eye screen                    ~50 frames of black from ~f1610
                                  where the arcade shows a picture.
                                  The rom sets the backdrop black
                                  deliberately, so this is EMPTINESS,
                                  not a blackout. Untested against
                                  CHEVFIX.
    ZEUS MESSAGE, and it is ONE   RE-DIAGNOSED 2026-09-19 (Mike caught
    DEFECT not two                this; I had reported it as two
                                  unrelated things and missed the link).
                                  The "leftover glyphs" ARE FRAGMENTS OF
                                  THE ZEUS MESSAGE. Rig captures of
                                  mdb48 show R, M, R, M, U at identical
                                  positions in the Zeus screen AND in
                                  mid-gameplay as the beast, a totally
                                  different scene. Earlier recorded
                                  stale glyphs were O, Y, N. Every one
                                  of those letters is in "RISE FROM
                                  YOUR GRAVE".
                                  So it is NOT stale text from
                                  elsewhere. The scene message DELIVERS
                                  ONLY A FEW OF ITS CHARACTERS, and
                                  those few are then NEVER CLEARED. The
                                  full line never displays at all --
                                  which is the same bug seen from the
                                  other end.
                                  Two symptoms, one writer. Fixing the
                                  delivery should fix the persistence,
                                  and chasing "stale glyphs" as a
                                  clearing problem was aimed at the
                                  wrong half.
                                  ALSO UNEXPLAINED in the Zeus frame: a
                                  large solid YELLOW rectangle at about
                                  x 85-110, y 125-175 (cells col 10-13,
                                  row 15-21). Not text, not lightning
                                  (the bolt draws separately). Looks
                                  like a block of missing art.
    leftover text glyphs          DIAGNOSED 2026-09-17, and it is the
                                  ONLY defect Mike sees on notag1.
                                  FOUR cells, frozen identical at
                                  f2000/f3000/f4000, planted once before
                                  f2000 and never cleared:
                                    r9 c43 0x024F 'O'
                                    r9 c55 0x0259 'Y'
                                    r11 c35 0x024E 'N'
                                    r11 c47 0x0220 space, colour 2
                                  NOT a capture failure -- FB_TEXT and
                                  TEXT_U agree to the word (1856
                                  compared, 0 differ), so the mask and
                                  its backstop are the wrong tree.
                                  They lie outside both TXT_WRAM_WRITERS
                                  ranges: a scene-level message writer
                                  still on the FB path. See O-6.
    shadow-column dither over MD content   untouched

## OPEN CARDS

    SCENE ANCHORING       SOLVED for captures, still open for the rig.
                          The anchor is the 68K scene timer WRAM
                          0xFFF02A (one tick per GAME frame), already
                          dumped on every gameplay_speed run and
                          already stored in night_run's `timers`.
                          Two captures are comparable iff their
                          interpolated game frames match -- rom SIZE
                          predicts nothing. O-1's 6x cold-run variance
                          is a RIG problem and is NOT addressed by
                          this.
    drop the TAGKEEP      BUILT (rom/night/notag1.32x) and BLOCKED.
    FAMILY                Not one flag: PEN_REPAINT and PEN_HOLD@16264
                          are nested inside TAGKEEP and die with it,
                          but PEN_HOLD@2622 survives as a pen leak, so
                          the family goes together. It had NEVER
                          COMPILED -- mdp_wipe_set_tags was defined
                          inside #ifdef TAGKEEP and called from
                          mdp_assign_set outside it (fixed 2026-09-16,
                          line build proven identical).
                          black_pct 4.6/4.6/4.1 -> 4.6/4.6/4.2, but
                          that is NOT an answer: it removes 1408 B of
                          .bss, and frame-indexed captures of two roms
                          of different size are different content.
    NTSKIP correctness    per-set push key: fold mdp_s_stmp for the
                          sets a row HOLDS, not allocator state per
                          row. 665 assigns / 800 frames means most of
                          the 84.7% survives. NOT a speed card any
                          more -- the rig says flat -- but the key bug
                          is real and NTKEY8's +24.6 points is real.
    transport ablation    the live axis. Design it against the 6x
                          variance first.
    BATCH CAP / PACKET    MEASURED 2026-09-19, and it is the
    WALL                  throughput lever. DMACENSUS=1, ares, 3000
                          frames of level 1:
                            MDBATCH=24 (line)   8.82 tiles/vint
                            MDBATCH=48         18.61
                            MDBATCH=64         18.61  saturated
                            MDBATCH=96          4.93  COLLAPSES
                            MDBATCH=160         5.22  COLLAPSES
                          8.8 was never demand -- it was the cap. The
                          collapse past 64 is the 688-word packet body
                          overflowing: at 17 words a record only ~40
                          fit.
                          BLACK TILES ARE RESIDENCY, so throughput IS
                          the defect, and this measures the 2-word
                          record directly: 688 words holds ~40 records
                          now and ~344 at 2 words, at which point the
                          limit becomes the 68K's VBLANK budget --
                          ~90 tiles/vint at ~200 cycles/tile against
                          ~18,500 cycles of NTSC vblank. Today's 8.8
                          would cost 10% of vblank to CPU-copy, 18.6
                          costs 20%. The 68K can afford it.
                          CART DMA IS NOT AVAILABLE for this (see
                          LESSONS): the 68K must CPU-copy from cart,
                          which mdspr_upload already does for sprite
                          art. Read srcref for how commercial titles
                          pace a bulk upload before picking a batch.
                          rom/night/mdb48.32x = the free half, one
                          constant, 2.1x. Anchored gate at game frame
                          1124: 138032/343845 px differ and the diff is
                          MORE ART (sky band largely gone, background
                          more complete, several stray glyphs absent);
                          reaches that game frame at emulator 1998 vs
                          the line's 2005. ON THE RIG, awaiting Mike.
    BAKED TILE            LIVE. md_emit_art's per-tile conversion is
    TRANSPORT             baked to ROM (TILESMD=1, 425KB at 0x263C00,
                          byte-identical on 941/941 live VRAM slots).
                          Rig verdict on the BYTE-width build
                          (tilesmd.32x, Mike 2026-09-17): "slightly.
                          but nothing significant" + "the Neff battle
                          feels pretty good".
                          WHY IT WAS ONLY SLIGHT: the bake removed the
                          ARITHMETIC and left the transport at BYTE
                          width -- 32 byte reads + 32 byte writes per
                          tile through a volatile pointer the compiler
                          cannot merge. The plan asks for a cart->VRAM
                          move; what shipped was a byte-copy loop.
                          NOW: halfword, 64 touches -> 32 (41b4dfc),
                          gated against the byte build at VRAM 0 of
                          65536 and pixels 0. rom/night/tilesmd2.32x,
                          ON THE RIG 23:34, awaiting the A/B against
                          the byte build Mike already played.
                          NEXT if it moves: 4-byte moves take it to 8,
                          but the art field must be 4-aligned and the
                          packet stride is 17 words, so alignment
                          alternates -- a packet_fmt card.
                          DO NOT read a flat result as "the bake is
                          the wrong direction": the recorded frame
                          threshold law says cuts under ~50 lines buy
                          nothing, so a real sub-threshold win reads
                          as flat until enough of them stack.
                          CAVEAT held: tilesmd runs ONE GAME FRAME
                          offset from the line. Constant, not
                          drifting. Mike accepted the tolerance.

## INSTRUMENT STATUS

    RIG FRAME CAPTURE     THERE IS NONE. Measured 2026-09-19:
                            /dev/MiSTer_cmd tight loop: accepts 769
                              req/s and COALESCES -- 30 requests in
                              39ms produced exactly ONE file.
                            /dev/MiSTer_cmd paced (wait for the file):
                              ~1 frame per 6.8 SECONDS.
                            /dev/fb0: reads at ~36fps, 960x540x32, but
                              it is the OSD layer -- 99.8% black, it
                              does NOT carry core video.
                          CONSEQUENCE, and it is a design rule for every
                          rig probe: A PROBE MUST NOT FLASH. Latch the
                          result in the ROM and hold a STABLE colour, so
                          one slow screenshot captures an answer
                          accumulated over many frames. Five cuts of
                          CARTDMAPROBE were unreadable because they
                          showed a per-frame value and the only way to
                          see it was Mike filming the CRT in slow-mo --
                          which then raised the fair question of whether
                          the camera was inventing the colours.
                          /media/fat/burst.sh on the rig carries these
                          numbers in its header.

    ares                  charges SH-2 INSTRUCTION CYCLES ONLY. No
                          SDRAM/uncached waits, no data cache, no
                          instruction fetch. SLAVE-GATED: the slave
                          reads as critical path.
    the rig (MiSTer)      MASTER-GATED. The only speed authority.
                          Flip rate varies 6x between cold runs --
                          scene-anchor everything.
    MAME (arcade)         the ORACLE. Always valid.
    MAME (our 32x)        a convenience model. Renders every NATIVE
                          build as confetti; cannot pixel-gate the
                          line. MD-side counters are honest.
    counter registries    m_main.c:72 (DIAG, 64 slots, FULL) and
                          m_main.c:718 (mdalloc_ctr, now 48 slots).
                          Written 2026-09-16 from a preprocessor-aware
                          census. READ ONE BEFORE ADDING A COUNTER --
                          no DIAG slot is free in every build.
    DIAG[36]              DOUBLE-BOOKED as r60_pkt_flip, and wiped
                          every frame. Unusable. The registry names
                          NINE more live DIAG collisions -- [35] [37]
                          [38] [39] [42] [50] [51] [52] [53]. Every
                          number read from one is the SUM of two
                          subsystems. [39] and [42] have FOUR owners.
    MDA[16]..[21]         WAS six-way double-booked: the NOTES 51
                          batch census had taken the allocator's own
                          slots, including the mdp_claim_pen pen-
                          starvation trio [19][20][21]. FIXED
                          2026-09-16 -- census moved to [32]..[37],
                          array grown to 48, tools/batch_census.py
                          follows. Any pen-starvation or batch figure
                          from before that date is a SUM.
                          The `[20] > [19]` that flagged it was NOT a
                          subset violation: both are the shipper's,
                          one a word count. The collision was real;
                          that arithmetic was not the proof.
    mdalloc_id[3]         HAD NO WRITER from LOOP29 158 to 2026-09-17
                          and read an unwritten .bss zero. Implemented
                          now (+[6] slots-on-screen). Any figure quoted
                          from it before that date is nothing.
                          Aim the watch: MDALLOCWHY=1 MDAWATCH=<set>.
    mdp_pen_own           stale after a free. NEVER read it without
                          masking on mdp_line_c != 0xFFFF.
    state_health.py       needs a .bs1; headless ares writes none.
                          Use the --dump rebuild.
    black_pct             night_run's frame guard. A WHOLE-FRAME black
                          fraction, swamped by legitimate black art: a
                          large silhouette blob at f4000 moved it 0.1.
                          NOT the instrument for a black-tile card.
    night_run shots       FRAME-INDEXED. Two roms of different image
                          size are photographed at different points in
                          the attract sequence. Diff `_end` before
                          comparing any two captures.
    diff_bytes_vs_base    displacement, not divergence. 1.45 MB was one
                          1408 B shift moving the tail; all 7 generated
                          artifacts were byte-identical. The "~1.3 MB =
                          stale bake" rule is SAME-FLAG only.
