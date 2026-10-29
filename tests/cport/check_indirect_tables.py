#!/usr/bin/env python3
"""Exercise code-pointer addressing without importing the full ROM generator."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class IndirectTables(unittest.TestCase):
    def setUp(self):
        tree = ast.parse((ROOT / 'tools/cport_prep.py').read_text())
        fn = next(n for n in tree.body
                  if isinstance(n, ast.FunctionDef) and n.name == '_rewrite_indirect')
        self.env = {'re': re, '_MARK': '(**(code **)', '_ind_literal_rom': [0]}
        exec(compile(ast.Module(body=[fn], type_ignores=[]), 'indirect-pass', 'exec'), self.env)

    def rewrite(self, expression):
        result, count = self.env['_rewrite_indirect']('(**(code **)' + expression + ')();')
        self.assertEqual(count, 1)
        self.assertIn('port_call68_at(0, LD32U', result)
        return result

    def test_final_boss_literal_table(self):
        # Native 19B64..19B76: LEA $1A076,A0; state*4; MOVEA.L; JSR.
        result = self.rewrite('((short)((ushort)unaff_A6[0x20] << 2) + 0x1a076)')
        self.assertIn('((int)ROM_BASE + 0x1a076)', result)
        self.assertEqual(self.env['_ind_literal_rom'][0], 1)

    def test_symbolic_table_uses_its_linker_mapping(self):
        self.rewrite('((int)&PTR_FUN_00017e24 + (int)(short)((ushort)DAT_fffff142 << 2))')
        self.assertEqual(self.env['_ind_literal_rom'][0], 0)

    def test_object_field_is_an_offset(self):
        result = self.rewrite('(unaff_A6 + 0x36)')
        self.assertNotIn('ROM_BASE', result)

    def test_mapped_table_is_not_rebased_twice(self):
        result = self.rewrite('((short)(state << 2) + ((int)ROM_BASE + 0x1a076))')
        self.assertEqual(result.count('ROM_BASE'), 1)
        self.assertEqual(self.env['_ind_literal_rom'][0], 0)


if __name__ == '__main__':
    unittest.main()
