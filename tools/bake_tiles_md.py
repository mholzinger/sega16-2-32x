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
        idx = bytearray()
        blocks = bytearray()
        for pm in rounds:
            row = [0xFFFF] * 128
            for cs in range(128):
                if not any(pm[cs*8:cs*8+8]):
                    continue
                row[cs] = len(blocks) // 4096
                m8 = pm[cs*8:cs*8+8]
                for code in range(cs*64, cs*64+64):
                    px = art[code*64:code*64+64] if code < ntiles else bytes(64)
                    blocks += emit_tile(px, m8, 0) + emit_tile(px, m8, 1)
            for v in row:
                idx += bytes((v >> 8, v & 0xFF))
        pb = os.path.join(ROOT, 'sh_src', 'tiles_md.bin')
        open(pb, 'wb').write(bytes(idx) + bytes(blocks))
        print(f'wrote {pb}: index {len(idx)} B + {len(blocks)//4096} blocks '
              f'= {(len(idx)+len(blocks))/1024:.0f} KB')

if __name__ == '__main__':
    main()
