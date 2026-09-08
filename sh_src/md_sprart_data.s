! P3 M1 — MD-format mob sprite art (tools/bake_mdspr.py).
! Pinned at cart 0x2F9100 (after .palscenes; the old low gap is consumed by the image) by mars.ld (.mdsprart): the 96KB gap between
! _etext and the game's bank-3 image. The 68K uploads it to VRAM 0x8000
! at boot through the 0x900000 window (bank 2, offset 0xF0000) before
! handing the window to the game. The SH-2 never reads it — the claim
! pass uses the generated index (md_sprart.h), not the pixels.
	.section .mdsprart, "a"
	.global _md_sprart_blob
_md_sprart_blob:
	.incbin "sh_src/md_sprart.bin"
