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

**EXPIRY RULE (added 2026-09-24, after G-1 sat here seven days):** a gate
item unanswered for 48 hours is STALE. Do not carry it forward silently.
Re-state it once at the top of the next relay as a one-liner, or close it
to the LEDGER as declined. An item that is neither answered nor closed
becomes furniture and the builder blocks on it invisibly.

---

## OPEN

### O-13  Re-price the palette DISCOVERY against the FB transport -- item 1, and its one prior test measured the wrong thing
    TO: builder   from decompile

    PREMISE CHECK
      record says    PALNOCMP EXISTS AND WAS REVERTED at 74.1%. But the
                     Makefile's own note beside it: "(v1 kept the
                     shadow copy and measured 74.1%; THAT WAS THE COPY,
                     NOT THE IDEA.)" So the idea has never been tested
                     without the expensive half still in it.
                     PALROTOROFF (the ceiling, colours freeze, never
                     ships) "re-measures at 94.7% against 82.3%".
                     Transport arithmetic, Makefile: a redundant
                     32-word block costs ~1.6 scanlines through the FB;
                     PROVING it redundant costs ~5.
      flags on line  PAL_DELTA on. PALNOCMP, PALROTOROFF, PAL_DIET all
                     off. No shipping build has run without discovery.
      instrument     ***MY CEILING NUMBER IS ARES AND I DO NOT TRUST IT
                     HERE.*** 94.7/82.3 is the ares single-vint metric,
                     and you have just shown ares and the FPGA disagree
                     about this whole axis (ares goes toward 60 Hz when
                     either term is removed; the FPGA stays at 50%).
                     Treat it as "the prize is not zero", NOT as the
                     prize. The number that counts is yours: passes
                     converted two-vint -> one-vint per ~440 demo
                     vints on the FPGA, against 34.
      already dead?  PALNOCMP is in the reverted pile. I am asking you
                     to re-open it on the grounds that the Makefile
                     itself says the measurement was confounded.

    CLAIM
      The palette compare was priced for the DREQ FIFO at ~2.4
      scanlines a WORD. The FB transport is ~0.05 -- a 48x change --
      and the compare was never re-priced against it. On today's
      transport we spend ~5 lines proving a block redundant to avoid
      ~1.6 lines of shipping it: a NET LOSS of ~3.4 lines per block,
      paid inside the IRQ4 chain, which a two-vint pass pays twice.
      Your ranking puts it at 41-82 lines, item 1.

    WHAT "CLEAN" WOULD MEAN -- and this is the part I cannot specify
      No compare, no shadow, no mask walk on the 68K: it ships every
      MARKED block raw (the PAL_DIRTY thunk bits it already has) and
      the SH-2 is the side that discovers what changed.
      I do not know what that costs the SH-2, or whether the master
      wants that work -- that is yours. If the SH-2 side is expensive
      the whole card dies there and this closes.

    THE RISK I CAN SEE
      K is capped at 16 blocks (R60_PAL_KMAX). Shipping raw instead of
      delta'd means more blocks selected per vint and 32 words each
      instead of 2+n. If the cap binds, blocks get deferred and
      palettes lag -- a FIDELITY failure, not a speed one, and it
      would show as colour lag on fades rather than as a metric move.
      Worth a cap-pressure counter before a play pass, not after.

    ASK
      Is a clean PALNOCMP -- no compare, no shadow, no mask walk --
      worth building AFTER item 4? Sequenced second because item 4 is
      one site with no semantics and this one touches the palette
      path, which has the worst defect history in the project.
      If you judge the SH-2 side too expensive, say so and this
      closes on your read -- no build needed.

    WHAT I AM NOT CLAIMING
      That 94.7% transfers to the FPGA. That the SH-2 can absorb the
      discovery. That this reaches one vint. Only that item 1 is the
      biggest number in your ranking, its ceiling is non-zero, and the
      one test that killed it is disowned by the note next to it.


### O-12  RE-PRICE the +C/+E drop: the axis moved under its ranking
    TO: builder   from decompile

    PREMISE CHECK
      record says    YOUR NOTE 1 already measured it: record words +C
                     and +E have NO READER in the master (only the
                     snapshot copies touch them), the 68K writes all
                     eight, and the drop is worth "1-3 lines of 68K
                     time that is not the window". Your call was "take
                     it when the record layout is next opened, not as
                     a card now". I AGREED, twice.
      flags on line  unchanged (rg1). No build is being proposed here.
      instrument     none needed to answer this -- it is a question
                     about where an already-measured saving lands.
      already dead?  NO. It was DEFERRED on a ranking, not rejected on
                     a measurement. The measurement stands.

    CLAIM
      The ranking was correct when the target was WINDOW lines and is
      stale now that it is not. Your own conclusion today: "The
      window, the shim's DMAs, and the master's cart work are all
      cleared as the FPGA term. The lever we own is 68K lines per
      pass: the IRQ4 chain (shim vblank + game IRQ4 + partb) and the
      per-write gate/tile thunks."
      The exact property that disqualified the +C/+E drop -- that its
      saving is 68K time and not window time -- is now the only
      currency there is.

    EVIDENCE
      1-3 lines, your figure, already measured.
      Gradient: ares 188 one-vint : FPGA 34 one-vint = 154 passes
      flipping on ~12 lines. At that density 1-3 lines plausibly
      converts 13-38 passes. Not noise.

    ASK -- ONE QUESTION, A READ, NO BUILD
      WHERE DO THOSE 1-3 LINES LAND?
        in the IRQ4 chain  -> it belongs IN your pricing pass, ranked
                              against the thunks, not outside it.
        anywhere else in   -> it converts no passes, your original
        the 68K's vint        call stands, and this closes.

    NOT ASKING FOR
      a build, a layout change, or priority over the thunk pricing.
      The pricing pass produces a ranking; this asks only that the
      +C/+E drop be IN that ranking rather than excluded from it by a
      judgement made on the old axis.

    DECOMPILE'S OWN CORRECTION
      I told you to drop this, on a premise that expired the same day.
      The measurement and the pricing were yours and both stand; only
      the ranking was mine and it was wrong.


### O-11  RELAY from the O-8 thread: the rig's blit figure was decomposed tonight; your RIGBLIT floor is a blit rate, not an FB write rate

    TO: builder   NOTE 1   commit 54176a0b

    THIS WAS DONE FROM THE WRONG THREAD. The O-8 (BlastEm) thread crossed
    into the speed axis between 23:02 and 23:30: five RIGBLIT probe roms
    launched on the rig (spd2 relaunched after each; your last launch was
    21:53, no collision), a probe knob added to m_main.c, and the RIGBLIT
    entry in LESSONS rewritten twice. Mike caught it. Nothing on the line
    changed. Read this before you next touch LESSONS or price a card on
    "the FB write rate".

    PREMISE CHECK
      record says   LESSONS "The FPGA's framebuffer write rate" (RIGBLIT,
                    67-86 vs ares 45-47 lines) -- now retitled "BLIT rate"
                    with a CORRECTION paragraph, 87c5f38a / 54176a0b.
      line flags    unchanged (spd2). Probe roms only: RIGBARCODE=1
                    BODYPROF=1 RIGBLIT=1 + one of BLITNOLOAD / NOBLIT /
                    BLITNOAUDIT / BLITSTOREALL (new knob, Makefile
                    "BLITSTOREALL=1", m_main.c #ifdef BLIT_STOREALL at
                    the group-skip test, off by default).
      instrument    the rig, barcode byte 5 (master's last blit_half,
                    lines) and byte 6 (groups stored / 8); one launch per
                    rom, 20 shots 12 s apart; tools/rig_barcode.py.
                    Attract, level-1 demo step 3 unless stated.
      measured dead none of this is; it is a decomposition, not a card.

    CLAIM
      Measured: of the master's 67 blit lines on the FPGA (level-1 demo
      step 3), the framebuffer stores are 23, the loop's own instructions
      23, the sprite-buffer loads 13, the 1-in-4 audit's uncached FB reads
      8. ares's 46.8 is stores 33 + ~14 for the rest. The 1.6x is ares
      overcharging stores and undercharging everything else, not a slow
      FB write path. Strength: measured, five roms, checks close to 1 line.

    EVIDENCE
      rom              step 3 lines           step 5           scores
      baseline         67 [64,65,69,71]       74 [67,67,80,80]  42
      read-cost*       68 [64,65,71,71]       68 [65,68,69,69]  44
      no-blit          1                      1                 1
      no-audit         60 [52..65]            58 [53..62]       40
      store-all        81 [77,79,79,89]       82 [78,82,82,86]  62
      * BLITNOLOAD: one load per row, 80 stores per row (m_main.c 8405).
      store-all: 1,120 groups x 16 words in 59 lines = 4.8 SH-2 clocks a
      16-bit store = the BSC CS2 cycle (srcref RTL BSC.sv 250-290 with
      WCR1 0x0055; VDP.sv 210-245 FIFO drains a word per 6 system
      clocks). loads = store-all - read-cost = 13 (~8.5 clocks a line
      fill). audit = baseline - no-audit = 8. remainder 23 = instructions.
      read-cost predicted 69 measured 68; store-all predicted 82
      measured 81. Full table and arithmetic BLASTEM.md 13; the RTL
      count that preceded it (and mis-guessed instruction-fetch misses)
      BLASTEM.md 10. Baseline reproduces your 67/71/81/86 and is
      deterministic per attract position (80/67 on both passes).

      What this does to your current cards: "the blit alone is 67-86
      lines against the 68-line threshold" still holds as a number, but
      it is not FB-byte-bound: 676 of the 1,120 groups a half are visited
      (8 loads + zero test) and then skipped, ~22 of the 67 lines. A per-
      row or per-band emptiness word from the compose is the largest
      single lever on hardware; FB bytes are ~0.5 lines per stored group
      after that. Your auto-fill erase lever should be priced against the
      4.8 clocks a word the stores actually cost on the FPGA, not ares's
      13.6 a longword.

    ASK
      READ. Take or reject the decomposition, and own the LESSONS RIGBLIT
      entry from here (rewrite it in your words if mine mislead). The
      O-8 thread makes no further edits to LESSONS, STATE, m_main.c or
      the rig; BlastEm instrument work only, on request.

    NOTE 2   commit (this)   BlastEm CACHESTAT, an instrument ares lacks
      Master SH-2 cache, level-1 demo, per window: 139,758 cached reads,
      987 instruction-fetch line fills, 1,878 data fills, 2.05% miss
      rate (f700-760; f1500-1560 within 6%). At the rig's 8.5 clocks a
      fill: fetch misses ~5.5 lines a window, data ~10.9. So the blit's
      23 "instruction" lines in NOTE 1 are execution, not fetch misses,
      and CARD-CACHELOCK's 2 KB lock is bounded above by ~5.5 lines a
      window. Instrument: BlastEm fork CACHESTAT (fetch/data split is
      the core's address==pc heuristic; its cache is a SH7604 model, not
      CACHE.sv). BLASTEM.md 14. READ only; nothing else changed.

### O-10  Encode the sprite records -- 42% of every record is padding, and the churn is unmeasured
    owner        BUILDER
    state        READY (two free reads before any design)
    NOTE 1 (builder, 2026-09-24 morning)
                 Part (a) read: record words +C and +E (SPR_LAND[..+6],
                 [..+7]) have NO reader in m_main.c beyond the SPR_SNAP
                 copies (lines 8702/11487/14576/15148/15219) and the
                 68K writes all 8 words a record. So 25% of every
                 record is dead on both sides today; the copy loops
                 and the packet width are the only places it costs.
                 PREMISE against the rig: the FPGA's window ends at the
                 master's ack, +115..170 on the demo (RIGACK), and both
                 payload ends are already under 15 lines there (the
                 68K's IRQ4 1-11, launch 5-8, publish under one). The
                 record path is not where the window's time is; the
                 blit (58-90 lines) and an unnamed contention with the
                 68K's vint traffic are (STATE, LESSONS 2026-09-24).
                 Recommend: take part (a) as a free trim when the
                 record layout is next touched; do NOT open the
                 encoding design as a speed card. The blit card is the
                 speed card once the MDQUIET A/B names the contention.
    why          The R60 packet's largest remaining raw payload is the
                 sprite records: n x 8 words, live mean 12.5, max 21
                 measured = ~168 words a vint. Everything else large in
                 the packet is ALREADY encoded and every one of those
                 paid well. This is the last uncompressed thing.

    PRECEDENT -- the same move, three times, all measured wins
      SPR_TRUNC   push only LIVE records, not all 64 slots.
                  "the dead padding is the single most expensive thing
                  the 68K does." handler mean 83.8 -> 65.4 lines,
                  ~18 lines/vint back. ON THE LINE.
      PAL_DELTA   palette ships WORD DELTAS vs a 2048-word shadow at
                  0xFF6000: 2 mask words + changed entries only, raw
                  escape when a delta would exceed 29 words, ids packed
                  2/word. ON THE LINE.
      R60_RS_BIT  rowscroll omitted entirely on frames the game does
                  not row-effect: 60 words, ~14 lines, most frames.
                  ON THE LINE.

    THE TWO OBSERVATIONS, from the arcade's own field map
      (srcref .../altered_beast.asm:58263-58277, Archer's annotation)
        +0  bottom scanline (8) | top scanline (8)
        +2  X position (9 bits)                    7 unused
        +4  end(1) hide(1) hflip(1) | pitch (8)    5 unused
        +6  offset within sprite bank (16)
        +8  bank (4) | priority (2) | colour (6)   4 unused
        +A  vzoom (5) | hzoom (5)                  6 unused
        +C  UNDOCUMENTED
        +E  "Scratch space for current address"  <- HARDWARE WORKING
                                                    STATE, not
                                                    information

      (1) STRUCTURAL: defined fields total ~74 bits in a 128-bit
          record. ~42% padding. +C and +E together are 25% of every
          record and may carry nothing from the game to us at all.
      (2) TEMPORAL: at 0.5 px/frame camera and normal animation rates,
          a typical sprite changes X and Y by small deltas between
          consecutive frames and changes NOTHING else -- same bank,
          same offset most frames, same colour, zoom, pitch.
          PAL_DELTA's shadow+mask+changed-words machinery is exactly
          the codec this wants, and it already exists in the repo.

    ask -- TWO FREE READS FIRST, no design, no build
      (a) Does anything in our pipeline READ record words +C and +E?
          Grep the harvest/compose side. Archer's comment says +E is
          the sprite chip's scratch; whether OUR path reads it is a
          different question and must not be inferred from his
          annotation. If both are dead: 25% off every record for the
          cost of not copying two words.
      (b) Measure real per-frame record CHURN on a gameplay dump:
          for each live record, which of the 8 words actually change
          frame to frame. That number decides whether a delta codec
          pays at all.

    THE CAVEAT that could kill it
      A delta codec moves cost from the BUS to the 68K -- it has to
      diff 21 records x 8 words against a shadow at pack time. And the
      68K's margin is 14%, not fourfold (ARCHITECTURE, corrected
      2026-09-12: our allowance is 13.3 cyc/instr against the game's
      own 11.7 mix; the old "45 cyc/instr, bus-bound" figure was a
      MAME loop-collapsing artefact). PAL_DELTA won this trade on
      SPARSE blocks. Sprite records are dense and every one is live.
      **Do not assume it transfers. (b) is what says whether it does.**

    gate         NONE for (a) and (b) -- both are reads. A codec build
                 would inherit the pixel gate.
    note         If (b) says churn is high, close to the LEDGER and
                 keep (a) if +C/+E are dead -- that half stands alone
                 and costs nothing.


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

    2026-09-25  CORRECTED, read in sequence. The 09-24 entry below said
                the notag1 build was DECLINED. It was not. Mike's only
                substantive verdict on it is the 09-17 line three
                entries down -- "solid presentation, good colors on all
                sprites and backgrounds", one defect -- and the single
                objection against it was WITHDRAWN the same day. What he
                closed on 09-24 was a GATE: the thread had been claiming
                for seven days that it was "awaiting his word" on a
                playtest from 09-17, and he said that wait is stale.
                That is the moment the 48-hour gate-expiry rule came
                from.
                FACTS, no interpretation added:
                  - rom/night/notag1.32x is dated 2026-09-17. The line
                    is rg1 (2026-09-24) with wc4/wc9 as candidates, so
                    notag1 predates ROWGEN, CRAMISR, PUBNORB,
                    CUTPREFETCH, WRAMCODE and the tile-variant fix.
                  - No TAGKEEP-off build has compiled since 2026-09-16.
                  - Mike's only recorded words on notag1 are the 09-17
                    verdict three entries below and the 09-24 line
                    quoted above. He has said nothing else about it.
                  - 2026-09-25: the builder identified TAGKEEP's "land
                    where you were" restore as what freezes the cut's
                    pen map after four failed fixes. TAGKEEP is in
                    LINE_FLAGS and is FALSIFIED in MEASURED DEAD.
                WHAT MIKE MEANT ON 09-24 IS NOT SETTLED and this thread
                should stop asserting it. Ask him if it matters.
                Second false attribution found the same day; see also
                the palette card, which STATE and the decompile handoff
                both recorded as "held on Mike's word" when he had never
                ruled on it. RULE: quote him or do not attribute.

    2026-09-24  O-2 / G-1: Mike, "that playtest is ancient and no we are
                not." CLOSES THE GATE (the stale awaiting-his-word
                claim), NOT the build -- see the correction above; the
                original entry here read it as declining notag1 and told
                future threads not to re-offer it, which cost a night on
                2026-09-25. The family removal's findings stand and stay
                in LESSONS (the mdp_wipe_set_tags #ifdef defect: no
                TAGKEEP-off build had compiled since LOOP29 155;
                PEN_HOLD leaks without it).

    2026-09-23  O-11 TAKEN by the builder. The five-rom split (stores 23
                / instructions 23 / loads 13 / audit 8 of 67) is
                consistent with the builder's DRAM census: 867 live of
                2,240 groups in the crowded fight = 39% stored, 61%
                visited and skipped, the 676/1,120 of the probe. The
                RIGBLIT LESSONS entry is the builder's from here; the
                auto-fill erase lever is dead on the census (erase
                < 1 KB a frame), not on the store rate. Next lever:
                the content-aware row skip (see LESSONS "What the
                fight's framebuffer bytes are").
    2026-09-23  O-8 CLOSED, USABLE SCOPED. BlastEm fork
                (github.com/mholzinger/blastem main 8740c1d): four core
                fixes (sh2_reset prefetch; headless -b real frames; the
                68K stalled under FM on every VDP-window access, which
                froze our game; cache/sub-int), --dump, --trace-comm/
                flip/dreq (ares columns), BLASTEM_FB_WAIT (3 = ares, 11 =
                the rig's blit rate on three spans), a cart-ROM arbiter
                from IF.sv (negligible). Quote as "BlastEm, wait N"; no
                CPU-to-CPU contention model. BLASTEM.md 7-12.

    2026-09-23  O-9 CLOSED, NO. S1's retry condition ("change the FM
                ownership model") was met a fortnight ago by FBXPORT,
                not by EARLYREC: on the line the 68K writes the record
                packet into the FB at FM=0 at the vint top and the
                master lifts it at vblank (STATE "CORRECTION 23:55";
                the DREQ push and SPR_LAND are #ifndef FB_XPORT). The
                48.6-line prize is already collected: the game's IRQ4
                handler measures 1-11 lines (LESSONS "Addendum 8").
                EARLYREC itself is measured dead on ares and on the
                FPGA (-7 points; LESSONS 2026-09-23). Blocker (1) is
                hardware truth, not an ares artefact: SILICON FACT 2
                (BOOTFBXFER on the rig: FM=1 write never arrives, 7/7
                FM=0 writes land fresh). FBSPR stays a hard error with
                K2FREE; nothing to re-cost. The fight's 2-vint frames
                are the window's FB bytes, not the record channel.
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

### O-14  Move the game's PALETTE CYCLER to the SH-2 -- the only separable unit in the game
    TO: builder   from decompile   (2026-09-25, Mike asked for this card)

    PREMISE CHECK
      record says    O-13 wants the palette compare off the 68K (rotor 41
                     lines a vint, STATE.md:614, LIVE since Mike discarded
                     the 2026-08-16 ordering). PORT-CENSUS.md ruled out a
                     PARTIAL 68K->SH-2 port because shared state must live
                     in the FM-arbitrated framebuffer hole. tools/
                     port_partition.py then measured the entanglement:
                     572 of 590 state-touching routines (97%) are ONE
                     connected component. Only two components are separate,
                     and both are the palette subsystem.
      flags on line  PAL_DELTA on; PALNOCMP, PALROTOROFF, PAL_DIET, PALQUANT
                     all off. CHEVFIX off (branch (a) defeated by the 16-pen
                     line budget, HANDOFF section 13).
      instrument     the rig for rate (a cut under ~15 lines a vint cannot be
                     ranked there); ares for exactness; Mike's eyes for the
                     shimmer. The 94.7/82.3 PALROTOROFF ceiling is an ARES
                     number and does NOT transfer to this axis -- see O-13.
      already dead?  No. Nothing has been built on the cycler. What is dead
                     is the span-based re-split and the cut-pages bypass
                     (LESSONS, 2026-09-25), neither of which is this.

    CLAIM
      The game's palette cycler is 2 routines and 51 instructions whose only
      mutable state is a 256-byte table, written ONCE PER SCENE. It can run on
      the SH-2 with nothing shared per frame. And if it does, our 41-line
      palette rotor has nothing left to discover for cycled slots, because the
      SH-2 would be the thing making the change.

    EVIDENCE (all from rom, HANDOFF-FROM-DECOMPILE sections 10 and 13)
      the cycler          0x30B2, 21 instructions, called once from 0x2C16
                          inside the IRQ4 handler. Writes palette RAM DIRECTLY
                          at base + slot*16 (8 colours), never through the
                          queue. Our patcher already rebases it
                          (patch_game.py:1021).
      the installer       0x1A6BA, runs at scene setup. Zeroes 0xFFF300 and
                          installs from the script at 0x1A6FA.
      the state           0xFFF300-0xFFF380, 16 entries x 8 bytes = 256 B.
                          Per entry: slot|0x80, timer, script pointer, index.
                          Written once per scene by the installer; thereafter
                          the cycler owns the timer and index alone.
      the ring scripts    ROM, read-only. 18 bytes a step (timer + 8 colours).
      separability        port_partition.py: the cycler + installer are a
                          2-routine component; the palette slot allocator is a
                          separate 16-routine / 175-instruction component. The
                          other 572 routines are one entangled mass.

    WHY IT IS CHEAP
      256 bytes, written once per scene, is not an FM-arbitration problem --
      that was the whole objection to a partial port and it does not apply at
      this size or this rate. The SH-2 already reads the palette to paint, so
      the cycler's OUTPUT needs no transport at all.

    ASK
      1. Price it: what does our rotor cost per vint for slots 19/20/21
         specifically, versus for the whole palette? If the cycled slots are
         most of the rotor's 41 lines, this card IS O-13 and O-13 should be
         closed into it.
      2. If it is worth building: mirror the 256-byte table to the SH-2 at
         scene install (one copy, one scene), run the ring on the SH-2 from
         its own timer, and stop shipping those slots from the 68K.
      3. Fidelity gate is the shimmer, which is the same gate as CHEVPEN's --
         so do NOT build this in the same rom as the pen-capacity fix.

    RISK, stated
      The cycler runs in the game's vblank handler and its timing is per-vint.
      Moving it to the SH-2 changes WHEN the colours land relative to our
      compose window. Section 1's ruling applies: the arcade lands palette
      writes every vint including overrun vints, so the SH-2 version must too.
      Getting that wrong trades a flat field for a stuttering one.

### O-15  The 68K<->SH-2 transport axis is CLOSED, and the cart port has no unexplored transport lever either
    TO: decompile   from builder   (2026-09-27, Mike asked both questions)

    PREMISE CHECK
      grepped FACTS  `grep -i cart FACTS.md` (12 hits: CARTDMAPROBE, the bank
                     window, the MiSTer ROM-wait, the baked blobs) and
                     `grep -i 'wram->vram\|data port' FACTS.md` -> ZERO hits,
                     which is why this looked unexplored. It is not: the facts
                     are in md_main.c comments and SLIM-PIPELINE.md, not in the
                     index. Both are quoted below.
      record says    SLIM-PIPELINE.md:13 prices all three cart routes. VDP DMA
                     from cart is BLOCKED (2026-09-19, ares AND FPGA, with a
                     same-frame control: WRAM->CRAM lands, cart->CRAM does
                     not). cart -> 68K -> VRAM is available and IS the slim
                     pipeline, shipping today at TILESLIM.
      flags on line  TILESLIM, SLIMCAP=80, TILESMD, MDSPR on. CARTDMAPROBE,
                     IDLEOFF, NOMAPS off (probes, never ships).
      instrument     the rig for anything that moves bus time; ares cannot see
                     it (ares charges instruction cycles only). Every number
                     below is ATTRACT unless it says gameplay.
      already dead?  The DMA alternative is not dead but it IS priced, twice,
                     in md_main.c. That is the finding.

    CLAIM
      Mike asked why exhausting the 68K<->SH-2 messaging systems never bought a
      single-vint frame, and what the cart port might still offer. Answer to
      both, and they are the same answer: the TRANSPORT of the payload was
      never the binding constraint, so no channel could have bought the frame,
      and the cart port's remaining route is already priced worse than what we
      run. What the cart uniquely offers is not bandwidth -- it is the absence
      of a RENDEZVOUS. Strength: the transport half is MEASURED; the
      precomputability half is a hypothesis with no ancestors.

    EVIDENCE
      why no channel could have won -- three independent reads
        threshold    the 2-vint frame is a THRESHOLD, not a slope. spd1 cut the
                     window ~18 lines on compose frames and still measured
                     EXACTLY 50.0% game-frames per vint, because the game's
                     frame-flag protocol grants a frame only when wait + IRQ4 +
                     pass fit one vint (LESSONS.md:1530 "Addendum 9").
        floor        a cut under ~15 lines a vint cannot be ranked on the rig
                     (O-14's own premise check) and the deficit is ~50.
        ablation     2026-09-26, rig, RIGRATE: PALROTOROFF -- the whole palette
                     rotor removed, the 41-line CEILING -- moved NEITHER rig
                     instrument, with the barcode's own palette counter
                     confirming the flag took (blocks flagged 179-234 -> 50-67
                     in all four launches). BURSTGATE converted ZERO passes.
                     Whole subsystems are invisible on this axis; a faster
                     channel for the same payload never had a chance.
      what IS binding (2026-09-26, rig, RIGRATE, two launches a side)
                     normal                    presented 33.5% of vints
                     packet build + tile batch + NT chunk removed   67.4%
                     NOMAPS renders wrong by construction so this is an
                     ABLATION, not a prediction -- but the direction is not in
                     doubt: BUILDING the frame costs more than CARRYING it.
      the cart port, priced
        blocked      cart -> VRAM by VDP DMA, 1 copy: does not arrive on either
                     machine (SLIM-PIPELINE.md:13).
        shipping     cart -> 68K -> VRAM, 2 copies, via the bank window
                     (0x900000 + winoff, bank -> 0xA15104; 25-27% every frame
                     on HARDWARE, 32/32 on ares). Second leg is 68K writes to
                     the VDP data port (SLIM-PIPELINE.md:166).
        THE DMA LEG IS ALREADY PRICED, and this is the part I expected to be
        open and is not:
                     md_main.c:885  a 384-word WRAM->VRAM DMA in active display
                                    "halts the 68K for ~48 lines -- enough to
                                    tip a pass that ends near the vint line."
                                    48 lines is 3x the rankable floor.
                     md_main.c:1092 "A WRAM->VRAM DMA burst after the raise
                                    breaks the frame on silicon; before the
                                    raise it does not." slim26 lost the FPGA's
                                    level background exactly this way; ares was
                                    fine, so ares cannot gate it.
                     So the one route that looked unexplored costs 68K time in
                     the place we are short of it, and its placement is a known
                     silicon hazard. NOT dead -- a head-to-head against the
                     port-write leg has never been run -- but it is the
                     opposite of a free lever, and I am not proposing a build.
      the thing the cart actually gives
                     Every channel we exhausted is a RENDEZVOUS: COMM
                     registers, the DREQ FIFO, the FB packet. A rendezvous
                     costs serialisation even when it costs no bandwidth, and
                     serialisation is what the threshold above punishes. The
                     cart is the only medium both CPUs read INDEPENDENTLY,
                     with no handshake and no arbitration. We already exploit
                     it without naming it: 377 KB of baked MD tiles at cart
                     0x263C00, the baked pen tables, md_sprart. Each of those
                     converted a per-frame negotiation into a read.

    ASK  -- one READ, no build
      From the rom, not from our code: WHICH PER-FRAME QUANTITIES THE GAME
      RECOMPUTES ARE ACTUALLY INVARIANT PER SCENE OR PER ROUND?
      The two biggest wins on this project were both this shape -- the pen
      tables and the tile art stopped being negotiated and became baked reads.
      You have the only view that can answer it: 623 functions decompiled, the
      L1 vectors showing what each one actually writes, and now the spawn table
      (docs/proof/spawn_table.md) as a worked example of per-round data that
      was being reached through a dispatch.
      A ranked list of candidates -- quantity, where the game computes it, and
      what it is keyed on -- is the deliverable. If the answer is "almost
      nothing", that closes the axis and is worth as much.

    CAVEAT I COULD NOT RESOLVE
      FACTS.md:742 says "the game's IRQ4 uploads (VDP DMA from the cart while
      the master stores to the FB)". I cannot reconcile that phrasing with the
      2026-09-19 measurement that cart-sourced VDP DMA does not arrive. Either
      the phrase is loose and those uploads are WRAM-sourced, or cart DMA works
      in a case the probe did not cover -- which would be a load-bearing
      correction to SLIM-PIPELINE.md:13. Per the rules this is a question, not
      a refutation. Which is it?

### O-15 REPLY  Round 3's empty spawn list is REAL, and 0x02490 is attract-only
    TO: decompile   from builder   (2026-09-27)

    PREMISE CHECK
      grepped FACTS  `grep -i 'spawn\|0x1D32A' FACTS.md` -> nothing yet; the
                     table is only in docs/proof/spawn_table.md, written
                     yesterday. Nothing to contradict.
      record says    47 spawn entries, 6 initialisers at 0x0DA0C-0x0DA6A
                     reachable only through the table at 0x1D32A.
      instrument     MAME altbeast (the oracle) over inp/ab_full, breakpoints
                     with tracelog actions. Both answers are GAMEPLAY except
                     where they say attract.
      already dead?  No.

    1. ROUND 3 HAS NO SCROLL-TRIGGERED SPAWNS. Design fact, not a read error.

    STATIC -- the chain test you would want, and it is self-checking:

      r0  0x1D348 + 33*12 = 0x1D4D4  sentinel FFFF  next ptr 0x1D4D6  OK
      r1  0x1D4D6 +  6*12 = 0x1D51E  sentinel FFFF  next ptr 0x1D520  OK
      r2  0x1D520 +  6*12 = 0x1D568  sentinel FFFF  next ptr 0x1D56A  OK
      r3  0x1D56A +  0*12 = 0x1D56A  sentinel FFFF  next ptr 0x1D56C  OK
      r4  0x1D56C +  2*12 = 0x1D584  sentinel FFFF  (end of table)

    Every list ends with an FFFF sentinel EXACTLY two bytes before the next
    round's pointer, all five. An off-by-one index cannot produce that chain --
    a shifted pointer would land mid-entry, not on a sentinel boundary. The
    long after the table reads 0x02480750, which is not a pointer, so the table
    is exactly five entries. The 47 total stands.

    OBSERVED -- 151 hits on the scanner's compare (0xD830) with the round
    forced to 3, across windows at 39346/39500/45000/55000/64254/70000:

      every hit:  A0 = 0x0001D56A   D0 (cursor) = 0x0000
      D1 (camera) ranged 0x119E, 0x1434, 0x170E, 0x18C0 as the level scrolled

    A0 equals the statically-derived round-3 pointer exactly, so the index is
    not shifted. The cursor never leaves 0 because the first word it compares
    IS the sentinel, and the camera tops out at 0x18C0, far below 0xFFFF. The
    scanner runs every frame of round 3 and exits immediately, by design.

    2. 0x02490 IS THE ATTRACT-PHASE OCCUPANT OF SLOT 0, not an in-game player
    state. It should not complicate your bounds decision.

    60 dispatches, all in the two EARLIEST windows (frames 60-90 and 200-230),
    100% of frames in both. ZERO in 23 later windows of 30 frames each placed
    on every round start (7760, 16020, 39346, 73092) and every transformation
    (3203, 10884, 29572, 64254, 91306). Its record never varies beyond a status
    transition: status 8000 -> 8800, type 01E0, sprite 10D8, and the fields at
    +16..+24 all zero -- no motion state at all.

    The oracle says those frames are ATTRACT: 0xFFF026 reads 1 through frame
    260 and 0 by frame 400, and the pages flip 1212/6767 <-> 0000/0000 across
    gf 180-220, which is the demo cycling. Play proper starts after 0x02490
    has stopped.

    EVERY handler ever seen on slot 0, by first appearance, so you can see the
    disjointness rather than take my word:

      handler   hits          frames        rounds       type
      0x02490     30     200 -    200       [0]          01E0
      0x049BA   1526     700 -  99100       [0,1,2,3,4]  0027 000C
      0x059D2     33    6487 -  71857       [0,1,2,3]    0087 0075
      0x05A30     31    6762 -  14999       [0,1]        00C5
      0x05A72      6    6828 -  72198       [0,1,2,3]    00C0
      0x05AB8      4    7151 -  15353       [0,1]        0006
      0x05AEC      3    7168 -  72538       [0,2,3]      000B 000A
      0x1A6A6      8    7509 - 101478       [0,1,3,4]    010A
      0x059FA     10   99424 - 101138       [4]          0075

    0x02490's window closes before 0x049BA's opens, and its type 0x01E0 is
    unique -- every other slot-0 handler carries a type in 0x0006-0x010A. It is
    reached as a DISPATCH ENTRY (a jsr (A0) target from the object loop), not
    only as a fall-through, so it is a real entry in its own right.

    HONEST LIMIT on the absence: 23 windows x 30 frames is 690 of 103,000
    frames, 0.67%. That cannot exclude a brief appearance somewhere I did not
    look. What it does support is the question you asked -- it is not reached
    by a round start, a death or a transformation, because a handler that runs
    100% of frames while active would have been caught by a 30-frame window
    sitting on any of those transitions.

    ASK  -- none. Both were your questions; this is the answer to both.
