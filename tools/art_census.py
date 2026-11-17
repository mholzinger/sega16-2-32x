#!/usr/bin/env python3
"""Art census for the baked-art renderer, over the arcade's per-frame sprite table
(gameplay_dump.lua GD_SPRDUMP: 1 KB a frame from GD_SPRFROM): which sprite art is drawn, how
often, and what it costs baked.

Decode follows the slave plotter (r60_main.c PLOT5 / ROWFAST, jts16_obj_scan.v:78, jts16_obj_draw.v:115):
  row r of a non-zoomed record reads words from addr + pitch*(r+1) (mod 64K words in its bank),
  forward (flip: backward), four 4-bit pens a word, high nibble first (flip: low first); pen 0 and
  pen 15 draw nothing; the row ends after the word whose last-drawn nibble is 15.
A frame = (bank, addr, pitch, flip, height). Baked size counted two ways:
  runs:   per row 2 B + per opaque run 2 B (skip,len) + len pens, 1 B a pen        (bake_sprites.py)
  groups: per row 4 B + the opaque span [first..last pixel) padded to 4-byte groups, 1 B a pen
          (what a longword copy wants; x4 if the four alignments are pre-shifted)
Usage: art_census.py <spr.bin> <from_frame> <sprites.bin> <timeline.csv> [win=lo-hi ...]
Scenes: level (scroll < 0x1800) / arena (scroll >= 0x1800, once in play); windows by arcade frame.
"""
import struct, sys, collections, csv
sprbin, from_frame, sprom, tlpath = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
wins = [(w.split('=')[0], tuple(int(v) for v in w.split('=')[1].split('-'))) for w in sys.argv[5:]]
b = open(sprbin, 'rb').read()
nfr = len(b) // 1024
rom = open(sprom, 'rb').read()
W = struct.unpack('>%dH' % (len(rom) // 2), rom)
scroll = {}
for r in csv.DictReader(open(tlpath)):
    scroll[int(r['frame'])] = (int(r['scroll'], 16), int(r['f028'], 16) & 1)

def decode(bank, addr, pitch, flip, h):
    base = (bank & 7) * 0x10000
    rows = []; words = 0; a = addr
    for r in range(h):
        a = (a + pitch) & 0xFFFF
        o = a; px = []
        for k in range(256):
            w = W[base + (o & 0xFFFF)]
            if flip: nib = [w & 15, (w >> 4) & 15, (w >> 8) & 15, w >> 12]; o -= 1
            else:    nib = [w >> 12, (w >> 8) & 15, (w >> 4) & 15, w & 15]; o += 1
            px += [p if 1 <= p <= 14 else 0 for p in nib]
            words += 1
            if nib[3] == 15: break
        rows.append(px)
    return rows, words

def size_runs(rows):
    n = 0
    for px in rows:
        n += 2; i = 0; L = len(px)
        while i < L:
            while i < L and px[i] == 0: i += 1
            j = i
            while j < L and px[j] != 0: j += 1
            if j > i: n += 2 + (j - i); i = j
    return n

def size_groups(rows):
    n = 0
    for px in rows:
        n += 4
        nz = [i for i, p in enumerate(px) if p]
        if nz: n += ((nz[-1] + 4) & ~3) - (nz[0] & ~3)
    return n

cache = {}
def art(key):
    if key not in cache:
        rows, words = decode(*key)
        cache[key] = (rows, words, size_runs(rows), size_groups(rows))
    return cache[key]

use = {'level': collections.Counter(), 'arena': collections.Counter()}
inst = collections.Counter()
stat = collections.defaultdict(lambda: collections.Counter())    # scene/window -> sums
nframes = collections.Counter()
pf = []                                                           # per frame: (frame, recs, words, zwords, copy_bytes)
inplay = False; arena_at = None
for k in range(nfr):
    f = from_frame + k
    blk = b[k * 1024:(k + 1) * 1024]
    sc, play = scroll.get(f, (0, 0))
    if play: inplay = True
    if inplay and sc >= 0x1800 and arena_at is None: arena_at = f
    scene = 'arena' if arena_at is not None else 'level'
    tags = [scene] + [n for n, (lo, hi) in wins if lo <= f <= hi]
    recs = words = zwords = zrecs = copyb = runb = 0
    for i in range(64):
        e = struct.unpack('>8H', blk[i * 16:(i + 1) * 16])
        if e[2] & 0x8000: break
        if e[2] & 0x4000: continue
        top, bot = e[0] & 0xFF, e[0] >> 8
        if top >= bot: continue
        pitch = struct.unpack('b', bytes([e[2] & 0xFF]))[0]
        x = (e[1] & 0x1FF) - 184
        if x >= 320 or x < -4 * abs(pitch) - 8: continue
        h = min(bot, 224) - top
        hz, vz = e[5] & 0x1F, (e[5] >> 5) & 0x1F
        key = ((e[4] >> 8) & 0xF, e[3], pitch, 1 if e[2] & 0x100 else 0, h)
        rows, nw, sr, sg = art(key)
        recs += 1; words += nw
        if hz or vz: zwords += nw; zrecs += 1; continue
        use[scene][key] += nw; inst[key] += 1
        copyb += sg; runb += sr
    pf.append((f, recs, words, zwords, copyb))
    for t in tags:
        nframes[t] += 1
        s = stat[t]; s['recs'] += recs; s['words'] += words; s['zwords'] += zwords; s['zrecs'] += zrecs
        s['copy'] += copyb; s['runs'] += runb
        if words > s['maxw']: s['maxw'] = words
        if copyb > s['maxc']: s['maxc'] = copyb

print('%d frames %d..%d, arena from %s' % (nfr, from_frame, from_frame + nfr - 1, arena_at))
print('%-10s %6s %6s %8s %8s %6s %8s %8s   %s' % ('scene', 'frames', 'rec/f', 'words/f', 'max', 'zoom%w', 'copyB/f', 'max', 'copy bytes = baked groups the new path stores a frame; words = source words the plotter decodes today'))
for t in ['level', 'arena'] + [n for n, _ in wins]:
    n = nframes[t]
    if not n: continue
    s = stat[t]
    print('%-10s %6d %6.1f %8.0f %8d %6.1f %8.0f %8d' % (t, n, s['recs'] / n, s['words'] / n, s['maxw'], 100 * s['zwords'] / max(1, s['words']), s['copy'] / n, s['maxc']))

def report(name, cnt):
    keys = sorted(cnt, key=lambda k: -cnt[k])
    tot_w = sum(cnt.values())
    r = sum(cache[k][2] for k in keys); g = sum(cache[k][3] for k in keys)
    print('%s: %d unique frames; baked runs %d KB, groups %d KB (x4 alignments %d KB); %d row-words drawn' % (
        name, len(keys), r // 1024, g // 1024, 4 * g // 1024, tot_w))
    acc = accr = accg = 0; marks = [50, 80, 90, 95, 99, 100]
    for n, k in enumerate(keys, 1):
        acc += cnt[k]; accr += cache[k][2]; accg += cache[k][3]
        while marks and 100 * acc / tot_w >= marks[0]:
            print('   top %4d frames cover %3d%% of row-words: runs %4d KB, groups %4d KB' % (n, marks[0], accr // 1024, accg // 1024))
            marks.pop(0)
    both = sum(1 for k in keys if k[3] and (k[0], k[1], k[2], 0, k[4]) in cnt)
    print('   flipped frames %d, of which %d also drawn unflipped' % (sum(1 for k in keys if k[3]), both))
    hv = collections.Counter((k[0], k[1], k[2], k[3]) for k in keys)
    print('   distinct (bank,addr,pitch,flip) %d; with 2+ heights %d' % (len(hv), sum(1 for v in hv.values() if v > 1)))
    print('   top 10 by row-words:')
    for k in keys[:10]:
        rows, words, sr, sg = cache[k]
        print('     bank %d addr %04x pitch %4d flip %d h %3d: %8d words (%5d inst, %2d w/row max) runs %5d B groups %5d B' % (
            k[0], k[1], k[2], k[3], k[4], cnt[k], inst[k], max(len(px) for px in rows) // 4, sr, sg))

report('LEVEL 1 (to the arena)', use['level'])
report('ARENA (smoke + Neff)', use['arena'])
both = collections.Counter(use['level']); both.update(use['arena'])
report('ALL', both)
