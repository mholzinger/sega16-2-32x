#!/usr/bin/env python3
"""Lossless explicit ZB1 extension; synthetic ROM only, no emulator/build."""
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
sys.path.insert(0, str(ROOT/'tools'))
import s16art
from bake_art import keyword
import bake_zoom
import extend_zoom_atlas as extension


class ExtensionTests(unittest.TestCase):
    def setUp(self):
        self.words = (0xf12f,)*65536
        self.key = [0, 0, 1, 0]
        rows, _, _ = s16art.decode_scaled(self.words, *self.key, 2, 1, 0)
        self.body = bake_zoom.pack(rows)
        self.base = struct.pack('>4sIIIIII', b'ZB1\0', 1, 28+len(self.body),
                                0, keyword(self.key), 1, 28)+self.body
        self.manifest = {'entries': [dict(key=self.key, zoom=2, height=3),
                                     dict(key=[0, 100, -1, 1], zoom=66, height=4)]}

    def test_exact_preservation_sort_and_decode(self):
        blob, report = extension.extend(self.base, self.manifest, self.words, 4096)
        packets = extension.raw_packets(blob)
        self.assertEqual(packets[keyword(self.key), 1], self.body)
        self.assertEqual(bake_zoom.verify(blob, self.words), 3)
        self.assertEqual(report['base_entries'], 1)
        self.assertEqual(report['added_entries'], 2)
        self.assertEqual(report['sha256'], hashlib.sha256(blob).hexdigest())
        self.assertEqual(extension.extend(blob, {'entries': []}, self.words, len(blob))[0], blob)

    def test_budget_exact_boundary_and_no_drop(self):
        blob, _ = extension.extend(self.base, self.manifest, self.words, 4096)
        self.assertEqual(extension.extend(self.base, self.manifest, self.words, len(blob))[0], blob)
        with self.assertRaisesRegex(ValueError, 'no entries dropped'):
            extension.extend(self.base, self.manifest, self.words, len(blob)-1)

    def test_manifest_negative_cases(self):
        bad = [[], {}, {'entries': {}}, {'entries': [dict(key=self.key, height=2)]}]
        for name, value in [('zoom', 0), ('zoom', 1024), ('zoom', True),
                            ('height', 0), ('height', 225), ('height', 1.5)]:
            entry = dict(key=self.key, zoom=2, height=3);entry[name] = value
            bad.append({'entries': [entry]})
        for key in [[8, 0, 1, 0], [0, 65536, 1, 0], [0, 0, -129, 0],
                    [0, 0, 128, 0], [0, 0, 1, 2], [0, 0, 1], '00010000']:
            bad.append({'entries': [dict(key=key, zoom=2, height=3)]})
        bad += [{'entries': [self.manifest['entries'][0]]*2},
                {'entries': [dict(key=self.key, zoom=1, height=2)]}]
        for manifest in bad:
            with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                extension.extend(self.base, manifest, self.words, 4096)

    def test_malformed_base_before_decode(self):
        changes = [(0, b'ZP1\0'), (4, struct.pack('>I', 0xffffffff)),
                   (8, struct.pack('>I', len(self.base)+4)), (12, b'\0\0\0\1'),
                   (16, b'\x80\0\0\0'), (20, b'\0\0\0\0'),
                   (24, struct.pack('>I', 16)), (24, struct.pack('>I', 29)),
                   (28, struct.pack('>HH', 225, 4)),
                   (28, struct.pack('>HH', 2, 321)),
                   (32, struct.pack('>BBH', 0, 255, 0xfffc)),
                   (32, struct.pack('>BBH', 0, 1, 4)),
                   (36, b'\xff\xff\xff\xff')]
        for offset, value in changes:
            base = bytearray(self.base);base[offset:offset+len(value)] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                extension.extend(base, {'entries': []}, self.words, 4096)
        with self.assertRaises(ValueError):extension.raw_packets(self.base[:-1])
        truncated = bytearray(self.base[:-4]);struct.pack_into('>I', truncated, 8, len(truncated))
        with self.assertRaises(ValueError):extension.raw_packets(truncated)

    def test_overlap_and_duplicate_index_rejected(self):
        base = struct.pack('>4sIII', b'ZB1\0', 2, 40+len(self.body), 0)
        base += struct.pack('>6I', keyword(self.key), 1, 40, keyword(self.key), 2, 40)+self.body
        with self.assertRaisesRegex(ValueError, 'overlapping'):extension.raw_packets(base)
        duplicate = bytearray(base);struct.pack_into('>I', duplicate, 32, 1)
        with self.assertRaisesRegex(ValueError, 'unique'):extension.raw_packets(duplicate)

    def test_pixels_must_match_raw_rom(self):
        base = bytearray(self.base);base[36] = 7
        with self.assertRaisesRegex(ValueError, 'disagree'):
            extension.extend(base, {'entries': []}, self.words, 4096)

    def test_unrepresentable_or_missing_source_declines(self):
        with self.assertRaises(ValueError):
            extension.extend(self.base, {'entries': [dict(key=[7, 0, 1, 0], zoom=2, height=3)]},
                             self.words, 4096)
        words=list(self.words);words[100:201]=[0x1234]*100+[0x123f]
        with self.assertRaises(ValueError):
            extension.extend(self.base, {'entries': [dict(key=[0, 99, 1, 0], zoom=2, height=1)]},
                             words, 4096)

    def test_original_selection_cli_still_exact(self):
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);raw=d/'sprites.bin';raw.write_bytes(struct.pack('>65536H', *self.words))
            sprite=bytearray(1024)
            struct.pack_into('>8H', sprite, 0, 0x0200, 184, 1, 0, 0, 1, 0, 0)
            struct.pack_into('>H', sprite, 20, 0x8000)
            (d/'spr.bin').write_bytes(sprite)
            command=[sys.executable, str(ROOT/'tools/bake_zoom.py'), '--rom', str(raw),
                     '--out', str(d/'old.bin'), '--budget', '4096', 'synthetic='+str(d)]
            subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
            self.assertEqual((d/'old.bin').read_bytes(), self.base)

    def test_alias_checks_and_cli_validation_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary);base=d/'base.bin';base.write_bytes(self.base)
            raw=d/'sprites.bin';raw.write_bytes(struct.pack('>65536H', *self.words))
            manifest=d/'poses.json';manifest.write_text(json.dumps(self.manifest))
            for alias in [base, d/'symlink.bin', d/'hardlink.bin']:
                if alias.name == 'symlink.bin':alias.symlink_to(base)
                if alias.name == 'hardlink.bin':os.link(base, alias)
                with self.subTest(alias=alias), self.assertRaises(ValueError):
                    extension.reject_aliases([base], [alias])
            with self.assertRaises(ValueError):extension.reject_aliases([raw], [d/'out', d/'out'])
            out=d/'out.bin';out.write_bytes(b'existing output')
            command=[sys.executable, str(ROOT/'tools/extend_zoom_atlas.py'), '--base-atlas', str(base),
                     '--manifest', str(manifest), '--sprites', str(raw), '--budget', '16', '--out', str(out)]
            result=subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(out.read_bytes(), b'existing output')
            command[command.index('--budget')+1]='4096'
            subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
            self.assertEqual(out.read_bytes(), extension.extend(self.base,self.manifest,self.words,4096)[0])
            self.assertEqual(base.read_bytes(), self.base)
            command[command.index('--out')+1]=str(d/'hardlink.bin')
            result=subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(base.read_bytes(), self.base)

    def test_empty_base_is_canonical_and_matches_extension(self):
        self.assertEqual(extension.EMPTY_BASE, struct.pack('>4sIII', b'ZB1\0', 0, 16, 0))
        all_entries = {'entries': [dict(key=self.key, zoom=1, height=2)] + self.manifest['entries']}
        fresh, report = extension.extend(extension.EMPTY_BASE, all_entries, self.words, 4096)
        legacy, _ = extension.extend(self.base, self.manifest, self.words, 4096)
        self.assertEqual(fresh, legacy)
        self.assertEqual((report['base_entries'], report['added_entries']), (0, 3))
        self.assertEqual(extension.extend(extension.EMPTY_BASE, {'entries': []}, (), 16)[0],
                         extension.EMPTY_BASE)

    def test_empty_cli_inputs_hashes_budget_and_aliases(self):
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);raw=d/'sprites.bin';raw.write_bytes(struct.pack('>65536H', *self.words))
            manifest=d/'poses.json';manifest.write_text(json.dumps(self.manifest))
            out=d/'out.bin';report=d/'report.json'
            inputs={p:p.read_bytes() for p in [raw,manifest]}
            command=[sys.executable,str(ROOT/'tools/extend_zoom_atlas.py'),'--empty-base',
                     '--manifest',str(manifest),'--sprites',str(raw),'--budget','4096',
                     '--out',str(out),'--report',str(report)]
            subprocess.run(command,capture_output=True,text=True,timeout=10,check=True)
            self.assertEqual(out.read_bytes(), extension.extend(extension.EMPTY_BASE,self.manifest,self.words,4096)[0])
            info=json.loads(report.read_text())
            self.assertEqual(info['inputs'], {str(p):hashlib.sha256(v).hexdigest() for p,v in inputs.items()})
            self.assertEqual(info['base_source']['bytes'],16)
            self.assertEqual(info['base_source']['sha256'],hashlib.sha256(extension.EMPTY_BASE).hexdigest())
            saved=out.read_bytes(),report.read_bytes()
            command[command.index('--budget')+1]='16'
            self.assertNotEqual(subprocess.run(command,capture_output=True,timeout=10).returncode,0)
            self.assertEqual((out.read_bytes(),report.read_bytes()),saved)
            command[command.index('--budget')+1]='4096'
            sym=d/'manifest-link';sym.symlink_to(manifest)
            hard=d/'raw-link';os.link(raw,hard)
            for alias in [raw,manifest,sym,hard,report]:
                bad=command.copy();bad[bad.index('--out')+1]=str(alias)
                self.assertNotEqual(subprocess.run(bad,capture_output=True,timeout=10).returncode,0)
            self.assertEqual({p:p.read_bytes() for p in inputs},inputs)
            self.assertEqual(report.read_bytes(),saved[1])

    def test_base_selection_required_and_mutually_exclusive(self):
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);out=d/'out';out.write_bytes(b'keep output')
            common=['--manifest',str(d/'absent.json'),'--sprites',str(d/'absent.bin'),
                    '--budget','16','--out',str(out)]
            for base in [[],['--empty-base','--base-atlas',str(d/'absent.zb1')]]:
                run=subprocess.run([sys.executable,str(ROOT/'tools/extend_zoom_atlas.py'),*base,*common],
                                   capture_output=True,text=True,timeout=10)
                self.assertEqual(run.returncode,2)
                self.assertEqual(out.read_bytes(),b'keep output')


if __name__ == '__main__':
    unittest.main()
