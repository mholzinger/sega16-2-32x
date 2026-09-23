# STATE — what is true right now

**Volatile document. PRUNE IT, do not append to it.**
Last updated: 2026-09-22.

If a line here is stale, fix the line. Do not add a newer line below it.
That is how the logs became unreadable.

---

## THE LINE

    rom/night/spd2.32x          THE LINE = `make line` (2026-09-23 00:50)
                                = spd1 + PUBNORB=1 (the publish's replay
                                copies built from SDRAM, not read back
                                from the framebuffer). Compose-frame FM
                                drop 108-112, gate spins 6-14 (line at
                                the day's start: 124-135 / 22-26). ares
                                palgate PASS, 18/19, rig 3/3.
    rom/night/spd1.32x          previous line (2026-09-23 00:20)
                                = cut1 + CRAMISR=1 (the 32X CRAM painted
                                at the flip in vblank, off the FM window)
                                + BLITAUDITDIV=4 (the blit audits one
                                skipping row in four per window). Speed
                                work only: the game-visible FM window on
                                compose frames 124-135 -> 112-120 lines,
                                gate spins 22-26 -> 10-18; game speed
                                still 50.0% (threshold, LESSONS 00:05).
                                ares palgate PASS, 18/19 anchors, rig 3/3.
                                RISK for Mike's eyes: the blit's mask
                                audit heals a lie in <= 8 frames instead
                                of 2 (the purple-band class).
    rom/night/cut1.32x          previous line (2026-09-22 21:20)
                                = eye9 + CUTPREFETCH=1 (the cut's art
                                prefetched on the cut bit; the scene
                                install keeps prefetched slots clean and
                                skips re-marking sets with no line). ares:
                                19/19 anchors clean, palgate PASS, the
                                cut's black period ~45 -> ~15 frames
                                (LESSONS "the cut's transition is the
                                scene install's re-mark"); rig 3/3.
    rom/night/eye9.32x          previous line (2026-09-22 19:50)
                                = eye8 + the glow keyed on the cut pages
                                (harmless; ares 16/19 same transients);
                                rig 3/3.
    rom/night/eye8.32x          previous line (2026-09-22 19:40) = eye6 + rounds
                                bake both tile variants (the level-1 boss
                                gate's purple: LESSONS "single-variant blob
                                drew purple"); ares palgate PASS, scenegate
                                16/19 (cut+10/+30 = the switch, pic+60 one
                                untagged cell in the blank), rig 3/3. Mike
                                to confirm at the boss gate.
    rom/night/eye6.32x          previous line (2026-09-22 19:15)
                                = eye4 + scene 5 harvested inside the mid
                                splash (sets 65/66 no longer overflow) +
                                the OFF state keyed on the cut pages (the
                                level stays intact until the switch).
                                ares 18/19 (cut+10 still re-marks 762 FG
                                cells at the cut bit -- eye7), palgate
                                PASS, rig 3/3. MIKE 2026-09-22 19:20:
                                "YES! You fixed the eyes animation!"
                                REGRESSION (Mike, 19:30): purple through
                                the FG at the level-1 boss gate = the
                                single-variant blob's round masks from the
                                three-scene audit (sets 81/83/84 BG-only
                                there, FG at the gate; colour 0 purple).
                                eye8 = rounds bake both variants.
    rom/night/eye4.32x          previous line (2026-09-22 18:55)
                                = eye2 + the attract cleanup (STATE "ATTRACT
                                CLEANUP 2026-09-22", LESSONS same day):
                                per-cell cat-1 mask slots (the pupil
                                notch), C1MASKTAB=1 on LINE_FLAGS, scene 9
                                keyed on the cut pages and packed alone
                                (the cut's blue), full-row shipping on fast
                                pans and harvested scenes, harvest layer
                                masks. ares: 17/19 anchors (cut+10/+30 are
                                the switch), palgate PASS, eye 223 -> 210,
                                cut f1580 299 -> 14; rig 3/3. Mike saw the
                                notch fixed on eye3. Next: eye5 = scene 5
                                re-harvested inside the mid splash (its
                                old frames were level 2's demo: sets 64-73
                                in the splash, 65/66 overflowed).
    rom/night/eye2.32x          previous line (2026-09-22 18:00)
                                = attract8b + the attract harvest reading
                                plane B at its real address (0xE000; it
                                read 0xD000, empty, so scenes 5-9 had no
                                BG sets: the eye's iris showed set 35 on
                                three CRAM entries -- Mike's "loose
                                tiles") and sets taken from the walker's
                                per-cell mirror (md_dbg_nt) with a layer
                                mask; the tile blob in single-variant
                                2 KB blocks (694 -> 414 KB, LESSONS
                                2026-09-22). ares: 19/19 anchors clean,
                                refusals 37,000 -> 0, palgate PASS; eye
                                vs the arcade phase-matched 239 -> 223
                                cells (yellow), ~105 (blue); rig 3/3.
                                MIKE'S PASS 2026-09-22: "eye2. easily."
                                PALQUANT=1 (ramp-nearest quantiser, one
                                MD level darker on 16/32 inputs; ares
                                eye 223 -> 38 cells vs MAME) was A/B'd
                                on the rig and REJECTED by eye: MAME's
                                tone is not the cabinet's, and the
                                "correct" ramp is ares's, unmeasured on
                                the FPGA. Knob kept, off; a rig ramp
                                probe would settle it. The blue-to-yellow switch is
                                the switch-in-view class (600+ cells for
                                ~40 frames, both builds).
    rom/night/attract8b.32x     previous line (2026-09-22 16:30)
                                = attract7 + the splash re-harvested across
                                its whole palette animation and packed by
                                per-pixel colour history (the logo's red
                                and white phases drew an outline over a
                                dark fill: a pen shared by pixels that
                                animate apart). ares: the logo's pens equal
                                the game's palette at the red frame; rig
                                3/3. The relief-to-logo switch still
                                shows ~20-30 black frames: the game's
                                page clear+refill shown in view (LESSONS
                                2026-09-22) -- the transport-floor
                                design card, not a bug in this build.
    rom/night/attract7.32x      previous line (2026-09-22 14:45) = attract6 +
                                attract step 5 mapped to the round (the
                                second level-1 demo; its black sky was
                                scene 7's table, read through the
                                barcode). ares: second demo's sky full at
                                +150. Rig 3/3 level demos; ONE capture
                                (launch 3, ~113 s) showed the SCORE TABLE
                                without its tiled backdrop -- the same
                                shape (a scene's table not landing on one
                                launch). Readout twin a7bc, 8 score-screen
                                captures over 4 launches: backdrop palette
                                landed (6 entries), 0 stale cells -- not
                                caught; OPEN, rarer than 1/8. The level-1
                                sky loss is CLOSED (it was the step map).
                                On the rig for Mike's pass.
    rom/night/attract6.32x      previous line (2026-09-21 18:15)
                                = attract5 with the 68K boot stack moved
                                to 0xFF4FF0 (.bss headroom). Rig 3/3.
                                OPEN: level 1's sky goes black on some
                                launches (attract5 launch 2/3; the
                                readout twin a5bc read 29 live entries on
                                every level capture across 4 launches --
                                not caught yet). AWAITING MIKE'S PLAY PASS.
    rom/night/attract5.32x      previous (2026-09-21 16:40)
                                = attract4 + ART_TAIL joined to the slim
                                route (tile records ride every cell
                                chunk's tail): the eye's hold 23 -> 9
                                vints, picture 26 -> 19, splash 34 -> 30,
                                no dropped records. Rig: tail4 (same
                                bytes but the stamp) 3/3; attract5 itself
                                2/3 -- launch 2 lost LEVEL 1'S SKY
                                (screenshots/rig_attract5, captures
                                2-4 and 12: black behind the ruins, the
                                sky present in capture 1) = Mike's
                                "revisiting level one shows black
                                background tiles", seen here on the
                                FIRST visit too. Launch-dependent, ares
                                clean: the hardware race class. Readout
                                twin (a5bc) queued x4 to catch it.
    rom/night/attract4.32x      previous line (2026-09-21 14:45)
                                = attract3 + md_pending at file scope (the
                                re-mark paths bump it: orphaned dirty
                                slots had left the transformation's red
                                field black) + scene 9 = harvest + the
                                ground sets. ares scene gate 19/19 clean
                                (the cut drains by +30, matches the
                                arcade from +60: blue zigzag, red field,
                                flames); rig 3/3. Residual: scene-start
                                holds of ~0.3-0.5 s (the load), and the
                                cut's first ~40 frames load in view with
                                a black sky (its sky sets do not fit the
                                table beside the zigzag's). AWAITING
                                MIKE'S PLAY PASS -- on the rig.
    rom/night/attract3.32x      previous line (2026-09-21 14:10)
                                = attract2 + THE ATTRACT BAKE: scenes 5-9
                                (splash, eye, picture, scores, the
                                transformation) harvested from ares and
                                baked into the tables and the tile blob;
                                tables keyed by SCENE from the state word;
                                slim budget 80/vint with a word-capped
                                emitter; hold settles from the game's
                                blank. ares scene gate 19/19 anchors
                                clean; rig 3/3. Residual: a baked scene
                                still shows ~0.3-0.5 s of our hold after
                                the game's blank (the load), and the
                                transformation loads its zigzag field in
                                view over ~1 s (no game blank there).
                                AWAITING MIKE'S PLAY PASS -- on the rig.
    rom/night/attract2.32x      previous line (2026-09-21 11:25)
                                = attract1 + the echo belt's re-mark half
                                actually firing (re-blast spacing counted
                                apart from the packet's age). Rig 4/4
                                launches, every background; the readout
                                twin (attbc2) 3/3 with the palette
                                counters agreeing on both CPUs. Awaiting
                                Mike's play pass -- on the rig.
    rom/night/attract1.32x      previous candidate (2026-09-21 10:45): 1/3
                                on the rig -- the re-mark half was inert
                                = round1 + high-score gates on by default
                                + BGBOTTOM (bottom-band backstop only
                                under opaque FG) + MDSREMARK + the echo
                                belt's second half (re-mark an unechoed
                                push). Rig: 4/4 launches, every
                                background; score table fills; level-2
                                floor complete. OPEN: the eye picture
                                loads in view (~2.5 s) -- its tiles are
                                unbaked (LESSONS attract cards, 5).
                                AWAITING MIKE'S PLAY PASS -- on the rig.
    rom/night/round1.32x        previous line (2026-09-21 09:30)
                                = echo2 + the round's MD table installed
                                on a round change while on screen (the
                                attract's level-2 demo had no background:
                                the game writes its round after the
                                display-on edge and the palette detector
                                cannot see level 2). Rig 3/3: level-1 and
                                level-2 demo backgrounds on every launch.
                                AWAITING MIKE'S PLAY PASS -- it is on the
                                rig.
    rom/night/echo2.32x         previous line (2026-09-21 05:20)
                                = slim31 + FBXECHO=1 (the framebuffer
                                packet echo belt) + the slim diag and
                                palette shadow moved out of slim_art.
                                Rig: pad-5 layout 3/3 (0/6 before),
                                line layout 3/3; ares palette gate PASS
                                on eight layouts. Closes the level-start
                                black background (LESSONS 2026-09-21
                                "lost boot-storm packet"). AWAITING
                                MIKE'S PLAY PASS -- it is on the rig.
    rom/night/slim31.32x        previous line (2026-09-21 01:15).
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
    rig (MiSTer)                attract8b (pushed 2026-09-22 16:30)

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
    ATTRACT CLEANUP 2026-09-22    Mike's three rig captures on eye2:
    (evening)                     (1) eye pupil notch (three iris cells
                                  showing iris where the pupil sprite is):
                                  FIXED in eye3 -- the runtime cat-1 mask
                                  cache (64 slots by code & 63) thrashed on
                                  the harvested scene; class-2 cells now own
                                  a mask slot; C1MASKTAB=1 joined LINE_FLAGS
                                  (C1_STAMP assumed it). ares 223 -> 210
                                  cells, rig 3/3.
                                  (2) transformation cut: black/stipple
                                  around the flames = scene 9's blue (set
                                  19, colour-cycling) OVERFLOWED the bake,
                                  keyed on the cut bit ~15 frames before the
                                  page switch: FIXED in eye4 (keyed on
                                  TEXT_C[0x740] == 0xAAAA, harvested after
                                  the switch, packed alone, per-pixel keys):
                                  f1580 299 -> 14 cells. The ~30-frame
                                  transition at the switch remains (switch-
                                  in-view class). Rig pending.
                                  (3) the pan to the beast eye (9-12 px a
                                  frame): stale entering columns FIXED by
                                  full-row shipping (64-cell plane rows on
                                  fast scrolls and harvested scenes, 24
                                  columns of lead); the remaining black
                                  cells are art requested at the row's next
                                  visit (rotation 9 windows) -- transport
                                  floor. Windows/frame measured 1.00 (0.50-
                                  0.88 across the game's page switch).
                                  (4) eye5: scene 5's harvest frames moved
                                  inside the mid splash (4530-4600 were
                                  level 2's demo: sets 64-73 in the splash,
                                  65/66 overflowed) -- overflow gone, 18/19.
                                  (5b) eye7/eye9: the cut bit no longer
                                  disables the baked scan or stops the
                                  glow (keyed on the cut pages) -- harmless
                                  but NOT the cut+10 count: by cut+10 the
                                  pages are already 10/11 (LESSONS "cut+10
                                  is already the switch"); the pre-switch
                                  window is ~10 frames.
                                  (5) eye6: the OFF state (pictures) keyed
                                  on the cut pages like scene 9, so the
                                  level stays intact until the switch (rig
                                  capture: black bottom band during the
                                  transformation start; ares cut+10 had
                                  901 dirty FG cells). Rig pending.
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

    BlastEm              CROSS-CHECKED AND REJECTED for the transport
                         axis (O-8, 2026-09-23, BLASTEM.md sections 7-8).
                         Boots both SH-2s after a three-line fix in its
                         sh2.cpu sh2_reset (stale prefetch across the
                         68K reset pulse; source tree
                         ~/src/blastem-0c61d0d95463, patched, no VCS).
                         On the BODYPROF figure aligned on the game's
                         vint counter: FRT clock and one-window-per-vint
                         cadence agree with ares; master half 20.0 lines
                         vs ares 26.6 vs rig 67-86. It undercharges FB
                         writes; the FPGA overcharges them 1.6x. Usable
                         for logic cross-checks with --dump (sdram,
                         wram); its `-b N` is HALF-frames. No number
                         from it goes into a card.

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

    LEVEL-START RACE      CLOSED 2026-09-21 06:45 (LESSONS "lost boot-storm
                          packet" + its 06:30 correction). The master's
                          pre-flip lift overlapped a 68K blast in
                          progress; the finished packet sat in the
                          displayed bank until the master's fill erased
                          it; the two-deep BAD1 belt had rotated past
                          it. FBXECHO=1 (on the line): the master echoes
                          its lifted sequence in packet word 5 bits
                          12-15, the 68K re-blasts an unechoed packet
                          from partb_hook two vints after its blast.
                          Rig: pad-5 0/6 -> 3/3, line 3/3; ares gate
                          PASS on eight layouts. Mike's pass pending.

    TRANSPORT FLOOR       MEASURED 2026-09-21 (LESSONS "The transport floor,
                          measured" + the two dead levers). ART_TAIL on
                          the line took the eye's hold 23 -> 9 vints.
                          Dead: compose skip, flip skip, page budget --
                          the load is bound by the game's own gated
                          tilemap writes and the walk's rotation.
                          BUILT 2026-09-22 20:45 as CUTPREFETCH=1 (on
                          LINE_FLAGS, rom/night/cut1.32x pending rig):
                          on the cut bit the master claims ways for
                          pages 10/11's tiles (free, else untouched
                          for 32 windows) and ships them under scene
                          9's table (md_tag bit 30); mds_install keeps
                          those slots clean and no longer re-marks
                          sets with no line in the new table. ares,
                          anchor-relative: dirty at the switch 965 ->
                          16; black cells +10/+15/+20/+25 = 232/36/19/0
                          (eye9: 357/454/496/595, clear by +55); 0
                          cells vs the arcade at +25. LESSONS "The
                          cut's transition is the scene install's
                          re-mark". Remaining ~15 frames = the row
                          rotation. The pan's end (no install, page 0
                          rewritten by the game) is not covered.
                          ORIGINAL DESIGN (not built): PRE-SWITCH WALK. The
                          arcade shows the transformation complete on
                          its first frame because the game writes the
                          cut's pages (10/11) during the level and only
                          switches page pointers. Walk a scene's pages
                          into the SECOND half of each 64x32 name table
                          (rows 28-31 + the unused columns cannot hold
                          it; use plane A/B's alternate 0x2000 tables:
                          VRAM has room for a second pair) while the
                          current scene shows, then switch the VDP's
                          table registers with the game's pointer
                          switch. Cost: a second walker pass per window
                          during play; the tile slots are shared.
                          Gate: cut+1 non-black == arcade's.
                          VRAM CHECK 2026-09-22: plane bases step 8 KB
                          (reg 82/84), so alternates need 0x8000/0xA000
                          -- the MD sprite art lives there (0x8000-
                          0xAE40 for the boss set, md_sprart_info.h)
                          and the free pieces (0xB000 window, 0xD000,
                          0xF400) total 10 KB against its 11.5 KB. The
                          cheaper form: PREFETCH the next scene's art
                          (the cut's sets 19-21 are known and pinned)
                          while the level shows, then on the page-word
                          change walk all 28 rows as full 64-cell rows
                          in 3 windows (28 x 65 words over three 688-
                          word packets) instead of the 9-window
                          rotation: ~4 frames of transition instead of
                          ~30. Measured today: the cut's transition is
                          f1540-1575 (eye4), the pan's end is the same
                          class (pages 0101 -> 0000 with a new page 0).

    60 Hz FLOOR           MEASURED 2026-09-22 21:00-23:00 (LESSONS "The
                          60 Hz floor on the line, measured" + addenda).
                          The game runs a 2-vint frame at 50% with ZERO
                          misses and the transport posts every vint; the
                          frame is ~327 lines because the game's IRQ4
                          waits for the master's FM-held window (45 lines
                          on frames whose sprites did not change, ~110
                          where they did) and its pass then spins 22-26
                          lines in gated stores. BODYPROF (FRT = 45.8
                          ticks/line, NOT 128): the window body on a
                          compose frame = launch 15 + blit/compose half
                          66 + apply_cram 10 + publish 16; the master
                          then idles 130-160 lines. Cuts measured on
                          ares: BLITAUDITDIV=4 (66 -> 58), CRAMISR
                          (apply_cram 10 -> 0, paints at the flip in
                          vblank), PUBNORB (the 2 x 368 FB read-backs
                          -> SDRAM copies, ~8 lines). FM drop on compose
                          frames 124-135 -> 112-120 with the first two;
                          spd1 = cut1 + CRAMISR + BLITAUDITDIV=4 is in
                          the gates/rig. Dead: TEXTCAPOFF (no change),
                          BLITSHIFT=48 (does not move the half: the
                          half is compose-bound, not copy-bound). NEXT:
                          the compose itself (slave in-window + master
                          tail; SPROF stamps in probe8), the launch's
                          15 lines (records from the FB + hash), then
                          the 68K side (consume 17, post-ack tail 15-20).
                          FLOOR (2026-09-23, BLITPROF): the window is
                          FB-write-bound at ~470 B/line aggregate; a
                          compose frame writes ~28 KB -> ~85 lines
                          minimum vs the 68 the threshold needs. LEVERS
                          = BYTES: MD VDP sprite offload (claims 0 on
                          level 1; MDSPR_WHY census running), BLITHASH
                          (content skip), then EARLYREC. MTASKINWIN
                          dead as built (LESSONS).
                          RIG (RIGBLIT, 2026-09-23): the FPGA's FB
                          write rate is ~1.6x slower than ares (blit
                          67-86 lines per compose window on the level-1
                          demo vs 45-47 in ares). Hardware one-vint
                          needs <= ~15 KB FB writes a frame; the MD
                          offload is capped by the single sprite
                          palette line, and a SECOND line is closed by
                          measurement (LESSONS "Two tile palette lines
                          do not hold level 1": 33 distinct colours,
                          3-4 sets overflow at every frame); BLITHASH
                          has no 18 KB home. Left: an SDRAM home for
                          BLITHASH, EARLYREC (mixed cadence), or a
                          different sprite route.
                          DESIGN (23:40, not built): PASS-END STRIKE.
                          The game's IRQ4 is 1-11 lines (ISR-exit V
                          ring); the frame is wait-for-ack + ~10 +
                          pass (147 light / 184 heavy). One vint needs
                          the FM window to end by ~68 (heavy) / ~105
                          (light). The window's cost on compose frames
                          is the slave's sprite compose + the chase
                          blit (~66) because records arrive at the
                          vint. Strike the sprite RAM the moment the
                          game's pass ends (a gate thunk at the idle-
                          loop entry 0x903982, GAMEGATE-style) through
                          the DREQ FIFO into SDRAM (the FB is the 68K's
                          at FM=0, but sbuf is SDRAM), compose on both
                          CPUs while FM is still low, and leave the
                          window = blit + publish (~30-45 lines). The
                          master idles 130-160 lines a vint today, so
                          the compose has room before the vint.
                          REFINED 23:50: the sprite records already
                          reach SDRAM through the DREQ FIFO (r60_push
                          at the post -> SPR_LAND); they are final at
                          the END OF THE GAME'S IRQ4 (its upload), ~90
                          lines after vblank, and today they wait until
                          the next vint's post (170 lines idle). EARLY
                          PUSH: call the records push at fmgate_ret
                          (IRQ4 exit; POST_LATE's site, but ONLY the
                          push -- no raise, no window), let the SH-2
                          poll the DMAC landing in its idle loop and
                          launch the compose on landing; the vint's
                          window then only blits + publishes. No FM is
                          needed for any of it (FIFO + SDRAM).
                          CORRECTION 23:55: on the line (FB_XPORT) the
                          records do NOT ride the FIFO -- r60_push and
                          the SPR_LAND landing are #ifndef FB_XPORT; the
                          68K strikes them into FB_SPR at the vint top
                          (FM=0) and the master reads them at FM=1. So
                          an early compose needs the records over the
                          FIFO again (EARLYREC: the FIFO code exists
                          behind the ifdefs; the FIFO's partial-landing
                          hazards are in memory lost-push-belt /
                          ARMGATE) -- or a different split:
                          MTASK IN-WINDOW: the master composes its own
                          rows before its blit half instead of idling
                          46 lines behind the slave's compose. The row
                          ownership machinery exists (BANDSHIFT /
                          RG2SHIFT: the master owns [lo+36+BS, hi) of
                          each band as nat_mtask stage 1) but runs in
                          the post-ack tail, one window late for the
                          blit; running the stage-1 chunks inside the
                          window before BLIT_HALF, with the master
                          owning rows (SHIPBANDSHIFT=4 SHIPRG2SHIFT=0;
                          the bare BANDSHIFT/RG2SHIFT lose to the ship
                          list's literals, LESSONS 01:10), halves the
                          compose wall (~-25 lines). BUILT as
                          MTASKINWIN (nat_mchunk factored); probe13
                          measures it with the real split. The
                          chunk body is inline in the poll loop
                          (m_main.c ~12530-12600) and time-boxed; it
                          needs factoring (RAMCODE budget: ~300 B free
                          after BODYPROF's latch trick).
    ATTRACT BAKE          DONE 2026-09-21 14:10 (LESSONS "The attract
                          bake"). Open residue: (a) the ~0.3-0.5 s hold
                          after a picture's blank = the load (1120 cells
                          x 2 planes + art at 80/vint); (b) the
                          transformation's zigzag field loads in view
                          ~1 s (the game does not blank it; its sets
                          are baked, its cells still walk in over 8
                          windows); (c) the level's plane B stays
                          dirty (1120) through the cut and re-ships
                          after -- cosmetic, hidden by the eye's blank.

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
