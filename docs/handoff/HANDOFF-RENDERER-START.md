# HANDOFF — the renderer/picture lane. READ THIS FIRST.

Written 2026-10-08 for a fresh thread. Supersedes `HANDOFF-RENDERER-2026-10-08.md`, which is
kept only for the long-form traps in its §7-§8.

**Lane:** the picture under the port. The port thread owns `tools/cport_prep.py`,
`sh_src/portfull/` and the game-logic gate's *contents*. A port change goes to STATE's open
list, not into those files.

**HEAD:** `fb37e550`. Tree clean. Nothing pushed. Gate green.

---

## 1. Where things stand, in one table

| | state |
|---|---|
| `make check-demo` records | **0 of 20** — every live record matches the arcade at all 20 ticks |
| FB staging | 20 of 20 pages match truth |
| picture check | **cannot arbitrate a layout change** — see §3 |
| port clock | x1.013-x1.015 of the arcade axis |
| rig | **34.8 ticks/second, 39% one-vint** (differenced; 33.4 cumulative at 222 s), rom pc9rig sha1 `37cafdc6` — re-measured 2026-10-09 over FOUR log-proven launches under distinct names. **The old 47.5 / 73% is STRUCK: it rested on one reading of 336f13c7.** A line needs three launches, not one reading |
| attract parity — **`tools/attract_parity.py`, mean \|luma diff\| per anchor scene vs the ARCADE's no-coin cold boot, best over the ladder. The only picture instrument that counts; a self-baseline never does (§3)** | boot 51 · logo rewrite 19 · logo red 27 · cut black 35 · demo 45 · face 19 · eye 44 · eye pan 27 · demo 2 48 |
| ares speed, **not a hardware number** | pc9rig reads 57.9 ticks/s · 90.2% one-vint over frames 1200→5400, 86-88% one-vint inside either demo (RIG_SCRAP at sdram 0x38000, two frames differenced). The `97.3%` in HANDOFF-ADVISOR is **unsourced** — no span or instrument was recorded and it does not reproduce |
| batch 1 | **parked on `port/batch1` (`0d8ce155`), reverted off native1 in `99db5052`** — approved on logic, unranked, and unrankable on ares |

## 2. THE FIRST THING TO KNOW: the gate was lying for two days

`check-demo` reported A 0074 (39 bytes) and A 00c4 (59 bytes) as the port's own defect,
stable across runs, and they were **never a defect**. The gate was comparing arcade and port
dumps taken on opposite sides of the game's own frame boundary.

It was caught by a **pad control**: 688 bytes of dead space in `.ramtext`, nothing hot moved,
no function changed. It shifted the port 4 wall frames, moved the failing targets to entirely
different ones, and took the picture 0 → 7 of 20. *A change that cannot alter behaviour must
not alter the gate.*

The fix is `0xFFF01C` — the latch the arcade's IRQ4 handler self-gates on — in the alignment
key. The key is now **tick · game frame · frame latch**, all three in the picture sidecars
alongside the adopting rom, and a comparison landing on a different value of any of them is
REFUSED, not counted.

`tests/demo-b/README.md` carries the standing rule: **a standing difference gets a pad
control before it gets a card.**

## 3. THE SECOND THING: the picture is not a function of game state

At a target where all four key parts agree — tick 0228, game frame 2034, latch 0, every live
record identical — HEAD and the pad differ by **5,148 of 69,632 framebuffer bytes**. The
framebuffer holds pen indices, so the obvious reading was the allocator handing the same art
different pens. Resolving both through their own CRAM kills that: **colours differ by 6,127,
more than the indices.**

The renderer's output depends on its own allocation history. Mike's call, in order:

1. **Option 2, now:** compare the picture against the ARCADE via `attract_parity`, not
   against the port's own past. Immune to this by construction.
2. **Option 1, next:** make the renderer deterministic — the allocator card, §5.
3. **Option 3, never:** widening the threshold.

Until option 2 lands, **judge a code change on the record half plus the rig; read the picture
as advisory.**

## 4. NEXT ACTION

The rig was down at handoff, and Mike's call is to proceed without it. In order:

1. ~~The fourth MD CRAM line census.~~ **DONE 2026-10-08: the line is FREE, and the index in
   the question was wrong.** The MDSPR line is CRAM line **0**, not 3 — lines 1-3 are the tile
   lines (`md_main.c:612` sprite pal → CRAM 0x02; `md_main.c:1237` `pal_shadow_w[48]` → CRAM
   0x20). `tools/mdspr_census.py` walked the VDP's own SAT (VRAM 0xF000, by LINK) at 86 frames
   from 25 to 2150: **zero drawn entries at every frame**, and `MDSPR_CNT` (sdram 0x28E20)
   reads **0 entries / 0 claims cumulatively** at 2150, so no sampling gap can hide a claim.
   The claim pass is not dead — `MDSPR_PAL[0]` is 0 every frame while its pens track the scene
   and CRAM line 0 carries them from frame 625 on. The port paints the sprite line and never
   references it. Full verdict and the three-step path in STATE.
2. **The allocator card** (§5) — started 2026-10-09, **colour blocker cleared, blocked
   elsewhere**, branch `port/mdlines4` (`1b00ebb1`), two builds spent, neither links.
   `tools/round_split.py --demo` re-packs round 0 with the demos' missing sets: 3 lines
   refuses, 4 lines gives `[15, 15, 13, 11]` of 15 with six pens spare. The two blockers that
   replaced it are both measured, and **neither is a flag**:
   - **the art blob** — 518,656 (line) → 547,328 with two sets added, against a 540,672-byte
     gap to `.porttest`; over by 6,656. The layer-mask recovery is **forbidden** for rounds
     0-4 (`bake_tiles_md.py:88-95`, Mike's boss capture, "the transparency shows as purple"),
     so it needs either the static-half split landed first or a cart-layout call.
   - **the fourth line never reaches CRAM** — the 68K ships 48 words to CRAM 0x20, the SH-2
     publishes 48, and `MDSPROFF` does not stop the 68K's own sprite-palette DMA to CRAM 0x02.
     Three sites, in STATE. **No picture read off a 4-line build means anything until they are
     done.**
   `bake_tilecram.py:78`'s "THREE, not four" warning stays TRUE of the line and is now FALSE
   of the port — do not take the fourth line on a NATIVE build without re-running the census.
3. **When the rig answers:** batch 1's payoff is unmeasured. `build/port/rom/pc9rt1.32x`,
   sha1 `39165e5029f1aa70fdb869e48df4286e0f27d329`. Three launches under DISTINCT names,
   two readings each ~70 s apart, differenced. **DONE 2026-10-09: batch 1 CONVERTS ZERO** —
   seven launches, shots at 82/152/222 s, pc9rt1 within 0.5 ticks/s and 2 points of pc9rig at
   every elapsed point, both signs. It stays on `port/batch1` and is not a speed card.

## 5. The allocator card = the eye card. Cost it from the record.

`grep FACTS.md LESSONS.md` and the `sky-allocator-order` memory answer this already:

- Mechanism: **first-come MD pen packing + LRU VRAM slots deciding by arrival order.**
  "Arrival order at the cut decides."
- Prescribed fix: **per-scene static assignment** — `MDSTATIC=1`, FACTS 206, arc
  `docs/design/STATIC-SCENE.md`.
- **It is already on.**

It still fails because `MD_ROUND` re-keyed those tables from the detected scene to the game's
ROUND, the whole attract runs at round 0, and round 0's table covers **30 of the 71 sets the
attract draws**. The other 41 ride the dynamic path — the path dependence of §3, and the
reason the eye scene's face is 97.2% transparent (a set absent from the table is REFUSED an
MD line and renders as BACKDROP; `pal_rounds_md.h`'s own header says so).

Written and tested, ready to use:

| tool | what it gives |
|---|---|
| `tools/round_sets.py --round 0` | the 71 sets round 0 actually references, derived from the arcade corpus |
| `tools/round_split.py --emit` | the static half as table 10, packed [15,14,6], tables 0-9 byte-for-byte |

The split key is **`0xFFF022`**: 0x0C in the title/high-score/post-demo screens, 0x09 in both
demos, nothing else, and it LEADS the art at the cut. No frame draws both families.

**The measured blocker:** the MD colour budget is 3 lines x 15. Static half fits (31 colours).
Demo half needs 44 and does not pack — over by four colours, because sets 19-21 (674,851
cells) are referenced by the demos and absent from the table.

**Two recoveries that come with the split, both measured:** a sub-table **inherits its sets'
layer masks** (36 of 39 draw on one layer: 78 blocks → 42), and **tables 5-9 need no art**
once the attract is key-selected (293 → 229 blocks). Together they are what make it fit
`.tilesmd`'s 560 KB gap at all.

**Already ruled out, do not retry:** merging sets into `mdr_s_map` alone does NOTHING — a set
also needs `mdr_s_line`, `mdr_s_used` and its colours in `mdr_line_c`. The full split builds,
selects correctly (key and round verified in the port's own work RAM) and still leaves the
face 96.6% transparent, so the table was never the last blocker. The allocator census says
the next stage is set-to-slot assignment (`mdp_assign_set` ran 14 times; "blanked: FG cell
empty" is the largest blank reason and GREW as refusals fell).

## 6. Card 2, the speed card

Ranked by `tools/sh2_profile.py` over `ares-headless --profile` (master, 1800 attract frames):

| symbol | region | share |
|---|---|---|
| `_m_main` | SDRAM | 49.62% — one PC is 9.3% of all cycles: the idle spin |
| `_visr_vbi` | ~~CART~~ → **moved in batch 1** | 10.70% |
| `_build_maps_chunk` | SDRAM | 4.99% |
| `_port_object_dispatch` | CART | 2.39% |
| `_flip_span` | CART | 2.01% |
| `___ashlsi3_r0` | CART | 1.93% — libgcc: needs a linker placement, not an attribute |
| `_port_pal_probe` | CART | 1.83% |
| `_mdp_note_tile` | CART | 1.42% |
| `_irq4_handler` | CART | 1.26% |

`.ramtext` has **512 bytes free** after batch 1 (`__ramtext_end` 0x06037E00, region ends
0x06038000). Batch 2 must fit that or reclaim space. A cart→SDRAM move is **invisible to
ares**, so only the rig can price one.

## 7. Open defects

**The hour death.** Black at **57 minutes on two fresh launches**, instrument bars included —
so the frame body stopped painting, not the game. Ruled out: `PAL_SETGEN` (seeded one step
from 0xFFFF, renders normally; generations read max 209 at frame 600 and reset per scene) and
**all seven** 16-bit counters stepped per presented frame, seeded at once with
`PORTGENSEED=1`. The clue is wrong. **It has never been reproduced on ares** — establishing
whether ares reproduces it over an hour is the next move, not another counter.

**The gameplay tape does not reach the boss.** `tools/inputs/level1_boss.csv`, 2,074 events to
frame 16022. The hero is alive at the end but PLATEAUS from ~frame 7200, pinned between x
0x1350 and 0x1415 — a fixed obstacle, not drift. `tests/gameplay/` exists and deliberately
REFUSES to compare until the port's input replay is proven to land the same inputs at the same
game frames.

**For the port thread** (in STATE's open list): the port rom's content depends on
`/tmp/cport_overrides.json`, a cumulative list that never prunes, so a FAILED compile
permanently poisons every later rom. Removing three dead symbols moved the gate 3 of 20 → 2 of
20 and shifted the B scene ten frames.

## 8. Traps, each paid for once

- **Probe builds are named.** `make port PORT_EXTRA=...` writes `build/port/rom/<slug>.32x`
  and never `s16.32x`; the gate refuses a probe. Before this, a seeded probe was compared
  against `s16.32x` as its control — and that file *was* the probe. Both sides agreed
  perfectly, which is what a correct control looks like.
- **The rom's sha1 changes on every build** (the stamp embeds a timestamp): a run identity,
  never a code identity. The gate's dump cache is keyed on it and so can never hit across
  builds — masking the stamp bytes would make an unchanged rebuild free.
- **Look at the image before counting anything.** A census "proving" the rig's background was
  garbage compared the beast scene against the title card. Quantising to rule out the MiSTer's
  gamma made the wrong answer look *robust*.
- **The attract does not align by game tick across machines** — the sequence depends on each
  machine's NVRAM. Compare the same scene, or use a rig-vs-rig control.
- **The rig's bar counters are cumulative since boot.** Quote elapsed time; difference two
  readings on ONE run to divide the boot out.
- **The launch API no-ops on an already-running path.** Distinct name per launch, proven in
  `/tmp/remote.log`.
- **`attract_parity` needs `--port`** (the port never blanks at the cut); without it it exits
  "OFFSET: not found" and reads like a broken rom. Its corpus lives in `build/attract` and is
  regenerated by `tools/arcade_refdump.lua` — it used to be pinned to a dead session's
  scratchpad and could not run at all.
- **`MDALLOCWHY=1` did not link on a port rom** (~1.5 KB of arrays against a few hundred bytes
  of `.bss` margin), which is why "which stage refuses this cell" was open with the answer
  already instrumented. The arrays now sit in probe scrap under `PORT_PLAY`; read with
  `--dump "32X SDRAM:0x38400:0x5D0:out.bin"`.

## 9. Instruments

| tool | what |
|---|---|
| `tools/rig_bars.py` | ticks/second and 1-vint share off a screenshot, ares or rig |
| `tools/fb_png.py` | DRAM + 32X CRAM dump → native 320x224 PNG (ares `--screenshot` is scaled and filtered) |
| `tools/sh2_profile.py` | SH-2 PC profile → symbols ranked by cycles |
| `tools/round_sets.py` | a round's colour sets, derived from the arcade corpus |
| `tools/round_split.py` | the static half as its own table, packed into 3 MD lines |
| `tools/attract_parity.py --port` | the arcade oracle — the only check that sees a tile wrong on BOTH machines |
