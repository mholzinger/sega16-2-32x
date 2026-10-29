OUTPUT_ARCH(m68k)
SEARCH_DIR(.)

MEMORY {
	rom (rx) : ORIGIN = 0x008C0410, LENGTH = 4M
	ram (wx) : ORIGIN = 0x00FF0000, LENGTH = 64K
}

PROVIDE (__stack = 0x01000000);

SECTIONS {
	.text 0x008C0410 : {
		__text_start = .;
		*(.text.keepboot)
		*(.text .text.*)
		*(.rodata .rodata.*)
		/* The boot copies .data from __text_end with WORD reads. Rounding
		 * only the symbol left SIZEOF(.text) odd after some UI strings, so
		 * .data's ROM load address was one byte before the copy source. */
		. = ALIGN(2);
		__text_end = .;
	} > rom
	__text_size = SIZEOF(.text);

	.data 0x00FF0100 : AT(LOADADDR(.text) + SIZEOF(.text)) {
		__data_start = .;
		*(.data .data.*)
		. = ALIGN(2);               /* copy the final initialized byte too */
		__data_end = .;
	} > ram
	__data_size = SIZEOF(.data);

	.bss 0x00FF0100 + SIZEOF(.data) : {
		__bss_start = . ;
		*(.bss .bss.*)
		__bss_end = ALIGN(2);
	} > ram
	__bss_size = SIZEOF(.bss);
}

ASSERT(LOADADDR(.data) == __text_end && (SIZEOF(.data) % 2) == 0,
       "68K word-copy source and .data image alignment disagree")

/* .data + .bss grow upward from 0xFF0100 into free WRAM. The first thing
 * above them is PAL_SHADOW at 0xFF6000 (md_src/packet_fmt.h), then
 * spriteram at 0xFF7000 — none of it linked, so the linker cannot see
 * the collision. LOOP28 84 audited the gap (15,916 bytes, zero writes
 * after boot over 4100 level-1 and 9000 attract frames) and this is its
 * fence. If a staging buffer takes the room, this is what tells you. */
ASSERT(__bss_end <= 0x00FF6000,
       "68K .data+.bss overran free WRAM and hit PAL_SHADOW at 0xFF6000")
/* The boot/shim stack tops at 0xFF3FF0 (md_start.s) and grows down toward
 * .bss; keep at least 4KB. It used to live at 0xFFBFF0, 184 bytes above
 * the FM-gate thunk table, and the boot-time vint handler overwrote the
 * table's tail whenever its frame grew by 32 bytes (2026-09-19). */
/* WRAMCODE region 2 (2026-09-24, tools/wram_reloc.py) occupies 0xFF4600-0xFF4BFF.
 * GUARDED ON THE REGION EXISTING (2026-09-27): the fence is only a fence when
 * WRAMCODE2 puts code there, and unconditionally it failed the TEST CARTRIDGE
 * link twice for .bss the region was never going to contend with. The Makefile
 * passes --defsym __wramcode2=1 when the region is in use, so the assert is
 * live exactly then and the next fence up (the boot stack at 0xFF4C00) still
 * applies always. */
ASSERT(!DEFINED(__wramcode2) || __bss_end <= 0x00FF4600,
       "68K .bss reached WRAMCODE region 2 at 0xFF4600 (move the region or trim .bss)")
/* THE BOOT STACK FENCE, and the TEST CARTRIDGE gets a smaller reserve for a
 * stated reason (2026-09-27). Shipping builds keep the full 1KB below the
 * stack top at 0xFF4FF0. Under TESTCART the GAME NEVER RUNS -- the suite takes
 * BOOT_BANK3HALT's slot and never returns -- so the only stack that exists is
 * boot's (measured 202 bytes) plus the suite's own paint frames. 512 bytes is
 * still 2.5x the measured depth, and the cart went 22 bytes over the 1KB line
 * purely because its timing loops must live in .data: executing them from ROM
 * would change the very numbers they measure.
 * Same shape as the __wramcode2 fence above, and for the same reason -- an
 * unconditional fence is not a fence when it guards against a contender that
 * is not in the build. */
ASSERT(DEFINED(__testcart) || __bss_end <= 0x00FF4C00,
       "68K .bss reached the boot stack region (top 0xFF4FF0, keep 1KB: boot depth measured 202 bytes)")
ASSERT(!DEFINED(__testcart) || __bss_end <= 0x00FF4E00,
       "TESTCART .bss reached the boot stack region (top 0xFF4FF0, 512B reserve vs 202B measured depth -- trim the suite)")
