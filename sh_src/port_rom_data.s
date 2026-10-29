! The REAL arcade program image, for the port's constant reads.
!
! NOT objcopy. `sh-elf-objcopy -I binary -O elf32-sh -B sh2` produced a section of
! the right SIZE and the right FLAGS (CONTENTS, ALLOC, LOAD, READONLY, DATA) with
! NO DATA IN IT -- measured 2026-09-28: the source has 223,663 nonzero bytes of
! 262,144 and the object had 200 nonzero bytes of 262,616, all of them ELF header.
! The symptom was every ROM table in the port reading zero, which sent 128 of one
! frame's indirect calls to address 0x00000, silently.
!
! .incbin is what every other blob in this tree uses (game_high_data.s,
! port_l2_data.s) and it demonstrably works -- .portdata's bytes arrive.
!
! NOT .gamehigh: that image is REBASED (+0x900000 on every ROM reference) for the
! 68K's bank window, so its operand bytes differ from what the decompiled code
! expects to read as DATA.
	.section .portrom,"a"
	.global _port_rom_image
	.align 2
_port_rom_image:
	.incbin "roms/altbeast/prog68k.bin"
