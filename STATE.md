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

## THE AXIS — TRANSPORT (settled 2026-09-16)

**Neither SH-2's compute is the wall. Measured, both processors:**

    master compute ablated    84.7% of the name-table walk removed
                              -> FLAT on the rig (medians moved the
                                 wrong way, ranges overlap)
    slave compute ablated     ares' 58.3 fps was SLAVE work, and ares
                              prices the slave -- it moved there and
                              nowhere else
    transport, NBUILD1 off    0.33 fps against the line's 9.3  = 28x

**The transport is the only axis with a demonstrated slope.** Caveat:
removing a brake is not the same as reducing load. The 28x shows where
the sensitivity lives, not that headroom exists there.

**Next measurement: does the transport have a slope when LOAD is reduced,
not when a brake is removed?** Scene-anchor it — the rig's flip rate
varies 6x between cold runs of the same rom and that must be understood
first.

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

## SHIPPED — do not cost these as if they were proposals

**ARCHITECTURE.md §4 reads like a proposal because it was written as
one. It shipped.** Both scroll planes are on the MD VDP:

    -DMD_BG -DMD_BG_FG0     both planes on the MD VDP
    -DC1_NOFB               framebuffer cat-1 pass DELETED
    -DC1_PUNCH -DC1_STAMP   sprite suppression under cat-1, per-pixel
    -DMD_SPR -DMDSPR_TOP    MD sprite path
    -DMD_STATIC -DPAL_STATIC -DPEN_MATCH
    -DSET_COLS              replaces bm_scan_rows in GAMEPLAY

`.build_flags` is the authority for what is on the line. **Read it before
costing anything.**

## OPEN DEFECTS Mike can see

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
    leftover transformation text  untouched
    shadow-column dither over MD content   untouched

## OPEN CARDS

    drop TAGKEEP          clean counters, falsified premise, aimed at
                          the visible black tiles
    NTSKIP correctness    per-set push key: fold mdp_s_stmp for the
                          sets a row HOLDS, not allocator state per
                          row. 665 assigns / 800 frames means most of
                          the 84.7% survives. NOT a speed card any
                          more -- the rig says flat -- but the key bug
                          is real and NTKEY8's +24.6 points is real.
    counter registry      two index collisions in two weeks
                          (DIAG[36]/r60_pkt_flip, MDA[19]/[20]).
                          20 minutes.
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
    DIAG[36]              DOUBLE-BOOKED as r60_pkt_flip, and wiped
                          every frame. Unusable.
    MDA[19] / MDA[20]     DOUBLE-BOOKED with the cell-chunk shipper.
                          MDA_ADD(20) accumulates a WORD COUNT.
                          Unusable until moved.
    mdp_pen_own           stale after a free. NEVER read it without
                          masking on mdp_line_c != 0xFFFF.
    state_health.py       needs a .bs1; headless ares writes none.
                          Use the --dump rebuild.
