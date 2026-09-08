#!/usr/bin/env python3
"""PALSTATIC bake — per-scene PAL_SH images + detect probes (PALSTATIC.md).

    python3 tools/palscene_bake.py          # regen sh_src/pal_scenes.h
    python3 tools/palscene_bake.py --stats  # cluster/probe report only

Consumes discover/palscenes/*.palsh — raw 2048-word big-endian PAL_SH
dumps harvested from headless-ares runs (and Mike's savestates). Three
files are the NAMED scene anchors (normal / boss_smoke / transform);
every other dump is a stability witness that must cluster with one of
them, or the bake fails loudly (a fourth scene means the anchor list
is stale, not that the clusterer should invent one).

Emits sh_src/pal_scenes.h:
  pscene_pal[PSCENE_N][2048]  full PAL_SH image per scene (cart .text)
  pscene_probe[PSCENE_N][16]  8 x (word-index, value) detect probes —
                              words STABLE inside the scene's witness
                              cluster, UNIQUE against all other
                              scenes' images at that index, and
                              SPREAD >=64 indices apart (v1.1: the
                              v1 pull — 4 consecutive-index probes
                              sat in ONE palette line, which fades
                              as a unit; a desaturation transit
                              matched all 4 at once. Spread probes
                              live in different blocks the game
                              rewrites at different vints).

The runtime (PALSTATIC=1, m_main.c) evaluates the probes after each
palette-delta apply; on a scene change it copies the whole image into
PAL_SH and bumps every PAL_SETGEN — apply_cram's memo then repaints
all consumers in the SAME window, collapsing the scene-cut palette
trickle (10-20 smeared vints) to detect latency (~2-4).
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'discover' / 'palscenes'
OUT = ROOT / 'sh_src' / 'pal_scenes.h'

# 2026-09-02, tried and WITHDRAWN: foreign anchors for the level-2 head
# scene and the crystal-ball screen (from Mike's states). The head scene
# has ZERO stable discriminators against normal — level 2 reuses level
# 1's tile/text palette, so 'normal' detecting there is correct. The
# ball's palette already fails normal's probes. And a foreign anchor
# that thins the probe set below 8 pairs leaves zero-filled (0,0) pairs
# that only pass while palette word 0 is 0. Law: compare against the
# game palette in WRAM 0xFF9000, never the retired FB copy at 0x85F000.
ANCHORS = ['normal', 'boss_smoke', 'transform']
# v1.1: transform is a clustering anchor but NOT loadable — dense
# span sampling (48 frames, 2026-09-01) measured ZERO stable
# transform-vs-normal discriminators: every word that distinguishes
# the scene IS the flash animation. A static load would fight the
# animation and no probe can hold it. Its span rides the delta
# pipeline; the runtime's unknown-state reset gives the EXIT cut an
# atomic reload instead.
LOADABLE = ['normal', 'boss_smoke']
WORDS = 2048
REGION = 1024              # tile+text half: scene identity lives here;
                            # the sprite half tracks per-moment actors
TOL = 40                    # measured: fades jitter <=34 tile words;
                            # scenes sit >=62 apart (distance matrix,
                            # 2026-08-31)


def load(p):
    d = p.read_bytes()
    assert len(d) == WORDS * 2, f"{p}: not a 2048-word PAL_SH dump"
    return list(struct.unpack(f">{WORDS}H", d))


def main():
    scenes = {a: load(SRC / f"{a}.palsh") for a in ANCHORS}
    # witness clustering: every non-anchor dump must match one anchor
    for p in sorted(SRC.glob('*.palsh')):
        if p.stem in ANCHORS:
            continue
        w = load(p)
        best = min(ANCHORS, key=lambda a: sum(
            1 for i in range(REGION) if w[i] != scenes[a][i]))
        d = sum(1 for i in range(REGION) if w[i] != scenes[best][i])
        if d > TOL:
            sys.exit(f"BAKE FAIL: {p.name} matches no anchor "
                     f"(closest {best}, {d} words off) — a new scene? "
                     f"add an anchor, do not widen TOL blindly")
        print(f"  witness {p.name}: {best} ({d} words off)")

    # probes (v1.1): 8 pairs per scene = 4 stable discriminators
    # against EACH other scene (a full 8-match therefore cannot be any
    # other baked image). Stability = the word is identical across all
    # of the scene's own witnesses (kills the fade-active words v1
    # probed — the aliasing pull). Prefer distinct 32-word blocks: the
    # 68K ships and the game fades per block, so same-block probes
    # move as a unit. NOTE: 4+4 assumes PSCENE_N==3; revisit the split
    # when a 4th scene lands.
    NPROBE = 8
    witness = {a: [] for a in ANCHORS}
    for p in sorted(SRC.glob('*.palsh')):
        if p.stem in ANCHORS:
            continue
        w = load(p)
        best = min(ANCHORS, key=lambda a: sum(
            1 for i in range(REGION) if w[i] != scenes[a][i]))
        witness[best].append(w)
    per = NPROBE // (len(ANCHORS) - 1)
    # glow words are LIVE ANIMATION in every scene and level (census
    # 2026-09-01, tools/glow_bake.py) — a probe on one matches only
    # at its capture's animation phase, so the confirmed detect can
    # never hold 3 landings on it (the boss_smoke single-witness trap:
    # per-scene stability cannot catch a word its one witness froze).
    GLOWWORDS = ({0x36} | set(range(0x99, 0xA0))
                 | set(range(0xA1, 0xA6)) | set(range(0xA9, 0xAE)))
    probes = {}
    for a in LOADABLE:
        # 0x0000/0xFFFF are excluded as VALUES too: they are the
        # attractors every fade and flash passes through — a probe
        # whose expected value is white matches mid-transform-flash
        # (measured: probes 0x3/0x30 = 0xFFFF reset the no-match
        # counter every flash pulse and the exit reload never fired)
        stable = [i for i in range(REGION)
                  if scenes[a][i] not in (0x0000, 0xFFFF)
                  and i not in GLOWWORDS
                  and all(w[i] == scenes[a][i] for w in witness[a])]
        got = []
        blocks = set()
        for b in ANCHORS:
            if b == a:
                continue
            disc = [i for i in stable
                    if scenes[a][i] != scenes[b][i]
                    and i not in [g[0] for g in got]]
            take = []
            for i in disc:                       # pass 1: new blocks
                if len(take) == per:
                    break
                if (i >> 5) not in blocks:
                    take.append(i)
                    blocks.add(i >> 5)
            for i in disc:                       # pass 2: fill
                if len(take) == per:
                    break
                if i not in take:
                    take.append(i)
            assert len(take) == per, \
                f"{a} vs {b}: only {len(take)} stable discriminators"
            got += [(i, scenes[a][i]) for i in sorted(take)]
        probes[a] = got
        print(f"  probes {a}: {[(hex(i), hex(v)) for i, v in got]}")

    if '--stats' in sys.argv:
        return
    with open(OUT, 'w') as f:
        f.write("/* GENERATED by tools/palscene_bake.py — do not edit.\n"
                " * Per-scene PAL_SH images + detect probes; PALSTATIC.md.\n"
                f" * Loadable: {', '.join(LOADABLE)} (level-1 corpus;\n"
                f" * anchors {', '.join(ANCHORS)}),\n"
                " * 2026-08-31). Regenerate per round. */\n")
        f.write(f"#define PSCENE_N {len(LOADABLE)}\n")
        f.write("static const uint16_t pscene_probe[PSCENE_N][16]\n"
                "    __attribute__((section(\".palscenes\"))) = {\n")
        for a in LOADABLE:
            row = ', '.join(f"0x{i:04X}, 0x{v:04X}" for i, v in probes[a])
            f.write(f"    {{ {row} }},   /* {a} */\n")
        f.write("};\n")
        f.write("static const uint16_t pscene_pal[PSCENE_N][1024]\n"
                "    __attribute__((section(\".palscenes\"))) = {\n")
        for a in LOADABLE:
            f.write(f"    {{ /* {a} */\n")
            w = scenes[a]
            for i in range(0, REGION, 8):
                f.write("    " + ",".join(f"0x{x:04X}" for x in
                                          w[i:i + 8]) + ",\n")
            f.write("    },\n")
        f.write("};\n")
    print(f"wrote {OUT}")


if __name__ == '__main__':
    main()
