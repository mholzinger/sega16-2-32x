#!/usr/bin/env python3
"""FLAG AUDIT — find build flags that cannot do anything.

    tools/flag_audit.py           # full report
    tools/flag_audit.py --quiet   # exit 1 if any DEAD flag is found

Two inertness classes, both of which have really happened here and both
of which cost weeks before anyone noticed:

  DEAD FLAG   `ifdef FOO` in the Makefile adds -DFOO_BAR, and FOO_BAR
              appears nowhere in sh_src/ or md_src/. The flag is a no-op
              and a sweep over it measures one number many times.

  INERT STATE A static array or variable is READ by a guard but never
              WRITTEN, so the guard is false forever. `pal_streak` was
              declared, read at the PALSTREAK backoff test, and never
              incremented; a 25-point sweep produced four identical
              results and the flags had never done anything in a shipped
              rom (LOOP28 85).

Not covered, and stated so it is not mistaken for a pass: a flag whose
define IS referenced but whose effect is undone elsewhere. `BGPACK2`
(2026-09-10) changed MDP_LINES from 3 to 2 while a GENERATED table,
`mds_s_line[]`, still handed out line index 2 — one constant, two
sources of truth. That needs the generated headers checked against the
constants they were baked from, which this tool does not do.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRCDIRS = ("sh_src", "md_src")


def sources():
    out = {}
    for d in SRCDIRS:
        p = os.path.join(ROOT, d)
        if not os.path.isdir(p):
            continue
        for fn in os.listdir(p):
            if fn.endswith((".c", ".h", ".s")):
                out[os.path.join(d, fn)] = open(
                    os.path.join(p, fn), errors="replace").read()
    return out


def makefile_flags():
    """ifdef FOO ... SHCCFLAGS/MDCCFLAGS += -DBAR  ->  {FOO: [BAR, ...]}"""
    txt = open(os.path.join(ROOT, "Makefile"), errors="replace").read().split("\n")
    flags = {}
    cur = None
    for line in txt:
        m = re.match(r'\s*ifdef\s+(\w+)', line)
        if m:
            cur = m.group(1)
            flags.setdefault(cur, [])
            continue
        if re.match(r'\s*endif', line):
            cur = None
            continue
        if cur:
            for d in re.findall(r'-D(\w+)', line):
                flags[cur].append(d)
            for v in re.findall(r'\+=\s*(\w+_\w+)\.o', line):
                flags[cur].append(v)
    return flags


def main():
    quiet = "--quiet" in sys.argv
    src = sources()
    blob = "\n".join(src.values())
    flags = makefile_flags()

    dead, ok, nodef = [], [], []
    for flag, defs in sorted(flags.items()):
        if not defs:
            nodef.append(flag)
            continue
        live = [d for d in defs if re.search(r'\b%s\b' % re.escape(d), blob)]
        if live:
            ok.append((flag, defs))
        else:
            dead.append((flag, defs))

    if not quiet:
        print("FLAG AUDIT\n")
        print("  %d Makefile flags carry a -D; %d of those are referenced in "
              "the sources." % (len(ok) + len(dead), len(ok)))
        if nodef:
            print("\n  %d flags add no -D (object lists, sub-make vars, "
                  "value flags) -- not audited here:" % len(nodef))
            print("    " + ", ".join(nodef[:24])
                  + (" ..." if len(nodef) > 24 else ""))

    if dead:
        print("\n  DEAD FLAGS (%d) -- the define is referenced nowhere:" % len(dead))
        for flag, defs in dead:
            print("    %-22s adds %s" % (flag, ", ".join("-D" + d for d in defs)))

    # INERT STATE: a static array read by a guard but never written.
    print("\n  INERT-STATE SCAN (static arrays read but never assigned):")
    inert = []
    for path, text in sorted(src.items()):
        for m in re.finditer(r'^\s*static\s+(?:volatile\s+)?\w+\s+(\w+)\s*\[', text, re.M):
            name = m.group(1)
            reads = len(re.findall(r'\b%s\s*\[' % re.escape(name), text))
            writes = len(re.findall(
                r'\b%s\s*\[[^\]]*\]\s*(?:=[^=]|\+\+|--|[-+*/|&^]=)' % re.escape(name), text))
            writes += len(re.findall(r'\+\+\s*%s\s*\[' % re.escape(name), text))
            if reads >= 2 and writes == 0:
                inert.append((path, name, reads))
    if inert:
        for path, name, reads in inert:
            print("    %-28s %-22s %d reads, 0 writes  <-- pal_streak class"
                  % (path, name, reads))
    else:
        print("    none found.")

    if dead:
        return 1
    print("\n  no dead flags.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
