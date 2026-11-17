#!/usr/bin/env python3
"""Profile baked sprite packets and screen two-slot cache policies.

The AB1 row format stores every group between the first and last opaque group.
The runtime skips an all-zero group, but still reads and processes it.  This
census reports the maximum work a sparse/multi-run row format could remove.
Counts are weighted by actual sprite-table occurrences in the supplied corpora.

The packet-size report shows how much of that observed workload fits a cache
capacity. ``--two-slot`` evaluates small-slot capacity and height-route pairs
inside a fixed arena. It reports eligibility, not predicted cache hits or speed;
temporal reuse and hardware phase time still require measurement.
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s16art  # noqa: E402

DEFAULT_SMALL_WORDS = 256
PACKET_LIMITS = (128, 256, 384, 512, 640, 768, 1024)
HEIGHT_LIMITS = (16, 24, 32, 40, 48, 56, 64)


def count_rows(rows):
    stored = useful = 0
    for row in rows:
        first, groups = s16art.row_span(row)
        stored += groups
        useful += sum(any(row[x:x + 4])
                      for x in range(first, first + groups * 4, 4))
    return len(rows), stored, useful


def report(label, records, rows, stored, useful):
    holes = stored - useful
    print('%-12s records=%7d rows=%9d groups=%10d useful=%10d holes=%9d (%5.2f%%)'
          % (label, records, rows, stored, useful, holes,
             100.0 * holes / max(1, stored)))


def report_packet_capacities(occurrences, packet_words, records):
    for limit in PACKET_LIMITS:
        served = sum(count for key, count in occurrences.items()
                     if packet_words[key] <= limit)
        print('packet <=%4d words: %7d/%7d occurrences (%5.1f%%), %4d/%4d keys'
              % (limit, served, records, 100.0 * served / max(1, records),
                 sum(size <= limit for size in packet_words.values()), len(packet_words)))


def report_default_height_routes(occurrences, maximum_height, packet_words):
    """Show overflow against the engine's default 256-word small slot."""
    for height_limit in HEIGHT_LIMITS:
        routed = sum(count for key, count in occurrences.items()
                     if maximum_height[key] <= height_limit)
        overflow = sum(count for key, count in occurrences.items()
                       if maximum_height[key] <= height_limit
                       and packet_words[key] > DEFAULT_SMALL_WORDS)
        print('height <=%2d -> small: %7d occurrences, %7d overflow 256 words (%5.1f%%)'
              % (height_limit, routed, overflow,
                 100.0 * overflow / max(1, routed)))


def report_two_slot_policies(args, occurrences, maximum_height, packet_words,
                             records):
    small_sizes = args.candidate_small or (128, 192, 256, 320, 384, 512)
    height_limits = args.candidate_height or HEIGHT_LIMITS
    policies = []
    for small_words in small_sizes:
        large_words = args.arena_words - small_words
        if small_words < 2 or large_words < 2:
            raise SystemExit('two-slot candidates must leave at least two words per slot')
        for height_limit in height_limits:
            small_occurrences = large_occurrences = 0
            for key, count in occurrences.items():
                if (maximum_height[key] <= height_limit
                        and packet_words[key] <= small_words):
                    small_occurrences += count
                elif packet_words[key] <= large_words:
                    large_occurrences += count
            policies.append((small_occurrences + large_occurrences,
                             small_occurrences, large_occurrences,
                             small_words, large_words, height_limit))
    print('two-slot capacity candidates '
          '(eligibility only; benchmark temporal reuse):')
    print(' covered    small    large  smallW largeW height coverage')
    for covered, small, large, small_words, large_words, height_limit in sorted(
            policies, reverse=True)[:12]:
        print('%8d %8d %8d %7d %6d %6d %7.2f%%'
              % (covered, small, large, small_words, large_words, height_limit,
                 100.0 * covered / max(1, records)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', default='sh_src/sprites.bin')
    parser.add_argument('--two-slot', action='store_true',
                        help='rank capacity/height policies for a two-slot prepared cache')
    parser.add_argument('--arena-words', type=int, default=1024)
    parser.add_argument('--candidate-small', type=int, action='append',
                        help='small-slot word capacity to evaluate; repeatable')
    parser.add_argument('--candidate-height', type=int, action='append',
                        help='height routing threshold to evaluate; repeatable')
    parser.add_argument('corpus', nargs='+', help='spr.bin file or directory containing it')
    args = parser.parse_args()
    words = s16art.load_rom(args.rom)
    pose_stats = {}
    occurrences = collections.Counter()
    maximum_height = {}
    totals = [0, 0, 0, 0]
    for name in args.corpus:
        path = os.path.join(name, 'spr.bin') if os.path.isdir(name) else name
        with open(path, 'rb') as corpus_file:
            raw = corpus_file.read()
        if len(raw) % 1024:
            raise SystemExit('%s is not a whole sprite-table corpus' % path)
        records = rows = stored = useful = 0
        for start in range(0, len(raw), 1024):
            for key, height, zoomed in s16art.records(raw[start:start + 1024]):
                if zoomed:  # Prepared packets currently serve unscaled AB1 art only.
                    continue
                identity = (key, height)
                occurrences[key] += 1
                maximum_height[key] = max(maximum_height.get(key, 0), height)
                if identity not in pose_stats:
                    decoded, _, _ = s16art.decode(words, *key, height)
                    pose_stats[identity] = count_rows(decoded)
                pose_rows, pose_stored, pose_useful = pose_stats[identity]
                records += 1
                rows += pose_rows
                stored += pose_stored
                useful += pose_useful
        report(os.path.basename(os.path.dirname(path)), records, rows, stored, useful)
        for i, value in enumerate((records, rows, stored, useful)):
            totals[i] += value
    report('TOTAL', *totals)
    records = totals[0]
    # A prepared packet is one header word per row plus its stored groups; the
    # tallest occurrence of every key is already counted in pose_stats.
    packet_words = {key: sum(pose_stats[key, height][:2])
                    for key, height in maximum_height.items()}
    report_packet_capacities(occurrences, packet_words, records)
    report_default_height_routes(occurrences, maximum_height, packet_words)
    if args.two_slot:
        report_two_slot_policies(args, occurrences, maximum_height, packet_words,
                                 records)


if __name__ == '__main__':
    main()
