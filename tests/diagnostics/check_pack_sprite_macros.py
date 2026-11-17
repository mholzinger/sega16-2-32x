#!/usr/bin/env python3
"""Bounds, selection and corruption checks for the generic MC1 packer."""
import json
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import pack_sprite_macros as packer  # noqa: E402
import sprite_macro_census as macro  # noqa: E402
from bake_art import keyword  # noqa: E402


KEY = 0x031401D0


def scaled_pose(rows, row_words):
    encoded = []
    for row in rows:
        padded = row + [0] * ((-len(row)) & 3)
        values = tuple(int.from_bytes(bytes(padded[at:at + 4]), 'big')
                       for at in range(0, len(padded), 4))
        encoded.append((0, values, len(row)))
    return rows, row_words, encoded, max(map(len, rows), default=0)


class PackSpriteMacrosTest(unittest.TestCase):
    def setUp(self):
        self.poses = {(KEY, 1): scaled_pose(
            [[1, 2, 3, 4, 5, 6, 7, 8],
             [8, 7, 6, 5, 4, 3, 2, 1]], [2, 2])}
        self.patterns = [dict(first_tick=10, frequency=3,
                              records=[(40, 20, 22, 1, 2),
                                       (44, 20, 22, 1, 3)])]

    def test_exact_budget_and_round_trip(self):
        reference, _ = packer.pack_patterns(self.patterns, KEY,
                                            self.poses, 4096)
        exact = len(reference)
        with self.assertRaisesRegex(ValueError, 'admits no'):
            packer.pack_patterns(self.patterns, KEY, self.poses, exact - 1)
        blob, report = packer.pack_patterns(self.patterns, KEY,
                                            self.poses, exact)
        self.assertEqual(len(blob), exact)
        self.assertEqual(packer.verify(blob), 1)
        self.assertEqual(report['patterns'], 1)
        self.assertEqual(report['captured_frames'], 3)

    def test_shared_pool_round_trip_and_deduplicates(self):
        patterns = self.patterns + [dict(first_tick=20, frequency=2,
                                         records=[(48, 20, 22, 1, 2),
                                                  (52, 20, 22, 1, 3)])]
        plain, _ = packer.pack_patterns(patterns, KEY, self.poses, 8192)
        shared, report = packer.pack_patterns_shared(patterns, KEY,
                                                     self.poses, 8192)
        self.assertEqual(packer.verify(shared), 2)
        self.assertEqual(struct.unpack_from('>H', shared, 4)[0], 2)
        self.assertLess(len(shared), len(plain))
        self.assertEqual(report['patterns'], 2)
        self.assertLess(report['shared_assets'], 8)

        corrupt = bytearray(shared)
        _fingerprint, pattern_at, _size = struct.unpack_from('>III', corrupt, 32)
        struct.pack_into('>I', corrupt, pattern_at + 16 + 12, len(corrupt) + 4)
        with self.assertRaises(ValueError):
            packer.verify(corrupt)

    def test_corrupt_packet_and_directory_are_rejected(self):
        blob, _ = packer.pack_patterns(self.patterns, KEY, self.poses, 4096)
        corrupt = bytearray(blob)
        _fingerprint, pattern_at, _size = struct.unpack_from('>III', corrupt, 32)
        _fields = struct.unpack_from('>hhHBBhhIIHH', corrupt, pattern_at + 16)
        packet_at, packet_bytes = _fields[7:9]
        self.assertTrue(packet_bytes)
        struct.pack_into('>I', corrupt, pattern_at + 16 + 16,
                         packet_bytes + len(corrupt))
        with self.assertRaises(ValueError):
            packer.verify(corrupt)

    def test_corrupt_scalar_metadata_is_rejected(self):
        blob, _ = packer.pack_patterns(self.patterns, KEY, self.poses, 4096)
        _fingerprint, pattern_at, _size = struct.unpack_from('>III', blob, 32)
        fields = struct.unpack_from('>hhHBBhhIIHH', blob, pattern_at + 16)
        metadata_at = fields[10] * 4
        self.assertGreater(metadata_at, 0)

        corrupt = bytearray(blob)
        struct.pack_into('>H', corrupt, pattern_at + 16 + 22, 0)
        with self.assertRaisesRegex(ValueError, 'metadata'):
            packer.verify(corrupt)

        corrupt = bytearray(blob)
        corrupt[pattern_at + metadata_at + 2] = 0
        with self.assertRaisesRegex(ValueError, 'metadata'):
            packer.verify(corrupt)

        corrupt = bytearray(blob)
        corrupt[pattern_at + metadata_at + 4] = 0
        with self.assertRaisesRegex(ValueError, 'metadata'):
            packer.verify(corrupt)

        corrupt = bytearray(blob)
        struct.pack_into('>I', corrupt, 36, len(corrupt) + 4)
        with self.assertRaises(ValueError):
            packer.verify(corrupt)

    def test_minimum_saving_excludes_marginal_pattern(self):
        with self.assertRaisesRegex(ValueError, 'admits no'):
            packer.pack_patterns(self.patterns, KEY, self.poses, 4096,
                                 min_saved_stores=1000)

    def test_current_serialized_patterns_match_full_scalar_surfaces(self):
        """Replay every selected real pattern, including interleaved punches."""
        artifact = ROOT / 'build/codex-sound-compact/smoke-owner-macros-port-v4.mc1'
        report_path = ROOT / 'build/codex-sound-compact/smoke-owner-macros-port-v4.json'
        workset = ROOT / 'build/codex-sound-compact/workset-macro-word.bin'
        workset_json = ROOT / 'build/codex-sound-compact/workset-macro-word.json'
        sprites = ROOT / 'build/port/sh_src/sprites.bin'
        manifest_path = ROOT / 'games/altbeast/graphics/neff-smoke-unscaled-1.json'
        corpus = ROOT / 'build/codex-sound-compact/port-smoke-corpus/sprite-watch.csv'
        if not all(path.is_file() for path in
                   (artifact, report_path, workset, workset_json, sprites,
                    manifest_path, corpus)):
            self.skipTest('current macro artifact is not present')
        blob = artifact.read_bytes()
        report = json.loads(report_path.read_text())
        wanted = {row['first_tick'] for row in report['selected']}
        captures = {tick: table for tick, table in macro.sprite_captures(corpus)
                    if tick in wanted}
        self.assertEqual(set(captures), wanted)
        page = workset.read_bytes()
        metadata = json.loads(workset_json.read_text())
        poses = macro.zoom_packets(page, metadata)
        # Decode the unscaled pose from its immutable raw-art contract.  Do not
        # resolve it through build/port/artbake_N.bin: every candidate rebuild
        # legitimately rewrites those global cache windows, which made this
        # serialized-artifact test depend on whichever ROM happened to build
        # most recently.
        sprite_words = struct.unpack(f'>{sprites.stat().st_size // 2}H',
                                     sprites.read_bytes())
        manifest = json.loads(manifest_path.read_text())
        for entry in manifest['entries']:
            bank, address, pitch, flip = entry['key']
            key = keyword((bank, address, pitch, flip))
            rows, _width, row_words = macro.s16art.decode(
                sprite_words, bank, address, pitch, flip, entry['height'])
            poses[key, 0] = (rows, row_words)

        patterns = []
        count = struct.unpack_from('>H', blob, 6)[0]
        for index in range(count):
            _fingerprint, offset, _size = struct.unpack_from(
                '>III', blob, 32 + index * 12)
            records = struct.unpack_from('>H', blob, offset + 6)[0]
            descriptors = [struct.unpack_from(
                '>hhHBBhhIIHH', blob, offset + 16 + record * 24)
                for record in range(records)]
            patterns.append((offset, descriptors))

        def paint(screen, yy, x, row, palette):
            for word, pens in macro.emitted_words(row, x).items():
                for byte, pen in enumerate(pens):
                    if pen:
                        screen[yy][word * 4 + byte] = (palette << 4) + pen

        def punch(screen, record, pose_rows):
            x, y, bottom, _zoom, _palette = record
            right = min(320, x + max(map(len, pose_rows), default=0))
            for yy in range(y, min(bottom, 224)):
                for xx in range(max(0, x), right):
                    if (xx * 3 + yy * 5) % 13 < 3:
                        screen[yy][xx] = 0xEE

        seen = set()
        for selected in report['selected']:
            tick = selected['first_tick']
            runs = macro.live_runs(captures[tick], KEY, poses)
            self.assertTrue(runs)
            records = next(run for run in runs if len(run) == selected['records'])
            matches = []
            for offset, descriptors in patterns:
                signature = [(d[0], d[1], d[1] + d[3], d[2], d[4])
                             for d in descriptors]
                if signature == records:
                    matches.append((offset, descriptors))
            self.assertEqual(len(matches), 1)
            offset, descriptors = matches[0]
            seen.add(offset)
            scalar = [[0x77] * 320 for _ in range(224)]
            owner = [row[:] for row in scalar]
            for record in records:
                x, y, bottom, zoom, palette = record
                pose_rows = poses[KEY, zoom][0]
                for dy, row in enumerate(pose_rows[:max(0, min(bottom, 224) - y)]):
                    paint(scalar, y + dy, x, row, palette)
                punch(scalar, record, pose_rows)
            for record, descriptor in zip(records, descriptors):
                (_x, _y, zoom, _height, palette, draw_x, draw_y,
                 packet_at, packet_bytes, _pixels, _metadata_quads) = descriptor
                if packet_bytes:
                    rows, _words, end = macro.s16art.unpack_frame(
                        blob, offset + packet_at)
                    self.assertEqual(end, offset + packet_at + packet_bytes)
                    for dy, row in enumerate(rows):
                        paint(owner, record[1] + draw_y + dy,
                              record[0] + draw_x, row, palette)
                punch(owner, record, poses[KEY, zoom][0])
            self.assertEqual(owner, scalar, f'full surface differs at tick {tick}')
        self.assertEqual(len(seen), report['patterns'])


if __name__ == '__main__':
    unittest.main()
