#!/usr/bin/env python3
"""Regenerate FACTS.md -- the greppable index of everything already established.

FACTS.md is DERIVED. Never edit it by hand; edit LESSONS.md or STATE.md
and re-run this. One line per fact, each with the file:line that carries
the measurement, so a `grep -i` answers "do we already know this?" in one
command instead of a read of 506,000 words.

    python3 tools/build_facts.py            # rewrite FACTS.md
    python3 tools/build_facts.py --check    # exit 1 if it is stale
"""
import re, sys, subprocess

LESSONS, STATE, OUT = 'LESSONS.md', 'STATE.md', 'FACTS.md'
MESSAGES = 'MESSAGES.md'   # the open cards AND their premise checks;
# unindexed until 2026-09-25, which is how this thread contradicted its own
# written caveat on O-13 twice in a row
# The CONFIGURATION sources. These are not history -- they are what the build
# and the patcher are set to RIGHT NOW, which is where the worst re-derivations
# come from: a 2026-09-24 analysis of the WRAMCODE block set duplicated a code
# comment in tools/game_altbeast.py committed 22 minutes earlier.
import glob as _glob
CONFIG_SOURCES = ['Makefile'] + sorted(_glob.glob('games/*/game.py'))

# STATE sections worth indexing, in the order they matter for a premise check
STATE_SECTIONS = [
    ('MEASURED DEAD', 'MEASURED DEAD -- do not re-propose without new evidence'),
    ('SHIPPED',       'SHIPPED -- do not cost these as if they were proposals'),
    ('THE TIERS',     'THE TIERS -- what each machine is truth for'),
    ('THE LINE',      'THE LINE -- the current build and its predecessors'),
    ('OPEN DEFECTS',  'OPEN DEFECTS Mike can see'),
    ('OPEN CARDS',    'OPEN CARDS'),
    ('INSTRUMENT STATUS', 'INSTRUMENT STATUS'),
]


def flatten(s, width=0):
    s = re.sub(r'\s+', ' ', s).strip().rstrip('.')
    if not width or len(s) <= width:
        return s
    return s[:width - 1] + '…'


def lessons_facts():
    """Every ## AND ### heading in LESSONS is a claim; so is every Addendum, where
    a lot of the measured numbers actually live.

    THE ## HEADINGS WERE NOT INDEXED, and 56 of the 67 of them were invisible to
    FACTS.md -- the lookup that exists to stop re-derivation. Measured 2026-09-30
    after re-deriving "A SYMBOL USED AS AN ARRAY BASE INDEXES IN ITS OWN ELEMENT
    SIZE", which LESSONS has carried since 2026-09-27 under the ## heading "Two more
    width rules from the same root" and tools/gen_selftest.py:665 has implemented
    since. `grep -i width FACTS.md` returned nothing relevant, so the grep CLAUDE.md
    prescribes could not have found it.

    This docstring used to say "every ### heading is a claim", which was the
    assumption behind the gap: a lesson whose heading IS the claim and whose body
    carries the rule is usually written as a ## section."""
    lines = open(LESSONS, encoding='utf-8').read().splitlines()
    out, section = [], 'Unsectioned'
    for i, line in enumerate(lines, 1):
        m = re.match(r'^## (?!#)(.*)', line)
        if m:
            section = flatten(m.group(1))
            out.append((section, section, i))      # the heading IS a claim
            continue
        m = re.match(r'^### (.*)', line)
        if m:
            out.append((section, flatten(m.group(1)), i))
            continue
        if re.match(r'^Addendum\b', line):
            body, j = [line], i
            while j < len(lines) and lines[j].strip():
                body.append(lines[j]); j += 1
            out.append((section, flatten(' '.join(body), 400), i))
    return out


def state_facts():
    """STATE's indented term/description blocks: 4-space indent starts an entry."""
    lines = open(STATE, encoding='utf-8').read().splitlines()
    heads = [(i, l[3:].strip()) for i, l in enumerate(lines) if l.startswith('## ')]
    out = {}
    for key, _ in STATE_SECTIONS:
        start = next((i for i, t in heads if t.upper().startswith(key)), None)
        if start is None:
            continue
        end = next((i for i, _ in heads if i > start), len(lines))
        items, cur, curln = [], None, 0
        for i in range(start + 1, end):
            l = lines[i]
            if re.match(r'^ {4}\S', l):
                if cur:
                    items.append((flatten(cur, 400), curln))
                cur, curln = l.strip(), i + 1
            elif re.match(r'^ {5,}\S', l) and cur:
                cur += ' ' + l.strip()
            elif not l.strip():
                continue
            elif cur:
                items.append((flatten(cur, 400), curln)); cur = None
        if cur:
            items.append((flatten(cur, 400), curln))
        out[key] = items
    return out


def config_facts():
    """Makefile flag comments and the per-game patcher TABLES: what is
    configured now, as opposed to what was once measured."""
    out = []
    for path in CONFIG_SOURCES:
        try:
            lines = open(path, encoding='utf-8').read().splitlines()
        except OSError:
            continue
        if path.endswith('Makefile'):
            i = 0
            while i < len(lines):
                if re.match(r'^# [A-Z][A-Z0-9_]*\s*=', lines[i]):
                    body, j = [lines[i][2:]], i + 1
                    while j < len(lines) and lines[j].startswith('# ') and \
                            not re.match(r'^# [A-Z][A-Z0-9_]*\s*=', lines[j]):
                        body.append(lines[j][2:]); j += 1
                    out.append(('build flags', flatten(' '.join(body)),
                                path, i + 1))
                    i = j
                else:
                    i += 1
        else:
            for i, l in enumerate(lines):
                m = re.match(r"^\s*'([A-Z][A-Z0-9_]*)'\s*:", l)
                if not m:
                    continue
                body, j = [l], i + 1
                depth = l.count('[') + l.count('(') - l.count(']') - l.count(')')
                while depth > 0 and j < len(lines) and j - i < 24:
                    body.append(lines[j])
                    depth += (lines[j].count('[') + lines[j].count('(')
                              - lines[j].count(']') - lines[j].count(')'))
                    j += 1
                out.append((path.split('/')[-1], flatten(' '.join(body)),
                            path, i + 1))
    return out


def messages_facts():
    """Every ### card in MESSAGES.md, plus the caveat lines inside it that are
    the whole reason a card is qualified: 'record says', 'instrument', and any
    line shouting in capitals."""
    out, card = [], None
    try:
        lines = open(MESSAGES, encoding='utf-8').read().splitlines()
    except OSError:
        return out
    for i, l in enumerate(lines, 1):
        m = re.match(r'^### (.*)', l)
        if m:
            card = flatten(m.group(1))
            out.append(('cards', card, i))
            continue
        s2 = l.strip()
        key = re.match(r'^(record says|instrument|flags on line)\b', s2)
        shout = re.search(r'\*\*\*|[A-Z]{6,}', s2) and len(s2) > 30
        if card and (key or shout):
            # a caveat is a paragraph, not a line: gather the wrapped remainder
            body, j = [s2], i
            while j < len(lines):
                nxt = lines[j]
                if not nxt.strip() or re.match(r'^#', nxt):
                    break
                if re.match(r'^\s*(record says|instrument|flags on line|TO:|ASK|CLAIM|'
                            r'EVIDENCE|PREMISE)\b', nxt):
                    break
                body.append(nxt.strip()); j += 1
            out.append(('card caveats',
                        f'[{card.split()[0]}] {flatten(" ".join(body), 600)}', i))
    return out


def render():
    L, S, C = lessons_facts(), state_facts(), config_facts()
    M = messages_facts()
    n = len(L) + len(C) + len(M) + sum(len(v) for v in S.values())
    w = []
    w.append('# FACTS — the index of what is already established\n')
    w.append(f'**GENERATED by `tools/build_facts.py` from `LESSONS.md` and `STATE.md`.**')
    w.append('Do not edit by hand — edit the source and regenerate. '
             f'{n} facts.\n')
    w.append('## Use this BEFORE deriving anything\n')
    w.append('```')
    w.append('grep -i <keyword> FACTS.md          # does the record already answer this?')
    w.append('```\n')
    w.append('Each line ends with the `file:line` that carries the measurement. '
             'Read that\nentry before re-deriving its subject.\n')
    w.append('**Two rules this file exists to enforce:**\n')
    w.append('1. **For a question about a FEATURE, read the file that implements it '
             'first.**\n   A block list, a flag, a tool\'s tables — the answer is '
             'usually a code\n   comment, not a derivation. (2026-09-24: a 60-line '
             'analysis of the WRAMCODE\n   block set was committed 22 minutes after '
             'the builder committed the block\n   list itself, comment and all.)\n')
    w.append('2. **Never assert a negative about the record without grepping it in '
             'the same\n   turn.** "Never measured", "not reported", "no instrument '
             'exists" — each of\n   those was wrong at least once, and each cost a '
             'correction round-trip.\n')
    w.append('---\n')
    groups = []
    for g, _, _, _ in C:
        if g not in groups:
            groups.append(g)
    for g in groups:
        w.append(f'## CONFIGURED NOW — {g}\n')
        for gg, text, path, ln in C:
            if gg == g:
                w.append(f'- {text}  `{path}:{ln}`')
        w.append('')
    for key, title in STATE_SECTIONS:
        if not S.get(key):
            continue
        w.append(f'## {title}\n')
        for text, ln in S[key]:
            w.append(f'- {text}  `STATE.md:{ln}`')
        w.append('')
    for g in ('cards', 'card caveats'):
        rows = [(t, ln) for gg, t, ln in M if gg == g]
        if not rows:
            continue
        w.append(f'## OPEN {g.upper()} (MESSAGES.md)\n')
        for t, ln in rows:
            w.append(f'- {t}  `MESSAGES.md:{ln}`')
        w.append('')
    seen = []
    for sec, _, _ in L:
        if sec not in seen:
            seen.append(sec)
    for sec in seen:
        w.append(f'## {sec}\n')
        for s, text, ln in L:
            if s == sec:
                w.append(f'- {text}  `LESSONS.md:{ln}`')
        w.append('')
    return '\n'.join(w) + '\n'


if __name__ == '__main__':
    new = render()
    if '--check' in sys.argv:
        try:
            cur = open(OUT, encoding='utf-8').read()
        except FileNotFoundError:
            cur = ''
        if cur != new:
            print(f'{OUT} is stale; run: python3 tools/build_facts.py', file=sys.stderr)
            sys.exit(1)
        print(f'{OUT} up to date')
    else:
        open(OUT, 'w', encoding='utf-8').write(new)
        print(f'{OUT}: {new.count(chr(10) + "- ")} facts')
