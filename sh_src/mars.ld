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
	/* 8 MB, NOT 4. The 4 MB cap was OURS, not the hardware's: the MiSTer core decodes
	 * the SH-2's cart select over 0x02000000-0x03FFFFFF (IF.sv:782) and its loader
	 * carries rom_sz as [24:0] with the download path on ioctl_addr[23:1], i.e. 16 MB
	 * (S32X.sv:766,1067). Mike's call, 2026-10-20.
	 * WHAT MAY GO ABOVE 0x02400000: SH-2-ONLY sections. The 68K sees cart only at
	 * 0x000000-0x3FFFFF direct and 0x880000-0x9FFFFF through its window (IF.sv:780-781),
	 * so .text, .tilesmd, .mdsprart, .sounddata, .sfxdata and everything the MD side
	 * reads MUST stay in the low 4 MB. .portrom, .portdata and .portcode are read only
	 * by the SH-2, so they moved up -- which also hands the whole 0x02340000-0x02400000
	 * run back to the art and audio lanes, which could not previously coexist. */
	rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00800000
	ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00
}

PROVIDE (__stack = 0x0603FC00);

SECTIONS {
	.text 0x02000000 : AT(0x00000000) {
		/* EXCLUDE_FILE, not a later section, because ld's FIRST matching pattern
		 * wins: without this the 150 KB PORTFULL port lands here and .text runs
		 * straight into .tilesmd at 0x26A000. It belongs in .portcode. */
		EXCLUDE_FILE(sh_src/portfull/*.o) *(.text .text.*)
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
	ASSERT(__romimg_end <= 0x002F8000,
	       ".text+.data+.ramtext ROM image spilled into .palscenes at 0x2F8000")

	/* PALSTATIC: per-scene PAL_SH images (pal_scenes.h) in the unused
	 * upper half of the mdsprart slot — SH-2 read-only const data,
	 * fetched through the cached cart view like the sprite art. */
	/* THE SOUND BLOB (2026-10-18, R60SOUND): cart 0x260000..0x2F8000 was the MD tile blob's
	 * room (.tilesmd, the old 68K art path, dead under r60: NOTILESMD=1). The 68K reads it
	 * through bank 2 of the 0x900000 window at 0x960000. */
	ASSERT(__romimg_end <= 0x00260000,
	       ".text+.data+.ramtext ROM image reached .sounddata at 0x260000")
	.sounddata 0x02260000 : AT(0x00260000) {
		KEEP(*(.sounddata))
	} > rom
	ASSERT(ADDR(.sounddata) + SIZEOF(.sounddata) <= 0x022F8000,
	       ".sounddata overflows into .palscenes at 0x2F8000")

	/* THE SFX BANK (R60SFX): the arcade's YM effects as PWM voices, in the free run between
	 * .mdsprart's end (0x2FE2E0) and .portrom (0x340000). */
	.sfxdata 0x022FE400 : AT(0x002FE400) {
		KEEP(*(.sfxdata))
	} > rom
	ASSERT(ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x02400000, ".sfxdata overflows past the low 4 MB (the 68K-visible region)")

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
	/* TESTCART MOVES THE BLOB UP A PAGE, and only TESTCART (2026-09-27).
	 * Defining TEST_CART for the SH-2 grows .text by ~12 KB, which pushes the
	 * .ramtext ROM image from 0x267164 to 0x26A144 -- 324 bytes past this
	 * blob. The suite's own SH-2 code is NOT the cause: .ramtext is byte for
	 * byte the same size in both builds (0x6E60) and every TEST_CART function
	 * is in .porttest; it is the LOAD ADDRESS that moves, because .text grew
	 * underneath it.
	 * So the blob and .porttest step up 0x1000 when --defsym __testcart=1 is
	 * passed, and the SHIPPING LAYOUT IS BYTE-IDENTICAL to before. Safe here
	 * because TILESLIM is not wired in this build -- tools/check_tilesmd_addr.py
	 * says "md_main.c has no slim-path literal", so nothing outside this file
	 * depends on the address. Wire TILESLIM and that stops being true. */
	/* MOVED 0x26A000 -> 0x26C000 UNDER PORTFULL ONLY, ON 2026-10-03, to make a
	 * FULL-ART PORT BUILD LINK -- and conditional because THE LINE MUST NOT MOVE.
	 * Shifting a 506 KB blob by 8 KB changes the cart address of everything after it,
	 * and this project has MEASURED 64 bytes of dead .data moving the level-1 speed
	 * ladder 18 points (LESSONS, "speed gate resolution floor"). The accepted line
	 * (rg2) stays byte-identical; only a PORTFULL rom sees the new address.
	 * `.ramtext`'s ROM image had grown to 0x26AACB and overlapped this blob by 2,763
	 * bytes once the port's driver joined .text. The gap between .ramtext's end and
	 * .porttest at 0x2E9000 was too small for a 0x7EA00 blob by about 1.2 KB, so
	 * .porttest moved up to 0x2F0000 as well -- it is a PROBE section, empty unless
	 * PORTTEST=1, and 32 KB is still more than tools/gen_selftest.py asks for.
	 * tools/check_tilesmd_addr.py enforces the agreement with md_main.c's three
	 * literals FOR BOTH CASES; its regex had rotted into a no-op and was fixed
	 * first, and it had also been reading only the VMA while the AT() -- the clause
	 * that actually decides where the bytes land -- sat on the next line. */
	.tilesmd (DEFINED(__portfull) ? (DEFINED(__testcart) ? 0x0226D000 : 0x0226C000)
	                              : (DEFINED(__testcart) ? 0x0226B000 : 0x0226A000)) :
	    AT(DEFINED(__portfull) ? (DEFINED(__testcart) ? 0x0026D000 : 0x0026C000)
	                           : (DEFINED(__testcart) ? 0x0026B000 : 0x0026A000)) {
		KEEP(*(.tilesmd))
	} > rom
	ASSERT(LOADADDR(.tilesmd) + SIZEOF(.tilesmd) <= 0x002F8000,
	       ".tilesmd overran the gap into .palscenes at 0x2F8000")
	/* 2026-09-20: the ROM image (.text+.data+.ramtext) reached 0x264050 and
	 * silently overlapped .tilesmd at 0x264000 on a probe build. The blob
	 * moved to 0x268000, then 0x26A000 (2026-09-25, the 68K side grew 3 KB with WRAMCODE) and this says so in words when it happens again. */
	/* ONLY WHEN THE BLOB IS ACTUALLY LINKED. The check is address-based, so with
	 * NOTILESMD=1 it fired against an EMPTY .tilesmd -- there is nothing at
	 * 0x26A000 to collide with, and a probe that drops the blob should not be
	 * refused for running into it. Measured 2026-10-01 on the PORTFULL build. */
	ASSERT(SIZEOF(.tilesmd) == 0 ||
	       __romimg_end <= LOADADDR(.tilesmd),
	       ".ramtext ROM image reached .tilesmd -- move the blob (mars.ld + md_main.c, make tilesmd-addr)")

	/* PORTTEST=1 ONLY, and ADDITIVE: nothing on the line moves. The SH-2
	 * self-test (sh_src/port_selftest.c, generated) is decompiled 68K code
	 * checked against the arcade oracle, and it was capped at 13 functions
	 * because its .text growth pushed the .ramtext ROM image into .tilesmd
	 * above. That cap matters more than it looks: 24% of the corpus can ONLY be
	 * verified on the target (a 32-bit big-endian machine), because on the host
	 * a work-RAM pointer does not fit the 32-bit cell it came out of and the ROM
	 * image carries the opposite byte order -- see LESSONS, "24% of the corpus
	 * can ONLY be verified on the target".
	 *
	 * So it gets its own home in the MEASURED free gap between .tilesmd's end
	 * (0x2E8A00 at the current blob size) and .palscenes at 0x2F8000 -- about
	 * 62 KB. Executing from cart is slower than .ramtext and completely
	 * irrelevant for a test that runs a few dozen vectors once a vint. Empty and
	 * harmless when PORTTEST is off. */
	/* 2026-10-09, MDLINES4: the four-line pen table takes the art blob from 518,656 to
	 * 551,424 bytes and .porttest at 0x2F0000 left it 540,672 -- over by 10,752. .porttest
	 * moves up 12 KB under __portfull ONLY, so the LINE's cart addresses do not move. It is
	 * a PROBE section, empty unless PORTTEST=1, so a port build loses nothing; the
	 * 0x2F8000 ASSERT below still guards it with 20 KB. If a PORTTEST=1 PORT build ever
	 * trips that ASSERT, lower --max in tools/gen_selftest.py -- not before. */
	.porttest (DEFINED(__portfull) ? (DEFINED(__testcart) ? 0x022F4000 : 0x022F3000)
	                               : (DEFINED(__testcart) ? 0x022EA000 : 0x022E9000)) :
	    AT(DEFINED(__portfull) ? (DEFINED(__testcart) ? 0x002F4000 : 0x002F3000)
	                          : (DEFINED(__testcart) ? 0x002EA000 : 0x002E9000)) {
		KEEP(*(.porttest))
	} > rom
	ASSERT(LOADADDR(.porttest) + SIZEOF(.porttest) <= 0x002F8000,
	       ".porttest overran the gap into .palscenes at 0x2F8000 -- lower --max in tools/gen_selftest.py")
	ASSERT(LOADADDR(.tilesmd) + SIZEOF(.tilesmd) <= LOADADDR(.porttest),
	       ".tilesmd now reaches .porttest -- the blob grew; move .porttest up or the blob down")

	/* PORTTEST=1 ONLY, NOLOAD, and never shipped. A FULL 16 KB CONTIGUOUS MIRROR of
	 * the 68K work RAM, so the SH-2 self-test can drop windowing altogether: one
	 * base offset, no guard-edge artefacts, no per-function window budget (27
	 * functions were rejected on that budget alone).
	 *
	 * Placed at 0x0602CC00, immediately above sbuf's end (0x06019000 + 80,640 =
	 * 0x0602CB80) and as far as possible from the Master stack at 0x0603FC00. This
	 * region IS the documented worst-case render-stack spill room, so it is not
	 * free in the ordinary sense -- the justification is that (a) PORTTEST is a
	 * probe build that never ships, and (b) the mirror carries 0xA5 guard bytes
	 * that the self-test checks after every vector, so a stack incursion reports a
	 * MIRROR ARTEFACT rather than a false pass. A silent wrong answer is the thing
	 * being avoided; a detected one is acceptable in a probe. */
	.portmirror 0x0602CC00 (NOLOAD) : {
		. = ALIGN(0x10);
		*(.portmirror)
	} > ram
	ASSERT(ADDR(.portmirror) + SIZEOF(.portmirror) <= 0x06031000,
	       ".portmirror reached .ramtext at 0x06031000 -- shrink the mirror")

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

	/* PORTFULL (docs/design/FULL-PORT.md): the decompiled 68K game running on
	 * the SH-2. Occupies the window .sprbake would use, so the two are mutually
	 * exclusive -- build with NOSPRBAKE=1. Empty in every other rom, and an
	 * empty output section costs nothing.
	 *   .portrom   the REAL prog68k.bin, so the port's constant reads are right.
	 *              NOT .gamehigh: that image is REBASED (+0x900000 on every ROM
	 *              reference) for the 68K's bank window, so its operand bytes
	 *              differ from what the decompiled code expects to read as data.
	 *   .portdata  the two 16 KB L2 oracle captures.
	 *   .portcode  the port itself. */
	.portrom 0x02400000 : AT(0x00400000) {
		/* EXPLICIT object reference, not `KEEP(*(.romimg))`: the wildcard form
		 * produced a section of the right SIZE with ZERO CONTENTS (measured
		 * 2026-09-28 -- 256 KB of zeros in the ELF, so every ROM table the port
		 * read came back 0 and 128 indirect calls a frame dispatched to
		 * address 0). Naming the object is how .portcode already does it. */
		KEEP(*(.portrom))
	} > rom
	/* 64 KB. It was 32 (exactly the two 16 KB captures), then 48. It holds ONE
	 * 16 KB seed plus the multi-frame DELTA blob: 256 frames in 42,744 bytes
	 * (tools/l2_pack.py). The second full image was dropped -- it is the seed
	 * with delta frame 1 applied -- which is where 16 KB of this came from. */
	.portdata 0x02440000 : AT(0x00440000) {
		KEEP(*(.portdata))
	} > rom
	/* 52 KB. It was 32 (the two 16 KB captures), then 48, now 52: the per-frame
	 * I/O capture (sh_src/port_l2_io.s, 3,144 bytes for 258 frames) is the frame's
	 * OTHER input and took the seed + deltas + I/O past 48 KB. .portcode has ~72 KB
	 * spare at -Os, so it moves up rather than the chain being trimmed. */
	ASSERT(SIZEOF(.portdata) <= 0xD000,
	       ".portdata (L2 seed + delta blob + I/O capture) grew into .portcode at 0x238D000")
	.portcode 0x0244D000 : AT(0x0044D000) {
		KEEP(*(.portcode))
		KEEP(*(.tapepad))   /* measurement builds: the in-port input tape (PORTTAPEPAD, tools/tape_pad.py) */
		/* A WILDCARD, NOT THREE FILENAMES. An object named WITHOUT wildcard
		 * characters in a SECTIONS command is a DEMAND: ld loads that file
		 * whether or not it is on the command line. Naming port.o,
		 * objdispatch.o and dispregs.o here therefore broke EVERY build that is
		 * not PORTFULL, and broke it in two directions at once (found
		 * 2026-10-02, introduced in e4fb3e1e):
		 *
		 *   the objects EXIST  -> they are pulled in, but layout.ld (which
		 *                         PROVIDEs _WRAM_BASE, _ROM_BASE and every
		 *                         absolute 68K symbol) is only on the link line
		 *                         under PORTFULL, so hundreds of undefined
		 *                         references -- plus a .portcode/.sprbake LMA
		 *                         overlap, since NOSPRBAKE is also off
		 *   they DO NOT exist  -> "cannot find sh_src/portfull/port.o"
		 *
		 * So `make line` -- THE SHIPPING LINE -- could not link either way. A
		 * pattern WITH wildcard characters is matched against the files already
		 * in the link instead, which is what was always meant.
		 *
		 * This also covers dispregs.o, whose 64 x 15 x 4 = 3.8 KB table must stay
		 * out of the normal .rodata: there it pushed .ramtext's ROM image into
		 * .tilesmd and the link failed (measured 2026-10-01). */
		sh_src/portfull/*.o(.text .text.* .rodata .rodata.*)
	} > rom
	ASSERT(SIZEOF(.sprbake) == 0 || SIZEOF(.portcode) + SIZEOF(.portrom) == 0,
	       "PORTFULL and SPRBAKE both want 0x02340000 -- build PORTFULL with NOSPRBAKE=1")
	ASSERT(0x0244D000 + SIZEOF(.portcode) <= 0x02800000,
	       ".portcode overflows the 4 MB cart")
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
	/* PORTHOT raises the cap into the probe scrap at 0x38000-0x39000, which is free on
	 * the line (RIG_SCRAP, RSHASH and GR all live there and are all probe-only). SPR_LAND
	 * at 0x39000 is the hard stop. Without the override the cap is unchanged. */
	__ramtext_cap = DEFINED(__ramtext_cap_override) ? __ramtext_cap_override : 0x06038000;
	ASSERT(__ramtext_end <= __ramtext_cap,
	       ".ramtext overflows its slot (0x31000-0x38000, or -0x39000 under PORTHOT; probe scrap 0x38000-0x39000, SPR_LAND 0x39000)")
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

	/* THE MASTER'S HOT GAME CODE, REGION A (PORTHOT2, 2026-10-24). The port runs the
	 * translated game from CART, and the MiSTer core holds DTACK for a full SDRAM access on
	 * every cart read (srcref/S32X_MiSTer rtl/32X/IF.sv RS_MD_WAIT/RS_MD_READ): the master's
	 * tick measured 126 lines a vint on the office rig against 42 on ares. tools/hot_set.py
	 * picks the functions with the most cart wait per byte (tools/hot_set.txt) and the build
	 * puts them in section .ramtext2, which lands HERE: in the gap between .bss's end and the
	 * renderer's fixed regions at 0x06019000. Its ROM image follows .ramtext's, and both boot
	 * copies in mars_start.s walk __hotcopy_tab (size 0 = skipped) before M_OK, so the slave
	 * guarantee holds. Empty without PORTHOT2 -- the copy table then copies nothing. */
	.ramtext2 ALIGN(_end, 16) : AT(LOADADDR(.ramtext) + SIZEOF(.ramtext)) {
		__ramtext2_start = .;
		*(.ramtext2 .ramtext2.*)
		. = ALIGN(0x4);
		__ramtext2_end = .;
	} > ram
	__ramtext2_lma  = LOADADDR(.ramtext) + SIZEOF(.ramtext);
	__ramtext2_size = __ramtext2_end - __ramtext2_start;
	/* REGION B (.ramtext3) is not linked yet: it needs the .portstack shrink. The copy
	 * table carries its slot with size 0. */
	__ramtext3_lma  = __ramtext2_lma + __ramtext2_size;
	__ramtext3_start = 0;
	__ramtext3_size = 0;
	/* the last byte of the SH-2 ROM image (.text, .data, .ramtext, .ramtext2[, .ramtext3]):
	 * every guard below that used to read LOADADDR(.ramtext) + SIZEOF(.ramtext) reads this */
	__romimg_end = __ramtext3_lma + __ramtext3_size;
	ASSERT(__ramtext2_end <= 0x06019000,
	       ".ramtext2 (the hot set's region A) grew into the renderer's region block at 0x06019000 -- lower the hot_set.py budget")
	ASSERT((__ramtext2_size & 3) == 0, ".ramtext2 size must be a multiple of 4 for the boot copy")
	ASSERT(__romimg_end <= 0x00260000,
	       ".text+.data+.ramtext+.ramtext2 ROM image reached .sounddata at 0x260000")

	/* ---- THE PORT'S TWO SDRAM BLOCKS ARE DECLARED, NOT ASSUMED ----------------
	 *
	 * cport_layout.py PROVIDEs _MMIO_BASE (0x06019000, 10 compacted 4 KB pages) and
	 * _WRAM_BASE (0x0602CC00, the 68K's 16 KB of work RAM) as ABSOLUTE symbols --
	 * hand-picked addresses in the gap above .bss, invisible to this script. So
	 * nothing stopped .bss growing into them, and nothing stopped anything else
	 * using the gap.
	 *
	 * Measured 2026-10-02: the driver fills the MMIO stubs with 0xFF, the frame runs
	 * and writes real S16 video state there -- 1,594 bytes at frame 250 growing to
	 * 3,196 by 280, the picture updating -- and by the dump only 2 bytes of the fill
	 * survive. Something else owns that memory. THE PORT'S VIDEO OUTPUT IS THE
	 * RENDERER'S FUTURE INPUT (docs/design/PORT-OUTPUT.md), so it has to be real
	 * reserved space before anything can read it.
	 *
	 * NOLOAD: no ROM image, and crt0 must NOT zero them -- the driver seeds both.
	 *
	 * 2026-10-03, AND THIS IS THE OUTPUT PATH: the four S16 VIDEO regions no longer
	 * live in a block of their own.  cport_layout.py now places them ON THE
	 * RENDERER'S OWN SDRAM SHADOWS -- tile -> TILEMAP_U 0x19000, text -> TEXT_U
	 * 0x26000, palette -> PAL_SH 0x27000, sprite table -> SPR_SNAP 0x28400, parsed
	 * out of sh_src/m_main.c with each size taken as the distance to the next
	 * define.  The renderer reads exactly those four and nothing else, so the port's
	 * picture becomes its input with no transport and no per-frame cost.  They are
	 * NOT reserved here: the renderer's own `sbuf` already owns them, and declaring
	 * them twice is what an overlap looks like when it is CORRECT.
	 *
	 * What is left over is the port's NON-video footprint, and it is one contiguous
	 * 17 KB: the sparse I/O ports (0x0602C800, 1 KB) and the 68K's work RAM
	 * (0x0602CC00, 16 KB).  Both sit in the top of the 32 KB art cache CACHE_C
	 * (0x29000-0x31000), so a build that renders AND runs the port has 17 KB to find
	 * -- halving CACHE_C to 256 slots is the only candidate of the right size, and
	 * it is a pixel-affecting change that has to be measured, not assumed. */
	/* THE PORT'S COROUTINE STACK (PORTCORO=1).  8 KB, so `frame_rotated` can YIELD
	 * at each wait_frame_flag and RESUME where it stood instead of being re-entered
	 * from its root every frame.  Free-run already runs by re-entry at 0x0AE2 (there
	 * is no prologue to erase state), but re-entry throws away the SH-2 CALL STACK
	 * at every frame boundary, and the free-run curve is the one thing standing
	 * between the port and a playable build -- so the stack is a named suspect with
	 * an instrument already pointed at it.
	 *
	 * IT OVERLAPS THE ART CACHE (CACHE_C, 0x29000-0x31000, 512 x 64 B) AND THAT IS
	 * NOT FREE.  It is harmless for the L2 gate, which reads work RAM only, and it
	 * is a real conflict for a build that renders -- the same conflict the port's
	 * I/O block and work RAM already have.  The port's non-video footprint is now
	 * 25 KB (stack 8 + I/O 1 + work RAM 16) out of CACHE_C's 32, and what has to be
	 * counted against it is everything the port MAKES free: the packet transport's
	 * .ramtext, its .bss buffers, and the MD-side staging.  That count has not been
	 * taken; until it is, this overlap is declared, not solved.
	 *
	 * 0xA5-painted by the driver on launch so the watermark is a MEASUREMENT.  The
	 * port's own 68K stack is elsewhere -- emulated inside work RAM at A7 =
	 * 0xFFFFFF00 -- so this holds SH-2 frames only. */
	/* 0x1F00, NOT 0x2000 (2026-10-22): the Makefile places the port's five I/O shadow
	 * pages at --io-base 0x0602C700 (0x500 bytes), so a 0x2000 stack topping at 0x2C800
	 * put I/O page 0 on the coroutine's root frames. Measured on the parity harness
	 * (hp1, tape level1_full.csv): the game's second death at the boss-arena lock
	 * wrote I/O cells over the saved frame at 0x2C774 (PR and r11-r13 read 0), the
	 * driver never resumed it, and the port froze at frame 30924 with the renderer
	 * idling normally. cport_layout.py also puts its odd bucket (the decompiler's
	 * nonsense addresses, e.g. _DAT_7ac2c3c0) at io_base - 0x100 = 0x0602C600, so the
	 * stack must end at 0x0602C600: 0x1E00. The watermark in that run was 404 bytes,
	 * so nothing is lost. The ASSERT below ties this to layout.ld's own _MMIO_BASE so
	 * the two placements cannot drift apart again. */
	.portstack 0x0602A800 (NOLOAD) : {
		_PORT_CORO_BASE = .;
		. = . + 0x1E00;
		_PORT_CORO_TOP = .;
	} > ram
	ASSERT(_PORT_CORO_TOP <= _MMIO_BASE - 0x100,
	       "the port's coroutine stack overlaps the I/O shadow pages or the odd bucket (mars.ld .portstack vs the Makefile's --io-base)")
	.portio   0x0602C600 (NOLOAD) : { . = . + 0x600; } > ram
	.portwram 0x0602CC00 (NOLOAD) : { . = . + 0x4000; } > ram
	ASSERT(_end <= 0x06019000,
	       ".bss grew into the renderer's region block at 0x06019000")
	ASSERT(0x0602C800 + 0x400 <= 0x0602CC00,
	       "the port's I/O block grew into its work RAM at 0x0602CC00")
	ASSERT(_PORT_CORO_TOP <= 0x0602C800,
	       "the port's coroutine stack grew into its I/O block at 0x0602C800")
	ASSERT(0x0602CC00 + 0x4000 <= 0x06031000,
	       "the port's work RAM grew into .ramtext at 0x06031000")

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
	ASSERT(SIZEOF(.ramtext2) == 0 || SIZEOF(.hero_overlay) == 0,
	       ".hero_overlay is not empty: its buffers were parked above _end, where .ramtext2 now lives")

	.eh_frame 0 (NOLOAD) :
	{
		*(.eh_frame)
	}
}
