# STATE — what is true right now

**Volatile document. PRUNE IT, do not append to it.**
Last updated: 2026-09-16.

If a line here is stale, fix the line. Do not add a newer line below it.
That is how the logs became unreadable.

---

## THE LINE

    rom/night/bldS.32x          Mike PASSED it 2026-09-14. Do not re-ask.
    rom/s16.32x                 bldS-equivalent (stamp bytes only)
    rig (MiSTer)                bldS

`rom/s16.32x` is whatever was built LAST and is usually a probe. It must
be left holding the LINE build at the end of any session.

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
    black tiles, 4.8% of cells    mechanism named: a slot reference
                                  outliving its set's pen map.
                                  TAGKEEP's deferred wipe is a
                                  candidate cause and is falsified
                                  independently -- removing it is the
                                  first thing to try.
    transformation screen         zigzag + ornaments DRAW under
                                  CHEVFIX=1 (not on the line).
                                  Colours wrong: 3 distinct against
                                  an expected 7.
    eye screen                    ~50 frames of black from ~f1610
                                  where the arcade shows a picture.
                                  The rom sets the backdrop black
                                  deliberately, so this is EMPTINESS,
                                  not a blackout. Untested against
                                  CHEVFIX.
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

## INSTRUMENT STATUS

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
