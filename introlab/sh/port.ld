/* The launcher linked for its cart page inside the port (make port).
 *
 * rom: the page as slot 7 shows it. The page's first 0x3A620 bytes are the port's music data
 *      (every page shown in slot 7 carries it: the game's 68K reads music through that window
 *      on every vblank), then tools/launcher_page.py's 16-byte header, then this program.
 *      The port's Makefile (LAUNCHER_BASE) and that tool check the address against the table.
 * ram: borrowed from the game until LAUNCH: 0x06019000-0x06027000, where the port keeps its
 *      copies of the arcade's tile map and text layer. Both are all zero before the game's
 *      first tick (ares, base rom, frame 13) and the loader hands them back zeroed. The
 *      palette copy and the renderer's tables from 0x06027000 up are filled during boot
 *      and are not touched.
 * stacks: the port's coroutine stack, 0x0602A800-0x0602C600, which the game paints when it
 *      starts (sh_src/mars.ld .portstack). */
OUTPUT_ARCH(sh)
MEMORY {
	rom (rx) : ORIGIN = 0x023BA630, LENGTH = 0x000459D0
	ram (wx) : ORIGIN = 0x06019000, LENGTH = 0x0000E000
}
SECTIONS {
	.text : {
		KEEP(*(.lhead))
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > rom
	.ramtext : {
		_lram_start = .;
		*(.ramtext .ramtext.*)
		*(.data .data.*)
		. = ALIGN(4);
		_lram_end = .;
	} > ram AT> rom
	_lram_load = LOADADDR(.ramtext);
	.bss (NOLOAD) : {
		_lbss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		_lbss_end = .;
	} > ram
	_lborrow_start = ORIGIN(ram);
	_lborrow_end = ORIGIN(ram) + LENGTH(ram);
	_lstack_primary = 0x0602C600;
	_lstack_secondary = 0x0602B600;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
