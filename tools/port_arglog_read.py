#!/usr/bin/env python3
"""Decode CPORT_ARGLOG's ring from an ares sdram dump of _port_arglog (64 B at sdram 0x3A700, n at 0x3A740) +
_port_arglog_n (4 B):  tools/port_arglog_read.py ring.bin n.bin "FUN:param,..."
Prints the last 8 entries in order: seq, id/name, raw value, and the domain the
value falls in (SH-2 / work RAM / ROM / video / other)."""
import sys
ring = open(sys.argv[1], 'rb').read(); n = int.from_bytes(open(sys.argv[2], 'rb').read(), 'big')
names = sys.argv[3].split(',')
def dom(v):
    if 0x06000000 <= v < 0x06040000: return 'SH-2 SDRAM'
    if 0x02000000 <= v < 0x02400000: return 'SH-2 ROM'
    if (v >> 24) in (0, 0xFF) and (v & 0xFFFFFF) >= 0xFFC000: return '68K work RAM'
    if 0x400000 <= v < 0x440400 or 0x840000 <= v < 0x841000: return '68K VIDEO'
    if v < 0x40000: return '68K ROM'
    return 'other'
start = n - 8 if n > 8 else 0
for k in range(start, n):
    i = k & 7
    tag = int.from_bytes(ring[i*8:i*8+4], 'big'); val = int.from_bytes(ring[i*8+4:i*8+8], 'big')
    print(f'seq={tag & 0xFFFFFF:7d} {names[tag >> 24]:28s} {val:#010x}  {dom(val)}')
