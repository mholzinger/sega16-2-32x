#!/usr/bin/env python3
"""One-line scorecard of a port build from a fixed set of ares-headless dumps.

    tools/port_scorecard.py DIR PREFIX
reads DIR/PREFIX_{tilemap,palsh,vram,cram,cram32,f031}.bin, DIR/PREFIX_gf.csv
(frame,gameframe,<vint counter>) and DIR/PREFIX_s<N>.png, and prints what the
line produces at the same frame beside it when DIR/line_* exists. Every number
is read from the dump; nothing is derived from a listing.
"""
import sys, os, csv

def words(p):
    b = open(p, 'rb').read()
    return [int.from_bytes(b[2*i:2*i+2], 'big') for i in range(len(b)//2)]

def score(D, P):
    out = {}
    p = f'{D}/{P}_tilemap.bin'
    if os.path.exists(p):
        w = words(p); out['tilemap words'] = f'{sum(1 for x in w if x)}/{len(w)}'
    p = f'{D}/{P}_palsh.bin'
    if os.path.exists(p):
        w = words(p); out['PAL_SH words'] = f'{sum(1 for x in w if x)}/{len(w)}'
    p = f'{D}/{P}_vram.bin'
    if os.path.exists(p):
        v = open(p, 'rb').read()
        out['VRAM art slots'] = f'{sum(1 for s in range(0xB000//32) if any(v[s*32:s*32+32]))}/1408'
        nt = [int.from_bytes(v[0xC000+2*i:0xC000+2*i+2], 'big') for i in range(0x1000)]
        out['NT non-blank'] = f'{sum(1 for x in nt if (x & 0x7ff) != 0x3ff and x)}/4096'
    p = f'{D}/{P}_cram.bin'
    if os.path.exists(p):
        w = words(p); out['MD CRAM'] = f'{sum(1 for x in w if x)}/64'
    p = f'{D}/{P}_cram32.bin'
    if os.path.exists(p):
        w = words(p); out['32X CRAM'] = f'{sum(1 for x in w if x)}/256'
    p = f'{D}/{P}_f031.bin'
    if os.path.exists(p):
        out['f031'] = open(p, 'rb').read().hex()
    p = f'{D}/{P}_gf.csv'
    if os.path.exists(p):
        rows = list(csv.reader(open(p)))[1:]
        last = rows[-1]; vint = int(last[2], 16)
        out['vints/frame'] = f'{vint}/{int(last[0])+1} = {vint/(int(last[0])+1):.2f}'
    try:
        from PIL import Image
        for f in sorted(os.listdir(D)):
            if f.startswith(P + '_s') and f.endswith('.png'):
                im = Image.open(f'{D}/{f}').convert('RGB'); px = list(im.getdata())
                nb = sum(1 for r, g, b in px if r + g + b > 30)
                out[f'picture {f[len(P)+2:-4]}'] = f'{100*nb/len(px):.1f}%'
    except ImportError:
        pass
    return out

D, P = sys.argv[1], sys.argv[2]
a = score(D, P)
b = score(D, 'line') if P != 'line' else {}
for k, v in a.items():
    print(f'{k:16s} {P}: {v:18s}' + (f'line: {b[k]}' if k in b else ''))
