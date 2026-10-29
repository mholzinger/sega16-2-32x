#!/usr/bin/env python3
"""Compare TESTCART results across machines from work-RAM dumps (2026-09-27).

    python3 tools/tc_compare.py ares=/tmp/ares.bin blastem=/tmp/bl.bin ...

WHY A DUMP AND NOT A SCREENSHOT. The cart's whole point is that a diff across
machines is the finding, and its table is already the readout for a human. For
a MACHINE the values are better taken raw: ares-headless and the BlastEm fork
both support the identical `--dump wram:ADDR:LEN:FILE` (blastem genesis.c:26-41,
big-endian bytes), so one dump spec feeds both and no pixel decoding sits
between the number and the comparison.

    ares-headless rom --frames 1500 --dump wram:0xFFF200:0xF0:ares.bin
    blastem -b 600 -m 32x --dump wram:0xFFF200:0xF0:bl.bin rom

BLASTEM NEEDS THE THREE 32X BIOS ROMS IN THE CWD and opens them with a plain
CWD-relative fopen (BLASTEM.md 37-45). Without them the SH-2s never boot --
the symptom is "Unimplemented instruction: 0, pc=0" and no dump file at all,
which reads like a broken switch rather than a missing file.

LAYOUT, from md_src/testcart.c's scratch map: tc_v[48] longs at 0xFFF200,
tc_res[48] bytes at 0xFFF2C0 (1 pass, 2 fail, 3 report-only, 0 not run). The
names, references, categories and provenance letters are PARSED OUT OF
testcart.c rather than restated here, so this tool cannot drift from the cart.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "md_src" / "testcart.c"
def layout():
    """PARSED FROM testcart.c, not restated here. The first version hardcoded
    0x00/0xC0/48 and went stale the moment the table grew to 56 -- it then read
    tc_res from inside tc_v and printed other tests' verdicts under the new
    tests' names, which is the same silent-misalignment failure the cart itself
    was just fixed for. A tool that duplicates a layout is a second place for
    it to be wrong."""
    t = SRC.read_text()
    n = int(re.search(r"#define TC_MAX\s+(\d+)", t).group(1))
    v = int(re.search(r"#define tc_v\s+\(\(volatile uint32_t\*\)0x([0-9A-Fa-f]+)\)", t).group(1), 16)
    r = int(re.search(r"#define tc_res\s+\(\(volatile uint8_t \*\)0x([0-9A-Fa-f]+)\)", t).group(1), 16)
    return 0, r - v, n


V_OFF, RES_OFF, NMAX = layout()
PROV = "CRDE"


def strip_comments(t):
    return re.sub(r"/\*.*?\*/", " ", t, flags=re.S)


def table(name, cast):
    m = re.search(r"TC_%s\[[^\]]*\][^=]*=\s*\{(.*?)\n\};" % name, SRC.read_text(), re.S)
    if not m:
        sys.exit(f"tc_compare: cannot find TC_{name} in testcart.c")
    body = strip_comments(m.group(1))
    return [cast(x.strip()) for x in body.split(",") if x.strip()]


def names():
    m = re.search(r"TC_NAME\[[^\]]*\][^=]*=\s*\{(.*?)\n\};", SRC.read_text(), re.S)
    return re.findall(r'"([^"]*)"', strip_comments(m.group(1)))


def catnames():
    m = re.search(r"TC_CATNAME\[[^\]]*\][^=]*=\s*\{(.*?)\n\};", SRC.read_text(), re.S)
    return re.findall(r'"([^"]*)"', strip_comments(m.group(1)))


def load(path):
    b = Path(path).read_bytes()
    if len(b) < RES_OFF + NMAX:
        sys.exit(f"tc_compare: {path} is {len(b)} bytes, need {RES_OFF + NMAX}")
    v = [int.from_bytes(b[V_OFF + i * 4: V_OFF + i * 4 + 4], "big") for i in range(NMAX)]
    return v, list(b[RES_OFF:RES_OFF + NMAX])


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: tc_compare.py name=dump.bin [name=dump.bin ...]")
    machines = []
    for a in argv[1:]:
        if "=" not in a:
            sys.exit(f"tc_compare: expected name=path, got {a}")
        n, p = a.split("=", 1)
        machines.append((n, *load(p)))

    nm, ref = names(), table("REF", lambda s: int(s, 0))
    prov, cat = table("PROV", int), table("CAT", int)
    cats = catnames()

    ran = max((i for _, _, r in machines for i in range(NMAX) if r[i]), default=-1) + 1
    w = max(len(n) for n, _, _ in machines)
    print(f"{'':4} {'test':16} {'REF':>6} {'P':1} " +
          " ".join(f"{n:>{max(6, w)}}" for n, _, _ in machines))
    diffs = fails = 0
    for c in range(len(cats)):
        idx = [i for i in range(ran) if cat[i] == c]
        if not idx:
            continue
        print(f"-- {cats[c]}")
        for i in idx:
            cells, vals = [], set()
            for _, v, r in machines:
                if r[i] in (1, 2):
                    cells.append("PASS" if r[i] == 1 else "FAIL")
                    if r[i] == 2:
                        fails += 1
                else:
                    cells.append(f"{v[i]:04X}")
                vals.add((r[i], v[i]))
            mark = " " if len(vals) == 1 else "<"
            if len(vals) != 1:
                diffs += 1
            print(f"T{i:02X}  {nm[i]:16} {ref[i]:6X} {PROV[prov[i]]:1} " +
                  " ".join(f"{c2:>{max(6, w)}}" for c2 in cells) + f" {mark}")
    print()
    print(f"{ran} tests, {len(machines)} machines, {fails} FAIL, "
          f"{diffs} rows where the machines disagree"
          + ("" if diffs else " -- every row identical"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
