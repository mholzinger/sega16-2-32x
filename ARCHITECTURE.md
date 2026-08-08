# How a 32X draws a frame — the library, this port, and the pivot

This document exists because the port kept getting optimised against
the wrong bottleneck. It fixes the frame of reference: what the
hardware can actually do, what the shipped 32X games actually did, what
this port does instead, and what it should do.

Everything marked **MEASURED** has a number behind it in this repo.
Everything marked **ASSUMED** does not, and is a place someone can be
wrong. Hardware claims cite `srcref/jtcores` Verilog by file and line.

---

## 1. The one number

We did not replace a 10 MHz 68000 with 23 MHz SH-2s. We replaced
**dedicated raster hardware with software.** System 16 has a tilemap
generator and a sprite chip that draw the screen free, in hardware,
every frame. The 32X has a framebuffer, and every pixel is a CPU store.

| | |
|---|---|
| 32X framebuffer write bandwidth | **6.76 MB/s** (MEASURED) |
| One pass over a 320x224 8bpp screen | 71,680 bytes |
| That pass at 60 Hz | 4.30 MB/s |
| **Budget** | **~1.6 screen passes per frame at 60 Hz** |

The blit alone is one full pass. That leaves 0.6 passes for BG, FG,
priority tiles, text and sprites composited with priority — not enough
for one layer, let alone five. 

**The 20 Hz cadence is not a design failure. It is this number.** Any
proposal that does not change the number of screen passes cannot change
the framerate. This is the test to apply to every idea before costing
it out.

---

## 2. What the commercial library actually did — MEASURED

**All fourteen titles surveyed.** `tools/vdp_survey.lua` taps the
68000's writes to the VDP ports (`$C00000`/`$C00004`) and to the
adapter control register (`$A15100`), emulator-side, and counts them
per 300-frame window. Run it as:

    SV_OUT=out.txt SV_FRAMES=1800 mame 32x -cart "<rom>" -rompath ./mame \
      -skip_gameinfo -video none -sound none -nothrottle \
      -autoboot_script tools/vdp_survey.lua

| title | frames | VDP data/frame | DMA/frame | FM writes |
|---|---:|---:|---:|---:|
| NBA Jam TE | 3000 | 284.2 | 0.07 | 5 |
| WWF Raw | 3000 | 97.8 | 0.05 | 5 |
| Star Wars Arcade | 120 | 72.6 | 1.62 | 5 |
| MK II Arcade Edition | 3000 | 57.4 | 0.91 | 5 |
| After Burner Complete | 3000 | 47.9 | 0.05 | 5 |
| Kolibri | 120 | 34.8 | 0.00 | 5 |
| WWF WrestleMania | 3000 | 30.4 | 1.11 | 9 |
| Space Harrier | 3000 | 22.2 | 0.00 | 5 |
| T-Mek | 600 | 18.6 | 0.09 | 5 |
| Virtua Racing Deluxe | 3000 | 14.0 | 0.42 | **1717** |
| Virtua Fighter | 3000 | 10.2 | 0.28 | **294** |
| 36 Great Holes | 3000 | 6.5 | 0.62 | 5 |
| **this port** | 3000 | **0.1** | **0.00** | **5987** |

Chaotix and MK2 (original) have no row: at 3000 frames both exceed the
700 s harness timeout, and the instrument only writes its output at
exit, so a timeout yields nothing rather than a partial sample. Their
pre-correction figures were 46.2 and 79.3 data/frame, FM 263 and 5.
Both appear in the plane table in section 7, which is the stronger
measurement anyway.

Our port's FM count rose 355 -> 5987 between rounds because the fixed
walker actually reaches gameplay: ~2 writes/frame is FM raised and
lowered once per vint, which is the per-frame arbitration described
below. The earlier 355 was a game that had barely started.

### There are two independent axes, not three architectures

An earlier draft of this document folded these into a single
"Model A / B / C" spectrum. **That was wrong, and the survey is what
disproved it.** The two questions are orthogonal:

**Axis 1 — who draws the tile layers?** Every commercial title feeds
the MD VDP continuously **in the state the menu walker reached**, and
this port never does. But feeding is not drawing, and for most titles
that state was attract mode — see the correction below, where MK2 turns
out to feed the VDP heavily in attract and not use it at all in a
fight. **Only Chaotix and MK2 have gameplay-verified plane data.** Our 367 writes all
land in the first 300 frames: the VDP is configured once at boot and
never touched again. Every other title's tail windows show sustained
traffic.

**Axis 2 — who arbitrates the framebuffer?** Here the library really
does split, and cleanly. FM writes are **5** — a boot-time setup and
nothing more — for nine of the fourteen. Four titles toggle it per
frame: Chaotix (263), Virtua Fighter (654), Virtua Racing Deluxe (917),
and us (355). This independently confirms the LOOP 11 claim that
Chaotix, VRDX and VF are the three titles keeping a busy 68000
alongside SH-2 rendering.

### The finding that matters — CORRECTED, twice, read this carefully

A previous revision of this section said: "we are not doing what MK2
does; MK2 pushes 79 VDP writes per frame and never stopped using the
Mega Drive's raster hardware; using the MD VDP is universal practice,
fourteen of fourteen; we are the anomaly." **That was measured on
ATTRACT MODE and it is wrong.**

Replaying a recorded MK2 session (`-playback`) into a real fight and
reading the name tables at that moment:

    Plane A empty | Plane B empty | sprites 0 | window empty
    vramNZ = 20512   <- the shadow HOLDS data; it is not blind

**During an MK2 fight the MD VDP contributes nothing to the picture.**
Every pixel is the 32X framebuffer. The 20512 words of VRAM patterns
are residue from the attract screens, which really do drive the VDP.
The per-frame write traffic in the table above is, for MK2, an
attract-mode number.

So this port did NOT misread MK2. It copied it accurately. The two
verified gameplay architectures are:

| | MK2 (verified, fight) | Chaotix (verified, level) |
|---|---|---|
| tile planes | empty | Plane B 80% full, HW-scrolled |
| MD sprites | 0 | in use |
| the picture | 100% 32X framebuffer | MD background + 32X on top |

### GAMEPLAY-VERIFIED, four titles, via recorded input replay

Mike recorded real play sessions; `-playback` replays them
deterministically from frame 0 so the VRAM shadow sees every write.
Sampled inside the recorded window, with a screenshot at the sample
point confirming the game state:

| title | Plane A | Plane B | MD spr | window | churn / scroll | MD's role in the picture |
|---|---|---|---|---|---|---|
| Knuckles' Chaotix | 0.12 | **0.80** | yes | – | A churns; **B scrolls via VSRAM** | live scrolling background |
| After Burner | 0.57 | 0.40 | 19 | 0.62 | churn 1–22, no scroll | HUD + static content |
| Space Harrier | 0.50 | empty | 0 | 0 | **churn 0, no scroll** | inert during play |
| Mortal Kombat II | empty | empty | 0 | 0 | – | inert during play |
| **this port** | empty | empty | 0 | 0 | – | inert, always |

**Of four gameplay-verified commercial titles, exactly ONE puts a live
scrolling tile plane on the MD VDP — and it is the only one that shares
Altered Beast's shape.** Space Harrier and MK2 are inert during play.
After Burner uses the MD for HUD furniture. The three that don't use it
are a fighting game and two sprite-scaling 3D games: they have no tile
structure for the VDP to draw, so the framebuffer costs them nothing.

**CRITICAL CAVEAT ON `fill`: it measures what is IN the name table, not
what is ON SCREEN.** The 32X layer can cover the MD entirely, so
populated planes do not prove visible output — Space Harrier's Plane A
sits at 50% while never changing or scrolling, which is stale or hidden
content, not a drawn background. Only the combination *populated +
churning or scrolling* is positive evidence, which is why Chaotix is
the one clear yes. The `window` figure is also an overcount: it reads a
full 40x28 from the window base without applying the window position
registers.

**The honest case for the pivot is therefore narrower, and it is an
argument about GAME SHAPE, not about being an outlier.** MK2 is a
fighting game: two big digitised actors on a backdrop, no tile
structure worth the VDP's help, so putting everything in the
framebuffer costs it nothing. Altered Beast is a scrolling tilemap
game, and Chaotix — the one title in the library with the same shape —
puts its background on the VDP and scrolls it for free. We inherited
an architecture that fits MK2's problem, not ours.

That is a much weaker headline than "fourteen of fourteen" and a better
argument, because it survives contact with the data. But be clear about
the sample: **the evidence that our target architecture works on this
hardware is one title.** Chaotix proves it is possible and shows how.
It does not prove it is common, because it is not.

The Chaotix/VRDX/VF material below still matters, but for the *other*
axis: it is the reference for how a busy 68000 and per-frame FM
arbitration coexist, which is what we will need once the 68000 is
feeding the VDP as well as running the game.

### What high traffic does and does not prove

**It proves the MD VDP is being fed every frame. It does not prove
what it is drawing.** A title could be updating only a HUD, a score
line, or a border. Space Harrier's dead-flat 1200 writes per 300-frame
window (exactly 4/frame) reads much more like a score counter than a
tile plane. Per-title "what is on the MD layer" is still ASSUMED and
would need a different instrument — see §7.

### The three arbitration models, for the second axis

### Model A — "set-once FM": the SH-2 keeps the framebuffer

*Mortal Kombat II, WWF Raw, NBA Jam TE, Space Harrier, After Burner,
Star Wars Arcade, T-Mek, Kolibri, 36 Great Holes* — nine of fourteen,
FM writes = 5 (MEASURED).

FM is set once at startup and never handed back, so the 68000 never
waits on the SH-2 for framebuffer access and there is no window to
arbitrate. The 68000 still drives the MD VDP the whole time — it simply
does it through the VDP ports, which FM does not gate.

**This is the arbitration model this port copied. We also copied
something MK2 never did: we stopped using the VDP.**

### Model B — "Chaotix": MD VDP draws the tile planes, 32X adds sprites and colour

*Knuckles' Chaotix.* (MEASURED, disassembled — see LOOP11.md.)

The Mega Drive VDP draws the scrolling tile planes in hardware, with
hardware scrolling, at 60 Hz, for zero CPU. The 32X composites sprites
and colour on top. The 68000 runs a full Sonic engine, drives MD
sprite/scroll DMA, *and* arbitrates the framebuffer, every frame.

Five decisions make a busy 68000 compatible with SH-2 rendering:

1. **Bulk data never crosses the framebuffer.** The per-frame display
   list goes 68000 → DREQ FIFO (`$A15112`) → SH-2 DMAC ch0 → SDRAM. The
   SH-2's CMD handler copies nothing: it programs `SAR0=0x20004012`,
   `DAR0=SDRAM`, `TCR0=len`, `CHCR0=0x44E1`, `DMAOR=1`, and returns. The
   DMA drains in the background at zero CPU cost.
2. **Palette is a change queue, not a blit.** The 68000 accumulates
   `(CRAM offset, colour)` pairs in MD RAM at `$FFFFD860` during the
   frame and drains only those under FM=0 — 40–60 cycles per *changed
   colour*, versus ~31 scanlines for a full CRAM write.
3. **`COMM0==0` is an idle token, not a request/grant.** The SH-2's idle
   loop zeroes COMM0 to publish "I am parked in a loop touching only
   COMM0". The 68000 tests it and takes FM **unilaterally**. No round
   trip.
4. **The 68000 polls and skips; it does not wait.** `tst.w $A15120 /
   beq take-it / rts`. A missed palette update costs one frame of stale
   colours; a blocking wait costs a frame of physics.
5. **The SH-2 is allowed to stall.** Its V-int handler will block if it
   collides with a 68000 FM window — fine, *because the windows are
   microseconds*.

Its FM window is a few hundred 68000 cycles.

### Model C — "VRDX": the 68000 owns the frame flip

*Virtua Racing Deluxe, Virtua Fighter.*

As Model B, but the 68000 owns FBCTL and the frame flip outright while
the SH-2s render 3D. VRDX pushes 0x500 words/frame through the same
DREQ FIFO, waiting on the CMD-accepted bit (`btst #0,$A15103`) before
pushing and polling FIFO-full (`$A15107` bit 7) between every 4-word
group.

### The finding that matters

**A busy 68000 is not what forces a long handoff.** Three shipped
titles keep one. What forces it is *how much data crosses the boundary
and whether either side blocks* — and Model B/C solved both without
giving up the MD VDP.

---

## 3. Where this port sits

We took Model A's arbitration *and* threw away the VDP, which no
shipped title does. Concretely:

- Both scroll planes and the text layer are **software-rendered** into
  a staging buffer, then blitted to the framebuffer.
- Sprites are composited in software with a ten-deep priority chain.
- The MD VDP draws **nothing** — 0.2 writes/frame, all at boot
  (MEASURED, §2). Unique in the library.
- We toggle FM 355 times, which is Chaotix/VRDX/VF behaviour, not
  MK2's — so we pay the arbitration cost of the busy-68000 model while
  getting none of its benefit, because the 68000 is not drawing
  anything with the time.
- A full frame ships every 3 vints — **20 Hz**.

### What the ares telemetry says (MEASURED, `tools/state_health.py`)

Two savestates, baseline build `95d17bcf` and probe build `0faba162`:

| | baseline | probe |
|---|---|---|
| blit windows sampled | 5603 | 1200 |
| **mean FM window span** | **35.6 lines** | **35.6 lines** |
| — of which blit | 28.4 | 28.5 |
| — of which CRAM | 3.5 | 3.3 |
| worst handler, total | 157 of 262 | 156 of 262 |
| — window/ack portion | 110 | 112 |
| vints per display cycle | 3.03 | 3.22 |

Both of the above are SHORT samples — see the correction below.

**THE MEAN AND THE TAIL ANSWER DIFFERENTLY, AND BOTH MATTER.** Those
two states are 403 and 1889 cycles. A 3709-cycle baseline session
(`rom/s16.bs1`) says:

| | mean window | worst window/ack | margin |
|---|---|---|---|
| short states (403 / 1889 cy) | 35.6 lines | 110–112 | 105 |
| long session (3709 cy) | 38.2 lines | **210** | **8** |

- **The MEAN is blit-bound.** 28.8 of 38.2 lines is the blit moving
  71,680 bytes. No handshake change can touch it, and the only thing
  that shrinks it is drawing fewer pixels — §4.
- **The TAIL is ack-bound.** 210 lines is 5.5x the mean and the excess
  is not blit. It leaves 8 lines of margin in a 262-line frame, and it
  is what a player feels as a hitch.

So the "~200-line window" that older docs cite was **never stale** — a
short sample simply cannot contain it. I retracted it on 403- and
1889-cycle evidence and was wrong to. The same long state reproduces
the rest of the documented baseline that the short ones contradicted:
blit skips 26.6% (ref 31.5%), dreq_incomplete 21.0% (ref 21.4%),
restore past vblank 0.7% (ref 0.8%) — where the short samples read
0.7% dreq_incomplete and looked like a huge improvement.

**Treat any ares comparison made on under ~3000 cycles as unreliable.**
Maxima under-sample badly, and the maximum is the interesting number
here. Judge against the range, not one run.

The blit is 28.5 lines because it moves 71,680 bytes. The only thing
that shrinks it is **drawing fewer pixels.**

---

## 4. The pivot: move the tile planes to the MD VDP

Adopt Model B. The Mega Drive shipped an official Altered Beast; the MD
alone can draw these backgrounds. The 32X should be *adding* to that,
not redrawing everything.

### What moves — verified against the RTL

The two scroll layers, and only those. Every structural feature maps:

| System 16 | Mega Drive VDP | verdict |
|---|---|---|
| Two scroll planes | Plane A / Plane B | match |
| 8x8 tiles, per-tile palette select | same | match |
| Per-tile priority bit | same | match |
| Whole-screen H/V scroll | same | match |
| Row scroll: per-8-line (`jts16_mmr.v:67-68`) | per-line | MD is better |
| Column scroll: per-16px (`jts16_scr.v`) | per-16px | exact match |

**MEASURED: Altered Beast never enables row or column scroll in 700
samples**, so round 1 needs neither.

**Pattern residency fits.** MEASURED peak 599 scroll + 96 text distinct
patterns ≈ 22 KB at 4bpp, plus 16 KB of name tables — inside 64 KB VRAM.

### What cannot move — sprites, categorically

Three independent hard stops, all expressibility rather than fidelity:

- **Hardware zoom** — shrink to ~0.5 in 32 steps (`jts16_obj_draw.v:70`).
  The werewolf transformation rides on this.
- **Ragged variable-width strips**, up to full-screen size
  (`jts16_obj_draw.v:107`). The MD sprite format cannot express them.
- **The MD's ceiling** — 20 sprites / 320 pixels per line.

### The real blocker is priority, not colour

The 32X composites against MD video across exactly **one** boundary:
per-pixel transparency plus a single global 32X-vs-MD selector. System
16 interleaves them **ten deep** (`jts16_prio.v:84-95`):

    T1 > S3 > T0 > F1 > S2 > F0 > B1 > S1 > B0 > S0

Sprites in the 32X framebuffer with both planes on the MD means every
sprite is in front of everything or behind everything. The player would
stop being occluded by foreground scenery.

**The escape, and it is affordable — MEASURED.** Keep the FG layer's
priority (cat-1) tiles in the 32X framebuffer, painted *over* the
sprites, and let the MD draw FG cat-0 plus the whole BG. In demo
gameplay the FG carries 307–340 priority tiles of 1189 visible (~26% of
the screen); the BG carries ~4 on average.

    residual 32X pass  ≈ 0.26 of a screen   against a 1.6-pass budget
    leaving            ≈ 1.3 passes for sprites

That is the whole point of the exercise, and it fits.

### Colour is a grind, not a wall

The naive framing — "128 colour sets against 4 MD palettes, 32x short"
— is wrong. **MEASURED peak demand is 21 distinct tile colour sets on
screen at once** (12 FG + 16 BG, union 21) against an MD capacity of 8.

Eight, not four, because S16 sets are 8 colours and MD palettes are 16:
one MD palette holds one FG/text set (pens 0–7, colour 0 transparent)
**and** one BG set (pens 8–15, since **BG colour 0 is opaque** —
`jts16_prio.v:87`, a real semantic difference).

**2.6x, not 32x.** But it is a different 21 every scene, so it needs a
per-scene 21→8 merge, not a static allocation. Plus 5bpp→3bpp per
channel on the moved layers: banding in the sky and cave gradients. The
32X framebuffer is 5-5-5, identical to S16, so the loss is confined to
whatever moves.

### Verdict

**The plan works, but the FG layer splits across both chips rather than
moving wholesale.** If the split proves too complex, priority — not
palette — is what forces the retreat.

And it generalises: every System 16 title has this shape. Solve it once,
and it is the kit.

---

## 5. Two port beliefs confirmed against the RTL

Both were load-bearing in `compose` and both were previously unverified:

- **The priority model** (BG cat0=1, BG cat1=2, FG cat0=2, FG cat1=4,
  text cat0=4, text cat1=8; sprite draws iff `1<<pp > level`) is exactly
  equivalent to `jts16_prio.v:84-95`. Nuance: a sprite blocked by a
  priority tile at one layer can still win at a higher slot, and the
  scalar-max formulation reproduces that correctly.
- **Shadow is `shadow & ~pal[15]`** — `jts16_colmix.v:88`, line for
  line. MEASURED: Altered Beast sets bit 15 on 89–119 of 1024 tile
  entries at any moment, so it is not a dead feature.

## 6. Open contradictions — flagged, not guessed

- **Is H scroll suppressed while column scroll is active?** The hardware
  notes say yes; jtcores says no (`jts16_scr.v:93`). Unresolved.
  Currently moot — Altered Beast enables neither.
- **Shadow arithmetic.** jtcores does x0.75 with no highlight path
  (`jts16_colmix.v:80-89`); the hardware notes describe 1/2 with a
  highlight. We match jtcores/MAME. Revisit only if a real PCB
  disagrees.

## 7. Gaps in this document

Honest inventory of what is *not* backed by measurement here:

- **What each title's MD layer DRAWS is PARTLY answered — and the
  answer is not uniform.** `tools/vdp_planes.lua` shadows VRAM and VSRAM
  from the write stream (data port *and* 68K→VRAM DMA, which the data
  port never sees) and reads the Plane A/B name tables directly. Last
  sample of a 3000-frame run, visible 40x28 window:

  | title | Plane A | Plane B | verdict |
  |---|---|---|---|
  | Space Harrier | fill 0.90, churn 1056 | fill 0.85, churn 972 | both planes, heavily redrawn |
  | Knuckles' Chaotix | fill 0.12, churn 53 | **fill 0.80, churn 0, scrolling** | HW-scrolled background + busy FG |
  | After Burner | fill 0.56, churn 0 | fill 0.40, churn 0 | both planes populated |
  | Virtua Racing Dx | fill 1.00, dist 25, churn 0 | empty | one full static plane |
  | Mortal Kombat II | **empty** | **empty** | unresolved — see below |
  | NBA Jam TE | **empty** | **empty** | unresolved — see below |
  | **this port** | **empty** | **empty** | draws nothing, confirmed |

  **CHURN=0 WITH MOVING SCROLL IS THE HARDWARE-SCROLLING SIGNATURE, not
  a null result.** Chaotix's Plane B is 80% full, never rewritten, and
  its VSRAM moves (993 → 703 → 749 → 753). One register write scrolls a
  whole plane; our software renderer must touch every pixel to do the
  same, which is the 28.5 lines of blit in §3. Read `churn=0` as
  "static" and the conclusion inverts — it nearly did, twice.

  **STILL UNRESOLVED, and it is the pivot's load-bearing claim.** MK2
  and NBA Jam push heavy VDP traffic (57 and 284 writes/frame) with
  *empty name tables*, so that traffic is going to sprites, patterns or
  CRAM rather than tile planes. Either they genuinely do not use MD tile
  planes, or the menu walker never reached gameplay. **A screenshot at
  the sample point distinguishes these and has not been taken.** Until
  it is, "every title draws tile planes with the VDP" is NOT
  established; only "every title feeds the VDP" is. The weaker claim
  still makes this port the outlier, but the pivot's case wants the
  stronger one.

- **The menu walker pauses games, and it silently corrupted a whole
  measurement round.** Pulsing Start on a cadence walks past the menus
  and then hits pause; `churn` reads 0 and the scroll freezes, which
  looks exactly like a static screen. A screenshot was the only thing
  that caught it. Both instruments now use Start for the first 900
  frames only. **Any measurement in commit `60e456b` was taken with the
  bug present** — the corrected per-frame rates in §2 replace them.
  Chaotix and MK2 have no corrected survey row: at 3000 frames they
  exceed the 700 s timeout, and `vdp_survey.lua` only writes its output
  at the end, so a timeout yields no data rather than partial data.
- **Two titles are short-run.** Kolibri and Star Wars Arcade hang in
  MAME's 32x driver somewhere between frame 120 and 600 — with inputs
  disabled too, so it is the driver, not the attract-buster. Their
  numbers are 120-frame samples with no tail window and should be
  treated as boot-phase traffic, not gameplay. T-Mek is a 600-frame
  sample. WWF Raw's tail reads `41917, 0, 0` — it stops feeding the VDP
  partway, which is more likely a stuck attract state than a real idle.
- **The destination split is unreliable.** The instrument's
  VRAM/CRAM/VSRAM classification depends on tracking the two-word
  control sequence, and auto-increment runs leave most writes
  unattributed (our own port reads `other=360` of 367). Only the
  totals, DMA counts and FM counts in §2 are trustworthy. Do not cite
  the per-destination numbers without fixing the decoder.
- **Model C's FBCTL ownership is read from the flip path only** — VRDX
  and VF's 917/654 FM writes confirm per-frame arbitration, not
  specifically who owns the flip.
- **The tile-RAM read-back question is unanswered** and gates the DREQ
  staging step. Which tile-RAM addresses does the game read back, and
  how many bytes do they span? `tools/patch_game.py` already classifies
  the game's accesses — extending it to report READS is the cheap way
  in. If reads touch only a small subset, the rest can be write-only and
  streamed.
- **The DREQ truncation** (short lands, cause unknown) is untraced.
  Volume is not the problem: VRDX pushes 0x500 words/frame through the
  same FIFO and ours is ~594 words. Chaotix waits on the CMD-accepted
  bit before pushing and polls FIFO-full between 4-word groups; our push
  loop does neither.

## 8. Measuring the library — what works and what does not

Notes for whoever runs these instruments next, so the dead ends are
only walked once.

- **MAME does not expose MD VRAM.** `:gen_vdp` advertises a `videoram`
  address space, and it reads back all zeros — including on a frame
  where the write-stream shadow shows Plane B 80% full. It is a
  declared space, not the live VRAM array. **`tools/vdp_planes.lua`
  must shadow VRAM from the write stream**, which is why it also has to
  re-implement 68K→VRAM DMA: the bulk of a Genesis game's VRAM traffic
  never touches the data port. Do not "simplify" it to direct reads.
- **Consequence: savestates are not usable for plane measurement.** The
  shadow needs the write history from boot, and a loaded state has
  none. Counters in SDRAM/MD RAM are fine to read from a state
  (`tools/state_health.py` does exactly that) — name tables are not.
- **The way to measure real gameplay is `-record` / `-playback`.** A
  human plays to the interesting state once and records the input file;
  the replay runs deterministically from frame 0, so the shadow sees
  every write, and the instrumentation can be swapped and re-run as
  often as needed against the identical session:

      # once, by hand — play through to the state that matters
      mame 32x -cart "<rom>" -rompath ./mame -record fight.inp
      # then, repeatably, with any instrument attached
      mame 32x -cart "<rom>" -rompath ./mame -playback fight.inp \
        -video none -sound none -nothrottle \
        -autoboot_script tools/vdp_planes.lua

  This is the right division of labour: navigating an unfamiliar game's
  menus is slow and error-prone to script blind (see the pause bug in
  §7, and MK2, whose attract sequence outlasts a 2500-frame run), while
  replaying and measuring is exactly what scripting is good at.
- **The generic menu walker is a fallback, not a method.** It reaches
  gameplay in some titles and parks in attract in others, and a low
  reading from it is ambiguous between "this title does not use tile
  planes" and "this title never left the menu". Screenshot before
  believing a null result.
