! THE PER-FRAME SYSTEM 16 I/O CAPTURE, built by tools/l2_io_pack.py from the same
! playback as the work-RAM captures (tools/l2_oracle.lua, MAME's ioport layer -- no
! bus access, so no input latch is consumed).
!
! WHY IT EXISTS. A frame's inputs are the work-RAM seed PLUS the controller and dip
! state. Only the first was ever captured, so the port's MMIO stub returned a
! constant and every input-derived cell was wrong by construction: 9 of the 19 cells
! the rotated real frame gets wrong at the seed frame (FULL-PORT.md 38), written by
! FUN_00001366 as `DAT_ffffe028 = ~DAT_00c41003`. The gate was paying the hand-rolled
! four-call body for not reading the controller at all.
!
! Format (big-endian, read in place by sh_src/port_l2.c):
!   u32 'L2IO'; u32 nframes; u32 nports; u32 port[nports];
!   then per frame nports bytes, padded to 4.
	.section .portdata,"a"
	.align 2
	.global _l2_io
_l2_io:
	/* PATH COMES FROM -I (L2DIR), not from this line: GNU as searches the
 * assembler include path for .incbin, so a second capture is selected by
 * `make ... L2DIR=docs/proof/l2attract` with no edit here. The canonical
 * names are seed.bin / deltas.bin / io.bin in each capture directory. */
	.incbin "io.bin"
