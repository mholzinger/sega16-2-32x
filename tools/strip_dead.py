#!/usr/bin/env python3
"""Delete the C that the shipping build compiles out.

    python3 tools/strip_dead.py build/port/rom/NAME.flags            # report only
    python3 tools/strip_dead.py build/port/rom/NAME.flags --apply    # rewrite the sources

NAME.flags is the compiler command line of a built rom (68000 flags, then
SH-2 flags). Every -D on it is ON. Every other -D the Makefile can add is a
build option; an option is switched OFF here unless something still uses it:

  * a script under tools/ or tests/ passes it (VAR=...), or
  * a Makefile recipe or a *_FLAGS list names it, or
  * it matches KEEP below (the optional builds that stay options).

unifdef then resolves each #if on the ON and OFF names and leaves every other
condition alone. The proof that nothing live was touched is the rebuild:
tools/romcmp.py against the rom the flags came from must report 0 bytes.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Optional builds that stay options: the hardware test cart, the rig probes,
# the port's self test, the launcher, and the tape/diagnostic builds the gates run.
# Switched off by decision (2026-11-15) although a script still names them: the
# script is a retired probe, or the option only served the retired `debug` target.
FORCE_OFF = set('''FB_SPR_READ MD_HSCROLL_DIRECT MD_VERIFY PAL_STORM R60_OPMLANE R60_ROWDIFFCHECK R60_ZOOMWORDS TAIL_CENSUS TXT_WRAM WRAM_CODE'''.split())

KEEP = re.compile(r'^(TEST_CART|TC_|RIG_|PORT_TEST|PORT_SELFTEST|LAUNCH|GD_|PORT_TAPE|PORT_DIAG|PORT_HOT|R60_DIAG)')

# Sources the rom is built from. Generated C (portfull/) and the separate
# programs (introlab/, sndtest/) are not touched.
SOURCES = {
    'md': ['md_src'],
    'sh': ['sh_src', 'sh_src/r60'],
}
EXT = ('.c', '.h', '.inc', '.s', '.S')


def defines(cmd):
    out = {}
    for m in re.finditer(r'-D([A-Za-z_][A-Za-z0-9_]*)(?:=(\S+))?', cmd):
        out[m.group(1)] = m.group(2)
    return out


def split_flags(path):
    """The .flags line is the 68000 command followed by the SH-2 command."""
    text = open(path).read()
    i = text.find(' -m2 ')
    if not text.lstrip().startswith('-m68000') or i < 0:
        sys.exit('strip_dead: %s is not "<68000 flags> <SH-2 flags>"' % path)
    return defines(text[:i]), defines(text[i:])


def makefile_options(mk):
    """{define: set(make variables that add it)}, from the ifdef/ifeq nesting."""
    opts, stack = {}, []
    for line in mk.split('\n'):
        s = line.strip()
        m = re.match(r'(ifdef|ifndef)\s+(\w+)', s) or re.match(r'(ifeq|ifneq)\s*\(\s*\$\((\w+)\)', s)
        if m:
            stack.append(m.group(2))
        elif re.match(r'(ifeq|ifneq|ifdef|ifndef)\b', s):
            stack.append(None)
        elif s.startswith('endif'):
            if stack:
                stack.pop()
        if not line.startswith('\t'):
            for d in re.findall(r'-D([A-Za-z_][A-Za-z0-9_]*)', line):
                # the innermost condition is the option; outer ones are the block it lives in
                opts.setdefault(d, set()).update(v for v in stack[-1:] if v)
    return opts


def used_vars(mk):
    """Make variables that a script, a recipe or a *_FLAGS list sets."""
    used = set()
    for line in mk.split('\n'):
        if line.startswith('\t') or re.match(r'\w*_FLAGS\s*[:+?]?=', line) or re.match(r'\w+_(EXTRA|DEFS)\s*[:+?]?=', line):
            used.update(re.findall(r'\b([A-Z][A-Z0-9_]+)=', line))
    for top in ('tools', 'tests'):
        for d, _, files in os.walk(os.path.join(ROOT, top)):
            for f in files:
                if f.endswith(('.sh', '.py', '.mk', '.md', '.cmd')):
                    try:
                        used.update(re.findall(r'\b([A-Z][A-Z0-9_]+)=', open(os.path.join(d, f), errors='replace').read()))
                    except OSError:
                        pass
    return used


def files_for(side):
    out = []
    for d in SOURCES[side]:
        p = os.path.join(ROOT, d)
        out += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(EXT))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('flags')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--list', action='store_true', help='print every option switched off')
    a = ap.parse_args()

    on = dict(zip(('md', 'sh'), split_flags(a.flags)))
    from mktext import makefile_text
    mk = makefile_text(ROOT)
    opts, used = makefile_options(mk), used_vars(mk)
    every_on = set(on['md']) | set(on['sh'])

    off, kept = set(), set()
    for d, vs in opts.items():
        if d in every_on:
            continue
        if d in FORCE_OFF:
            off.add(d)
        elif KEEP.match(d) or (vs & used) or not vs:
            kept.add(d)
        else:
            off.add(d)
    if a.list:
        print('OFF (%d): %s' % (len(off), ' '.join(sorted(off))))
        print('KEPT AS OPTIONS (%d): %s' % (len(kept), ' '.join(sorted(kept))))

    total_before = total_after = 0
    for side in ('md', 'sh'):
        # A file under md_src is also read by the SH-2 build (shared headers),
        # so a name is only resolved where both builds agree on it.
        other = on['sh' if side == 'md' else 'md']
        args = []
        for d, v in sorted(on[side].items()):
            if KEEP.match(d):
                continue
            shared_ok = side == 'sh' or d in other
            if shared_ok:
                args.append('-D%s%s' % (d, '=' + v if v else ''))
        args += ['-U' + d for d in sorted(off)]
        for f in files_for(side):
            src = open(f, errors='surrogateescape').read()
            r = subprocess.run(['unifdef', '-B'] + args, input=src, capture_output=True, text=True, errors='surrogateescape')
            if r.returncode == 2 and f.endswith(('.s', '.S')):
                # assembler comments hold apostrophes unifdef reads as char literals
                print('%-34s left alone (unifdef cannot parse it)' % os.path.relpath(f, ROOT))
                continue
            if r.returncode == 2:
                sys.exit('strip_dead: unifdef failed on %s: %s' % (f, r.stderr.strip()))
            b, n = src.count('\n'), r.stdout.count('\n')
            total_before += b
            total_after += n
            if b != n:
                print('%-34s %6d -> %6d lines' % (os.path.relpath(f, ROOT), b, n))
                if a.apply:
                    open(f, 'w', errors='surrogateescape').write(r.stdout)
    print('TOTAL %d -> %d lines (%d removed, %.0f%%)%s' % (
        total_before, total_after, total_before - total_after,
        100.0 * (total_before - total_after) / max(1, total_before),
        '' if a.apply else '   [report only; --apply rewrites]'))
    print('options: %d on, %d switched off, %d kept as options' % (len(every_on), len(off), len(kept)))


if __name__ == '__main__':
    main()
