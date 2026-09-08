#!/usr/bin/env python3
"""P3 M1 — bake mob-class sprite art into MD VDP tile format.

    python3 tools/bake_mdspr.py            # regenerate outputs
    python3 tools/bake_mdspr.py --stats    # print budget table only

Consumes discover/play.csv + discover/attract.csv (the census keys) and
sh_src/sprites.bin (the interleaved sprite ROM), REUSING bake_sprites'
decoder — the one that ships with a byte-identical replay gate — so the
pixel walk here is the proven one, not a re-derivation.

Emits, for the v1 class list (S16 colour sets 0x00, 0x10, 0x0B):
  sh_src/md_sprart.bin   MD 4bpp tiles, per-key per-subsprite,
                         column-major within a subsprite (the VDP's
                         multi-tile order: tileNumber = tileX*htiles).
                         Uploaded verbatim to VRAM 0x8000 at boot.
  sh_src/md_sprart.h     SH-2 claim-pass index: per key the SPR_SNAP
                         match fields + subsprite SAT templates.
  md_src/md_sprart_info.h  68K boot-upload constants (length; the cart
                         offset is FIXED by mars.ld at 0x2F0000).

Pens: 1..14 pass through (MD pen == S16 pen, so MD CRAM line 0 holds
the live S16 palette 1:1); 0 and 15 (strip end / mid-row skip) become
MD pen 0 = transparent. A key's rect width is the max drawn extent
over its rows; subsprites tile the rect in <=32px chunks both axes.

VRAM budget: MDSPR_VRAM_BASE 0x8000, hard cap 0xB000 (the region the
existing tile-upload guard already admits and nothing else uses). The
build FAILS if the class list exceeds it.
"""
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_sprites as bs

ROOT = Path(__file__).resolve().parent.parent
OUT_BIN = ROOT / 'sh_src' / 'md_sprart.bin'
OUT_H = ROOT / 'sh_src' / 'md_sprart.h'
OUT_MD_H = ROOT / 'md_src' / 'md_sprart_info.h'

# v2 (2026-08-28): ONE MD palette line exists (line 0; the BG allocator
# measured 1/1/4 free pens — a line steal would evict live BG colours),
# so ONE anchored colour set at a time. v3 (2026-09-01, docs/design/BOSSFIGHT.md):
# PER-SCENE tables — the zombies and the boss never share a screen, so
# each scene gets its own art blob (uploaded at the scene cut), key
# slice, and anchor. anchor=None = DYNAMIC: the runtime tracks which
# of the scene's sets owns the records (the boss head sets 0x23/0x24
# flip across silhouette phases) with sustained-majority hysteresis.
# boss.csv is synthesized from Mike's savestates (tools/boss_census.py)
# — the bots never reach the boss.
SCENES = [
    # (name, csv, sets, min_count, anchor or None=dynamic)
    ('normal', 'play.csv', (0x09,), 40, 0x09),
    ('boss',   'boss.csv', None, 1, None),   # sets=None: set-agnostic (0xFF wildcard keys)
]
VRAM_BASE = 0x8000
VRAM_CAP = 0xB000
TILE0 = VRAM_BASE // 32


def load_keys(name, sets, min_count):
    """Colour sets are per-scene overloaded -- attract scenes reuse set
    numbers for different actors (measured: adding attract.csv grows the
    blob 12K -> 70K). Unknown records stay SH-2-composed."""
    keys = {}
    for name in (name,):
        p = ROOT / 'discover' / name
        if not p.exists():
            continue
        for r in csv.DictReader(open(p)):
            if int(r['count']) < min_count:
                continue
            if r['native'] != '1':
                continue
            cset = int(r['w4'], 16) & 0x3F
            if sets is None:
                cset = 0xFF              # wildcard: art-identity keys
            elif cset not in sets:
                continue
            k = r['key']
            if k in keys:
                keys[k]['count'] += int(r['count'])
                continue
            keys[k] = {
                'key': k,
                'bank': int(r['bank']),
                'addr': int(r['addr'], 16),
                'd2lo9': int(r['w2'], 16) & 0x1FF,   # pitch | flip<<8
                'pitch': int(r['pitch']),
                'flip': int(r['flip']),
                'height': int(r['height']),
                'set': cset,
                'count': int(r['count']),
            }
    return sorted(keys.values(), key=lambda k: -k['count'])


def rect_of(rom, k):
    """Decode a key to a dense pen rectangle (pens 0..14, 0=clear)."""
    pitch = k['pitch'] if k['pitch'] < 128 else k['pitch'] - 256
    rows = bs.decode_frame(rom, k['bank'], k['addr'], pitch,
                           k['flip'], k['height'])
    w = 0
    grid = []
    for pens in rows:
        line = [p if 1 <= p <= 14 else 0 for p in pens]
        while line and line[-1] == 0:
            line.pop()
        w = max(w, len(line))
        grid.append(line)
    for line in grid:
        line.extend([0] * (w - len(line)))
    return grid, w, len(grid)


def chunks(n, cap=32):
    out = []
    o = 0
    while o < n:
        c = min(cap, n - o)
        out.append((o, c))
        o += c
    return out


def emit_key(grid, w, h, blob, tile_rel):
    """Split the rect into <=32x32 subsprites; append column-major MD
    tiles to blob. Returns ([(dx,dy,size,tile)], tiles_emitted)."""
    subs = []
    for dy, ch in chunks(h):
        for dx, cw in chunks(w):
            wt = (cw + 7) // 8
            ht = (ch + 7) // 8
            for tx in range(wt):            # column-major
                for ty in range(ht):
                    for r in range(8):
                        y = dy + ty * 8 + r
                        b = bytearray(4)
                        for px in range(8):
                            x = dx + tx * 8 + px
                            pen = grid[y][x] if (y < h and x < w) else 0
                            if px & 1:
                                b[px >> 1] |= pen
                            else:
                                b[px >> 1] |= pen << 4
                        blob.extend(b)
            size = ((wt - 1) << 2) | (ht - 1)
            subs.append((dx, dy, size, tile_rel))
            tile_rel += wt * ht
    return subs, tile_rel


def main():
    rom = bs.Rom(bs.load_rom())
    blob = bytearray()
    scenes_out = []          # (name, key0, nkeys, woff, words, anchor)
    all_baked = []           # (k, w, h, subs) across scenes
    art_cache = {}           # (bank,addr,d2lo9,height) -> (subs, w, h)
    tile_rel_by_blob = {}
    for name, srccsv, sets, min_count, anchor in SCENES:
        keys = load_keys(srccsv, sets, min_count)
        woff = (len(blob) + 1) // 2
        # each scene's blob starts a fresh tile space at VRAM_BASE
        tile_rel = 0
        blob_start = len(blob)
        key0 = len(all_baked)
        cache = {}           # per-scene art cache (same VRAM window)
        dropped = []
        for k in keys:
            art = (k['bank'], k['addr'], k['d2lo9'], k['height'])
            if art in cache:
                subs, w, h = cache[art]
            else:
                grid, w, h = rect_of(rom, k)
                if w == 0 or h == 0:
                    continue
                # GREEDY BUDGET (keys arrive count-sorted): a key that
                # would overflow the VRAM window stays SH-2-composed —
                # art budget goes to the mobs, never a hard fail
                trial = bytearray()
                subs, _tr = emit_key(grid, w, h, trial, tile_rel)
                if len(blob) - blob_start + len(trial) > \
                        VRAM_CAP - VRAM_BASE:
                    dropped.append((k['key'], k['count'], len(trial)))
                    continue
                blob.extend(trial)
                tile_rel = _tr
                cache[art] = (subs, w, h)
            all_baked.append((k, w, h, subs))
        if dropped:
            print(f"  scene {name}: DROPPED {len(dropped)} keys "
                  f"(stay SH-2): " +
                  ", ".join(f"{k} x{c} ({b}B)" for k, c, b in dropped))
        swords = (len(blob) - blob_start + 1) // 2
        scenes_out.append((name, key0, len(all_baked) - key0,
                           woff, swords, anchor, sets))
        print(f"  scene {name}: keys {len(all_baked) - key0} "
              f"art {len(blob) - blob_start}B "
              f"({len(cache)} unique rects)")
    used = len(blob)
    print(f"total blob {used}B cart")
    if '--stats' in sys.argv:
        return

    OUT_BIN.write_bytes(bytes(blob))

    subs_flat = []
    lines = [
        "/* GENERATED by tools/bake_mdspr.py -- do not edit. */",
        f"#define MDSPR_NKEYS {len(all_baked)}",
        f"#define MDSPR_TILE0 {TILE0}   /* VRAM 0x{VRAM_BASE:04X} */",
        f"#define MDSPR_NSCENES {len(scenes_out)}",
        "typedef struct { unsigned short addr; unsigned short d2lo9;",
        "    unsigned char bank, height, set, wpx, hpx, nsub;",
        "    unsigned short sub0; } mdspr_key_t;",
        "typedef struct { unsigned char dx, dy, size, pad;",
        "    unsigned short tile; } mdspr_sub_t;",
        "typedef struct { unsigned char key0, nkeys, anchor, aset1;",
        "    unsigned short blob_woff, blob_words; } mdspr_scene_t;",
        "/* anchor 0xFF = dynamic between the scene's two sets",
        " * (aset1 = the alternate; key0's set = the initial) */",
        "static const mdspr_key_t mdspr_keys[MDSPR_NKEYS] = {",
    ]
    for k, w, h, subs in all_baked:
        lines.append(
            f"  {{ 0x{k['addr']:04X}, 0x{k['d2lo9']:03X}, {k['bank']}, "
            f"{k['height']}, 0x{k['set']:02X}, {w}, {h}, {len(subs)}, "
            f"{len(subs_flat)} }},  /* {k['key']} x{k['count']} */")
        subs_flat.extend(subs)
    lines.append("};")
    lines.append(f"static const mdspr_sub_t mdspr_subs[{len(subs_flat)}] = {{")
    for dx, dy, size, tile in subs_flat:
        lines.append(f"  {{ {dx}, {dy}, 0x{size:02X}, 0, {tile} }},")
    lines.append("};")
    lines.append("static const mdspr_scene_t "
                 f"mdspr_scenes[MDSPR_NSCENES] = {{")
    for name, key0, nk, woff, words, anchor, sets in scenes_out:
        a = 0xFF if anchor is None else anchor
        aset1 = 0   # vestigial (N-ary leader election replaced the pair)
        lines.append(f"  {{ {key0}, {nk}, 0x{a:02X}, 0x{aset1:02X}, "
                     f"{woff}, {words} }},  /* {name} */")
    lines.append("};")
    OUT_H.write_text("\n".join(lines) + "\n")

    md = ["/* GENERATED by tools/bake_mdspr.py -- do not edit. */",
          f"#define MDSPR_BLOB_WORDS {scenes_out[0][4]}  "
          "/* scene 0 (boot upload) */",
          f"#define MDSPR_VRAM_BASE 0x{VRAM_BASE:04X}",
          "/* cart placement is FIXED by sh_src/mars.ld (.mdsprart) */",
          "#define MDSPR_CART_BANK 2",
          "#define MDSPR_CART_WINOFF 0xF9100",
          f"#define MDSPR_NSCENES {len(scenes_out)}",
          "/* per-scene blob word-offset,word-count (68K runtime",
          " * upload at scene cuts, chunked in the vint) */",
          "static const unsigned short mdspr_scene_blob"
          f"[{len(scenes_out)}][2] = {{"]
    for name, key0, nk, woff, words, anchor, sets in scenes_out:
        md.append(f"  {{ {woff}, {words} }},  /* {name} */")
    md.append("};")
    OUT_MD_H.write_text("\n".join(md) + "\n")
    print(f"wrote {OUT_BIN.name}, {OUT_H.name}, {OUT_MD_H.name}")

if __name__ == '__main__':
    main()
