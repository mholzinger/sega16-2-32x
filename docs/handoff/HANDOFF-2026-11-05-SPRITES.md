# HANDOFF 2026-11-05 23:30: the enemy sprite defect on the FPGA, and what this thread got wrong

**Follow-up 2026-11-06:** The baked copy's bank bits on transparent bytes are a
measured cause, repaired inadcaa3e7. Read `SPRITE-PIXEL-AUDIT.md` and current STATE
before acting on this historical baseline. Normal p15bpt7 is the new play
candidate; selected arcade-command fixture pens pass exactly on FPGA in three
calibrated captures. Mike subsequently confirms transparency fixed on p15bpt7
(2026-11-06). Separate gameplay/boss coverage and cadence remain open. Do not
re-push p15bpt4 merely because the old recipe below says to do so.
The current profiling pass is `docs/performance/VINT-HOTPATHS-2026-11-06.md`;
normal p15bpt7 is restored on the rig and no speed optimization is promoted.

Written for the next thread. Everything numeric here has a file; everything I judged by
looking at thumbnails is marked UNVERIFIED and must not be relied on. Mike's closing words to
this thread: "you can't tell; this means you are hallucinating." He is right about the
pictures: I compared sprites by eye at thumbnail scale and called them identical three times.

## State of the line

- CANDIDATE: `build/port/rom/p15bpt4.32x` (build 4214a35f, worktree advisor-bisect, branch
  soundlab). On the rig it was p15bpt5 at 23:00 (the same rom minus bake window 1, Makefile
  `ARTBAKENOWIN1=1`); it showed the same defect, so window 1 is excluded. Re-push p15bpt4:
  `tools/mister_push.sh --name mister.office.local build/port/rom/p15bpt4.32x`.
- Flags that made it: `make candidate NAME=p15bpt4 EXTRA="CARTSSF=1 R60SOUND=1 R60PSGMUSIC=1
  R60PTONE=1 R60SPANCAP=1536 R60TXTPOOLHYB=1 R60BAKEART=1 R60BAKEPAGES=1 R60PUNCHTHRU=1"`.
- CLOSED today by Mike on the rig: the beast-transformation seam (STATE "beast transformation",
  commits 16a1f005 and 1572efd7). Measured, not eyeballed: R60_DIAG[15] span-timeout closes,
  r60_nt_shadow vs VRAM, packet decodes, per-frame luma vs the arcade.
- OPEN, UNLOCATED: "sprites for enemy zombie and the boss battle and the head, all broken"
  (Mike, p15bpt4 on the FPGA, 22:35; same on p15bpt5). His screenshots:
  `screenshots/20261105_2231*-p15bpt4.png` and `20261105_2232*-p15bpt4.png` in the main
  checkout. Earlier wordings of the same report: "zombies slightly transparent, same as parts
  of the boss, the floating head has jagged sprite opacity" (p15bpt2, ares and rig, 11-05 10:17).

## What is measured (trust these)

1. The level-1 tape probe `p15bpt2ft6` (PORTGODMODE PORTFORM=3 PORTFORMTICK=768 R60PFFORM=1
   PORTTAPEPAD) draws zombies on the FPGA; rig shots in `build/accept/rig/cut_zombies/` and
   `cut_stones/`. Whether they are RIGHT is unverified: Mike watched the cut on that rom, not the
   zombies, and my "identical to ares" was by eye.
2. Bake window 1 (cart 0x02375000, 45 KB) is not the cause: p15bpt5 without it shows the defect.
3. ares, candidate code, level1_form tape, frames 1300-1339: the composite changes between
   consecutive frames are small (mean luma < 12, the 4-frame beat is the smoke animation);
   scratch `thru/seq/`. The framebuffer's sprite pixel counts swing by half between frames, but
   that is the WRITE bank mid-pass; the shown bank is complete. Numeric.
4. Through-pen audit (R60PUNCHTHRU): every framebuffer pixel of value 255 categorised by the S16
   priority bit and the MD art under it (scratch `thru/`, `thru_g/`): under opaque priority pixels
   or, a few hundred a frame, under transparent ones in the grass. Whether the latter sit on
   sprite pixels was judged by eye: UNVERIFIED. The punch's mask cache (PRIO_TAG/PRIO_MASK at
   SDRAM 0x3E400/0x3C400) matches the cart art for the grass tiles, bank 1. Numeric.
5. No stray pen-15 or bank-pen-0 sprite pixels in the framebuffer (the text pool's pens are the
   HUD glyphs). Numeric.
6. The arcade's level-1 walking zombies are drawn every frame (sprite table dump
   scratch `arc_spr.bin`, frames 1650-1661: slots 2-7 present every frame, no hide bit); the
   white wolf alternates palette 7/8 one frame in four. The round-clear beam over Neff's face
   is the arcade's own solid/checker alternation (memory round-clear-beam-60hz, measured 09-05).
7. In play on ares the old code logged 0 span-timeout closes over 30000 frames of the god tape
   and shipped 32-row strip packets at the four page advances (frames 3726, 8492, 12649, 19769).
   The claim that those strips time out on the FPGA and explain the sprites is INFERRED, never
   measured on the rig, and Mike's rig pass of p15bpt4 says the sprites are still broken, so
   treat it as wrong.

## What I claimed by eye (do not trust)

- "static zombies in the open: rig shot = ares frame = arcade, pixel-alike" (STATE 11-05).
- "Mike's rig zombie and the arcade's walking zombie are the same art" (23:15, the file
  `screenshots/compare_zombie_rig_vs_arcade.png`). Mike: "you can't tell".
- "the through pixels under transparent grass lie where no sprite is on the arcade".
- Both earlier closures of this item in STATE (flash cadence; the page-advance timeout).

## How to locate it without eyes

The arcade is the oracle and the comparison must be pixel-exact at the same game tick:

1. Build a reference sprite layer for a frame from the ARCADE's own sprite table and the
   sprite ROM (tools/art_census.py `decode(bank, addr, pitch, flip, h)` decodes a frame;
   tools/gameplay_dump.lua `GD_SPRDUMP` dumps the 1 KB sprite table per frame; the record layout
   is in tools/play_pairs.py's docstring: end e[2]&0x8000, hide e[2]&0x4000, top/bot e[0],
   x=(e[1]&0x1FF)-184, pitch e[2]&0xFF, off e[3], pal e[4]&0x3F, bank (e[4]>>8)&0xF, zoom
   e[5]&0x3FF). Render it as (x, y, palette, pen) per pixel.
2. Take the port's shown framebuffer bank at the same tick (ares `--dump "32X DRAM:0:0x40000"`,
   line table in the first 0x200 bytes, shown bank = the one whose contents did not change since
   the previous frame; the pen tables map (bank, pen) to a palette entry) and compare pen by pen
   inside every sprite's box. Report counts per sprite: pixels the arcade draws that the port
   does not, and the reverse. Zero is the bar. This is the "object table exactness" discipline
   applied to pixels.
3. Do it first in ares on the route-matched god rom (`make parity-candidate NAME=<x>g TAPEPAD=1`,
   tools/inputs/level1_full.csv, the port follows the arcade tick for tick to the Aggar fight,
   arcade shots at frame+33 in scratch `g_arc/`), then on the rig with the same tape rom and
   tools/rig_cut.sh (the rig's screenshot cadence is ~7 s; align by the hero's x in WRAM is not
   available there, so match the HUD score instead).
4. If ares is exact and the rig is not, the defect is FPGA-side (candidates in order: the
   overwrite-image byte semantics of the sprite plot on the MiSTer, the CRAM commit under PEN
   on the MiSTer (memory mister-palette-pen), the uncached FB read cost changing pass timing).
   If ares is not exact either, it is ours and the pen-by-pen counts say which sprites.
5. Ask Mike for a phone video of the TV if the defect is temporal; a still cannot show a
   cadence fault and I wasted an evening proving that.

## Housekeeping

- The rig keeps the last rom launched. After any probe: `RIG_RESTORE=<candidate> tools/rig_cut.sh`
  or push the candidate by hand, and read `/var/log/ACTIVEGAME` before naming the loaded rom.
- Scratchpad of this thread (temporary, not in the repo):
  `/tmp/sega16-2-32x/7a3c1577-6f7f-4d91-8437-57121fea1163/scratchpad/`
  (`thru/`, `thru_g/`, `thru/seq/`, `g_arc/`, `g_port/`, `arc_spr.bin`, `bpt4_ares/`).
- Commits today on soundlab (advisor-bisect): dad4e601 .. 0ad36c29 and this file.
