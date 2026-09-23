# MESSAGES — the thread exchange log

**This replaces Mike as the bus.** Both threads read the OPEN section,
act, and append to the LEDGER. Mike reads the GATE QUEUE.

Newest OPEN item at the top. **When an item closes, move its one-line
outcome to the LEDGER and DELETE the open entry.** Do not let this file
grow like the logs did.

---

## GATE QUEUE — things that need Mike

**Escalate here ONLY for: a build that changes a pixel he can see, rig
time, or a direction call.** Never for instrument repair, premise checks,
arithmetic corrections, or anything ending "no pixel changed."

    (empty)

---

## OPEN

### O-7  Lift the per-title WRAM constants out of the engine
    owner        BUILDER
    state        READY
    why          INTENT.md says the deliverable is a reusable S16->32X
                 kit, not this port. Today a SECOND title requires
                 editing m_main.c, which makes it a fork rather than a
                 kit. This is the one change that converts "edit the
                 engine per title" into "write a declaration file".
    premise      measured 2026-09-23, not estimated:
                   engine            25,328 lines, generic
                   bake scripts      10, rerun per title, automatic
                   game_derive.py    549 lines with game_align.py;
                                     translates a reference title's
                                     TABLES through disassembly
                                     alignment and BYTE-VERIFIES each
                                     entry the way the patcher will
                   per-title decl    game_<title>.py TABLES:
                                     altbeast 35 keys / 0 None
                                     goldnaxe 58 keys / 13 None
                                     -> derive got ~78% on the one
                                        cross-GAME datapoint
    THE DEBT     12 distinct Altered Beast WRAM addresses are hardcoded
                 in engine C across ~47 sites:
                   0xFFF142 round                12 uses
                   0xFFF148 object/cut marker     7
                   0xFFF031 attract step          6
                   0xFFF02A                       6
                   0xFFF026 credited play         4
                   0xFFF144 0xFFF0D2 0xFFF0C0 0xFFF095
                   0xFFF029 0xFFF028 0xFFF018     1 each
                 (m_main.c ~35 sites, md_main.c ~5)
    PREMISE TRAP grep -c GAME_ALTBEAST on the engine returns 2, which
                 reads as "already game-agnostic". BOTH are actually
                 GAME_ALTBEASTJ -- region variants. The ifdef count is
                 not a measure of game coupling; the hardcoded
                 addresses are. Do not re-quote the 2.
    ask          move the 12 into game_<title>.py TABLES and read them
                 through the existing per-title mechanism. Mechanical,
                 no behaviour change: the values for altbeast stay
                 identical, so a byte-diff of the built rom against the
                 line is the test.
    verify       `make line` output must be byte-identical to the
                 current line apart from the build stamp. If it is not,
                 a constant was mistranscribed.
    gate         NONE. No pixel, no rig, no direction. Byte-identical
                 rom is the whole acceptance test.
    after        with this done the per-title math is:
                   engine edits   0
                   decl keys      ~58, derive gets ~78%
                   hand-derived   ~13 (the residue derive cannot
                                  byte-verify, i.e. where titles
                                  genuinely differ)
                   bakes          10, automatic
                 CAVEAT: 78% is ONE datapoint and the 13 leftovers are
                 not a random sample -- they are what alignment could
                 not verify.


### O-6  Leftover text glyphs -- the ONLY thing left on notag1
    owner        BUILDER
    state        DIAGNOSED, needs the oracle to confirm the direction
    premise      Mike, 2026-09-17, on the G-1 frames: "the only issue
                 with notag is the leftover text glyphs. otherwise,
                 solid presentation, good colors on all sprites and
                 backgrounds."
    WHAT they are (decoded from TEXT_U, not guessed)
                 four cells, 64-col x 29-row text layer:
                   row  9 c43 = 0x024F  'O'
                   row  9 c55 = 0x0259  'Y'
                   row 11 c35 = 0x024E  'N'   (reads as "M" on screen)
                   row 11 c47 = 0x0220  space, colour 2 (invisible)
                 high byte 0x02 = the red/gold colour bits. Byte
                 offsets 0x4D6, 0x4EE, 0x5CE, 0x5DE.
                 IDENTICAL at f2000, f3000 and f4000 -- planted once
                 before f2000 and never cleared. Not accumulating.
    NOT the capture
                 FB_TEXT (32X DRAM 0x1F000) vs TEXT_U (SDRAM 0x26000)
                 at f4000: 1856 words compared, ZERO differ. The text
                 capture is faithful; the glyphs are genuinely in the
                 source. The whole layer holds only 40 non-blank cells.
                 So TEXTCAP_MASK / the every-8th-vint backstop /
                 MASK_PROBE are all the WRONG TREE for this one.
    where it is  TXT_WRAM_WRITERS (tools/game_altbeast.py:274) covers
                 exactly two writers -- credit line 0x3AAE and health
                 bar 0x4D54, byte ranges 0xCB4-0xD03. These four cells
                 are at 0x4D6-0x5DE, OUTSIDE both. They belong to a
                 scene-level message writer still on the FB path, which
                 is what that table's own comment warns about ("a
                 pending footprint copy must not re-plant over the
                 clear -- the phantom P2 orbs, 2026-09-07").
                 A 68K write to the FB at FM=1 is silently dropped
                 (md_main.c:3629 and the flip-latch record), so a clear
                 that straddles the raise loses its tail. LEADING
                 hypothesis, NOT yet proven for these cells.
    ask          (a) ORACLE: does MAME altbeast have these four cells
                     blank at the same game moment? That decides
                     whether the clear is lost or never issued.
                 (b) then either add the writer to TXT_WRAM_WRITERS or
                     fix the gate.
    gate         none for the read.
    ATTEMPT 1 FAILED (2026-09-17, cost a rig launch)
                 FM-gated 0x9052 (entry, 6 bytes, 0x41F9) + span
                 (0x9052,0x9072). Built clean, thunk 290->298 words,
                 well under the 0xBFF0 bound. RED SCREEN on the rig.
                 Cause: 0x9052 is NOT rare. Caller 0x996 is in the main
                 loop (gated on 0xFFF148) and the routine writes 400
                 longs = 1,600 bytes, so the gate holds FM=0 across a
                 kilobyte-plus clear every frame an object holds -- the
                 SH-2 never gets the FB, the 32X layer never composes,
                 and the bare MD backdrop shows. REVERTED; the line
                 reproduces bldS again.
                 NOTE the gate also changes THE LINE, not just a
                 candidate: game_altbeast.py feeds the game-body bake
                 and FM_GATE is on the line.
    next idea    granularity, not mechanism: gate the per-row setup at
                 0x905E (movew #19,%d2, 4 bytes) so each row is an 80-
                 byte window and FM is released between rows -- the
                 0x3A9A "dbf re-enters gate" pattern. UNVERIFIED and
                 NOT to be put on the rig without an ares check first.


### O-1  Transport ablation: does reducing LOAD have a slope?
    owner        BUILDER
    state        DESIGN
    blocked by   the rig's flip rate varies 6x between cold runs of the
                 same rom. Scene-anchor first or this is unreadable.
    premise      STATE.md AXIS: both compute axes measured flat.
                 NBUILD1 off = 0.33 vs 9.3 fps = 28x, but that is a
                 BRAKE REMOVED, not load reduced. The question is
                 whether reducing transport WORK has a slope.
    ask          (a) what drives the 6x cold-run variance?
                 (b) then an ablation that reduces transport load,
                     scene-anchored, read on the rig
    gate         rig time -> GATE QUEUE before running

### O-2  Drop the TAGKEEP FAMILY -- MEASURED, awaiting Mike (G-1)
    owner        BUILDER
    state        RESULT IN. Escalated as G-1.
    built        rom/night/notag1.32x, reproducible to 3 bytes.
    what it took (none of which the card knew):
      1. mdp_wipe_set_tags was DEFINED inside #ifdef TAGKEEP and CALLED
         from mdp_assign_set outside it, so NO TAGKEEP-off build had
         compiled since LOOP29 155. Fixed; line build proven identical.
      2. PEN_HOLD at m_main.c:2622 survives TAGKEEP removal as a pen
         leak with no release timeout, so the FAMILY goes together.
    result       NOT a black-tile result. black_pct 4.6/4.6/4.1 ->
                 4.6/4.6/4.2, and black_pct cannot resolve the defect
                 anyway. What it actually did is restore the gameplay
                 HUD and add silhouette blobs -- see G-1.
    withdrawn    the first read of this called the frame differences
                 trajectory divergence from the 1408 B .bss shrink.
                 WRONG: the 68K scene timer is identical on both roms
                 at every window and IRQ4 miss is 0.0 on both. The
                 captures were always comparable.
    next         depends on Mike's call in G-1.
    WHY (chased 2026-09-17, allocator RULED OUT)
                 The colour-set allocator does not explain it.
                   census, gameplay f3000, both builds:
                     line   2 set frees {41,46}, 0 tags wiped at free,
                            [22]=[23]=0 -- the deferred wipes NEVER
                            resolve, so those tags stay stale for the
                            whole run
                     notag  5 set frees {40,41,44,45,46}, 64 tags wiped
                     onscr-wiped 0 on BOTH; the allocator is quiescent
                   at their free (mdalloc_id[3], which I had to
                   IMPLEMENT -- it had no writer and every previous
                   reading of it was an unwritten .bss zero):
                     line 41, 46      0 on-screen cells
                     notag 40         65 cells / 64 slots, tile codes
                                      consecutive 0x80280a14..17
                     notag 44, 45     0
                   at f3000 all five sets are UNASSIGNED with identical
                   mdp_s_line / mdp_s_used / pen map / line-0 colour
                   table on BOTH builds. No difference to explain it.
                 And architecturally it could never have been there:
                 m_main.c:4132 pins the HUD text class PERMANENTLY to
                 group 0 and "no allocator/steal/evict loop ever touches
                 index 0".
    where next   the TEXT path, not the allocator: text_grp[par][0] and
                 group 0's special paint at m_main.c:4830 (entry 0 is
                 the through bit; pens 1-7 only). The lives portrait is
                 a sprite and wants the MD sprite path. Both are reads.

### O-4  NTSKIP correctness (not a speed card any more)
    owner        BUILDER, design from DECOMPILE note 119
    state        DESIGN
    premise      the rig says the SPEED case is flat. The KEY BUG is
                 still real: the row key folds scroll/pages/content
                 generations and NOT allocator state, so a skipped row
                 keeps a stale slot reference and those cells go black.
                 Same mechanism as the line's 4.8%.
    ask          per-set PUSH key: fold mdp_s_stmp for the sets a row
                 HOLDS, not allocator state per row. 665 assigns / 800
                 frames means most of the 84.7% survives.
    gate         pixel change -> GATE QUEUE when built

---

## LEDGER — closed, one line each

    2026-09-17  G-1 ANSWERED by Mike: notag1 is "solid presentation,
                good colors on all sprites and backgrounds", the ONLY
                issue being leftover text glyphs. So dropping the
                TAGKEEP family is NOT the trade I escalated -- it is a
                clean improvement with one known defect left. A proper
                candidate build for a play pass is offered and awaiting
                his word.
    2026-09-17  WITHDRAWN: "notag1 adds silhouette blobs at f4000".
                The black shape and the red wolf are legitimate enemy
                sprites (melting death animation; a red hellhound).
                I pattern-matched the known SILH defect without
                identifying the sprite -- the exact rule I had written
                into LESSONS.md two ticks earlier. Mike's eye caught
                it, as the record says it repeatedly does.
    2026-09-17  Leftover glyphs are NOT a capture failure: FB_TEXT and
                TEXT_U agree to the word. Opened as O-6.
    2026-09-16  O-5 CLOSED by a read. The scene anchor already exists
                and is already collected: 68K scene timer WRAM
                0xFFF02A, one tick per GAME frame, dumped by
                gameplay_speed on every run and stored in night_run's
                `timers`. Interpolate it to the shot frame per rom and
                two captures are comparable iff the game frames match.
                attract_parity.py anchors differently -- it bisects for
                the display-gate mailbox 0xFFB001 bit 5 at the
                title->demo cut and applies ONE constant OFFSET, which
                corrects a fixed lag but not ongoing drift.
    2026-09-16  O-2 measured. TAGKEEP family off RESTORES the gameplay
                HUD (lives, x2, 50000) that the line drops at f2000/
                f3000/f4000, and adds silhouette blobs at f4000.
                Escalated to Mike as G-1. Not a black-tile result.
    2026-09-16  WITHDRAWN: 'the notag1 frames are trajectory
                divergence'. Scene timer identical on both roms at all
                five windows, IRQ4 miss 0.0 both. They were always
                comparable. Renderer flags cannot move the 68K clock.
    2026-09-16  .build_flags is NOT the authority for the line -- it is
                the stamp of the LAST BUILD. At session start it
                carried NT_SKIP, NT_KEY8, BOOT_VALUE and BOOT_FLIPRATE,
                none of which are in the Makefile's LINE_FLAGS. The
                authority is LINE_FLAGS (Makefile:2890) / `make line`.
    2026-09-16  O-3 CLOSED. Counter registries written from a
                preprocessor-aware census: DIAG at m_main.c:72 (64
                slots, FULL -- 0x28000..0x280FF, BM starts at 28100),
                mdalloc_ctr at m_main.c:718. Nine live DIAG collisions
                beyond the known [36]; [39] and [42] have four owners.
    2026-09-16  MDA collision was SIX slots, not two. The NOTES 51
                batch census (db8d834) had taken [16]..[21] from the
                allocator (eea4cf8), including the mdp_claim_pen pen-
                starvation trio. Census moved to [32]..[37], array
                grown to 48, tools/batch_census.py follows. Line build
                proven unaffected: identical SH-2 assembly vs HEAD.
    2026-09-16  The `[20] > [19]` subset violation was a FALSE alarm --
                both are the shipper's, one a word count. The collision
                was real; that arithmetic was not the proof of it.
    2026-09-16  Compute axis CLOSED. Master ablation (84.7% of the
                name-table walk) FLAT on the rig; ares' 58.3 was slave
                work on a slave-gated instrument. Neither SH-2's
                compute is the wall. Retires CACHELOCK, the footprint
                split, NT_SKIP-as-speed, the __ramtext_size ranking.
    2026-09-16  TAGKEEP falsified: [22]=0 [23]=26.
    2026-09-16  MDA[19]/[20] found double-booked by the subset rule
                (a subset counter may not exceed its parent).
    2026-09-16  Chevron zigzag + ornaments DRAW under CHEVFIX=1.
                Colours wrong: 3 distinct against an expected 7.
    2026-09-16  S4 found ALREADY SHIPPED before it was costed.
                SET_COLS found already replacing bm_scan_rows in
                gameplay before it was costed.
    2026-09-16  Camera bound settled from rom: 0.5 px/frame max,
                vertical a compile-time constant.
