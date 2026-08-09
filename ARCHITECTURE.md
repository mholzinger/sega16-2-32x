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

**[RETRACTED — the instrument was broken. See section 12. The corrected
reading is that Space Harrier and MK2 also drive the MD VDP hard during
gameplay, and the "one of four" conclusion below is wrong.]**

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
  is what a player feels as a hitch. **It is still unsolved** — see the
  note below on why the obvious fix does not work here.

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

### Why Chaotix's arbitration does not port, even though its rendering might

Worth separating, because the library gives us two different lessons
and only one of them transfers.

The 68000-side protocol — idle token, poll-and-skip, never wait — was
implemented faithfully and **fails on this port**: cadence 3.03 -> 7.48
vints/cycle, 20 Hz down to roughly 8, "broken the second the game
starts" on Mike's play pass.

**The premise does not hold for us. Chaotix's SH-2 is PARKED most of
the time; ours is SATURATED.** An idle token is free exactly when "is
the master busy?" is usually NO — the 68000 takes FM unilaterally and
never blocks. Our master software-renders every layer, so at the poll
instant it is genuinely mid-strip: the MD either waits (spending 68000
time it does not have) or skips (dropping a whole blit phase). Measured
on ares, 20.5% of windows skipped.

MAME inverted the usual trap here. Its SH-2 is ~3x faster, so the
master looked parked — 10% skips — and the protocol looked viable.

**The transferable lesson from Chaotix is section 4: put the tile
planes on the VDP so the master stops being saturated.** Fix the
workload and the arbitration question changes shape; renegotiate the
arbitration while the master is still doing all the drawing and there
is nothing to win. Reduce what crosses the boundary (DREQ+DMAC staging,
a palette change queue) rather than renegotiating when it crosses.

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

## 9. The tile-RAM read-back question — ANSWERED. It does not block the pivot.

Section 7 listed this as the gate on step 2 (DREQ+DMAC tile staging):
*which tile-RAM addresses does the game read back, and how many bytes
do they span?* If the answer were "the whole 64 KB image", the tilemap
could not leave the framebuffer and the pivot would need a different
shape. Static analysis of the arcade binary (`m68k-elf-objdump -D` over
`roms/altbeast/prog68k.bin`):

**Tile RAM is written through pointers, never through absolute
addresses.** Of 29 absolute operands in 0x400000-0x40FFFF, all but one
are `lea` pointer setups. Tracing each pointer: **11 sites write-only,
1 site reads.** But that scan UNDER-REPORTS, because the important
reads happen in called subroutines, outside the trace window — worth
knowing before trusting a similar sweep.

The complete read set is two things:

**1. Collision probes — ~15 KB, the real answer.** `lea 0x400000,%a0`
at `0x683c`, then `bsr` into `0x6936+` where the tests live:

    6936:  tstw %a0@(0,%d4:w)     6942:  tstw %a0@(0,%d5:w)
    694e:  tstw %a0@(0,%d6:w)     695a:  tstw %a0@(0,%d7:w)

63 indexed reads through a tile-RAM pointer across the binary. The
index arithmetic at 0x6806-0x683a bounds them:

    d1 = ((x & 0x600) << 3) | ((y & 0x1F8) >> 2)      max 0x307E
    d2 = ((v & 0x0F8) << 4)                           max 0x0F80
    d6 = d1 + d2                                      max 0x3FFE

So the reads span **0x400000-0x403FFE, ~15 KB of the 64 KB** — the
scroll-plane name tables, read a WORD AT A TIME to ask "is this cell
solid?". Not a bulk transfer: a handful of `tst.w` per actor per frame.

**2. Round-transition scratch — 1 KB, and it is not tilemap data at
all.** At `0x1B76A`, 256 longs of the game's own work RAM at 0xFFFC00
are stashed into tile RAM at 0x401000 and read straight back at
`0x1B7A4` after the transition. The game is using video memory as a
scratch buffer. It can be redirected anywhere readable; nothing else
depends on it living there.

### Why this UNBLOCKS the pivot rather than gating it

Section 7 framed the read-back as the hard problem because the 32X
framebuffer is not readable by the 68000 in any convenient way. **But
the pivot moves these tables to MD VRAM, and the 68000 CAN read MD VRAM
— through the VDP data port.** The collision sites are single-word
reads, so the natural fix is to thunk those 63 sites to VDP-port reads
(`tools/patch_game.py` already emits write-observer thunks; this is the
read-side twin of a mechanism that exists).

**The thing that made the read-back look fatal was the destination we
had chosen, not the game.** Put the tilemap where the 68000 can read
it and the problem dissolves. Remaining cost to size before building:
a VDP-port read is far slower than a RAM read, and there are up to 63
sites — but they fire a few times per actor per frame, not per pixel.

Fallback if the port reads prove too slow: shadow the ~15 KB in MD work
RAM (64 KB total, already largely spoken for) and read the shadow. The
game writes that data itself, so a shadow is free to maintain — it is
the same stream that feeds the VDP.

## 10. PIVOT SLICE 1a — MD video composites through the 32X layer. VERIFIED.

The whole pivot rests on the 32X being able to leave holes for MD video
to fill, and **nothing had ever tested it**. The port has driven the MD
VDP at 0.2 writes/frame since it was written and both name tables read
empty on hardware, so the compositing path had never once been
exercised. Testing it cost an hour; assuming it would have cost a month.

`make MDBG=1` paints a font-glyph pattern into MD Plane B (0xE000,
already enabled by `md_start.s`) and has the SH-2 leave the BG rows at
pixel 0 instead of composing them. **Result: the MD pattern appears,
through the 32X image, exactly in the rows the BG vacated.**

### The thing that was wrong, and it had been wrong from the start

**Transparency on the 32X is bit 15 of the CRAM ENTRY, not "pixel index
== 0".** `m_main.c` reserves group 0 so that "no composed pixel is ever
VALUE 0 (the MD-through value)" — the slot was deliberately kept for
this and then **never armed**, because `cram_mirror[0]` has always been
`0x0000`: through-bit clear, i.e. opaque black.

First attempt therefore painted the BG rows solid black rather than
transparent, which is the same mechanism NOTES recorded years ago for
dropped banks ("all zeros -> every pixel hits CRAM[0] = 0x0000,
through-bit clear = opaque black"). One word — `cram[0] = 0x8000` — and
the layer opens up.

    cram[0] = 0x0000   index-0 pixels are opaque black   <- shipped state
    cram[0] = 0x8000   index-0 pixels are MD video       <- what we need

### What this buys beyond the pivot

The strobe gets cheaper for free. A restore that misses vblank shows
the un-painted bank, which today is 71,680 bytes of index 0 = solid
black = the flash. Once the through bit is armed AND the BG lives on
the MD VDP, that same dropped bank shows **the MD background** instead
of black. The artifact degrades from a black flash to a missing
foreground — still wrong, far less violent.

### Where that leaves the pivot's assumptions

    compositing works                 VERIFIED (this section)
    read-back is affordable           ANSWERED (section 9, ~15 KB)
    patterns fit in 64 KB VRAM        counted, not yet uploaded for real
    priority split is affordable      MEASURED (0.26 of a pass)
    21 -> 8 colour merge              MOSTLY DISSOLVES, see section 11

Next slice is 1b: replace the font pattern with real System 16 BG tiles
— convert the patterns to MD 4bpp planar, generate the name table from
the game's tilemap, drive scroll from its registers. Colour stays wrong
until the merge is built; a single-scene static allocation is enough to
judge the geometry.

### Slice 1b, step 1 — the pattern conversion, verified offline

`tools/md_tiles.py`. S16 tiles are 3 bitplanes (pens 0-7); MD wants 4bpp
packed, 32 bytes/tile, 4 bytes/row, **high nibble = LEFT pixel**. Pens
0-7 drop straight into a nibble, which is exactly what leaves 8-15 free
for the FG/BG palette pairing in section 4.

    tools/md_tiles.py verify  ->  16384 tiles, 0 round-trip mismatches

and rendering tiles 512-767 through the conversion shows the stage-1
masonry, so the format is right in shape as well as in bits. Verified
off-target before a line of on-target code, because a wrong pixel order
would have shown up as "the pivot does not work" rather than as "the
converter is byte-swapped".

**VRAM budget, concrete.** `md_start.s` puts Window at 0xB000, Plane A
at 0xC000, Plane B at 0xE000, hscroll 0xFC00, sprites 0xFE00. So
patterns own **0x0000-0xAFFF = 45,056 bytes = 1408 tiles**, less the 45
font tiles already there = **1363 available**. Peak measured demand is
599 distinct scroll patterns (+96 text) — it fits roughly 2x over, which
is the first time that claim has been checked against the actual VRAM
map rather than against 64 KB.

**Transport, decided.** The MD cannot read SH-2 SDRAM, so patterns have
to cross somehow. Three options considered:
  - *MD-planar copy in cart ROM* — +512 KB and needs bank management via
    0xA15104 to reach it. Rejected for now.
  - *SH-2 converts at runtime into FB scratch, MD uploads* — reuses the
    established FB channel, no ROM or build changes. There is 1536 bytes
    of documented dead FB space at 0x11A00-0x12000 (NOTES), which is 48
    tiles per batch: ~13 batches to move 600 patterns. **Chosen.**
  - *MD converts* — it has no source data. Not possible.

Next step is that transport plus the name-table write; the tilemap word
decode the MD needs is already established in `compose_layer_regs`:
`code = w & 0x1FFF` (bank-remapped when bit 12 is set),
`colour = (w >> 6) & 0x7F`.

## 11. Colour is not the long pole. The depth loss does the merging for us.

Section 4 framed this as "21 distinct tile colour sets against an MD
capacity of 8, 2.6x, needs a per-scene merge". That framing counts SETS
of 8 as if all 8 pens were distinct and no two sets overlapped. Measured
against 28 real captured scenes (`cram_mirror` out of the ares states),
neither holds.

**1. The depth loss is invisible.** MD CRAM is 3 bits per channel
against System 16's 5. Quantising the live palette from `rom/s16.bs1`:

    5bpp -> 3bpp error, per channel, in 0..31 steps:  max 2,  mean 1.05

Rendering the actual captured frame both ways side by side, the images
are indistinguishable. ~3% error on a palette that is mostly greys and
earth tones.

**2. That same quantisation collapses the colour COUNT by a third.**
Distinct colours per scene, before and after MD quantisation, over the
28 states with a live palette:

    mean   92 distinct at 5-bit   ->   58 at MD 3-bit
    worst 133 distinct at 5-bit   ->   81 at MD 3-bit
    MD total capacity = 64 (4 palettes x 16 pens)

**The mean whole-frame demand lands UNDER the MD's entire 64-colour
capacity once quantised** — and that is the whole frame, sprites
included, when sprites are not even moving. The subset that actually
migrates (BG + FG cat-0) is smaller again.

The palette is far more redundant than a set count suggests: sets share
colours, and 3-bit quantisation merges near-duplicates that were only
ever distinct at 5-bit.

### What this changes

No runtime 21->8 merge algorithm. **Precompute the assignment**: we have
the ROM and every palette state the game reaches, so the set->palette
mapping and the quantised entries can be computed offline per scene and
shipped as a table. Runtime cost becomes a CRAM upload, not a search.

Remaining risks, stated so they are not forgotten:
  - The worst scenes still quantise to 81 whole-frame. If the migrating
    subset exceeds 64 in any scene, that scene needs a real merge —
    measure the BG+FG-cat0 subset specifically before assuming it does
    not.
  - Colour CYCLING. LOOP 8 found regions 0 and 1 are rewritten every
    vint by the cycling sets. A precomputed static table cannot follow
    that; those sets need a live CRAM path, which the MD has (a change
    queue, Chaotix-style — section 2 model B item 2).
  - These counts come from the composed 32X palette, not from the BG
    layer in isolation. The conclusion is directionally safe because the
    subset is strictly smaller, but the exact figure is not measured.

## 12. CORRECTION: the plane reader was dropping every DMA'd write

Mike asked whether Space Harrier and After Burner use the 68000 for
background layering at reduced colour. Testing it exposed a bug in
`tools/vdp_planes.lua` that invalidates most of section 7.

**The bug.** The VDP control word composes a 6-bit code: CD3-CD0 select
the destination, CD5/CD4 select DMA. The shadow compared the WHOLE
composed code against 1/3/5, so any DMA'd transfer — where CD5 is set
and the code reads 33/35/37 instead — fell through every branch and was
silently discarded. **That is most of a Genesis game's VRAM traffic**,
which the tool's own docstring says in as many words, immediately above
the code that failed to do it.

It surfaced only because the CRAM control read `cramNZ=2` for Chaotix,
whose colourful MD background is visible in a screenshot. An absence
that a known-positive control contradicts.

**Corrected, gameplay-verified, DMA included:**

| title | Plane A | Plane B | MD spr | window | churn |
|---|---|---|---|---|---|
| Space Harrier | 0.97, 325 tiles | 0.96, 410 tiles | 77 | 0.98 | 1120 / 1115 |
| Mortal Kombat II | 1.00, 973 tiles | empty | 64 | 0.85 | 1120 |
| Knuckles' Chaotix | 0.09, 42 | 0.80, 46 | 16 | – | 214 / scrolls |
| After Burner | 0.57, 137 | 0.40, 115 | 19 | 0.62 | 3, scroll moving |
| **this port** | **empty** | **empty** | **0** | **0** | **–** |

**Space Harrier runs both planes essentially full and fully redrawn
every sample, with 77 sprites and live scroll.** MK2 mid-fight runs a
full Plane A with 973 distinct patterns and 64 sprites. Both were
reported in section 7 as inert. They are not.

### What this invalidates

  - The section 7 plane table, and the "only one of four" conclusion.
  - The MK2 retraction: "MK2 mid-fight draws NOTHING with the MD VDP"
    was itself an artifact of this bug. MK2 uses the MD VDP heavily.
  - The "pivot's evidence is a sample of ONE" framing. Several titles
    drive the MD VDP during gameplay, so the pivot is BETTER supported
    than the corrected-once story claimed, not worse.

What survives untouched: **this port writes 0.2 VDP writes/frame and its
name tables are empty** — no DMA is involved on our side, and slice 1a
independently confirmed our planes were blank before we painted them.
We are still the outlier.

### Still unresolved

`cramNZ` reads 0-1 for Space Harrier, After Burner and MK2, while
Chaotix reads 54.

**I went after this and could not settle it. Writing down where it
stops rather than guessing a third time.**

Counting the two CRAM write paths separately: Chaotix does 3910
data-port + 96320 DMA writes and ends with a real 54-colour palette.
Space Harrier does **1** CRAM write in 2000 frames of gameplay, value
0x0010 into entry 0, with both planes at 0.97/0.96 fill and 77 sprites.
A full plane with a black palette cannot be on screen.

But the shadow **installs its tap on the first `frame_done`, so all VDP
traffic during boot is invisible to it.** A game that loads its palette
once at startup and never touches it again is indistinguishable from
one that never loads a palette. Installing the tap at script load
instead was tried and is strictly worse: it desyncs the two-word
control state machine (vramNZ collapsed 31112 -> 2, CRAM writes 1 ->
16425, palette still all zero).

So there are two live explanations and this tool cannot separate them:
  1. SH/AB/MK2 load MD CRAM at boot, before the tap exists, and their
     MD layers ARE visible — in which case Mike's "background layering
     on the 68000" reading is right and they are doing it in colour.
  2. Their MD layers are genuinely unpalettised and invisible, drawn
     into VRAM and covered by the 32X.

**Treat `cramNZ == 0` as UNMEASURED, not as absence.** Settling it needs
a different instrument — reading CRAM out of the emulator rather than
reconstructing it, which MAME does not expose for `:gen_vdp` any more
than it exposes VRAM (section 8). It is also not on the pivot's critical
path: our own colour budget is measured from our own savestates
(section 11), and does not depend on what Space Harrier does.

## 13. PIVOT SLICE 1b — real S16 tiles reach MD VRAM through the framebuffer

`make MDBG=1` now ships actual Altered Beast patterns to the Mega Drive
and displays them under the 32X layer.

**The transport.** Inside the window (FM=1) the SH-2 converts a batch of
40 S16 tiles to MD 4bpp planar straight into the dead FB block at
0x11A00 — 1280 bytes of payload plus a 4-byte header, inside the 1536
available. After the ack (FM=0) the MD reads the same block through its
0x840000 window and pushes 640 words to VRAM.

**Self-describing and idempotent, deliberately.** FB staging is
PER-BANK, and that skew is exactly what broke the palette path once
before (`patch_game.py`, the 0x840000 note). Rather than reason about
which bank the MD will see, the packet carries its own base tile code
and the magic word is written LAST. A stale read re-uploads tiles the
MD already has: one wasted VRAM write, nothing corrupted. **No bank
reasoning anywhere in the path.**

**Result: recognisable Altered Beast artwork appears on the MD layer**,
in the rows the 32X BG vacates. Same code path as slice 1a, different
data source — the only change is that the pixels now come from the S16
tile ROM across the framebuffer, so their appearance IS the proof that
the transport works.

    SH-2  tile_pixels() -> 4bpp planar -> FB 0x11A00   (FM=1, in window)
    MD    FB 0x851A00 -> VDP VRAM slot = code*32       (FM=0, post-ack)
    MD    Plane B cell N = slot N                      (a tile sheet)

**Cost, measured:** 40 tiles/window is 2560 pixel conversions inside the
FM window, and 640 VDP writes in the MD tail. The shipping build is
untouched (parity 24.26, _end 0x06018d38) because all of it is behind
`#ifdef MD_BG`, but that per-window cost is real and will need pricing
when this stops being a probe — the window is the thing we are trying
to shrink.

**One disagreement on the record.** `tools/vdp_planes.lua` reads this
build as `fill=1.00 dist=1` — one repeated tile — which the screenshot
plainly contradicts, and `vramNZ=32737` claims essentially all of VRAM
is populated when the uploads provably stop below 0xB000. The plane
reader has now been wrong three times (missed DMA, missed boot traffic,
and this), so the screenshot is the evidence here and the reader is
not. **Do not use it to judge our own builds until it earns it back.**

### What is still missing before this is a real background

  1. The name table is a tile SHEET, not the game's tilemap. Wiring it
     means the MD reading tilemap words (`code = w & 0x1FFF`,
     bank-remapped on bit 12) and mapping S16 code -> VRAM slot.
  2. No slot allocator. Slot == code works only while codes stay under
     1363; peak distinct demand is 599, so a first-come allocator with
     no eviction is enough, but it does not exist yet.
  3. No scroll. The registers are snapshotted on the SH-2 side and have
     to reach the MD.
  4. Colour is a grey ramp in palette 0, not the game's sets — section
     11 says precompute the assignment, which is not built either.

### Slice 1b CONFIRMED ON ares — and I nearly called it a failure

`rom/ARES_mdbg.bs9`, 4134 cycles, plus a 2237-frame capture.

**Visual:** the MD band renders "0123456789" and "ABCDEFGHIJKLMNO"
straight through the 32X layer. My first read was that these were
`md_start.s`'s boot font surviving at slots 0-44, i.e. the upload had
never happened. **Wrong: S16 tile indices 0-255 ARE the game's own text
font** (`tools/md_tiles.py png out.png 0` shows it). Those glyphs are
our transported data.

**Byte-exact confirmation, because the visual nearly fooled me:**
searching the savestate for specific converted tiles finds S16 tile 400
at 0xd9566 and tile 800 at 0xdc766 — exactly 400*32 apart, i.e. sitting
in MD VRAM at `slot == code`, which is the mapping the MD uploader
uses. The packet magic 0xB6B6 is in the state too.

So the whole chain works on real hardware semantics:

    SH-2 convert -> FB 0x11A00 (FM=1)  ->  MD read 0x851A00 (FM=0)
      -> VDP VRAM slot=code  ->  Plane B  ->  visible through the 32X

**The per-bank staging skew did not bite**, which is the design paying
off: the packet is self-describing and re-uploading is harmless, so the
path never has to know which bank it got.

**Cost on ares, and it is heavy:** blit skips 26.6% -> 61.4% of cycles.
40 tiles/window is 2560 pixel conversions inside the FM window — the
window we are trying to shrink. Deferrals actually FELL (342 -> 162)
and dreq_incomplete fell (21% -> 10.9%), so this is not general
overload; it is specifically the window getting longer. A real
implementation ships patterns only when the tilemap dirties them, not
40 every window forever, so this number is a probe artifact rather than
a projection — but it does mean the steady-state upload rate has to be
budgeted, not assumed free.

**Method note.** Two of my last three conclusions from a picture were
wrong. Cross-checking the frame against the savestate cost two minutes
and flipped the verdict from "transport failed" to "transport works".
Read the memory, not the screen.

## 14. Per-band update rate — Mike's eye found a real scheduling imbalance

Mike boxed a horizontal band of a gameplay frame and said it "rendered
frames, animations and timing perfectly through the entire
playthrough". Measured against the 9738-frame ares capture with
`tools/row_health.py` (capture is ~57fps of a 20Hz display, so a
fully-updating row reads ~2.85 frames between changes):

    band          rows      interval    never-updating
    R0 slave       0- 35      5.02       1
    R0 master     36- 71     53.79      22
    R1 slave      72-107     13.61       0
    R1 master    108-143      8.70       0
    R2 slave     144-183      6.01       0
    R2 master    184-223     10.57       8

    Mike's box   130-181      5.54
    everything else          14.35

**His box updates 2.6x more often than the rest of the screen**, and it
lands on R2-slave plus the tail of R1-master. He read a real signal off
the screen that no counter we had was reporting.

**~~The actionable part: R2 slave 6.01 vs R1 slave 13.61~~ — RETRACTED
by the control capture, see below.** I claimed the R1/R2 spread was
scheduling rather than content because "both bands take the same hit"
from the probe. That reasoning was wrong and the control says so.

**CONFOUNDS, which matter here more than usual:**
  - A row that never changes because nothing MOVES there is
    indistinguishable from a row the pipeline is starving. R0 master's
    53.79 is largely sky — and worse, in THIS corpus it also contains
    the static tile-sheet band that `MDBG` paints. That number is an
    artifact of the probe, not a finding.
  - The corpus is an MDBG build, which inflates blit skips 26.6% ->
    61.4%. The absolute intervals are therefore pessimistic across the
    board. **The R1-vs-R2 slave comparison survives this** because both
    bands take the same hit.
  - What would settle it: the same capture on a clean shipping build.
    Then compare band-for-band between the two, which removes the
    content confound entirely.

### The control capture: the imbalance was the probe, and the pipeline is fairly even

Same measurement on a clean `rom/s16.32x` capture (8512 frames):

    band          rows      CLEAN     MDBG probe
    R0 slave       0- 35    12.75        5.02      <- sky, 9 rows static
    R0 master     36- 71     6.71       53.79
    R1 slave      72-107     3.72       13.61
    R1 master    108-143     3.16        8.70
    R2 slave     144-183     4.04        6.01
    R2 master    184-223     5.81       10.57

    Mike's box   130-181     3.67        5.54
    everything else          6.39       14.35
    play area 36-223         4.65                  (ideal 2.85)

**R1 slave 3.72 against R2 slave 4.04 — the 2.3x spread is gone.** So
the imbalance was an artifact of the MDBG probe, not a scheduling bug in
the band queue, and the "drop_s0 rotation is not spreading staleness"
lead is dead. My stated reason for believing the confound was survivable
— "both bands take the same hit" — was simply false; the probe hit R1
much harder than R2.

What the control DOES establish, and it is worth having:
  - **The clean pipeline is fairly even across the play area**, 3.16 to
    5.81 against an ideal of 2.85. There is no starved band.
  - **Mike's box is still the best region even on a clean build** (3.67
    vs 6.39 elsewhere) — his eye was reading something real both times,
    it just is not a defect.
  - The remaining gap from 2.85 to 4.65 is the blit-skip rate showing up
    as visible staleness: this session measured 39.1% skips, and ~4.65
    is about what one-in-three dropped frames looks like.

That last line is the useful one. **Per-band update rate is a direct
visual proxy for the blit-skip counter**, so `tools/row_health.py` can
score a build from a capture without a savestate — and unlike parity, it
measures the thing the player actually perceives.
