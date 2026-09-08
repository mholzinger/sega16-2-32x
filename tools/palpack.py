#!/usr/bin/env python3
"""LOOP 19 — offline sprite palette packer (analysis stage).

    tools/palpack.py [harvest.txt] [discover/*.csv ...]

WHY OFFLINE. The runtime allocator hands every live sprite colour SET
its own 16-entry pair and, when it cannot, silently shares an owned pair
or leaves the set unmapped (drawing it in pair 15, the shadow ramp).
Measured: 9 sets want a pair, 6 get one, 4 share -- which is most of the
wrong-colour punch list. The game is fixed and fully known, so the
packing can be solved with every scene visible at once instead of
greedily, one cycle at a time, at runtime.

THE KEY INSIGHT, borrowed from the tile side's `mdp_s_used`: S16 art
leaves garbage in arcade-invisible palette entries, so a set's 15 usable
slots are mostly NOT used. Two sets can share one pair with NO per-pixel
remap -- the draw stays `base + pixel` -- as long as they never disagree
on a pen they BOTH actually draw. That is free sharing; colour-level
merging would need a remap in the hottest loop and is priced separately.

INPUTS, both already produced by existing tooling:
  * tools/palharvest.lua  -> per cycle, the live colour sets and the 16
    colours each holds (palettes are written by the game at runtime, so
    they are observed, not read from a ROM table).
  * tools/sprite_discover.lua -> discover/*.csv, one row per unique
    decode job, carrying the raw sprite record w0..w7. The colour set is
    w4 & 0x3F, and bank/addr/pitch/flip/height drive the same offline
    decoder bake_sprites.py uses -- so pen usage per set comes from the
    ROM, with no runtime probe.
"""
import sys, csv, collections
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import importlib.util
spec = importlib.util.spec_from_file_location(
    'bake', Path(__file__).resolve().parent / 'bake_sprites.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


def pen_usage(csv_paths):
    """set id -> bitmask of pen values its art actually draws."""
    rom = bake.Rom(bake.load_rom())     # word-addressed banked view
    used = collections.defaultdict(int)
    jobs = 0
    for p in csv_paths:
        with open(p) as fh:
            for row in csv.DictReader(fh):
                try:
                    w4 = int(row['w4'], 16)
                    bank = int(row['bank'])
                    addr = int(row['addr'], 16)
                    pitch = int(row['pitch'])
                    flip = int(row['flip'])
                    height = int(row['height'])
                except (KeyError, ValueError):
                    continue
                sc = w4 & 0x3F
                if sc == 0x3F or height <= 0:
                    continue          # shadow sets own reserved pair 15
                jobs += 1
                for prow in bake.decode_frame(rom, bank, addr, pitch,
                                              flip, height):
                    for pen in prow:
                        if 1 <= pen <= 14:
                            used[sc] |= 1 << pen
    return used, jobs


def load_cycles(path):
    cycles = []
    for line in open(path):
        f = line.split()
        if not f or f[0] != 'C':
            continue
        sets = {}
        for tok in f[4:]:
            g = tok.split(',')
            sets[int(g[0], 16)] = [int(x, 16) for x in g[1:]]
        if sets:
            cycles.append(sets)
    return cycles


def compatible(pa, pb, ma, mb):
    """Can these two sets share one pair with no remap? Only pens BOTH
    draw have to agree; a pen only one of them uses is free real estate."""
    both = ma & mb
    for v in range(1, 15):
        if (both >> v) & 1 and pa[v] != pb[v]:
            return False
    return True


def pack(sets, used):
    """Greedy clique-cover over the compatibility graph. Sets whose art
    was never seen in discovery get their own pair (unknown usage is
    assumed full)."""
    groups = []
    for k in sorted(sets, key=lambda s: -bin(used.get(s, 0xFFFE)).count('1')):
        mk = used.get(k, 0xFFFE)
        for g in groups:
            if all(compatible(sets[k], sets[o], mk, used.get(o, 0xFFFE))
                   for o in g):
                g.append(k)
                break
        else:
            groups.append([k])
    return groups


def main():
    args = sys.argv[1:]
    harvest = args[0] if args else '/tmp/palharvest2.txt'
    csvs = args[1:] or sorted(str(p) for p in Path('discover').glob('*.csv'))
    used, jobs = pen_usage(csvs)
    print(f"pen usage from {jobs} decode jobs in {len(csvs)} csv(s): "
          f"{len(used)} colour sets")
    hist = collections.Counter(bin(m).count('1') for m in used.values())
    print("  pens used per set: " +
          "  ".join(f"{n}:{c}" for n, c in sorted(hist.items())))

    cycles = load_cycles(harvest)
    worst_naive = worst_packed = 0
    unknown = collections.Counter()
    worst_detail = None
    for sets in cycles:
        for k in sets:
            if k not in used:
                unknown[k] += 1
        groups = pack(sets, used)
        if len(sets) > worst_naive:
            worst_naive = len(sets)
        if len(groups) > worst_packed:
            worst_packed = len(groups)
            worst_detail = groups
    print(f"\ncycles: {len(cycles)}")
    print(f"WORST naive (a pair per set): {worst_naive}")
    print(f"WORST used-pen-aware packing: {worst_packed}")
    print(f"pairs the allocator can actually hand out: 6")
    if unknown:
        print(f"\nsets live but never seen in discovery (assumed full "
              f"usage): {sorted(unknown)}")
    if worst_detail:
        print(f"worst cycle grouping: {worst_detail}")


if __name__ == '__main__':
    main()
