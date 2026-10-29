# HANDOFF — the renderer/picture lane, 2026-10-07/08

Lane: the picture under the port. The port thread owns `tools/cport_prep.py`,
`sh_src/portfull/` and the game-logic gate's *contents*; a port change goes to STATE's open
list, not into those files.

Commit range `a2b5aba9..419dbb15`. Nothing pushed.

## 0. The one thing to read first

**The gate was lying, and it had been lying for two days.** `check-demo` reported A 0074 (39
bytes) and A 00c4 (59 bytes) as the port's own defect, stable across runs. They were never a
defect: the gate was comparing arcade and port dumps taken on **opposite sides of the game's
own frame boundary**. With `0xFFF01C` — the latch the arcade's IRQ4 handler self-gates on —
in the alignment key, the gate prints *"every live record matches the arcade at all 20 ticks,
and the FB's tilemap staging matches truth"*.

It was found by a **pad control**: 688 bytes of dead space in `.ramtext`, nothing hot moved,
no function changed. It shifted the port 4 wall frames, moved the failing targets to entirely
different ones, and took the picture 0 → 7 of 20. A change that cannot alter behaviour must
not alter the gate. `tests/demo-b/README.md` now carries that as the standing rule:

> A standing difference gets a pad control before it gets a card.

The alignment key is now three things, and all three are in the picture sidecars:
**tick · game frame · frame latch** (plus the adopting rom, as provenance). A comparison that
lands on a different value of any of them is REFUSED, not counted.

## 1. State of the gate

| half | status |
|---|---|
| records | **SOUND.** HEAD 0 of 20; the 688-byte pad also 0 of 20 — the same reading under a layout change. |
| FB staging | 20 of 20 pages match truth. |
| picture | **CANNOT ARBITRATE A LAYOUT CHANGE.** See §2. |

`make check-demo` measures a snapshot of the rom and names its sha1 in the first line.
A probe build (`PORT_EXTRA=...`) now lands at `build/port/rom/<slug>.32x` and **never**
`s16.32x`; the gate refuses a probe two ways.

## 2. THE PICTURE IS NOT A FUNCTION OF GAME STATE

Measured at a target where all four key parts agree — tick 0228, game frame 2034, latch 0,
every live record identical — HEAD and the pad differ by **5,148 of 69,632 framebuffer
bytes**. The framebuffer stores pen indices, so the obvious reading was the allocator handing
the same art different pens. Resolving both through their own CRAM kills that: **colours
differ by 6,127, more than the indices.**

The renderer's output depends on its own allocation history. Mike's call, in order:
**option 2 now** (compare the picture against the ARCADE via `attract_parity`, not the port's
own past), **option 1 next** (make the renderer deterministic — the allocator card, §5),
**option 3 never** (widening the threshold).

## 3. The rig

Numbers are from `PORTRIG=1`, which paints ticks/second and the 1-vint share as two bars in
screen rows 216-223 and is read by `tools/rig_bars.py` off the rig's own screenshot.

**THE LINE: 34.8 ticks/second, 39% one-vint** (differenced, four log-proven launches,
2026-10-09), rom pc9rig sha1 `37cafdc6`. **The 47.5 / 73% this file was written around is
STRUCK — it rested on a single reading.**
Every speed card reports against that, three launches, differenced readings.

Traps, all paid for:
- **The counters are cumulative since boot**, so a reading is an average over the whole run
  including the boot. Quote elapsed time with every number. Differencing two readings on ONE
  run divides the boot out — that is where the struck 47.5 came from (32.4 at 68 s, 42.4 at
  202 s), from ONE reading. Four launches of the same rom later read 34.8 / 39%.
- **The launch API no-ops on an already-running path.** Push under a DISTINCT NAME per launch
  and prove it in `/tmp/remote.log`; a second POST to the same path is not a second launch.
- **The pen cannot be a fixed index.** 0xFF lands on CRAM[255]=0x0000 and FB value 0 is
  transparent — black on black. The bars use the brightest entry in the live palette, scanned
  from the renderer's SDRAM shadow `cram_mirror`, so no CRAM is touched.
- **FB value 0 is transparent**, so the strip's uncovered part shows the MD plane through it:
  black on ares, tiles on the rig. The reader finds the brightest pen and measures its run
  from x=0; a two-colour/one-transition signature works on ares and fails on the rig.
- **The picture dies at 57 minutes**, twice, bars included — see §4.

## 4. Open defects, with what is already ruled out

**The eye scene draws no face.** Its cells are sets 22-32; round 0's table carries 30 sets
(72-101) and a set absent from the table is REFUSED an MD line and renders as BACKDROP
(`pal_rounds_md.h`'s own header). The name table is byte-identical to the arcade's (0 of
0xD000), the FB is 97.2% transparent across the face, so it is not the name table and not the
palette. Ruled out by measurement: the art IS baked (round 6); merging sets into `mdr_s_map`
alone does NOTHING (a set also needs `mdr_s_line`, `mdr_s_used` and its colours in
`mdr_line_c`); the full split builds, selects and still leaves the face 96.6% transparent.
The allocator census (§5) says the next stage to look at is set-to-slot assignment.

**The hour death.** Black at **57 minutes on two fresh launches**, bars included — so the
frame body stopped painting, not the game. `PAL_SETGEN` disproved (seeded one step from
0xFFFF, renders normally; generations read max 209 at frame 600 and reset per scene). All
SEVEN 16-bit counters stepped per presented frame seeded to 0xFFF0 at once (`PORTGENSEED=1`):
not black. The clue is wrong. **It has never been reproduced on ares** — that is the next
move, not another counter.

**The demo half's missing art.** Sets 19-21, 674,851 cells, render as backdrop today and need
four colours more than three MD lines hold. The one question: **is the fourth MD CRAM line
free during the demos?** That is a census of the sprite attribute table over the demo frames.

## 5. The allocator card (option 1) — cost it from the record, not from scratch

`grep FACTS.md LESSONS.md` and the `sky-allocator-order` memory give the answer already:
the mechanism is **first-come MD pen packing + LRU VRAM slots deciding by arrival order**
("arrival order at the cut decides"), the prescribed fix is **per-scene static assignment**
(`MDSTATIC=1`, FACTS 206, arc `docs/design/STATIC-SCENE.md`), and **it is already on**.

It still fails because `MD_ROUND` re-keyed those tables from the detected scene to the game's
ROUND, the whole attract runs at round 0, and round 0's table covers **30 of the 71 sets the
attract draws**. The other 41 ride the dynamic path — which is the path dependence in §2.

**So the allocator card and the eye card are one card.** Derivations are written and tested:
`tools/round_sets.py` (per-round set list from the arcade corpus) and `tools/round_split.py`
(the static half as its own table, packed [15,14,6], tables 0-9 byte-for-byte). The split key
is `0xFFF022` — 0x0C in the title/high-score/post-demo screens, 0x09 in both demos, nothing
else, and it LEADS the art at the cut. The measured blocker is the MD colour budget: 3 lines
x 15, static half fits, demo half over by four colours.

Two recoveries that come with the split, both measured: a sub-table **inherits its sets'
layer masks** (36 of 39 draw on one layer: 78 blocks → 42), and **tables 5-9 need no art**
once the attract is key-selected (293 → 229 blocks), which is what makes it fit `.tilesmd`'s
560 KB gap at all.

## 6. Card 2, the speed card — where it stands

Ranked, not landed. `tools/sh2_profile.py` over `ares-headless --profile`:

| symbol | region | share |
|---|---|---|
| `_m_main` | SDRAM | 49.62% — one PC is 9.3% of all cycles: the idle spin |
| `_visr_vbi` | CART | **10.70%** |
| `_port_object_dispatch` | CART | 2.39% |
| `_flip_span` | CART | 2.01% |
| `___ashlsi3_r0` | CART | 1.93% — libgcc: needs a linker placement, not an attribute |
| `_port_pal_probe` | CART | 1.83% |

The hottest symbol is already in SDRAM and half of it is the spin, so the targets are the
cart-resident ~23%. **`.ramtext` has 1,200 bytes free**, not the 28 KB its extent implies.

**Batch 1** = `visr_vbi` (572 B) + `mdp_quant` (116 B), 11.66% of master cycles. Built; its
keyed gate was in flight when this was written. `LOCKCODE_ROM` is deliberately empty in this
build and its comment warns the LINE must keep no attribute there, so the batch makes it
`RAMCODE` **only under `PORT_PLAY`** — the accepted line cannot move.

Note what the gate can and cannot say here: a cart→SDRAM move is invisible to ares, so the
two scorecards should be IDENTICAL if the move is behaviour-neutral. Any difference is §2's
path dependence, not evidence about the move. **Only the rig can say whether it was worth
doing.**

## 7. Instruments added this session

| tool | what |
|---|---|
| `tools/rig_bars.py` | reads ticks/second and the 1-vint share off a screenshot, ares or rig |
| `tools/fb_png.py` | DRAM + 32X CRAM dump → native 320x224 PNG (ares `--screenshot` is scaled/filtered and cannot stand in) |
| `tools/sh2_profile.py` | SH-2 PC profile → symbols, ranked by cycles |
| `tools/round_sets.py` | a round's colour sets, derived from the arcade corpus |
| `tools/round_split.py` | the static half as its own table, packed into 3 MD lines |
| `tests/gameplay/` | the gameplay harness skeleton; it REFUSES to compare until the port's input replay is proven |

`tools/attract_parity.py` was **rotted and is fixed**: line 27 pinned `SCRATCH` to a dead
session's scratchpad, so the one tool that grades against the arcade could not run at all.
It resolves to `build/attract` now and prints the regeneration command when the corpus is
missing. `--port` is MANDATORY (the port never blanks at the cut); without it the tool exits
"OFFSET: not found" and reads like a broken rom.

The MD allocator's own census (`MDALLOCWHY=1`) **did not link on a port rom** — ~1.5 KB of
arrays against a few hundred bytes of `.bss` margin — which is why "which stage refuses this
cell" was open with the answer already instrumented. The arrays move to probe scrap
(0x26038400) under `PORT_PLAY`; read with `--dump "32X SDRAM:0x38400:0x5D0:out.bin"`.

## 8. Method notes that cost something

- **Look at the image before counting anything.** A tile-plane census "proving" the rig's
  background was garbage compared the beast scene against the title card. Opening both
  settled in one second what two rounds of statistics got wrong — and quantising to rule out
  the MiSTer's gamma made the wrong answer look *robust*.
- **The attract does not align by game tick across machines.** At a matched tick ares is on
  the title card and the rig is on the beast scene; the sequence depends on each machine's
  NVRAM. Compare the same scene, or use a rig-vs-rig control.
- **`make port PORT_EXTRA=...` used to overwrite `s16.32x`.** A seeded probe was compared
  against that file as its control — and the file *was* the probe. Both sides agreed
  perfectly, which is exactly what a correct control looks like. Fixed in §1.
- **The port rom's content depends on `/tmp/cport_overrides.json`**, a cumulative list that
  never prunes, so a FAILED compile permanently poisons every later rom. Removing three dead
  symbols moved the gate 3 of 20 → 2 of 20 and shifted the B scene ten frames. Port thread's
  lane; it is in STATE's open list.
- **The rom's sha1 changes on every build** (the stamp embeds a timestamp), so it is a run
  identity, never a code identity — and the gate's dump cache, keyed on it, can never hit
  across builds. Masking the stamp bytes would make an unchanged rebuild free.
