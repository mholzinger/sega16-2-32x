#!/usr/bin/env python3
"""Known-byte cases for the sprite pixel measurement instrument."""
import struct
import sys
import unittest
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from sprite_pixel_audit import reference, framebuffer, counts
from sprite_penid_check import decode
from PIL import Image


class SpritePixels(unittest.TestCase):
    def fixture(self, records, rom_words):
        table = bytearray(1024)
        for slot, record in enumerate(records):
            struct.pack_into('>8H', table, slot * 16, *record, 0, 0)
        struct.pack_into('>H', table, len(records) * 16 + 4, 0x8000)
        rom = bytearray(0x100000)
        for addr, word in rom_words.items():
            struct.pack_into('>H', rom, addr * 2, word)
        return reference(table, rom)

    def test_pitch_before_first_row_and_transparent_end_word(self):
        p, owner, rows = self.fixture([(0x0200, 184, 1, 0, 2, 0)],
                                     {1: 0x102f, 2: 0x3f4f})
        self.assertEqual(p[:2, :4].tolist(), [[33, -1, 34, -1], [35, -1, 36, -1]])
        self.assertEqual(rows[0]['emitted_pixels'], 4)
        self.assertEqual(int((owner == 0).sum()), 4)

    def test_flip_negative_pitch_bank_wrap_and_left_clip(self):
        # bank2, address0 minus pitch1 wraps toffff; low nibble first.
        p, _, _ = self.fixture([(0x0100, 183, 0x1ff, 0, 0x203, 0)],
                               {0x2ffff: 0xf321})
        self.assertEqual(p[0, :3].tolist(), [50, 51, -1])

    def test_later_opaque_pixel_wins_but_transparent_does_not_erase(self):
        p, owner, _ = self.fixture([(0x0100, 184, 1, 0, 1, 0),
                                   (0x0100, 184, 1, 1, 2, 0)],
                                  {1: 0x123f, 2: 0x040f})
        self.assertEqual(p[0, :4].tolist(), [17, 36, 19, -1])
        self.assertEqual(owner[0, :4].tolist(), [0, 1, 0, -1])

    def test_zoom_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'zoom or shadow'):
            self.fixture([(0x0100, 184, 1, 0, 1, 1)], {})

    def test_physical_bank_line_table_and_dot_shift(self):
        dram = bytearray(0x40000)
        for y in range(224):
            struct.pack_into('>H', dram, 0x20000 + y * 2, 0x100 + y * 160)
        dram[0x20201:0x20204] = b'\x11\x22\x33'
        p = framebuffer(dram, 1, 1)
        self.assertEqual(p[0, :3].tolist(), [17, 34, 51])

    def test_counts_separate_punch_wrong_pen_and_extra(self):
        p, owner, records = self.fixture([(0x0100, 184, 1, 0, 2, 0)], {1: 0x102f})
        actual = np.zeros((224, 320), dtype=np.uint8)
        actual[0, :3] = [255, 0x73, 0x74]
        bankpal = list(range(16))
        bankpal[7] = 2
        row = counts(p, owner, actual, bankpal, records)[0]
        self.assertEqual(row['missing_or_occluded'], 1)
        self.assertEqual(row['punched'], 1)
        self.assertEqual(row['wrong_palette_or_pen'], 1)
        self.assertEqual(row['extra_in_box'], 1)

    def test_bank_bits_preserve_zero_bytes_for_all_pen_groups(self):
        # Packed arithmetic used by BK_COLOR, checked against scalar byte
        # coloring over every four-pen combination and every hardware bank.
        n = np.arange(65536, dtype=np.uint32)
        raw = ((n & 15) | ((n & 240) << 4) |
               ((n & 3840) << 8) | ((n & 61440) << 12))
        # Pen15 is not present in baked rows; test its behavior as nonzero too.
        carry = (raw + np.uint32(0x0f0f0f0f)) & np.uint32(0x10101010)
        for bank in range(16):
            packed = raw | carry * np.uint32(bank)
            scalar = np.zeros_like(raw)
            for shift in (0, 8, 16, 24):
                pen = (raw >> shift) & 15
                byte = np.where(pen != 0, pen | (bank << 4), 0).astype(np.uint32)
                scalar |= byte << shift
            self.assertTrue(np.array_equal(packed, scalar), f'bank {bank}')

    def test_penid_calibration_uses_exact_colors_and_rejects_aliases(self):
        rgb = np.zeros((224, 320, 3), dtype=np.uint8)
        for i in range(256):
            x, y = i % 64 * 5, 208 + i // 64 * 4
            rgb[y:y + 4, x:x + 5] = [i, 255 - i, 17]
        rgb[60, 20] = [0x73, 255 - 0x73, 17]
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'ids.png'
            Image.fromarray(rgb).save(image)
            actual, colors = decode(image)
            self.assertEqual(len(colors), 224)
            self.assertEqual(int(actual[60, 20]), 0x73)
            rgb[208:212, 10:15] = rgb[208, 5]  # pen2 aliases pen1
            Image.fromarray(rgb).save(image)
            with self.assertRaisesRegex(ValueError, 'aliases'):
                decode(image)

    def test_penid_rejects_resized_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'scaled.png'
            Image.new('RGB', (640, 448)).save(image)
            with self.assertRaisesRegex(ValueError, 'native320'):
                decode(image)


if __name__ == '__main__':
    unittest.main()
