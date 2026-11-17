#!/usr/bin/env python3
"""Run the generated fixture decoder and compare every word with native sources."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SceneFixture(unittest.TestCase):
    def test_two_tables_emit_a_pair(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = bytearray(2048)
            struct.pack_into('>HH', source, 0, 0x1111, 0x2222)
            struct.pack_into('>H', source, 4, 0x8000)
            struct.pack_into('>HH', source, 1024, 0x3333, 0x4444)
            struct.pack_into('>H', source, 1028, 0x8000)
            table = tmp / 'pair.bin'
            header = tmp / 'fixture.h'
            table.write_bytes(source)
            subprocess.run(['python3', str(ROOT / 'tools/sprite_fixture.py'),
                            str(table), str(header)], check=True)
            text = header.read_text()
            self.assertIn('#define PORT_SPRITE_FIXTURE_PAIR 1', text)
            self.assertIn('port_sprite_fixture_next', text)
            self.assertIn('0x1111, 0x2222', text)
            self.assertIn('0x3333, 0x4444', text)

    def test_bank_selector_extracts_one_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = bytearray(2048)
            struct.pack_into('>HHH', source, 0, 0x1111, 0x2222, 0x8000)
            struct.pack_into('>HHH', source, 1024, 0x3333, 0x4444, 0x8000)
            table = tmp / 'pair.bin'
            header = tmp / 'fixture.h'
            table.write_bytes(source)
            subprocess.run(['python3', str(ROOT / 'tools/sprite_fixture.py'),
                            str(table), str(header), '--bank', '0'], check=True)
            text = header.read_text()
            self.assertNotIn('PORT_SPRITE_FIXTURE_PAIR', text)
            self.assertNotIn('port_sprite_fixture_next', text)
            self.assertIn('0x1111, 0x2222', text)
            self.assertNotIn('0x3333, 0x4444', text)

    def test_selected_pages_decode_exactly_and_respect_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            table = tmp / 'sprite.bin'
            sprites = bytearray(1024)
            struct.pack_into('>H', sprites, 4, 0x8000)
            table.write_bytes(sprites)
            prefix = tmp / 'scene'
            tiles = bytes((i * 17 + (i >> 9)) & 255 for i in range(65536))
            text = bytearray(4096)
            struct.pack_into('>HH', text, 0x740 * 2, 0xc720, 0xff00)
            Path(str(prefix) + '_tilemap.bin').write_bytes(tiles)
            Path(str(prefix) + '_text.bin').write_bytes(text)
            header = tmp / 'fixture.h'
            subprocess.run(['python3', str(ROOT / 'tools/sprite_fixture.py'),
                            str(table), str(header), str(prefix)], check=True)
            harness = tmp / 'decode.c'
            harness.write_text('''#include <stdio.h>
#include <assert.h>
#include "fixture.h"
static unsigned short tiles[0x6802], text[2050];
int main(void) {
    unsigned i;
    tiles[0]=tiles[0x6801]=text[0]=text[2049]=0xface;
    port_plane_fixture_apply(tiles+1,text+1);
    assert(tiles[0]==0xface && tiles[0x6801]==0xface);
    assert(text[0]==0xface && text[2049]==0xface);
    for(i=1;i<=0x6800;i++){putchar(tiles[i]>>8);putchar(tiles[i]&255);}
    for(i=1;i<=2048;i++){putchar(text[i]>>8);putchar(text[i]&255);}
    return port_sprite_fixture[2]!=0x8000;
}
''')
            exe = tmp / 'decode'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c99', '-O1',
                            '-fsanitize=address,undefined', str(harness), '-o', str(exe)], check=True)
            decoded = subprocess.check_output([str(exe)])
            expected = bytearray(0xd000)
            for page in (0, 2, 7, 12):
                expected[page*4096:(page+1)*4096] = tiles[page*4096:(page+1)*4096]
            self.assertEqual(decoded, expected + text)


if __name__ == '__main__':
    unittest.main()
