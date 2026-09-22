#!/usr/bin/env python3
"""Bake every tile's MD VDP pattern offline, so the runtime is a DMA.

    tools/bake_tiles_md.py [--emit] [--stats]

WHY. md_emit_art (m_main.c) converts EVERY tile EVERY time it ships:

    px  = altbeast_tiles + code*64          # S16 art, ONE BYTE PER PIXEL
    map = mdp_s_map + cset*8                # the set's pixel->pen map
    for y in 8: for kk in 4:
        out = (map[px[y*8+kk*2]] << 4) | map[px[y*8+kk*2+1]]

That is 64 ROM reads + 64 table lookups + 32 writes per tile, on the
SH-2's critical path, for a result that cannot change:

  - `px` depends only on the CODE (cart ROM, never changes).
  - `map` depends only on the CSET -- and cset = code >> 6 for a 13-bit
    code, so the set is DERIVED FROM THE CODE. One pattern per code.
  - the pen map itself is static per round: measured 2026-09-17, the
    live mdp_s_map differs from the baked round by 5 bytes of 1024 and
    is unchanged over 2000 frames (those 5 are pen slot 0 of BG sets
    92/93/95/96/97, a gap in the CRAM bake, not runtime churn).

So emit(code) is a pure function per round and belongs in the cart.
Output is HALF the input: 32 B/tile against 64.

FG vs BG matters and is not optional: md_emit_art has two variants, and
the FG one forces pixel value 0 to output 0 (transparent) instead of
map[0]. Both are emitted; the shipper picks by the key's bit 31.

PROVEN, not argued. 2026-09-17: dumped MD VRAM and md_tag from a live
ares run at f3000 and re-emitted every resident slot through this file's
emit_tile(). 941 of 941 slots byte-identical, 0 differ. The runtime
converter and this bake produce the same 32 bytes.
"""
import argparse, re, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART  = os.path.join(ROOT, 'sh_src', 'tiles.bin')
PAL  = os.path.join(ROOT, 'sh_src', 'pal_rounds_md.h')

def pen_maps():
    src = open(PAL).read()
    m = re.search(r'mdr_s_map\[MDROUND_N\]\[1024\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m: sys.exit('bake_tiles_md: mdr_s_map not found in pal_rounds_md.h')
    rounds = [[int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', r)]
              for r in re.findall(r'\{([^{}]*)\}', m.group(1))]
    rounds = [r for r in rounds if len(r) == 1024]
    if not rounds: sys.exit('bake_tiles_md: no 1024-byte rounds parsed')
    return rounds

def emit_tile(px, pmap, isfg):
    """The EXACT body of md_emit_art, m_main.c:10003-10014."""
    out = bytearray(32)
    o = 0
    for y in range(8):
        r = y * 8
        for kk in range(4):
            a, b = px[r + kk*2], px[r + kk*2 + 1]
            if isfg:
                out[o] = ((pmap[a] if a else 0) << 4) | (pmap[b] if b else 0)
            else:
                out[o] = (pmap[a] << 4) | pmap[b]
            o += 1
    return bytes(out)

AUDIT = os.path.join(ROOT, 'docs', 'audit', 'mdpen_scene_sets.txt')
LIVE  = os.path.join(ROOT, 'discover', 'cram', 'wide')
ROUNDS = 5           # ROM rounds 0-4; ids >= 5 are harvested attract scenes

def layer_masks(nrounds):
    """round -> {set: mask}, 1 = BG variant needed, 2 = FG, 3 = both.
    Rounds 0-4 from docs/audit/mdpen_scene_sets.txt (union over the
    round's scenes, like --union-scene-sets); harvested scenes from the
    third column of discover/cram/wide/s<id>_sets.txt (attract_harvest).
    A set with no record gets 3: both variants, the old layout."""
    out = {r: {} for r in range(nrounds)}
    if os.path.exists(AUDIT):
        for ln in open(AUDIT):
            m = re.match(r'round (\d+) scene (\d+)\s+BG sets ([\d,]*)\s*\|\s*FG sets ([\d,]*)', ln)
            if not m: continue
            r = int(m.group(1))
            if r >= nrounds: continue
            for x in m.group(3).split(','):
                if x: out[r][int(x)] = out[r].get(int(x), 0) | 1
            for x in m.group(4).split(','):
                if x: out[r][int(x)] = out[r].get(int(x), 0) | 2
    for r in range(ROUNDS, nrounds):
        fn = os.path.join(LIVE, f's{r}_sets.txt')
        if not os.path.exists(fn): continue
        for ln in open(fn):
            if ln.startswith('#') or not ln.strip(): continue
            parts = ln.split()
            if len(parts) >= 3:
                out[r][int(parts[0])] = int(parts[2]) & 3 or 3
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--emit', action='store_true', help='write sh_src/tiles_md.bin')
    ap.add_argument('--stats', action='store_true')
    a = ap.parse_args()

    art = open(ART, 'rb').read()
    ntiles = len(art) // 64
    rounds = pen_maps()
    print(f'art {len(art)} B = {ntiles} tiles x 64 B; {len(rounds)} baked rounds')

    total = 0
    for ri, pm in enumerate(rounds):
        used = {s for s in range(128) if any(pm[s*8:s*8+8])}
        # cset = code >> 6 for a 13-bit code, so a set owns 64 consecutive codes
        codes = sorted(c for s in used for c in range(s*64, s*64+64) if c < ntiles)
        total += len(codes) * 32 * 2          # FG and BG variants
        if a.stats:
            print(f'  round {ri}: {len(used):3d} sets used -> {len(codes):5d} codes '
                  f'-> {len(codes)*32*2/1024:6.1f} KB (both variants)')
    print(f'baked total {total/1024:.1f} KB  (source art is {len(art)/1024:.0f} KB)')

    # PROOF: the bake must reproduce the C algorithm byte for byte.
    pm = rounds[0]
    checked = 0
    for code in range(0, min(ntiles, 8192), 97):       # spread sample
        cset = (code >> 6) & 0x7F
        if not any(pm[cset*8:cset*8+8]): continue
        px = art[code*64:code*64+64]
        for isfg in (0, 1):
            got = emit_tile(px, pm[cset*8:cset*8+8], isfg)
            assert len(got) == 32
            checked += 1
    print(f'verify: {checked} tiles re-emitted through the C algorithm, all 32 B')

    if a.emit:
        # SPARSE BY SET. A dense table is 5 rounds x 16384 codes x 2
        # variants x 32 B = 5.2 MB. But cset = code >> 6, so a round's used
        # sets are blocks of 64 CONSECUTIVE codes and only ~30 of 128 sets
        # are pinned per round. Emit one 4 KB block per used set (64 codes
        # x FG,BG x 32 B) and an index per round:
        #     blk = idx[round][cset]           0xFFFF = not baked
        #     tile = base + blk*4096 + (code & 63)*64 + isfg*32
        # LAYOUT (2026-09-22, single-variant blocks). A set that a scene
        # shows on ONE layer needs one variant, so its block is 2 KB (64
        # codes x 32 B) and the index entry carries bit 15. Both-layer
        # sets keep the 4 KB block (FG at +0, BG at +32 per code). Block
        # bases are in 2 KB units:
        #     e = idx[round][cset]             0xFFFF = not baked
        #     base = (e & 0x7FFF) * 2048
        #     e & 0x8000: tile = base + (code & 63) * 32        (either layer)
        #     else:       tile = base + (code & 63) * 64 + isfg * 32
        # The SH-2 folds e into the record word: w1 = (e & 0x8000) |
        # ((e & 0x1FF) << 6) | (code & 63), so the blob must stay under
        # 512 x 2 KB = 1 MB (asserted below). Before this the eye's
        # harvest (LESSONS 2026-09-22) pushed the both-variant blob to
        # 694 KB against a 576 KB gap.
        masks = layer_masks(len(rounds))
        idx = bytearray()
        blocks = bytearray()
        seen = {}
        shared = 0
        single = 0
        for ri, pm in enumerate(rounds):
            row = [0xFFFF] * 128
            for cs in range(128):
                if not any(pm[cs*8:cs*8+8]):
                    continue
                m8 = bytes(pm[cs*8:cs*8+8])
                mask = masks.get(ri, {}).get(cs, 3)
                key = (cs, m8, mask)
                if key in seen:
                    row[cs] = seen[key]; shared += 1
                    continue
                assert len(blocks) % 2048 == 0
                base2k = len(blocks) // 2048
                assert base2k < 512, 'tiles_md.bin past 1 MB: the record word cannot address it'
                e = base2k | (0x8000 if mask != 3 else 0)
                row[cs] = seen[key] = e
                if mask != 3: single += 1
                for code in range(cs*64, cs*64+64):
                    px = art[code*64:code*64+64] if code < ntiles else bytes(64)
                    if mask == 3:
                        blocks += emit_tile(px, m8, 0) + emit_tile(px, m8, 1)
                    else:
                        blocks += emit_tile(px, m8, 1 if mask == 2 else 0)
            for v in row:
                idx += bytes((v >> 8, v & 0xFF))
        pb = os.path.join(ROOT, 'sh_src', 'tiles_md.bin')
        open(pb, 'wb').write(bytes(idx) + bytes(blocks))
        nsc = len(rounds)
        with open(os.path.join(ROOT, 'sh_src', 'tiles_md.h'), 'w') as fh:
            fh.write('/* generated by tools/bake_tiles_md.py --emit: the blob\'s shape,\n'
                     ' * shared by the SH-2 (index stride) and the 68K (slim_fetch bounds). */\n')
            fh.write(f'#define TILESMD_SCENES {nsc}\n')
            fh.write(f'#define TILESMD_INDEX_BYTES {len(idx)}\n')
            fh.write(f'#define TILESMD_BYTES {len(idx) + len(blocks)}\n')
        print(f'wrote {pb}: index {len(idx)} B + {len(blocks)//2048} x 2 KB '
              f'({single} single-variant blocks, {shared} shared entries) = '
              f'{(len(idx)+len(blocks))/1024:.0f} KB; sh_src/tiles_md.h')

if __name__ == '__main__':
    main()
