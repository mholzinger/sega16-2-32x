#!/usr/bin/env python3
"""THE 32X FRAMEBUFFER AS THE MiSTer WRITES IT: a native 320x224 PNG.

    ares-headless rom --frames N --dump dram:0:0x40000:fb.bin --dump cram:0:512:cram.bin
    tools/fb_png.py fb.bin cram.bin out.png

WHY IT EXISTS. ares' own --screenshot is the SCALED video output -- measured 1415x243, with
the visible 224 lines inside 243 and the scaler blending edges, 11 distinct colours in a row
the rom painted in two. So it cannot stand in for the rig's screenshot when a reader measures
pixel runs (tools/rig_bars.py): reading it as 320-wide gave 13.0 ticks/second and scaling
both axes gave 64.0, against the 56.9 the scrap words said. The MiSTer writes the emulated
320x224 with no scaling, so THIS is the rig's format, rendered from the same framebuffer and
CRAM the hardware would show.

Layout: displayed pixels at DRAM 0x200 + y*320 + x, one byte each, indexing 32X CRAM's
256 entries of 15-bit BGR (bit 15 unused).
"""
import struct, sys, zlib

W, H, FB0 = 320, 224, 0x200


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    fb = open(sys.argv[1], 'rb').read()
    cr = open(sys.argv[2], 'rb').read()
    pal = []
    for i in range(256):
        v = struct.unpack('>H', cr[i * 2:i * 2 + 2])[0] if (i * 2 + 2) <= len(cr) else 0
        r = (v & 0x1F) << 3
        g = ((v >> 5) & 0x1F) << 3
        b = ((v >> 10) & 0x1F) << 3
        pal.append(bytes((r | r >> 5, g | g >> 5, b | b >> 5)))
    raw = bytearray()
    for y in range(H):
        raw.append(0)                                   # filter: none
        o = FB0 + y * W
        for x in range(W):
            raw += pal[fb[o + x]] if o + x < len(fb) else b'\0\0\0'

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 6))
           + chunk(b'IEND', b''))
    open(sys.argv[3], 'wb').write(png)
    print('%s: %dx%d from %s + %s' % (sys.argv[3], W, H, sys.argv[1], sys.argv[2]))


if __name__ == '__main__':
    main()
