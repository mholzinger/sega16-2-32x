#!/usr/bin/env python3
"""Known source-map cases for larger-block census measurements."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from tile_block_census import tile_at, physical_code, census


class TileBlocks(unittest.TestCase):
    def test_quadrants_and_world_wrap(self):
        tiles = [page for page in range(16) for _ in range(2048)]
        self.assertEqual(tile_at(tiles, 0x3210, 0, 0), 0)
        self.assertEqual(tile_at(tiles, 0x3210, 64, 0), 1)
        self.assertEqual(tile_at(tiles, 0x3210, 0, 32), 2)
        self.assertEqual(tile_at(tiles, 0x3210, 64, 32), 3)
        self.assertEqual(tile_at(tiles, 0x3210, 192, 96), 3)

    def test_pages_outside_port_shadow_are_not_read(self):
        self.assertEqual(tile_at([], 0xffff, 0, 0), 0)

    def test_mcu_bank_and_attribute_bits(self):
        self.assertEqual(physical_code(0x8123, 2), 0x123)
        self.assertEqual(physical_code(0x9123, 2), 0x2123)
        self.assertEqual(physical_code(0x9123, 7), 0x3123)

    def test_visible_block_edges_and_empty_mask_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp) / 'ev_1'
            tiles = bytearray(65536)
            struct.pack_into('>H', tiles, 0, 0x8001)
            text = bytearray(4096)
            struct.pack_into('>H', text, 0x74c * 2, 192)
            struct.pack_into('>H', text, 0x74d * 2, 192)
            Path(str(prefix) + '_tilemap.bin').write_bytes(tiles)
            Path(str(prefix) + '_text.bin').write_bytes(text)
            Path(str(prefix) + '_wram.bin').write_bytes(bytes(16384))
            keys = {s: dict(art=set(), mask=set()) for s in (2, 4, 8)}
            art = bytearray(1048576)
            art[64] = 8  # high bits are not an opaque source pen
            first = census(prefix, art, keys)['planes'][0]
            self.assertEqual(first['visible_cells'], 1120)
            self.assertEqual(first['empty_priority_cells'], 1)
            self.assertEqual(first['empty_priority_rows'], 8)
            self.assertEqual([first['blocks'][s]['visible_blocks'] for s in ('16', '32', '64')],
                             [280, 70, 20])
            self.assertEqual(first['blocks']['32']['visible_priority_mask_and_keys_bytes'], 160)
            art[64] = 7
            second = census(prefix, art, keys)['planes'][0]
            self.assertEqual(second['empty_priority_cells'], 0)
            self.assertEqual(second['empty_priority_rows'], 7)


if __name__ == '__main__':
    unittest.main()
