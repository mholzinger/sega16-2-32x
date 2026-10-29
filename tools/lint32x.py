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

The next four are the ones that decide whether the rom RUNS ON REAL
HARDWARE at all. Each is a lesson this project paid for; none of them
reproduce under emulation, which is why none of them were caught by a
test:

  H7 RAMCODE     a per-vint SH-2 function linked into the CART WINDOW
                 (0x02xxxxxx) instead of SDRAM (0x06xxxxxx). NOTES.md:181:
                 "While RV=1 the SH-2s must NEVER touch ROM" -- and RV=1
                 is held for the whole game because the arcade binary's
                 self-references need the cart identity-mapped at
                 0x000000 (NOTES.md:162). Measured cost where it is only
                 slow rather than fatal: a 4-6x fetch stall on EVERY
                 instruction (md_main.c ~515; NOTES-FROM-DECOMPILE 57
                 caught flip_span and visr_vbi at 0x0204xxxx this way).
                 The fix is `RAMCODE` on the definition -- and
                 `__attribute__((noinline))` with it, because a static
                 with a section placement still inlines under LTO.
                 Needs rom/s16.lst; skipped if absent.

  H8 FBBYTE      a BYTE-wide access to the framebuffer. TOOLKIT.md:617,
                 "FB byte-write zero-drop -- patch game byte-writers to
                 word writes": a byte write to the FB does not write a
                 byte, it drops. The 68K half of this is why patch_game.py
                 has a byte-writer pass at all.

  H9 FSSPIN      a spin on the FBCTL frame-select latch. TOOLKIT.md:615,
                 "FBCTL flips ONLY in early vblank, gated by the MD
                 V-counter ... never the 32X VBLK bit". The latch is
                 deferred to vblank by the hardware, so a spin waiting for
                 it to change while FM is held stalls the master for the
                 rest of the frame -- measured at 12 points of speed
                 (FLIPEDGEOFF, 2026-09-10) before the write was moved
                 inside vblank.

  H10 VDPLEAK    an arcade hardware address surviving in the PATCHED game
                 body of the built rom. The arcade's I/O lives at
                 0xC4xxxx and "0xC4 low byte lands on MD VDP -- MUST all
                 be patched" (NOTES.md, the small-patch set). One missed
                 operand is the 68K writing game data straight into the
                 Mega Drive VDP on real hardware. Scans rom/s16.32x;
                 skipped if absent.

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
CART_SH = (0x02000000, 0x03000000)  # cart window: forbidden to the SH-2 at RV=1
SDRAM_SH = (0x06000000, 0x07000000)
BYTE_CAST = re.compile(r'\(\s*(?:const\s+|volatile\s+)*u?int8_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)')
FS_SPIN = re.compile(r'\b(?:while|do)\b[^;]*MARS_VDP_FBCTL[^;]*MARS_VDP_FS'
                     r'|\bwhile\s*\([^;]*MARS_VDP_FS')
# Arcade windows that MUST NOT survive patching. The 0xC4 range is the one
# that reaches the Mega Drive VDP; the rest would read or write nothing.
ARCADE_WINDOWS = [('arcade I/O -> MD VDP 0xC4xxxx', 0xC40000, 0xC44000),
                  ('MD VDP ports 0xC000xx',         0xC00000, 0xC00020),
                  ('arcade tile RAM 0x40xxxx',      0x400000, 0x410000),
                  ('arcade text RAM 0x41xxxx',      0x410000, 0x420000),
                  ('arcade sprite RAM 0x44xxxx',    0x440000, 0x450000),
                  ('arcade palette 0x84xxxx',       0x840000, 0x850000)]
GAME_BODY = (0x800, 0x40400)        # native offsets, NOTES.md "Cart layout"
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


def rule_H7(sh, md):
    """Per-vint SH-2 functions that did not land in SDRAM."""
    lst = os.path.join(ROOT, 'rom', 's16.lst')
    if not os.path.exists(lst):
        return
    import code_census
    defs = {'sh': ' '.join(t for _, recs in sh for t in []) or None}
    # reuse the census' own call graph rather than re-deriving reachability
    per_file, funcs, calls = code_census.collect(code_census.defs_for_line())
    hot = code_census.reachable(calls, code_census.VINT_ROOTS)
    sym = {}
    for ln in open(lst):
        p = ln.split()
        if len(p) >= 3 and p[1] in 'tT':
            sym.setdefault(p[2].lstrip('_'), int(p[0], 16))
    bad = sorted((sym[n], n) for n in hot
                 if n in sym and CART_SH[0] <= sym[n] < CART_SH[1])
    # A name that says boot is reachable from m_main but runs once. Keep it
    # visible and out of the count; `--rule H7` prints the whole set.
    BOOTISH = re.compile(r'boot|_init$|^init|reseed|^hs_(stub|patch)$')
    live = [(a, n) for a, n in bad if not BOOTISH.search(n)]
    once = [n for _, n in bad if BOOTISH.search(n)]
    # THE FIX HAS A BUDGET. .ramtext is 0x06031000..0x06038000 (mars.ld:147
    # and its ASSERT) = 28672 B, and the linker already fails the build on
    # overflow. Quoting the shortfall is the difference between a usable
    # finding and "add RAMCODE to eighteen functions".
    used = free = None
    for ln in open(lst):
        p_ = ln.split()
        if len(p_) >= 3 and p_[2] == '__ramtext_size':
            used = int(p_[0], 16); free = 0x7000 - used
    size = {}
    try:
        import subprocess as _sp
        nm = os.environ.get('MARSDEV', os.path.expanduser('~/src/marsdev/mars'))
        nm = os.path.join(nm, 'sh-elf', 'bin', 'sh-elf-nm')
        elf = os.path.join(ROOT, 'rom', 's16.elf')
        if os.path.exists(nm) and os.path.exists(elf):
            for ln in _sp.run([nm, '-S', elf], capture_output=True,
                              text=True).stdout.splitlines():
                p_ = ln.split()
                if len(p_) >= 4:
                    size[p_[3].lstrip('_')] = int(p_[1], 16)
    except Exception:
        pass
    need = sum(size.get(n, 0) for _, n in live)
    budget = ''
    if free is not None:
        budget = (f' BUDGET: moving all {len(live)} costs {need} B and .ramtext has '
                  f'{free} B free of 0x7000 (mars.ld:168 fails the build past it) — '
                  f'this is a RANKED migration, not a sweep. Cheapest first: '
                  + ', '.join(f'{n}({size.get(n, 0)}B)' for _, n in
                              sorted(live, key=lambda x: size.get(x[1], 1 << 30))[:6])
                  + '.')
    if live:
        yield ('ERROR', 'H7', f'{len(live)} per-vint SH-2 function(s) link into the '
               f'CART WINDOW instead of SDRAM. At RV=1 the SH-2 must not fetch from '
               f'the cart (NOTES.md:181); where it merely works it is a 4-6x fetch '
               f'stall on every instruction (md_main.c ~515). Fix: RAMCODE + '
               f'__attribute__((noinline)) on the definition — a static with a '
               f'section placement still inlines under LTO. '
               + ', '.join(f'{n}@0x{a:06X}' for a, n in live) + '.' + budget)
    if once:
        yield ('INFO', 'H7', f'{len(once)} more in the cart window that run at boot, '
               f'not per vint: ' + ', '.join(once))


def rule_H8(sh, md):
    for f, recs in sh + md:
        for r in recs:
            if not r.live:
                continue
            for m in BYTE_CAST.finditer(strip_comments(r.text)):
                a = int(m.group(1), 16)
                if FB_SH[0] <= a < FB_SH[1] or FB_MD[0] <= a < FB_MD[1]:
                    yield ('ERROR', 'H8', f'{f}:{r.line} reaches the framebuffer '
                           f'(0x{a:X}) through a uint8_t pointer in {r.func}() — a '
                           f'byte write to the FB drops (TOOLKIT.md:617). Use words.')


def rule_H9(sh, md):
    for f, recs in sh:
        for r in recs:
            # The stock marsdev library (Hw32x*) runs at boot and its
            # FlipWait exists precisely to wait. The rule is about the
            # per-vint path.
            if not r.live or r.func.startswith('Hw32x'):
                continue
            if FS_SPIN.search(strip_comments(r.text)):
                yield ('WARN', 'H9', f'{f}:{r.line} spins on the FBCTL frame-select '
                       f'latch in {r.func}() — the hardware defers the latch to '
                       f'vblank, so this waits out the frame if FM is held '
                       f'(TOOLKIT.md:615; 12 points, 2026-09-10). Gate the flip on '
                       f'the MD V-counter instead.')


def rule_H10(sh, md):
    """Arcade hardware addresses surviving in the patched game body."""
    import struct
    rom = os.path.join(ROOT, 'rom', 's16.32x')
    if not os.path.exists(rom):
        return
    body = open(rom, 'rb').read()[GAME_BODY[0]:GAME_BODY[1]]
    hits = {}
    for i in range(0, len(body) - 3, 2):
        v = struct.unpack_from('>I', body, i)[0]
        for name, lo, hi in ARCADE_WINDOWS:
            if lo <= v < hi:
                hits.setdefault(name, []).append((GAME_BODY[0] + i, v))
    for name, where in sorted(hits.items()):
        off, v = where[0]
        yield ('WARN', 'H10', f'{len(where)} long(s) in the patched game body point '
               f'into {name} — first 0x{v:06X} at rom offset 0x{off:X}. If any is an '
               f'OPERAND the 68K writes there on real hardware (NOTES.md, the '
               f'small-patch set). Confirm with tools/code_stream.py; data that '
               f'merely looks like an address is a false positive.')


RULES = {'H1': rule_H1, 'H2': rule_H2, 'H3': rule_H3, 'H4': rule_H4, 'H5': rule_H5,
         'H7': rule_H7, 'H8': rule_H8, 'H9': rule_H9, 'H10': rule_H10}
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
