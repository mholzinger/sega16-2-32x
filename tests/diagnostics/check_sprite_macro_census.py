#!/usr/bin/env python3
"""Focused host checks for the reusable macro-sprite census."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import sprite_macro_census as macro  # noqa: E402


KEY = 0x031401D0


def scaled_pose(rows, row_words):
    """Make the complete decoded/raw ZB1 contract used by production."""
    encoded = []
    for row in rows:
        padded = row + [0] * ((-len(row)) & 3)
        values = tuple(int.from_bytes(bytes(padded[at:at + 4]), 'big')
                       for at in range(0, len(padded), 4))
        encoded.append((0, values, len(row)))
    return rows, row_words, encoded, max(map(len, rows), default=0)


def record(key=KEY, zoom=1, x=40, y=20, bottom=24, palette=2):
    bank = (key >> 24) & 7
    pitch = (key >> 16) & 255
    address = key & 0xffff
    flip = (key >> 28) & 1
    return struct.pack(">8H", y | (bottom << 8), x + 184,
                       pitch | (flip << 8), address,
                       (bank << 8) | palette, zoom, 0, 0)


def table(*records):
    end = struct.pack(">8H", 0, 0, 0x8000, 0, 0, 0, 0, 0)
    return (b"".join(records) + end).ljust(64 * 16, b"\0")


class MacroCensusTest(unittest.TestCase):
    def test_sprite_captures_accepts_ares_watch_csv(self):
        one = table(record(x=10))
        two = table(record(x=20))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "watch.csv"
            path.write_text("frame,gameframe,sdram:0x28400:0x400\n" +
                            f"7,3,{one.hex()}\n8,4,{two.hex()}\n")
            captures = list(macro.sprite_captures(path))
        self.assertEqual(captures, [(7, one), (8, two)])

    def test_target_runs_never_cross_an_intervening_record(self):
        poses = {(KEY, 1): scaled_pose([[1, 2, 3, 4]], [1])}
        blob = table(record(x=10), record(x=20),
                     record(key=0x031501D0, x=30), record(x=40))
        self.assertEqual([len(run) for run in macro.live_runs(blob, KEY, poses)],
                         [2, 1])

    def test_missing_pose_is_a_barrier(self):
        poses = {(KEY, 1): scaled_pose([[1, 2, 3, 4]], [1])}
        blob = table(record(zoom=1), record(zoom=2), record(zoom=1))
        self.assertEqual([len(run) for run in macro.live_runs(blob, KEY, poses)],
                         [1, 1])

    def test_overlapping_records_reduce_stores(self):
        poses = {(KEY, 1): scaled_pose(
            [[1, 2, 3, 4], [5, 6, 7, 8]], [1, 2])}
        records = [(40, 20, 22, 1, 2), (40, 20, 22, 1, 2)]
        result = macro.compose(records, KEY, poses)
        self.assertEqual(result["layers"], 1)
        self.assertGreater(result["scalar_stores"], result["macro_stores"])
        self.assertEqual(result["saved_stores"],
                         result["scalar_stores"] - result["macro_stores"])

    def test_rowpix_accounting_is_not_destination_store_pricing(self):
        row = [1, 0, 0, 2]
        self.assertEqual(len(macro.emitted_words(row, 1)), 2)
        self.assertEqual(macro.runtime_rowpix_words(row, 1, 1), 1)
        # The clipped helper's nonzero phase charges destination stores.
        clipped = [1, 0, 2, 3, 4, 5, 6, 7]
        self.assertEqual(macro.runtime_rowpix_words(clipped, 317, 1),
                         len(macro.emitted_words(clipped[:3], 317)))
        # Sparse row is short, but the complete pose still selects the clipped
        # shifted helper and emits its carry word.
        sparse = [0] * 20 + [0, 7, 6, 7, 0, 0]
        self.assertEqual(macro.runtime_rowpix_words(sparse, 277, 858, 48), 2)
        # ZB1's header width, not the longest decoded row, selects the clipped
        # helper.  The stored interval then charges its shifted carry word.
        encoded = (7, (0x07060700,), 34)
        self.assertEqual(macro.runtime_encoded_rowpix_words(
            encoded, 258, 462, 62), 1)
        self.assertEqual(macro.runtime_encoded_rowpix_words(
            encoded, 258, 462, 64), 2)

    def test_owner_layers_preserve_record_boundaries_and_price_container(self):
        poses = {(KEY, 1): scaled_pose(
            [[1, 2, 3, 4, 5, 6, 7, 8],
             [8, 7, 6, 5, 4, 3, 2, 1]], [2, 2])}
        records = [(40, 20, 22, 1, 2), (44, 20, 22, 1, 3)]
        result = macro.compose_owner(records, KEY, poses)
        self.assertEqual(result["owner_visible_records"], 2)
        self.assertEqual(result["owner_descriptor_bytes"], 16 + 24 * 2)
        self.assertEqual(result["owner_saved_stores"],
                         8 - result["owner_macro_stores"])
        rows = [{**result, "frequency": 3}]
        exact = 32 + 12 + result["owner_bytes"]
        below, fit = macro.owner_budget_rows(rows, [exact - 1, exact])
        self.assertEqual((below["patterns"], below["used"]), (0, 32))
        self.assertEqual((fit["patterns"], fit["used"]), (1, exact))

    def test_owner_packets_preserve_interleaved_priority_punch_order(self):
        poses = {(KEY, 1): scaled_pose(
            [[1, 2, 3, 4, 5, 6, 7, 8],
             [8, 7, 6, 5, 4, 3, 2, 1],
             [1, 0, 2, 0, 3, 0, 4, 0]], [2, 2, 2])}
        records = [(20, 10, 13, 1, 2), (24, 10, 13, 1, 3),
                   (22, 11, 14, 1, 4)]
        blob, _metrics = macro.build_owner_pattern(records, KEY, poses)
        width, height = 64, 32
        initial = [[0xEE] * width for _ in range(height)]

        def punch(screen, record):
            x, y, bottom, zoom, _palette = record
            right = x + max(map(len, poses[KEY, zoom][0]))
            for yy in range(y, bottom):
                for xx in range(x, right):
                    if 0 <= xx < width and 0 <= yy < height and (xx * 3 + yy) % 7 < 2:
                        screen[yy][xx] = 0

        def draw_row(screen, y, x, row, palette):
            for word, pens in macro.emitted_words(row, x).items():
                for byte, pen in enumerate(pens):
                    if pen:
                        screen[y][word * 4 + byte] = (palette << 4) | pen

        scalar = [row[:] for row in initial]
        for x, y, bottom, zoom, palette in records:
            for dy, row in enumerate(poses[KEY, zoom][0][:bottom - y]):
                draw_row(scalar, y + dy, x, row, palette)
            punch(scalar, (x, y, bottom, zoom, palette))

        owner = [row[:] for row in initial]
        for index, record in enumerate(records):
            descriptor = struct.unpack_from('>hhHBBhhIIHH', blob, 16 + index * 24)
            (_x, _y, _zoom, _source_h, palette, draw_x, draw_y,
             at, size, _pixels, _reserved) = descriptor
            if size:
                rows, _words, end = macro.s16art.unpack_frame(blob, at)
                self.assertEqual(end, at + size)
                x, y = record[0] + draw_x, record[1] + draw_y
                for dy, row in enumerate(rows):
                    draw_row(owner, y + dy, x, row, palette)
            punch(owner, record)
        self.assertEqual(owner, scalar)

    def test_opacity_runs_are_maximal_and_record_clipped(self):
        rows = [[1, 2, 3, 4, 1, 0, 2, 3, 5, 6, 7, 8],
                [0, 1, 2, 3, 0, 0, 0, 1],
                [1, 2, 3, 4]]
        stats = macro.opacity_runs(rows)
        self.assertEqual(stats["groups"], 6)
        self.assertEqual(stats["nonempty_groups"], 6)
        self.assertEqual(stats["rows_fully_opaque"], 1)
        self.assertEqual(stats["longest_opaque_groups"], 2)
        self.assertEqual(stats["opaque_groups"], 3)
        self.assertEqual(stats["opaque_runs"], 3)
        self.assertEqual(stats["mixed_runs"], 2)
        self.assertEqual(stats["rows_single_opaque_run"], 1)
        poses = {(KEY, 1): (rows, [3, 2, 1])}
        clipped = macro.record_opacity([(0, 20, 22, 1, 0)], KEY, poses)
        self.assertEqual(clipped["rows"], 2)
        self.assertEqual(clipped["groups"], 5)


if __name__ == "__main__":
    unittest.main()
