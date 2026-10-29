#!/usr/bin/env python3
"""EVERY TRANSFORMED LINE, PAIRED WITH THE GHIDRA LINE IT CAME FROM.

Mike, 2026-09-27: "inspect every line of c. we dont need a sweeping regex. lets do each
line independently." Two sweeping passes had already over-reached by then -- 377
absolute-short candidates were really 143, and a blanket A68 rewrite hit 372 sites for
the 2 that warranted it. A pattern matching is not evidence that a rewrite is right.

So this prints one reviewable record per transformed site: what Ghidra wrote, what the
port now says, the address the transform derived, and the check that address passes or
fails. Nothing here edits anything -- it exists so a human or a careful pass can reject
sites one at a time.

    tools/cport_review.py                     absolute-short sites (the default)
    tools/cport_review.py --kind w68          16-bit work-RAM pointer sites
    tools/cport_review.py --kind cast         casts inserted by cport_cast.py
    tools/cport_review.py --suspect           ONLY sites that fail a check
"""
import argparse, re, os

ap = argparse.ArgumentParser()
ap.add_argument('--port', default='docs/proof/cport/altbeast_port.c')
ap.add_argument('--corpus', default='docs/proof/altbeast.c')
ap.add_argument('--kind', default='abs', choices=['abs', 'w68', 'cast'])
ap.add_argument('--suspect', action='store_true', help='only sites that fail a check')
ap.add_argument('--limit', type=int, default=0)
a = ap.parse_args()

WRAM_LO, STACK_LO = 0xFFC000, 0xFFFE00
port = open(a.port).read().split('\n')
corpus = open(a.corpus).read()

# the function each port line sits in, so the Ghidra original can be located
fn_of, cur = {}, '?'
for i, l in enumerate(port, 1):
    m = re.match(r'\s*(?:\w+\s+)*?(\w+)\(void\)\s*$', l) or re.match(r'^(\w+)\(void\)$', l)
    if m:
        cur = m.group(1)
    fn_of[i] = cur

# Ghidra's text per function, for the before/after pairing
gh = {}
for blk in re.split(r'(?=/\* ---- 0x)', corpus):
    m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)', blk)
    if m:
        gh[m.group(2)] = blk

PATS = {
    'abs':  re.compile(r'W68PTR\(0x([0-9A-F]{6})u\)'),
    'w68':  re.compile(r'\bW68\('),
    'cast': re.compile(r'=\s*\((byte|ushort|uint|char|short|undefined\d?|'
                       r'byte \*|char \*|uint \*|ushort \*|undefined\d? \*)\)'),
}
pat = PATS[a.kind]

shown = 0
for i, l in enumerate(port, 1):
    m = pat.search(l)
    if not m:
        continue
    notes = []
    if a.kind == 'abs':
        addr = int(m.group(1), 16)
        if not (WRAM_LO <= addr < STACK_LO):
            notes.append('ADDRESS OUTSIDE WORK RAM')
        # the displacement the rewrite came from must reconstruct the address
        disp = 0x10000 - (addr & 0xFFFF)
        notes.append('from -0x%X' % disp)
        # an expression that is a POINTER makes this pointer arithmetic, not
        # absolute-short: the whole distinction (LESSONS 2026-09-27)
        # THE SAME RULE THE TRANSFORM USES, applied independently: a BARE register or
        # pointer term inside the rewritten expression means the address was formed from
        # a POINTER, so this was never absolute-short. A subscripted one
        # (`unaff_A6[8] * 0x10`) is a value being read, which is fine.
        mexp = re.search(r'W68PTR\(0x[0-9A-F]{6}u\)\s*\+\s*\((.*)\)\s*\)', l)
        inner = mexp.group(1) if mexp else ''
        bare = re.compile(r'\b(?:p[a-z]Var\d+|(?:unaff|in|extraout)_A\d\w*'
                          r'|register0x[0-9a-fA-F]+)(?!\s*\[)')
        if any(not inner[m.end():].lstrip().startswith('[')
               and not inner[:m.start()].rstrip().endswith('*')
               for m in bare.finditer(inner)):
            notes.append('*** BARE POINTER TERM IN THE EXPRESSION -- not absolute-short')
    if a.kind == 'w68':
        # W68 translates a 16-bit cell INTO AN ADDRESS. That is only right if the result
        # is USED as an address. If the sign-extended value is used as a NUMBER -- a
        # coordinate, a counter, a compare -- translating it corrupts the value. The
        # honest test is whether the result is dereferenced or assigned to a pointer.
        # THE CONTEXT OUTSIDE THE CALL is what decides, not the text inside it. A
        # first attempt tested the whole line for a pointer cast and passed every site,
        # including `uVar3 = (int)W68(...) + 1` -- because `*(short *)` appears INSIDE
        # the W68 argument at every site, being the read of the 16-bit cell. A check
        # that cannot fail is not a check.
        i0 = l.index('W68(')
        depth, j = 0, i0 + 3
        while j < len(l):                    # find the matching close paren
            if l[j] == '(':
                depth += 1
            elif l[j] == ')':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        before, after = l[:i0], l[j + 1:]
        # an address use: the call sits under a pointer cast, or is indexed/arrowed
        deref = bool(re.search(r'\(\s*\w+\s*\*\s*\)\s*(?:\(\s*\w+\s*\)\s*)*$', before))
        indexed = bool(re.match(r'\s*(?:\[|->)', after))
        # a value use: arithmetic on the result
        arith = bool(re.match(r'\s*\+\s*[A-Za-z_]', after))
        if arith:
            notes.append('*** ADDED TO A VARIABLE -- the cell held a DISPLACEMENT, '
                         'not an absolute address')
        elif not (deref or indexed):
            # FOLLOW THE VARIABLE. "assigned to something" is not a verdict: the
            # question is whether that something is later used as an ADDRESS. Look
            # forward in the same function for a dereference of it.
            mv = re.match(r'\s*(\w+)\s*=\s*$', before.replace('(int)', '').rstrip())
            if mv:
                v = mv.group(1)
                k, used_as_ptr = i, False
                while k < len(port) and not re.match(r'^\}', port[k]):
                    t = port[k]
                    if k != i - 1 and re.search(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(?\s*'
                                                + re.escape(v) + r'\b', t) \
                            or re.search(r'\b' + re.escape(v) + r'\s*(?:\[|->)', t):
                        used_as_ptr = True
                        break
                    k += 1
                notes.append('%s later dereferenced: address use' % v if used_as_ptr
                             else '*** %s NEVER DEREFERENCED -- may be a value' % v)
            else:
                notes.append('?? shape not recognised -- read it')
        else:
            notes.append('under a pointer cast: address use')
    bad = any(n.startswith('***') or n.startswith('ADDRESS') for n in notes)
    if a.suspect and not bad:
        continue
    fn = fn_of.get(i, '?')
    print('%s:%d  [%s]' % (os.path.basename(a.port), i, fn))
    print('   now: %s' % l.strip()[:110])
    if fn in gh:
        # the nearest Ghidra line with the same displacement, for the pairing
        want = ('+ -0x%X' % (0x10000 - (int(m.group(1), 16) & 0xFFFF))) if a.kind == 'abs' else None
        if want:
            cand = [g.strip() for g in gh[fn].split('\n') if want in g]
            if cand:
                print('   was: %s' % cand[0][:110])
    if notes:
        print('   %s' % '; '.join(notes))
    shown += 1
    if a.limit and shown >= a.limit:
        break
print('\n%d site(s) of kind %s%s' % (shown, a.kind, ' FAILING A CHECK' if a.suspect else ''))
