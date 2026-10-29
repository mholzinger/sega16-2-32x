#!/usr/bin/env python3
"""BUILD-TIME GUARD for the three mistakes that keep costing days.

    python3 tools/port_guard.py          # exits 1 on any violation

WHY THIS IS CODE AND NOT A DOCUMENT. The rule "a symbol used as an array base
indexes in its own element size" was written in LESSONS on 2026-09-27 AND
implemented in tools/gen_selftest.py, and on 2026-10-01 it was re-derived from
scratch anyway -- because it lived in a file nobody was looking at. A written rule
relies on someone remembering it at the moment of writing code. A failing build does
not. Every check here exists because its absence cost at least one day.

  1. RESULT-SLOT OVERLAP. port_l2_out / port_l2m_out / port_watch_out are shared
     arrays with hand-maintained maps in comments. Six slot collisions so far; the
     last one read back 4312, a plausible wrong number that was a slot FIELD value.
  2. WIDTH DISAGREEMENT. An A6-relative access whose width contradicts the arcade's
     OBSERVED width at that offset. DAT_fffff142 (ushort where the program uses a
     byte) cost a day: the index scaled by 256 and floor_collide read garbage.
  3. UNTRANSLATED 68K ADDRESS. A value returned by a function whose declared return
     is a 68K address, dereferenced without going through P68/W68/ROM_BASE. p_A0 in
     port_zoom_scale read SH-2 address 1 and D3 stayed 0 for two build cycles.
"""
import collections, json, os, re, sys

FAIL = []
UNIT = 'sh_src/portfull/altbeast_port.c'
DRV  = 'sh_src/port_l2.c'

# ---- 1. result-slot overlap -------------------------------------------------
def check_slots():
    if not os.path.exists(DRV):
        return
    src = open(DRV).read()
    sizes = dict(re.findall(r'volatile uint (port_\w+)\[(\d+)\]', src))
    # EXCLUDE THE DECLARATION ITSELF. `volatile uint port_l2_out[80];` matched the
    # index pattern and the guard reported "writes index [80] but is declared [80]"
    # for every array -- a false positive on its own first run.
    decl_spans = [m.span() for m in re.finditer(r'volatile uint port_\w+\[\d+\]', src)]
    def in_decl(pos):
        return any(a <= pos < b for a, b in decl_spans)
    writes = collections.defaultdict(set)
    for m in re.finditer(r'\b(port_\w*out)\s*\[\s*(\d+)\s*\]', src):
        if in_decl(m.start()):
            continue
        writes[m.group(1)].add(int(m.group(2)))
    # the declared map: lines like " *   [20..31]   per watched cell: ..."
    for arr in sizes:
        n = int(sizes[arr])
        bad = sorted(i for i in writes.get(arr, ()) if i >= n)
        if bad:
            FAIL.append('%s: writes index %s but is declared [%d]'
                        % (arr, bad[:5], n))
    # NO CROSS-ARRAY POOLING. The first version collected [a..b] ranges from the
    # map comments of all THREE arrays into one list and reported nine overlaps
    # between unrelated arrays -- the same "shape without a discriminator" failure
    # the LESSONS entry describes, committed inside the guard meant to prevent it.
    # Overlap is only meaningful within ONE array's map, and the maps are not
    # machine-attributable to their array yet, so this check is DISABLED rather
    # than wrong. The index-bound check above is the part that has teeth.

# ---- 2. width disagreement with the arcade ---------------------------------
def check_widths():
    if not (os.path.exists(UNIT) and os.path.exists('docs/proof/l1_vectors.jsonl')):
        return
    obs = {}
    for line in open('docs/proof/l1_vectors.jsonl'):
        line = line.strip()
        if not line:
            continue
        v = json.loads(line)
        a6 = (v.get('entry') or {}).get('a', [0] * 7)[6] & 0xFFFFFF
        for key in ('reads', 'writes'):
            for e in (v.get(key) or []):
                off = (int(e[0], 16) & 0xFFFFFF) - a6
                if 0 <= off < 0x80:
                    obs.setdefault(off, set()).add(e[2])
    src = re.sub(r'/\*.*?\*/', ' ', open(UNIT).read(), flags=re.S)
    W = {'byte': 1, 'char': 1, 'undefined1': 1, 'ushort': 2, 'short': 2,
         'undefined2': 2, 'uint': 4, 'int': 4, 'undefined4': 4}
    seen = collections.Counter()
    for ty, off in re.findall(r'\*\(\s*(\w+)\s*\*\)\s*\(\s*unaff_A6\s*\+\s*(0x[0-9a-f]+)\s*\)', src):
        w = W.get(ty)
        o = int(off, 16)
        if w and o in obs and w not in obs[o] and max(obs[o]) < w:
            seen[(o, ty, w, tuple(sorted(obs[o])))] += 1
    for (o, ty, w, ow), n in seen.most_common(12):
        FAIL.append('A6+0x%02X accessed as %s (%dB) at %d site(s); the arcade only '
                    'ever accesses it %s' % (o, ty, w, n, 'B/'.join(map(str, ow)) + 'B'))

# ---- 3. a 68K address dereferenced without translation ---------------------
def check_domains():
    if not os.path.exists(UNIT):
        return
    src = re.sub(r'/\*.*?\*/', ' ', open(UNIT).read(), flags=re.S)
    # a value assigned from a function whose return is a 68K ROM address, then
    # dereferenced directly in the same body
    for m in re.finditer(r'(\w+)\s*=\s*sprite_frame_lookup\s*\(', src):
        var = m.group(1)
        tail = src[m.end():m.end() + 4000]
        if re.search(r'\*\s*\(\s*\w+\s*\*\)\s*\(\s*' + re.escape(var) + r'\b', tail):
            FAIL.append('%s holds a 68K address from sprite_frame_lookup and is '
                        'dereferenced without P68/ROM_BASE' % var)


def check_ptr_roundtrip():
    """A 68K POINTER ROUND-TRIPPED THROUGH A 16-BIT INTEGER.

    The 68000 could hold any work-RAM pointer in 16 bits, because work RAM was
    0xFFxxxx and the hardware sign-extended. The decompiled C does exactly that --
    it stores a pointer in a `short` and re-expands it later -- and on the SH-2 work
    RAM is at _WRAM_BASE (0x0602CC00), so the truncation DESTROYS the pointer and the
    re-expansion yields an address in the 32X BIOS.

    MEASURED 2026-10-02, and it is the whole reason the rooted main loop hangs:
    FUN_0000e16a scans the object table with

        for (; -1 < *(char *)(psVar10 + 4); psVar10 = psVar10 + 0x40) { }
        ...
        psVar10 = (short *)(int)psVar10[0x3c];     <-- a 16-bit work-RAM pointer
        sVar11  = (short)psVar10;                  <-- an SH-2 pointer truncated

    so the scan walks off into the BIOS and the empty for-loop never terminates. The
    port runs for 8000 ares frames without finishing one seeded frame.

    w68.h already has the correct translation in W68(); nothing applies it here, and
    no type forbids the mix. That is PORT-ARCH step 3's `a68` type, which step 1 turns
    out to depend on.
    """
    if not os.path.exists(UNIT):
        return
    src = re.sub(r'/\*.*?\*/', ' ', open(UNIT).read(), flags=re.S)
    blocks = re.split(r'(?=/\* ---- 0x)', open(UNIT).read())
    for b in blocks:
        m = re.match(r'/\* ---- (0x[0-9A-F]+)\s+(\w+)', b)
        if not m:
            continue
        body = re.sub(r'/\*.*?\*/', ' ', b, flags=re.S)
        # ONLY THE UNTRANSLATED ONES. 35 of the 59 raw matches already read
        # `(byte *)(int)W68(...)` -- the (int) there is cast noise around a
        # correct translation. A guard that counts those is decoration, and it
        # reported FUN_000154C0 as a NEW violation for a line that was right
        # (2026-10-02).
        n = 0
        for mm in re.finditer(r'\([A-Za-z_][A-Za-z0-9_]*\s*\*\)\(int\)\s*', body):
            if 'W68' not in re.split(r'[;\n]', body[mm.end():])[0][:40]:
                n += 1
        if n:
            FAIL.append('%s %s: %d pointer(s) re-expanded from a 16-bit integer '
                        '((T *)(int)x) with no W68 translation' % (m.group(1), m.group(2), n))
    for b in blocks:
        m = re.match(r'/\* ---- (0x[0-9A-F]+)\s+(\w+)', b)
        if not m:
            continue
        body = re.sub(r'/\*.*?\*/', ' ', b, flags=re.S)
        # ONLY AN *ASSIGNMENT* TO THE SP IS A DEFECT NOW.
        #
        # register0x0000003c is 68K register space 0x3C = A7, and since 2026-10-02
        # the port initialises it to the real reset SP, 0xFFFFFF00 (defs.c
        # port_reset_sp, re-run per frame). So READING AND WRITING THROUGH IT IS
        # CORRECT -- those are the 68000's own `jsr` return-address pushes, and they
        # land in work RAM at 0xFFFFFEFC..0xFFFFFF00 where the arcade put them.
        # That is how the port's stack top came to read 0x0AE2, the arcade's own
        # value.
        #
        # What is still wrong is a function that MOVES the SP without restoring it:
        # Ghidra models `-(SP)` as a decrement with no matching pop, so its SP drifts
        # down one slot per iteration and a value spilled before a call is read back
        # from a different cell. That is what made FUN_0000E16A produce
        # `10 11 12 66 CE FF...` and it is why that one is hand-written now.
        # AND ONLY A *SELF-RELATIVE* ASSIGNMENT. `register0x0000003c = &DAT_ffffff00`
        # is Ghidra's model of `lea 0xffffff00,%sp` -- the loop top at 0x62A
        # re-loading the real reset SP -- and it is exactly right. Counting it made
        # the guard flag frame_rotated, the function the port now runs every frame,
        # for being faithful (2026-10-02). The defect is `SP = SP - n`: a move with
        # no matching pop, after which a value spilled across a call is read back
        # from a different cell.
        n_asg = len(re.findall(
            r'register0x0000003c\s*=[^;]*register0x0000003c', body))
        if n_asg:
            FAIL.append('%s %s: ASSIGNS register0x0000003c (the 68K SP) %d time(s) '
                        '-- Ghidra models -(SP) with no matching pop, so a value '
                        'spilled across a call is read back from the wrong cell'
                        % (m.group(1), m.group(2), n_asg))

check_slots()
check_widths()
check_domains()
check_ptr_roundtrip()

# A BASELINE, or the guard blocks everything forever. The eight A6-width
# disagreements it finds on 2026-10-01 are PRE-EXISTING and each needs a Ghidra
# cycle, so failing the build on them would stop all work. They are recorded as
# accepted-and-known; the guard fails only on violations NOT in that file.
#
# Shrinking this file is the actual measure of progress on the class. Growing it
# without a reason is how a guard becomes decoration.
BL = 'docs/audit/port_guard_baseline.txt'
known = set()
if os.path.exists(BL):
    known = set(l.strip() for l in open(BL) if l.strip() and not l.startswith('#'))
new_f = [f for f in FAIL if f not in known]
if '--baseline' in sys.argv:
    open(BL, 'w').write('# port_guard: accepted, pre-existing violations.\n'
                        '# Each needs a Ghidra declaration change. SHRINK this file.\n'
                        + '\n'.join(FAIL) + '\n')
    print('port_guard: baselined %d violation(s) -> %s' % (len(FAIL), BL))
    sys.exit(0)
if new_f:
    print('port_guard: %d NEW VIOLATION(S)' % len(new_f))
    for f in new_f:
        print('  %s' % f)
    sys.exit(1)
print('port_guard: clean -- %d known violation(s) in the baseline, 0 new' % len(known or FAIL))
