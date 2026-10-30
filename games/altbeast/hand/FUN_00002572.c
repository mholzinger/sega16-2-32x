/* FUN_00002572 -- THE TITLE LOGO'S TILE MAPS, four rectangles, with the altbeastj logo.
 *
 *   2572: lea 0x26c20,%a0        the rectangle list
 *   2578: bsrs 0x258a            x3, each leaving A0 past its rectangle
 *   257e: bras 0x258a            the fourth, whose rts returns to the caller
 *
 * REGION 1 (altbeastj, tools/region_diff.py 2026-10-30). The list sits at the same address in
 * both sets, but its first two rectangles differ: US 20x20 at tile 0x2400/0x2428 (ALTERED
 * BEAST), JP 20x15 at 0x2480/0x24A8 (the Juuouki logo, 400 bytes shorter). The last two are
 * byte-identical. So JP blits its own two from region1_026c20 (tools/region_data.py takes
 * them from the altbeastj program at build time), steps over the US two without drawing
 * them, and blits the shared last two. */
extern const unsigned short region1_026c20[];

/* one rectangle of a 0x258A list: offset, width-1, height-1, then the words */
static undefined2 *rect_skip(undefined2 *p_A0)
{
  ushort *pu = (ushort *)A68PTR(p_A0);
  return (undefined2 *)(pu + 3 + ((uint)pu[1] + 1u) * ((uint)pu[2] + 1u));
}

uint FUN_00002572()
{
  undefined2 *puNext = (undefined2 *)(&DAT_00026c20);

  if (port_region == 1u) {
    FUN_0000258a((undefined2 *)FUN_0000258a((undefined2 *)region1_026c20));
    puNext = rect_skip(rect_skip(puNext));
  } else {
    puNext = (undefined2 *)FUN_0000258a(puNext);
    puNext = (undefined2 *)FUN_0000258a(puNext);
  }
  puNext = (undefined2 *)FUN_0000258a(puNext);
  FUN_0000258a(puNext);
  return 0;
}
