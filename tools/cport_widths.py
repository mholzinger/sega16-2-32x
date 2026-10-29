#!/usr/bin/env python3
"""THE WIDTH OF EVERY WORK-RAM ACCESS, READ OUT OF THE 68K's OWN BYTES.

    python3 tools/cport_widths.py                      # the mismatch report
    python3 tools/cport_widths.py --addr FFF132        # one cell, every site
    python3 tools/cport_widths.py --all                # every cell, every width

WHY THIS EXISTS. The port declares one C type per absolute symbol, and when that
width disagrees with what the 68000 actually did, the result is a SILENT WRONG VALUE:
a 16-bit access at a long's own address is its HIGH half on a big-endian machine, and
C's narrowing cast takes the LOW half. That defect cost a day and was found by hand,
one cell at a time, from the far end of a five-step causal chain (FULL-PORT.md 56).

It never needed to be found by hand. The program says what width it uses, in its own
instruction encodings, and there is a 68K objdump in the tree:

    ~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump -D -b binary -m m68k:68000

So: disassemble, count the access widths per address, and diff against what the port
DECLARED. Every disagreement is a candidate defect, ranked by how often the program
touches the cell.

DISASSEMBLY IS ALIGNED PER FUNCTION, which is the whole reason this is trustworthy.
A linear sweep of a ROM with embedded tables decodes data as instructions and invents
operands; every count it produces is then part noise. docs/audit/func_ranges.txt
carries 938 ranges from Ghidra's own function boundaries, so each range is
disassembled from a REAL instruction boundary and nothing outside a function body is
decoded at all.

WHAT IT CANNOT TELL YOU. A `.w` access at ADDR is unambiguous about its width, but
not about intent: a long that the program loaded into a register and then used `.w`
really is the register's low half, and that is invisible here because it is not an
access at ADDR. So this report NAMES SUSPECTS; the L2 gate arbitrates each one. The
same discipline as CPORT_CLAMP and CPORT_HI, which is where its output goes.
"""
import argparse, collections, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDUMP = os.environ.get('M68K_OBJDUMP',
    os.path.expanduser('~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump'))
ROM = os.path.join(ROOT, 'roms/altbeast/prog68k.bin')
RANGES = os.path.join(ROOT, 'docs/audit/func_ranges.txt')
SYMS = os.path.join(ROOT, 'sh_src/portfull/dat_syms.h')

ap = argparse.ArgumentParser()
ap.add_argument('--addr', help='one 68K address (hex), every site listed')
ap.add_argument('--all', action='store_true', help='every cell, not just mismatches')
ap.add_argument('--min-refs', type=int, default=1)
ap.add_argument('--json', metavar='FILE',
                help='write the census as JSON for tools/cport_prep.py to clamp with')
a = ap.parse_args()

if not os.path.exists(OBJDUMP):
    raise SystemExit('no 68K objdump at %s (set M68K_OBJDUMP)' % OBJDUMP)

# ---- the function ranges, so every disassembly starts on a real boundary ----
ranges = []
for l in open(RANGES):
    if l.startswith('#'):
        continue
    f = l.split()
    if len(f) >= 4 and f[0].startswith('0x'):
        ranges.append((int(f[0], 16), int(f[1], 16) + 1, f[3]))
ranges.sort()

# ---- one objdump per CONTIGUOUS run of ranges, to keep the process count sane ----
runs = []
for lo, hi, nm in ranges:
    if runs and lo <= runs[-1][1]:
        runs[-1][1] = max(runs[-1][1], hi)
    else:
        runs.append([lo, hi])

# A size suffix is the last character of the mnemonic for the ones that carry it.
# `lea`, `moveq`, `swap`, `jsr`, `bsr`, branches and the bit ops carry none; those
# never name a data width at an absolute address in a way that matters here, except
# btst/bset/bclr/bchg, which are byte operations on memory.
_SIZED = re.compile(r'^([a-z]{2,7})([bwl])$')
_BITOP = ('btst', 'bset', 'bclr', 'bchg', 'tas')
# one-operand instructions whose operand is a DESTINATION, not a source
_ONEOP_WRITES = {'clr', 'st', 'sf', 'neg', 'negx', 'not', 'ext', 'extb', 'swap',
                 'nbcd', 'bset', 'bclr', 'bchg', 'tas'}
# absolute-short operands sign-extend: 0xf132 in the encoding, 0xfffff132 printed.
_ABS = re.compile(r'0x(ffff[0-9a-f]{4}|00[0-9a-f]{6}|[0-9a-f]{1,8})\b')

cells = collections.defaultdict(lambda: collections.Counter())
sites = collections.defaultdict(list)
_ndis = 0
for lo, hi in runs:
    out = subprocess.run([OBJDUMP, '-D', '-b', 'binary', '-m', 'm68k:68000',
                          '--start-address=0x%X' % lo, '--stop-address=0x%X' % hi, ROM],
                         capture_output=True, text=True).stdout
    for l in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{4}\s+)+)\s*([a-z][a-z0-9]*)\s*(.*)$', l)
        if not m:
            continue
        pc, mn, ops = int(m.group(1), 16), m.group(3), m.group(4)
        _ndis += 1
        sm = _SIZED.match(mn)
        if sm:
            base, wid = sm.group(1), {'b': 1, 'w': 2, 'l': 4}[sm.group(2)]
        elif mn in _BITOP:
            base, wid = mn, 1
        else:
            continue
        for om in _ABS.finditer(ops):
            v = int(om.group(1), 16)
            if (v & 0xFFFF0000) != 0xFFFF0000:
                continue                      # not a work-RAM absolute-short
            addr = v & 0xFFFFFF | 0xFF0000
            # WHICH SIDE OF THE COMMA, and the single-operand forms are not all
            # reads: `clr`/`st`/`sf`/`scc` WRITE their one operand, `tst` reads it,
            # and the read-modify-writes (`addq`, `subq`, `neg`, `not`, `eor`...)
            # do both. Getting this wrong makes `clrw 0xfffff02c` look like a word
            # READ of a cell the program only ever writes a byte to, which is the
            # opposite of the conclusion.
            half = ops.split(',')
            if len(half) > 1:
                is_dst = om.group(0) in half[-1]
            else:
                is_dst = base in _ONEOP_WRITES
            cells[addr][(wid, 'W' if is_dst else 'R')] += 1
            sites[addr].append((pc, mn, ops.strip()))

print('disassembled %d instructions across %d function run(s); %d work-RAM cells '
      'touched' % (_ndis, len(runs), len(cells)))

# ---- what the port DECLARED ----
decl = {}
for l in open(SYMS):
    m = re.match(r'extern (\w+)\s*(\**)\s*(\S+);\s*/\* 0x([0-9A-F]{8}), (\d+) refs', l)
    if not m:
        continue
    ty, star, nm, ad, refs = m.group(1), m.group(2), m.group(3), int(m.group(4), 16), int(m.group(5))
    w = 4 if star else {'byte': 1, 'ushort': 2, 'short': 2, 'uint': 4, 'int': 4}.get(ty)
    a68 = 0xFF0000 | (ad & 0xFFFF) if (ad & 0xFFFF0000) == 0xFFFF0000 else ad
    # the WIDEST declaration at an address wins: Ghidra emits both DAT_x (byte) and
    # _DAT_x (the real width) for an overlapping pair, and the wide one is the cell.
    if w and (a68 not in decl or w > decl[a68][0]):
        decl[a68] = (w, nm, refs, ty + star)

if a.addr:
    want = 0xFF0000 | (int(a.addr, 16) & 0xFFFF)
    d = decl.get(want)
    print('\n0x%06X  declared %s  observed %s' % (
        want, '%s (%dB)' % (d[3], d[0]) if d else '(not declared)',
        dict(cells.get(want, {}))))
    for pc, mn, ops in sorted(sites.get(want, [])):
        print('   %05X  %-8s %s' % (pc, mn, ops))
    sys.exit(0)

if a.json:
    import json
    doc = {}
    for addr, c in cells.items():
        widths = collections.Counter()
        for (w, _rw), n in c.items():
            widths[w] += n
        doc['%08X' % (0xFFFF0000 | (addr & 0xFFFF))] = {
            'dominant': widths.most_common(1)[0][0],
            'uses': sum(widths.values()),
            'widths': {str(w): n for w, n in sorted(widths.items())},
        }
    with open(a.json, 'w') as fh:
        json.dump(doc, fh, indent=0, sort_keys=True)
        fh.write('\n')
    print('wrote %d cell(s) to %s' % (len(doc), a.json))
    sys.exit(0)

rows = []
for addr, c in cells.items():
    tot = sum(c.values())
    if tot < a.min_refs:
        continue
    widths = collections.Counter()
    for (w, _rw), n in c.items():
        widths[w] += n
    dom = widths.most_common(1)[0][0]
    d = decl.get(addr)
    dw = d[0] if d else None
    mism = dw is not None and dw != dom
    if a.all or mism:
        rows.append((tot, addr, dom, dw, d, dict(widths)))
rows.sort(reverse=True)

print('\n%-8s %-5s %-9s %-9s %-26s %s'
      % ('68K', 'uses', 'OBSERVED', 'DECLARED', 'symbol', 'width histogram'))
for tot, addr, dom, dw, d, widths in rows:
    print('%06X   %-5d %-9s %-9s %-26s %s'
          % (addr, tot, '%dB' % dom, ('%dB' % dw) if dw else '-',
             d[1] if d else '', ' '.join('%dB:%d' % (w, n) for w, n in sorted(widths.items()))))
print('\n%d cell(s) where the 68000\'s dominant access width disagrees with the '
      'port\'s declaration.' % len(rows))
print('A disagreement is a SUSPECT, not a defect: a long loaded into a register and '
      'then used .w is\nthe register\'s low half and leaves no trace at this address. '
      'The L2 gate arbitrates each one,\nwhich is what CPORT_CLAMP and CPORT_HI are for.')
