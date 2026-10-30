/* THE PORT'S SDRAM MAP: where the S16 video shadows live (SH-2 uncached addresses).
 * One source for the renderer (sh_src/r60) and the port generator: tools/cport_prep.py
 * and tools/cport_layout.py parse the SDRAM_* lines, so keep the form
 * `#define SDRAM_<NAME> 0x<8 hex>u`. Each region ends where the next begins: the tile
 * shadow runs to TEXT_U, text to PAL_SH, palette to DIAG, sprites to SYNC.
 * Moved out of sh_src/m_main.c (the retired renderer) 2026-10-29. */
#ifndef SDRAM_MAP_H
#define SDRAM_MAP_H
#define SDRAM_TILEMAP_U  0x26019000u   /* 13 pages x 2K words */
#define SDRAM_TEXT_U     0x26026000u   /* 2048 words */
#define SDRAM_PAL_SH     0x26027000u   /* 2048 words */
#define SDRAM_DIAG       0x26028000u
#define SDRAM_SPR_SNAP   0x26028400u   /* 64 x 8 words */
#define SDRAM_SYNC       0x26028800u
/* THE GAME'S WORK RAM (68K 0xFFC000-0xFFFFFF, 16 KB) as the SH-2 sees it, uncached; the
 * port's .portwram (sh_src/portfull/layout.ld _WRAM_BASE 0x0602CC00). Engine, not game:
 * a title's cells come from sh_src/game_cfg.h (GAME_WRAM_*) through SDRAM_WRAM_AT. */
#define SDRAM_WRAM_U     0x2602CC00u
#define SDRAM_WRAM_68K   0xFFC000u
#define SDRAM_WRAM_AT(a68)  (SDRAM_WRAM_U + ((a68) - SDRAM_WRAM_68K))
#endif
