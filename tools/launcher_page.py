#!/usr/bin/env python3
"""The launcher's cart page (LAUNCHER=1): sh_src/launcher_page.bin. KIT.

    launcher_page.py OUT BASE PAYLOAD

The page is linked after the last game page (sh_src/pages_data.s), so its number follows
the game's page layout instead of being fixed. It starts with the music data, like every
page shown in slot 7 (the 68K reads music through that window every vblank); OUT is what
follows it, from offset BASE of the page (the Makefile's LAUNCHER_BASE). PAYLOAD is the
launcher program (introlab: make port), linked for slot 7 at BASE + 16; its table's first
entry must lie inside the payload at that address, or the build stops. Layout, big-endian:

    0x00  "LNCH"
    0x04  payload bytes (padded to a multiple of 4)
    0x08  sum of the payload's 32-bit words, modulo 2^32
    0x0C  reserved, 0
    0x10  payload

Both SH-2s re-sum the payload through slot 7 at boot (sh_src/r60/r60_launcher.c), so a
page that does not reach either CPU fails there and not as a wrong picture.
"""
import struct, sys

PAGE = 0x80000
SLOT7 = 0x02380000
if len(sys.argv) != 4:
    raise SystemExit(__doc__)
base = int(sys.argv[2], 0)
body = open(sys.argv[3], 'rb').read()
body += b'\0' * (-len(body) % 4)
if base + 16 + len(body) > PAGE:
    raise SystemExit('launcher_page: %d bytes from offset 0x%X do not fit one 512 KB page' % (16 + len(body), base))
magic, entry = struct.unpack('>II', body[:8])
start = SLOT7 + base + 16
if magic != 0x4C505247 or not start <= entry < start + len(body):
    raise SystemExit('launcher_page: the payload is not a launcher program linked at 0x%08X '
                     '(magic %08X, primary entry %08X): introlab/sh/port.ld and LAUNCHER_BASE disagree'
                     % (start, magic, entry))
total = sum(struct.unpack('>%dI' % (len(body) // 4), body)) & 0xFFFFFFFF
open(sys.argv[1], 'wb').write(b'LNCH' + struct.pack('>III', len(body), total, 0) + body)
print('%s: %d payload bytes at page offset 0x%X, sum %08X, %d bytes of the page free'
      % (sys.argv[1], len(body), base, total, PAGE - base - 16 - len(body)))
