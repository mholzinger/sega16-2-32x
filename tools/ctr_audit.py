#!/usr/bin/env python3
"""COUNTER AUDIT — the instrument check, run on the source.

    tools/ctr_audit.py                      # audit the line
    tools/ctr_audit.py --sh-defs "-DA -DB"  # explicit flag sets
    tools/ctr_audit.py --show DIAG          # print the live slot map
    tools/ctr_audit.py --all                # ignore the baseline
    tools/ctr_audit.py --bless               # rewrite the baseline

A BASELINE, NOT A CLEAN SLATE. The slot registry at m_main.c:72 marks
seven live collisions `*** COLLISION ***` today, one of them with the
count "DESTROYED" -- they are real and they are not this tool's to fix.
tools/ctr_baseline.txt records them, so the gate fails on the NEXT one and
the existing set stays visible as INFO. Do not re-bless to silence a
finding you introduced; `--all` shows everything.

Nine instruments have been caught lying in this project. Two whole classes
of it are STATIC PROPERTIES OF THE SOURCE, so they never need to be found
by a contradictory number again:

  COLLISION   two live writers, in different functions, on one slot.
              `DIAG[36]` was the nearest-colour fallback counter AND
              `r60_pkt_flip`, which XORs it every gap; nine more live
              collisions existed, two of them four-way, and on the MDA
              side the overlap was six slots wide. Cost: two debugging
              cycles each, twice. (LESSONS.md "Counter indices collide")

  NO WRITER   a slot that is READ, and documented, and never assigned.
              `mdalloc_id[3]` read 0 for a month because `.bss` is zeroed;
              a hypothesis was "falsified" on 2026-09-17 with it. An
              unwritten index produces a plausible zero that nothing else
              catches. (LESSONS.md "A documented counter index may have
              NO WRITER AT ALL")

  OVERFLOW    an index past the end of its block. DIAG is 64 slots and
              FULL: 0x28000..0x280FF is 256 B and BM starts at 0x28100.
              Slot 64 writes into BM.

  OVERLAP     two fixed SDRAM scratch blocks whose observed extents
              intersect. A block placed in "the 384 B the FBCLEAR comment
              calls spare" read back 0x01010101. Cached (0x06/0x20) and
              uncached (0x26/0x24) spellings of one address are the SAME
              MEMORY and are normalised before comparing.

THE ACCESSOR, NOT THE ARRAY. `grep 'MDA\\['` finds nothing: the macro form
`MDA(19)` is how most of them are written, and that single blind spot is
why the six-slot MDA collision survived a census. Every spelling of each
block is listed in BLOCKS below; add new ones there.

Guards are evaluated against THE LINE's -D set (`make line-defs`), so
"live" means live in the shipping rom, not "present in the file".
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cguard import scan, parse_defs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOW_OVERLAPS = False

# name -> (file, slots or None, [accessor regexes capturing the index])
# A counter block is audited only if it is listed here.
BLOCKS = {
    'DIAG':        ('sh_src/m_main.c', 64,
                    [r'\bDIAG\s*\[\s*(\d+)\s*\]', r'\bdiag_add\s*\(\s*(\d+)\s*,']),
    'mdalloc_ctr': ('sh_src/m_main.c', 48,
                    [r'\bmdalloc_ctr\s*\[\s*(\d+)\s*\]', r'\bMDA\s*\(\s*(\d+)\s*\)',
                     r'\bMDA_ADD\s*\(\s*(\d+)\s*,']),
    'mdalloc_id':  ('sh_src/m_main.c', 24, [r'\bmdalloc_id\s*\[\s*(\d+)\s*\]']),
    'CEN':         ('sh_src/m_main.c', 64, [r'\bCEN\s*\[\s*(\d+)\s*\]']),
    'mds_ctr':     ('sh_src/m_main.c', 6,  [r'\bmds_ctr\s*\[\s*(\d+)\s*\]']),
}

# An OWNING write accumulates: ++, +=, |=, ^=, or `= <something computed>`.
# `DIAG[19] = 0;` in m_boot_init is an INITIALISER and owns nothing -- the
# boot zeroes nine slots in a row and every one of them would otherwise be
# reported as a second owner of a counter it merely clears.
W_OWN  = re.compile(r'\s*(\+\+|--|\+=|-=|\|=|&=|\^=)')
W_INIT = re.compile(r'\s*=(?!=)\s*((?:0[xX][0-9a-fA-F]+|\d+)\s*[;,)]|'
                    r'[A-Z][A-Z0-9_]*\s*\[\s*\d+\s*\]\s*=)')
W_ASSIGN = re.compile(r'\s*=(?!=)')
W_CALL = re.compile(r'\b(diag_add|MDA|MDA_ADD)\s*\(')
# `static volatile uint32_t mds_ctr[6];` is a DECLARATION; its [6] is a
# size, not an index, and reading it as one invented a NO-WRITER finding.
DECL = re.compile(r'^\s*(?:static\s+|extern\s+|const\s+|volatile\s+)*'
                  r'u?int(?:8|16|32)_t\s+\w+\s*\[')

BLOCKDEF = re.compile(
    r'#\s*define\s+(\w+)\s+\(\(\s*(?:const\s+)?(?:volatile\s+)?'
    r'u?int(8|16|32)_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')


def alias(addr):
    """SH-2 area bits: 0x0.. cached, 0x2.. uncached, same DRAM."""
    return addr & 0x0FFFFFFF


def defs_for_line():
    """The line's -D sets, without disturbing .build_flags (the FLAGSTAMP
    $(shell) in the Makefile runs at parse time on every invocation)."""
    stamp = os.path.join(ROOT, '.build_flags')
    save = open(stamp, 'rb').read() if os.path.exists(stamp) else None
    try:
        out = subprocess.run(['make', '--no-print-directory', 'line-defs'],
                             cwd=ROOT, capture_output=True, text=True).stdout
    finally:
        if save is not None:
            open(stamp, 'wb').write(save)
    md = sh = ''
    for ln in out.splitlines():
        if ln.startswith('MD '):
            md = ln[3:]
        elif ln.startswith('SH '):
            sh = ln[3:]
    return sh, md


def audit(sh_defs, md_defs, show=None):
    findings = []
    cache = {}

    def recs_for(path, defs):
        if path not in cache:
            cache[path] = scan(os.path.join(ROOT, path), parse_defs(defs))
        return cache[path]

    # ---- slot census -------------------------------------------------
    for name, (path, slots, pats) in BLOCKS.items():
        defs = md_defs if path.startswith('md_') else sh_defs
        writes, reads, inits = {}, {}, {}
        for r in recs_for(path, defs):
            s = r.text
            if s.lstrip().startswith(('*', '/*', '//')):
                continue                       # registry comments are not sites
            if DECL.match(s):
                continue                       # a declaration's [N] is a size
            for pat in pats:
                for m in re.finditer(pat, s):
                    idx = int(m.group(1))
                    rest = s[m.end():]
                    call = bool(W_CALL.match(s[m.start():])) or bool(
                        re.search(r'\b(diag_add|MDA|MDA_ADD)\s*\(\s*%d\s*[,)]' % idx, s))
                    if call or W_OWN.match(rest):
                        writes.setdefault(idx, []).append(r)
                    elif W_ASSIGN.match(rest):
                        (inits if W_INIT.match(rest) else writes).setdefault(idx, []).append(r)
                    else:
                        reads.setdefault(idx, []).append(r)

        if show == name:
            for i in sorted(set(writes) | set(reads)):
                w = [x for x in writes.get(i, []) if x.live]
                print(f'  [{i:2}] {"LIVE" if w else "probe":5} '
                      f'{len(writes.get(i,[]))}w/{len(reads.get(i,[]))}r  '
                      + ', '.join(sorted({f"{x.func}:{x.line}" for x in (w or writes.get(i,[]))})))

        for i, rs in writes.items():
            if slots is not None and i >= slots:
                findings.append(('ERROR', 'OVERFLOW',
                                 f'{name}[{i}] is past the block ({slots} slots) '
                                 f'at {rs[0].file}:{rs[0].line}'))
            live = [x for x in rs if x.live]
            sites = sorted({(x.func, x.line) for x in live})
            owners = {f for f, _ in sites}
            if len(owners) > 1:
                findings.append(('ERROR', 'COLLISION',
                                 f'{name}[{i}] has {len(owners)} live writers: ' +
                                 ', '.join(f'{f}:{l}' for f, l in sites)))
        for i, rs in reads.items():
            if not any(x.live for x in rs):
                continue
            if not any(x.live for x in writes.get(i, [])):
                r0 = [x for x in rs if x.live][0]
                zeroed = any(x.live for x in inits.get(i, []))
                findings.append(('ERROR', 'NO-WRITER',
                                 f'{name}[{i}] is read live at {r0.func}:{r0.line} and '
                                 + ('only ever zeroed' if zeroed else 'never assigned')
                                 + ' — it reads a constant'))

    # ---- fixed-block overlap ----------------------------------------
    blocks = []
    for path, defs in (('sh_src/m_main.c', sh_defs), ('md_src/md_main.c', md_defs)):
        recs = recs_for(path, defs)
        idx_max = {}
        for r in recs:
            if not r.live:
                continue
            for m in re.finditer(r'\b([A-Z][A-Z0-9_]{1,})\s*\[\s*(\d+)\s*\]', r.text):
                n, i = m.group(1), int(m.group(2))
                idx_max[n] = max(idx_max.get(n, -1), i)
        for r in recs:
            m = BLOCKDEF.match(r.text.strip())
            if not m or not r.live:
                continue
            n, w, a = m.group(1), int(m.group(2)) // 8, int(m.group(3), 16)
            if a < 0x02000000:
                continue                       # not an SH-2 memory block
            span = None
            cm = re.search(r'/\*(.*)', r.text)
            if cm:
                c = cm.group(1)
                mm = re.search(r'\[(\d+)\]\s*\[(\d+)\]', c) or re.search(r'(\d+)\s*(?:words|slots)', c)
                if mm:
                    span = (int(mm.group(1)) * int(mm.group(2))) if mm.lastindex == 2 else int(mm.group(1))
            declared = span is not None
            if span is None:
                span = idx_max.get(n, 0) + 1
            blocks.append((n, alias(a), span * w, r.line, span, declared))
    blocks.sort(key=lambda b: b[1])
    unconfirmed = 0
    for i in range(len(blocks) - 1):
        n1, a1, s1, l1, c1, d1 = blocks[i]
        n2, a2, s2, l2, c2, d2 = blocks[i + 1]
        if n1 == n2 or s1 == 0 or a1 + s1 <= a2:
            continue
        # `TEXT_U` / `TEXT_C` is one block read two ways, by design.
        if re.sub(r'_(U|C)$', '', n1) == re.sub(r'_(U|C)$', '', n2):
            continue
        if not d1:
            # The extent came from the largest index this tool happened to
            # see, which under-counts loops and over-counts strided writes.
            # Guessing here produced eight findings and no bugs; they are
            # counted, not printed. `--overlaps` lists them.
            unconfirmed += 1
            if not SHOW_OVERLAPS:
                continue
        findings.append(('WARN', 'OVERLAP',
                         f'{n1} (0x{a1:07X}+{s1}B, {c1} '
                         f'{"declared" if d1 else "OBSERVED"}) overlaps '
                         f'{n2} (0x{a2:07X}) — m_main.c:{l1} vs :{l2}'))
    if unconfirmed and not SHOW_OVERLAPS:
        findings.append(('INFO', 'OVERLAP',
                         f'{unconfirmed} more block pair(s) overlap on OBSERVED '
                         f'indices only — `tools/ctr_audit.py --overlaps` to list'))
    return findings


BASELINE = os.path.join(ROOT, 'tools', 'ctr_baseline.txt')


def load_baseline():
    if not os.path.exists(BASELINE):
        return set()
    out = set()
    for ln in open(BASELINE):
        ln = ln.split('#')[0].strip()
        if ln:
            out.add(ln)
    return out


def key(kind, msg):
    """Stable across line-number churn: the kind and the slot, not the site."""
    m = re.match(r'(\w+)\[(\d+)\]', msg)
    return f'{kind} {m.group(1)}[{m.group(2)}]' if m else f'{kind} {msg[:40]}'


def main():
    sh = md = None
    show = None
    show_all = bless = False
    a = sys.argv[1:]
    while a:
        k = a.pop(0)
        if k == '--sh-defs':
            sh = a.pop(0)
        elif k == '--md-defs':
            md = a.pop(0)
        elif k == '--show':
            show = a.pop(0)
        elif k == '--all':
            show_all = True
        elif k == '--overlaps':
            globals()['SHOW_OVERLAPS'] = True
        elif k == '--bless':
            bless = True
        elif k in ('-h', '--help'):
            print(__doc__); return 0
    if sh is None or md is None:
        sh, md = defs_for_line()
    if show:
        print(f'{show} live slot map:')
    f = audit(sh, md, show)
    if bless:
        with open(BASELINE, 'w') as fh:
            fh.write('# Known counter findings, baselined %s. A NEW one fails\n'
                     '# `make lint`; these are pre-existing and tracked in the slot\n'
                     '# registry at sh_src/m_main.c:72. Regenerate with --bless only\n'
                     '# after FIXING something, never to silence a new finding.\n'
                     % __import__('datetime').date.today())
            for sev, kind, msg in sorted(f):
                fh.write(f'{key(kind, msg)}\n')
        print(f'  baseline written: {len(f)} findings')
        return 0
    base = set() if show_all else load_baseline()
    new = [x for x in f if key(x[1], x[2]) not in base]
    old = [x for x in f if key(x[1], x[2]) in base]
    errs = [x for x in new if x[0] == 'ERROR']
    for sev, kind, msg in sorted(new):
        print(f'  {sev:5} {kind:10} {msg}')
    if old:
        print(f'  INFO  baselined  {len(old)} known finding(s): '
              + ', '.join(sorted({key(k, m) for _, k, m in old})))
    if not new:
        print('  no new findings')
    print(f'  {len(errs)} new error, {len(new) - len(errs)} new warn')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
