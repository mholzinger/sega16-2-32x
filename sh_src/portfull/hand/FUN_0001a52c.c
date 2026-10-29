/* Crystal-ball tilemaps, from 0x1A52C..0x1A59C. The native MOVE.L streams
 * start at ROM 1C302/1C272 and tile RAM 405516, all two-byte aligned. Plain
 * undefined4 pointer accesses on SH-2 rounded the pairs down, shifting the
 * portrait one tile left and mixing adjacent tiles. Preserve the word stream. */
#ifdef PORT_TILEDIRTY
extern volatile ushort port_tile_dirty;
#endif
uint FUN_0001a52c()
{
    ushort *dst = (ushort *)A68VID(0x400494u);
    const ushort *src = (const ushort *)((const byte *)ROM_BASE + 0x1C302u);
    unsigned y, x;
    for (y = 0; y < 20u; y++) {
        for (x = 0; x < 20u; x++) dst[x] = *src++;
        dst += 64;
    }
    dst = (ushort *)A68VID(0x405000u);
    for (x = 0; x < 2048u; x++) dst[x] = 0x0200u;
    dst = (ushort *)FUN_0001a58a((undefined4 *)A68VID(0x405516u),
        (undefined4 *)LD32U((const byte *)ROM_BASE + 0x1C622u + (DAT_fffff142 & 7u) * 4u), 17u);
    src = (const ushort *)((const byte *)ROM_BASE + 0x1C272u);
    for (y = 0; y < 4u; y++) {
        for (x = 0; x < 18u; x++) dst[x] = *src++;
        dst += 64;
    }
#ifdef PORT_TILEDIRTY
    port_tile_dirty |= (ushort)((1u << 0) | (1u << 5));
#endif
    return 0;
}
