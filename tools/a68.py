#!/usr/bin/env python3
"""68K address -> where the port keeps it, as an ares --dump argument. Never hand-compute.
    tools/a68.py 0xFFEC80 [len]     ->  sdram:0x2F880:len   (work RAM: WRAM_BASE 0x0602CC00)
    tools/a68.py 0x840120           ->  sdram:0x27120       (video: cport_layout's VIDREG map)
2026-10-04: a hand-computed 0x2EC80 for 0xFFEC80 (forgot WRAM_BASE's own 0x2CC00) read an
object slot as the sprite draw-order lists and cost an hour of 'phantom object' theory."""
import sys
WRAM_BASE = 0x0602CC00
VID = [(0x400000, 0x40D000, 0x06019000), (0x410000, 0x411000, 0x06026000),
       (0x840000, 0x841000, 0x06027000), (0x440000, 0x440400, 0x06028400)]
a = int(sys.argv[1], 16) & 0xFFFFFF; n = sys.argv[2] if len(sys.argv) > 2 else '1'
if a >= 0xFFC000: sh = WRAM_BASE + (a - 0xFFC000)
else:
    sh = next((base + (a - lo) for lo, hi, base in VID if lo <= a < hi), None)
    if sh is None: sys.exit(f'{a:#x}: not work RAM and not a mapped video region')
print(f'sdram:{sh - 0x06000000:#x}:{n}    (SH-2 {sh:#010x})')
