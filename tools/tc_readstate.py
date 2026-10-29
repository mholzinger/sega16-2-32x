#!/usr/bin/env python3
"""Read the test cartridge's results out of an ares savestate.

    tools/tc_readstate.py rom/night/testcart1.bs1

WHY BY SIGNATURE, not by offset. The suite's block is 0x7C21, tests run, tests
passed, then one byte per test up to TC_MAX=16 -- so a clean pass looks like
7C21 0008 0008 then eight 01s and eight 00s, which is 22 bytes no other data
plausibly imitates. Hunting that is robust; a hardcoded work-RAM offset is not.
An attempt to derive one from this state found three candidate bases that
disagreed, because A5 5A and 12 34 56 78 occur elsewhere by chance.

68K work RAM is stored BYTE-SWAPPED in the container, like SDRAM (SD=0x23B in
the other state tools), so the magic appears as 21 7C.

This works for ares only. Real hardware has no savestate -- there the screen is
the whole channel, which is why the cart holds green on a clean pass and walks
the per-test colours when something fails.
"""
import argparse, sys

NAMES = ['WRAM word', 'WRAM long', 'cart bank long', 'cart bank word',
         'MD VRAM', 'MD CRAM', 'DMA WRAM->VRAM', 'SH-2 alive']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state')
    a = ap.parse_args()
    st = open(a.state, 'rb').read()

    # swapped magic, then run and passed as swapped words
    for i in range(0, len(st) - 24, 1):
        if st[i] == 0x21 and st[i + 1] == 0x7C:
            # BYTE-SWAPPED PAIRS, aligned to the block. A big-endian word
            # whose logical bytes are (hi, lo) is stored (lo, hi), so the
            # value is st[i+2] | st[i+3]<<8 -- NOT the other way round, which
            # is how the first version of this tool found the block and then
            # rejected it for an implausible run count of 2048.
            run = st[i + 2] | (st[i + 3] << 8)
            pas = st[i + 4] | (st[i + 5] << 8)
            if not (1 <= run <= 16 and pas <= run):
                continue
            # and each result byte likewise sits at its pair partner
            res = bytes(st[i + 6 + (k ^ 1)] for k in range(16))
            if any(b > 2 for b in res):
                continue
            print('state %s' % a.state)
            print('  found at 0x%X (byte-swapped magic 21 7C)' % i)
            print('  run %d   passed %d   %s' % (run, pas,
                  'ALL PASS' if pas == run else '*** %d FAILED ***' % (run - pas)))
            for k in range(run):
                nm = NAMES[k] if k < len(NAMES) else 'test %d' % k
                print('    T%-2d %-16s %s' % (k, nm,
                      {0: 'not run', 1: 'PASS', 2: 'FAIL'}.get(res[k], '?')))
            return 0
    print('no test-cartridge result block found in ' + a.state, file=sys.stderr)
    print('(was the rom built with TESTCART=1, and did the state capture it '
          'after the suite ran?)', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
