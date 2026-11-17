#!/usr/bin/env python3
"""Altered Beast selection data: canonical descriptors and raw-only atlas replay."""
import hashlib
import json
import os
from pathlib import Path
from types import SimpleNamespace
import sys
import struct
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from bake_art import keyword,unkeyword
from bake_sprite_workingset import requested_poses
import extend_zoom_atlas as atlas

GRAPHICS=ROOT/'games/altbeast/graphics'
RAW_SHA='a86929d1c0ab5c7735b2ce77457c074dec6d3ddfab710c12ac8629a4fd16f155'
ATLAS_SHA='4334dcb929fd0cb4a90576ef48b15d08f4d3111f9537ebf90992a476cd4fa69f'
UNSCALED_SEMANTIC_SHA='42e4b051166dd1cf4380a4eda75485347fbd1336958658a6135f26cb4e9585ab'

class ManifestTests(unittest.TestCase):
    def test_scaled_canonical_selection(self):
        document=json.loads((GRAPHICS/'neff-scaled-45.json').read_text())
        poses=atlas.additions(document)
        self.assertEqual(len(poses),45)
        self.assertEqual([(keyword(e['key']),e['zoom']) for e in document['entries']],sorted(poses))
        for entry in document['entries']:
            self.assertEqual(tuple(entry['key']),unkeyword(keyword(entry['key'])))

    def test_unscaled_preserves_canonical22_selection(self):
        path=GRAPHICS/'neff-unscaled-22.json';raw=path.read_bytes();document=json.loads(raw)
        self.assertEqual(len(document['entries']),22)
        for e in document['entries']:
            self.assertEqual(set(e),{'key','height'})
            self.assertTrue(all(type(v)is int for v in e['key']))
            self.assertIs(type(e['height']),int)
            self.assertEqual(tuple(e['key']),unkeyword(keyword(e['key'])))
        poses=requested_poses(SimpleNamespace(unscaled_manifest=path,corpus=[]),{path:raw})
        self.assertEqual(len(poses),22)
        self.assertEqual([keyword(e['key']) for e in document['entries']],sorted(poses))
        canonical=json.dumps(sorted(poses.items()),separators=(',',':')).encode()
        self.assertEqual(hashlib.sha256(canonical).hexdigest(),UNSCALED_SEMANTIC_SHA)

    def test_fresh45_from_user_raw_sprites(self):
        explicit=os.environ.get('ALTBEAST_SPRITES')
        paths=[Path(explicit)] if explicit else [ROOT/'sh_src/sprites.bin',ROOT/'build/port/sh_src/sprites.bin']
        source=next((p for p in paths if p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==RAW_SHA),None)
        if source is None:
            if explicit:self.fail('ALTBEAST_SPRITES is not the expected interleaved US sprite stream')
            self.skipTest('user raw sprite ROM absent; set ALTBEAST_SPRITES to run raw-only replay')
        before=hashlib.sha256(source.read_bytes()).hexdigest()
        manifest=json.loads((GRAPHICS/'neff-scaled-45.json').read_text())
        raw=source.read_bytes();words=struct.unpack('>%dH' % (len(raw)//2),raw)
        blob,report=atlas.extend(atlas.EMPTY_BASE,manifest,words,62528)
        self.assertEqual(len(blob),62528)
        self.assertEqual(hashlib.sha256(blob).hexdigest(),ATLAS_SHA)
        self.assertEqual((report['base_entries'],report['added_entries']),(0,45))
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),before)

if __name__=='__main__':unittest.main()
