# docs/design/PIPELINE.md — every step a pixel takes, and who demands it

Written 2026-08-22 at Mike's direction: "I keep fighting you because
you keep preserving a made-up model that should not exist." This is
the full data path for every art type, one step per line, so the
made-up parts can be struck through by a human and not defended by
the machinery that grew them.

## How to read this

Every step carries two tags:

- **SILICON** — the 32X/MD hardware demands it. Cannot be struck.
- **BINARY** — running the unmodified arcade 68K code demands it.
  Can only be struck by changing what the patcher does, never by
  wishing the game wrote elsewhere.
- **MODEL** — an artifact of runtime-modelling the System 16. The
  made-up parts live here. Presumed guilty.
- **DEFENSE** — a copy/validation added to fix a measured failure.
  Struck automatically when the failure's cause upstream is struck.

And one of:

- **KIT** — game-agnostic, survives into the S16→32X toolkit.
- **GAME** — belongs in the per-game descriptor (patch map, inputs,
  bank scheme). If a step is neither KIT nor GAME it has no right
  to exist.

Strike protocol: Mike strikes lines. What survives is the P3 spec.

## The measured frame budget under R60 (2026-08-22 builds)

At 60Hz a frame is 262 lines. Where they go today (gameplay load,
deterministic ares runs):

    68K handler          67.8 lines   (push ~59 of it = packet words
                                       x ~85 cyc/adapter access)
    pre-flip capture     ~17 mean     (the flip-decline driver:
                                       17% of flips miss vblank)
    SH-2 compose+blit    fits the gap (slave echo timeouts 0/3441)
    game logic           what's left; arcade needs p99 72.9% of an
                                       arcade frame (M1)

So the two binding constraints are the 68K PUSH WORDS and the
PRE-FLIP CAPTURE. Compose is not currently binding — which is why
the LOOP19 "SPRBAKE is neutral" verdict was true then and is not a
verdict on the bake now: it was measured when blit/transport
dominated at 20Hz. The bake's 60Hz value is heavy-frame compose
headroom (the declines cluster on heavy frames) and — decisive for
the toolkit — deleting the runtime S16 interpreter entirely.

## SPRITE — record (16 bytes, ~20-40/frame)

| # | step | cost | tags | struck by |
|---|------|------|------|-----------|
| S1 | game writes record to WRAM 0xFF7000 (patch redirect) | free | BINARY, GAME | FBSPR: patch redirects sprite RAM into FB staging instead — the game's own write becomes the transport |
| S2 | 68K vint handler carries records into DREQ FIFO | ~8 w/rec at 85cyc/access — the handler's fat | MODEL | S1's strike deletes this whole leg |
| S3 | DMAC lands FIFO in SDRAM (SPR_LAND) | free (HW) | MODEL (consequence of S2) | same |
| S4 | harvest validates magic+length, copies to SPR_SNAP | ~1 line | DEFENSE | torn-transport risk dies with S2; ONE snapshot-at-raise remains (FM read rule: the SH-2 cannot read the old bank post-flip — measured 2026-08-22, 89% garbage) |
| S5 | compose interprets the 1988 format live: per row fetch nibbles from cart ROM, unpack 4bpp, remap pens via map[], write sbuf | the most expensive compose work; zoomed path worst (LOOP19) | MODEL, KIT | THE BAKE: build-time convert every frame to 8bpp linear rows; inner loop becomes long copies. Pen base stays runtime (one add) so one baked frame serves every palette pair — already proven by bake_sprites.py's byte-identical gate |
| S6 | blit sbuf → FB | fits; sequential | SILICON, KIT | — (FB random writes stall ~47us/row; compose-in-SDRAM + sequential blit is the CHEAP path. Measured, LOOP7d/18.) |
| S7 | flip at vint | free | SILICON, KIT | — |

Minimum reachable: game write (S1') → snapshot at raise (S4') →
baked-copy compose (S5') → blit → flip. Record moves twice, pixels
move twice. Nothing on this silicon does better.

## TILE — the 32X layer (staging word → screen)

| # | step | cost | tags | struck by |
|---|------|------|------|-----------|
| T1 | game writes tile word → FB staging | free | BINARY (game reads tile RAM back: 15KB collision probes) + MODEL (residency CHOICE: FB was picked so read-backs and transport were free) | WRAM mirror + write thunks: game reads back from WRAM for free; FMGATE writer-gate thunks already intercept every one of these writes and can mark dirty |
| T2 | pre-flip capture: dirty FB pages → TILEMAP_U truth | ~17 lines mean, THE decline driver | MODEL (consequence of T1) | struck with T1 — no FB residency, nothing to capture |
| T3 | post-flip restore: truth → new draw bank | post-flip lines | MODEL (consequence of T1 + double-buffer) | struck with T1 |
| T4 | compose: tilemap word → tile pixels from cart ROM, 4bpp unpack + remap per pixel → sbuf | per-pixel interpreter, same class as S5 | MODEL, KIT | tile bake: all tiles → 8bpp 64B blocks at build time; compose becomes copies |
| T5 | blit, flip | — | SILICON, KIT | — |

T1's strike must answer the SCENE-CUT STORM: a full tilemap rewrite
is ~7.5K words in one frame — that burst is WHY FB residency was
chosen. The mirror path needs a bulk-flush story for cuts (one
FB-scratch flush, or amortized force-full rows — the NT_WRAP healer
already exists). This is the one open design item on the strike.

## TILE — the MD plane lane (background offload)

| # | step | cost | tags | struck by |
|---|------|------|------|-----------|
| M1 | SH-2 builder converts S16 tile → MD 4bpp through mdp_quant per pixel | gap time | MODEL, KIT | partially bakeable: pixel→pen quantization depends on runtime CRAM allocation; bake the unpack, keep the quantize |
| M2 | builder stages packet (md_pkt/md_pktA in SDRAM) | free-ish | KIT | — (SH-2-built data must cross to the 68K somehow; FB is the only shared RAM) |
| M3 | publish packet → FB hole (0x11A00/0x1E800) | copy | KIT | — (same reason) |
| M4 | 68K consumes: VDP-DMA straight from the FB window | 3-8 lines, direct since 2026-08-22 | KIT | already at minimum — the WRAM staging leg was struck this session |
| M5 | MD VDP renders planes behind the 32X layer | free (HW) | SILICON, KIT | — |

## PALETTE

| # | step | cost | tags | struck by |
|---|------|------|------|-----------|
| P1 | game RMWs palette words → WRAM mirror (patch thunks mark dirty) | free | BINARY, KIT | — |
| P2 | dirty blocks ride the push (K*32 words) | words at 85cyc/access | MODEL | if sprites leave the push (S2 struck), palette is the push's main remaining tenant; it can ride FB staging the same way (game-write-is-transport needs a thunk target change) or stay — 2-3 blocks/frame typical is small |
| P3 | harvest applies to PAL_SH, repaints CRAM via allocator | ~1 line | KIT | — (dynamic CRAM allocation is the kit's palette engine) |

## TEXT / REGS / ROWSCROLL

Text: game writes FB text region (BINARY, FBTEXT), slave captures
928 longs in parallel with the truth drain (KIT, struck to the slave
this session), restore post-flip. Rides the same FB-residency logic
as tiles — if T1 is struck, text should follow the same mirror path.
Regs (20 words): ride the push, tiny, fine. Rowscroll (60 words):
ships only-when-changed since packet v2 (this session).

## THE BAKE — what exists, what a toolkit-grade bake changes

Exists today (LOOP 17, in-tree, accuracy-gated):
- tools/bake_sprites.py — offline decode to a segment/run format,
  with a build-failing byte-identical gate against a port of the
  live algorithm. 477 frames, 659KB, .incbin'd, SPR_BAKE fast path
  already in m_main.c. Verdict then: NEUTRAL — at the 20Hz profile.
- Limits of the existing bake: frames found by PLAY DISCOVERY (not
  exhaustive), native-zoom ungated non-shadow only.

Toolkit-grade bake (the P3 shape):
1. EXHAUSTIVE, from ROM structure: walk the sprite ROM's frame
   space, not a discovery log. A translation system cannot depend
   on someone playing the game first. Same converter runs on all
   S16B titles — it decodes the CHIP's format (one RTL in jtcores
   covers the whole board family), not the game's.
2. TILES TOO: all tiles → 8bpp blocks. Kills the T4 interpreter.
3. FORMAT: 8bpp linear, long-aligned, pen values final relative to
   a per-sprite base (one add at draw, proven by the existing gate).
4. ZOOM: the open design item — prescaled variants vs skip tables.
   Derive from jtcores (jts16b zoom RTL) + MAME, do not guess.
   Altered Beast zooms constantly (the scale-stepping defect is on
   Mike's must-fix list), so the zoom story is not optional.
5. Cart budget: bake roughly doubles ~700KB of art. 32X carts go to
   4MB. Non-issue.

## TARGET ARCHITECTURE AFTER THE STRIKES

    game 68K writes (patched):
      sprite records -> FB staging     (write IS the transport)
      tile/text words -> WRAM mirror   (read-backs free; thunks mark dirty)
      palette RMW    -> WRAM mirror    (as today)
    68K vint handler: gates, announce, consume MD packets (DMA),
      raise, post, push HEADER+DIRTY WORDS ONLY (regs + dirty tiles
      + pal blocks — no sprite records, target < 100 words typical)
    SH-2 at raise: snapshot records from FB staging (one copy),
      NO capture, NO restore, flip immediately after snapshot
      -> the 17-line pre-flip span and the 17% decline tail die
    SH-2 gap: compose = long copies from BAKED ROM art -> sbuf,
      blit halves, build MD packets, publish
    per-game descriptor: patch map, input map, bank scheme, dips

Every struck step subtracts from all nine future ports, not one.

## Session discipline for the strikes

Each strike lands as its own measured build on `rebuild60`:
falsifier run (deterministic ares + input playback), flip rate,
handler lines, harvest fails, FB render — before the next strike.
No compound changes. The graves (docs/design/REBUILD.md, docs/log/LOOP*.md NEGATIVE
RESULTS) are re-read before each one.

## STRIKE RESULTS (2026-08-22, measured same day as the map)

**S1 (sprite records via FB): STRUCK BACK — three variants, all dead.**
1. FBSPR=1 game-direct upload: ZERO sprites. The game's vint upload
   runs after the shim exits, inside the master's FM=1 span; ares
   discards MD FB writes at FM=1. The LOOP24 exclusion guard was
   protecting exactly this — the K2FREE liberation ended the "68K
   spins the whole FM span" invariant LOOP20's audit relied on.
2. Shim copies records→FB pre-post: flips 83%→70%. Any 68K work
   ahead of the post pushes the flip past the edge guard.
3. Copy after post: sprites would lag scroll by one frame. Fidelity
   kill; not built.
VERDICT: the DREQ push IS the minimal same-frame 68K→SH-2 record
channel on this hardware under this FM model. S2/S3/S4 stand as
DEFENSE, not MODEL. Do not retry without changing FM ownership.

**T1 (tiles to WRAM mirror): VERDICT FLIPPED by the write census.**
MAME range-watchpoint over tile staging (0x852000-0x85DFFF),
title + gameplay windows: **92% of frames have ZERO tile writes.**
The rest are storms — 4,400 mean / 11,040 max words in a single
frame (title backdrop blitter, segment loads). Two consequences:
- The FIFO could never carry the storms (11K words ≫ any budget),
  so the WRAM-mirror-plus-packet design is DEAD as a full
  replacement.
- The existing FB-residency + dirty-page capture is ALREADY the
  right storm transport, and its steady-state cost is ~zero (no
  dirt, no capture). The made-up part was believing capture cost
  every frame. T2/T3 reclassify as KIT (demand-driven bulk path).

**Where the frame losses actually live** (ares flip trace, 3600f
gameplay run): settled gameplay runs ~99% flip rate (8/6/1/0
misses per 250-frame bucket late in the run). Misses cluster in
intro/transition zones (runs of 3-6, over loads — largely
invisible) plus ~160 isolated singles across the run. The singles
are the visible-stutter budget; suspects are pal-storm push frames
and capture sticky-tails after storms. That — not a wholesale
transport rewrite — is the remaining 60Hz gap.

Surviving strikes, reprioritized:
1. S5/T4 — the runtime S16 decoder. Still the right kill for the
   TOOLKIT (translation system) and for heavy-frame headroom; not
   a flip-rate lever. Exhaustive bake from ROM + zoom story.
2. The isolated-singles tail: correlate misses with pal K and
   sticky-capture state; tune those paths (bounded work, not
   surgery).
3. 68K handler words (300 mean): records are irreducible (see S1
   verdict); pal/regs/rowscroll already conditional. Remaining
   diet is small; accept unless gameplay speed measures short.
