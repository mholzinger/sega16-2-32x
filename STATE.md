# STATE — what is true right now

**Volatile document. PRUNE IT, do not append to it.**
Last updated: 2026-09-20.

If a line here is stale, fix the line. Do not add a newer line below it.
That is how the logs became unreadable.

---

## THE LINE

    rom/night/slim31.32x        THE LINE = `make line` (2026-09-21 01:15).
                                = slim30 + the first 16 BG palette
                                publishes forced (the FB persists across
                                a warm relaunch). Rig: background present
                                on 2 of 3 launches -- a per-launch race,
                                layout-biased (LESSONS 01:30). MIKE'S
                                CALL 2026-09-21: "THIS is the new line!"
                                (Zeus text solved, sprites smoother; the
                                black-background launch is the race).
                                Delta from bldS: docs/design/
                                LINE-DELTA-bldS-slim31.md. Mike played
                                slim30:
                                "background fixed, presentation tighter,
                                frames feel slower, one Zeus glyph left".
    rom/s16.32x                 line-equivalent (stamp bytes only)
    rig (MiSTer)                rom/night/barcode5p5.32x -- A PROBE (RIGBARCODE=1 on the pad-5 layout); push slim31 back before handing over

    rom/night/slim25.32x        previous line. Mike: score table fixed
                                (14:20), then "black background" (18:24,
                                capture). Layout-dependent, see LESSONS.
    rom/night/slim23.32x        previous line (same day): + per-pixel
                                cat-1 masks for unbaked pages (the
                                transform) + the Zeus typewriter's row
                                mark.
    rom/night/slim21.32x        previous line (2026-09-20 03:30): the
                                slim pipeline, before the two fixes.
    rom/night/lineV.32x         the line before that (FB tile route +
                                the boot-stack move). Keep for "was this
                                always broken".
    rom/night/lineT.32x         == tilesmd4 (2026-09-18).

INVARIANT CHECK: rom/s16.32x differs from the line at the stamp bytes
only (`25f3 3fff`: BUILD_HASH32 at 0x25F3xx and the build string at 0x3FFFD4):

    cmp -l rom/s16.32x rom/night/slim21.32x \
      | awk '{printf "%x\n",$1-1}' | cut -c1-4 | sort -u

`rom/s16.32x` must be left holding the LINE build at the end of any
session. The slim diag counters at WRAM 0xFF3400-0xFF340F and the
SLIMVALUE readout are still compiled in; remove once nobody needs them.

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
    - wolf transformation: FIXED 2026-09-20. The flames are FG
      priority tiles on page 10 (all 800 cells, MAME census); our
      punch erased whole cells on that unbaked page. Per-pixel masks
      from the tile art now (m_main.c c1rt_class).
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
    ZEUS MESSAGE                  FIXED 2026-09-20 on slim23 (Mike: "fixed
                                  the zeus text"); slim30 marks ALL rows
                                  instead of rows 8-11 (layout, LESSONS),
                                  unverified by Mike. The writer is the object
                                  state routine at 0x56E8: one glyph
                                  every two frames into rows 9/11, then
                                  zeros over the same cells. It was
                                  FM-gated but never MARKED its 4-row
                                  group for TEXTCAPMASK; the mark is in
                                  its thunk now (patch_game.py). The
                                  "leftover glyphs" were the same
                                  defect: lost erase stores.
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

    LEVEL-START RACE      MECHANISM FOUND 2026-09-21 05:30 (LESSONS "lost
                          boot-storm packet"): ares reproduces it on a
                          losing layout (rom/night/barcode6.32x). The
                          68K's boot palette storm loses FB packet 2
                          (blocks 15-29 = the preloaded level-1 palette)
                          to a bank flip; the master's gap echo arrives
                          one push too late for the two-deep re-mark
                          belt. FIX CANDIDATE: FBXECHO=1 (master echoes
                          its lifted sequence in packet word 5 bits
                          12-15; 68K re-blasts unechoed packets).
                          Gates: ares palgate over six layouts (running),
                          then rig 3 launches on pad-5 and the line,
                          then Mike. Not yet on the line.

    RIG BARCODE READOUT   BUILT 2026-09-21 (`make line RIGBARCODE=1`,
                          docs/design/RIG-READOUT.md, decode with
                          tools/rig_barcode.py). 80 bits per capture,
                          written into the picture through the MD
                          window plane (rows 22-27, four-colour cells,
                          six votes per bit), nothing flashes and no
                          palette entry the BG uses below 61 is touched.
                          Carries: vint, packets consumed, palette-
                          flagged consumed, the SH-2's published /
                          flagged counts (packet header words 4/6 high
                          bytes), CRAM 16-47 live count, shadow, round /
                          cut / attract step. Rig 2026-09-21 02:48-02:57:
                          35 of 36 captures decoded over three launches
                          of barcode5; SH-2 published == 68K consumed on
                          every capture; CRAM 16-47 = 29 on every level
                          capture (3/3 launches kept the background).
                          It replaces the CRAM flood for anything with
                          more than one number in it.

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
