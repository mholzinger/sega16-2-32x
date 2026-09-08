! Rebased 68K game image (unpair project, NOTES.md "REBASE DESIGN v2"):
! full 256KB arcade image with every ROM reference +0x900000, executed
! by the 68K through the banked 0x900000 cart window (0xA15104 = 3,
! cart offset 0x300000). Placement fixed by mars.ld (.gamehigh).
	.section .gamehigh,"a"
	.incbin "game_high.bin"
