#!/usr/bin/env python3
"""Fault-injection checks for the native sprite/foreground pen audit."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from sprite_scene_audit import compare_scene


class SpriteScene(unittest.TestCase):
    def setUp(self):
        self.expected = np.full((224, 320), -1, dtype=np.int16)
        self.owners = self.expected.copy()
        self.actual = np.zeros((224, 320), dtype=np.uint8)
        self.fg = np.zeros((224, 320), dtype=bool)
        self.text = self.fg.copy()
        self.bankpal = list(range(16))
        self.records = [dict(slot=0, box=[10, 10, 13, 11])]
        self.owners[10, 10:13] = 0
        self.expected[10, 10:13] = [1, 2, 3]
        self.actual[10, 10:13] = [1, 255, 200]
        self.fg[10, 11] = True
        self.text[10, 12] = True

    def run_audit(self):
        return compare_scene(self.expected, self.owners, self.records, self.actual,
                             self.bankpal, self.fg, self.text)

    def test_visible_foreground_and_text_are_distinct(self):
        r = self.run_audit()
        self.assertTrue(r['passed'])
        self.assertEqual([r['totals'][k] for k in ('visible', 'foreground_covered', 'text_covered')], [1, 1, 1])

    def test_missing_pen(self):
        self.actual[10, 10] = 0
        r = self.run_audit()
        self.assertFalse(r['passed'])
        self.assertEqual(r['totals']['missing'], 1)

    def test_bank_pen_zero_is_opaque_and_wrong(self):
        self.actual[10, 10] = 16
        r = self.run_audit()
        self.assertFalse(r['passed'])
        self.assertEqual(r['totals']['wrong_palette_or_pen'], 1)
        self.assertEqual(r['totals']['missing'], 0)

    def test_bank_mapping(self):
        self.actual[10, 10] = 0x21
        self.bankpal[2] = 3
        self.expected[10, 10] = 0x31
        self.assertTrue(self.run_audit()['passed'])

    def test_wrong_through_pen_is_not_silently_normalized(self):
        self.actual[10, 11] = 0
        r = self.run_audit()
        self.assertFalse(r['passed'])
        self.assertEqual(r['totals']['wrong_through_pen'], 1)

    def test_foreground_leak(self):
        self.actual[10, 11] = 2
        r = self.run_audit()
        self.assertFalse(r['passed'])
        self.assertEqual(r['totals']['foreground_leaks'], 1)

    def test_extra_outside_expected_sprite_boxes(self):
        self.actual[100, 100] = 16
        r = self.run_audit()
        self.assertFalse(r['passed'])
        self.assertEqual(r['totals']['extra'], 1)

    def test_excluded_rows_are_reported(self):
        self.owners[209, 10] = 0
        self.expected[209, 10] = 1
        self.assertEqual(self.run_audit()['sprites'][0]['outside_audited_rows'], 1)


if __name__ == '__main__':
    unittest.main()
