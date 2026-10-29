#!/usr/bin/env python3
"""Mixed byte/word object-status accesses must retain their native widths."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class BossStatusByte(unittest.TestCase):
    def setUp(self):
        tree = ast.parse((ROOT / 'tools/cport_prep.py').read_text())
        fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef)
                  and n.name == '_boss_status_byte')
        self.env = {'re': re, '_bb': [0]}
        exec(compile(ast.Module(body=[fn], type_ignores=[]), 'boss-status', 'exec'), self.env)

    def test_all_native_byte_test_entries(self):
        for entry in ['18afc','18b4a','18b90','1946c','194a0']:
            with self.subTest(entry=entry):
                src = 'uint FUN_000%s()\n{\n  if ((DAT_ffffd400 & 8) == 0) return 0;\n}\n' % entry
                out = self.env['_boss_status_byte'](src)
                self.assertIn('*(volatile byte *)&DAT_ffffd400 & 8', out)
        self.assertEqual(self.env['_bb'][0], 5)

    def test_word_writes_and_unrelated_functions_are_preserved(self):
        src = 'uint FUN_000191de()\n{\n  _DAT_ffffd400 = 0x8000;\n}\n'
        self.assertEqual(self.env['_boss_status_byte'](src), src)

    def test_big_endian_drawn_flag(self):
        # Native BTST #3,(A5) tests the first byte, not bit3 of a word.
        for status, visible in [(0x8800,True),(0x8000,False),(0x0008,False)]:
            self.assertEqual(bool(status.to_bytes(2,'big')[0] & 8), visible)
        self.assertFalse(0x8800 & 8)

if __name__ == '__main__':
    unittest.main()
