#!/usr/bin/env python3
"""Whole-game art census: several per-round sprite dumps (census_round.lua: spr.bin 1 KB a frame)
merged into one hot set. Same decode and sizes as tools/art_census.py.
Usage: art_census_all.py <sprites.bin> name=dir ...
"""
import struct, sys, collections, os
rom = open(sys.argv[1], 'rb').read()
W = struct.unpack('>%dH' % (len(rom) // 2), rom)
runs = [a.split('=') for a in sys.argv[2:]]

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
        cache[key] = (words, size_groups(rows))
    return cache[key]

per = {}
for name, d in runs:
    b = open(os.path.join(d, 'spr.bin'), 'rb').read()
    nfr = len(b) // 1024
    cnt = collections.Counter(); tot = zoomw = 0; pfr = []
    for k in range(nfr):
        blk = b[k * 1024:(k + 1) * 1024]; fl = []
        for i in range(64):
            e = struct.unpack('>8H', blk[i * 16:(i + 1) * 16])
            if e[2] & 0x8000: break
            if e[2] & 0x4000: continue
            top, bot = e[0] & 0xFF, e[0] >> 8
            if top >= bot: continue
            pitch = struct.unpack('b', bytes([e[2] & 0xFF]))[0]
            x = (e[1] & 0x1FF) - 184
            if x >= 320 or x < -4 * abs(pitch) - 8: continue
            key = ((e[4] >> 8) & 0xF, e[3], pitch, 1 if e[2] & 0x100 else 0, min(bot, 224) - top)
            nw, sg = art(key)
            tot += nw
            if e[5] & 0x3FF: zoomw += nw; continue
            cnt[key] += nw; fl.append((key, nw))
        pfr.append(fl)
    per[name] = (nfr, cnt, tot, zoomw, pfr)

print('%-8s %7s %7s %9s %6s  %s' % ('run', 'frames', 'uniq', 'groupsKB', 'zoom%', 'words/frame'))
union = collections.Counter()
for name, (nfr, cnt, tot, zoomw, pfr) in per.items():
    union.update(cnt)
    print('%-8s %7d %7d %9d %6.1f  %7.0f' % (name, nfr, len(cnt), sum(cache[k][1] for k in cnt) // 1024, 100 * zoomw / max(1, tot), tot / max(1, nfr)))
keys = sorted(union, key=lambda k: -union[k])
print('UNION: %d unique frames, %d KB groups (100%%)' % (len(keys), sum(cache[k][1] for k in keys) // 1024))
totw = sum(union.values())
# the hot set by whole-game words, and what it covers per run
for mark in (80, 90, 95, 99):
    acc = 0; sel = []
    for k in keys:
        if 100 * acc / totw >= mark: break
        acc += union[k]; sel.append(k)
    s = set(sel); kb = sum(cache[k][1] for k in sel) // 1024
    cov = ' '.join('%s %4.1f%%' % (n, 100 * sum(v for k, v in per[n][1].items() if k in s) / max(1, sum(per[n][1].values()))) for n in per)
    print('  top %4d frames = %2d%% of game words, %4d KB; per run: %s' % (len(sel), mark, kb, cov))
# the budgeted set: what fits in N KB, best-first
for budget in (256, 384, 512, 560):
    acc = 0; kb = 0; sel = set()
    for k in keys:
        if kb + cache[k][1] > budget * 1024: continue
        kb += cache[k][1]; acc += union[k]; sel.add(k)
    cov = ' '.join('%s %4.1f%%' % (n, 100 * sum(v for k, v in per[n][1].items() if k in sel) / max(1, sum(per[n][1].values()))) for n in per)
    print('  budget %3d KB: %4d frames, %4.1f%% of game words; per run: %s' % (budget, len(sel), 100 * acc / totw, cov))
shared = sum(1 for k in keys if sum(1 for n in per if k in per[n][1]) > 1)
print('frames drawn in 2+ runs: %d of %d' % (shared, len(keys)))

# THE PEAKS: for the budget sets, the baked share of the plotter's words on the heaviest frames of
# each run (top 10% by non-zoomed words), which is where the two-vint frames are
for budget in (384, 512, 560):
    kb = 0; sel = set()
    for k in keys:
        if kb + cache[k][1] > budget * 1024: continue
        kb += cache[k][1]; sel.add(k)
    line = []
    for n in per:
        pfr = per[n][4]
        tw = sorted(sum(nw for _, nw in fl) for fl in pfr)
        if not tw: continue
        thr = tw[int(len(tw) * 0.9)]
        heavy = [fl for fl in pfr if sum(nw for _, nw in fl) >= thr and fl]
        shares = [sum(nw for k, nw in fl if k in sel) / sum(nw for _, nw in fl) for fl in heavy]
        line.append('%s %4.1f%% (min %4.1f%%, %d words/frame at the peak)' % (n, 100 * sum(shares) / len(shares), 100 * min(shares), tw[-1]))
    print('  budget %3d KB, heaviest 10%% of frames baked share: %s' % (budget, '; '.join(line)))
