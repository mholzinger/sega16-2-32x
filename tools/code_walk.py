#!/usr/bin/env python3
"""Find the code in a 68000 rom with no Ghidra and no reference listing.

    python3 tools/code_walk.py [rom] > addrs.txt

`tools/code_stream.py` decodes the instruction addresses in
`repair_seeds`, which exists because someone once had a reference
disassembly of THIS title. A new title has neither, and the whole
Ghidra-free pipeline is built on having that address set. This produces it
from the rom alone.

Recursive descent from the 68000 vector table: disassemble from an
address, walk forward until the flow ends, push every branch and call
target, repeat. That is the only bootstrap the rest of the kit needs.

Descent from the vectors alone reaches 4.4% of this rom, because System 16
dispatches nearly everything through `jmp (a0)` with a0 loaded from a
routine pointer. So the walk runs to a FIXPOINT: after each pass it
harvests, from the code it has just decoded, every longword the program
installs as an object routine pointer (`move.l #addr,$02(a6)`) and every
address pushed for an `rte` dispatch (`pea addr` before `rte`), seeds
those, and goes again. Each pass decodes more code, which reveals more
installers.

It then tries the POINTER TABLES the same way — a run of longwords that
mostly point at decoded code is a jump table — and on this rom that finds
almost nothing, for a reason worth knowing: the per-scene dispatch tables
at 0x92F0 and 0x17E24 have NO entry reachable any other way, so there is
nothing to validate them against. A table whose every target is only
reachable through the table itself cannot be bootstrapped.

MEASURED ON ALTERED BEAST, against a reference disassembly's own 19137
instruction addresses:

    descent + harvest + tables   5601 addresses,  29.3% recall, 0 wrong
    plain LINEAR sweep          34748 addresses,  99.8% recall, 15641 wrong

    and the descent set is a strict SUBSET of the linear sweep: on 5601
    addresses the two never disagree.

So neither is "the" answer and the kit uses both. `--linear` prints the
sweep. Take the SUPERSET for a hazard census, where missing a TAS is the
failure and a false positive costs one hand check. Take the VERIFIED set
for a rom map, where a false instruction inflates the code coverage and
hides data. Where they disagree, the descent is right.
"""
import re, subprocess, sys, os

OD = os.environ.get('M68K_OBJDUMP',
     '/Users/mikeholzinger/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')
HEAD = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*?)\s*$')
CONT = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*$')
TARGET = re.compile(r'0x([0-9a-f]+)$')

# flow ends here and nothing after is reachable by falling through
STOPS = ('rts', 'rte', 'rtr', 'jmp', 'bra', 'braw', 'bras', 'stop', 'illegal')
# these branch AND fall through
BRANCH = re.compile(r'^(b(eq|ne|cc|cs|pl|mi|hi|ls|ge|lt|gt|le|vc|vs)[sw]?|db\w+)$')
CALL = re.compile(r'^(jsr|bsr[sw]?)$')
WINDOW = 0x800


def disasm(rom_path, start, stop):
    o = subprocess.run(
        [OD, '-D', '-b', 'binary', '-m', 'm68k:68000', rom_path,
         '--start-address=0x%X' % start, '--stop-address=0x%X' % stop],
        capture_output=True, text=True).stdout
    r = []
    for ln in o.splitlines():
        m = HEAD.match(ln)
        if m:
            r.append([int(m.group(1), 16),
                      len(m.group(2).replace(' ', '')) // 2,
                      m.group(3), m.group(4)])
            continue
        m = CONT.match(ln)            # objdump wraps anything over 6 bytes
        if m and r:
            r[-1][1] += len(m.group(2).replace(' ', '')) // 2
    return r


INSTALL = re.compile(r'^#(-?\d+),%(?:fp|a\d)@\(2\)$')
PEA = re.compile(r'^0x([0-9a-f]+)$')


def harvest(seen):
    """Code addresses the decoded code itself names, which descent misses."""
    out = set()
    items = sorted(seen.items())
    for i, (a, (n, mn, ops)) in enumerate(items):
        if mn == 'movel':
            m = INSTALL.match(ops)          # object $02, the routine pointer
            if m:
                out.add(int(m.group(1)) & 0xFFFFFF)
        elif mn == 'pea':
            m = PEA.match(ops)              # pushed for an rte dispatch
            if m and any(items[j][1][1] == 'rte'
                         for j in range(i + 1, min(i + 4, len(items)))):
                out.add(int(m.group(1), 16))
    return out


def tables(rom, seen, run=4, hit=0.75):
    """Runs of longwords that mostly point at code already decoded.

    A jump table's entries are code by construction, so a window whose
    members overwhelmingly land on known instruction boundaries is one,
    and the entries that are NOT yet known are the code descent could not
    reach. This is how the object routine tables get in.
    """
    out = set()
    n = len(rom)
    ok = [False] * (n // 2 + 1)
    for i in range(0, n - 3, 2):
        v = int.from_bytes(rom[i:i + 4], 'big')
        ok[i // 2] = 0x400 <= v < n and v in seen
    for i in range(0, n - 3 - 4 * run, 4):
        w = [ok[(i + 4 * k) // 2] for k in range(run)]
        if sum(w) >= max(2, int(run * hit)):
            for k in range(run):
                v = int.from_bytes(rom[i + 4 * k:i + 4 * k + 4], 'big')
                if 0x400 <= v < n and not (v & 1):
                    out.add(v)
    return out


def walk(rom_path, seeds, seen=None):
    rom = open(rom_path, 'rb').read()
    if seen is None:
        seen = {}
    work = list(seeds)
    runs = 0
    while work:
        a = work.pop()
        if a in seen or not (0 <= a < len(rom)) or a & 1:
            continue
        runs += 1
        ins = disasm(rom_path, a, min(a + WINDOW, len(rom)))
        for addr, n, mn, ops in ins:
            if addr in seen:
                break                 # joined a run we already have
            seen[addr] = (n, mn, ops)
            m = TARGET.search(ops)
            if m and (BRANCH.match(mn) or CALL.match(mn) or
                      mn.startswith(('jmp', 'bra'))):
                t = int(m.group(1), 16)
                if t not in seen:
                    work.append(t)
            if mn.split('.')[0] in STOPS or mn in STOPS:
                break
        else:
            work.append(a + WINDOW)   # ran off the window still in flow
    return seen, runs


def main():
    rom_path = 'roms/altbeast/prog68k.bin'
    if '--linear' in sys.argv:
        args = [a for a in sys.argv[1:] if not a.startswith('--')]
        if args:
            rom_path = args[0]
        rom = open(rom_path, 'rb').read()
        start = int.from_bytes(rom[4:8], 'big')
        ceiling = int(os.environ.get('CODE_CEILING', '0x1EF20'), 16)
        ins = disasm(rom_path, start, min(ceiling, len(rom)))
        for a, n, mn, ops in ins:
            print('%X' % a)
        sys.stderr.write('%d boundaries, linear from 0x%X to 0x%X — a '
                         'SUPERSET: data decoded as instructions is in here\n'
                         % (len(ins), start, ceiling))
        return
    extra = []
    args = sys.argv[1:]
    if args and not args[0].startswith('--'):
        rom_path = args[0]
    for i, x in enumerate(args):
        if x == '--seed':
            extra.append(int(args[i + 1], 16))
    rom = open(rom_path, 'rb').read()
    seeds = [int.from_bytes(rom[v * 4:v * 4 + 4], 'big') for v in range(1, 64)]
    seeds = [s for s in seeds if 0 < s < len(rom)] + extra
    seen, runs, passes = {}, 0, 0
    work = list(seeds)
    while work:
        passes += 1
        got, r = walk(rom_path, work, seen)
        runs += r
        work = [a for a in harvest(seen) if a not in seen and 0 < a < len(rom)]
        if not work:
            work = [a for a in tables(rom, seen)
                    if a not in seen and 0 < a < len(rom)]
    for a in sorted(seen):
        print('%X' % a)
    sys.stderr.write('%d instructions, %d vector seeds, %d passes, %d runs\n'
                     % (len(seen), len(seeds), passes, runs))


if __name__ == '__main__':
    main()
