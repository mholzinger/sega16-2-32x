#!/usr/bin/env python3
"""Every string TESTCART prints must be renderable (2026-09-27).

tc_glyph knows space, ! 0-9 A-Z : . - > | + % and NOTHING ELSE -- an unknown
character returns glyph 0 and draws as a BLANK. It does not warn, and the
result looks like a spacing choice rather than a bug: the help page's
"?  NO REFERENCE YET" rendered as "   NO REFERENCE YET", and the same '?' has
been silently invisible in the delta column since that column was written.

So the glyph set is parsed out of tc_glyph and every string literal in
testcart.c is checked against it. Run it after touching any on-screen text.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "md_src" / "testcart.c"


def glyphset(text):
    body = re.search(r"static uint16_t tc_glyph\(char c\)\s*\{(.*?)\n\}", text, re.S)
    if not body:
        sys.exit("tc_glyphcheck: cannot find tc_glyph in testcart.c")
    b = body.group(1)
    ok = set(" ")
    for lo, hi in re.findall(r"c >= '(.)' && c <= '(.)'", b):
        ok |= {chr(x) for x in range(ord(lo), ord(hi) + 1)}
    ok |= set(re.findall(r"c == '(.)'", b))
    ok |= set(re.findall(r"case '(.)':", b))
    return ok


def strip_comments(t):
    """Quoted prose inside a COMMENT is not printed. The first version of this
    tool scanned the raw file and reported 30 missing glyphs, all of them
    lowercase letters in explanatory comments -- a checker whose output is 99%
    false positives is one nobody reads twice."""
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith("/*", i):
            j = t.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif t.startswith("//", i):
            j = t.find("\n", i)
            i = n if j < 0 else j
        elif t[i] == '"':
            j = i + 1
            while j < n and t[j] != '"':
                j += 2 if t[j] == "\\" else 1
            out.append(t[i:j + 1]); i = j + 1
        else:
            out.append(t[i]); i += 1
    return "".join(out)


def main():
    raw = SRC.read_text()
    ok = glyphset(raw)
    text = strip_comments(raw)
    print("glyphs: " + "".join(sorted(ok)).replace(" ", "_"))
    # ONLY WHAT IS ACTUALLY PRINTED. A bare scan also catches #include names,
    # section attributes and asm constraints -- "common.h", ".rodata", "+d" --
    # none of which reach a glyph. The strings that do are tc_puts/tc_frame
    # arguments and the four on-screen tables.
    printed = []
    for m in re.finditer(r'tc_(?:puts|frame)\s*\([^;]*?"((?:[^"\\]|\\.)*)"', text):
        printed.append(m)
    for tag in ("TC_NAME", "TC_CATNAME", "TC_PROVCH"):
        t = re.search(r"%s\[[^\]]*\][^=]*=\s*\{(.*?)\n\};" % tag, text, re.S)
        if t:
            printed += list(re.finditer(r'"((?:[^"\\]|\\.)*)"', t.group(1)))
    bad = {}
    for m in printed:
        lit = m.group(1)
        if "\\" in lit:
            continue
        line = raw.count("\n", 0, raw.find(m.group(0))) + 1
        for ch in lit:
            if ch not in ok:
                bad.setdefault(ch, []).append((line, lit))
    if not bad:
        print(f"PASS  every printable string in {SRC.name} is renderable")
        return 0
    for ch, uses in sorted(bad.items()):
        print(f"FAIL  {ch!r} has no glyph -- draws as a BLANK, {len(uses)} use(s)")
        for line, lit in uses[:3]:
            print(f"        testcart.c:{line}  \"{lit}\"")
    return 1


if __name__ == "__main__":
    sys.exit(main())
