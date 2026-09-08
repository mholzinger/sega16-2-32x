# LOOP 16 — ONE PASS, TWO PRIZES: SINGLE-SNAPSHOT COMPOSE + THE
# 2-WINDOW CYCLE (band tearing dead + game CPU 50% -> ~66%)

Kickoff. Read LOOP15 (the wrap/speed era: WINSPAN, the handler-mean
meter, the per-word push) and docs/log/DEVNOTES.md. Where LOOPs and
ARCHITECTURE.md disagree, ARCHITECTURE wins.

## WHERE THIS STANDS (2026-08-16, Mike's v6 state + verdicts)

BUILD 9e571923 (rom/s16_mdbgall_v6.32x), canonical line
`make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1`. Mike: jitter GONE,
"VERY presentable"; speed is the complaint. The meters (all
always-on now, from any state):
  - 68K handler mean 126-132 lines/vint -> game ~50% of the MD 68K
    (~38-40% of the arcade's 10MHz 68000).
  - Split: window/ack-wait mean 48.8 (master's pre-ack FM work) vs
    OWN-TAIL mean 83.1 = consume 9-10 (post palette-skip) + DREQ
    push ~45-52 + staged-DMA playback + glue.
  - dreq misaligned = 1 per session (per-word push; class closed).

**THE OWN-TAIL IS AT ITS FLOOR in this architecture** (2026-08-16
analysis): sprites already ship 1x/cycle, palette is already
dirty-driven, regs must ride every window that composes, and TEXT AT
1/3 RATE IS A DOCUMENTED REGRESSION (the k1-only apply, LOOP7-era —
almost re-bought this session; the apply comment at m_main's DREQ
tail carries the negative). Only removing a WINDOW removes its cost.

## THE DESIGN

Today: 3 windows/cycle (k0,k1,k2); each latches regs and composes
one band from ITS latch -> adjacent bands sample the game up to 2
vints apart = the band-tearing class; and every vint pays the full
handler (~130 lines mean).

The pass:
  1. **Single-snapshot compose**: latch layer regs + rowscroll ONCE
     per cycle (at the k2 pickup, with the sprite snapshot); all
     three bands compose from that latch. Band tearing dies by
     construction (one game-state per displayed frame). Scroll
     latency becomes a uniform 1 cycle (the pres-2.0 dx class
     already documents display latency; this makes it uniform).
  2. **2-window cycle**: with per-window reg freshness irrelevant,
     drop the k0 window entirely — the MD posts nothing on that
     vint; its handler is just the game's own vint chain. Transport
     re-plans: sprites 1x/cycle (unchanged), text chunk on BOTH
     remaining windows (rate preserved: 2 chunks/cycle), palette
     dirty-pairs on both (rate preserved), regs on both (feeds the
     single latch; second is cheap insurance). DREQ landing/phase
     logic: wskip cycles mod 2; the SH2's phase-remembering rule
     ("k = prev+1") adapts.
  3. The master's 3 compose/blit phases redistribute across 2
     windows + post-ack time — the master has MORE time per window
     (its wait share shrinks too); bq/band machinery unchanged.

EXPECTED: handler mean ~130 -> ~90 (the dropped window costs only
the game-vint glue) -> game ~66% of the MD 68K (~50% of true
arcade); band tearing CLOSED. The two remaining thirds are the
0.77x clock ceiling and the master's pre-ack work (blit — the
separate lane if still needed).

## FALSIFIERS / GATES

  - Parity statics EXACT (title 2.44 / eyehold 3.37, shipping).
  - Text cadence must NOT regress: title + scream parity are the
    text/palette-sensitive statics; watch the INSERT COIN block and
    the cycling logo (the LOOP7a/8 signatures).
  - Cadence (vints/cycle) target 3.0 with 2 windows — the meter
    semantics change (windows/cycle 2); re-baseline magic_smoke.
  - dreq misaligned stays ~1 (per-word push unchanged).
  - handler mean on Mike's state: the number that declares victory.
  - Mike's play: speed feel + NO new band tearing + text freshness.

## ORDER (each step gated before the next)

  1. Single-snapshot compose alone (3 windows still): proves the
     latch change with zero transport risk. Gate: statics + Mike
     (tearing should already improve).
  2. The 2-window transport re-plan (MD post schedule + SH2 phase
     logic + length publishing). Gate: full suite + ares state.
  3. Re-tune what the freed master time buys (deferral/reject
     margins) and re-visit CUT_BLANK verdict + load-in feel.

## 2026-08-16 — STEP 1 SHIPPED AND ARES-VERDICTED (v7, BUILD
## 2d79d5a1): tearing down to "a little", sprites GOOD; step 2 spec
## refined — the text-cadence trap is SOLVED by an 852-word packet

Mike on v7 (SNAP1), and the HONEST framing he corrected me to:
**"closer to MAME — less tearing and less sprite garbage"** — not
"near-flawless"; still slow, still some banding/tearing. The oracle
is MAME and we are CLOSER, not arrived. State clean: misaligned=1,
handler mean 134 (unchanged, as designed — step 1 moves work; step
2 removes it), skips 1.0-1.8%. Step 2 is "a huge lever to work
towards" (Mike) — it is the next session, pure execution.

RESIDUAL-TEAR suspects for later (small class): sprite CRAM painting
at k0/k2 ("sprites tolerate" — mid-frame palette recolours), and
whatever step 2's window restructure shifts. Re-judge after step 2.

**STEP 2 REFINED (the design decisions, made):**
  - 2-window naive = text at 1/cycle = THE DOCUMENTED REGRESSION.
    FIX: grow the sprite packet 596 -> 852 words (regs 80 + sprites
    512 + text chunk 256 + pads). SPR_LAND grows 0x394A8 -> 0x396A8
    — verified room below missq at 0x3A000. The other packet stays
    596 (regs + pal pair + text). Text = 2 chunks/cycle PRESERVED;
    palette dirty-pairs 1/cycle (was up to 2 — watch the cycling
    sets, regions 0/1, with pal_probe if scream regresses).
  - 68K/cycle: pushes 1448 words (vs 1532) + one fewer window's
    consume/stage/glue + the third vint runs NO handler beyond the
    game's own vint chain. Expected handler mean ~134 -> ~105
    (the blit's FM wait does NOT shrink — same total blit spread
    over 2 windows), game ~49% -> ~60% of the MD 68K.
  - THE REAL WORK is the master's cycle machine: 3 k-roles (R1 /
    R2+complete / flip+R0) over 2 windows — band queue scheduling,
    the k2 flip/restore points, pres-2.0 invariants, phase memory
    (k = prev+1 MOD 2), DREQ_LEN 852, landed/residue recalibration.
    SESSION-SCALE. Do it whole and gated, never half, in a fresh
    session starting from THIS file.

## STEP 2 IMPLEMENTATION SPEC (2026-08-16 — design COMPLETE, next
## session executes; all line refs = post-v7 tree)

KEY DISCOVERY: the MD is the cycle master — the SH2 READS k from the
command word (m_main:3009 `k = (c0>>4)&3`) and is reactive per-k.
Step 2 = teach the MD to post 2 windows per 3 vints + redistribute
what k0 carried.

New cycle (3 vints): [idle vint — no post, no push, game only] ->
[k1 window] -> [k2 window]. Rejects retry their slot (stretch the
cycle), idle vint never consumed by a retry.

1. MD (md_main): vint scheduler posts k1,k2 only (wskip cycles over
   {1,2} + one idle beat after k2). Push phases: after-k1 = SPRITES
   852 (regs 80 + spr 512 at 82 + TEXT chunk at 594 + magic 850);
   after-k2 = pal/text as today (596/340). Length publish per kk.
   Text rotation advances on EVERY push (both carry a chunk =
   2/cycle preserved). pal pairs 1/cycle — watch regions 0/1
   (pal_probe) for the cycling-set convergence class.
2. SH2 transport: DREQ_LEN(k) per-k: k==1 -> 852 else 596 (rearm
   uses current k = the following push's phase ✓). prev_k mapping
   `k ? k-1 : 2` WORKS UNCHANGED for k∈{1,2}. Magic gate lengths:
   got_spr -> 852, else 596/340. Text-apply gains the sprite-packet
   case: src=594, need=850 (tb still from word 81). SPR_LAND grows
   to 852 words (0x39000..0x396A8 — verified < missq 0x3A000).
3. Band launches (m_main:3882-3892, rg = k): k2 -> launch R0 +
   bq-ENQUEUE R1 (the queue is built for deferred bands;
   complete-or-defer semantics unchanged); k1 -> launch R2. Compose
   time per band matches today (R0/R1 get the idle+k1 gap = 2
   vints, R2 gets 1).
4. Blit redistribution (m_main:3335-3343 + slave_window_k:2144
   row table): W_k1 blits R0+R1's rows (M 36-110, S 110-184
   — ~74/CPU, the window grows ~30 lines), W_k2 blits R2's
   (M 184-204, S 204-224) pre-flip. Verify the slave table's
   current k-map before editing (comment at 2014: slave k1 =
   144-184 — read slave_window_k, don't trust the comment).
5. Unchanged on their windows: k1 housekeeping (cap_drain 3,
   apply_cram, par^=1, DIAG[9] cadence), k2 (cap_drain 13, flip,
   restore, SNAP_ONE snapshot). k0's only unique work was R1's
   launch + its blit third + cram_paint_spr (k2 still covers) +
   a transport window — all redistributed above.
6. Costs to eyeball after: MD_BG cell/tile cadence drops to
   40 windows/s (draw-in 0.5 -> 0.75s, storms 1.5x longer;
   CUT_BLANK covers), WINSPAN per-window similar, handler mean
   target ~105 (game ~60% of the MD 68K).
GATES: canaries first (dreq_inc, DRQR[7], cadence semantics
change — magic_smoke re-baseline), then parity anatomy, then
Mike (speed feel + text freshness + the cycling logo + tearing).

## 2026-08-16 (night) — STEP 2 LANDED: THE 2-WINDOW CYCLE RUNS
## (v8, BUILD 3f122b1f). MAME handler mean 113.4 -> 95.6.

`make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1` =
rom/s16_mdbgall_v8.32x. All three bands launch at k2 from one
snapshot; R1/R2's slave cmds chain from the poll loop; blits regroup
k1=R0+R1 / k2=R2; sprite packet 852 with the text chunk at 594; MD
posts k1,k2,idle. Gates: smoke cycles 1194/dreq 0/[7]=1, parity
title in-family, demo scene renders FULLY at steady state
(f2300 snap), shipping statics untouched. **HMEAN A/B on MAME:
v7 113.4 -> v8 95.6 lines/vint (n windows 3569 -> 2385).**

BUGS PAID (recorded for the era):
  - Chain echo state MUST be independent of tile_cmd: the shared
    per-window drain zeroes tile_cmd, the chain wedged on a stale
    echo, cycle dead at 2/65s. pend_wait carries the chain's own
    reference. DIAGNOSED VIA LUA PC-SAMPLING (state["PC"].value +
    disassembly at the pinned PC) — bank that instrument.
  - Region guard paid twice: wrap trackers + md_lastb + snap[2]
    all moved to the 0x39800 gap (SPR_LAND end 0x396A8 .. missq
    0x3A000). Under WIN_TWO, _snap has NO lst symbol — bs9_audit
    --snap address is FIXED 0x06039940 (base 0x39800, hs 0x39870,
    lastb 0x398E0, n/c 0x39930/32, snap 0x39940..0x399E8).

KNOWN COSTS (by design, watch on ares): MD_BG transport 40
windows/s (draw-in 1.5x longer — eyehold anchor now catches deep
mid-load, 56.66 = load anatomy, heals clean); R2's compose gets the
chain tail (watch bottom-band staleness in heavy scenes); deferral/
DIAG[13] with 3 bands queued at once.

MIKE (v8): (a) SPEED — the headline; state's handler mean is the
scoreboard (expect ~110-115 from 134.6, game ~55-58%), (b) tearing
(single-snapshot now cycle-wide — should be at least as good as v7),
(c) text/HUD freshness (852-packet path), (d) load-in feel (slower
by design — CUT_BLANK covers).

## 2026-08-17 — v8 ARES VERDICT + v9 REBALANCE (BUILD 85802cde)

v8 on ares: **handler mean 134.6 -> 107.3, game 49% -> 59% of the MD
68K (~45% of arcade)** — step 2's prize landed. Costs Mike saw:
splash/start loads slow, ZEUS CUTSCENE partial/stuttering (deferrals
3506 — the 3-at-once bq burst vs depth 4 = complete-or-defer at
half cadence), one tearing pair, rejects 4.7% (k1 carried the double
blit AND the 852 push).

**v9 (rom/s16_mdbgall_v9.32x) = three rebalances, all within the
design:**
  1. EVEN blit split: k1 ships rows 0-112, k2 112-224 (56/CPU per
     window; blit ranges need no band alignment, deadlines
     unchanged) — was 144/80.
  2. Push weight moved OFF k1: sprite packet back to standard 596;
     the k2-tail packet carries pal + BOTH text chunks (852/596,
     txt rotation advances 512 there, second chunk wraps at 2048;
     SH2 applies tb and tb+256). DREQ_LEN k2->852.
  3. ENQUEUE-WITH-POST: each chained band's bq entry is created when
     its slave cmd posts (BQ_PUSH) — the 3-at-once burst is gone.
     Chain leftovers at k2 are DROPPED, not flushed
     (complete-or-defer: the region shows last frame's coherent
     rows for one cycle).
  DELETED: the reject-healing belt (MDVERIFY proved the FB channel
  lossless — 0 stale re-reads in 7079 packets; it defended nothing
  and WIN_TWO needed the region-guard bytes).

Gates v9: smoke 1196/dreq 0/[7]=1, **MAME HMEAN 95.6 -> 90.4**,
title parity in-family, steady-state demo renders fully clean,
shipping statics EXACT, _end 0x18e60.

MIKE (v9): (a) the Zeus cutscene — the stutter fix is the headline
of this round; (b) speed feel + state (expect handler mean ~100-105
from 107.3, rejects well under 4.7%); (c) tearing pair class; (d)
text/HUD freshness (both chunks now land at k1 in one packet).

## 2026-08-17 — v9 ARES VERDICT: REJECTS 4.7% -> 0.1%, ZEUS
## CONSISTENT. Step 2's era is CLOSED at its operating point.

Mike: "zeus stutters in sprite scale in and out. but is consistent
during the animation." State: cadence 3.00, rejects 6 (0.1%),
misaligned 1, skips 0.3%, handler mean 110 (game ~58% of the MD
68K, ~45% of arcade). The scale stutter = 20Hz temporal aliasing
(we present every 3rd game frame — inherent to the cadence) plus
residual deferral moments (2076/2059 cycles: a heavy cutscene band
overruns its slot -> complete-or-defer half-cadence; possible future
fix = a 2-deep slave mailbox to deparallelize the launch chain —
PARKED, diminishing returns). The architecture is at its designed
operating point: ~58% game CPU, 20Hz presentation, near-MAME
visuals, jitter dead, cuts covered by CUT_BLANK.

CANONICAL BUILD (silent-release candidate):
`make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1`
= rom/s16_mdbgall_v9.32x, BUILD 85802cde.

## 2026-08-17 — THE RELEASE BAR, DEFINED (Mike): "We don't ship
## until this is a flawless game" — with FLAWLESS meaning:

  - NEAR arcade port; shifted is OK.
  - Sprites in OUR 32X palette — near match, judged by eye in our
    palette, NOT by RGB diff vs arcade (softens §11 further).
  - **Scaling/animation must MATCH the arcade's MOTION** — via OUR
    architecture, not the arcade's scaler method ("that's why we
    have two SH-2 processors"). This REOPENS the Zeus scale
    stepping as a MUST-FIX (was parked as inherent 20Hz aliasing).

MOTION-PARITY LANE (open): smooth scale needs more presented frames
where compose load peaks (cutscenes) — only possible if the second
SH-2 has unused capacity. MEASURE FIRST: v10 (BUILD 78f9c4b2,
rom/s16_mdbgall_v10.32x) adds the SLAVE IDLE METER (s_main idle
polls, 64:1 throttled to keep the bus quiet, scrap 0x28FA8;
state_health prints "slave idle polls"). Next decisions hang on
Mike's state: high slave idle -> shift compose rows slave-ward
(faster band completion -> fewer cutscene deferrals) and evaluate a
cutscene-only 2-vint cycle (30Hz presentation where the game's CPU
demand is low — the MD shim can read the scene state at 0xFFF031 to
switch cadence). Low slave idle -> the compose is genuinely
saturated and the lane needs a different idea.

## 2026-08-17 — THE SPRITE-FRAME CACHE THESIS, MEASURED: 73% of
## decode jobs / 74-85% of decode COST is repeated work

Mike's architecture push ("larger sprites to fit our buffers and
gates; memory-segmented pipes for scaling vs animation") is the
texture-cache pattern we already proved on tiles — and the reuse
probe (`make SPRREUSE=1`, k2-snapshot walk, ROWHASH overlay,
BUILD d90198f9) sizes it: attract 65s = sprites 5426, repeats 73%,
cost units 2.9M, repeated cost 74% (85% at 25s). The pre-decoded
frame cache's hit ceiling is ~3/4 of the sprite compose bill — the
largest SH-2 cost in the system.

THE MECHANISM (LOOP17 material — design sketch):
  - Cache pre-decoded, pre-scaled, palette-resolved sprite frames
    keyed (data addr, zoom, set, flip) with an md_tag-style LRU;
    compose becomes a copy for hits. Misses (the SCALING stream —
    monotonic zooms) keep the live decoder with the freed budget:
    Mike's two pipes.
  - MEMORY: right-size CACHE_C (64KB tile cache, half-idle under
    MDBGALL — only cat-1 uses it now) -> ~32KB carve. MEASURE its
    real pressure first (hit counters) before cutting.
  - The freed compose budget is what funds the motion-parity lane
    (cutscene 30Hz / fewer deferrals). Slave-idle number (v10)
    still pending from Mike — the other fork input.
  - Risk ledger: a second allocator = more slot-collision surface
    (the era's tax); eviction bugs would present as the buried
    foreign-art class; output must stay pixel-identical (it is by
    construction — same decoder, run once).

## TRAPS CARRIED FORWARD

  - The DREQ protocol is the most trap-laden subsystem in the repo:
    magic-tail, displacement, partial-apply, phase memory. Change
    the push and the apply in the SAME commit, whole and gated.
  - Text at 1/3 refresh rate = documented regression. 2-window
    keeps 2 chunks/cycle by riding both windows.
  - Fixed-address map FULL; new arrays -> .bss; lst is the address
    oracle. Region guard headroom ~0x100 — diet before adding.
  - MDBGALL parity anchors are a phase lottery; judge by anatomy.
  - Probe roms freeze at their commit; check lineage before handoff.
