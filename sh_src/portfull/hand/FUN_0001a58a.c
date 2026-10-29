/* 0x1A58A: 18 tile words per row, then A0 += 128, DBF D0.
 * MOVE.L on the 68000 permits two-byte alignment. The portrait's destination
 * 0x405516 and both ROM streams have that alignment, so copy words on SH-2.
 * Return the advanced A0 for the caller's four footer rows. */
uint FUN_0001a58a(undefined4 *p_A0,undefined4 *p_A1,undefined4 p_D0)
{
    ushort *dst = (ushort *)A68PTR(p_A0);
    const ushort *src = (const ushort *)A68PTR(p_A1);
    unsigned rows = (ushort)p_D0 + 1u;
    while (rows--) {
        unsigned x;
        for (x = 0; x < 18u; x++) dst[x] = *src++;
        dst += 64;
    }
    return (uint)dst;
}
