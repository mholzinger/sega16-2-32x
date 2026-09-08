# LOOP 24 — THE TEAR: the flip must never latch mid-scan

Mike, closing LOOP23: X is the most playable build yet; "the screen
tearing is where we need work." Read LOOP23 for FMGATE; LOOP11's
CMDINT postmortem is REQUIRED READING before touching pickup.

## THE MECHANISM (from the X state, not a guess)

flip-late-latches = 7.2% of cycles (~1/sec at 30Hz): the k2 flip is
issued after a POLLED pickup (the master discovers the command between
compose strips), so heavy strips push the FBCTL write 1-3ms late and
the LATCH slides past vblank — a bank switch mid-scan is a tear, by
construction. The 6.0% rejects are the same vints overrunning.

## WHAT DIED BEFORE (do not re-buy)

LOOP11 CMDINT: interrupt-driven pickup with strip YIELDING — retired.
"Interrupt pickup does not remove latency, it moves it to
band-completion, and the pipeline cannot absorb it there. ANY
preemption costs more than it saves" — where the cost was skips and
the yield machinery. The prize here is different (tear removal, not
lines) and the mechanism below has NO yield/resume: a bounded,
in-order ISR that does the flip work itself.

## THE DESIGN: the SH-2 owns the flip clock

The SH-2 receives its OWN V-interrupt every vblank — no 68K post, no
pickup, no polling. On k2-cadence vints its V-ISR:
  1. raises FM itself (shared bit; the MD's gated stores just block),
  2. runs the flip-critical span: truth drain -> FBCTL flip -> restore
     (+ the text capture that must straddle it), ~1.9ms, guaranteed
     inside vblank,
  3. returns; the interrupted compose strip resumes IN ORDER.
The k2 WINDOW BODY (blit, captures, launches) stays on the polled
pickup — its timing never mattered. The 68K's k2 spin then shrinks to
~the flip span or dies outright, which is ALSO the last piece of the
68K's 60Hz arithmetic.

**Risk register (LOOP11's ghost):** the ISR steals ~1.9ms/cycle from
mid-compose. No yields, but band completion shifts by that much.
MEASURE FIRST: (a) SPANPROBE miss-bins for where pickups land today;
(b) an ISR-cost probe (flip span in the V-ISR, body unchanged, 68K
spin kept) — bands/skips must hold before the spin is touched.

**The sprite-transport fork rides along:** k1-vint uploads already
drop (FM=1); k2-vint uploads are what the captures live on. If the
flip-span split extends FM across the k2-vint upload too, the sprite
list must leave the FB — resurrect the push for it (affordable
post-PKTSLIM: ~100-200 words ≈ 7-13 lines; packet_fmt is the single
source for the new family). Decide from the probe, not from taste.

## GATES

MAME structure A/B + skips; wpcatch conformance unchanged; ares state:
flip-late-latches -> ~0, rejects -> baseline 2-3%, cadence -> 2.05;
Mike's eyes on the tear specifically.

## VISRFLIP — THE ISR-COST PROBE (built 2026-08-20, MAME-gated)

`make ... VISRFLIP=1`. The master enables its OWN V-interrupt
(mars_start.s: enable 0x08, trampoline saves the caller-set and calls
visr_vbi). The ISR at vblank entry: bail fast if COMM0 is live (a
still-open window — the body owns the FB, and the 68K defers this
vint's post anyway); else wait <=700 FRT ticks (~15 lines) for the
68K's post. A k2 post (0x2020) means FM=1 and V in the 68K's own
[DF,E8] entry gate, so the ISR runs flip_span() — the whole
capture -> truth drain -> FBCTL flip -> restore block, extracted
verbatim from the window body (m_main.c) — and flags visr_flip_done.
The body consumes the flag at pickup instead of flipping; every bail
leaves the cycle byte-for-byte on the LOOP23 polled path. 68K spin
KEPT, window body UNCHANGED, per the risk register above. Mutual
exclusion is structural: between ISR-set and body-consume COMM0 stays
live, which makes the next ISR bail; body pg_*/cycle_dirt reloads are
forced by a compiler barrier at the flag check.

Counters DIAG[49]=fires, [58]=ISR flips, [56]=body fallbacks,
[59]=stale bails, [60]=no-post bails, [61]=k1 posts, [62]/[63]
max/sum ISR ticks (wait+span). [57] looked free and is NOT — the
MD_BG tile batcher counts tiles-sent there on every shipping build;
first cut collided. Readers: tools/visr_probe.lua (MAME),
state_health.py VISRFLIP line (ares).

MAME gate (visr_probe.lua, identical scripted play, f3200):
  base: cycles 1521 skips 0 hmean 53.5 window 35.8
  visr: cycles 1527 skips 0 hmean 53.0 window 35.7
  fires 3192 =vints; ISR flips 1154 vs body fallbacks 429 (73%
  in-ISR even with MAME's 3x-fast SH-2 shrinking the post-wait
  bound ~3x in 68K terms — expect a higher ISR share on ares);
  stale 0, late-latches 0. Parity statics IDENTICAL between arms
  (title 91.72/91.72, eyehold 69.98/69.98, no-SPRTRUNC builds per
  the per-flag rule). wpcatch: unchanged BY CONSTRUCTION — no 68K
  edit, no FM edge moved (the ISR waits for the 68K's own raise;
  only the flip's position inside the FM span moves earlier).

MAME cannot price the span (no FB bus-stall model) or the post-wait
(clock skew) — the ares state prices both: the VISRFLIP line's span
mean/max against vblank=38, and the flip-late/reject/cadence trio
against X's 7.2%/6.0%/2.29.

Ares rom: `rom/test/Y_visrflip_flick.32x` (BUILD 484e2020+,
stamped VISRFLIP). Watch for: the tear (THE item), sprite freshness
(the transport fork question — if k2-vint uploads now land during
FM=1 the sprite list must move to the push; decide from the state's
packet/freshness counters, not from taste), and any new chop from
the ~2-line ISR steal out of mid-compose strips.

## Y VERDICT (ares state 1, 2026-08-21) — MECHANISM CONFIRMED, COST
## MOVED TO THE 68K, AND THE FORK IS FORCED

Scene note (Mike): state 1 is the BUSIEST stretch of the level —
multiple enemy types plus the gravestones rising out of the ground
(a page-dirty storm: tilemap animation every cycle). Read every
number as worst-scene; A/B against X only same-scene.

The probe's question, answered:
- ISR owns the flip OUTRIGHT: fires 7406, isr-flips 3525,
  body-fallback 0 (100% in-ISR), stale 2, no-post 473 ~= the 439
  rejects (no post on a rejected vint — consistent, not a fault).
  k1-seen 3405 ~= cycles: the 68K's post beats the 700-tick bound
  essentially always on ares.
- THE TEAR MECHANISM IS DEAD: no flip is issued from polled pickup
  anymore. flip-late-latches 7.2% (X) -> 3.2% (114/3525). NOT the
  ~0 the gate asked for: the residual is the PRE-FLIP HALF of the
  span (<=15-line wait + capture + FULL truth drain, which
  correctness pins pre-flip) pushing the FBCTL write past vblank
  end on heavy-drain cycles — span mean 52.1 lines, max 370 (scene
  bursts: deep watch + 13-page drains). The tail is now OURS to
  trim, not the pickup lottery's.
- Health held: cadence 2.17 (X 2.19-2.27), rejects 5.9% (X 6.0%),
  skips 0, strobe 0, preempt timeouts 0, NT wipe 0 mismatches,
  pen drift catastrophic 2. No corruption class opened.
- THE BILL LANDED ON THE 68K, exactly where LOOP11's ghost said:
  window/ack 46-64 (X) -> 68.2 mean, worst 255 = SEVEN lines of
  frame margin. Mechanism: the k2 ack waits on the interrupted
  strip PLUS the ~52-line ISR steal, and the 68K spin pays both.
  Handler mean 84.7 (top of X's range; busiest scene).

DECISION — the next build takes the design's step 2 whole:
1. KILL THE 68K k2 SPIN (post-and-return, like k1's v2 move). The
   flip no longer needs the spin's timing; the steal converts from
   68K stall into SH-2 concurrency. This is the remaining piece of
   the 68K's 60Hz arithmetic (own-tail is only 16.5 lines).
2. TAKE THE SPRITE-TRANSPORT FORK — forced, not tasted: with the
   spin dead the game's k2-vint upload runs while FM=1 (the SH-2
   still owns the window ~52+ lines in), so the k2-vint FB upload
   dies the k1 death. Sprite list leaves the FB for the push
   (packet_fmt single-header change; ~100-200 words post-PKTSLIM).
3. TRIM THE PRE-FLIP HALF: TEXTCAPSLAVE=1 rides along (slave
   captures text in parallel with the drain — ~10 lines off the
   half that decides the latch), chasing the 114 residual late
   latches toward the gate's ~0.
4. Re-gate: MAME structure/statics as before, then ares: window/ack
   should collapse toward own-tail, late latches ~0-1%, cadence
   toward 2.05, Mike's eyes on tear + sprite freshness (the push
   must not reopen the SPRTRUNC freshness class).

(Corpus note: screenshots/ refreshed from the Y playthrough,
dedup interrupted mid-run — treat the tail of the corpus as
non-deduplicated when scanning by size.)

## K2FREE — THE SPIN-KILL DESIGN (census-derived, 2026-08-21)

MEASURED FIRST (tools/wpcatch_hv.lua — wpcatch + a V column; vint
writes are bimodal E6-EA / 2C-40 under today's k2 spin):
- sprite upload (PCs 0x902B3A-4C): vint-context, every vint. Dies
  under FM-covered vints; must return to WRAM + push.
- layer regs (words 740-74D; PCs 0x902AD8-0x902B18): vint-context,
  every vint. Same verdict. Rowscroll (7C0+): ZERO hits in gameplay.
- text glyphs: EVERY writer PC observed falls inside the FMGATE span
  table or its thunk region — main-loop, entry-gated, SAFE. The
  glyph harvest (FBTEXT) survives.
- tile staging (0x852000+): zero vint-context writes.

THE SHAPE: mostly a REVERT to proven transport. Build drops FBSPR
and PKTSLIM (sprites -> 0xFF7000 WRAM + the SPR_TRUNC 84+8n push;
prefix-82 packet family returns, live again via a patch_game split:
text offsets >= 0xE80 — regs+rowscroll — go to the 0xFF8000 mirror,
glyphs stay in FB). Keeps FBTEXT, PAL32, SPRTRUNC, VISRFLIP.

Protocol under K2FREE (both CPUs):
- 68K k2: heartbeat -> defer-if-COMM0-live -> MID-SPAN DEFER CHECK
  (the k1 check, now mandatory: the game RUNS during the k2 window)
  -> raise -> post -> stage_play -> push -> return. NO spin, NO FM
  clear (the SH-2 owns FM's fall). k2 handler ~5 lines vs 68.2.
- 68K k1: belt -> md_consume(A) + budgeted md_consume(B) at ENTRY
  (consume is STAGED — needs FM=0, not vblank; md_stage_play right
  after the post flushes it in-vblank same vint) -> gates -> raise
  -> post -> push (records+prefix). Post delayed ~11 lines mean by
  the consume; v3's grave was a ~200-line displacement, this is
  budget-capped and MAME-gated (bands/skips must hold).
- Deferred/overrun consume drops a whole MD-plane packet (publish
  overwrites it): tolerated — the nt builder's rotating force-full
  row heals losses by design; counted.
- V-ISR: bound widens to 1400 ticks; on ANY post seen it arms the
  DREQ channel (k1 -> SPR_LAND 596; k2 -> a NEW 216-word landing at
  0x39A00, the verified-free gap over md_dbg — the k2 packet must
  not clobber k1's records before the k2 snapshot reads them); k2
  posts seen <=700 ticks additionally run the flip span. The body's
  dreq_rearm compiles out — the ISR owns arming; a missed post
  (nopost ~1%) means that vint's push aborts on the bounded spin
  and retries, the existing degrade.
- Same-vint pairing: prev_k == k (the packet landing at vint N is
  the one pushed AT vint N). Harvest sits mid-body AFTER the blit
  (~40+ lines) vs push completion ~25 lines — ordered in the mean;
  the magic-tail-at-landed-2 check catches the rare overlap (skip,
  stale beats torn, counted).
- k1 harvest validates records and latches spr_ok/spr_landed; the
  k2 snapshot copies SPR_LAND->SPR_SNAP from that latch (records
  stable across the k2 vint by the split landing buffers).
  FLICK_FUSE re-sources zseen from the landed copy (the FBSPR
  #error relaxes under K2_FREE).
- Prefix apply (PKT[0..79] -> TEXT_U 740/7C0) re-enables under
  K2_FREE (it was #ifndef FB_TEXT_READ); the ISR text CAPTURE trims
  to 464 longs (glyphs only — FB reg words are dead now and would
  clobber packet-fed regs); the restore stays full-width (harmless).
- Known accepted exposure: game main-loop FB READS during the now-
  longer FM=1 spans — same class FMGATE k1 already tolerates (the
  entry gates sit at pointer-formation, covering read spans too).

MAME pixel gate: K2FREE+SPRFULL=1 pushes all 64 records (full 596,
TE sets, MAME reads it) — the SPRTRUNC/harvest precedent; pal stays
MAME-colour-blind as today. Speed/cost gates on headless ares.

## K2FREE BUILT AND GATED (2026-08-21) — FIVE PROBE-CAUGHT FIXES DEEP

The first cut regressed everything and each iteration's counters named
the next bug. The graves, so nobody re-digs them:
1. GATE READS ENTRY V. The k1-entry consumes ran before the
   early-vblank gate; live V then read past E8 and the gate rejected
   its OWN window (rejects 40.6%, cadence 3.48, reject-retry lock).
   Same for the heartbeat: a live-V heartbeat past EA made the SH-2
   skip gate drop the k1 BLITS. Both read v_entry now.
2. THE OLD SPIN WAS A BUS-QUIET GUARANTEE NOBODY WROTE DOWN. Freeing
   the 68K at post+3 put the game's cart fetches under the master's
   cart-resident flip span: span 82.9 -> 124 lines, flip-late 26%,
   overrun-stale 276. The FLIP-HOLD fixes it: the k2 68K polls COMM4
   from WRAM (zero bus traffic) until the FBCTL-write echo (0xF102),
   ~15-25 lines vs the 68.2 it replaces — "the spin shrinks to ~the
   flip span", the design's own middle option.
3. k1 PRE-ANNOUNCE (COMM6, boot-heartbeat reg, runtime-free). The k1
   post sits ~12-55 lines behind the consumes; no sane ISR wait
   catches it and a missed arm kills that cycle's packet (armed 56%,
   c10 recaptures fed the k2 drain). The announce fires at 68K entry
   pre-consume; the ISR arms on it without needing the post.
4. ANNOUNCE LIFECYCLE: an announce that outlives its vint makes the
   next (k2) ISR arm k1 and skip the flip (342 fallbacks + a
   records-clobber path). ISR clears COMM6 on ANY window it
   processes; the 68K clears it at window_done (reject/defer).
5. k2-ONLY COMM4 CLEAR: the unconditional pre-post clear erased the
   ISR's fresh k1 arm echo — push gate aborted EVERY k1 push (1718
   aborts, sprites frozen, cadence deceptively pretty). The clear
   exists only so a stale 0xF102 can't satisfy the flip-hold.

Push diet (the cost problem LOOP11's ghost predicted): per-GROUP FIFO
polling for K2FREE pushes (NT_WRAP's per-word belt cost ~half the push
span; the tail-drop class it guarded is WATCHED via DRQR[7], not
assumed) + the k2 packet drops its 80-word prefix (K2F mixed families
in packet_fmt.h: k1 = 84+8n with live regs/rowscroll, k2 = the proven
slim family 4/40..136; regs at 30Hz = today's rate).

FINAL NUMBERS (attract 3600, ares-headless, vs Y baseline):
  handler mean  80.2 -> 49.1   window/ack 66.3 -> 33.3
  cadence       2.313 -> 2.195 rejects    9.4% -> 6.9%
  skips 0, flip-late 14.4% (par with Y's 13.5% — the deep-watch
  pre-flip drain tail is the remaining tear item, unchanged in
  nature; flip-pos mean 50 lines is tail-skew, median in-vblank),
  fallback 268 (late-entry k2s, body handles), stale 127 (body
  overruns under game concurrency — WATCH on gameplay), push-aborts
  231 (6%, reject-correlated = correct), consume-B deferrals 499
  (one-cycle-late MD packets, healed by the force-full row).
  68K game share ~69% -> ~81% of the MD 68K (~62% of the arcade).
  tools/ares_gate.py gate vs Y_attract_3600: PASSED 6/6 — the
  headless gate's first real build decision.
MAME (visr_probe, scripted play): skips 0, cycles 1556 (Y 1527),
hmean 31.8 (Y 53.0), 99.7% flips in-ISR, stale 0, latelatch 0.

Open items for the next iteration: the deep-watch drain tail (the
flip-late tail — pre-drain at k1/idle, or drain chunking with a
vblank bound); stale 127 overruns; consume-B deferral rate under
gameplay load; whether per-group FPUSH re-opens the tail-drop class
on real hardware (DRQR[7] watches).

SIXTH GRAVE (MAME-only wedge, two days of parity MISSING-scenes):
FPUSH wrote its word UNCONDITIONALLY after spin exhaustion — dropped
on ares (harmless forever), defer_access'd FOREVER in MAME. Never hit
pre-K2FREE because the old protocol's DMA always kept pace; now the
DMAC cycle-steals against the master's ISR span, the FIFO backs up,
and exhaustion is a live path. The write is spin-guarded now (both
FPUSH forms), and the normal arms oversize +8 words so an abort's
FIFO residue can never overshoot the next arm (SPRFULL keeps exact
arms + the per-word belt: TE must set for MAME to read the landing).

FINAL GATES (BUILD f8473eda):
- MAME structure: skips 0 both arms; normal arm hmean 31.8 (Y 53.0),
  99.7% flips in-ISR; SPRFULL arm runs clean end-to-end.
- Parity statics (SPRFULL arm vs the no-SPRTRUNC Y-era captures):
  title 91.88 vs 91.72 HOLDS; eyehold 86.00 vs 69.98 — a static
  IMPROVED 16 points (regs/rowscroll at live k1 rate is the likely
  mechanism); dynamics moved with the cadence change as the rule
  allows (scream -1, demo/demo2 -11).
- ares_gate vs Y baseline: 5/6 — flip-late 15.9% sits ~1.4pp over
  the +1pp band (build-to-build attract scatter on the deep-watch
  tail; 14.4% on the sibling build). Reported, not tuned away: the
  tail is the named open item and Mike's eyes on the TEAR (not the
  latch counter) are the real verdict.

Ares rom: `rom/test/Z_k2free_flick.32x` (BUILD f8473eda+, stamped
K2FREE). Watch: the tear (the ISR owns every timely flip now —
late-ENTRY vints fall back to the body, ~8%), sprite freshness (the
push transport + one-frame staleness), HUD/text (glyph harvest
unchanged, regs now packet-fed), MD-plane staleness (consume-B
deferrals ~14% attract), and overall speed feel (68K game share
~69% -> ~81%).

## Z GAMEPLAY STATE (2026-08-21, Mike's play, same busy-stretch class
## as Y's state 1) — THE SPEED PRIZE LANDED ON GAMEPLAY

vs Y state 1 (the X-era numbers in brackets where different):
- **cadence 2.07** (Y 2.17) — closest any build has come to the 2.05
  target. rejects 2.0% (5.9%). skips 0, strobe 0, NT wipe 0,
  preempt 0 — no corruption class open.
- **68K handler 52.0 (84.7); window/ack 30.2 (68.2); game share 80%
  (68%) of the MD 68K, ~62% of the arcade.** Own-tail 21.8 (16.5):
  the +5.3 is the push, exactly the priced trade.
- Tear machinery: flip-pos MEAN 35.4 lines — inside vblank — max 239
  (the deep-watch tail). flip-late 4.9% (Y 3.3%, +1.6pp). ISR owns
  83.7% of flips; **body-fallback 653 (16%)** — late game-IRQ
  entries + overruns; those flips are the residual tear exposure and
  the FIRST open item now.
- Transport health, the watched risks priced: dreq_incomplete 15% of
  cycles (real partial landings now — sprites keep last frame those
  cycles); **dreq misaligned 25 (0.3%) — the FIFO word-loss class
  the per-word belt guarded DID fire**, at 0.3% with clean healing
  (spin headroom 2562/2600 says the polls never run dry — these are
  ares full-FIFO drop races, not exhaustion); push-aborts 3.8%;
  consume-B deferrals 23% of cycles (MD-plane packets a cycle late
  under load — up from 14% attract).
- worst handler 254 with tail=235 (8 lines margin): one worst-case
  consume+push stack nearly ate a frame — bound the tail next pass.

Open-item order for the next iteration, from this state:
1. The 16% body-fallback flips (late-entry class) — the tear's
   remaining home. 2. The deep-watch drain tail (flip-pos max 239).
3. consume-B 23% / dreq_incomplete 15% under load (sprite+MD-plane
   freshness). 4. The 235-line worst tail. Mike's eyes on the tear
   and speed feel decide whether this ships as the new canonical.

## MIKE'S Z SCREENS (2026-08-21) -> TWO ROOT CAUSES, BOTH FIXED (Z2)

Mike: speed VASTLY improved ("we are on the way"); four screenshots:
load-screen tile mess, garbled title splash, Zeus dither question,
two tearing frames.

ROOT 1 — THE MD-PLANE A CHANNEL WAS STRUCTURALLY DEAD (the load mess
and the garbled splash, plus the persistent garbage squares in the
gameplay shots). MDVERIFY probe: 2772 seq jumps in 2778 consumes,
stale-rereads 0 — the k1-published A packet crossed the k2 FLIP
before its k1-entry consume and was read one generation stale from
the OTHER BANK every cycle; the fresh A was then overwritten =
lost. The tile batch's dirty-clears and allocator claims happen at
BUILD time, so every lost packet left md_tag claiming tiles VRAM
never received — persistent wrong art, not a healing delay. My
"deferrals tolerated, force-full row heals" assumption was WRONG for
tile batches; v8 was lossless BY CONSTRUCTION and K2FREE had broken
that contract. FIX: per-k SDRAM staging (md_pktA at 0x39A00 — the
K2 DREQ landing moved to 0x394C0 to free it), BOTH packets publish
at the k2 window POST-FLIP (bank-stable: no flip between publish
and the k1-entry consume), magic handshake (consume zeroes word 0;
publish defers on a still-set magic; builder holds off while its
staging is unpublished — pend flags). Nothing lost, only late.
Post-fix MDVERIFY: stale 0; residual 1244 seq jumps ARE the design
(A rides one cycle deeper than B: +2 jumps on every A->B pair).

ROOT 2 — THE TEAR WAS IMMEDIATE OUT-OF-VBLANK LATCHES, NOT DEFERRED
ONES. Z state arithmetic: 676 body-fallback flips but only 182 late
latches — ~490 out-of-vblank FBCTL writes latched INSTANTLY =
mid-scan bank swap = the tear (LOOP7c's "ares defers" is not the
whole story at the edge). FIX: EDGE GUARD in flip_span — past 1748
ticks (38 lines) from the vblank ISR stamp, DECLINE the flip
(capture+drain keep, cycle_dirt carries, 0xF1FF echo releases the
held 68K, DIAG[44]): a dropped frame beats a tear, everywhere,
including the body-fallback path.

Z2 ATTRACT NUMBERS (BUILD 3edc89a6+): **flip-late 196 -> 1** (every
issued flip latches instantly; flip-pos max 37.0 lines — no write
ever leaves vblank), declined [44]=233 (~14% of k2s become clean
drops), cadence 2.116 (BEST yet), rejects 3.3%, handler 54.7,
skips 0, ISR span mean 39 max 135 (was 76/370), fallback 372
(halved — lighter drains shorten windows). Publish/build deferrals
~34% of cycles = the lossless machinery working. MAME: clean,
skips 0, tickmax 2003.

Counters added: [42] publish deferrals, [43] build deferrals,
[44] flips declined. Ares rom: rom/test/Z2_k2free_flick.32x.
Watch: load screen + title resolve CLEAN now (the A channel is the
falsifier); tearing should be GONE (declines show as occasional
frame hitches instead); Zeus dither = design question, Mike's call.


## Z3 (2026-08-21 evening) — MIKE'S BLACK BOOT DECODED, AND THE
## LOSSLESS FIX VERIFIED AT THE PIXEL LEVEL

Mike cold-booted Z2: splash BLACK (text layer only), fine after
reset. Chased it through MAME snapshots (misleading — MAME's 3x SH-2
skews boot phase), then went DIRECT with ares-headless memory dumps
at frame 300, both builds, rendered to PNG by hand:
- MD VRAM/CRAM/VSRAM and the 32X layer are near-IDENTICAL between
  the Y build and Z3 at the same ares frame (32X: exactly 2911
  nonzero pixels both).
- The rendered NT plane: Y holds the title art GARBLED (the exact
  scramble in Mike's screenshot #10 — the A-channel disease was in
  Y all along, on screen from the first fill); Z3 holds it CLEAN
  AND READABLE. The lossless publish fix, seen working in pixels.
- The black splash is therefore FILL LATENCY, not loss: the
  lossless pipeline defers ~30-40%% of packets under the boot storm
  (pub/build-defer counters), so the plane shows blank-until-
  resolved (~3-5s) where the lossy Z showed instant garbage. A
  reset skips the fill (VRAM persists). Boot-window census also
  re-confirmed all glyph writers main-loop even at splash time.
- Consume-B V-budget widened (0xF6 -> wrap-0x40): it was protecting
  the ISR arm timing the COMM6 pre-announce already covers, and it
  was the top deferral feeder (55%% of boot cycles).

Follow-up candidate (cosmetic): a boot fast-fill — e.g. suspend the
publish-defer rule until the first full NT rotation completes, so
cold boot fills at Z speed but without the poison. Low stakes.

Ares rom: rom/test/Z3_k2free_flick.32x (BUILD a5e290b0+). Gate vs
Y baseline: PASSED. Same watch list as Z2 plus: cold-boot splash
should be black ~3-5s then CLEAN (no garble); title art clean; if
the boot black bothers, the fast-fill follow-up is next.


## Z3 PLAY REPORTS DECODED (2026-08-21 night) — THE FLIP TEAR IS DEAD;
## WHAT REMAINS HAS THREE NAMES

Mike's Z3 state: flip-late [31]=0, flip-pos max 36 lines, late-k2 0 —
NO FBCTL write left vblank in the whole run. The bank-swap tear class
is instrumentally extinct. His three reports (raw corpus frames):

1. "Tearing" on smoke/character/gravestones = the WIN_TWO SPLIT-BLIT
   SEAM family: the frame ships as k1 rows 0-112 / k2 rows 112-224,
   so fast motion shows one-frame temporal seams at the fixed band
   boundaries — the documented cost of the 2-window ship, now the
   DOMINANT visible artifact precisely because the flip tear is gone.
   The real fix is the 60Hz arc (halves the seam age), not a patch.
2. "Blitter" (the black box punched through the smoke, one frame) =
   sprite-transport transients under record bursts: dreq_incomplete
   13.3%%, misaligned 22 (the watched FIFO word-loss class fires
   under DMAC contention during heavy scenes). Stale-beats-torn
   keeps it transient. Lever: push timing away from the ISR span,
   or per-record apply.
3. The PURPLE BAND (rows ~197-204, persists through the transform
   sequence) = PAL-BLOCK DEFERRAL LATENCY: the transform's palette
   storm meets the lossless pipeline's k2-packet deferrals (~34%%),
   a band composes with a stale cram_mirror generation and shows as
   a wrong-palette stripe until recomposed. Lever: split deferral
   granularity — pal blocks should bypass the defer (tiny, idempotent)
   even when the NT payload defers.

ZEUS / FLICKFUSE — MIKE'S RULING: "dithering is a deviation from the
source. we stay true to the source on that path to 60hz." FLICKFUSE
comes OUT of the play line; Zeus renders as raw 30Hz-sampled flicker
until the 60Hz arc restores the arcade's true temporal translucency.
(LOOP21's fuse stays in the tree behind its flag for reference.)

CANONICAL LINE (post-LOOP24): make MDBGALL=1 BQCHUNK=1 NTWRAP=1
WIN2=1 SPRTRUNC=1 BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16
FBTEXT=1 CUT30=1 PAL32=1 FMGATE=1 K2FREE=1  (NO FBSPR, NO PKTSLIM,
NO FLICKFUSE). Scoreboard vs LOOP23's X: 68K game share 68%% -> 82%%,
cadence 2.29 -> 2.18 gameplay / 2.12 attract, rejects 6%% -> 3.4%%,
flip tear extinct, MD-plane transport lossless.

LOOP25 AGENDA (the 60Hz arc, which is now also the seam fix):
1. Pal-block defer bypass (the purple band — small, do first).
2. Boot fast-fill (cosmetic black splash).
3. The 60Hz arithmetic: 68K handler 45.9 mean with own-tail 19.6 —
   the window/ack 26.2 shrinks further only via ISR-span diet
   (deep-watch drain chunking); then k1,k2 at 60Hz cadence.
4. Sprite transient hardening under bursts.
