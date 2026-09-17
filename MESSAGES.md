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

### O-2  Drop TAGKEEP
    owner        BUILDER
    state        READY TO BUILD
    premise      [22]=0 [23]=26, 100% MOVED, two independent windows
                 (this arc + LOOP29 155's "0 of 59"). Its premise is
                 that a re-assigned set usually lands back on the same
                 (line, pen map). It never does. It is ON THE LINE and
                 holds stale references for nothing.
    ask          build it, read black-tile share against bldS
    gate         pixel change -> GATE QUEUE when built

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
