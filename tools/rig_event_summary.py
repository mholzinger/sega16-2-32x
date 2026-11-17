#!/usr/bin/env python3
"""Summarize a completed five-page event-footer capture bundle.

This consumes the generic footer format, without game handlers, ROM names or
assumed native event durations. FRT-derived fields remain raw display words:
uncertain modular sums cannot establish absolute CPU duration or FPS. Two
VInt edges give a lower-bound overrun count; zero/one edge does not prove a
deadline was met.
"""
import argparse
import json
from pathlib import Path
from rig_event import read


def split(value):
    return [value >> 16, value & 65535]


def summarize(directory, signature=None):
    directory = Path(directory)
    pages, shots = {}, []
    # Rig cuts contain montage.png. Only numbered source captures enter the
    # measurement; bad geometry/colors in any selected source still reject.
    for path in sorted(directory.glob("[0-9]*.png")):
        decoded = read(path)
        page, words = decoded["page"], decoded["raw32_values"]
        if page in pages and pages[page] != words:
            raise ValueError(f"{directory}: retained page{page} changed in {path.name}")
        pages[page] = words
        shots.append(dict(path=str(path), **decoded))
    if set(pages) != set(range(5)):
        raise ValueError(f"{directory}: require all five pages; found{sorted(pages)}")
    signatures = [split(pages[4][i])[1] for i in (1, 3)]
    if signatures[0] != signatures[1]:
        raise ValueError("phase build signatures disagree")
    actual_signature = signatures[0]
    # Recognized worker and working-set/boot families assign bit7 to LIGHT.
    # Other producers retain arbitrary signatures without inheriting meaning.
    prefix=actual_signature & 0xFF00
    known_worker=0xE000<=prefix<=0xE300 or 0xE800<=prefix<=0xEF00
    known_ws=prefix in (0xD400,0xD800,0xDC00)
    known_r60=known_worker or known_ws
    working_set=bool(prefix&0x0800) if known_ws else None
    boot_banks=bool(prefix&0x0400) if known_ws else None
    frt_available = not bool(actual_signature & 0x80) if known_r60 else None
    store_stats_available = True if known_ws else not bool(prefix&0x0100) if known_worker else None
    producer_max_quanta = (8 if prefix&0x0200 else 1) if known_worker and actual_signature&0xC else None
    # Historical worker signatures predate the guarded-future policy bit and
    # do not encode lead distance (for example ECA5 actually used lead8).
    producer_lookahead_rows = (16 if prefix<0xE400 else 32 if prefix&0x0400 else 8) if known_worker and actual_signature&0xC and actual_signature&0x40 else None
    if signature is not None and actual_signature != signature:
        raise ValueError(f"wrong build signature:{actual_signature:#06x}, expected{signature:#06x}")
    events = []
    for index in range(2):
        a, b = 2 * index, 2 * index + 1
        ticks, vints = split(pages[0][a])
        acks, samples = split(pages[0][b])
        ge2, repeated_samples = split(pages[1][a])
        master_flags, slave_flags = split(pages[4][a])
        start_tick, _ = split(pages[4][b])
        if not ticks or not vints or not samples or not acks:
            raise ValueError(f"phase{index+1}: zero event duration, acknowledgments or rendered samples")
        if 65535 in (ticks, vints, acks, samples, ge2, repeated_samples):
            raise ValueError(f"phase{index+1}: footer count may be clamped")
        if samples != repeated_samples or ge2 > samples:
            raise ValueError(f"phase{index+1}: inconsistent sample/edge counts")
        if not (master_flags & 2 and slave_flags & 2) or (master_flags | slave_flags) & 1:
            raise ValueError(f"phase{index+1}: event records are not completed")
        if (master_flags | slave_flags) & 32:
            raise ValueError(f"phase{index+1}: saturated counters")
        events.append(dict(phase=index+1, native_ticks=ticks, vints=vints,
            applied_acks=acks, rendered_samples=samples,
            applied_acks_per_native_tick=acks/ticks,
            rendered_samples_per_native_tick=samples/ticks,
            passes_crossing_at_least_two_vint_edges=ge2,
            lower_bound_overrun_fraction=ge2/samples,
            master_flags=master_flags, slave_flags=slave_flags,
            head_partial=bool(slave_flags & 4), tail_partial=bool(slave_flags & 8),
            tag_gap=bool(slave_flags & 64),
            source_uncertain=bool((master_flags | slave_flags) & 16),
            frt_metrics_available=frt_available,
            working_set=working_set, boot_banks=boot_banks,
            store_stats_available=store_stats_available,
            producer_max_quanta=producer_max_quanta,
            producer_lookahead_rows=producer_lookahead_rows,
            start_native_tick=start_tick,
            raw_frt_display_pairs=dict(plot_punch=split(pages[1][b]),
                master_tick_planes=split(pages[2][a]),
                master_idle_snapshot=split(pages[2][b]),
                clear_wait=split(pages[3][a]),plot_punch_max=split(pages[3][b]))))
    return dict(directory=str(directory), signature=actual_signature,
                frt_metrics_available=frt_available, events=events,
                working_set=working_set, boot_banks=boot_banks,
                store_stats_available=store_stats_available,
                producer_max_quanta=producer_max_quanta,
                producer_lookahead_rows=producer_lookahead_rows,
                pages={str(k):v for k,v in pages.items()}, screenshots=shots,
                caveat=__doc__.strip() + (' LIGHT marks all FRT metrics unavailable; displayed zero pairs are placeholders, not measured zero duration.' if frt_available is False else '') + (' An odd R60 prefix marks destination-store totals unavailable, not measured zero store totals. Worker work-identity counters remain available.' if store_stats_available is False else ''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--signature", type=lambda value:int(value, 0))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    reports = [summarize(directory, args.signature) for directory in args.directories]
    result = json.dumps(reports, indent=2) + "\n"
    if args.out:
        args.out.write_text(result)
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
