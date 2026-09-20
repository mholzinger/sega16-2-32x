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
		__text_end = ALIGN(2);
	} > rom
	__text_size = SIZEOF(.text);

	.data 0x00FF0100 : AT(LOADADDR(.text) + SIZEOF(.text)) {
		__data_start = .;
		*(.data .data.*)
		__data_end = ALIGN(2);
	} > ram
	__data_size = SIZEOF(.data);

	.bss 0x00FF0100 + SIZEOF(.data) : {
		__bss_start = . ;
		*(.bss .bss.*)
		__bss_end = ALIGN(2);
	} > ram
	__bss_size = SIZEOF(.bss);
}

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
ASSERT(__bss_end <= 0x00FF3800,
       "68K .bss reached the boot stack region (top 0xFF3FF0, keep 2KB: boot depth measured 202 bytes)")
