#!/usr/bin/env python3
"""Is an INCREMENTAL 68K -> SH-2 port possible?

PORT-CENSUS.md ruled out a partial port: state shared between the two CPUs must
live in the framebuffer hole, which is FM-arbitrated, and the 68K is locked out
of it for the ~106 lines the master holds FM. That argument only bites on SHARED
state. A cluster of routines whose WRAM footprint NO OTHER routine touches could
move to the SH-2 with its data in SDRAM and share nothing.

This looks for such clusters: build the bipartite graph routine <-> WRAM region,
take connected components, and report each component's size. A small component
is an incrementally portable unit. One giant component means the state is fully
entangled and only a full port is possible.

    python3 tools/port_partition.py
"""
import re, sys, collections, importlib.util

spec = importlib.util.spec_from_file_location('vc', 'tools/vint_census.py')
vc = importlib.util.module_from_spec(spec); spec.loader.exec_module(vc)

WRAMOP = re.compile(r'0x(ff[0-9a-f]{4,6})\b')
TERM = vc.TERM


def region_of(a):
    for lo, hi, name in vc.REGIONS:
        if lo <= a < hi:
            return (lo, hi, name)
    return None


def main():
    ins, order = vc.load()
    adj, ind = vc.edges(ins)
    tbl = vc.pointer_targets(ins, order)
    reached = vc.walk(vc.ENTRIES, adj, ins, order, tbl)

    # every call target that is reached = a routine entry
    entries = set(vc.ENTRIES) | {t for a, ts in adj.items() if a in reached
                                 for t in ts if t in reached}
    entries |= (tbl & reached)
    idx = {a: i for i, a in enumerate(order)}

    r2reg, reg2r = {}, collections.defaultdict(set)
    sizes = {}
    for e in entries:
        if e not in idx:
            continue
        i, n, regs = idx[e], 0, set()
        while i < len(order):
            a = order[i]; n += 1
            for m in WRAMOP.finditer(ins[a][1]):
                v = int(m.group(1), 16)
                if v < 0xFF0000:
                    v |= 0xFF0000
                r = region_of(v & 0xFFFFFF)
                if r:
                    regs.add(r)
            if TERM.match(ins[a][1]):
                break
            i += 1
        sizes[e] = n
        r2reg[e] = regs
        for r in regs:
            reg2r[r].add(e)

    # union-find over routines joined by a shared region
    parent = {e: e for e in r2reg}
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    for r, rs in reg2r.items():
        rs = list(rs)
        for x in rs[1:]:
            union(rs[0], x)

    comp = collections.defaultdict(list)
    for e in r2reg:
        comp[find(e)].append(e)

    touching = [e for e in r2reg if r2reg[e]]
    isolated = [e for e in r2reg if not r2reg[e]]
    print(f'reached routine entries          : {len(r2reg)}')
    print(f'  touch no indexed WRAM region   : {len(isolated)}  '
          f'({sum(sizes[e] for e in isolated)} instructions)')
    print(f'  touch at least one region      : {len(touching)}\n')
    print(f'connected components among the {len(touching)} state-touching routines:')
    groups = sorted(( [e for e in v if r2reg[e]] for v in comp.values() ),
                    key=len, reverse=True)
    groups = [g for g in groups if g]
    for g in groups[:8]:
        regs = set().union(*(r2reg[e] for e in g))
        instr = sum(sizes[e] for e in g)
        print(f'  {len(g):>4} routines, {instr:>6} instructions, '
              f'{len(regs)} region(s): ' +
              ', '.join(sorted(n.split(",")[0] for _, _, n in regs))[:88])
    if len(groups) > 8:
        print(f'  ... and {len(groups)-8} more')
    print()
    big = groups[0] if groups else []
    print(f'VERDICT: largest component holds {len(big)} of {len(touching)} '
          f'state-touching routines '
          f'({100*len(big)/max(1,len(touching)):.0f}%)')
    if len(groups) > 1:
        print(f'         {len(groups)-1} separate component(s) exist -- '
              f'candidates for an incremental port')


if __name__ == '__main__':
    main()
