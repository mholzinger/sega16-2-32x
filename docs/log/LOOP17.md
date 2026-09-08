# LOOP 17 — THE SPRITE BAKE: precompute in the ROM what the
# beam-racer had to re-derive per scanline. HANDOFF KICKOFF.

Fresh session: read this, then docs/log/DEVNOTES.md; LOOP16 has the 2-window
cycle era in full. Where LOOPs and ARCHITECTURE.md disagree,
ARCHITECTURE wins. Memory `release-bar-flawless` is the bar.

## WHERE THIS STANDS (2026-08-17 EVENING — read this, then jump to
## "START HERE NEXT" at the bottom of this file)

**Canonical build (2026-08-17):** `make MDBGALL=1 BQCHUNK=1 CUTBLANK=1
NTWRAP=1 WIN2=1 SPRTRUNC=1` — SPRTRUNC folded in on the ares verdict
(handler mean 91.3). Baseline before today: handler mean 110, skips 0.3%, slave
idle 14,427/cycle (v10.bs9, BUILD 78f9c4b2).

**TODAY'S LANDED WORK** (all committed, nothing pushed):
  - **cat-1 fix** (in the canonical build): the last slave band of a
    WIN_TWO cycle had no successor to drain its deferred FG cat-1, so
    rows 144..183 lost their over-sprite tiles every frame — the
    arcade's tall grass blades came out a flat strip. Mike: "RESTORED!"
    Costs nothing measurable on identical play.
  - **`SPRBAKE=1`** — pre-decoded sprite frames (477 native, 659KB
    blob, build-time pixel-identity gate). ares: handler mean
    110 -> 105.2, slave idle -> 16,190, 34.9 bake hits/cycle, 4.5%
    miss. MAME called it speed-neutral; ares overruled it.
  - **`SPRTRUNC=1`** — push only the live sprite records (mean 12.5 of
    64). Measured 83.8 -> 65.4 handler mean, tail 63.3 -> 46.5.
    **AWAITING MIKE'S ARES VERDICT — this is the biggest lever found.**
  - **`DRQPROBE=1` + tools/drq_probe.py** — settled that partial DREQ
    landings ARE readable on ares (233/234) and are invisible in MAME
    (0/62). That fact unblocked SPRTRUNC and is now a kit invariant.
  - **tools/health_mame.lua** — state_health's meters live in MAME on
    a scripted play, for cheap A/Bs without spending an ares pass.

**MAME'S ROLE CHANGED TODAY (Mike): it is the LOOK-AND-FEEL oracle
(mame altbeast), NOT an authority on our machine.** See CLAUDE.md
"What MAME is for now". A build can be correct and render wrong in
MAME — SPRTRUNC is exactly that.

Mike's A/B vs MAME: backgrounds solid, character pixel work solid.
Remaining: **animation stepping/tearing and gameplay speed** — and
the RELEASE BAR says motion must MATCH arcade (our methods, our
palette OK): the Zeus scale stepping is a MUST-FIX, not accepted.

Closed eras (never reopen without new evidence): transport loss
(misaligned 1914->1, per-word push), stale tilemap truth
(PG_STICKY), co-owner pens, presentation strobe classes, the
3-window handler tax (WIN_TWO: mean 134->110), band-tear majority
(single-snapshot). Dead ends list: LOOP15/16 TRAPS + DEAD ENDS.

## THE TWO MEASUREMENTS THAT DEFINE THIS LOOP

  - **Sprite decode reuse: 73% of per-cycle jobs identical to the
    previous cycle; 74-85% of decode COST is repeated** (SPRREUSE
    probe, BUILD d90198f9, MAME attract).
  - **Slave idle: 14,427 polls/cycle** — the second SH-2 has large
    unused capacity.

## THE JOB, in order (bake-first — Mike: "we have full control —
## precompute in the bake, not the engine")

ROM BUDGET (measured): altbeast_sprites = exactly 1MB at
0x02042CE0; cart 3.25MB of the 4MB no-mapper ceiling -> **768KB
free**. Full byte-expansion (2MB) doesn't fit; full replacement has
a completeness risk (frames defined by runtime tables). Hence the
HYBRID BAKE with live fallback:

1. **DISCOVERY**: extend the SPRREUSE probe (its key IS the bake
   key: sprite data addr + geometry words + zoom + set) to DUMP the
   unique-key set — Lua reads the tables at 0x28D00/0x28DC0 per
   cycle, or better: log every NEW key with full 8-word entries.
   Coverage runs: 65s attract + replays of the recorded inputs in
   inp/ (mame -playback) + Mike's manual passes. Late-discovered
   frames just stay on the live path — correct, not broken.
   **JOB 1 RESULT (2026-08-17) — DISCOVERY DONE, THE BAKE FITS.**
   Instrument: `tools/sprite_discover.lua` (arcade oracle, object RAM
   0x440000 — same decode as sprite_census.lua), driver
   `tools/sprite_discover.sh`, report `tools/sprite_discover.py`;
   CSVs in `discover/`. Two passes: attract 3901 frames + scripted
   play 7201 frames (coin/start/walk/mash), 68,469 record sightings.
     - **743 unique keys: 477 native-zoom, 266 zoom-only.**
     - **Full native bake = 440.0 KB of the 768 KB free — FITS, with
       328 KB spare.** No top-N cut needed; bake everything native.
     - Coverage: native records are 94.9% of sightings and **93.7% of
       decode words** — the 6.3% remainder is zoomed, and stays on the
       live decoder by design.
     - Cost is fiercely concentrated: **12 frames (9.0 KB) = 50% of
       all decode words**; 53 frames (44.8 KB) = 75%; 177 frames
       (169 KB) = 90%. A partial bake would already pay; the full one
       is affordable anyway.
     - Largest single frame 4.6 KB (h=108, 2272 words).
   The KEY is (bank, addr, d2 pitch+flip, height) — pixel-determining
   only. Colour set is deliberately OUT (it is a runtime `base` add,
   so excluding it merges palette-pair twins and raises the hit rate);
   xpos, priority and absolute top are out for the same reason.
   Sizes are MEASURED by walking the strip like the chip does (row
   ends when the LAST nibble of a word is 0xF), not estimated.
   Caveats for job 2: coverage is attract+scripted only — later levels
   will add keys (they stay live, which is correct, not broken); and
   the arcade list is 128 records where the port's snapshot is 64, so
   a few discovered keys may never appear on our path (wasted ROM, not
   wrong pixels). Re-run the driver after any of Mike's passes and
   merge; `sprite_discover.py` takes any number of CSVs.
   NOTE: this MAME build has no `emu.register_stop` — the CSV is
   written only when DISC_FRAMES is reached. Never kill a run early.

2. **tools/bake_sprites.py**: decode each discovered frame OFFLINE
   from the sprite ROM (port the compose_sprites walk in python —
   nibbles, strips, pitch, flip variants NOT needed) into a linear
   4bpp copy-friendly blob + open-addressed hash index; link into
   the 768KB free region (new .s incbin like sprites_data.s).
   **BUILD-TIME PIXEL-IDENTITY CHECK**: for every baked frame,
   re-decode via the exact live algorithm and compare — the
   accuracy gate runs before the ROM exists. Zoom==native only
   (the scaling pipe keeps the live decoder).
   **JOB 2 RESULT (2026-08-17) — BAKE BUILT, PIXEL IDENTITY GREEN.**
   `tools/bake_sprites.py` pre-decodes the 477 native frames offline
   and emits `sh_src/sprbake.bin` (+ `sprbake.h`), .incbin'd by
   `sh_src/sprbake_data.s`, linked ONLY under SPRBAKE=1.
     - **Accuracy gate: 81,380 row renders, 0 mismatches.** Every row
       of every baked frame is rendered twice in python — once by a
       port of the LIVE algorithm, once by a replay of the baked
       format — at four x positions (100 left-clip, 184 the NIB_NC
       aligned case, 300, 496 right-clip). Mismatch = build failure,
       so the gate fires before a rom exists.
     - **Blob 659.1 KB** (frames 635.1 + index 24.0); measured free
       gap 743 KB, so **84 KB headroom** after the bake.
     - Index: 2048 open-addressed slots, **max probe 4**, mean 0.14.
       Hash is one 32-bit multiply (SH-2 has MUL.L) of the packed key,
       shifted down. The cheap 16x16 fold was measured on the real key
       set first and clusters badly (max probe 12 at 1024 slots): one
       animation's frames sit a few words apart and share d2 exactly,
       so the hash must mix low bits upward.
     - Format: per frame, a row-offset table then per row
       `u16 nsegs` + `{u8 skip, u8 len, u8 pen[len]}`. Pens are stored
       as BYTES (1..14), not packed nibbles — the point is a copy
       loop, and the runtime adds `base` per pen. That costs ~195 KB
       over the 440 KB packed estimate in job 1 and buys the fast
       path; it still fits. Trailing transparency is dropped.
   **CART GUARD ADDED** (`make`, every build): `.gamehigh` is PINNED
   at 0x02300000 and the rom image ends at 0x02340000 — the bake's
   free space is that gap, NOT the 4MB no-mapper ceiling, and ld does
   not police the overlap. A silent overrun would corrupt the 68K high
   rom and fail as a game bug far from its cause. Current image ends
   0x2EB040.
   Two build traps paid here: this is GNU Make **3.81**, which has no
   `&:` grouped targets (it parses `&` as another target, so two such
   rules fight — the pre-existing game_body rule is one); and SPRBAKE
   started as a LINK-only flag, which `.build_flags` cannot see, so
   `make` after `make SPRBAKE=1` said "nothing to be done" and handed
   the probe rom back as the baseline. SPRBAKE now also defines
   `-DSPR_BAKE`, which is what job 3 compiles against anyway.
   Gates after the change: statics title 2.44 dx=0 / eyehold 3.37
   dx=0 EXACT (parity_loop17_bake), region guard 0x06018BB0, shipping
   stamped normal, plain build links ZERO sprbake symbols.

3. **Compose fast path** (flag SPRBAKE=1): per sprite, hash-probe
   the ROM index; hit -> linear copy loop (sequential cache-line
   ROM reads); miss -> live decoder untouched. Region guard
   headroom ~0x1A8 — budget the code, diet plan ready (LOOP16's
   relocation playbook).
   **JOB 3 RESULT (2026-08-17) — FAST PATH CORRECT, BUT MAME SHOWS NO
   WIN.** `compose_sprites` under SPR_BAKE hash-probes per sprite; a hit
   replays the baked segments, a miss falls through to the live decoder
   untouched. Taken only for native-zoom, ungated, non-darkening
   sprites; everything else is live by construction.
     - **Region guard: fits, 80 bytes spare** (canonical MDBGALL+BQCHUNK
       +CUTBLANK+NTWRAP+WIN2+SPRBAKE _end 0x06018FB0). It did NOT fit at
       first (0x06019038, 56 over). What bought the room: `bake_find`
       moved OUT of RAMCODE to cart rom — it runs once per SPRITE per
       strip while the draw loop runs once per PIXEL, so it is the half
       that can afford a slow fetch. Also cut: the forced-live and
       baked-row meters.
     - CODE SIZE HERE IS NOT LINEAR IN SOURCE. Dropping the probe's trip
       counter for a bare `for(;;)` GREW .ramtext 24 bytes; adding
       `noinline` while still in RAMCODE grew it 40. gcc's inlining
       decision moves in 40-byte steps between edits that change nothing
       relevant. MEASURE THE GUARD after every edit; do not reason about
       it.
     - **Correctness: pixel-exact where it can be compared at identical
       timing.** The first 460 emulated frames are BYTE-IDENTICAL to the
       shipping build (per-frame screen checksum), and the title and
       scream anchors are exact — title 2.44 dx=0 with ZERO differing
       pixels against the shipping capture.
     - **A REAL BUG SHIPPED THROUGH THE JOB-2 GATE, and the gate is now
       fixed.** `decode_frame` walked `range(1, height+1)` with
       `pitch*(r+1)`, so every baked row was the live decoder's NEXT row.
       The checker missed it because it fed the SAME wrong row list to
       both renderers: it proved the FORMAT round-trips and never proved
       the ADDRESSING. `check_frame` now derives the live rows
       independently from the record fields and parses the emitted
       RECORD BYTES on the baked side. Verified by reintroducing the
       bug: 50,024 of 81,380 row renders fail, 0 after the fix.
     - **SPEED: the bake is NOT faster in MAME.** Same cycle count (964
       both), in-window time 1,258,325 -> 1,290,433 = **+2.6%**. The one
       clean pre-divergence sample (f400, before any timing shift) is a
       wash. Prime suspect is cart-ROM traffic: the blob stores one BYTE
       per opaque pen, where the live decoder reads one WORD per FOUR
       pens — roughly double the bus volume, spent to save instructions
       on a machine whose bottleneck is the cart bus. The fix, if the
       ares verdict agrees, is to repack the pens as NIBBLES (halves both
       the traffic and the 659 KB blob) and unpack with shifts; the win
       then comes from skipping transparent runs and dropping the
       per-pixel range test, not from a byte copy.
     - **THE STATICS GATE CANNOT JUDGE THIS CHANGE.** Any real timing
       change shifts the attract phase, so the state-anchored eyehold and
       demo captures land on a different moment (eyehold 3.37 -> 31.89,
       demo dx +24 -> -24) even when every comparable pixel is identical.
       The first divergent frame is 461, and what differs there is the
       SKIP-RATE DEBUG BARS — the instrumentation, not the content.
       Judge this class by: (a) per-frame checksum equality up to the
       first timing divergence, (b) title/scream exactness, (c) Mike on
       ares. Do not read eyehold 31.89 as a rendering regression without
       looking at the frame.

   **ARES VERDICT 1 (2026-08-17, savestate 9, BUILD fbbb7e31) — A REAL
   BUG, AND THE SPEED NUMBER IT PRODUCED IS VOID.** Mike saw a tan band
   of FOREIGN ART (recognisable sprite pixels) down the right of the
   ROUND CLEAR screen, and sprites missing from the BOTTOM band (grass
   below the knees, the man->beast transition). state_health: handler
   mean 110.9 (v10: 110), flip/blit skips 1.8% (v10: 0.3%), rejects
   0.4% (0.1%), slave idle 13351/cycle (14427).
   ROOT CAUSE: `bake_find` had no `return 0` after its bounded probe
   loop. Falling off the end handed the caller whatever was in r0, which
   it used as a frame pointer and DREW -- arbitrary memory decoded as
   pens (hence real sprite art in the band), with a garbage row count
   and segment stream burning the compose window (hence the unfinished
   bottom band and the skip rate).
   IT ONLY FIRES ON A MISS. Attract has ZERO misses -- every frame in it
   was discovered -- so MAME, the parity statics and the per-frame
   checksum could not see it; ROUND CLEAR is exactly the content the
   discovery corpus does not cover.
   gcc WARNED ON EVERY BUILD ("control reaches end of non-void
   function") and the warning went past in the noise. `-Werror=return-
   type` is now on both compilers, because this class is silent
   corruption.
   **THE 110.9 / 1.8% NUMBERS MEASURE THE BUG, NOT THE BAKE** -- every
   miss was drawing a garbage frame. Re-measure before drawing any
   conclusion about whether the bake pays.
   After the fix, MAME gameplay (canonical flags, coin+play to f3200):
   in-window 2,278,443 vs 2,278,463 shipping, SAME cycles (1063) and
   skips (1), 39,626 bake hits, 0 misses -- speed-NEUTRAL, and gameplay
   frames 0 px different at f2000 and f3200.

   **ARES VERDICT 2 (2026-08-17, savestate 9, BUILD 15b8cdde) — THE
   BAKE PAYS.** With the `return 0` fix in:
     - **68K handler mean 110 -> 105.2 lines/vint** (game ~58% -> ~60%
       of the MD 68K, ~46% of the arcade). The v10 baseline it beats is
       the WIN_TWO era's own win (134 -> 110).
     - Slave idle 14,427 -> **16,190 polls/cycle**; flip/blit skips 1.8%
       (bugged bake) -> **0.7%**; rejects 0.3%; cadence 3.01; drift 0.
     - Worst handler is now **TAIL-dominated** (window/ack 0, tail 251)
       where v10 was window/ack-dominated. The bake moved the bottleneck
       off the master's FM work — the split (window 46.9 / tail 58.2)
       names the next surgery, and it is no longer compose.
     - Hit rate on Mike's real play, read out of the savestate:
       **34,332 hits / 1,609 misses (4.5% miss), 34.9 hits per cycle.**
       Discovery covers 95.5% of what he actually played; the misses
       fall through to the live decoder, which is what they are for.
   MAME COULD NOT SEE ANY OF THIS (it called the bake speed-neutral).
   The trap "MAME cannot rank SH2 timing" earned its place again.
   OPEN, AND NOT THE BAKE: Mike reports the bottom band missing content
   (grass at the foot line, the man->beast transition in that area).
   A/B'd in MAME over 2000 gameplay frames, canonical vs canonical+
   SPRBAKE, hashing ONLY rows y>=180: **9 frames differ, in two 3-frame
   runs** — animation phase, not a missing band. The bake is exonerated
   in MAME; the next step is Mike replaying the same spot on canonical
   WITHOUT SPRBAKE to confirm it is pre-existing in the MDBGALL bundle.

   **THE MISSING BOTTOM LAYER IS THE DEFERRED FG CAT-1, AND IT IS NOT
   THE BAKE (2026-08-17).** Mike: grass missing at the foot line, and
   the man->beast transition in that area. Proven, in order:
     - His savestate on BUILD 7c18d18c reads **SPRBK hits 0, misses 0**
       and the rom links zero sprbake symbols — he was on canonical
       WITHOUT the bake and the layer was still missing. Bake cleared.
     - Arcade vs ares at the same spot: the arcade grass is TALL BLADES
       that overlap the boots and legs; ours is a flat strip. The
       missing thing is the FG cat-1 pass (tiles drawn OVER sprites),
       not a sprite and not a sprite SIZE.
     - **A/B settles it: `make CAT1INLINE=1` (new flag, -DNOCAT1DEFER=1)
       brings the blades back and matches the arcade.** Deferred: gone.
       Inline: correct.
   MECHANISM: the slave records its band's cat1+text into a SINGLE slot
   (cat1_lo/hi/bank/par/t0/t1 + cat1_valid) and runs it at the top of
   the NEXT slave_concurrent_k. One slot, three slave bands per cycle —
   `deferrals=1334` against `cycles=1336` is ~1 per cycle where 3 bands
   are recorded, so bands are overwriting each other's owed cat1 before
   it is drawn.
   CAT1INLINE IS THE DIAGNOSIS, NOT THE SHIP: LOOP 10 moved cat1 to the
   gap for the strobe win, and the comment above the pass records that
   striping it also cost V-gate rejects 0.7 -> 5.9% and sprite
   artifacts. The fix is to make the deferral NOT LOSE WORK (a slot per
   band, or drain before record), keeping the gap placement.

   **CAT-1 FIXED (2026-08-17) — the last band had no successor.**
   Corrected mechanism (my first reading was wrong: `deferrals` in
   state_health is DIAG[13], the BAND QUEUE's full-queue counter, and
   has nothing to do with cat1). The real cause is a WIN_TWO
   interaction: WIN_TWO launches all three slave halves at k2 and
   CHAINS them, so the LAST band R2 (rows 144..183) has no following
   call inside the cycle — its owed cat1+text would not run until the
   next cycle's first call, AFTER R2 has shipped. The grass line at the
   foot sits exactly in 144..183.
   FIX: R2 drains its cat1 at its own tail, with its params still live
   (rg==2 also pins the text rows to 18..23, so no reload through the
   cat1_* record). R0/R1 keep the deferral untouched — the LOOP 10
   strobe placement is preserved for the two bands that actually have a
   gap after them; the last chain link never had one.
   Cost: +80 bytes of RAMCODE (canonical _end 0x06018E50 -> 0x06018EA0).
   Canonical+SPRBAKE now lands at EXACTLY 0x06019000 — passing, with
   ZERO margin. The next RAMCODE byte in that combination has to buy
   its room first.
   SHIPPING IS UNTOUCHED: the fix is inside `#ifdef WIN_TWO`, shipping
   _end unchanged at 0x06018BB0, statics title 2.44 dx=0 / eyehold 3.37
   dx=0 EXACT.
   Verified against the arcade at the level-1 start: the tall blades are
   back and overlap the sprites as they do on the arcade.

   **THE TAIL, MEASURED (2026-08-17). IT IS THE DREQ PUSH, AND THE PUSH
   IS MOSTLY DEAD WORDS.**
   New instrument: **`tools/health_mame.lua`** — state_health's meters
   read LIVE in MAME off the same MD addresses, on a scripted play, so
   builds A/B without spending an ares pass. Read it as a RANKING, not
   a clock (SH-2 ~3x fast), but the MD-side halves are 68K/VDP timing,
   which MAME models honestly — and the tail dominates there too
   (tail 63.3 vs window 20.5 at f3200).
   TAIL SPLIT (`TAILPROBE=1`, LOOP 6 accumulators): **DREQ push 67.2
   lines/vint, stream 0.4, palscan 0.2.** The push IS the tail.
   WHY IT COSTS THAT MUCH — two multiplying facts:
     - Under NT_WRAP (in the bundle) FPUSH polls FIFO-full BEFORE EVERY
       WORD, not once per 4-word group. That is the LOOP15 ares fix for
       dropped burst tails and it is correct; it means every word costs
       TWO slow 68K->32X register accesses (poll + write).
     - **The sprite list is pushed as a FIXED 512 words. Measured live
       records: mean 12.5 of 64, max 21** (terminator scan of the MD
       mirror 0xFF7000 over 2000 gameplay frames). So ~100 words of
       live data ride inside 512 words of push.
   THE SURGERY: truncate the sprite push at the terminator. The arming
   scheme ALREADY supports variable length — the master arms the max
   and lets `landed` (armed - TCR0) speak, which is what makes the
   palette payload optional today. Needed, in ONE gated commit (the
   DREQ trap): MD publishes 82 + 8*live(padded to a 4-word group) + 2
   at 0xA15110 and pushes exactly that; master accepts the variable
   `landed` instead of comparing to 596/852 literals, keeps the magic
   tail check at landed-2, and copies only the landed sprite words into
   SPR_SNAP (the terminator rides inside the copy, so compose stops
   there and stale tail records are never read).
   SIZE OF THE PRIZE: ~400 words off the sprite phase. At the measured
   cost per word that is tens of lines of 68K per vint against a
   handler mean of ~107 — the largest single lever found this loop.

   **TRUNCATION ATTEMPT 1 — HUGE PRIZE, REVERTED, BLOCKER NAMED
   (2026-08-17).** Built both sides: MD scans the mirror for the
   terminator, publishes 84+8*nrec at 0xA15110 and pushes nrec*2
   groups; master accepts the variable length family
   (landed>=92, <=596, (landed-84)&7==0), keeps the magic-tail check at
   landed-2, and copies only `landed-84` words into SPR_SNAP.
   **THE PRIZE IS REAL AND LARGE**, measured on identical scripted play
   (tools/health_mame.lua, canonical + cat1fix, no bake):
     - **68K handler mean 83.8 -> 65.4**; tail 63.3 -> 46.5; window
       20.5 -> 18.8. That is ~18 lines/vint of 68K time back.
   **BUT IT DOES NOT LAND.** MD side verified correct in MAME
   (publen=228, ng=36, pushes=996 — exactly one per sprite window, and
   228 = 84 + 8*18 for the 18 live records). The MASTER never sees it:
   over 996 sprite-phase windows `landed` was only ever 0 (TCR0 still at
   the armed value = nothing transferred) or 596 (a stale TE), NEVER
   228. So the packet is pushed and the partial transfer is invisible to
   the `landed = armed - TCR0` accounting, `aligned` stays 0, and the
   master correctly skips every sprite packet — stale beats displaced.
   WHAT THIS POINTS AT: the whole scheme rests on partial DREQ landings
   being readable from TCR0 mid-transfer. That is the same property the
   OPTIONAL PALETTE payload already relies on (arm 852, push 596), and
   LOOP 13 already recorded that "MAME never showed it: dreq_inc reads 1
   there" — so MAME's DREQ model may simply not update TCR0 until the
   armed count completes, which would make this UNTESTABLE IN MAME and
   possibly fine on ares. That is exactly the situation the DREQ trap
   forbids shipping into, so it is reverted, not parked half-applied.
   NEXT ATTEMPT, in order of preference:
     a. Settle the mechanism first: on ares, does a SHORT push report a
        partial TCR0? One instrumented probe build answers it and
        decides everything else. Do not write more protocol until it is
        answered.
     b. If partial TCR0 is not readable: make the transfer COMPLETE by
        arming what the MD will push — the MD publishes the NEXT push's
        nrec through a COMM register, the master arms that. Exact-match
        arming means TE sets and nothing depends on partial reads.
     c. If neither: a fixed SMALLER cap (e.g. 32 records = 340 words,
        measured max live is 21) with a full-596 fallback when the list
        overflows, both lengths TE-complete.

   **ARES SETTLED IT: PARTIAL DREQ LANDINGS ARE READABLE (2026-08-17,
   BUILD 2425a7db, tools/drq_probe.py).** Over 3743 sprite windows with
   one-record-short pushes every 16th: **233 read landed==588, ZERO read
   0** (expected ~234). The optional-palette partial case fired too (11
   text packets at 596 against an 852 arm). MAME shows the opposite —
   62 of 62 short pushes invisible — so **MAME does not model partial
   DREQ landings and is the WRONG ORACLE for this whole class.** The
   standing rule earned its keep again: ares is hardware truth when they
   disagree.
   **`make SPRTRUNC=1` IMPLEMENTED** (MD + master in one commit, per the
   DREQ trap): MD scans for the terminator, publishes 84+8*nrec and
   pushes nrec*2 groups; master accepts that length family, keeps the
   magic-tail check at landed-2, copies only `landed-84` words, and no
   longer counts a validly-terminated short packet as incomplete.
   Measured (identical scripted play): **handler mean 83.8 -> 65.4, tail
   63.3 -> 46.5.** Region guard: SPRTRUNC alone _end 0x06018EE8 (280B
   spare).
   **MAME CANNOT GATE THIS BUILD.** There every truncated packet reads
   landed==0 and is skipped, so MAME shows FROZEN SPRITES and moved
   statics. That is the emulator artifact the probe just characterised,
   not a regression. Gate SPRTRUNC on Mike's ares pass; keep gating the
   SHIPPING build in MAME as always (statics stay title 2.44 dx=0 /
   eyehold 3.37 dx=0 EXACT, _end 0x06018BB0, zero sprbake symbols).
   **SPRTRUNC + SPRBAKE DO NOT FIT TOGETHER YET**: combined _end
   0x06019048, 72 bytes over. If only one can ship, truncation wins on
   measurement (~18 lines vs the bake's ~5). Buying the 72 bytes is a
   LOOP16-relocation-playbook job.

4. **Measure**: handler mean / deferrals / slave idle / bake hit
   rate (add a hit/miss meter, scrap 0x28FBC free) — then spend
   the freed budget, in order:
   a. cutscene deferrals -> 0 (band completion accelerates);
   b. the 30Hz-CUTSCENE cadence experiment (MD shim reads scene
      state 0xFFF031, skips the idle beat in cutscenes — halves
      Zeus scale stepping exactly where Mike sees it);
   c. the 60Hz MD-plane scroll experiment (feed hscroll/VSRAM
      every vint from live regs — TRUE racing-the-beam
      backgrounds on real silicon; tiny cost, biggest
      arcade-feel win per byte; watch coherence vs the
      single-snapshot composed layer — judged by eye).
5. Parked/open cosmetics for later passes: the tearing-pair class
   (frames 1317/1318 corpus), load-in speed (transport floor,
   CUT_BLANK covers), sky palette (judged in OUR palette per the
   bar — taste pass with Mike).

## INSTRUMENTS (tools/, all session-proof)

  - state_health.py: handler mean + split, WINSPAN, slave idle,
    drift, misaligned — one paste from any ares state.
  - SPRREUSE probe (`make SPRREUSE=1`, NEVER SHIP): per-cycle
    reuse at the k2 snapshot; ROWHASH overlay 0x28D00; counters
    0x28FAC. Extend for discovery dumping.
  - hmean.lua / winspan_check.lua / magic_smoke.lua / frame_snap /
    cut_snap / nt_dump+nt_audit+arc_dump / parity_run.sh.
  - Lua PC-sampling (state["PC"].value + objdump at the pinned
    address) — THE deadlock diagnostic; found the chain-echo wedge.

## TRAPS (paid for; do not re-learn)

  - Fixed map FULL. 0x39800 gap ends 0x399E8 (24B left); scraps:
    0x28FBC..0x28FFF (~68B), 0x28F4E-4F, 0x28F68-7F. Slot
    collisions are the era's tax — grep every define TWICE
    (mdp_s_used and the md_pkt stack-clobber both shipped bugs).
  - Master stack dips >=576B below 0x3F000 — nothing above 0x3ED80.
  - Region guard headroom ~0x1A8; the bake path pays its way
    (LOOP16 relocation playbook: fixed-gap moves + probe-only
    exclusions).
  - Probe roms freeze at their commit — check lineage before any
    handoff (the cutblank-rom void).
  - Flavor by cmp vs saved copies, never by BUILD hash; rebuilds
    are not bit-reproducible.
  - MDBGALL parity anchors are a phase lottery — judge diff
    ANATOMY (eye/logo/load classes), not %.
  - MAME cannot rank SH2 timing (~3x fast) and cannot reproduce
    ares FIFO loss; those verdicts need Mike's state.
  - The DREQ protocol: change push and apply in the same commit,
    whole and gated, or not at all.
  - Text below 2 chunks/cycle = documented regression. Master idle
    padding = documented regression (no slack). IDLETOKEN/CMDINT/
    PGROTOR/restore-narrow: dead, see LOOP15/16.

## GATES ON EVERY COMMIT (unchanged)

  - tools/parity_run.sh: shipping statics title 2.44 / eyehold
    3.37 dx=0 EXACT.
  - Bake pixel-identity check green (once step 2 exists).
  - grep ' _end$' rom/s16.lst < 0x06019000.
  - Shipping stamped normal; probe builds never to ares.
  - Mike's ares play pass — the bar is release-bar-flawless:
    look AND motion; ask how it FEELS.


## SPRTRUNC ARES VERDICT — LANDED (BUILD 0700e5f3, savestate 9)

**68K handler mean 106.9 -> 91.3 lines/vint. The game gets ~65% of the
MD 68K (~50% of the arcade), up from ~59%.** Tail 62.0 -> 46.5;
window/ack unchanged at 44.9, so the two halves are now EQUAL and the
tail is no longer the dominant term.
Protocol health is clean, which is the part that mattered:
`dreq_incomplete 0.0%`, `misaligned 1`, `skips 0.3%`, `V-gate rejects
0.0%`, cadence 3.00, slave idle 13,814/cycle. No sign of a mis-sized
packet anywhere.
Mike's feel: "plays faster, frame drops, screen tearing present but not
severe, sprite blitter present."
RIG NOTE: MAME predicted tail 46.5 and ares measured 46.5 — EXACT. The
MD-side counters are modelled honestly, so tools/health_mame.lua can
carry cheap A/Bs of the handler split from here. It is only OUR
machine's internals (SH-2 timing, FIFO loss, partial DREQ landings)
that MAME cannot rank.
FIRST FALSE START, worth remembering: the first attempt was measured on
a rom built from a LINE-WRAPPED make command, so only `MDBGALL=1
BQCHUNK=1` reached make. The build stamp said `normal` instead of
`SPRTRUNC` and the counters screamed it (misaligned 118 = NT_WRAP off,
slave idle 149/cycle = WIN2 off, window/ack 84.3 = the 3-window tax
back) — and the tearing it showed was the pre-WIN2 band-tear class, not
anything to do with truncation. ALWAYS check
`python3 tools/build_id.py show rom/s16.32x` before believing a state.

## CUT30 ARES VERDICT — 30Hz REACHED, BUT THE TRADE IS BAD (BUILD
## 696d9c0e, savestate 9)

Cadence 3.00 -> **2.04 vints/cycle: the 30Hz display works.** Everything
else got worse, and by more than MAME predicted:
  - **68K handler mean 91.3 -> 137.8.** The game drops from ~65% to
    **~47%** of the MD 68K (~37% of the arcade) — worse than the v10
    baseline we started the day at.
  - window/ack 44.9 -> **69.0** (the master can no longer ack in time),
    flip/blit skips 0.3% -> **3.8%**, V-gate rejects 0.0% -> 1.8%,
    slave idle 13,814 -> 11,473.
MAME said the handler mean would be 92.7 and skips 0. ares says 137.8
and 3.8%. MAME under-modelled the MASTER's half exactly as expected
(SH-2 ~3x fast there) — the master, not the 68K, is what cannot sustain
a 2-vint cycle.
**VERDICT: do not ship CUT30 as an unconditional cadence.** The display
rate is not the binding constraint; the master's compose+blit is. Buying
30Hz by taking a third of the game's CPU is the wrong direction — and
that is the falsifier this build was built to fire, so it did its job.
It stays as a flag for the cutscene-gated version (where the game logic
is idle and the CPU is affordable), which is the original 4b intent.

## THE FRAME PIPE IS MOSTLY EMPTY — MD_PAYOFF RE-MEASURED AFTER MDBGALL

Mike's architectural challenge ("we have a fundamentally different
architecture, stop assuming we must race a vint beam") sent me back to
the MD_PAYOFF probe, whose own comment flagged its result as stale:
LOOP 9 measured 13-17% of the blit skippable **with the BG still in the
framebuffer dirtying every row**. That is no longer the configuration.

Re-measured under the canonical MDBGALL + SPRTRUNC bundle (MAME,
scripted gameplay, 219,802 rows):
  - **WHOLE ROWS entirely transparent: 71.4%**
  - 32px groups fully transparent: 77.9%
  - transparent area (longs): 83.4%
The blit ships 320x224 bytes every frame and **~7 rows in 10 carry
nothing at all.** This is a CONTENT measurement, not a timing one, so
MAME is a fair source for it (we already proved our composed frames are
pixel-identical there) — but confirm the ratio on ares, where the scene
mix differs.

**ALSO FIXED:** the probe computed a row-level `any` and threw it away
(`(void)any;`), so the row number — the one that decides whether the
"ship the whole frame every cycle" premise still holds — had never been
counted. It now lands in the scrap block and `state_health.py` prints
all three ratios.

**ARES CORRECTS THE RATIO (BUILD 1554ae7b, 221,322 rows of real
gameplay): WHOLE ROWS 31.9%, 32px groups 62.7%, area 79.4%.**
MAME's 71.4% rows came from my scripted play (thin scenes); Mike's real
gameplay has the HUD, the cat-1 grass strip and more actors, so far
fewer rows are ENTIRELY empty. Trust 31.9%.
The shape of the answer changes with it, and this is the useful part:
**area is 79% empty but only 32% of ROWS are, so the transparency is
SCATTERED.** Row granularity therefore buys ~32% of the blit; GROUP
(32px) granularity buys ~63% — nearly double, for one test per 8 longs
instead of one per row. That is the granularity to build at.
Note the tension with LOOP 9's finding that per-LONG branching costs
more than the store: a group is 8 longs, so this is a different bet, not
a re-run of that one. Budget it as ~10 tests per row against ~45,000
bytes of write traffic saved per frame.

**THE CATCH, and it is the whole design problem: WE PAGE-FLIP.** One
bank stages while the other displays, so a skipped row does not keep
last frame's pixels — it keeps the pixels from TWO frames ago, in that
bank. Skipping is only safe when the TARGET BANK's copy of that row is
already transparent. So the design is a per-bank, per-row transparency
bit (224 bits x 2 banks = nothing), set by COMPOSE as it writes rather
than by re-scanning: MD_PAYOFF costs a full extra read pass precisely
because it scans, and scanning would eat the win it measures.
Sketch: compose marks a GROUP dirty when it writes a pixel; blit ships a
group iff (group dirty) OR (target bank's group not already clear);
clearing a group that just went empty is itself a write, so it must be
tracked, not assumed. State needed: 224 rows x 10 groups x 2 banks =
4,480 bits = 560 bytes — which does NOT fit the fixed map's remaining
scraps, but the 0x3A000 block ("missq — 2.3KB nobody owns", LOOP17
traps) has room.
NOTE the neighbouring finding LOOP 9 recorded: per-LONG branching costs
more than the store, so fine-grained skipping loses. ROW granularity is
a different proposition — one test, 320 bytes skipped.

## BLITSKIP ATTEMPT 1 — CORRECT IDEA, WRONG BANK IDENTITY. REVERTED.

Built the group-level skip: in blit_half, OR the 8 longs of each 32px
group (free — the blit already LOADS them to store them) and skip the 8
stores when the group is zero AND this bank's FBCLEAR bit says it is
already zero there. Per-bank per-row 10-bit mask at 0x3A000, 896B.
**It corrupts.** Against the same build without it, 4 sampled gameplay
frames differ by 6.5k-9.9k pixels: Zeus's head vanished and a solid
yellow block persisted where the statue was — i.e. groups skipped that
needed writing, and stale content left standing. Not black: STALE.
CAUSE (hypothesis, not yet proven): the bank index is sniffed per call
as `MARS_VDP_FBCTL & MARS_VDP_FS`. But a window does a first-bank blit,
the FLIP, then a second-bank blit, and BOTH CPUs call blit_half around
an edge the MASTER owns. The slave can read FS on the wrong side of that
edge and consult the wrong half of the mask, which both skips needed
writes and leaves stale groups marked clear.
THE FIX IS TO STOP SNIFFING: the flip is already sequenced through
SYNC[2]/SYNC[3] (slave step 1 = first-bank blit done, 2 = second-bank;
master publishes "flip latched, second bank writable"). The bank each
blit_half call targets is therefore KNOWN by that protocol — pass it in
or read it from the same state, and the race disappears.
**MAME CANNOT MEASURE THE WIN EITHER WAY** — LOOP 9: "80% of the blit is
a bus-stall floor... that is the whole MAME figure", so MAME models only
the ~2.7us instruction issue. It showed hmean 65.4 -> 66.1, i.e. the
cost of the compare and none of the saving. MAME is still useful here
for CORRECTNESS (build it WITHOUT SPRTRUNC so MAME renders faithfully,
then diff frames against the same build without BLITSKIP — that is how
this bug was caught in one run). The SPEED verdict needs ares.

## THIS LOOP IS CLOSED — CONTINUE IN docs/log/LOOP18.md

docs/log/LOOP18.md is the handoff kickoff: current state, the blit measurements
that define the next arc, the job order (BLITSKIP attempt 2 first, then
dirty-rect compose), the instruments, and every trap paid for today.
What follows below is the original LOOP17 plan, kept for its detail.

## (LOOP17'S OWN NEXT-STEPS, superseded by docs/log/LOOP18.md)

1. **DONE — SPRTRUNC verified on ares (91.3). Mike's call whether it
   folds into the canonical bundle**, which is a gate-rebaseline
   decision like the NT_WRAP skip-rate bars were: it cannot be gated in
   MAME (frozen sprites there by construction), so the shipping rom
   stays the MAME-gated one.
2. **Then the 30Hz CUTSCENE cadence** (item 4b below): the MD shim
   reads scene state 0xFFF031 and skips the idle beat in cutscenes,
   halving the Zeus scale stepping exactly where Mike sees it.
3. **Then the 60Hz MD-plane scroll** (4c): feed hscroll/VSRAM every
   vint from live regs. Biggest arcade-feel win per byte; watch
   coherence against the single-snapshot composed layer, judged by eye.

**OPEN CONSTRAINT:** `SPRTRUNC + SPRBAKE` together are 72 bytes over
the region guard (_end 0x06019048). Alone: SPRTRUNC 0x06018EE8,
SPRBAKE 0x06019000 exactly. If only one ships, truncation wins on
measurement (~18 lines vs ~5). Buying those 72 bytes is a LOOP16
relocation-playbook job.

**TOOLKIT.md IS THE DELIVERABLE** (Mike, emphatically, 2026-08-17):
update it AS work lands, not at the end of a loop. Today's kit yield
is already in it — the sprite-bake section and three transport
invariants.
