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

### O-3  Counter registry
    owner        BUILDER
    state        READY
    premise      two index collisions in two weeks. DIAG[36] is also
                 r60_pkt_flip and is wiped every frame. MDA[19]/[20]
                 are shared with the cell-chunk shipper where
                 MDA_ADD(20) accumulates a word count.
    ask          a registry comment listing every MDA and DIAG index
                 with its owner, same role as the memory map at
                 m_main.c:1329. Then move the pen-starvation counters
                 to free indices and re-run.
    gate         none. No pixel, no rig.

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
