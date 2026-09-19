OUTPUT_ARCH(sh)
SEARCH_DIR(.)

/* The memory map look like this:
 * +--------------------+ <- 0x02000000
 * | .text              |
 * |        _etext      |
 * |        ctor list   | the ctor and dtor lists are for
 * |        dtor list   | C++ support
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * +--------------------+ <- 0x06000000
 * | .data              | initialized data goes here
 * |        _edata      |
 * +--------------------+
 * | .bss               |
 * |        __bss_start | start of bss, cleared by crt0
 * |        _end        | start of heap, used by sbrk()
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * |        __stack     | top of stack (for Master SH2)
 * +--------------------+ <- 0x0603FC00
 */

MEMORY {
	rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000
	ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00
}

PROVIDE (__stack = 0x0603FC00);

SECTIONS {
	.text 0x02000000 : AT(0x00000000) {
		*(.text .text.*)
		. = ALIGN(0x4);

		__CTOR_LIST__ = .;
		___CTOR_LIST__ = .;
		LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
		*(.ctors)
		LONG(0)
		__CTOR_END__ = .;
		__DTOR_LIST__ = .;
		___DTOR_LIST__ = .;
		LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
		*(.dtors)
		LONG(0)
		__DTOR_END__ = .;
		*(.rodata .rodata.*)
		*(.gcc_except_table .gcc_except_table.*)

		. = ALIGN(0x10);
		__INIT_SECTION__ = . ;
		*(.init)
		SHORT (0x000B)	/* rts */
		SHORT (0x0009)	/* nop */

		. = ALIGN(0x10);
		__FINI_SECTION__ = . ;
		*(.fini)
		SHORT (0x000B)	/* rts */
		SHORT (0x0009)	/* nop */

		_etext = .;
		*(.lit)
		. = ALIGN(0x4);
	} > rom
	_stext = SIZEOF (.text);

	/* Rebased 68K game image (unpair project): the FULL 256KB arcade
	 * image, every ROM reference +0x900000, executed by the 68K through
	 * the banked 0x900000 cart window (bank 3 -> cart 0x300000). Fixed
	 * placement: the delta is baked into the patched binary. */
	/* P3: MD-format mob sprite art at a FIXED cart offset so the 68K
	 * boot upload needs no link-time coordination (md_sprart_info.h
	 * hardcodes bank 2 / window offset 0xF0000). Sits in the gap
	 * between .text's end and the game's bank-3 image; the assert
	 * fails the link if .text ever grows into it. */
	.mdsprart 0x022F9100 : AT(0x002F9100) {
		KEEP(*(.mdsprart))
	} > rom
	ASSERT(LOADADDR(.ramtext) + SIZEOF(.ramtext) <= 0x002F8000,
	       ".text+.data+.ramtext ROM image spilled into .palscenes at 0x2F8000")

	/* PALSTATIC: per-scene PAL_SH images (pal_scenes.h) in the unused
	 * upper half of the mdsprart slot — SH-2 read-only const data,
	 * fetched through the cached cart view like the sprite art. */
	.palscenes 0x022F8000 : AT(0x002F8000) {
		KEEP(*(.palscenes))
	} > rom
	/* BAKED MD TILES (2026-09-17). tools/bake_tiles_md.py emits every
	 * tile already converted to MD 4bpp with its round's pen map applied,
	 * so md_emit_art becomes a 32-byte copy instead of 64 ROM reads + 64
	 * table lookups + 32 writes. Layout: 5 rounds x 128 u16 index
	 * (0xFFFF = set not baked), then 4KB blocks of 64 codes x FG,BG.
	 * PLACED EXPLICITLY in the measured free gap at 0x263C00-0x2F8000
	 * (593KB of 0xFF fill; 425KB needed). Not left to ld: the Makefile's
	 * own note is that a silent overrun corrupts the 68K high rom and
	 * "fails as a GAME bug a long way from its cause". */
	.tilesmd 0x02264000 : AT(0x00264000) {
		KEEP(*(.tilesmd))
	} > rom
	ASSERT(LOADADDR(.tilesmd) + SIZEOF(.tilesmd) <= 0x002F8000,
	       ".tilesmd overran the 0x263C00-0x2F8000 gap into .palscenes")

	.gamehigh 0x02300000 : AT(0x00300000) {
		KEEP(*(.gamehigh))
	} > rom
	/* SPRBAKE blob: the 768KB window above .gamehigh (bake_sprites.py
	 * budgets against exactly this). Cached cart view like .palscenes. */
	.sprbake 0x02340000 : AT(0x00340000) {
		KEEP(*(.sprbake))
	} > rom
	ASSERT(SIZEOF(.sprbake) <= 0xC0000,
	       ".sprbake blob overflows the cart at 0x400000")
	ASSERT(0x002F9100 + SIZEOF(.mdsprart) <= 0x00300000,
	       ".mdsprart blob grew into .gamehigh at 0x300000")
	ASSERT(SIZEOF(.palscenes) <= 0x8000,
	       ".palscenes overflows its slot before .gamehigh")

	.data 0x06000000 : AT(LOADADDR(.text) + SIZEOF(.text)) {
		*(.got.plt) *(.got)
		*(.shdata)
		*(.data .data.*)
		_edata = .;
		. = ALIGN(0x4);
	} > ram

	/* Hot renderer code copied ROM->cacheable SDRAM at boot (d32xr Lever 1).
	 * Placed contiguously after .data, with its load image contiguous after
	 * .data's in ROM, so the MARS header's single ROM->SDRAM copy (Source
	 * _stext, Size _sdata) carries BOTH .data and .ramtext over — no crt0
	 * change. _sdata is therefore the combined size. Functions opt in via
	 * __attribute__((section(".ramtext"))). */
	/* SESSION 7 task 0: .ramtext moved OUT of the low region to
	 * 0x06031000-0x06039000, the upper half of the old 64KB tile cache
	 * (CACHE_C is 512 slots now, m_main.c). The ROM image stays
	 * contiguous after .data; the MARS header copy (Size _sdata) carries
	 * .data only, and main_reset in mars_start.s copies .ramtext to its
	 * home (__ramtext_lma -> __ramtext_start, __ramtext_size bytes)
	 * before it releases the slave. This buys ~23KB under the 0x19000
	 * region guard for sbuf's neighbours. */
	.ramtext 0x06031000 : AT(LOADADDR(.data) + SIZEOF(.data)) {
		. = ALIGN(0x400);
		__ramtext_start = .;
		/* CARD CACHELOCK: the block copied into cache ways 0/1 and
		 * held there by CCR.TW. Must be 1KB-ALIGNED and a whole
		 * number of KB: a way is 1KB covering all 64 sets exactly
		 * once, so alignment is what makes set index == way offset.
		 * docs/design/CARD-CACHELOCK.md */
		__locktext_start = .;
		*(.ramtext.lock)
		. = ALIGN(0x400);
		__locktext_end = .;
		*(.ramtext .ramtext.*)
		. = ALIGN(0x4);
		__ramtext_end = .;
	} > ram
	__ramtext_lma  = LOADADDR(.ramtext);
	__ramtext_size = SIZEOF(.ramtext);
	_sdata = SIZEOF(.data);
	ASSERT(__ramtext_end <= 0x06038000,
	       ".ramtext overflows its 0x31000-0x38000 SDRAM slot (probe scrap 0x38000-0x39000, SPR_LAND 0x39000)")
	ASSERT(__locktext_end - __locktext_start <= 0x800,
	       ".ramtext.lock exceeds the two cache ways it is locked into (2048 B)")
	ASSERT(__ramtext_size > 0 && (__ramtext_size & 3) == 0,
	       ".ramtext size must be a non-zero multiple of 4 for the boot copy")

	.bss 0x06000000 + SIZEOF(.data) :
	{
		__bss_start = . ;
		*(.shbss)
		*(.bss .bss.*)
		*(COMMON)
		_end =  ALIGN (0x8);
		__end = _end;
		end = _end;
	} > ram

	/* MEMORY OVERLAY: buffers used ONLY at startup/menu and DEAD during gameplay,
	 * parked ABOVE _end (just under the Master stack at 0x0603F000). This keeps
	 * _end -- the protected .bss boundary the stack must never cross -- ~71 KB
	 * lower, so the render stack has ~75 KB of room; its worst-case spill lands in
	 * these dead buffers, never in live data. NOLOAD: not in ROM, not zero-inited
	 * by crt0 -- each buffer is scratch, filled before use. */
	.hero_overlay (NOLOAD) :
	{
		. = ALIGN(0x8);
		*(.hero_overlay)
	} > ram

	.eh_frame 0 (NOLOAD) :
	{
		*(.eh_frame)
	}
}
