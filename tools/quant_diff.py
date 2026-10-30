#!/usr/bin/env python3
"""Differential test: the SH-2's r60_quant against the baker's md() (2026-09-27, re-aimed 2026-10-29).

WHY THIS EXISTS. The MD pen tables are generated on the host by
tools/bake_tilecram.py, and the renderer quantises live S16 colours at runtime
(sh_src/r60/r60_planes.c, r60_quant) to match pens against them. Two
implementations of one function in two languages, and when they drift the
symptom is not a crash -- it is PINK TREES. LOOP29 196: the baked tables
carried md_word()'s field positions where md_pack9()'s were wanted, so the
SH-2 re-read every colour wrong, and white (7,7,7) came back as (6,5,3).
bake_tilecram.py:md() carries a docstring warning that it MUST match the
runtime, which is a comment, not a test. This is the test.

HOW IT AVOIDS RE-DERIVING THE BUG. It does not reimplement the C in Python.
It EXTRACTS r60_quant from r60_planes.c verbatim, compiles it with the host cc,
runs it over all 65536 S16 colour words, and diffs against the baker's own
md()/md_pack9(). The C under test is the repo's text, not a transcription.

It aimed at sh_src/m_main.c's mdp_quant (and its PAL_QUANT_RAMP variant) until
that renderer was retired; r60_quant is the quantiser the shipping rom runs.

    python3 tools/quant_diff.py          # exits 1 if the two disagree
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "sh_src" / "r60" / "r60_planes.c"


def extract_function():
    """r60_quant, verbatim: from its signature to the brace that closes it."""
    text = SRC.read_text(errors="replace")
    m = re.search(r"static uint16_t r60_quant\(uint16_t v\)\s*\{", text)
    if not m:
        sys.exit("quant_diff: no 'static uint16_t r60_quant(uint16_t v)' in r60_planes.c -- "
                 "the quantiser was restructured and this tool needs re-aiming.")
    depth = 0
    for j in range(m.end() - 1, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text.count("\n", 0, m.start()) + 1, text[m.start():j + 1]
    sys.exit("quant_diff: unterminated r60_quant")


HARNESS = """#include <stdint.h>
#include <stdio.h>
%s
int main(void)
{
    for (unsigned v = 0; v < 65536u; v++)
        printf("%%u\\n", (unsigned)r60_quant((uint16_t)v));
    return 0;
}
"""


def run(fn):
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "q.c"
        c.write_text(HARNESS % fn)
        exe = Path(d) / "q"
        cc = subprocess.run(["cc", "-O2", "-std=c99", "-w", str(c), "-o", str(exe)],
                            capture_output=True, text=True)
        if cc.returncode:
            sys.exit("quant_diff: host cc failed on the extracted function:\n" + cc.stderr)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    return [int(x) for x in out.stdout.split()]


def main():
    line_no, fn = extract_function()
    print(f"extracted r60_planes.c:{line_no} r60_quant ({fn.count(chr(10)) + 1} lines)")
    sys.path.insert(0, str(ROOT / "tools"))
    import bake_tilecram as bake
    py = [bake.md_pack9(bake.md(v)) for v in range(65536)]
    c = run(fn)
    bad = [v for v in range(65536) if c[v] != py[v]]
    if bad:
        print(f"FAIL  r60_quant vs baker md(): {len(bad)} of 65536 differ")
        for v in bad[:6]:
            print(f"        word {v:#06x}  C {c[v]:#05x}  py {py[v]:#05x}")
        return 1
    print("PASS  r60_quant == baker md() on all 65536 S16 words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
