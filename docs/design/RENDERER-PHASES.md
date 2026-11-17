# R60 renderer pass and ownership map

This is the navigation guide for the renderer in `sh_src/r60/r60_main.c`. It
describes the accepted Altered Beast line built by `tools/build_line.sh`. That
line combines the Makefile's `R60_LINE_FLAGS` and `CANDIDATE_FLAGS` with the
working-set, snapshot-lease and prepared-sprite options pinned by the script.
The mapper-page ownership protocol is documented separately in
[WORKING-SET-LIFECYCLE.md](WORKING-SET-LIFECYCLE.md).

The code is not a simple sequence of independent filters. Clearing uses history
from prior sprite passes, text clearing must precede sprites, foreground priority
is restored inside the sprite loop, and publication is coupled to the 68K consume
span. Preserve that ordering unless a new design proves a replacement contract.

## Vocabulary

| Prefix or term | Meaning |
| --- | --- |
| `bk_` | Baked unscaled sprite-art lookup and copy path |
| `sp_` | Prepared sprite packet: baked pixels with palette-bank bits applied |
| `ks_` | Keep-sprite state retained independently for each framebuffer bank |
| `cp_` | Clear plan prepared while the slave would otherwise wait |
| `cx_` | Exact per-row clear extents |
| `prio_`, `pr_`, `pm_` | Foreground-priority state, row form and mask form |
| `txt_` | Text shadow, pending cells, clear and redraw state |
| `ac_` | Older repeated-art cache experiment, separate from prepared packets |
| punch | Restore foreground priority over sprite pixels; it is not collision logic |
| hold | A display/packet state that can suppress drawing or flipping; see the local contract before each use |

`bkp` is the physical framebuffer bank being written. Local names such as `bk`,
`bank` and `bank2` are aliases scoped to one phase; they do not identify a
different framebuffer.

## Master timeline

The master owns simulation and publishes renderer inputs; it does not normally
draw the accepted line's slave-owned framebuffer pass.

1. Run the queued native game tick through `port_play_tick()`.
2. Copy the resulting sprite table into the snapshot slot not owned by the
   slave, then advance `R60_SNAPSEQ`.
3. Prepare plane-transfer work with `r60_planes_prepare()`.
4. Stay out of the VDP window while the slave draws and publishes.

The optional `R60_MBAND` path is different: after the slave posts a band
descriptor and `R60_CLEARED`, the master draws the rows above the split and posts
`R60_MDONE`. It is not enabled by the accepted Altered Beast line.

## Slave pass timeline

`s_main()` repeats this pass:

1. **Accept input.** Close any prior 68K consume span, wait for the pending flip
   state to be safe, claim the newest snapshot and bind its working-set page.
2. **Prepare decisions.** Begin the prepared-packet epoch, heal text needed by
   keep decisions, decide which old sprites can remain, and optionally plan the
   older art cache.
3. **Clear stale sprite pixels.** Clear prior row spans for this physical bank.
   Kept spans survive; overflow safely expands to a full-bank clear.
4. **Clear stale text.** Erase changed or displaced glyphs before sprites. Moving
   this after sprite drawing erases sprite pixels inside glyph boxes.
5. **Plot sprites and restore priority.** For each sprite, try baked/prepared art
   and fall back to the established plotter. Immediately punch its foreground
   priority region, or use an enabled in-plot mask form. There is no independent
   whole-frame priority phase.
6. **Draw text.** Redraw pending text in front of the completed sprite layer.
   This consumes the clear phase's row and horizontal-extent records.
7. **Publish and present.** Publish plane packets, commit CRAM, request or defer
   the framebuffer flip, open the 68K consume span, and finally echo the pass in
   `R60_DONE`.

## Control words

These words live in uncached SDRAM because they coordinate processors and the
68K adapter.

| Word | Contract in the accepted line |
| --- | --- |
| `R60_SNAPSEQ` | Latest completely published master snapshot sequence; its low bit selects the slot |
| `R60_DRAWSEQ` | Snapshot sequence claimed by the slave; protects its slot from overwrite |
| `R60_GO` | Pass sequence currently leased to renderer work |
| `R60_DONE` | Last pass whose slave work is complete |
| `R60_PUB` | Pass whose plane packet has been published |
| `R60_SHOWN` | Logical pass selected for presentation; a deferred flip may not have landed yet |
| `R60_FPEND` | A framebuffer flip was requested and still needs confirmation |
| `R60_FMDOWN` | The 68K packet-consume span is open |
| `R60_SEEN` | `COMM0` value captured when that span opened |
| `R60_SPANF` | Pass whose consume span most recently closed |
| `R60_CLEARED` | Optional MBAND only: clear is complete and the band descriptor is published |
| `R60_MDONE` | Optional MBAND only: master-side band work is complete |

`R60_SHOWN`, the tracked write bank and the hardware FS bit are deliberately not
synonyms. Deferred-flip and hold paths can advance or retain them at different
moments.

## Ordering constraints

- Snapshot claim must precede mapped-page and prepared-cache access.
- Text heal and the pending-cell snapshot must precede keep decisions.
- Keep decisions must precede clearing because they decide which old spans survive.
- Text clear must precede sprite plotting; text redraw must follow it.
- Priority restoration is coupled to each sprite's fresh box or row spans.
- Plane publication precedes CRAM commit and flip request.
- An FS write while the 68K consume span owns the VDP window can be lost; the
  deferred path flips only after the acknowledgement closes that span.
- Section placement and scratch aliases are load-bearing. Do not extract hot
  helpers, inline `noinline` functions, or regroup state without checking the
  linker map and exact ROM bytes.

Performance measurements and the prepared-packet proof are in
[RENDER-PREPARATION.md](RENDER-PREPARATION.md) and the linked performance reports.
