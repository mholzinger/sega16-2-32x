#!/usr/bin/env python3
"""bake_art.py: the baked-art blob for the slave's copy path (step 3 of the bake order,
docs: STATE.md "ART CENSUS FOR THE BAKED-ART RENDERER").

Reads sprite-RAM dumps of arcade play (tools/arcade_round_census.lua / gameplay_dump.lua
GD_SPRDUMP: 1 KB a frame), ranks every distinct sprite frame (bank, addr, pitch, flip) by the
ROM words the plotter would spend on it over the whole corpus, and packs the best-first set
that fits the budget in the groups format of tools/s16art.py, tallest height seen per key.
Zoomed records stay on the plotter and do not count.

  python3 tools/bake_art.py --rom sh_src/sprites.bin --chunks ADDR:BYTES,... \\
      --out sh_src/artbake.bin --hdr sh_src/artbake.h r0=build/gate/neff/art/round0 ...

Blob layout (big-endian, what the SH-2 reads):
  0   'AB1\\0'
  4   u32 n            index entries
  8   u32 bytes        whole blob
  12  u32 reserved
  16  n x { u32 key, u32 offset }   sorted by key; key = flip<<28 | bank<<24 | (pitch&0xFF)<<16 | addr
  then the frames (each 4-byte aligned) in s16art.pack_frame form

Under --chunks the index sits at the head of chunk 0 and every frame goes to the first window
with room, the index carrying its SH-2 address; --out gets a _N suffix per chunk.

PER-LEVEL PAGES (--pages, 2026-11-04): chunk 0 is the tail of a 512 KB mapper page that the
68K switches with the level, so there is one chunk-0 body PER PAGE and the index in it covers
the GLOBAL frames (chunks 1.., shared by every page) plus that page's own. --pages names the
runs that own each page in order, '+'-joined when several runs are one level:
  --pages r0+tape,r1,r2,r3,r4        page 8 = r0 and the tape, page 9 = r1, ...
Global = the frames seen in two or more pages, best-first by whole-corpus words, into chunks
1..; then each page's tail takes the rest best-first by THAT page's words. Outputs: chunks 1..
as <out>_N, page 0's tail as <out>_0, the other pages' tails as <out>_pP (P = 9, 10, ...).

The self-check re-reads every frame of every page through its index with s16art.unpack_frame
and compares it pixel for pixel against a fresh decode: a packing bug fails the BUILD, not
Mike's eye.
"""
import argparse, collections, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s16art  # noqa: E402

FIRST_PAGE = 8


def keyword(key):
    """bits 0-15 addr, 16-23 pitch (signed byte), 24-26 bank, 28 flip. The flip bit sat at 23
    once, on top of the pitch's sign (2026-11-04): 23 of 115 frames indexed under the wrong
    key, missed by a self-check that compared Python keys instead of the words it wrote."""
    bank, addr, pitch, flip = key
    return (flip << 28) | (bank << 24) | ((pitch & 0xFF) << 16) | addr


def unkeyword(kw):
    pitch = (kw >> 16) & 0xFF
    return ((kw >> 24) & 7, kw & 0xFFFF, pitch - 256 if pitch > 127 else pitch, (kw >> 28) & 1)


def padded(b):
    return len(b) + (-len(b) & 3)


def fit(ranked, frames, chunks, reserve):
    """best-first first-fit of ranked keys into chunks; chunk 0's head holds an index of
    reserve(nchosen) bytes, iterated to a fixed point (the count only shrinks). Returns the
    chosen list in ranking order and the index reservation in entries."""
    nidx = len(ranked)
    chosen = []
    for _round in range(8):
        fill = [0] * len(chunks)
        fill[0] = reserve(nidx)
        chosen = []
        for key in ranked:
            need = padded(frames[key])
            for c, (base, size) in enumerate(chunks):
                if fill[c] + need <= size:
                    fill[c] += need
                    chosen.append(key)
                    break
        if len(chosen) <= nidx and _round:
            break
        nidx = len(chosen)
    assert len(chosen) <= nidx
    return chosen, nidx


def place_all(chosen, frames, chunks, bodies, place):
    """append the chosen frames to the chunk bodies first-fit in the given order"""
    for key in chosen:
        need = padded(frames[key])
        for c, (base, size) in enumerate(chunks):
            if len(bodies[c]) + need <= size:
                place[key] = (c, len(bodies[c]))
                bodies[c] += frames[key] + b'\0' * (-len(frames[key]) & 3)
                break
        else:
            raise SystemExit('placement disagreed with the fitting pass for %s' % (key,))


def build_index(entries, nidx, place, chunks, body0_frames):
    """chunk-0 body: the index (nidx entries reserved) then this page's frames; place gets
    the page frames' offsets. entries = every key the index lists."""
    body = bytearray(b'AB1\0' + struct.pack('>III', len(entries), 0, 0) + b'\0' * (8 * nidx))
    for key in body0_frames:
        place[key] = (0, len(body))
        body += FR[key] + b'\0' * (-len(FR[key]) & 3)
    for i, key in enumerate(sorted(entries, key=keyword)):
        c, o = place[key]
        struct.pack_into('>II', body, 16 + 8 * i, keyword(key), chunks[c][0] + o)
    return body


FR = {}


def self_check(body0, bodies, place, chunks, W, hmax, label):
    n = struct.unpack_from('>I', body0, 4)[0]
    bad = 0
    for i in range(n):
        kw, addr = struct.unpack_from('>II', body0, 16 + 8 * i)
        key = unkeyword(kw)
        if key not in place:
            print('%s: index entry %d decodes to a key that was never baked: %08x -> %s' % (label, i, kw, key)); bad += 1; continue
        c, o = place[key]
        if addr != chunks[c][0] + o:
            print('%s: index entry %d address %08x is not where %s was placed' % (label, i, addr, key)); bad += 1; continue
        if i and kw <= struct.unpack_from('>I', body0, 16 + 8 * (i - 1))[0]:
            print('%s: index not sorted/unique at %d' % (label, i)); bad += 1
        src = body0 if c == 0 else bodies[c]
        rows, rownw, _ = s16art.unpack_frame(bytes(src), o)
        ref, _, refnw = s16art.decode(W, *key, hmax[key])
        if len(rows) != len(ref):
            bad += 1; print('%s: height mismatch %s' % (label, key)); continue
        if rownw != refnw:
            bad += 1; print('%s: row word counts differ for key %s' % (label, key)); continue
        for r, (p, q) in enumerate(zip(rows, ref)):
            m = max(len(p), len(q))
            if list(p) + [0] * (m - len(p)) != list(q) + [0] * (m - len(q)):
                bad += 1; print('%s: row %d differs for key %s' % (label, r, key)); break
    return n, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default='sh_src/sprites.bin')
    ap.add_argument('--budget-kb', type=int, default=None, help='one blob of this size (--out is its file)')
    ap.add_argument('--chunks', default=None,
                    help='addr:bytes,... the cart windows the bake may use (SH-2 addresses); the index '
                         'then carries absolute addresses and --out gets a _N suffix per chunk')
    ap.add_argument('--pages', default=None,
                    help="per-level pages: the runs owning page 8, 9, ... in order, '+'-joined per page "
                         "(needs --chunks: chunk 0 is each page's tail, chunks 1.. are global)")
    ap.add_argument('--out', required=True)
    ap.add_argument('--hdr', default=None)
    ap.add_argument('--from-frame', type=int, default=0, help='skip the first N dumped frames of every run')
    ap.add_argument('runs', nargs='+', help='name=dir (dir holds spr.bin)')
    a = ap.parse_args()
    W = s16art.load_rom(a.rom)
    wordcache = {}

    def words(key, h):
        k = (key, h)
        if k not in wordcache:
            wordcache[k] = s16art.decode(W, *key, h)[1]
        return wordcache[k]

    weight = collections.Counter()           # key -> words over the whole corpus
    hmax = {}
    per = {}                                 # run -> (frames, total words, words per (key, h))
    for spec in a.runs:
        name, d = spec.split('=')
        b = open(os.path.join(d, 'spr.bin'), 'rb').read()
        nfr = len(b) // 1024
        tot = 0
        bykey = collections.Counter()
        for k in range(a.from_frame, nfr):
            for key, h, zoomed in s16art.records(b[k * 1024:(k + 1) * 1024]):
                nw = words(key, h)
                tot += nw
                if zoomed:
                    continue
                bykey[(key, h)] += nw
                weight[key] += nw
                if h > hmax.get(key, 0):
                    hmax[key] = h
        per[name] = (nfr - a.from_frame, tot, bykey)
        print('%-6s %6d frames, %d words, %d keys so far' % (name, nfr - a.from_frame, tot, len(weight)))

    if a.chunks:
        chunks = [(int(x.split(':')[0], 0), int(x.split(':')[1], 0)) for x in a.chunks.split(',')]
    else:
        chunks = [(0, a.budget_kb * 1024)]
    ranked = sorted(weight, key=lambda k: -weight[k])
    for key in list(ranked):
        rows, _, rownw = s16art.decode(W, *key, hmax[key])
        try:
            FR[key] = s16art.pack_frame(rows, rownw)
        except ValueError:
            ranked.remove(key)          # a row the header cannot describe: the plotter keeps it
    stem, ext = os.path.splitext(a.out)
    reserve = lambda n: 16 + 8 * n       # noqa: E731

    if not a.pages:
        chosen, nidx = fit(ranked, FR, chunks, reserve)
        order = sorted(chosen, key=lambda k: -weight[k])
        bodies = [bytearray() for _ in chunks]
        bodies[0] += b'\0' * reserve(nidx)          # placeholder: the index goes in last
        place = {}
        place_all(order, FR, chunks, bodies, place)
        body0 = bytearray(b'AB1\0' + struct.pack('>III', len(chosen), 0, 0) + b'\0' * (8 * nidx)) + bodies[0][reserve(nidx):]
        for i, key in enumerate(sorted(chosen, key=keyword)):
            c, o = place[key]
            struct.pack_into('>II', body0, 16 + 8 * i, keyword(key), chunks[c][0] + o)
        bodies[0] = body0
        struct.pack_into('>I', bodies[0], 8, sum(len(b) for b in bodies))
        n, bad = self_check(bodies[0], bodies, place, chunks, W, hmax, 'blob')
        if bad:
            print('SELF-CHECK FAILED: %d frames' % bad); sys.exit(1)
        chosen_set = set(chosen)
        tw = sum(weight.values())
        cw = sum(weight[k] for k in chosen)
        total = sum(len(b) for b in bodies)
        budget = sum(s for _, s in chunks)
        print('bake: %d of %d keys, %d bytes of %d (%.1f KB in %d window%s), %.1f%% of the corpus plotter words'
              % (len(chosen), len(weight), total, budget, total / 1024, len(chunks), 's' if len(chunks) > 1 else '', 100 * cw / max(1, tw)))
        for c, (base, size) in enumerate(chunks):
            print('  window %d at 0x%08X: %6d of %6d bytes' % (c, base, len(bodies[c]), size))
        for name, (nfr, tot, bykey) in per.items():
            served = sum(v for (k, h), v in bykey.items() if k in chosen_set and h <= hmax[k])
            print('  %-6s %5.1f%% of its words served (%d words/frame)' % (name, 100 * served / max(1, tot), tot / max(1, nfr)))
        if a.chunks:
            for c, b in enumerate(bodies):
                with open('%s_%d%s' % (stem, c, ext), 'wb') as f:
                    f.write(b)
        else:
            with open(a.out, 'wb') as f:
                f.write(bodies[0])
        if a.hdr:
            with open(a.hdr, 'w') as f:
                f.write('/* generated by tools/bake_art.py: do not edit */\n')
                f.write('#define ARTBAKE_N %d\n#define ARTBAKE_BYTES %d\n#define ARTBAKE_WINDOWS %d\n' % (len(chosen), total, len(chunks)))
                for c, (base, size) in enumerate(chunks):
                    f.write('#define ARTBAKE_WIN%d_ADDR 0x%08XU\n#define ARTBAKE_WIN%d_BYTES %d\n' % (c, base, c, len(bodies[c])))
        print('self-check passed: %d frames re-read through the index match a fresh decode' % n)
        return

    # ---- per-level pages ----
    if len(chunks) < 2:
        raise SystemExit('--pages needs --chunks with the page tail first and at least one global window')
    pages = [p.split('+') for p in a.pages.split(',')]
    for p in pages:
        for r in p:
            if r not in per:
                raise SystemExit('--pages names run %s, which was not given' % r)
    pw = []                                                    # per page: key -> words
    for p in pages:
        c = collections.Counter()
        for r in p:
            for (k, h), v in per[r][2].items():
                c[k] += v
        pw.append(c)
    npages = len(pages)
    shared = [k for k in ranked if sum(1 for c in pw if c[k]) >= 2]
    gchunks = [(0, 0)] + chunks[1:]                            # chunk 0 takes nothing here
    gchosen, _ = fit(shared, FR, gchunks, lambda n: 0)
    gorder = sorted(gchosen, key=lambda k: -weight[k])
    bodies = [bytearray() for _ in chunks]
    place = {}
    place_all(gorder, FR, gchunks, bodies, place)
    gset = set(gchosen)
    print('global: %d of %d shared keys (%d keys in all) in windows 1..%d, %d bytes'
          % (len(gchosen), len(shared), len(ranked), len(chunks) - 1, sum(len(b) for b in bodies[1:])))

    page_bodies = []
    tw = sum(weight.values())
    cw_all = 0
    served_pages = []
    for pi, p in enumerate(pages):
        prank = [k for k in sorted(pw[pi], key=lambda k: -pw[pi][k]) if k not in gset and k in FR and pw[pi][k]]
        ng = len(gchosen)
        pchosen, nidx = fit(prank, FR, [chunks[0]], lambda n: reserve(ng + n))
        porder = sorted(pchosen, key=lambda k: -pw[pi][k])
        pplace = dict(place)
        entries = gchosen + pchosen
        body0 = build_index(entries, ng + nidx, pplace, chunks, porder)
        struct.pack_into('>I', body0, 8, len(body0) + sum(len(b) for b in bodies[1:]))
        n, bad = self_check(body0, bodies, pplace, chunks, W, hmax, 'page %d' % (FIRST_PAGE + pi))
        if bad:
            print('SELF-CHECK FAILED on page %d: %d frames' % (FIRST_PAGE + pi, bad)); sys.exit(1)
        page_bodies.append(body0)
        pset = gset | set(pchosen)
        served_pages.append(pset)
        pws = sum(pw[pi].values())
        pcov = sum(pw[pi][k] for k in pset)
        cw_all += pcov
        print('page %d (%s): %d own frames + %d global, tail %d of %d bytes, %.1f%% of its words'
              % (FIRST_PAGE + pi, '+'.join(p), len(pchosen), ng, len(body0), chunks[0][1], 100 * pcov / max(1, pws)))
    print('bake: %.1f%% of the corpus plotter words served by the level pages (global windows alone %.1f%%)'
          % (100 * cw_all / max(1, tw), 100 * sum(weight[k] for k in gchosen) / max(1, tw)))
    for c, (base, size) in enumerate(chunks):
        if c:
            print('  window %d at 0x%08X: %6d of %6d bytes' % (c, base, len(bodies[c]), size))
    for pi, p in enumerate(pages):
        for name in p:
            nfr, tot, bykey = per[name]
            served = sum(v for (k, h), v in bykey.items() if k in served_pages[pi] and h <= hmax[k])
            print('  %-6s %5.1f%% of its words served on page %d (%d words/frame)' % (name, 100 * served / max(1, tot), FIRST_PAGE + pi, tot / max(1, nfr)))
    for c in range(1, len(chunks)):
        with open('%s_%d%s' % (stem, c, ext), 'wb') as f:
            f.write(bodies[c])
    for pi, body0 in enumerate(page_bodies):
        name = '%s_0%s' % (stem, ext) if pi == 0 else '%s_p%d%s' % (stem, FIRST_PAGE + pi, ext)
        with open(name, 'wb') as f:
            f.write(body0)
    if a.hdr:
        with open(a.hdr, 'w') as f:
            f.write('/* generated by tools/bake_art.py: do not edit */\n')
            f.write('#define ARTBAKE_N %d\n#define ARTBAKE_BYTES %d\n#define ARTBAKE_WINDOWS %d\n'
                    % (len(gchosen), sum(len(b) for b in bodies[1:]) + max(len(b) for b in page_bodies), len(chunks)))
            f.write('#define ARTBAKE_PAGES %d\n#define ARTBAKE_FIRST_PAGE %d\n' % (npages, FIRST_PAGE))
            for c, (base, size) in enumerate(chunks):
                f.write('#define ARTBAKE_WIN%d_ADDR 0x%08XU\n#define ARTBAKE_WIN%d_BYTES %d\n'
                        % (c, base, c, max(len(b) for b in page_bodies) if c == 0 else len(bodies[c])))
    print('self-check passed: every page re-read through its index matches a fresh decode')


if __name__ == '__main__':
    main()
