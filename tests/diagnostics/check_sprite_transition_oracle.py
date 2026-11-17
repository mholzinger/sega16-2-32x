#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from sprite_transition_oracle import compose, patched_frame

TARGET = 0x1234


def record(slot, key, words, x, pens, palette=0x10):
    opaque = [index for index, pen in enumerate(pens) if pen]
    span = None if not opaque else ((x + opaque[0]) & ~3,
                                    (x + len(pens) + 3) & ~3)
    return dict(slot=slot, key=key, words=tuple(words), x=x, y=0,
                palette=palette, rows=[pens], spans=[span],
                box=(x, x + len(pens), 0, 1))


class TransitionOracleTests(unittest.TestCase):
    def test_unchanged_lower_record_is_not_redrawn_over_carried_target(self):
        rows = [record(0, 1, (1,), 0, [2, 2, 2, 2]),
                record(1, TARGET, (2,), 0, [3, 3, 3, 3], 0x20)]
        actual, _cleared, _collided, changed, restored = patched_frame(
            rows, rows, TARGET)
        self.assertEqual(actual, compose(rows))
        self.assertEqual((changed, restored), (0, 0))

    def test_vacated_target_pixel_reports_nonzero_lower_restore(self):
        old = [record(0, 1, (1,), 0, [2, 2, 2, 2]),
               record(1, TARGET, (2,), 0, [3, 3, 3, 3], 0x20)]
        new = [record(0, 1, (1,), 0, [2, 2, 2, 2]),
               record(1, TARGET, (3,), 0, [3, 0, 3, 3], 0x20)]
        actual, _cleared, _collided, changed, restored = patched_frame(
            old, new, TARGET)
        self.assertEqual(actual, compose(new))
        self.assertEqual((changed, restored), (1, 1))

    def test_later_record_preserves_painter_order(self):
        old = [record(0, TARGET, (1,), 0, [3, 3, 3, 3], 0x20),
               record(1, 2, (2,), 2, [4, 4], 0x30)]
        new = [record(0, TARGET, (3,), 0, [3, 0, 3, 3], 0x20),
               record(1, 2, (4,), 1, [4, 4], 0x30)]
        actual, _cleared, _collided, _changed, _restored = patched_frame(
            old, new, TARGET)
        self.assertEqual(actual, compose(new))


if __name__ == "__main__":
    unittest.main(verbosity=2)
