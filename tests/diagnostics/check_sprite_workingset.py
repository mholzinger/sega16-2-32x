#!/usr/bin/env python3
"""Offline working-set page tests; no compiler, emulator, ROM build or network."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import s16art
import pack_sprite_macros
from bake_art import keyword
from bake_zoom import pack
from bake_sprite_workingset import (build_opaque_run_sidecar,
                                    verify_opaque_run_sidecar)


class WorkingSetTests(unittest.TestCase):
    def test_opaque_run_sidecar_selects_longest_thresholded_interval(self):
        rows = [[1, 2, 3, 4, 5, 6, 7, 8, 1, 0, 2, 3,
                 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 1,
                 2, 3, 4, 5],
                [1, 2, 3, 0]]
        body = s16art.pack_frame(rows, [7, 1])
        atlas = struct.pack('>4sIIIIII', b'ZB1\0', 1, 28 + len(body),
                            0, 0x1234, 1, 28) + body
        sidecar = build_opaque_run_sidecar(atlas, 4)
        self.assertEqual(verify_opaque_run_sidecar(atlas, sidecar), 1)
        pose = struct.unpack_from('>I', sidecar, 16)[0]
        self.assertEqual(struct.unpack_from('>HH', sidecar, pose), (2, 0))
        self.assertEqual(sidecar[pose + 4:pose + 8], bytes((3, 4, 0, 0)))

        corrupt = bytearray(sidecar)
        corrupt[pose + 4:pose + 6] = bytes((2, 5))  # includes the zero group
        with self.assertRaisesRegex(ValueError, 'transparent pen'):
            verify_opaque_run_sidecar(atlas, corrupt)
        with self.assertRaisesRegex(ValueError, 'threshold'):
            build_opaque_run_sidecar(atlas, 0)

        pen15 = bytearray(atlas)
        pen15[28 + 4 + 4] = 15  # packet header, then first row header
        with self.assertRaisesRegex(ValueError, '0..14'):
            build_opaque_run_sidecar(pen15, 4)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        words = [0] * 65536
        words[1:3] = [0x123f, 0x450f]
        words[0x101:0x103] = [0xabcf, 0xde0f]
        self.words = tuple(words)
        self.raw = struct.pack('>65536H', *words)
        self.key = keyword((0, 0, 1, 0))
        self.other = keyword((0, 0x100, 1, 0))
        rows, _, counts = s16art.decode(words, 0, 0, 1, 0, 2)
        self.global_packet = s16art.pack_frame(rows, counts)
        rom = bytearray(b'\xff' * 0x4000)
        rom[0x1000:0x1000 + len(self.global_packet)] = self.global_packet
        struct.pack_into('>4sIIIII', rom, 0x3000, b'AB1\0', 1,
                         24 + len(self.global_packet), 0, self.key, 0x02001000)
        self.rom = bytes(rom)
        scaled, _, _ = s16art.decode_scaled(words, 0, 0, 1, 0, 2, 1, 0)
        scaled_packet = pack(scaled)
        self.atlas = struct.pack('>4sIIIIII', b'ZB1\0', 1, 28 + len(scaled_packet),
                                 0, self.key, 1, 28) + scaled_packet
        self.music = b'unchanged music\x00' * 13
        self.write('sprites.bin', self.raw)
        self.write('base.32x', self.rom)
        self.write('zoom.bin', self.atlas)
        self.write('music.bin', self.music)
        self.manifest = {'entries': [dict(key=[0, 0, 1, 0], height=2),
                                     dict(key=[0, 0x100, 1, 0], height=2)]}
        self.write('poses.json', json.dumps(self.manifest).encode())

    def write(self, name, raw):
        (self.directory / name).write_bytes(raw)

    @staticmethod
    def transition_blob():
        palette = bytes((5, 0, 0, 0))
        state = struct.pack('>HH6H', 1, 16, 1, 2, 3, 4, 5, 6)
        packet_offset = 40 + len(palette) + len(state) * 2
        dictionary_offset = packet_offset + 24
        body_offset = dictionary_offset + 4
        body = struct.pack('>4BH', 2, 3, 1, 0, 0)
        total = body_offset + len(body)
        header = struct.pack('>4sHHHHIIIIIII', b'TP1\0', 1, 1, 2, 1, 1,
                             0x031401D0, 40 + len(palette), packet_offset,
                             dictionary_offset, body_offset, total)
        descriptor = struct.pack('>HHIIIHHI', 0, 1, body_offset, len(body),
                                 10, 1, 1, 7)
        return (header + palette + state * 2 + descriptor +
                b'\x01\x02\x03\x04' + body)

    def command(self, *extra):
        d = self.directory
        return [sys.executable, str(ROOT / 'tools/bake_sprite_workingset.py'),
                '--base-rom', str(d / 'base.32x'), '--index-rom-offset', '0x3000',
                '--global-window', '0x02001000:0x1000',
                '--sprites', str(d / 'sprites.bin'), '--unscaled-manifest', str(d / 'poses.json'),
                '--zoom-atlas', str(d / 'zoom.bin'), '--music', str(d / 'music.bin'),
                '--page-bytes', '4096', '--out', str(d / 'page.bin'),
                '--metadata', str(d / 'page.json'), '--header', str(d / 'page.h'), *extra]

    def run_bake(self, *extra, success=True):
        result = subprocess.run(self.command(*extra), capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def chunk_command(self, *extra):
        self.write('global.bin', self.global_packet)
        self.write('index.bin', self.rom[0x3000:0x3018])
        command = self.command()
        for option in ['--base-rom', '--index-rom-offset', '--global-window']:
            at = command.index(option)
            del command[at:at + 2]
        return command + ['--global-index-bin', str(self.directory / 'index.bin'),
                          '--global-chunk', '0x02001000:' + str(self.directory / 'global.bin'), *extra]

    def test_exact_page_and_determinism(self):
        self.run_bake()
        d = self.directory
        page = (d / 'page.bin').read_bytes()
        metadata = json.loads((d / 'page.json').read_text())
        self.assertEqual(len(page), 4096)
        self.assertEqual(page[:len(self.music)], self.music)
        at = metadata['zb1_offset']
        self.assertEqual(page[at:at + len(self.atlas)], self.atlas)
        self.assertEqual(page[-64:], b'\xff' * 64)
        self.assertEqual(metadata['accepted_global_entries'], 1)
        self.assertEqual(metadata['local_unscaled_packets'], 1)
        self.assertEqual(metadata['ab1_entries'], 2)
        ix = metadata['ab1_offset']
        self.assertEqual(struct.unpack_from('>II', page, ix + 16), (self.key, 0x02001000))
        local_key, local_address = struct.unpack_from('>II', page, ix + 24)
        self.assertEqual(local_key, self.other)
        rows, extents, _ = s16art.unpack_frame(page, local_address - metadata['page_address'])
        expected, _, expected_extents = s16art.decode(self.words, 0, 0x100, 1, 0, 2)
        self.assertEqual(rows, expected)
        self.assertEqual(extents, expected_extents)
        self.assertEqual((d / 'base.32x').read_bytes(), self.rom)
        self.assertEqual(metadata['page_sha256'], hashlib.sha256(page).hexdigest())
        self.assertIn('SPRITE_WORKSET_ZB1_ADDRESS', (d / 'page.h').read_text())
        self.run_bake()
        self.assertEqual((d / 'page.bin').read_bytes(), page)

    def test_verified_transition_blob_is_appended_and_bounded(self):
        transition = self.transition_blob()
        self.write('transition.bin', transition)
        self.run_bake('--transition-packets', str(self.directory / 'transition.bin'))
        metadata = json.loads((self.directory / 'page.json').read_text())
        page = (self.directory / 'page.bin').read_bytes()
        at = metadata['transition_offset']
        self.assertEqual(page[at:at + len(transition)], transition)
        self.assertEqual(metadata['transition_bytes'], len(transition))
        self.assertEqual(metadata['transition_sha256'], hashlib.sha256(transition).hexdigest())
        self.assertEqual(metadata['transition_old_count_mask'], 1 << 1)
        self.assertEqual(metadata['transition_new_count_mask'], 1 << 1)
        self.assertEqual(metadata['transition_count_pairs'], [[1, 1]])
        self.assertEqual(len(metadata['transition_fingerprint_pairs']), 1)
        self.assertIn('SPRITE_WORKSET_TRANSITION_ADDRESS',
                      (self.directory / 'page.h').read_text())
        header = (self.directory / 'page.h').read_text()
        self.assertIn('SPRITE_WORKSET_TRANSITION_OLD_COUNT_MASK_LO 0x2u', header)
        self.assertIn('SPRITE_WORKSET_TRANSITION_NEW_COUNT_MASK_LO 0x2u', header)
        self.assertIn('SPRITE_WORKSET_TRANSITION_TARGET_KEY 0x31401D0u', header)
        self.assertIn('SPRITE_WORKSET_TRANSITION_COUNT_PAIR_CASES case 0x41u:',
                      header)
        self.assertIn('SPRITE_WORKSET_TRANSITION_COUNT_PAIR_MATCH(pair)', header)
        self.assertIn('SPRITE_WORKSET_TRANSITION_FINGERPRINT_CASES case 0x',
                      header)

        corrupt = bytearray(transition)
        corrupt[-1] = 1  # dictionary index 1, but only entry 0 exists
        self.write('transition.bin', corrupt)
        saved = [(self.directory / name).read_bytes()
                 for name in ('page.bin', 'page.json', 'page.h')]
        self.run_bake('--transition-packets', str(self.directory / 'transition.bin'),
                      success=False)
        self.assertEqual([(self.directory / name).read_bytes()
                          for name in ('page.bin', 'page.json', 'page.h')], saved)

    def test_verified_owner_macros_follow_transition_and_are_bounded(self):
        poses = {(self.key, 1): (
            [[1, 2, 3, 4, 5, 6, 7, 8],
             [8, 7, 6, 5, 4, 3, 2, 1]], [2, 2],
            [(0, (), 8), (0, (), 8)], 8)}
        patterns = [dict(first_tick=10, frequency=3,
                         records=[(40, 20, 22, 1, 2),
                                  (44, 20, 22, 1, 3)])]
        macros, report = pack_sprite_macros.pack_patterns(
            patterns, self.key, poses, 2048)
        transition = self.transition_blob()
        self.write('macros.bin', macros)
        self.write('transition.bin', transition)
        self.run_bake('--transition-packets', str(self.directory / 'transition.bin'),
                      '--macro-packets', str(self.directory / 'macros.bin'))
        metadata = json.loads((self.directory / 'page.json').read_text())
        page = (self.directory / 'page.bin').read_bytes()
        transition_at = metadata['transition_offset']
        self.assertEqual(page[transition_at:transition_at + len(transition)],
                         transition)
        at = metadata['macro_offset']
        self.assertEqual(at, (transition_at + len(transition) + 3) & ~3)
        self.assertEqual(page[at:at + len(macros)], macros)
        self.assertEqual(metadata['macro_bytes'], len(macros))
        self.assertEqual(metadata['macro_patterns'], report['patterns'])
        self.assertEqual(metadata['macro_max_records'], 2)
        self.assertEqual(metadata['macro_target_key'], self.key)
        header = (self.directory / 'page.h').read_text()
        self.assertIn('SPRITE_WORKSET_MACRO_ADDRESS', header)
        self.assertIn('SPRITE_WORKSET_MACRO_TARGET_KEY', header)
        self.assertIn('#define SPRITE_WORKSET_MACRO_MAX_RECORDS 0x2u', header)

        corrupt = bytearray(macros)
        corrupt[0] = 0
        self.write('macros.bin', corrupt)
        saved = [(self.directory / name).read_bytes()
                 for name in ('page.bin', 'page.json', 'page.h')]
        self.run_bake('--transition-packets', str(self.directory / 'transition.bin'),
                      '--macro-packets', str(self.directory / 'macros.bin'),
                      success=False)
        self.assertEqual([(self.directory / name).read_bytes()
                          for name in ('page.bin', 'page.json', 'page.h')], saved)

    def test_overflow_fails_before_output(self):
        self.run_bake('--page-bytes', '256', success=False)
        self.assertFalse((self.directory / 'page.bin').exists())

    def test_exact_boundary_then_one_word_overflow(self):
        self.run_bake()
        metadata = json.loads((self.directory / 'page.json').read_text())
        guard = 4096 - metadata['used_page_bytes']
        self.run_bake('--guard-bytes', str(guard))
        self.assertEqual(json.loads((self.directory / 'page.json').read_text())['free_before_guard_bytes'], 0)
        page = (self.directory / 'page.bin').read_bytes()
        self.run_bake('--guard-bytes', str(guard + 4), success=False)
        self.assertEqual((self.directory / 'page.bin').read_bytes(), page)

    def test_zero_code_reservation_has_exact_disabled_identity(self):
        first = self.run_bake()
        outputs = [self.directory / name for name in ('page.bin', 'page.json', 'page.h')]
        saved = [p.read_bytes() for p in outputs]
        second = self.run_bake('--code-bytes', '0')
        self.assertEqual([p.read_bytes() for p in outputs], saved)
        self.assertEqual((first.stdout, first.stderr), (second.stdout, second.stderr))
        self.assertNotIn('code_', json.loads(saved[1]))
        self.assertNotIn(b'_CODE_', saved[2])

    def test_code_reservation_preserves_all_payload_bytes_and_global_references(self):
        self.run_bake()
        d = self.directory
        page = (d / 'page.bin').read_bytes()
        original = json.loads((d / 'page.json').read_text())
        self.run_bake('--code-bytes', '0x800')
        metadata = json.loads((d / 'page.json').read_text())
        self.assertEqual((d / 'page.bin').read_bytes(), page)
        for key, value in original.items():self.assertEqual(metadata[key], value, key)
        self.assertEqual(metadata['code_offset'], 4096 - 64 - 2048)
        self.assertEqual(metadata['code_address'], metadata['page_address'] + metadata['code_offset'])
        self.assertEqual(metadata['code_capacity_bytes'], 2048)
        self.assertEqual(metadata['free_art_gap_bytes'], metadata['code_offset'] - metadata['used_page_bytes'])
        self.assertEqual(page[metadata['code_offset']:-64], b'\xff' * 2048)
        header = (d / 'page.h').read_text()
        for name in ('CODE_OFFSET', 'CODE_ADDRESS', 'CODE_CAPACITY_BYTES', 'FREE_ART_GAP_BYTES'):
            self.assertIn('SPRITE_WORKSET_' + name, header)
        self.assertEqual(page[-64:], b'\xff' * 64)

    def test_code_exact_fit_and_payload_overrun_preserve_outputs(self):
        # Align unused atlas tail padding, retaining the exact frame packet.
        self.run_bake()
        used = json.loads((self.directory / 'page.json').read_text())['used_page_bytes']
        padding = (-used) % 64
        atlas = bytearray(self.atlas + b'\xff' * padding)
        struct.pack_into('>I', atlas, 8, len(atlas))
        self.write('zoom.bin', atlas)
        self.run_bake()
        used = json.loads((self.directory / 'page.json').read_text())['used_page_bytes']
        self.assertEqual(used % 64, 0)
        guard = 4096 - 64 - used
        self.run_bake('--code-bytes', '64', '--guard-bytes', str(guard))
        metadata = json.loads((self.directory / 'page.json').read_text())
        self.assertEqual(metadata['code_offset'], used)
        self.assertEqual(metadata['free_art_gap_bytes'], 0)
        saved = [(self.directory / name).read_bytes() for name in ('page.bin', 'page.json', 'page.h')]
        atlas += b'\xff' * 64
        struct.pack_into('>I', atlas, 8, len(atlas))
        self.write('zoom.bin', atlas)
        failed = self.run_bake('--code-bytes', '64', '--guard-bytes', str(guard), success=False)
        self.assertIn('exceeds page budget', failed.stderr)
        self.assertEqual([(self.directory / name).read_bytes() for name in ('page.bin', 'page.json', 'page.h')], saved)

    def test_code_bounds_alignment_and_alias_fail_before_output(self):
        for extra in [('--code-bytes', '-64'), ('--code-bytes', '1'), ('--code-bytes', '65'),
                      ('--code-bytes', '4096'), ('--code-bytes', '8192'),
                      ('--code-bytes', '64', '--guard-bytes', '4'),
                      ('--code-bytes', '64', '--page-address', '0x02380010')]:
            self.run_bake(*extra, success=False)
            self.assertFalse((self.directory / 'page.bin').exists())
        self.run_bake('--code-bytes', '64', '--out', str(self.directory / 'base.32x'), success=False)
        self.assertEqual((self.directory / 'base.32x').read_bytes(), self.rom)
        os.link(self.directory / 'zoom.bin', self.directory / 'page.bin')
        self.run_bake('--code-bytes', '64', success=False)
        self.assertEqual((self.directory / 'zoom.bin').read_bytes(), self.atlas)

    def test_code_reservation_keeps_packet_extent_and_page_alias_checks(self):
        self.run_bake('--code-bytes', '64', '--page-address', '0x02001000', success=False)
        self.assertFalse((self.directory / 'page.bin').exists())
        self.run_bake('--code-bytes', '64')
        saved = [(self.directory / name).read_bytes() for name in ('page.bin', 'page.json', 'page.h')]
        bad = bytearray(self.atlas)
        struct.pack_into('>I', bad, 24, len(bad) - 4)
        self.write('zoom.bin', bad)
        self.run_bake('--code-bytes', '64', success=False)
        self.assertEqual([(self.directory / name).read_bytes() for name in ('page.bin', 'page.json', 'page.h')], saved)

    def test_corrupt_global_pixels_rejected(self):
        bad = bytearray(self.rom)
        bad[0x1008] ^= 1
        self.write('base.32x', bad)
        self.run_bake(success=False)
        self.assertFalse((self.directory / 'page.bin').exists())

    def test_corrupt_atlas_pixels_and_offset_rejected(self):
        bad = bytearray(self.atlas)
        bad[-4] ^= 1
        self.write('zoom.bin', bad)
        self.run_bake(success=False)
        bad = bytearray(self.atlas)
        struct.pack_into('>I', bad, 24, 0)
        self.write('zoom.bin', bad)
        self.run_bake(success=False)

    def test_global_short_height_is_replaced_locally(self):
        rows, _, counts = s16art.decode(self.words, 0, 0, 1, 0, 1)
        short = s16art.pack_frame(rows, counts)
        rom = bytearray(self.rom)
        rom[0x1000:0x1000 + len(short)] = short
        self.write('base.32x', rom)
        self.run_bake()
        metadata = json.loads((self.directory / 'page.json').read_text())
        self.assertEqual(metadata['local_unscaled_packets'], 2)
        self.assertEqual(metadata['ab1_entries'], 2)
        self.assertEqual(metadata['local_overrides_global_keys'], [f'{self.key:08x}'])

    def test_decoded_global_index_and_corpus_inputs(self):
        d = self.directory
        self.write('globals.json', json.dumps({'entries': [dict(key=self.key, address=0x02001000,
                                                               packet_hex=self.global_packet.hex())]}).encode())
        table = bytearray(1024)
        struct.pack_into('>8H', table, 0, 2 << 8, 184, 1, 0x100, 0, 0, 0, 0)
        struct.pack_into('>H', table, 20, 0x8000)
        self.write('spr.bin', table)
        command = self.command()
        for option in ['--base-rom', '--index-rom-offset', '--unscaled-manifest']:
            at = command.index(option)
            del command[at:at + 2]
        command += ['--global-index-json', str(d / 'globals.json'), '--corpus', str(d / 'spr.bin')]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads((d / 'page.json').read_text())['requested_unscaled_keys'], 1)

    def test_output_must_not_alias_inputs(self):
        self.run_bake('--out', str(self.directory / 'base.32x'), success=False)
        self.assertEqual((self.directory / 'base.32x').read_bytes(), self.rom)

    def test_output_hardlink_must_not_modify_input(self):
        os.link(self.directory / 'base.32x', self.directory / 'page.bin')
        self.run_bake(success=False)
        self.assertEqual((self.directory / 'base.32x').read_bytes(), self.rom)

    def test_global_packet_must_not_cross_into_mapper_page(self):
        self.run_bake('--page-address', '0x02001010', success=False)
        self.assertFalse((self.directory / 'page.bin').exists())

    def test_binary_index_chunks_match_rom_mode(self):
        self.run_bake()
        expected = (self.directory / 'page.bin').read_bytes()
        result = subprocess.run(self.chunk_command(), capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.directory / 'page.bin').read_bytes(), expected)
        self.assertEqual((self.directory / 'global.bin').read_bytes(), self.global_packet)
        self.assertEqual((self.directory / 'index.bin').read_bytes(), self.rom[0x3000:0x3018])
        metadata = json.loads((self.directory / 'page.json').read_text())
        self.assertEqual(metadata['global_chunk_inputs'][0]['address'], 0x02001000)
        self.assertNotIn(str(self.directory / 'base.32x'), metadata['input_sha256'])

    def test_binary_chunk_data_bounds_and_input_collision(self):
        command = self.chunk_command()
        bad = bytearray(self.global_packet)
        bad[8] ^= 1
        for raw in [bad, self.global_packet[:4]]:
            self.write('global.bin', raw)
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((self.directory / 'page.bin').exists())
        self.write('global.bin', self.global_packet)
        command += ['--out', str(self.directory / 'global.bin')]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.directory / 'global.bin').read_bytes(), self.global_packet)

    def test_binary_index_input_cannot_be_output(self):
        command = self.chunk_command('--out', str(self.directory / 'index.bin'))
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.directory / 'index.bin').read_bytes(), self.rom[0x3000:0x3018])

    def test_overlapping_global_chunks_rejected(self):
        command = self.chunk_command('--global-chunk',
                                     '0x02001010:' + str(self.directory / 'global.bin'))
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('global chunks overlap', result.stderr)

    def test_music_must_match_base_when_requested(self):
        self.run_bake('--music-rom-offset', '0', success=False)
        self.assertFalse((self.directory / 'page.bin').exists())

    def test_decoded_globals_cannot_disagree_at_one_address(self):
        rows, _, counts = s16art.decode(self.words, 0, 0x100, 1, 0, 2)
        other = s16art.pack_frame(rows, counts)
        entries = [dict(key=self.key, address=0x02001000, packet_hex=self.global_packet.hex()),
                   dict(key=self.other, address=0x02001000, packet_hex=other.hex())]
        self.write('globals.json', json.dumps({'entries': entries}).encode())
        command = self.command('--global-index-json', str(self.directory / 'globals.json'))
        for option in ['--base-rom', '--index-rom-offset']:
            at = command.index(option)
            del command[at:at + 2]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('global packets conflict', result.stderr)
        self.assertFalse((self.directory / 'page.bin').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
