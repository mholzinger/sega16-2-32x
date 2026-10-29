! Rebased 68K game image (unpair project, NOTES.md "REBASE DESIGN v2"):
! full 256KB arcade image with every ROM reference +0x900000, executed
! by the 68K through the banked 0x900000 cart window (0xA15104 = 3,
! cart offset 0x300000). Placement fixed by mars.ld (.gamehigh).
!
! NOGAMEHIGH=1 OMITS IT, AND THAT IS WHAT MAKES A PLAYABLE PORT BUILD FIT.
!
! Under the SH-2 port the 68000 does not execute the game at all -- it keeps the
! i8751 shim, the VDP and the framebuffer handoff, and the game logic runs on the
! SH-2 from .portrom + .portcode. So this 256 KB rebased image is dead weight there,
! and it is EXACTLY the 256 KB a playable port build needs. Measured 2026-10-03:
!
!     .text (incl. tiles.bin 1024 + sprites.bin 1024)   2446 KB
!     .tilesmd 506   .sprbake 665   .ramtext 27   misc 24
!     .portrom 256   .portcode 420
!                                                     --------
!                                                       4344 KB  vs the 4 MB cart
!     minus .gamehigh                                   - 256
!                                                     --------
!                                                       4088 KB  -> FITS, 8 KB spare
!
! docs/design/PORT-OUTPUT.md 4 had this as "~290 KB OVER". That costing listed
! `.text` AND the art blobs separately when `.text` already CONTAINS the 2 MB of raw
! art, and it never counted .gamehigh as a saving. The honest number is that it fits.
	.ifndef NO_GAMEHIGH
	.section .gamehigh,"a"
	.incbin "game_high.bin"
	.endif
