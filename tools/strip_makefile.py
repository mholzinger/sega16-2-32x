#!/usr/bin/env python3
"""Remove from the Makefile the build options tools/strip_dead.py switched off.

    python3 tools/strip_makefile.py build/port/rom/NAME.flags            # report only
    python3 tools/strip_makefile.py build/port/rom/NAME.flags --apply

After strip_dead.py an off option still has its block:

    # `R60MBAND=1` = ...what it measured, and when...
    ifdef R60MBAND
    SHCCFLAGS += -DR60_MBAND
    endif

The define reaches no code, so the block and its comment paragraph go. They are
not thrown away: each is appended, verbatim, to docs/history/retired-flags.Makefile,
which tools/build_facts.py reads the same way it reads the Makefile, so the
measurements recorded in those comments stay one grep away in FACTS.md.

A block is removed only when it does nothing except add off defines: no else
branch, no rule, no object list, no other assignment. Anything else is reported
and left for a person.

The proof is the rebuild: NAME.flags of the new build must equal the old one and
tools/romcmp.py must report 0 bytes.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strip_dead as S

ARCHIVE = 'docs/history/retired-flags.Makefile'
OPEN = re.compile(r'\s*(ifdef|ifndef|ifeq|ifneq)\b')
VAR = re.compile(r'\s*(?:ifdef\s+(\w+)|ifeq\s*\(\s*\$\((\w+)\)\s*,)')
# what a pure option block may contain besides nested option blocks
ADDS = re.compile(r'\s*(SHCCFLAGS|MDCCFLAGS|CCFLAGS|SHASFLAGS|MDASFLAGS|ASFLAGS|\w*DEFS)\s*\+=')
NOISE = re.compile(r'\s*(#.*)?$|\s*\$\((error|warning|info)\b')


def blocks(lines):
    """[(start, end, var)] for every conditional block, at every depth."""
    out, stack = [], []
    for i, l in enumerate(lines):
        if l.startswith('\t'):
            continue
        if OPEN.match(l):
            stack.append(i)
        elif l.strip().startswith('endif') and stack:
            start = stack.pop()
            m = VAR.match(lines[start])
            out.append((start, i, (m.group(1) or m.group(2)) if m else None))
    return sorted(out)


def pure(body, off):
    """True when the block only adds defines and every one of them is off."""
    saw = False
    for l in body:
        if l.startswith('\t') or l.strip().startswith('else'):
            return False
        if OPEN.match(l) or l.strip().startswith('endif') or NOISE.match(l):
            continue
        if not ADDS.match(l):
            return False
        ds = re.findall(r'-D([A-Za-z_][A-Za-z0-9_]*)', l)
        if not ds or any(d not in off for d in ds) or re.search(r'(?<!-D)\$\(', l.split('+=', 1)[1].replace('-D', '')):
            # an append that is not a define (an include path, another variable) stays
            if not ds or any(d not in off for d in ds):
                return False
        saw = True
    return saw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('flags')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()

    on = set().union(*map(set, S.split_flags(a.flags)))
    path = os.path.join(S.ROOT, 'Makefile')
    mk = open(path).read()
    opts, used = S.makefile_options(mk), S.used_vars(mk)
    off = set()
    for d, vs in opts.items():
        if d in on:
            continue
        if d in S.FORCE_OFF or not (S.KEEP.match(d) or (vs & used) or not vs):
            off.add(d)

    lines = mk.split('\n')
    kill, gone_vars, left = set(), set(), []
    all_vars = {v for _, _, v in blocks(lines) if v}
    for s, e, var in blocks(lines):
        body = lines[s + 1:e]
        has_off = any(d in off for l in body for d in re.findall(r'-D([A-Za-z_]\w*)', l))
        if var and pure(body, off):
            kill.update(range(s, e + 1))
            gone_vars.add(var)
        elif has_off:
            left.append((s + 1, var))

    # gone_vars: every variable that lost a define block. A variable may keep another
    # block (a rule, an object list); the paragraph above the removed block described
    # the define, so it goes with it either way.

    # the comment paragraph(s) directly above a removed block: a paragraph starts at a
    # line of the form  # `VAR=...  and belongs to the variables its first line names
    for s, e, var in blocks(lines):
        if s not in kill:
            continue
        j = s - 1
        run = []
        while j >= 0 and lines[j].startswith('#'):
            run.append(j)
            j -= 1
        run.reverse()
        # The run directly above a removed block describes it. Split it into paragraphs
        # (a paragraph starts at  # `VAR=..  or  # VAR=1 = ..  or  # VAR=n: ..); a paragraph
        # stays only when its first line names nothing but variables that still have a
        # block, i.e. it documents something that survives. Everything else is archived.
        HEAD = re.compile(r'#\s*`|#\s*[A-Z][A-Z0-9_]{2,}=\w*\s*[:=]')
        paras, cur = [], []
        for i in run:
            if HEAD.match(lines[i]) and cur:
                paras.append(cur)
                cur = []
            cur.append(i)
        if cur:
            paras.append(cur)
        for p in paras:
            named = set(re.findall(r'\b([A-Z][A-Z0-9_]{2,})=', lines[p[0]])) if HEAD.match(lines[p[0]]) else set()
            if named and not (named & gone_vars) and (named & all_vars):
                continue
            kill.update(p)

    removed = [lines[i] for i in sorted(kill)]
    kept = [l for i, l in enumerate(lines) if i not in kill]
    # collapse the blank runs the removal leaves behind
    out = []
    for l in kept:
        if l.strip() == '' and out and out[-1].strip() == '':
            continue
        out.append(l)

    print('Makefile %d -> %d lines; %d option blocks removed (%d variables), %d comment lines archived' % (
        len(lines), len(out), sum(1 for s, e, v in blocks(lines) if s in kill), len(gone_vars),
        sum(1 for l in removed if l.startswith('#'))))
    if left:
        print('%d blocks hold an off define but do more than add defines; left alone:' % len(left))
        print('  ' + ' '.join('%s@%d' % (v, n) for n, v in left[:40]))
    if a.apply:
        arch = os.path.join(S.ROOT, ARCHIVE)
        os.makedirs(os.path.dirname(arch), exist_ok=True)
        head = '' if os.path.exists(arch) else (
            '# Build options removed from the Makefile, verbatim, oldest removal first.\n'
            '# Their code is gone (tools/strip_dead.py); the comments are the record of what\n'
            '# each one measured. Read by tools/build_facts.py. Nothing includes this file.\n')
        open(arch, 'a').write(head + '\n'.join(removed) + '\n')
        open(path, 'w').write('\n'.join(out))
    else:
        print('[report only; --apply rewrites the Makefile and appends to %s]' % ARCHIVE)


if __name__ == '__main__':
    main()
