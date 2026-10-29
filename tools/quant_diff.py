#!/usr/bin/env python3
"""Differential test: the SH-2's mdp_quant against the baker's md() (2026-09-27).

WHY THIS EXISTS. The MD pen tables are generated on the host by
tools/bake_tilecram.py and consumed at runtime by sh_src/m_main.c. Two
implementations of one function in two languages, and when they drift the
symptom is not a crash -- it is PINK TREES. LOOP29 196: the baked tables
carried md_word()'s field positions where md_pack9()'s were wanted, so the
SH-2 re-read every colour wrong, and white (7,7,7) came back as (6,5,3).
bake_tilecram.py:md() carries a docstring warning that it MUST match the
runtime, which is a comment, not a test. This is the test.

AND THERE ARE NOW TWO RUNTIME QUANTISERS. m_main.c:3093 branches on
PAL_QUANT_RAMP: the #else is the linear `(v5+2)>>2 clamp 7` the baker
implements, and the #ifdef side is a RAMP-AWARE table (mdp_q5) fitted to the
MD DAC's measured non-linear output, which deliberately moves inputs one level
down. PALQUANT=1 selects it. If it is ever put on the line while the baker
stays linear, the two disagree and MDROUND=1 -- which IS on the line -- puts
the disagreeing tables in force. That is the pink-trees failure mode re-armed,
and this tool is what notices.

HOW IT AVOIDS RE-DERIVING THE BUG. It does not reimplement the C in Python.
It EXTRACTS the preprocessor block from m_main.c verbatim, compiles it twice
with the host cc (once each side of the ifdef), runs both over all 65536 S16
colour words, and diffs against the baker's own md()/md_pack9(). The C under
test is the repo's text, not a transcription.

    python3 tools/quant_diff.py          # exits 1 if the line's pair disagree
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "sh_src" / "m_main.c"


def extract_block():
    """The PAL_QUANT_RAMP ifdef block, verbatim, by #if/#endif depth."""
    lines = SRC.read_text(errors="replace").splitlines()
    start = None
    for i, l in enumerate(lines):
        if l.strip() == "#ifdef PAL_QUANT_RAMP":
            start = i
            break
    if start is None:
        sys.exit("quant_diff: no '#ifdef PAL_QUANT_RAMP' in m_main.c -- the "
                 "quantiser was restructured and this tool needs re-aiming.")
    depth = 0
    for j in range(start, len(lines)):
        st = lines[j].strip()
        if st.startswith("#if"):
            depth += 1
        elif st.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return start + 1, "\n".join(lines[start:j + 1])
    sys.exit("quant_diff: unterminated #ifdef PAL_QUANT_RAMP")


HARNESS = """#include <stdint.h>
#include <stdio.h>
%s
int main(void)
{
    for (unsigned v = 0; v < 65536u; v++)
        printf("%%u\\n", (unsigned)mdp_quant((uint16_t)v));
    return 0;
}
"""


def run_variant(block, ramp):
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "q.c"
        c.write_text(HARNESS % block)
        exe = Path(d) / "q"
        cc = subprocess.run(
            ["cc", "-O2", "-std=c99", "-w"] + (["-DPAL_QUANT_RAMP"] if ramp else [])
            + [str(c), "-o", str(exe)],
            capture_output=True, text=True)
        if cc.returncode:
            sys.exit("quant_diff: host cc failed on the extracted block:\n"
                     + cc.stderr)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    return [int(x) for x in out.stdout.split()]


def main():
    line_no, block = extract_block()
    print(f"extracted m_main.c:{line_no} .. the PAL_QUANT_RAMP block "
          f"({block.count(chr(10)) + 1} lines)")

    sys.path.insert(0, str(ROOT / "tools"))
    import bake_tilecram as bake

    py = [bake.md_pack9(bake.md(v)) for v in range(65536)]
    linear = run_variant(block, ramp=False)
    ramp = run_variant(block, ramp=True)

    rc = 0

    # THE INVARIANT THAT PROTECTS THE LINE. PALQUANT is not in LINE_FLAGS, so
    # the shipped runtime is the #else branch, and it must agree with the baker
    # on every one of the 65536 words -- not "mostly", every one.
    bad = [v for v in range(65536) if linear[v] != py[v]]
    if bad:
        rc = 1
        print(f"FAIL  linear mdp_quant vs baker md(): {len(bad)} of 65536 differ")
        for v in bad[:6]:
            print(f"        word {v:#06x}  C {linear[v]:#05x}  py {py[v]:#05x}")
    else:
        print("PASS  linear mdp_quant == baker md() on all 65536 S16 words")

    # And the ramp variant is EXPECTED to differ -- that is what it is for. The
    # number is the finding: it says how far a PALQUANT=1 build would drift
    # from the baked tables if the baker were not taught the ramp too.
    dr = [v for v in range(65536) if ramp[v] != py[v]]
    print(f"INFO  ramp mdp_quant (PALQUANT=1) differs from the baker on "
          f"{len(dr)} of 65536 words")

    # PER-LEVEL, not per-word. A word differs if ANY of its three channels
    # does, so counting words says 87% and means nothing about the mapping.
    # The mapping is 32 five-bit levels wide, so compare THOSE: build each
    # level by putting it in the R channel alone (nibble in bits 0-3, LSB in
    # bit 12) and reading the low 3 bits of the result back out.
    moved = []
    for k in range(32):
        w = ((k >> 1) & 0xF) | ((k & 1) << 12)
        a, b = ramp[w] & 7, linear[w] & 7
        if a != b:
            moved.append((k, b, a))
    print(f"INFO  {len(moved)} of the 32 five-bit input levels move:")
    print("        " + "  ".join(f"{k}:{b}->{a}" for k, b, a in moved))
    # m_main.c:3103 claims "Sixteen of 32 inputs move one level down". Check
    # the comment against the code rather than believing it.
    down1 = all(a == b - 1 for _, b, a in moved)
    print(f"        m_main.c:3103 says sixteen move one level down -- "
          f"count {len(moved)}, all exactly one level down: {down1}")
    if len(moved) != 16 or not down1:
        rc = 1
        print("FAIL  m_main.c:3103's own claim about mdp_q5 no longer holds")
    if dr:
        print("INFO  so PALQUANT=1 with a LINEAR baker re-arms the pink-trees "
              "class of bug (baked table vs runtime disagree, and MDROUND=1 is "
              "on the line). Teach bake_tilecram.md() the mdp_q5 ramp before "
              "putting PALQUANT on the line.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
