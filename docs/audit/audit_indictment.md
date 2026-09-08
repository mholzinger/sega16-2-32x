# ADVERSARIAL ARCHITECTURE AUDIT — sega16-2-32x
### "The only answer is a completely perfect arcade port." Everything below is judged against that bar.

Date: 2026-08-21. Sources: ARCHITECTURE.md, TOOLKIT.md, docs/log/LOOP.md + LOOP6–25.md (full read),
sh_src/m_main.c (6,946 lines, 221 `#if`), md_src/md_main.c (2,188 lines, 81 `#if`),
md_src/packet_fmt.h, tools/patch_game.py (1,212 lines), Makefile (905 lines, **67 flag blocks, 62 distinct `-D` defines**).
Canonical build line is currently **14 flags long**:
`MDBGALL BQCHUNK NTWRAP WIN2 SPRTRUNC BLITSKIP DIRTYROW SPRBAKE BANDSHIFT=16 FBTEXT CUT30 PAL32 FMGATE K2FREE`.

---

## 0. THE CHARGE, STATED PLAINLY

This project set out to run an arcade game whose original hardware draws the screen **for free** —
a tilemap generator and a sprite chip — on a machine that also has a tilemap generator (the MD VDP)
and two 23MHz CPUs. The first architecture used **neither** piece of drawing hardware: it
software-rasterized all five layers into a framebuffer at 20Hz, mapped the game's video RAM into
that same framebuffer, and then spent ~25 documented loops building protocol machinery to survive
the consequences. The project's own documents convict it:

- ARCHITECTURE.md §3: *"We took Model A's arbitration and threw away the VDP, **which no shipped
  title does**... 0.2 VDP writes/frame, all at boot. **Unique in the library.**"*
- LOOP11: *"the arcade game's video RAM was remapped into the framebuffer... hence the per-frame
  handoff, hence the window, hence the 68K stall, hence the blit. **Every cost in the port descends
  from this one mapping.**"*
- LOOP20: *"the drawing already fits in a 60Hz frame, **twice over**... We ship that at 20Hz.
  The machine idles ~87% of every cycle."* — and the 68K's stall was *"56% shipping video state
  to the SH-2s"*: not drawing, **narrating**.
- ARCHITECTURE.md §1's founding claim — *"The 20Hz cadence is not a design failure. It is this
  number"* (the 6.76MB/s / 1.6-passes budget) — was **measured false** in LOOP18: *"80% of the
  blit is an FB-write bus-stall floor **is RETIRED**... writes were never the floor."* The number
  that rationalized 20Hz for weeks was a wrong cost model.

60Hz first appears as a stated target in **LOOP20 (2026-08-20)** — twenty loops in — despite
"framerate" being #1 on Mike's priority list since LOOP9 (2026-08-06). The intervening loops
optimized *inside* the 20Hz architecture (tail shaving, blit thirds, pickup latency, palette
memoization) and the NEGATIVE RESULTS ledger is dominated by attempts to tune a structure that
the eventual fixes (MDBGALL, FBSPR/FBTEXT, FMGATE, K2FREE) simply **deleted**.

The single most damning pattern: **the port's biggest wins were all removals of its own machinery.**

| Win | What it deleted | Where |
|---|---|---|
| MDBGALL pivot | software BG/FG0 compose (blit skips 23.4%→0.2%) | LOOP11–13, ARCH §15 |
| Presentation 2.0 | the blank-decoy flip pair + the whole strobe class | LOOP13 |
| SPRTRUNC | 400 dead words of every sprite push | LOOP17 |
| FBSPR/FBTEXT | the sprite & text DREQ payloads entirely | LOOP20 |
| PKTSLIM | the 80-word prefix "provably dead" | LOOP22 |
| FMGATE + K2FREE | the 68K's whole-window spin (win/ack 73.7→30.2) | LOOP23–24 |
| CUTBLANK removal | 10,000+ black pixels per level start it was causing | LOOP18/19 |

---

## 1. COMPLEXITY INVENTORY

Every named mechanism, what it defends against, its cost, and its record. "Broke/broken-by"
cites the LOOP entry.

| # | Mechanism | Defends against | Cost (LOC / flags / runtime) | Record: what it broke or what broke it |
|---|---|---|---|---|
| 1 | **FM window protocol** (raise → 68K spin → ack), 3 generations: whole-window spin → FMGATE trampoline + 49 gated sites → K2FREE flip-hold | 68K and SH-2 sharing one framebuffer that holds BOTH the display and the game's video RAM | The central tax: window/ack ran 73–210+ lines/vint of pure 68K stall for the project's whole life; FMGATE adds an rte-trampoline in md_start.s + generated gate thunks + `fmgate_derive.py` | The 200-line window IS the port's speed history. FMGATE v1–v6: **five failed timing variants** (LOOP23). K2FREE: **six graves** incl. the FPUSH MAME-wedge and the bus-quiet guarantee "nobody wrote down" (LOOP24). The spin turned out to be an accidental bus-quiet contract — removing it broke the flip span (span 82.9→124 lines). |
| 2 | **DREQ packet family** — 772→852→596/340→variable→84+8n / 4/40..136 word families; 4-word alignment; re-arm per window; split-by-phase; partial apply; **magic tail**; whitelist; per-word vs per-group FPUSH; overpush; double-buffered A/B packet staging; packet_fmt.h single-source | Moving sprite list / text / palette / regs from 68K to SH-2, because ares **discards MD writes to the FB while SH-2 owns FM** (savestate-proven) | packet_fmt.h (121 lines of `_Static_assert`s that exist because 4 consumers desynced); md_main push loops; m_main apply/whitelist; ~48.6 lines/vint of 68K at its peak (LOOP20: "the tail IS the DREQ push") | Two hard 68K deadlocks (LOOP6). 7b: bigger packet → 47% incomplete → artifact field. Overpush **masked** honest truncation as silent −2 word displacement (LOOP13). PAL32 whitelist bug: every palette packet rejected whole, invisible in MAME (LOOP22). KMAX=7 widening: "borderline unplayable" (LOOP25). Verdict on file: *"THE FIFO CANNOT CARRY BULK PALETTE AT THIS POINT IN THE FRAME"* (LOOP25). |
| 3 | **V-gate + quiet zone + reject/retry** (anti-flash gate, thresholds 11300/10300) | Shipping a half-composed frame; flips outside vblank | Threshold tuning across LOOP3–7; DIAG[7]; retry machinery | The 57–66% reject band was **BUILT** by three individually-correct commits (docs/log/LOOP.md "THE BAND'S ORIGIN": COMM 70 + palscan 45 + DREQ 49 lines = the tail); six iterations attacked the wrong term. Gate widening under FMGATE did nothing — the class was frame overrun (LOOP23 W). |
| 4 | **Blit pipeline**: thirds → WIN_TWO 2-window; band queue depth 4; complete-or-defer; BQ_CHUNK; BANDSHIFT; cat-1 single-slot deferral | Full-frame blit not fitting a 38-line vblank (pres-1.0); strobe (LOOP10 cat1 move) | m_main band-queue state machine; the k1/k2 **split-blit seam** — rows 0-112 and 112-224 from frames one cycle apart | 7f striping: hit its target, lost the play pass. Even-thirds: two permanent seams, reverted on play pass (neg 23). Cat-1 deferral silently ate the FG layer under WIN_TWO (last band had no successor — LOOP17). ~1 band dropped **every cycle forever** until BANDSHIFT (LOOP18); shift=32 overshot. The seam is now the dominant remaining tear (LOOP24 Z3) and its real fix is 60Hz. |
| 5 | **Page capture/restore "truth" machinery**: copy_pages, pg_pending/pg_watch/pg_deep, COMM10 live-dirty latch, cap_drain budgets, capture-wide⇒restore-wide | The game **reading back** its own tile staging, which lives in a **banked** framebuffer under a double buffer | copy_pages + cap_page/cap_drain/restore_pages in m_main; the deep-watch drain is today's flip-late tail (span max 239–370 lines, LOOP24) | 1a atomic ship: blind copy across banks = letter-soup tilemaps. PGROTOR: backgrounds **cycling between scenes' art** on ares (LOOP14). Restore-narrow: eyehold 8.18→26.56 (LOOP14). Early COMM10 merge: mid-stream captures poisoned truth (LOOP13). Entirely absent from every commercial title, because no commercial title put game-writable state in the FB. |
| 6 | **Presentation 2.0** (true double buffer, ONE k2 flip in-gate, edge guard, VISRFLIP V-ISR flip, flip-hold) | The strobe: ares defers out-of-vblank FBCTL writes a whole frame; at the edge they latch **instantly mid-scan** (the tear) | flip_span()/visr_vbi in m_main; ~1.9ms ISR steal | **Load-bearing and it worked**: strobe class extinct (0/10,448), flip tear "instrumentally extinct" (LOOP24 Z3). Its own bugs: flip-abort-after-issue corrupted banks (LOOP13 23:00); the ~490 instant out-of-vblank latches needed the edge guard (LOOP24 Z2). |
| 7 | **MD-plane transport (MDBGALL)**: SH-2 tile convert → FB dead block → MD consume → staged records → vblank DMA playback; md_tag LRU allocator; set eviction; usage-mask pens; NT_WRAP wrapped placement + mirror-diffed rows + force-full heal; lossless A/B publish handshake | Getting S16 tiles/nametables/palette to the MD VDP across the only shared memory (the FB), per-bank skew, beam racing, scene-cut storms | ~a third of m_main + the md_main receiver; 9-phase chunk rotation; consume was 47→52 lines of 68K before NT_WRAP phase B (→14.8) | Tag arrays placed inside PAL_SH (LOOP12); receiver raced the beam = tick-row (LOOP13, fixed by staged DMA); demand-bias starved the cell cursor (frozen foreign art); K2FREE broke the lossless contract (2,772 stale generations of 2,778 — LOOP24 Z). **But the pivot itself is the most successful thing in the repo** (tearing 23.4%→0.2% skips; the only same-shape commercial title does exactly this). |
| 8 | **Palette system** (three layers deep): 45 write-thunks + dirty bitmap → region pairs → PAL32 32-word blocks; SH-2 runtime allocator (32 groups, tile/sprite split, pr_key/pr_age/grp_key steal rules, memoized apply_cram, per-set generations, co-owner drift detection d²≥18) | 2048 words of CRAM-equivalent state crossing a 68K↔SH-2 boundary every frame; S16's 128 colour sets vs 32X's 256 CRAM entries | pal_thunks.h generation; the allocator is the single most bug-productive subsystem: **four flags built and measured dead in one loop** (SPRLATE 35/583, GRPRELOC 1/1,915, PAIRHOLD no-change, TILEDEDUP 17x WORSE pen drift on ares) | The purple band, black smoke, inverted-flash, stale-grass, yellow-slab, mint-tint families are ALL this subsystem (LOOP13/19/25). Storm census: mean 4.0 dirty blocks/vint vs a 4-block channel, 64-block storms, **16.9% of pal vints leave CRAM torn** (LOOP25). One "defect" chased for a loop was the arcade itself (gravestone yellow — the oracle was never asked, LOOP19). Current state: KMAX widening failed in the field twice, reverted; storm-flush design pending. |
| 9 | **Sprite machinery**: SPR_SNAP; SPRTRUNC variable push; SPRBAKE (659KB blob + discovery/bake/verify pipeline); FLICKFUSE temporal-alpha trackers; zoom path | Sprite list transport (see #2); decode cost; 60Hz temporal effects sampled at 20/30Hz | bake_sprites.py + sprite_discover + 659KB ROM; FLICKFUSE tracker state machine | bake_find missing `return 0` **drew arbitrary memory as sprites** (LOOP17). SPRBAKE measured **neutral three times** (LOOP18) — kept as dead weight. FLICKFUSE: two versions, ghost class, then **ruled out by Mike**: "dithering is a deviation from the source" (LOOP24). SPRTRUNC is real (~18 lines/vint) and survives. |
| 10 | **BLITSKIP/DIRTYROW** (per-bank group mask + ROWLIVE row-zero tracking, fb_draw_par bank publishing) | Blitting 71,680 bytes of which 79% is empty post-MDBGALL | 896B + 192B masks; verifier flags | Attempt 1 sniffed the bank and corrupted (Zeus's head vanished). Final win: **3.3 lines of 68K** (91.1→88.1) and DIRTYROW is scene-dependent-to-neutral on canonical (LOOP18). Weeks of blit-model work for single-digit lines — the cost model it was built on was retired by its own A/B. |
| 11 | **patch_game.py rebase + thunk generators** (tile dirty thunks, pal thunks, FM gate thunks, mirror remaps ×3 eras) | Running the original 68K binary against the 32X memory map | 1,212 lines; three generations of remap targets for sprites (FB→WRAM→FB→WRAM) and text (WRAM→FB→split) | The remap **churn itself** is evidence: sprite RAM moved 0x85E000→0xFF7000 (FB-discard era)→0x85E000 (FBSPR)→0xFF7000 (K2FREE). Each move was measurement-justified; the sum is that the project relearned where state can live four times. The rebase core (I/O mailboxes, MCU replication, bank shadow) is sound and necessary. |
| 12 | **Instrumentation estate**: DIAG 64 slots + fixed scraps, ~30 probe flags, state_health, parity harness, ares_gate, health_mame | Measuring any of the above | **EIGHT+ documented DIAG slot collisions** (LOOP13 ×4, LOOP18, LOOP21 ×2, LOOP24), each of which corrupted a measurement round or the pipeline itself; "the fixed map is FULL" | The instruments are also the project's best asset — but a fixed-address counter map with 8 collisions is itself an indictment of accreted state. |

Aggregate: **62 build defines, 67 Makefile flag blocks, 302 preprocessor conditionals across the
two main C files, a 14-flag canonical line, and a shipping rom (`make` bare) that is a
*different, older architecture* than the thing Mike actually plays** — the "shipping" MAME-gated
rom still runs pre-pivot software-composed everything, and exists mainly to keep two parity
numbers (title 2.44 / eyehold 3.37) alive.

---

## 2. ROOT-CAUSE CHAIN — the hypothesis, judged against the record

**Hypothesis under test:** almost everything descends from ONE choice — letting the patched
game's video writes land in the 32X framebuffer — which forced FM sharing, which forced the
window protocol, which forced captures/restores/packets.

**Verdict: substantially TRUE but incomplete. The record shows TWO coupled founding decisions
plus one hardware fact, and the hypothesis names only one of them.**

### Decision A — software-raster the whole screen on the SH-2s (the MK2 model, for a game that is not MK2-shaped)

ARCHITECTURE §2–3 documents this precisely: the port copied Mortal Kombat II's architecture
(everything is the 32X framebuffer, MD VDP inert) — and MK2 is *"a fighting game: two big
digitised actors on a backdrop, no tile structure worth the VDP's help."* Altered Beast is a
scrolling tilemap game; Chaotix — *"the one title in the library with the same shape — puts its
background on the VDP and scrolls it for free."* Consequences of A alone:

- The 1.6-passes-per-frame arithmetic and the 20Hz cadence (ARCH §1) — later shown to be a wrong
  *model* but a real *load*.
- A **saturated master SH-2**, which is what killed Chaotix's arbitration protocol when it was
  finally tried (LOOP11: *"A protocol whose premise is 'the master is usually idle' cannot be
  ported to a master that is the bottleneck"* — cadence 3.03→7.48, "broken the second the game
  starts").
- The 28.5-line blit inside every FM window — the reason the window could never be microseconds.

The chain A→(saturated SH-2)→(long window)→(68K stall) is independent of where the game's VRAM
lives. The hypothesis as stated does not produce 20Hz; A does.

### Decision B — map the game's video RAM into the framebuffer (the hypothesis)

`patch_game.py`: tile RAM → FB 0x852000, sprite RAM → FB 0x85E000 (originally), palette → FB
(originally), text → FB (under FBTEXT). LOOP11 states the consequence chain verbatim and it is
correct: *"for us it means the 68K needs FM=0 during gameplay to land its video writes, and the
SH-2 needs FM=1 to touch the framebuffer. Hence the per-frame handoff, hence the window, hence
the 68K stall, hence the blit. Every cost in the port descends from this one mapping."*

But B was not a free choice, and honesty requires saying so: **the 68K can write exactly two
places — 64KB of work RAM (mostly the game's own) and the FB window** (LOOP11: "a hard resource
constraint, not a refactor"). The game's tile RAM is a 64KB address space (~52KB live) and cannot
be mirrored in WRAM wholesale. So *some* game video state in the FB was forced. What was **not**
forced, and what generated most of the churn, is that sprite RAM (2KB), text RAM (4KB), and
palette RAM (4KB) — all of which FIT in WRAM and all of which spent eras there — kept being
moved in and out of the FB as each era rediscovered one horn of the same dilemma:

- **In the FB** → subject to the discard fact (see below) and per-bank skew → torn records, black
  actors, zeroed palette rows (docs/log/LOOP.md band's-origin; patch_game comments).
- **In WRAM** → must be *transported* to the SH-2 → the DREQ packet family and the 68K tail
  (48.6 lines/vint at peak), plus every FIFO loss class.

Four migrations of sprite RAM, three of text, three of palette. That oscillation is the purest
signature of an architecture fighting its own state placement.

### The hardware fact that weaponized B

**ares/hardware discard MD writes to the FB window while the SH-2 owns FM** (savestate-proven:
torn sprite records, zeroed palette rows — docs/log/LOOP.md, patch_game.py). MAME is lenient, which is
why the trap was invisible until hardware contact. This fact converted B from "shared memory"
into "time-division multiplexed memory," and *that* is what forced:

- the whole-window 68K spin (game code must not run while FM=1) → the window/ack tax,
- the per-vint DREQ narration of state the 68K already possessed (the three commits that built
  the 57–66% reject band, each "individually right" — docs/log/LOOP.md),
- the capture/restore truth machinery (banked staging + a double buffer means the game's
  read-backs see the wrong bank unless truth is captured and restored around every flip),
- FMGATE's 49 gated sites and the rte trampoline (letting the game run during the window by
  gating only its FB stores),
- K2FREE's ISR-owned flip and the sacrificial-write analysis.

### Judgment

Trace any mechanism in the §1 table to its root and you land on A, B, or their interaction:

- #1 FM window, #5 truth machinery, #11 remap churn → **B** (+ the discard fact).
- #2 DREQ family, #8 palette transport → **B** (state on the wrong side of the bus).
- #3 V-gate band, #4 band pipeline, #10 blit masks → **A** (the pass count / saturated master).
- #6 presentation, #9 sprite compose → A's renderer, with B's staging constraining the flip.
- #7 MDBGALL transport → the *repair* of A, forced to route through B's shared-FB channel.

So: the framebuffer-residency hypothesis correctly names the ancestor of the *protocol* complexity
(the majority of the LOC), but the *performance* ceiling and the band/tear families descend from
the renderer choice. Two roots, one interlock. The record's own repair sequence confirms it:
LOOP11–13 attacked A (move layers to the VDP) and killed the tearing; LOOP20–24 attacked B
(FBSPR/FBTEXT/PAL32/FMGATE/K2FREE) and tripled the game's CPU share. Neither repair was reached
until the accumulated tuning of the original structure had demonstrably flatlined.

---

## 3. THE 60HZ QUESTION — what structurally prevents it TODAY

Current state (LOOP24/25): 30Hz display (2.07–2.2 vints/cycle), game gets ~80% of the MD 68K
(~62% of the arcade's 10MHz), flip tear extinct, palette storms unresolved.

The raw drawing is NOT the blocker and has not been since MDBGALL:

    one 60Hz frame                16.66 ms
    blit (both CPUs)               3.75 ms
    compose (tiles+sprites+text)   2.63 ms   (sprites 2.19)
    TOTAL DRAWING                  6.38 ms  = 38%          (LOOP20)

What *is* structural, with numbers:

1. **The per-cycle FM/window span is 12.78ms on hardware** (LOOP22 decomposition: blit 5.75 +
   unaccounted glue 4.32 + flip/drain/restore 1.85 + apply_cram 0.65 + copy_pages 0.21). 60Hz
   means a full compose+blit+flip **every 16.6ms** — the span must roughly halve, and the
   "glue" (in-window text capture of 2048 words, FB_SPR fills, launches, joins) is pure
   architecture, not drawing.
2. **The flip must land inside a 38-line vblank every frame.** Today the edge guard *declines*
   ~14% of k2 flips in attract (LOOP24 Z2 [44]=233) and the pre-flip truth drain has a 239–370
   line worst case (deep-watch storms). At 60Hz a declined flip is a dropped frame at twice the
   rate. The drain tail is capture/restore machinery — i.e., decision B's residue.
3. **The capture/restore cost scales with flips.** Text restore is 4KB/cycle; page restores ride
   every flip. Double the flip rate, double the tax — unless the game's writable state leaves
   the banked FB (which is exactly the rebuild question).
4. **The palette channel is already saturated at 30Hz**: mean 4.0 dirty blocks/vint against a
   4-block/cycle channel, storms of 64, 16.9% torn-CRAM exposure; widening it through the FIFO
   failed in the field twice (LOOP25). 60Hz doubles cycles (helps drain) but the FIFO-vs-busy-
   master contention that killed KMAX=7 gets worse, not better. Bulk palette needs a non-FIFO
   path (the storm flush) or in-place residency.
5. **The 68K clock is a hard sub-ceiling**: 7.67MHz vs 10MHz = **0.77x**, minus handler share.
   At the current 52-line handler the game nets ~62% of arcade CPU; the realistic asymptote with
   a ~5-line handler is ~0.75x. If the arcade game has < ~25% per-frame headroom on its own
   hardware, the original binary **cannot** pace 60Hz on the MD 68K no matter what the video
   side does. Nobody has measured the arcade game's own frame occupancy. **Measure it** —
   `mame altbeast` with a scanline probe would answer in an afternoon, and it bounds the whole
   enterprise. (LOOP15 already conceded: "0.77x is the ceiling with the game on the MD 68K.")
6. **What dies free at 60** (LOOP22, correct): the WIN_TWO split-blit seam, scale stepping,
   cadence aliasing (Zeus), sprite/input lag, FLICKFUSE. The record agrees these are costs of
   sub-60 sampling, not defects. This is the strongest argument that 60 is the *simplifying*
   target, not a stretch goal: a window every vint deletes the k-phase state machine, the idle
   beat, the 2-vint gate arithmetic, and the seam class outright.

Bottom line: nothing in the *silicon* prevents 60Hz. What prevents it is (a) ~4–6ms/frame of
window machinery that exists to arbitrate B, (b) a flip-critical drain whose worst case is an
order of magnitude over vblank, and (c) an unmeasured 68K game-logic budget that could yet be
the real wall. (a) and (b) are self-inflicted; (c) is the one honest unknown.

---

## 4. ALTERNATIVE ARCHITECTURES — sketched against the negative-results record

### (a) Game writes NEVER touch the FB; FM=1 permanently; SH-2 free-runs at 60Hz double-buffered

The "Model A endgame": 68K keeps all game video state in WRAM mirrors; SH-2 owns the FB forever
(9 of 14 commercial titles set FM once — ARCH §2); state crosses via DREQ or shared-WRAM-read.

- **What dies from the current codebase:** the FM window protocol entirely (#1), FMGATE's 49
  gates and trampoline, K2FREE, the capture/restore truth machinery (#5) — no banked game
  staging, no restores, no pg_watch — the V-gate/reject apparatus, the flip-hold, most of
  packet archaeology. The blit itself can die: with FM held, composing straight into the hidden
  FB bank is legal by construction (the 85.8%-FM=0 measurement that killed it was taken at FM=0
  — LOOP11 explicitly flags this: *"it is not evidence against step 5; it is evidence FOR step 4"*).
- **What survives:** MDBGALL planes + the MD-plane transport (still need tiles/NT/CRAM to the
  VDP), staged-VDP-DMA playback, SPRTRUNC-style variable push, packet_fmt discipline, the
  compose core, patch_game rebase.
- **Known killers to design around, from the record:**
  1. **Tile RAM residency.** 52KB live tile staging cannot fit WRAM; the game *reads it back*
     (collision probes ~15KB @ 0x400000-0x403FFE, single words; 1KB scratch — ARCH §9). The
     write-log ring is DEAD as designed — thunks fire at pointer-load, not per-store, so values
     can't be logged (LOOP6 falsification; LOOP13 "the write-log ring is DEAD"). Options that
     remain: shadow the 15KB readable subset in WRAM (ARCH §9's own fallback — the game writes
     that data itself, so a shadow is free to maintain *if* the writers can be intercepted at
     STORE level, which means patching stores not pointer loads — patch_game must grow a
     store-level pass, cost unknown); or keep tile staging as the ONE FB tenant with a
     Chaotix-style microsecond window for it alone (tile writes are transition-bursty: 95.8% of
     cycles zero dirty pages, LOOP11 — the window would be rare and tiny).
  2. **DREQ under a busy master loses words** (ares full-FIFO drop race — LOOP13/15), and bulk
     transfer during the SH-2's busy span fails (LOOP25 KMAX=7). Mitigations already proven:
     per-word polling (costs ~13 lines), magic tail + whitelist, push scheduling into the SH-2's
     vblank. With FM=1 permanent the SH-2 *chooses* its quiet windows — Chaotix's CMD-accepted +
     FIFO-full-per-group protocol (ARCH §2 Model C: VRDX pushes 0x500 words/frame reliably this
     way) becomes applicable because the arbitration pressure is gone.
  3. **The 68K tail returns.** The push-narration cost that FBSPR/FBTEXT deleted comes back
     unless most state is *pulled* by the SH-2 from WRAM instead of pushed. Nothing in the
     record tests SH-2 reads of MD WRAM through 0x2xxxxxx at gameplay rates — the 32x-builder
     finding (SH-2 MMIO polling >100K/s disrupts 68K bus — NOTES/LOOP10) bounds it. **This is
     the one cheap probe alternative (a) needs before anyone commits.**
- Verdict: this is the architecture the last five loops have been converging on by subtraction
  (K2FREE already has the SH-2 owning FM's fall and the 68K down to ~5-line k2 handlers). The
  rebuild version reaches it directly instead of via 25 loops of strata.

### (b) Full MD-plane rendering — S16 tiles on VDP planes, MD sprite chip for sprites, SH-2 only for residue

- **Killed by measurement, keep it dead:** MD sprites cannot express S16 zoom (werewolf
  transformation — jts16_obj_draw.v:70), ragged strips, or the load: pixel-per-line budget
  exceeded on 8% of lines (upper bound), and the killer is **palette** — MD sprite palettes would
  compete with the already-oversubscribed 3 background lines (LOOP18: "the MD-hardware-sprite
  hybrid is dead on palette, not on geometry"). Cross-chip priority is also inexpressible
  (32X-vs-MD is one global boundary; S16 interleaves ten deep — jts16_prio.v:84-95).
- **What survives from it:** the current hybrid IS the correct partial form of (b): planes on
  the VDP, sprites + FG cat-1 + text on the 32X. Keep exactly that split; the census instruments
  (SPRLINE, sprite_census) are per-title kit tools for the next game.

### (c) 32X-native rewrite of the game loop (SH-2 runs the game logic; 68K becomes an I/O shim)

Never attempted; LOOP15 names it ("moving game logic off the 68K — a different project"). It is
the only architecture that beats the 0.77x clock ceiling, and it forfeits the project's premise
(run the original binary — the entire patch_game/derive-don't-guess asset). Cost: a full-game
reimplementation or static recompilation of 68K→SH-2, against the fidelity bar. **Not
recommended, but if §3 item 5's measurement says the arcade game needs >77% of its 10MHz, this
is the only door left and everyone should know that before rebuilding anything else.**

### (d) The pragmatic rebuild (recommended shape)

Not a new architecture — the CURRENT endpoint, rebuilt clean with 60Hz as the design center:

1. Keep: MDBGALL planes, staged-VDP playback, pres-2.0 flip discipline + edge guard + V-ISR,
   SPRTRUNC, packet_fmt, patch_game core, the instrument estate.
2. Take (a)'s state placement as the goal: sprites/text-regs/palette in WRAM (they are TODAY,
   post-K2FREE, except glyphs), tile staging as the sole FB tenant with a bounded window or a
   store-level WRAM shadow.
3. Design the frame as: one vint = one frame. V-ISR owns flip; compose into hidden bank
   (FM never drops for display purposes); 68K handler = consume + short push + game.
4. Solve palette residency (storm flush or in-place mirror read) BEFORE building anything else —
   it is the standing torn-frame defect and every palette mechanism in #8 is evidence the
   FIFO-streaming approach is exhausted.
5. Delete: every NEVER-SHIP probe flag into a separate probes.mk; the 20Hz/3-window code paths;
   the shipping-rom-as-museum (re-baseline the parity gate on the real architecture, on the
   scenes MAME can still see).

---

## 5. WHAT IS PRECIOUS AFTER ALL — the hard-won facts any rebuild must honor

Hardware/system facts (not code). Each cost at least one broken build to learn.

1. **MD writes to the FB window are DISCARDED while the SH-2 owns FM** (ares/hardware strict,
   MAME lenient). Savestate-proven: torn sprite records, zeroed palette rows. [docs/log/LOOP.md "band's
   origin"; patch_game.py sprite-RAM comment]
2. **An FBCTL flip written outside vblank either defers a whole frame (ares, LOOP7c) or latches
   INSTANTLY mid-scan (the tear — ~490 instant latches, LOOP24 Z).** The flip must be issued
   inside the gate; past the edge, *declining* the flip (drop a frame) beats issuing it. Once
   issued it cannot be taken back — always restore. [LOOP13 23:00; LOOP24 Z2 edge guard]
3. **DREQ FIFO contract:** drains in 4-word bursts (non-multiple-of-4 strands the tail, TE never
   sets); one transfer per arm, re-arm every window; a push into an undrained FIFO hard-deadlocks
   the 68K; **ares drops a 68K FIFO write that races a full FIFO WITHOUT decrementing the armed
   length** → silent word-displacement — any packet needs a positional magic tail + a length
   whitelist; below one burst remaining DREQ never asserts (overpush needed); **partial landings
   are readable on ares (233/234) and invisible in MAME (0/62)**; bulk pushes during the
   master's busy span lose words wholesale. [LOOP5/6; LOOP13 magic-tail; LOOP17 drq_probe;
   LOOP25 KMAX failure]
4. **COMM costs 1.27 lines/word vs DREQ 0.063** — the cost is the ack round-trip, not the
   payload; and an SH-2 polling adapter MMIO >~100K reads/s disrupts the 68K's own bus.
   [docs/log/LOOP.md decisive ratio; LOOP10 32x-builder mining]
5. **32X-over-MD transparency is CRAM bit 15 (the through bit), not pixel index 0** —
   `cram[0]=0x8000` opens the layer; index-0 with bit 15 clear is opaque black (the black-flash
   family). [ARCH §10]
6. **MD VDP writes must land in vblank** — a receiver racing the beam produces persistent
   phantom artifacts no RAM audit can see (the tick-row); stage the words and DMA them at vint
   top (~205 words/line DMA vs ~30/line move.w). [LOOP13 parts 3–4]
7. **The blit is throughput-bound on the whole memory system; partial removal returns
   sub-linearly** (−57% stores → −14% cost; −99% loads → −29%; DMAC blit 1.77x slower; cached ≡
   uncached FB writes). Only removing whole rows — reads AND writes — pays. LOOP9's "80%
   bus-stall floor" and "SDRAM reads 5x cheaper" are RETIRED; do not resurrect them. [LOOP18
   four-probe decomposition; docs/log/LOOP.md neg 20–21]
8. **Chaotix's idle-token protocol requires a mostly-idle SH-2** — ported onto a saturated
   master it collapses cadence (3.03→7.48). Interrupt-driven pickup with yielding moves latency
   to band-completion where a no-slack pipeline cannot absorb it. A bounded in-order ISR that
   does the critical work itself (VISRFLIP) is the form that works. [LOOP11 part (a) + CMDINT
   postmortem; LOOP24]
9. **S16 hardware facts (jtcores):** 10-deep priority interleave (jts16_prio.v:84-95, the port's
   scalar-max model is exactly equivalent); shadow = `shadow & ~pal[15]` (jts16_colmix.v:88);
   BG pen 0 is OPAQUE (jts16_prio.v:87); sprite zoom to ~0.5 in 32 steps + ragged variable-width
   strips ⇒ **S16 sprites are inexpressible on the MD sprite chip**; Altered Beast never enables
   row/column scroll. [ARCH §4–5; LOOP11 jtcores verdict]
10. **Game behavior facts (this title, measured):** tilemap is ~static in gameplay (95.8% of
    cycles zero dirty pages — scrolling is registers, not rewrites); sprite list fully rewritten
    every vint (mean 12.5 of 64 records live, max 21); text written sparsely (needs restore);
    palette mean 9.7 changed words/frame BUT storms dirty all 64 blocks at once; tile-RAM
    read-back = 15KB of single-word collision probes + 1KB relocatable scratch; sprite decode is
    73% repeated frame-to-frame; the game reads its own tile staging, and its writers mark at
    POINTER-LOAD (store-level logging impossible with the current thunk mechanism). [LOOP11
    TILERATE; LOOP17; LOOP22 pal_diff; LOOP25 PALSTORM; ARCH §9; LOOP13]
11. **The arcade itself does startling things — ask the oracle before "fixing" colour/flicker.**
    The gravestone yellow flash is genuine arcade behavior; Zeus's translucency is a graded
    temporal duty cycle (off-frames REMOVE the record) that only 60Hz sampling reproduces —
    Mike's ruling: no synthesized dither, stay true to source. [LOOP19; LOOP20 Zeus oracle;
    LOOP24 Z3]
12. **Emulator split-brain:** `mame altbeast` = the oracle, always valid. MAME's 32X = a
    convenience model — no SH-2 timing (~3x fast), no FIFO loss, no partial DREQ landings, no FB
    stall; two precise per-flag gating limits (SPRTRUNC pixels, BLITSKIP speed), everything else
    gates normally. ares ≈ hardware truth; treat any ares comparison under ~3000 cycles as
    unreliable; savestates carry SDRAM RAMCODE (check the BUILD stamp); probes move the thing
    they measure. [CLAUDE.md; LOOP11 sample-length correction; LOOP18 traps]
13. **Method invariants that repeatedly paid:** falsify every verifier before trusting its zero;
    build the control before reading the picture; a metric that won't move under speedups is an
    asymmetry, not a deficit; single-source every multi-consumer format (packet_fmt.h) and
    verify generated code by disassembly; `.build_flags` stamping (any flag measurement before
    0faba16 is void). [LOOP10/18/19/22]

---

## 6. THE SENTENCE

The port works — 30Hz, near-arcade visuals, flip-tear extinct, ~80% of the 68K back — and it got
there by spending roughly twenty of twenty-five loops discovering that its founding architecture
was wrong on both axes, then dismantling it one protocol at a time while keeping every
scaffolding layer the dismantling required. The current codebase is the strata of that history:
62 flags, three generations of window protocol, four migrations of sprite RAM, a museum shipping
rom, and a palette subsystem whose last four fixes were all measured dead. Nothing about the
hardware demands this. The record itself — MDBGALL's 100x tearing win, K2FREE's 5-line handler,
the 6.38ms drawing budget — proves a clean build centered on 60Hz, MD planes, WRAM-resident game
state, and an SH-2-owned flip clock needs none of the strata. Keep the facts in §5, the
instruments, and the pivot; burn the rest.
