#!/usr/bin/env python3
"""Preprocessor-aware line scanner — the shared front end for the linters.

Every static check in this repo is worthless without one, because
`sh_src/m_main.c` carries 866 `#if` blocks over 16.7k lines and 328
distinct flags exist across the two CPUs. A plain grep reports sites that
do not compile on the line and misses the ones that do; the DIAG slot
registry (m_main.c:72) was built by hand from exactly this kind of pass
after two index collisions cost two debugging cycles each
(LESSONS.md "Counter indices collide").

    from cguard import scan
    for r in scan("sh_src/m_main.c", defines):
        r.line, r.text, r.live, r.guards, r.func

`live` means "this line compiles in the given -D set". `guards` is the
full #if stack, so a site can be attributed to the flag that owns it.

Unknown identifiers evaluate to 0, which is what the C preprocessor does.
An expression this evaluator cannot parse makes the branch UNKNOWN: it is
reported live AND recorded in `.unparsed`, so a checker can say so rather
than quietly assuming.
"""
import re, sys
from dataclasses import dataclass, field

_DEFINED = re.compile(r'defined\s*(?:\(\s*(\w+)\s*\)|(\w+))')
_IDENT   = re.compile(r'(?<![\w.])([A-Za-z_]\w*)')
_NUM     = re.compile(r'\b0[xX][0-9a-fA-F]+|\b\d+')
# The last identifier before the '(' of a signature that opened a body.
# Found by watching brace depth 0 -> 1 rather than by matching a line:
# `RAMCODE static void f(void)` and a signature split over three lines
# both have to attribute correctly, and the return types here include
# RAMCODE, volatile and pointer forms.
_SIGNAME = re.compile(r'([A-Za-z_]\w*)\s*\([^()]*\)\s*$')


@dataclass
class Rec:
    line: int
    text: str
    live: bool
    guards: tuple
    func: str
    file: str


def _py(expr, defines):
    """Translate a #if expression into Python. Raises on anything odd."""
    e = expr.strip()
    e = re.sub(r'/\*.*?\*/', ' ', e)
    e = e.split('//')[0]
    # defined(X) / defined X  ->  True/False
    e = _DEFINED.sub(lambda m: 'True' if (m.group(1) or m.group(2)) in defines else 'False', e)
    # numeric literals: 0x.. stays valid Python; strip integer suffixes
    e = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', e)
    # bare identifiers -> their -D value, else 0 (C preprocessor rule)
    def ident(m):
        n = m.group(1)
        if n in ('True', 'False', 'and', 'or', 'not'):
            return n
        v = defines.get(n, 0)
        return str(v) if isinstance(v, int) else (v if re.fullmatch(r'-?\w+', str(v)) else '0')
    e = _IDENT.sub(ident, e)
    e = e.replace('&&', ' and ').replace('||', ' or ')
    e = re.sub(r'!(?=[\s(\w])', ' not ', e)
    if not re.fullmatch(r'[\s\w()<>=!+\-*/%&|^.]*', e):
        raise ValueError(expr)
    return e


def eval_guard(expr, defines):
    """True / False / None (unparsable)."""
    try:
        return bool(eval(_py(expr, defines), {'__builtins__': {}}, {}))
    except Exception:
        return None


def parse_defs(argstr):
    """'-DFOO -DBAR=3' -> {'FOO': 1, 'BAR': 3}"""
    out = {}
    for tok in str(argstr).split():
        if not tok.startswith('-D'):
            continue
        k, _, v = tok[2:].partition('=')
        if not v:
            out[k] = 1
        else:
            try:
                out[k] = int(v, 0)
            except ValueError:
                out[k] = v
    return out


def scan(path, defines, follow_defines=True):
    """Yield a Rec per line. `follow_defines` lets in-file #define/#undef
    inside LIVE blocks join the flag set, which matters here: half the
    feature switches in m_main.c are `#define X` under another flag."""
    defines = dict(defines)
    stack = []          # list of [taken_here, any_taken, expr]
    func = '<file>'
    depth = 0
    sig = []
    unparsed = []
    out = []
    with open(path, errors='replace') as fh:
        lines = fh.read().split('\n')
    # join preprocessor line continuations; keep the ORIGINAL line number
    joined, i = [], 0
    while i < len(lines):
        t, n0 = lines[i], i + 1
        while t.rstrip().endswith('\\') and i + 1 < len(lines):
            t = t.rstrip()[:-1] + ' ' + lines[i + 1]
            i += 1
        joined.append((n0, t))
        i += 1
    if True:
        for n, raw in joined:
            t = raw.rstrip('\n')
            s = t.strip()
            live = all(f[0] for f in stack)
            if s.startswith('#'):
                d = s[1:].strip()
                kw = d.split(None, 1)[0] if d else ''
                arg = d[len(kw):].strip()
                if kw in ('if', 'ifdef', 'ifndef'):
                    expr = {'ifdef': f'defined({arg})',
                            'ifndef': f'!defined({arg})'}.get(kw, arg)
                    v = eval_guard(expr, defines) if live else False
                    if v is None:
                        unparsed.append((n, expr)); v = True
                    stack.append([bool(v), bool(v), expr])
                elif kw == 'elif' and stack:
                    top = stack[-1]
                    outer = all(f[0] for f in stack[:-1])
                    v = (eval_guard(arg, defines) if outer and not top[1] else False)
                    if v is None:
                        unparsed.append((n, arg)); v = not top[1]
                    top[0] = bool(v) and not top[1]
                    top[1] = top[1] or top[0]
                    top[2] = arg
                elif kw == 'else' and stack:
                    top = stack[-1]
                    outer = all(f[0] for f in stack[:-1])
                    top[0] = outer and not top[1]
                    top[1] = True
                    top[2] = '!(' + top[2] + ')'
                elif kw == 'endif' and stack:
                    stack.pop()
                elif follow_defines and live and kw == 'define':
                    parts = arg.split(None, 1) if arg else []
                    k = parts[0].split('(')[0] if parts else ''
                    v = parts[1] if len(parts) > 1 else ''
                    if k:
                        try:
                            defines[k] = int(v.strip(), 0)
                        except Exception:
                            defines[k] = v.strip() or 1
                elif follow_defines and live and kw == 'undef':
                    defines.pop(arg.split()[0] if arg else '', None)
                # A directive line is still emitted. `#define BLK ((volatile
                # uint16_t*)0x2603A780)` IS the declaration of a fixed SDRAM
                # block, and dropping these silently disabled the cached/
                # uncached alias rule and the block-overlap check.
                out.append(Rec(n, t, live and kw not in ('if', 'ifdef', 'ifndef',
                                                         'elif', 'else', 'endif'),
                               tuple(f[2] for f in stack), func, path))
                continue
            # Count braces on LIVE lines only. A dead branch's braces are
            # not in the translation unit, and `#ifdef X { #else { #endif`
            # (common here) unbalances any counter that reads both.
            if not all(f[0] for f in stack):
                out.append(Rec(n, t, False, tuple(f[2] for f in stack), func, path))
                continue
            code = re.sub(r'"(\\.|[^"\\])*"', '""', t)
            code = re.sub(r"'(\\.|[^'\\])*'", "''", code)
            code = re.sub(r'/\*.*?\*/', ' ', code)
            if depth == 0:
                bare = code.split('//')[0].rstrip()
                if bare.strip():
                    sig.append(bare)
                    if len(sig) > 6:
                        sig.pop(0)
            op, cl = code.count('{'), code.count('}')
            if depth == 0 and op:
                blob = ' '.join(sig)
                blob = blob[:blob.rindex('{')] if '{' in blob else blob
                m = _SIGNAME.search(blob.strip())
                func = m.group(1) if m else (func if cl >= op else '<file>')
                sig = []
            depth += op - cl
            if depth < 0:
                depth = 0
            if depth == 0 and cl:
                sig = []
            out.append(Rec(n, t, all(f[0] for f in stack), tuple(f[2] for f in stack), func, path))
    scan.unparsed = unparsed
    scan.defines = defines
    return out


if __name__ == '__main__':
    defs = parse_defs(' '.join(sys.argv[2:]))
    recs = scan(sys.argv[1], defs)
    live = sum(1 for r in recs if r.live)
    print(f'{sys.argv[1]}: {len(recs)} code lines, {live} live '
          f'({100*live//max(1,len(recs))}%), {len(scan.unparsed)} unparsable guards')
    for n, e in scan.unparsed[:10]:
        print(f'  unparsed {sys.argv[1]}:{n}: {e}')
