# Scene-keyed tables: the costing, before any build (2026-10-10)

The four-line card closed as built-not-landed: at matched game frames the additive table reads
demo scene +4.5 and demo 2 +13.5 against the control, and both demo anchors are what it existed
for (STATE, and `port/mdlines4` a72523fd). Its one real gain was on the STATIC half -- logo
rewrite -28.5, cut black -21.0, face -17.9 -- which is allocator headroom, not the demos.

So: does keying the table on the scene byte fix the demo half? **No, and the numbers say why in
one line: the demo half needs four lines.** This page is the costing Mike asked for first.

## 1. The key is binary, so "scene-keyed" means exactly two tables

`0xFFF022` over the round-0 corpus (497 frames, `tests/demo-b/arcade`):

| key | frames | range | what it is |
|---|---|---|---|
| 0x09 | 437 | 456..2100 | both demos |
| 0x0C | 59 | 19..2190 | title, high score, post-demo |
| 0x00 | 1 | 18 | the boot card |

There is no finer per-scene key inside the demos. Demo A and demo B share 0x09.

## 2. What each half needs, derived from the corpus, not argued

`round_split.py`'s own corpus walk and exact packer, keyed each way:

| half | sets | distinct colours | colour groups | smallest fit |
|---|---|---|---|---|
| demo (0x09) | 32 | 44 | 12, sized [8,8,8,8,7,7,7,6,6,5,4,2] | **4 lines [15,15,13,11]** |
| static (0x0C) | 39 | 31 | 8, sized [8,8,8,6,6,5,3,1] | **3 lines [15,14,6]** |

**The demo half does not fit three lines, so a demo-keyed table cannot leave line 4 free as
headroom.** 44 colours against 45 slots is not the obstacle; the GROUPING is -- four groups of
8 and three of 7 against a 15-colour line.

And the sharp form of it: the demo half fits three lines **if and only if it omits sets 19, 20
and 21** -- that is today's table, 30 sets in [14,14,15], and omitting them is the defect the
card existed to fix (674,851 cells drawn as backdrop). Adding them forces a fourth line, and a
fourth line is what measured worse on both demo anchors.

## 3. So the only design the measurements support is the inverse of the card

Give the FOUR lines to the half that gained from them and leave the demos on today's table:

- static half keyed to its own table, four lines (or three plus line 4 as headroom)
- demo half unchanged: round 0's existing 30-set, three-line table
- sets 19/20/21 stay backdrop in the demos -- the pre-existing defect, NOT fixed by this

Expected gain, from the nine-anchor table: logo rewrite -28.5, cut black -21.0, face -17.9.
Expected demo cost: zero, because the demo path does not change. The 4.65%-more-vints price
also mostly goes away, since the extra CRAM line is only live under the static key.

## 4. The switch costs, each one priced

| item | cost | note |
|---|---|---|
| table ROM | ~1.5 KB per extra row | 64+64 words `line_c`/`pen_ring`, 128 `s_line`, 128 `s_used`, 1024 `s_map` |
| install at the key change | ~1,088 inner iterations, twice per attract cycle | `m_main.c:4234`, MDP_LINES*16 pens + 128 sets x 8; microseconds in the window |
| art index | **+256 B per table** | one row of 128 sets x 2 B; `TILESMD_INDEX_BYTES` 2560 -> 2816 |
| static-half ART | **+42 blocks = 86,016 B** | masks inherited (36 single-variant + 3 both); **78 blocks = 159,744 B without** |
| tables 5-9 art skipped | **-64 blocks = -131,072 B** | `TILESMD_SKIP=5,6,7,8,9`; their index rows stay 0xFFFF, which is what an unassigned set already reads |

**A BLOCK IS 2 KB, NOT 4 KB.** An earlier version of this page doubled every art figure. The
blob is `TILESMD_INDEX_BYTES + blocks x 2048`, and the three states, each measured off
`tools/bake_tiles_md.py --emit` rather than computed:

| state | index | blocks | bytes | against the 552,960 gap |
|---|---|---|---|---|
| today | 2560 | 252 | 518,656 | fits, 34,304 spare |
| + table 10, masks inherited | 2816 | 294 | 604,928 | **over by 51,968** |
| + tables 5-9 skipped | 2816 | 230 | **473,856** | **fits, 79,104 spare = 38 blocks** |

604,928 - 518,656 = 86,272 = 256 B of index for table 10's own row plus 42 x 2,048 of art;
there is no unaccounted byte.

So the art DOES fit, by 38 blocks, and it fits only because of two things that had to be
written: the mask inheritance (without it table 10 is 78 blocks and the margin is 4 blocks)
and the 5-9 skip.

## 5. What this page does not claim

It does not say the static-half gains survive being keyed rather than four-lined: they were
measured on a rom where BOTH halves had four lines. Separating them is the build, and its
first gate is the nine-anchor table against the same control.

Two of the nine cells are soft and stay soft for any future reading here: cut black and demo
scene sit where the arcade's game-frame counter advances two frames across twenty-one, because
the display is blanked across the cut.

## 6. Ordered after this

The picture axis' real number is the standing deficit, not the tables: at matched game frames
the CONTROL reads eye **55.1** and eye pan **49.0** mean luma against the arcade. Nothing in
the four-line card moved it, and no colour-table work has touched it. Instrument:
`tools/anchor_nine.py`.
