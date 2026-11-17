/* The launcher's 68K smoke program: one flat image, linked at 0 and run wherever it lands
 * (md/smoke68k.c, -mpcrel). 'SM', the entry's offset, the part copied to RAM, the rest;
 * the picture is appended after it by tools/gen_wisps.py. */
OUTPUT_FORMAT("binary")
SECTIONS {
	. = 0;
	.head : { SHORT(0x534D) SHORT(smoke_entry) }
	.wave : { wave_start = .; *(.wave) . = ALIGN(2); wave_end = .; }
	.text : { *(.entry) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(2); }
	smoke_pic = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
	ASSERT(SIZEOF(.wave) + 128 <= 0x1B8, "the wave and its steps must end below the tick slot at +0x1B8 (the room's state follows to +0x280)")
	.data : { *(.data .data.* .bss .bss.* COMMON) }
	ASSERT(SIZEOF(.data) == 0, "the program is run in place from the cart: no writable data")
}
