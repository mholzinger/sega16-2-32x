#!/usr/bin/env python3
"""READ THE PORTRIG BARS OFF A SCREENSHOT -- the rig's only readable channel.

    tools/rig_bars.py shot.png                 a local 320x224 png (ares or the rig)
    MISTER=root@mister.office.local tools/rig_bars.py --rom pc9rig   pull and read

A PORTRIG=1 rom paints two bars into the bottom strip of the 32X framebuffer (sbuf rows
224..231 = screen rows 216..223), because the rig has no memory dump and both CRAM channels
are out while the background is under investigation: a flood destroys the BG palette for the
rest of the run (LESSONS 830 rule 2) and the name-table barcode writes CRAM 61-63 itself.

  screen rows 216..219   ticks per second x 5, clamped to 300 px
  screen rows 220..223   the 1-vint share, per cent x 3, clamped to 300 px

The bar is drawn in framebuffer value 0xFF on a row cleared to 0x00, so the reader measures
the LEADING RUN of one colour against another and never needs to know the palette. Each row
of the strip votes; the median is reported, so one torn row cannot move the number.

TWO RULES FROM THE 2026-10-09 RIG SESSION, and the first one is why 47.5 ticks/second stood
for two days and was wrong by 13:

  A STRIP ROW THAT IS ONE COLOUR ACROSS ALL 320 PX IS NO BAR -- DISCARD THE SHOT. A bar is a
  LEADING RUN measured against a cleared remainder, so a row with no transition carries no
  length. read() enforces it (`0 < n < w`) and raises rather than return a number; the two
  helpers that used to live here, run_len() and transitions(), were dead code and run_len
  returned w for such a row with the comment "the whole row is one colour: a full bar", which
  is exactly backwards. Deleted.

  A LINE NEEDS THREE LAUNCHES, NOT ONE READING. 47.5 ticks/s and 73% one-vint came from
  reading 2 of a single launch (336f13c7); four fresh launches of the same rom, under
  distinct names and log-proven, read 34.8 and 39%. Differencing two readings of ONE run
  divides the boot out but says nothing about launch-to-launch spread.
"""
import os, sys, struct, zlib, subprocess, statistics

LO, HI = 216, 224          # the strip, in screen rows
X0, W = 0, 320


def unpng(path):
    d = open(path, 'rb').read()
    pos, idat, w, h, ct = 8, b'', None, None, None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ, data = d[pos + 4:pos + 8], d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, _bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride, rows, prev, i = w * ch, [], bytearray(w * ch), 0

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        if f == 1:
            for x in range(ch, stride): line[x] = (line[x] + line[x - ch]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                line[x] = (line[x] + ((line[x - ch] if x >= ch else 0) + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                line[x] = (line[x] + paeth(line[x - ch] if x >= ch else 0, prev[x],
                                           prev[x - ch] if x >= ch else 0)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def read(path):
    """MEASURE THE LEADING RUN OF THE BRIGHTEST PEN, from x=0.

    The first version required a row of exactly two colours with one change, which held on
    ares and FAILED ON THE RIG: FB value 0 is TRANSPARENT on the 32X, so the part of the
    strip the bar does not cover shows the Mega Drive plane through it. On ares that plane
    was black and the row read as two colours; on the rig it carries tiles, and the rig's
    own 320x224 screenshot gave 6 to 12 colours and 89 to 183 changes per row -- no match,
    0 bar rows, on a strip that was painted correctly all along (white from x=0, runs 162
    and 102).

    So the signature is the PEN, not the row: the rom paints with the brightest entry in
    the live palette, so the brightest colour in the image is the bar, and the measurement
    is how far it runs from x=0. Rows whose first pixel is that colour are the strip; it is
    split in half, top half bar A (ticks/second x5) and bottom half bar B (share x3). x
    scales by the image width over 320, so a scaled screenshot still reads."""
    w, h, ch, rows = unpng(path)
    sx = w / 320.0

    def px(y, x):
        o = x * ch
        return tuple(rows[y][o:o + 3])

    best, pen = -1, None
    for y in range(0, h, 2):               # the brightest colour anywhere in the picture
        for x in range(0, w, 2):
            c = px(y, x)
            s = c[0] + c[1] + c[2]
            if s > best:
                best, pen = s, c
    found = []
    for y in range(h - 1, max(h - 48, -1), -1):
        if px(y, 0) != pen:
            if found:
                break                      # the strip is contiguous; stop at its top edge
            continue
        n = 0
        while n < w and px(y, n) == pen:
            n += 1
        if 0 < n < w:
            found.append((y, n / sx))
        elif found:
            break
    if len(found) < 2 or len(found) % 2:
        raise SystemExit('rig_bars: %s (%dx%d): found %d bar rows in pen %s, need an even '
                         'count of at least 2 -- is this a PORTRIG=1 rom?'
                         % (path, w, h, len(found), pen))
    found.sort()                           # top to bottom: bar A then bar B
    half = len(found) // 2
    a = [v for _y, v in found[:half]]
    b = [v for _y, v in found[half:]]
    tps = statistics.median(a) / 5.0
    share = statistics.median(b) / 3.0
    return tps, share, [round(x, 1) for x in a], [round(x, 1) for x in b]


def main():
    if '--rom' in sys.argv:
        host = os.environ.get('MISTER')
        if not host:
            raise SystemExit('rig_bars: set MISTER=root@mister.office.local')
        rom = sys.argv[sys.argv.index('--rom') + 1]
        shot = subprocess.run(['ssh', host, 'ls -t /media/fat/screenshots/S32X/*-%s.png '
                               '2>/dev/null | head -1' % rom],
                              capture_output=True, text=True).stdout.strip()
        if not shot:
            raise SystemExit('rig_bars: no screenshot for %s' % rom)
        local = '/tmp/rig_bars_%s.png' % rom
        subprocess.run(['scp', '-q', '%s:%s' % (host, shot), local], check=True)
        paths = [local]
    else:
        paths = [a for a in sys.argv[1:] if not a.startswith('-')]
    for p in paths:
        tps, share, a, b = read(p)
        print('%s  ticks/second %5.1f   1-vint share %5.1f%%   (rows %s | %s)'
              % (os.path.basename(p), tps, share, a, b))


if __name__ == '__main__':
    main()
