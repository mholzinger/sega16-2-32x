#!/usr/bin/env python3
"""32X LINTER — the rules a generic C linter cannot know.

    tools/lint32x.py                        # audit the line
    tools/lint32x.py --sh-defs "-DA" --md-defs "-DB"
    tools/lint32x.py --rule H4              # one rule
    tools/lint32x.py --list                 # what it checks and why

cppcheck finds 31 style findings in 16.7k lines of m_main.c and not one
of the bug classes that has actually cost this project a session. Those
classes are all properties of THE 32X, and every rule below is one
hardware fact with the document that established it:

  H1 VOLATILE    a hardware address reached through a non-volatile
                 pointer. gcc may hoist, fold or drop the access; the
                 symptom is a register that "was never written".
  H2 ALIAS       one physical address spelled BOTH cached (0x06/0x20) and
                 uncached (0x26/0x24). The two CPUs then disagree about
                 what is in it. m_main.c:710: "attempt 1 died of the two
                 CPUs disagreeing about which bank they were writing, and
                 a stale cache line is the same bug wearing a different
                 hat."
  H3 ONE-SOURCE  an FB-transport address written as a literal instead of
                 taken from md_src/packet_fmt.h. SILICON.md 3,
                 "Addresses -- one source, both sides": the 68K view and
                 the master view of one region must move together.
  H4 FM-WRITE    a 68K framebuffer access on a path that has already
                 raised FM. SILICON.md FACT 2: at FM=1 the SH-2 side owns
                 the framebuffer and 68K writes there DO NOT LAND -- not
                 slow, not partial, absent. Proven on hardware, runA=7
                 vs runB=0. Reported as WARN: this is textual order
                 inside one function, not a flow analysis.
  H5 MIRROR      a *_MD / *_SH constant pair that does not satisfy
                 SH = 0x24000000 + (MD - 0x840000). The 68K and the
                 master must be looking at the same bytes.
  H6 NEVERSHIP   a flag the Makefile documents as "NEVER SHIP" or "probe
                 only" present in the LINE's -D set. PRESSURE is the one
                 CLAUDE.md names; the Makefile marks 40-odd more, and
                 several add per-vint work that shifts the very outcome
                 being measured.

Guards are evaluated against the line's flag set, so a rule fires only on
code that is actually in the shipping rom.
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cguard import scan, parse_defs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SH_FILES = ['sh_src/m_main.c', 'sh_src/s_main.c', 'sh_src/mars.c']
MD_FILES = ['md_src/md_main.c']

# SH-2 area bits. 0x2x = uncached, 0x0x = cached, same DRAM underneath.
def phys(a):
    return a & 0x0FFFFFFF

CAST = re.compile(r'\(\s*((?:const\s+|volatile\s+)*)u?int(?:8|16|32)_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')
PTRDEF = re.compile(r'#\s*define\s+(\w+)\s+\(\(\s*((?:const\s+|volatile\s+)*)'
                    r'u?int(?:8|16|32)_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')
HEX = re.compile(r'0x[0-9A-Fa-f]{5,8}')

# Hardware windows a non-volatile pointer must never reach.
def is_hw(a):
    return (0x24000000 <= a < 0x28000000 or 0x20000000 <= a < 0x20100000
            or 0x00A15000 <= a < 0x00A16000 or 0x00C00000 <= a < 0x00C00010
            or 0x04000000 <= a < 0x04010000)

FB_MD = (0x840000, 0x860000)        # 68K view of the 32X framebuffer
FB_SH = (0x24000000, 0x24020000)    # master view of the same
# FM is bit 15 of 0xA15100. Track it as a RUNNING STATE over the function
# text: the first version of this rule fired on a framebuffer touch 800
# lines after a raise that had been cleared 100 lines earlier.
FM_UP   = re.compile(r'0xA15100\s*(?:\)\s*)?\|=\s*0x8000')
FM_DOWN = re.compile(r'0xA15100\s*(?:\)\s*)?&=\s*0x7FFF', re.I)


def strip_comments(t):
    """Hex inside a comment is prose. A tracer comment naming 0x85F800
    was reported as a framebuffer write by the first cut of H4."""
    t = re.sub(r'/\*.*?\*/', ' ', t)
    t = re.sub(r'//.*$', ' ', t)
    # A block-comment continuation is `*` followed by SPACE. `*(uint16_t*)`
    # and `*mars_comm2` are dereferences at statement start -- the first
    # cut ate both and silently disabled H1, H3 and H4 on most real lines.
    t = re.sub(r'^\s*\*\s.*$', ' ', t)
    return t


def load(files, defs):
    out = []
    for f in files:
        p = os.path.join(ROOT, f)
        if os.path.exists(p):
            out.append((f, scan(p, parse_defs(defs))))
    return out


def rule_H1(sh, md):
    for f, recs in sh + md:
        for r in recs:
            if not r.live or r.text.lstrip().startswith(('*', '//', '/*')):
                continue
            code = strip_comments(r.text)
            for m in list(CAST.finditer(code)) + list(PTRDEF.finditer(code)):
                quals, a = m.group(m.lastindex - 1), int(m.group(m.lastindex), 16)
                if is_hw(a) and 'volatile' not in quals:
                    yield ('ERROR', 'H1', f'{f}:{r.line} non-volatile pointer to '
                           f'hardware 0x{a:08X} in {r.func}() — gcc may drop the access')


def rule_H2(sh, md):
    spell = {}
    for f, recs in sh:
        for r in recs:
            if not r.live:
                continue
            m = PTRDEF.match(r.text.strip())
            if m:
                a = int(m.group(3), 16)
                if a >= 0x02000000:
                    spell.setdefault(phys(a), []).append((m.group(1), a, r.line, f))
    for p, uses in sorted(spell.items()):
        areas = {u[1] >> 28 for u in uses}
        # `TILEMAP_U` / `TILEMAP_C` is a DELIBERATE pair: write through the
        # uncached window, read through the cached one. A pair that says so
        # in its names is a design, not the accident this rule hunts.
        stems = {re.sub(r'_(U|C|UC|CACHED|UNCACHED)$', '', n) for n, _, _, _ in uses}
        if len(areas) > 1 and len(stems) > 1:
            yield ('ERROR', 'H2', f'0x{p:07X} is spelled both cached and uncached: '
                   + ', '.join(f'{n}=0x{a:08X} ({f}:{l})' for n, a, l, f in uses)
                   + ' — one CPU will read a stale line. If the pair is '
                   'deliberate, name them <STEM>_U and <STEM>_C.')


def rule_H3(sh, md):
    known = set()
    pf = os.path.join(ROOT, 'md_src/packet_fmt.h')
    if os.path.exists(pf):
        for ln in open(pf):
            for h in HEX.findall(ln):
                known.add(int(h, 16))
    for f, recs in sh + md:
        if f.endswith('packet_fmt.h'):
            continue
        for r in recs:
            if not r.live or r.text.lstrip().startswith(('*', '//', '/*')):
                continue
            for h in HEX.findall(strip_comments(r.text)):
                a = int(h, 16)
                inband = FB_MD[0] <= a < FB_MD[1] or FB_SH[0] <= a < FB_SH[1]
                if inband and a in known:
                    yield ('WARN', 'H3', f'{f}:{r.line} spells 0x{a:X} as a literal in '
                           f'{r.func}() — packet_fmt.h already names it; the 68K and '
                           f'master views must move together (SILICON.md 3)')


def rule_H4(sh, md):
    for f, recs in md:
        fm, since, func = 0, 0, None
        for r in recs:
            if not r.live:
                continue
            if r.func != func:
                func, fm, since = r.func, 0, r.line
            code = strip_comments(r.text)
            if FM_UP.search(code):
                fm, since = 1, r.line
                continue
            if FM_DOWN.search(code):
                fm = 0
                continue
            if not fm:
                continue
            for h in HEX.findall(code):
                a = int(h, 16)
                if FB_MD[0] <= a < FB_MD[1]:
                    yield ('WARN', 'H4', f'{f}:{r.line} touches the framebuffer '
                           f'(0x{a:X}) in {r.func}(), which raised FM at :{since} and '
                           f'has not cleared it — at FM=1 a 68K write does not land '
                           f'(SILICON.md FACT 2)')


def rule_H5(sh, md):
    pf = os.path.join(ROOT, 'md_src/packet_fmt.h')
    if not os.path.exists(pf):
        return
    vals = {}
    for n, ln in enumerate(open(pf), 1):
        m = re.match(r'\s*#\s*define\s+(\w+?)_(MD|SH)\s+(0x[0-9A-Fa-f]+)', ln)
        if m:
            vals.setdefault(m.group(1), {})[m.group(2)] = (int(m.group(3), 16), n)
    for base, v in sorted(vals.items()):
        if 'MD' in v and 'SH' in v:
            want = 0x24000000 + (v['MD'][0] - 0x840000)
            if v['SH'][0] != want:
                yield ('ERROR', 'H5', f'packet_fmt.h:{v["SH"][1]} {base}_SH is '
                       f'0x{v["SH"][0]:08X}; {base}_MD 0x{v["MD"][0]:06X} mirrors to '
                       f'0x{want:08X} — the two CPUs are not looking at the same bytes')


def rule_H6(sh_defs, md_defs):
    """Flags the Makefile itself marks NEVER SHIP / probe only."""
    mk = open(os.path.join(ROOT, 'Makefile')).read().split('\n')
    banned, comment = {}, []
    for i, ln in enumerate(mk):
        s = ln.strip()
        if s.startswith('#'):
            comment.append(s)
            if len(comment) > 14:
                comment.pop(0)
            continue
        m = re.match(r'ifdef\s+(\w+)', s)
        if m:
            blob = ' '.join(comment).lower()
            if re.search(r'never ship|do not ship|probe only|never be shipped', blob):
                adds = []
                for j in range(i, min(i + 14, len(mk))):
                    if re.match(r'\s*endif', mk[j]):
                        break
                    adds += re.findall(r'-D([A-Z0-9_]+)', mk[j])
                for d in adds:
                    banned[d] = (m.group(1), i + 1)
        if s and not s.startswith('#'):
            comment = []
    live = set(parse_defs(sh_defs)) | set(parse_defs(md_defs))
    for d, (flag, line) in sorted(banned.items()):
        if d in live:
            yield ('ERROR', 'H6', f'-D{d} is on the line, but Makefile:{line} marks '
                   f'{flag} NEVER SHIP / probe only')


RULES = {'H1': rule_H1, 'H2': rule_H2, 'H3': rule_H3, 'H4': rule_H4, 'H5': rule_H5}
BASELINE = os.path.join(ROOT, 'tools', 'lint32x_baseline.txt')


def defs_for_line():
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


def main():
    a, sh_defs, md_defs, only, bless, show_all = sys.argv[1:], None, None, None, False, False
    while a:
        k = a.pop(0)
        if k == '--sh-defs':
            sh_defs = a.pop(0)
        elif k == '--md-defs':
            md_defs = a.pop(0)
        elif k == '--rule':
            only = a.pop(0).upper()
        elif k == '--bless':
            bless = True
        elif k == '--all':
            show_all = True
        elif k == '--list':
            print(__doc__); return 0
        elif k in ('-h', '--help'):
            print(__doc__); return 0
    if sh_defs is None or md_defs is None:
        sh_defs, md_defs = defs_for_line()

    sh, md = load(SH_FILES, sh_defs), load(MD_FILES, md_defs)
    found = []
    for name, fn in RULES.items():
        if only and name != only:
            continue
        found += list(fn(sh, md))
    if not only or only == 'H6':
        found += list(rule_H6(sh_defs, md_defs))

    if bless:
        open(BASELINE, 'w').write(
            '# Pre-existing 32X-rule findings, baselined. A NEW one fails `make lint`.\n'
            '# Regenerate with --bless only after FIXING something.\n'
            + ''.join(f'{k}|{m.split(" — ")[0]}\n' for _, k, m in sorted(found)))
        print(f'  baseline written: {len(found)} findings')
        return 0
    base = set()
    if os.path.exists(BASELINE) and not show_all:
        base = {l.strip() for l in open(BASELINE) if l.strip() and not l.startswith('#')}
    new = [x for x in found if f'{x[1]}|{x[2].split(" — ")[0]}' not in base]
    for sev, kind, msg in sorted(new):
        print(f'  {sev:5} {kind:4} {msg}')
    if len(found) - len(new):
        print(f'  INFO  base {len(found) - len(new)} baselined finding(s)')
    if not new:
        print('  no new findings')
    errs = sum(1 for x in new if x[0] == 'ERROR')
    print(f'  {errs} new error, {len(new) - errs} new warn')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
