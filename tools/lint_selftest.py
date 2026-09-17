#!/usr/bin/env python3
"""SELF-TEST for the linters — a rule nobody has seen fire is a rule
nobody should trust. Each fixture is a minimal violation of one rule.

    tools/lint_selftest.py        # exits 1 if any rule fails to fire

The first cut of H4 fired on a tracer COMMENT that mentioned 0x85F800
and missed nothing else, because it never noticed FM being cleared 100
lines earlier. Both directions are tested here.
"""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lint32x, ctr_audit
from cguard import scan, parse_defs

FIX = {
 'H1': ('sh', '''
void f(void) { *(uint16_t *)0x24012000 = 1; }
''', True),
 'H1-ok': ('sh', '''
void f(void) { *(volatile uint16_t *)0x24012000 = 1; }
''', False),
 'H2': ('sh', '''
#define A ((volatile uint16_t *)0x2603A780)
#define B ((volatile uint16_t *)0x0603A780)
''', True),
 'H4': ('md', '''
void shim(void) {
    *(volatile uint16_t*)0xA15100 |= 0x8000;
    *(volatile uint16_t*)0x85E000 = 7;
}
''', True),
 'H4-cleared': ('md', '''
void shim(void) {
    *(volatile uint16_t*)0xA15100 |= 0x8000;
    *(volatile uint16_t*)0xA15100 &= 0x7FFF;
    *(volatile uint16_t*)0x85E000 = 7;
}
''', False),
 'H4-comment': ('md', '''
void shim(void) {
    *(volatile uint16_t*)0xA15100 |= 0x8000;
    /* the sprite half at 0x85F800 reads empty */
    x = 1;
}
''', False),
}

RULE_OF = {'H1': lint32x.rule_H1, 'H2': lint32x.rule_H2,
           'H3': lint32x.rule_H3, 'H4': lint32x.rule_H4, 'H5': lint32x.rule_H5}


def run():
    bad = 0
    for name, (side, src, want) in FIX.items():
        rule = RULE_OF[name.split('-')[0]]
        with tempfile.NamedTemporaryFile('w', suffix='.c', delete=False) as fh:
            fh.write(src); path = fh.name
        recs = [(path, scan(path, {}))]
        got = list(rule(recs if side == 'sh' else [], recs if side == 'md' else []))
        os.unlink(path)
        ok = bool(got) == want
        print(f'  {"PASS" if ok else "FAIL"}  {name:12} '
              f'{"fired" if got else "silent"} (wanted {"fire" if want else "silence"})')
        if not ok:
            bad += 1
            for g in got:
                print(f'        {g}')
    print(f'  {len(FIX) - bad}/{len(FIX)} fixtures pass')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(run())
