! THE L2 SEED: 68K work RAM 0xFFC000-0xFFFFFF at the capture frame (9000) of
! `mame altbeast -playback ab_full`, taken by tools/l2_oracle.lua. 16384 bytes,
! big-endian as the 68K left them -- which is why the L2 driver is a rom and not a
! host program.
!
! THERE IS NO _l2_after ANY MORE. It was the next frame's full image, and it is
! exactly this seed with delta frame 1 applied (sh_src/port_l2_frames.s), so it
! cost 16 KB of a 48 KB window that the delta chain needed. The one-frame ladder
! scores through the same l2m_score() as every other frame.
	.section .portdata,"a"
	.global _l2_before
	.align 2
_l2_before:
	/* PATH COMES FROM -I (L2DIR), not from this line: GNU as searches the
 * assembler include path for .incbin, so a second capture is selected by
 * `make ... L2DIR=docs/proof/l2attract` with no edit here. The canonical
 * names are seed.bin / deltas.bin / io.bin in each capture directory. */
	.incbin "seed.bin"
