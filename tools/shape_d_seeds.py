#!/usr/bin/env python3
"""SHAPE D: routine pointers installed with `lea pc@(H),An ; move.l An,(d,A6)`.

    tools/shape_d_seeds.py roms/<game>/prog68k.bin [--json OUT]

tools/ghidra/seed_harvest.py knows two install idioms -- a pc-relative table of
longs, and `move.l #ADDR,(d,An)` with a literal. It does NOT know this one, where
the address is formed by a pc-relative `lea` and then stored into the object
record. Found 2026-09-26 by chasing what looked like a shared tail: seven
functions appeared to `goto` into 0x228C..0x2490, and those addresses turned out
to be handlers installed at record offset +2:

    2110: move.w #-32768,(0,A6)    ; object +0  = flags 0x8000
    2116: lea     (0x228c,PC),A0   ; A0 = the handler
    211a: move.l  A0,(2,A6)        ; object +2  = HANDLER POINTER
    211e: move.w  #0x01DD,(6,A6)   ; object +6  = type

which is exactly the record layout the oracle capture reports
(docs/proof/obj_handlers.md). Measured on altbeast: 118 install sites, 108
distinct targets, and the OFFSET DISTRIBUTION is the interesting part --

    +0x24   103 sites    a SECOND per-object routine pointer
    +0x02    13 sites    the dispatch handler the object loop calls
    +0x6C     2 sites

Seeding them added 89 newly disassembled functions and took low-region code
coverage from 62.7% to 64.0%.
"""
import argparse, json, struct

ap = argparse.ArgumentParser()
ap.add_argument('rom')
ap.add_argument('--json')
ap.add_argument('--lo', type=lambda x: int(x, 0), default=0x400)
ap.add_argument('--hi', type=lambda x: int(x, 0), default=0x1F000)
a = ap.parse_args()
img = open(a.rom, 'rb').read()
be16 = lambda o: struct.unpack('>H', img[o:o + 2])[0]
s16 = lambda o: struct.unpack('>h', img[o:o + 2])[0]
LEA = {0x41FA: 0, 0x43FA: 1, 0x45FA: 2, 0x47FA: 3, 0x49FA: 4, 0x4BFA: 5, 0x4DFA: 6}
MOV = {0x2D48: 0, 0x2D49: 1, 0x2D4A: 2, 0x2D4B: 3, 0x2D4C: 4, 0x2D4D: 5, 0x2D4E: 6}

tg, sites = {}, 0
for o in range(0, len(img) - 16, 2):
    w = be16(o)
    if w not in LEA:
        continue
    reg = LEA[w]
    t = o + 2 + s16(o + 2)
    if not (a.lo <= t < a.hi) or t & 1:
        continue
    for k in range(2, 20, 2):
        w2 = be16(o + 2 + k)
        if w2 in MOV and MOV[w2] == reg:
            tg.setdefault(t, set()).add(s16(o + 4 + k))
            sites += 1
            break
        if w2 in LEA and LEA[w2] == reg:
            break                       # register reloaded before any store

import collections
off = collections.Counter(d for v in tg.values() for d in v)
print('install sites %d, distinct targets %d' % (sites, len(tg)))
print('by object-record offset:')
for d, n in off.most_common():
    print('  +0x%02X  %4d' % (d, n))
if a.json:
    json.dump({'seeds': sorted('%X' % t for t in tg),
               'function_seeds': sorted('%X' % t for t in tg)}, open(a.json, 'w'))
    print('-> %s' % a.json)
