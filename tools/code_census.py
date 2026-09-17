#!/usr/bin/env python3
"""STRUCTURAL CENSUS — what the source has become, measured.

    tools/code_census.py                 # the report
    tools/code_census.py --dup           # duplicated blocks only
    tools/code_census.py --hot           # per-vint reachable set only
    tools/code_census.py --dead          # code not in the shipping rom
    tools/code_census.py --guards        # which flags own the dead lines

Four questions a pivoting codebase stops being able to answer by reading:

  SIZE   which functions have grown past the point where a change to one
         is a change to all of them.
  DEAD   how much of the file is #ifdef'd OUT on the line. This is the
         cost of eleven pivots, and it is invisible to every other tool:
         the compiler never sees it, git blame says it is recent, and
         grep reports it as live code.
  HOT    which functions are reachable from a PER-VINT entry point. The
         project's whole remaining gap is the per-vint pipeline
         (CLAUDE.md, "The scope"), so this set is where a cycle spent is
         a cycle spent 60 times a second, and everything else is not.
         Static reachability, NOT a profile -- it says what CAN run in a
         vint, not what did. `tools/frame_timeline.py` is the clock.
  DUP    blocks repeated verbatim after normalisation. Three copies of a
         span rule is three places to fix it and two places to forget.

Everything is measured against THE LINE's -D set, so a 400-line function
that is entirely probe code counts as the ~0 lines it compiles to.
"""
import hashlib, os, re, subprocess, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cguard import scan, parse_defs, eval_guard

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = [('sh_src/m_main.c', 'sh'), ('sh_src/s_main.c', 'sh'), ('sh_src/mars.c', 'sh'),
         ('md_src/md_main.c', 'md')]
# Entry points that run once per vertical interrupt.
VINT_ROOTS = {'shim_vblank', 'm_main', 'md_consume', 'r60_push', 's_main'}
BRANCH = re.compile(r'\b(if|for|while|case|&&|\|\||\?)\b|\?')
CALL = re.compile(r'\b([a-z_]\w{2,})\s*\(')
KEYWORD = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'case', 'do',
           'else', 'defined', 'volatile', 'static', 'const', 'int', 'void'}


def defs_for_line():
    stamp = os.path.join(ROOT, '.build_flags')
    save = open(stamp, 'rb').read() if os.path.exists(stamp) else None
    try:
        out = subprocess.run(['make', '--no-print-directory', 'line-defs'],
                             cwd=ROOT, capture_output=True, text=True).stdout
    finally:
        if save is not None:
            open(stamp, 'wb').write(save)
    d = {}
    for ln in out.splitlines():
        if ln[:3] in ('MD ', 'SH '):
            d[ln[:2].lower()] = ln[3:]
    return d


def collect(defs):
    per_file, funcs, calls = {}, {}, collections.defaultdict(set)
    for path, side in FILES:
        p = os.path.join(ROOT, path)
        if not os.path.exists(p):
            continue
        recs = scan(p, parse_defs(defs.get(side, '')))
        live = [r for r in recs if r.live]
        per_file[path] = (len(recs), len(live))
        for r in live:
            t = r.text.strip()
            if not t or t.startswith(('//', '/*', '*', '#')):
                continue
            key = (path, r.func)
            f = funcs.setdefault(key, {'live': 0, 'branch': 0, 'guards': set(), 'line': r.line})
            f['live'] += 1
            f['branch'] += len(BRANCH.findall(t))
            f['guards'].update(r.guards)
            for m in CALL.finditer(t):
                n = m.group(1)
                if n not in KEYWORD:
                    calls[r.func].add(n)
        # dead lines carry guards even though they are not live
        for r in recs:
            if not r.live and r.guards:
                funcs.setdefault((path, r.func), {'live': 0, 'branch': 0,
                                                  'guards': set(), 'line': r.line})
    return per_file, funcs, calls


def reachable(calls, roots):
    seen, stack = set(), list(roots)
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        stack += list(calls.get(n, ()))
    return seen


def dups(defs, win=8, minhits=2):
    """Normalised sliding-window hashes over LIVE lines only."""
    seen = collections.defaultdict(list)
    for path, side in FILES:
        p = os.path.join(ROOT, path)
        if not os.path.exists(p):
            continue
        recs = [r for r in scan(p, parse_defs(defs.get(side, '')))
                if r.live and r.text.strip()
                and not r.text.strip().startswith(('//', '/*', '*', '#', '}', '{'))]
        norm = [(r, re.sub(r'\s+', ' ', re.sub(r'\b\d+\b', 'N', r.text)).strip())
                for r in recs]
        for i in range(len(norm) - win):
            blob = '\n'.join(t for _, t in norm[i:i + win])
            if len(set(t for _, t in norm[i:i + win])) < win - 1:
                continue                      # a run of identical lines is a table
            h = hashlib.md5(blob.encode()).hexdigest()[:12]
            seen[h].append((path, norm[i][0].line, norm[i][0].func))
    out = []
    for h, hits in seen.items():
        far = [hits[0]]
        for x in hits[1:]:
            if all(abs(x[1] - y[1]) > win or x[0] != y[0] for y in far):
                far.append(x)
        if len(far) >= minhits:
            out.append(far)
    out.sort(key=lambda x: -len(x))
    return out


def main():
    a = sys.argv[1:]
    only = a[0].lstrip('-') if a else None
    defs = defs_for_line()
    per_file, funcs, calls = collect(defs)
    hot = reachable(calls, VINT_ROOTS)

    if only in (None, 'dead'):
        print('\nDEAD ON THE LINE — #ifdef\'d out of the shipping rom')
        tot = totlive = 0
        for path, (n, live) in sorted(per_file.items()):
            tot += n; totlive += live
            print(f'  {path:22} {n:6} lines  {live:6} live  '
                  f'{100 * (n - live) // max(1, n):3}% dead')
        print(f'  {"TOTAL":22} {tot:6} lines  {totlive:6} live  '
              f'{100 * (tot - totlive) // max(1, tot):3}% dead')

    if only in (None, 'size', 'hot'):
        print('\nBIGGEST LIVE FUNCTIONS  (H = reachable from a per-vint entry)')
        rows = sorted(funcs.items(), key=lambda kv: -kv[1]['live'])[:12]
        for (path, fn), d in rows:
            if d['live'] < 20:
                continue
            print(f'  {"H" if fn in hot else " "} {d["live"]:5} live  '
                  f'{d["branch"]:4} branches  {len(d["guards"]):3} guards  '
                  f'{os.path.basename(path)}:{d["line"]:<6} {fn}')

    if only in (None, 'hot'):
        hotf = [(k, v) for k, v in funcs.items() if k[1] in hot and v['live'] >= 20]
        print(f'\nPER-VINT REACHABLE: {len(hot)} functions, '
              f'{sum(v["live"] for _, v in hotf)} live lines in the ones over 20')

    if only in (None, 'guards'):
        # The guard that KILLS the line, not the outermost one. The first
        # cut printed `defined(MD_BG)` at the top of the list -- MD_BG is
        # ON the line, and it was the innermost guard doing the killing.
        print('\nWHO OWNS THE DEAD LINES — the guard that evaluates FALSE')
        own = collections.Counter()
        for path, side in FILES:
            p = os.path.join(ROOT, path)
            if not os.path.exists(p):
                continue
            d = parse_defs(defs.get(side, ''))
            for r in scan(p, d):
                if r.live or not r.guards:
                    continue
                killer = next((g for g in r.guards if eval_guard(g, d) is False),
                              r.guards[-1])
                own[re.sub(r'\s+', ' ', killer)[:56]] += 1
        for g, n in own.most_common(15):
            print(f'  {n:6}  {g}')
        print('  A guard owning hundreds of dead lines that is not in '
              'LINE_FLAGS and not\n  named as a live card in STATE.md is '
              'dead weight with a name.')

    if only in (None, 'dup'):
        d = dups(defs)
        print(f'\nDUPLICATED BLOCKS (8 live lines, literals normalised): {len(d)} sites')
        for hits in d[:8]:
            where = ', '.join(f'{os.path.basename(p)}:{l} ({f})' for p, l, f in hits[:4])
            print(f'  x{len(hits)}  {where}')
    print()


if __name__ == '__main__':
    main()
