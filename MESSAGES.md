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

### O-5  Scene anchoring -- the gate on BOTH live cards
    owner        BUILDER
    state        DESIGN, HIGHEST VALUE
    premise      O-1 needs it (rig flip rate varies 6x between cold runs
                 of the same rom). O-2 needs it (frame-indexed capture
                 puts two roms on different attract content when their
                 image sizes differ). Two independent cards, one
                 blocker. Nothing else on the list is unblocked.
    known        tools/attract_parity.py ALREADY aligns on the game's
                 own timeline rather than the frame counter
                 (2026-09-06). tools/gameplay_speed.py refuses windows
                 that cross a scene because the scene timer at
                 0xFFF02A restarts. So the anchor exists in one tool
                 and not in night_run's capture step.
    ask          (a) read: what does attract_parity.py anchor ON, and
                     can night_run's shots use the same anchor?
                 (b) then re-run O-2 against it
    gate         none for the read.


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

### O-2  Drop the TAGKEEP FAMILY  -- BLOCKED on scene anchoring
    owner        BUILDER
    state        BLOCKED (was READY TO BUILD, which it never was)
    blocked by   frame-indexed capture cannot compare these two roms.
                 Same blocker as O-1.
    built        notag1 = LINE_FLAGS minus TAGKEEP/PENHOLD/PENREPAINT.
                 rom/night/notag1.32x, reproducible to 3 bytes.
    what it took (none of which the card knew about):
      1. mdp_wipe_set_tags was DEFINED inside #ifdef TAGKEEP and CALLED
         from mdp_assign_set under MD_STATIC && MD_ROUND &&
         !ASSIGN_NOWIPE. Every TAGKEEP-off build since LOOP29 155 failed
         to compile. FIXED (scope moved; line build proven identical).
      2. PEN_HOLD at m_main.c:2622 survives TAGKEEP removal and is a pen
         leak with no release timeout, so the FAMILY goes, not the flag.
    result       black_pct f2000/3000/4000:
                     bldS    4.6 / 4.6 / 4.1
                     notag1  4.6 / 4.6 / 4.2
                 FLAT -- but do NOT read that as an answer. At f4000 the
                 notag1 frame carries a LARGE solid-black silhouette blob
                 the line does not, and black_pct moved 0.1. The metric
                 does not resolve the defect it is named for.
    why the frames cannot be compared at all:
                 notag1's _end is 0x60168b8 vs the line's 0x6016e38 --
                 exactly 0x580 = 1408 B = mdp_pend_tag[128] +
                 mdp_pend_line[128] + mdp_pend_map[1024] +
                 mdp_pend_used[128]. That shift is 22x the 64 B that
                 LAYOUTPROBE showed moves the ladder 18 points, and the
                 capture script is FRAME-INDEXED, so the two roms are at
                 different points in the attract sequence at "f3000".
                 The HUD/score/pickups visible in notag1 and absent in
                 bldS are that divergence, NOT a fix. Nearly escalated
                 as one.
                 (The 1.45 MB `diff_bytes_vs_base` is the same 1408 B
                 shift displacing the tail. The bake is byte-identical
                 between the two flag sets -- checked, all 7 generated
                 artifacts. night_run's "~1.3 MB = stale bake" rule is
                 only valid for SAME-flag comparisons.)
    ask          scene-anchored comparison. Until that exists this card
                 cannot be answered, and neither can O-1.
    gate         none yet. Nothing to show Mike: no comparable pixel.

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
