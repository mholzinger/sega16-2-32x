#!/usr/bin/env python3
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from sprite_transition_census import budget_rows, patch, run_patch, surface


class TransitionCensusTests(unittest.TestCase):
    def test_surface_preserves_painter_order_and_physical_words(self):
        key = 0x1234
        poses = {
            (key, 1): ([[1, 2, 0, 4, 5]], [2]),
            (key, 2): ([[6, 0, 7, 8]], [1]),
        }
        records = [(0, 3, 4, 1, 0x10), (1, 3, 4, 2, 0x20)]
        words, scalar = surface(records, key, poses)
        self.assertEqual(scalar, 4)
        self.assertEqual(words, {
            (3, 0): bytes((0x11, 0x26, 0x00, 0x27)),
            (3, 1): bytes((0x28, 0x00, 0x00, 0x00)),
        })

    def test_patch_counts_changes_and_background_restores(self):
        old = {(2, 4): bytes((1, 2, 3, 4)), (3, 4): bytes((1, 0, 0, 0))}
        new = {(2, 4): bytes((1, 0, 8, 4)), (4, 4): bytes((9, 0, 0, 0))}
        encoded, restored = patch(old, new)
        self.assertEqual(struct.unpack_from(">HH", encoded), (3, 0))
        self.assertEqual(restored, 2)
        self.assertEqual(len(encoded), 4 + 3 * 8)
        runs = run_patch(old, new)
        self.assertEqual(struct.unpack_from(">HH", runs), (3, 0))
        self.assertEqual(len(runs), 4 + 3 * (4 + 4))

    def test_budget_deduplicates_by_packet_row(self):
        rows = [
            dict(frequency=2, saved_stores=20, bytes=12, run_bytes=8),
            dict(frequency=1, saved_stores=5, bytes=8, run_bytes=8),
        ]
        self.assertEqual(budget_rows(rows, [12])[0]["weighted_saved_stores"], 40)
        self.assertEqual(budget_rows(rows, [8], "run_bytes")[0]["weighted_saved_stores"], 40)


if __name__ == "__main__":
    unittest.main(verbosity=2)
