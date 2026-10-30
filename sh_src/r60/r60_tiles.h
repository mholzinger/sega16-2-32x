#ifndef R60_TILES_H
#define R60_TILES_H

/* S16B's 13-bit tile number selects a fixed 4K bank or the MCU's bank 1.
 * Cache and sample the physical art index, not the unbanked tile number.
 * See segaic16.cpp:tilemap_16b_tile_info and m_main.c's PORT_VIDEO path. */
static inline unsigned r60_tile_bank1(void)
{
#ifdef PORT_FULL
    return *(const volatile unsigned char *)SDRAM_WRAM_AT(GAME_WRAM_TILE_BANKREQ) & 7u;
#else
    return MARS_SYS_COMM2 & 7u;
#endif
}

static inline unsigned r60_tile_index(unsigned code, unsigned bank1)
{
    code &= 0x1FFFu;
    if (code & 0x1000u) code = (code & 0xFFFu) + bank1 * 0x1000u;
#ifdef GAME_ALTBEASTJ
    if (code >= 0x4000u) code -= 0x2000u;
    else if (code >= 0x2000u) code = 0;
#endif
    return code & 0x3FFFu; /* the decoded ROM has 16K tiles */
}
#endif
