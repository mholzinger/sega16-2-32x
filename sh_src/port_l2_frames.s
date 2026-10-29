! MULTI-FRAME L2 GATE: the per-frame delta blob built by tools/l2_pack.py from 31
! consecutive captures of `mame altbeast -playback ab_full` (frames 9000-9030).
!
! WHY A BLOB AND NOT MORE .incbin CAPTURES. .portdata is 32 KB (mars.ld
! 0x02380000 -> 0x02388000) and the two full images below already fill it. A frame
! changes 28-48 of 16,384 bytes, so 30 further frames cost 4,764 bytes as deltas.
!
! Format (big-endian, read in place by sh_src/port_l2.c):
!   u32 nframes; u32 idx[nframes]; then per frame u32 count, count x (u16 off, u16 val)
! Offsets are from 0xFFC000 and each frame's records are SORTED, which is what lets
! the driver reconstruct expected(k) in one ascending pass with no 16 KB buffer --
! there is ~3 KB of headroom under the region guard at 0x06019000.
	.section .portdata,"a"
	.align 2
	.global _l2_frames
_l2_frames:
	/* PATH COMES FROM -I (L2DIR), not from this line: GNU as searches the
 * assembler include path for .incbin, so a second capture is selected by
 * `make ... L2DIR=docs/proof/l2attract` with no edit here. The canonical
 * names are seed.bin / deltas.bin / io.bin in each capture directory. */
	.incbin "deltas.bin"
