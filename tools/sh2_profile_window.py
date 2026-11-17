#!/usr/bin/env python3
"""Subtract deterministic prefix PC profiles to inspect a gameplay window.

This is ares instruction frequency, NOT FPGA time or optimization payoff.
Linker start/end aliases are ignored when a real function shares their address.
Negative counts reject nonmatching ROM/run prefixes instead of hiding them.
"""
import argparse
import bisect
import collections
import csv
import hashlib
import json
from pathlib import Path


def load(path):
    return {(r['cpu'], int(r['pc'], 0)): (int(r['instructions']), int(r['cycles']))
            for r in csv.DictReader(path.open())}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'lst', 'out'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--frames', type=int, required=True, help='number of frames in the subtracted window')
    args = ap.parse_args()
    if args.frames <= 0:
        ap.error('--frames must be positive')
    aliases = collections.defaultdict(list)
    for line in args.lst.read_text().splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in ('T', 't', 'W', 'w'):
            aliases[int(fields[0], 16)].append(fields[2])
    symbols = []
    for addr, names in aliases.items():
        real = [n for n in names if n != '_start' and not n.startswith('__')]
        symbols.append((addr, sorted(real or [n for n in names if n != '_start'] or names)[0]))
    symbols.sort()
    addresses = [s[0] for s in symbols]
    before, after = load(args.before), load(args.after)
    stats = collections.defaultdict(lambda: [0, 0])
    for key in before.keys() | after.keys():
        old, new = before.get(key, (0, 0)), after.get(key, (0, 0))
        ins, cycles = new[0] - old[0], new[1] - old[1]
        if ins < 0 or cycles < 0:
            raise ValueError(f'nonmatching prefix counters at {key}')
        cpu, pc = key
        if not (0x02000000 <= pc < 0x02400000 or 0x06000000 <= pc < 0x06040000):
            continue
        i = bisect.bisect_right(addresses, pc) - 1
        name = symbols[i][1] if i >= 0 else 'unknown'
        stats[cpu, name][0] += ins
        stats[cpu, name][1] += cycles
    result = dict(scope='instruction frequency only; spin loops and timing instrumentation included; not hardware timing',
                  frames=args.frames, inputs={}, cpus={})
    for name in ('before', 'after', 'lst'):
        path = getattr(args, name)
        result['inputs'][name] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    for cpu in ('master', 'slave'):
        rows = sorted([(name, *v) for (c, name), v in stats.items() if c == cpu and v[0]], key=lambda r: -r[1])
        total = sum(r[1] for r in rows)
        result['cpus'][cpu] = [dict(function=n, instructions=i, model_cycles=c,
                                    instructions_per_vint=i / args.frames, share=i / total)
                              for n, i, c in rows]
        print(cpu)
        for row in result['cpus'][cpu][:20]:
            print('%7.2f%% %8.0f ins/vint %s' % (100 * row['share'], row['instructions_per_vint'], row['function']))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
