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

### G-1  notag1 restores the gameplay HUD and adds silhouettes. A TRADE.
    build        rom/night/notag1.32x = the line minus the TAGKEEP
                 family (TAGKEEP + PENHOLD + PENREPAINT).
                 NOT a playable hand-over: it is a probe. Say the word
                 and it gets built as a proper candidate.
    look at      rom/night/o2_look/  -- bldS_3000 vs notag1_3000,
                 bldS_4000 vs notag1_4000, same frame, same input
                 script, directly comparable (proof below).
    what changed
        FIXED    the line DROPS the gameplay HUD -- lives icon, "x2",
                 and the 50000 high score are absent at f2000, f3000
                 AND f4000. notag1 draws all of them. Lives-region
                 distinct colours 90 (line) vs 335 (notag1), stable
                 over four consecutive frames on both.
        COST     at f4000 notag1 carries a large solid-black
                 silhouette blob over the creature and a red blob on
                 the wolf, where the line renders the explosion art
                 correctly. 10.7% of pixels differ at f4000, 2.6% at
                 f3000.
    why these frames ARE comparable (checked, because the first read
    of them was wrong and got withdrawn):
                 68K scene timer 0xFFF02A identical on both roms at
                 f1500/2200/2900/3600/4100 (874/1224/1574/1924/2174),
                 so 0 game-frames apart at every shot; irq4_miss_pct
                 0.0 on both; both stable across N..N+3 so it is not a
                 render-phase offset. The flags are SH-2 renderer
                 flags and never touch the 68K timeline.
    the call     this is a TRADE, not a win, so it is yours:
                 (a) is the HUD dropout worth the silhouettes?
                 (b) the HUD dropout is not in STATE.md OPEN DEFECTS
                     at all -- had you seen it, or is it new?
    NOT asked    no rig time. No play pass yet -- say whether you want
                 a candidate built first.

---

## OPEN

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
    next         depends on Mike's call in G-1. If the trade is worth
                 chasing, the question is WHY dropping the family
                 restores the HUD -- that mechanism is not understood
                 and is probably the more valuable half.

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
