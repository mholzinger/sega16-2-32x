# LESSONS — findings that must never be re-derived

**Each entry is a rule, the measurement that established it, and what it
cost when it was violated.** If you are about to propose something, check
this file against it first.

Add to this file only when a lesson cost real work. Do not add
speculation.

---

## About the instruments

### ares and the rig disagree about WHICH CPU the wall is on

**ares is SLAVE-gated. The rig is MASTER-gated.** On ares the wall is
`max(echo, mtask)` and echo wins, so removing master instructions cannot
move the ares number by construction.

*Cost:* every card sized from LOOP29 275's phase split aimed at the wrong
processor. NT_SKIP was shelved on an ares ranking (LOOP29 177) three days
before the correction (LOOP29 294) that changed its value.

### ares charges INSTRUCTION COUNT, not memory or fetch

No SDRAM waits, no uncached waits, no data cache, **no instruction
fetch**. So every size-for-count trade in this renderer's history
measured as a win on ares and may have been a loss on hardware.

*Corollary:* memory-traffic cards rank on the rig's frame probe. ares
keeps exactness.

### PROFILE GAMEPLAY, NOT ATTRACT

`bm_scan_baked_ok` requires `MD_STATE_PLAY` (or attract steps 1/3/5 with
bit 8). Attract fails it, so the live scan runs there **and only there**.

*Cost:* every number produced in the week of 2026-09-16 came from an
attract window. Two cards were costed against work that does not run in
gameplay. `bm_scan_rows` is 27.6% of `_m_main` on attract and **0.00%**
in play.

### A union over N frames is not a working set

The cache sees ONE generation. A 200-frame union reads 21,856 B; a
2-frame window reads 14,256 B; ten frames adds 1 KB.

*Cost:* a "1% cold tail" of 13,168 B was proposed for relocation. At a
2-frame window essentially all of it is already touched — there is no
cold region.

### Counter indices collide, and there is no registry

`DIAG[36]` is both the nearest-colour fallback counter and
`r60_pkt_flip` (`m_main.c:15023`) — and something wipes it every frame.
`MDA[19]`/`MDA[20]` are shared between `mdp_claim_pen` and the
cell-chunk shipper, where `MDA_ADD(20, sc[5])` accumulates a **word
count**.

*Detection rule:* **a subset counter may never exceed its parent.**
`MDA[20] = 23,038` against `MDA[19] = 218` is structurally impossible and
that is how the collision was found.

*Cost:* two debugging cycles. A registry comment listing every index with
its owner is cheaper than either.

### grep is not a free-space test

Live code reaches scratch through base pointers. `0x26028DE0` and
`0x26028DA0` both had zero literal references and both were live.

**The authority is the memory map comment at `m_main.c:1329`.**

*Cost:* two builds.

### Stale state reads as live state

`mdp_free_set` clears `mdp_line_c` and does NOT clear `mdp_pen_own`.
**Never read `mdp_pen_own` without masking on `mdp_line_c != 0xFFFF`.**

*Cost:* a regression was reported as an improvement.

### A metric can measure its own failure

"Non-field centre 83.3%" was a classifier keyed on the arcade's palette.
Once our palette was wrong, every pixel read non-field **by
construction** — the number measured the classifier failing, not
coverage.

### Eight instruments lied in one arc

Raw-DRAM histogram read as a decoded framebuffer; `DIAG[36]`; a corrupted
scratch block; two dead scratch addresses; `.bs1` vs `--dump`; a wrong
base address; the classifier above; `MDA[19]/[20]`.

**Assume a new number is wrong until its instrument is checked.**

## About reasoning

### A single counter-example is a reason to ask WHICH CASE, not to withdraw

The claim "cyclers own no pens and are never repainted" was withdrawn
because one pen (14) tracked correctly. That pen was **sole-owned**, and
`m_main.c:16082` gates the in-place recolour on `mdp_pen_rc == 1`. The
counter-example was the clue.

*Cost:* a correct mechanism was abandoned and a build was spent.

### The census answers the question you asked; the picture answers the one you didn't

Ten exchanges of pen/owner/colour/mask censuses on one screen. **Nobody
looked at the frame.** One look at f1575 closed the entire palette path.

### Check the premise, not the arithmetic

Six mechanisms died in one week. **Every one was arithmetically correct
and built on an unchecked premise** — a plane label, an occlusion
direction, an array's length, an attract profile.

*Rule:* before a card, state what the record already says and **read
`.build_flags`**. Both times that was done, it killed a card before it
cost anything.

### Docs written as proposals get re-proposed

`ARCHITECTURE.md §4` says "Adopt Model B". It shipped weeks ago. A full
costing exercise was one message from being spent on it.

*Rule:* state lives in `STATE.md`. Narrative docs are history.

## Hardware and game facts (derived from ROM/RTL, will not change)

### The bar is a threshold

60 Hz is 100% single-vint. Ships are vint-quantised, so 1.2 v/gen still
costs 2 and flips at 30. Nothing is paid until the wall crosses below
1.00 — then it all arrives (15% → 98% in one ablation).

### The camera moves at most 0.5 px/frame

Table at rom `0x1878`, 8 words, indexed by `active_enemies` clamped to 7,
max entry `0x0080`; math at `0x39CE` gives px/frame = v/256. **The only
non-zero writer of `FFF158` in the binary** — the other three write zero.

So a new 8-pixel tile column enters the viewport **at most once every 16
frames**, and with enemies on screen once every 32-512, or never.

### Vertical scroll never changes

`world_yposition` has four writers and all four write `#$1020`.
`unk_FFF0E6` has one writer, `#0`. Y scroll is the constant `0x20` on
both planes for the entire game.

### Tile RAM has no in-play writer

`m_main.c:3199` / LOOP-DECOMPILE 99. The tilemap is static during play,
which is what makes caching the scan **safe**, not merely fast.

### The transformation screen

`sub_3A00`'s cutscene branch (`0x3A0C-0x3A22`) zeroes all four scroll
registers and selects pages 10/11 (`#$AAAA`/`#$BBBB`). Static by
construction. `glow_chev` (`m_main.c:14620-14650`) is the exact gate —
"any page nibble >= 10". **Do not write another.**

The face itself is a four-palette **zoomed sprite** built by `sub_90F4`
(`0x90F4`), not tiles. Pages 10/11 are the backdrop behind it.

`0xFFF148` is an **object marker, value = slot + 1** (dispatcher
`0x398E`) — **not** a scene flag, despite `m_main.c:6611` calling it
"the game's own cutscene byte". That contradiction is unresolved and is
load-bearing for `mds_onscreen`.

### Palettes are QUEUED, and the arcade queue has no budget

`RequestPaletteUpdate` (`0x3B2E`) does not write a palette; it queues an
entry and `0x3C5A` drains **the entire queue every vblank** with no cap.
**Any partial landing on our side is ours.**

Each entry writes **14 colours / 28 bytes** to `dest + 2`. **Colours 0
and 15 of every slot are NEVER written by the game** — anything there is
our own initialisation and is not evidence.

### Cat-1 is on the FOREGROUND plane and is never tile-occluded

Pages 0-4 are the foreground (`snap[0]`, `m_main.c:2909`/`:15042`,
`pagesel_census.txt`). `jts16_prio.v:83-95` tests the foreground first,
so no tile plane is above cat-1. **Tile-occlusion of cat-1 is zero.**

*Cost of the inverted label:* six entries of occlusion percentages, all
void.
