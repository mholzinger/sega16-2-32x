#!/usr/bin/env python3
"""Resolve assembler conditionals on symbols no build defines any more.

    python3 tools/strip_asm.py NAME [NAME ...]            # report only
    python3 tools/strip_asm.py NAME [NAME ...] --apply

The C stripper (tools/strip_dead.py) cannot read the .s files: their conditionals
are the assembler's own (.ifdef NAME / .ifndef NAME / .else / .endif), fed by
--defsym on the command line. Once the Makefile block that passed --defsym NAME is
gone, NAME is never defined: the .ifdef arm is deleted, the .else arm (or the
.ifndef arm) is kept, and the directive lines go.

Every other conditional is left exactly as written. The proof is the rebuild:
tools/romcmp.py must report 0 bytes.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ('md_src', 'sh_src', 'sh_src/r60')
OPEN = re.compile(r'\s*\.if(n?def|n?e|eq|gt|ge|lt|le|n?b|n?c|)\b')
NAMED = re.compile(r'\s*\.if(n?)def\s+(\w+)\s*(?:[!/@|].*)?$')


def strip(lines, off):
    """Drop the arms that an undefined symbol never assembles."""
    out = []
    # one entry per open conditional: None = not ours (copy through),
    # True/False = ours, and whether the current arm is kept
    stack = []
    for l in lines:
        s = l.strip()
        dead = any(k is False for k in stack)
        m = NAMED.match(l)
        if OPEN.match(l):
            if m and m.group(2) in off and not dead:
                stack.append(bool(m.group(1)))      # .ifndef arm is kept, .ifdef arm is not
                continue
            stack.append(None if not dead else 'skip')
        elif s.startswith('.else') and stack:
            if stack[-1] is True or stack[-1] is False:
                stack[-1] = not stack[-1]
                continue
        elif s.startswith('.endif') and stack:
            k = stack.pop()
            if k is True or k is False:
                continue
        if not dead:
            out.append(l)
    if stack:
        raise ValueError('unbalanced conditionals')
    return out


def main():
    args = [a for a in sys.argv[1:] if a != '--apply']
    apply = '--apply' in sys.argv
    if not args:
        sys.exit(__doc__)
    off, total = set(args), 0
    for d in DIRS:
        p = os.path.join(ROOT, d)
        for f in sorted(os.listdir(p)):
            if not f.endswith(('.s', '.S')):
                continue
            path = os.path.join(p, f)
            src = open(path, errors='surrogateescape').read().split('\n')
            new = strip(src, off)
            if len(new) != len(src):
                print('%-28s %5d -> %5d lines' % (os.path.join(d, f), len(src), len(new)))
                total += len(src) - len(new)
                if apply:
                    open(path, 'w', errors='surrogateescape').write('\n'.join(new))
    print('TOTAL %d lines removed%s' % (total, '' if apply else '   [report only; --apply rewrites]'))


if __name__ == '__main__':
    main()
