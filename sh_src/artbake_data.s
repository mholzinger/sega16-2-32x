! THE BAKED ART, windows 1-3 (R60BAKEART, 2026-11-04). tools/bake_art.py packs the sprite
! frames the plotter spends the most ROM words on, pre-decoded into longword groups
! (tools/s16art.py), into the SH-2-readable gaps of the CARTSSF layout; the index (keys ->
! SH-2 addresses) lives at the head of window 0, the tail of the music page behind
! sh_src/sound.bin in sh_src/pages_data.s. The windows are pinned by tools/gen_ld_ssf.py and
! the Makefile hands the same addresses to the bake as --chunks.
!
! Linked ONLY under R60BAKEART=1 (the Makefile filters this object out of the wildcard, as
! it does sprbake_data.o): the blobs are Sega-derived and gitignored.
! window 1 is absent on a PORTTAPEPAD build (ARTBAKE_NOWIN1): the input tape takes .portcode past
! 0x375000, and an EMPTY output section pinned inside .portcode's range wiped the rom image from
! 0x375000 to the next section (p15bneff, 2026-11-04: the tape pad read 0xFF, the SH-2 ran wild).
! No input section = ld drops the output section.
.ifndef ARTBAKE_NOWIN1
		.section .artbake1, "a"
		.align	4
		.global _artbake_win1
_artbake_win1:
		.incbin "artbake_1.bin"
.endif

		.section .artbake2, "a"
		.align	4
		.global _artbake_win2
_artbake_win2:
		.incbin "artbake_2.bin"

		.section .artbake3, "a"
		.align	4
		.global _artbake_win3
_artbake_win3:
		.incbin "artbake_3.bin"
