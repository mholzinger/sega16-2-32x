# LOOP 23 — PER-SITE FM GATING: the 68K stops paying for the window

Mike's verdict (LOOP22 close): per-site gating; **fallback = the
set-once model** (FM never returned, video writes become Chaotix
change-queues over the 4-word push). Read LOOP22 for the arc that
funded this.

## BASELINE (hardware, two Mike savestates, T_pktslim_flick/BUILD c027dcd7)

    window/ack-wait mean 73.7-77.2 lines/vint, worst 224-230 of 262.
    own-tail 18.4-18.8. Game gets 64-65% of the 68K.
    SH-2 window: 12.78 ms/cycle (blit 5.75, glue 4.32, flip+restore
    1.85, apply_cram 0.65, copy_pages 0.21).

The wait is REAL SPAN (LOOP11's idle-token rejection re-confirmed by
the decomposition). The 68K's share of it is what this loop deletes.

## THE CENSUSES (wpcatch, MAME -debug; run BEFORE design was frozen)

  - **Game FB WRITES: 48 distinct sites, full attract->load->gameplay
    run.** Steady gameplay = ~59 stores/frame from 16 sites (sprite
    upload loop 0x902AD8..4C, text/HUD printers 0x903A9E/AC2/AFE +
    0x904D96, one tile-column writer). The rest are scene-load bulk
    copy loops (0x9036C0 alone = 143K stores) — burst-time, where a
    poll per store is invisible.
  - **Game FB READS: exactly ONE site** — 0x903AA6, the text printer's
    RMW partner (byte reads of 0x85FCxx). Every other FB read in the
    entire run is OUR SHIM's consume path (PC 0xFF0xxx), which already
    runs at FM=0 by construction.
  - Shim's own FB write: the 0x851A00 packet-live word (md_main:560
    consume block) — sequenced in part A, FM=0.

## THE DESIGN

**1. Reorder the vint chain with an RTE TRAMPOLINE** (md_start.s):
today `_vblank -> shim (consume+push+FM window+SPIN) -> game IRQ4 ->
rte`. New:
  - part A (shim, FM=0): consume packet, DREQ push, housekeeping.
  - swap the interrupt frame: save real (SR,PC), push fake frame
    aiming at part B (fake SR keeps IPL so no re-entry), `jmp` game
    IRQ4. The game's vint uploads run UNBLOCKED at FM=0 — arcade
    games upload during vblank, so the raise must come after them.
    Any internal exit works: every path ends in rte popping OUR frame.
  - part B (via the game's rte): raise FM, post the window command,
    restore the real frame, rte. **No spin anywhere.** The game's
    main loop runs concurrent with the whole SH-2 window.
  - CUT30 skip logic moves to part B unchanged (a vint that skips its
    window just never raises).

**2. Gate thunks for the 49 game sites** (patch_game, generated +
unidasm-verified like the pal thunks; displaced instructions from the
post-remap pre-jsr copy — pal_disp_saved discipline):
      gate:  tst.w (0xA15100).l      ; N = FM
             bmi.s gate              ; spin only while SH-2 owns
             move.w sr,-(sp)
             ori.w #0x700,sr         ; mask: a vint between poll and
             tst.w (0xA15100).l      ; store would raise FM under us
             bmi.s unmask_retry      ; (drop mask, spin again)
             <displaced store/read>
             move.w (sp)+,sr
             rts
    ~40 cycles/store x 59 stores/frame ~= 0.3 lines. The one READ site
    gates identically (its displaced instruction sets flags — the SR
    restore must not clobber the result flags: displaced op runs AFTER
    the re-check, flags re-established by... NOTE: restore SR wipes
    CCR. Sites whose following code reads CCR from the displaced op
    need the CCR merged into the pushed SR before restore — audit each
    of the 49 for CCR liveness in the generator, same rigor as thunk
    B's "nothing between here and the next flag-setter reads CCR".
**3. SH-2 drops FM itself** at window end (last FB access) via
    MARS_SYS_INTMSK bit 15, then posts the ack COMM as today. Part B
    checks the ack before raising: SH-2 still busy -> skip this vint's
    window (existing skip machinery).
**4. The flip stays at k2-start inside vblank** — the trampoline order
    preserves it: game vint finishes in early vblank, part B raises,
    the SH-2's flip lands within the V∈[DF,E2] gate. The k2
    drain->flip->restore invariant ("game writes staging only
    post-ack") holds automatically: gated stores CANNOT land while
    FM=1.

## GATES

  - MAME pixel A/B vs T2 (MAME is FM-lenient — never drops — so a
    gated build must render IDENTICALLY there; any diff = protocol
    breakage, not hazard).
  - wpcatch CONFORMANCE run: every FB-touching game PC in the full run
    must be in the generated gate list — the completeness proof, run
    per build.
  - state_health on ares: window/ack-wait should COLLAPSE (73.7 ->
    ~consume+glue). Play pass: any wrong-pixel-forever = a missed site
    or a CCR clobber.
  - 60Hz arithmetic check after: game % of 68K at 30Hz should jump
    from ~65% toward ~90%; then CUT60.

## DERIVATION COMPLETE (tools/fmgate_derive.py — run it, don't trust prose)

  - Census PCs are PREFETCH-SKEWED (a reported PC disassembled as a
    work-RAM read — impossible for an FB-write hit). Every PC resolved
    to its true accessor against the listing; 43/48 mechanical, 5 by
    neighborhood (the four once-per-frame text-reg stores 0x2ADE..2B08
    — abs.l forms — and one boot-time indexed store 0x573E).
  - The "read site" dissolved: 0x3AA4 is `clrb %a1@` — the 68000 CLR
    reads before writing. One store gate covers both. ZERO pure reads.
  - SR-context census: NO site is both-context. The ENTIRE sprite
    upload is VINT-only -> ungated by trampoline order. 30 MAIN-only
    store sites cluster into SEVEN subsystem spans with ~22 entry
    points, verified by a whole-listing control-transfer scan to
    fixpoint (0x369C found as a 17-caller alternate entry; span 6
    enters by fallthrough — generator must gate its head).

## MECHANISM (final)

  - Part A (C): overrun belt (FM still 1 -> bounded spin, counted),
    consume (FM=0), push, stage_play, wcmd computed and STASHED.
  - Trampoline (md_start.s): fake frame (SR=0x2700) -> game IRQ4 runs
    its uploads at FM=0 -> its rte lands in part B.
  - Part B (asm): defer if interrupted-PC ∈ spans-or-thunks, else
    publish comm2/comm10/heartbeat, raise FM, post stashed wcmd;
    rebuild real frame; rte. No spin anywhere.
  - Gate thunks at the ~22 entries: poll FM=0 then displaced entry
    instruction(s); the vint-between-poll-and-store race is closed by
    part B's defer, so NO SR masking and NO CCR hazard from masking.
  - SH-2 drops FM itself (MARS_SYS_INTMSK bit 15) before clearing
    COMM0 (the existing ack).
  - Heartbeat: single post at part B; pickup is immediate under this
    protocol (the mid-window refresh existed for pickup lag).

## NEXT (implementation, in order)

  1. patch_game FMGATE=1: entry-gate thunks + span table header.
  2. md_start.s trampoline + part B; shim_vblank split (stash wcmd).
  3. SH-2 FM drop at ack.
  4. Gates: MAME pixel A/B vs T2 (FM-lenient MAME must render
     IDENTICALLY — any diff is protocol breakage); wpcatch conformance
     (every FB-hit PC ∈ gated spans/vint-path); ares state (window/ack
     should collapse from 73.7); Mike's play pass.

## BUILT: SIX DESIGNS, TWO SURVIVING INVARIANTS, ONE SHIP (v6)

The trampoline + gate thunks were RIGHT ON THE FIRST BUILD (probe:
trampoline-only rendered clean). Everything else was window-TIMING
physics, mapped by five failed variants — each killed by a probe in
one run, none by ares:

  v1  k2 post-game-vint      -> flip missed vblank (hb V=04..F3),
                                skips == cycles, black screen
  v1b k1 post-game-vint      -> compose/blit launch deadlines slipped
                                ~2-3ms: garbled sbuf rows EVEN IN
                                LENIENT MAME (k1-skip probe = clean,
                                naming the mid-frame window as poison)
  v3  k1 raise post-consume  -> consume span (mean 10.6, MAX 70 lines)
                                re-slipped the same deadlines
  v4  k2 consume pre-raise   -> tripped the V<=E2 flip gate 347/419
                                (clean STILLS at ~5Hz flips — a frozen
                                display reads as clean; check skips)
  v5  k1 publish skipped     -> packet rate halved: mixed-generation
                                nametable speckle (the MDVERIFY class)

**The two invariants:** (1) each window must START within ~1ms of its
old slot (compose/blit deadlines); (2) the k2 flip gate must read an
ON-TIME V. v6 satisfies both:
  - k1: raise+post AT ENTRY, no spin, no consume. Game runs concurrent
    with the whole k1 window; its vint-path stores (sprite list, text
    regs — vint-only by census) land during FM=1 and are SACRIFICIAL:
    rewritten every vint, consumed at 30Hz, so the lost copies are
    exactly the frames the display never showed. ARES MUST CONFIRM.
  - k2: heartbeat = ENTRY V (written before the post as the gate
    demands), then consume (the k1 window's packet — its only FM=0
    shim slot), then the old raise/spin/drop; no comm12 rewrite at
    raise and no live spin refresh (a refresh racing the SH-2's single
    gate read would swap in the late V). Worst-case consume pushes the
    flip toward vblank's end — the deferred-latch path absorbs it
    (watch flip-late-latches on hardware).
  - k1 vints do not push (push/arm pairing: a push must follow its own
    window's re-arm). PKTSLIM made the k1 packet bitmap+tag only;
    marks accumulate in the OR-bitmap and ship on k2.
  - packet consume extracted to md_consume() with an FM guard.

MAME gates: attract + gameplay clean, skips=0, conformance census
PASS (48 write PCs = gated spans + vint path + our thunks/shim).
Shipping rom untouched (statics exact, _end 0x06018138).

**ARES roms: `rom/test/U_fmgate_flick.32x` (full stack) /
`U2_fmgate.32x`.** What only hardware can judge: the write-discard
sacrifice on k1-vint sprite/text-reg stores (MAME lands them), the
belt/defer rates, and the real 68K win — expect window/ack-wait to
drop by roughly the k1 share (~half of 73.7). The k2 spin is the
remaining half; it goes when the flip-span split lands.

## ARES ROUND 1 (U roms): FMGATE LANDED — window/ack 73.7 -> 48.2,
## game 64% -> 75% — with one truthiness bug painting sprites black

Mike's U state + corpus: speed win real; but smoke/enemy sprites ALL
BLACK, palette shifting, slow tile load-in, tearing, rejects 8.6%.

Savestate forensics (PAL_SH per sprite set, PAL32 bitmap, mirror):
sets 07/09 had PAIRS allocated, MIRROR data present, PAL_SH ZERO —
palette transport losing exactly some blocks. Cause: `window_ok =
fmg_k2old` — a THREE-STATE flag truthed as a bool, so k1 vints (==2)
pushed after all. The 4-word k1 packet drained into the still-armed
k2 channel, bumping the k2-tail landing 72 -> 76 words: whitelist-
REJECTED WHOLE, pal marks already consumed on the MD side. Also the
k1 push's word-80 harvest leaked tile marks (the slow-load-in
worsening). T-state comparison: 15/16 sprite sets populated pre-FMGATE
vs 6/16 under U — the fingerprint that named it.

Fix: `window_ok = (fmg_k2old == 1)` — one line. V roms cut.

NOTE for state_health: under FMGATE, dreq_incomplete ~= all k2 pickups
with residue 596 is EXPECTED (the skipped k1 push); teach the reader.

Ares roms: `rom/test/V_fmgate_flick.32x`. Open on hardware: the
rejects/cadence regression (8.6%, 2.28 vints/cycle) and tearing —
re-read on the V state before designing against them.

## ARES ROUND 2 (V state): FIX CONFIRMED; the reject class named

V state (fixed build): catastrophic pen drift 20 -> 5 (palette
transport healed), game 76%, window/ack 46.0. Remaining, re-measured
on the fixed build before designing (the standing rule):
  - V-gate rejects 10.6%, cadence 2.29 vints/cycle. Cause: the game's
    main loop RUNS now — including its own IRQ-MASKED critical
    sections — so vint entry lands past the E2 bound ~10% of vints.
    The E2 tightness protected pres-1.0's 75-row blit slices; under
    pres-2.0 only the flip needs vblank and the deferred latch absorbs
    stragglers. WIDENED under FM_GATE: MD entry gate DF..E8, SH-2
    heartbeat gate DF..EA.
  - flip-late-latches 24% of cycles: the v6 consume-before-raise tax.
    Latency, not corruption; shrinks when the flip-span split lands.
  - state_health now explains the FMGATE residue==596 signature
    (skipped k1 push, by design).

Ares roms: `rom/test/W_fmgate_flick.32x` (BUILD 23066c68). Expect
cadence back toward 2.05 and rejects to low single digits; sprites
should be COLOURED (the V fix, first visual check on hardware).

## ARES ROUND 3 (W state): gate-widening did NOT move rejects (8.6%,
## cadence 2.27) — which is what NAMED the class: FRAME OVERRUN

Entries weren't landing at E3..E8; they were landing mid-frame — the
pending vint fires at rte after a k2 vint that overran. The chain: the
pre-raise consume delays the k2 flip -> ~19% of flips latch late ->
the SH-2 latch wait stretches the window -> the 68K's spin stretches
past the frame -> the NEXT vint enters late -> reject -> retry ->
2.27 vints/cycle. The gate widening (kept — it is correct and free)
was aimed one level too shallow.

**v8 — THE DOUBLE-BUFFERED PACKET** (the design every earlier variant
was circling): k1 publishes to buffer A (0x11A00), k2 to buffer B
(0x1E800 — the 2KB FB hole the PAL32 hunt catalogued). The 68K
consumes BOTH in the k2 tail, post-flip, deadline-free, with
stage_play (self-clearing) between so the stagings don't collide. No
consume sits on any raise deadline; nothing overwrites an unconsumed
packet; packets stay at 2/cycle. md_consume() takes the buffer base;
the SH-2 alternates the publish target by k.

MAME: clean frames, cycles 413, no garbling; MEANtotal 57.8 (the
double consume moved ~4 lines INTO the measured tail — it is the same
work, now at a safe time). Expected on ares: flip-late-latches and
rejects collapse, cadence back toward 2.05.

Ares rom: `rom/test/X_fmgate_flick.32x` (BUILD 3884fda9+).

## CLOSED. Mike on X: "this version is more playable than the builds
## leading up. sprite load in seems better but the screen tearing is
## where we need work."

FMGATE v8 ships as the canonical-candidate state. Hardware scoreboard
for the loop: 68K handler 105.2 -> 63.8-81.0 (scene-dependent), game
60-65% -> 69-76%, window/ack 73.7 -> 46-64, flip-late 19% -> 7.2%,
rejects 8.6% -> 6.0%, cadence 2.19-2.27 (from 2.05 — the residual gap
IS the k2 spin's heavy-scene overruns). Sprite-freshness worry did not
materialize in play. Tearing carried to LOOP24 as THE item.

Canonical line (candidate): make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1
SPRTRUNC=1 BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16 FBSPR=1
FBTEXT=1 CUT30=1 PAL32=1 PKTSLIM=1 FMGATE=1 (+FLICKFUSE for play).
