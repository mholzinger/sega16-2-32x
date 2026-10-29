#!/usr/bin/env python3
"""MAKE THE PORT COMPILE CLEAN by inserting the casts gcc asks for.

Mike, 2026-09-27: "we want the compiler clean." The decompiled program puns pointers
and integers constantly -- the 68K stored handler addresses in work-RAM longs -- and
every one of those is a legitimate conversion that C requires to be EXPLICIT.
Suppressing them with -Wno-error hides real bugs among them: the sign fault in W68 and
the type loss in the shadow-drop transform were both sitting under suppressed casts.

So the casts are DERIVED rather than written by hand: run the compiler, read the type
it says it wanted, insert that cast, repeat until the diagnostic is gone. The compiler
is the authority on what conversion is needed, and a loop that ends with a clean build
cannot have guessed. Same shape as cport_build.sh's undeclared-symbol harvest.

    tools/cport_cast.py docs/proof/cport/altbeast_port.c [-I dir] [--max-passes 12]
"""
import argparse, re, subprocess, sys, os

ap = argparse.ArgumentParser()
ap.add_argument('src')
ap.add_argument('-I', dest='inc', action='append', default=[])
ap.add_argument('--max-passes', type=int, default=12)
ap.add_argument('--cc', default=os.path.expanduser(
    '~/src/marsdev/mars/sh-elf/bin/sh-elf-gcc'))
a = ap.parse_args()

CFLAGS = ['-m2', '-ffreestanding', '-c', '-O2', '-fno-strict-aliasing', '-std=gnu99']

# "assignment to 'T' {aka '...'} from 'U' ..." -- T is the type the compiler wanted.
ASSIGN = re.compile(r'^(\S+):(\d+):(\d+): (?:error|warning): assignment to '
                    r'[‘\'"]([^’\'"]+)[’\'"]')

# "returning 'T *' from a function with return type 'uint' ..." -- a function that
# returns an ADDRESS register. cport_prep normalises every signature to
# `uint NAME()` / `ulonglong NAME()`, so a routine whose 68K return lives in A0
# returns a pointer from an integer-typed function. On the SH-2 a pointer and a
# uint are both 32 bits, so the cast is LOSSLESS -- it is not the
# pointer-to-int-of-different-size sin. 7 such sites appeared the moment return
# storage was declared on the 47 value-producing routines.
RETURN = re.compile(r'^(\S+):(\d+):(\d+): (?:error|warning): returning '
                    r'[‘\'"][^’\'"]+[’\'"].*with return type '
                    r'[‘\'"]([^’\'"]+)[’\'"]')

# "passing argument N of 'F' ..." followed by "note: expected 'T' but argument is
# of type 'U'". Needed since the 549 register-parameter declarations
# (tools/ghidra/param_fixpoint.sh): call sites now pass real arguments, and
# Ghidra's types on the two sides of a call do not always agree. The expected type
# comes from gcc, not from a guess, exactly like the assignment rule.
# THE COLUMN IS NOT THE ARGUMENT'S START. For "from incompatible pointer type"
# gcc points at the offending SUB-EXPRESSION -- the `+` in `&DAT_00410000 + iVar1`
# -- so inserting a cast there produces `&DAT_00410000 (undefined *)+ iVar1`.
# Measured 2026-09-30. The callee NAME and the argument INDEX are reliable, so the
# argument is located by parsing the call instead.
ARGPASS = re.compile(r'^(\S+):(\d+):(\d+): (?:error|warning): passing argument '
                     r'(\d+) of [‘\'"]([^’\'"]+)[’\'"]')
EXPECTED = re.compile(r'note: expected [‘\'"]([^’\'"]+)[’\'"]')


def compile_once():
    cmd = [a.cc] + CFLAGS + sum((['-I', i] for i in a.inc), []) + [a.src, '-o', '/dev/null']
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stderr


def counts(err):
    return (sum(1 for l in err.split('\n') if ': error:' in l),
            sum(1 for l in err.split('\n') if ': warning:' in l))


lines = open(a.src).read().split('\n')
for p in range(a.max_passes):
    err = compile_once()
    e, w = counts(err)
    print('pass %d: %d errors, %d warnings' % (p, e, w))
    if e == 0 and w == 0:
        break
    # collect one edit per (line, col); apply from the right so columns stay valid
    edits = {}
    retedits = {}
    argedits = {}
    pend_arg = None
    for l in err.split('\n'):
        m = ASSIGN.match(l)
        if m:
            ln, col, ty = int(m.group(2)), int(m.group(3)), m.group(4)
            if ln - 1 < len(lines):
                edits.setdefault(ln, {})[col] = ty
            continue
        m = RETURN.match(l)
        if m:
            ln, ty = int(m.group(2)), m.group(4)
            if ln - 1 < len(lines):
                retedits[ln] = ty
            continue
        m = ARGPASS.match(l)
        if m:
            pend_arg = (int(m.group(2)), int(m.group(4)), m.group(5))
            continue
        m = EXPECTED.search(l)
        if m and pend_arg is not None:
            ln, idx, fn = pend_arg
            if ln - 1 < len(lines):
                argedits.setdefault(ln, []).append((fn, idx, m.group(1)))
            pend_arg = None
    def split_args(txt, i):
        """[(start, end)] of each top-level argument; i is the index of '('."""
        out, depth, start = [], 0, i + 1
        for j in range(i, len(txt)):
            c = txt[j]
            if c in '([':
                depth += 1
            elif c in ')]':
                depth -= 1
                if depth == 0:
                    out.append((start, j))
                    return out
            elif c == ',' and depth == 1:
                out.append((start, j))
                start = j + 1
        return out

    narg = 0
    for ln in sorted(argedits):
        # A CALL CAN SPAN LINES, and then the closing paren is not on the line gcc
        # named -- the argument is simply not found and the error survives every
        # pass. Take the line plus enough following lines to balance.
        span_lines = 1
        while (ln - 1 + span_lines <= len(lines)
               and ''.join(lines[ln - 1:ln - 1 + span_lines]).count('(')
                   > ''.join(lines[ln - 1:ln - 1 + span_lines]).count(')')
               and span_lines < 8):
            span_lines += 1
        text = '\n'.join(lines[ln - 1:ln - 1 + span_lines])
        # apply right-to-left so earlier spans stay valid
        for fn, idx, ty in sorted(argedits[ln], key=lambda e: -e[1]):
            k = text.find(fn + '(')
            if k < 0:
                continue
            spans = split_args(text, k + len(fn))
            if idx - 1 >= len(spans):
                continue
            # NOT `a` -- that is the argparse namespace, and shadowing it made
            # the write at the end of the pass raise AttributeError
            lo, hi = spans[idx - 1]
            arg = text[lo:hi].strip()
            if arg.startswith('(%s)' % ty):
                continue
            text = text[:lo] + '(%s)(%s)' % (ty, arg) + text[hi:]
            narg += 1
        new_lines = text.split('\n')
        lines[ln - 1:ln - 1 + span_lines] = new_lines
    if narg:
        print('  cast %d call argument(s) to the declared parameter type' % narg)
        open(a.src, 'w').write('\n'.join(lines))
        continue

    if not edits and not retedits:
        print('  no assignment diagnostics left to derive a cast from; stopping')
        break
    nret = 0
    for ln, ty in sorted(retedits.items()):
        text = lines[ln - 1]
        m = re.match(r'^(\s*)return\s+(.*?);\s*$', text)
        if not m:
            continue
        if m.group(2).startswith('(%s)' % ty):
            continue
        lines[ln - 1] = '%sreturn (%s)%s;' % (m.group(1), ty, m.group(2))
        nret += 1
    if nret:
        print('  cast %d return(s) to the function\'s own return type' % nret)
        open(a.src, 'w').write('\n'.join(lines))
        if not edits:
            continue
    n = 0
    for ln in sorted(edits):
        text = lines[ln - 1]
        for col in sorted(edits[ln], reverse=True):
            ty = edits[ln][col]
            # the caret sits on the '='; the RHS begins after it
            i = text.find('=', col - 1)
            if i < 0 or text[i:i + 2] == '==':
                continue
            j = i + 1
            while j < len(text) and text[j] == ' ':
                j += 1
            if text[j:j + len(ty) + 2] == '(%s)' % ty:
                continue                      # already cast; do not stack
            text = text[:j] + '(%s)' % ty + text[j:]
            n += 1
        lines[ln - 1] = text
    print('  inserted %d cast(s)' % n)
    if not n:
        break
    # A symbol's address cast to an INTEGER is the 68K address the symbol was named
    # after, not the SH-2 link address: `FUN_0000f8c0(&DAT_0001011e,&PTR_PTR_LAB_..._00010246)`
    # passes the pair's script as a value the callee stores into +0x24 (prog68k 0xF840
    # `lea 0x10246(pc),a2`), and the derived `(undefined4)(&PTR_PTR_LAB_..._00010246)`
    # stored 0x02350246 (rd106, slot 19's +0x24). 20 sites, 2026-10-04.
    _n = [0]
    def _sym68(m):
        _n[0] += 1
        return '0x%su' % (m.group(1).lstrip('0') or '0')
    lines = [re.sub(r'\(undefined4\)\(&[A-Za-z_]+(?:_[0-9a-f]{8}|_\d+)*_([0-9a-f]{8})\)', _sym68, l) for l in lines]
    print('cport_cast: symbol addresses cast to integers rewritten as 68K values: %d' % _n[0])
    open(a.src, 'w').write('\n'.join(lines))

err = compile_once()
e, w = counts(err)
print('final: %d errors, %d warnings' % (e, w))
sys.exit(0 if e == 0 else 1)
