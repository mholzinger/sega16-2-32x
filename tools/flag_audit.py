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
    # Regex cannot do this: indices nest (`pal_retry32[pal_blk[j] >> 3] =`)
    # and writes happen through `&name[...]` pointers. The first cut used
    # `[^\]]*` and reported three writes-that-exist as inert. Match
    # brackets properly instead.
    print("\n  INERT-STATE SCAN (static arrays read but never assigned):")
    inert = []
    for path, text in sorted(src.items()):
        for m in re.finditer(r'^\s*static\s+(?:volatile\s+)?\w+\s+(\w+)\s*\[',
                             text, re.M):
            name = m.group(1)
            # A macro alias hides the write: `#define RG_ACC(b) (rg_acc[b])`
            # is written as `RG_ACC(0)[i] |= d[i]`, which never mentions
            # rg_acc. If the name appears in ANY #define body, something
            # else may write it and we cannot tell. Conservative on purpose.
            if re.search(r'^\s*#\s*define\s+\w+(?:\([^)]*\))?[^\n]*\b%s\b'
                         % re.escape(name), text, re.M):
                continue
            reads = writes = escapes = 0
            for occ in re.finditer(r'(?<![\w.>])(&?)%s\b' % re.escape(name), text):
                i = occ.end()
                if occ.group(1) == "&":
                    escapes += 1
                    continue
                if i >= len(text) or text[i] != "[":
                    # bare mention: decays to a pointer, may be written afar
                    if text[occ.start()-1:occ.start()] not in ".>":
                        escapes += 1
                    continue
                depth = 0
                while i < len(text):
                    if text[i] == "[":
                        depth += 1
                    elif text[i] == "]":
                        depth -= 1
                        if depth == 0 and (i + 1 >= len(text) or text[i+1] != "["):
                            break
                    i += 1
                j = i + 1
                while j < len(text) and text[j] in " \t":
                    j += 1
                nxt = text[j:j+2]
                if (nxt[:1] == "=" and nxt[1:2] != "=") or \
                   nxt in ("++", "--", "+=", "-=", "*=", "/=", "|=", "&=", "^="):
                    writes += 1
                else:
                    reads += 1
            if reads >= 2 and writes == 0 and escapes == 0:
                inert.append((path, name, reads))
    if inert:
        for path, name, reads in inert:
            print("    %-24s %-18s %2d reads, 0 writes, address never taken"
                  % (path, name, reads))
            print("      ^ pal_streak class: a guard reading this is false forever")
    else:
        print("    none. (Every static array is written, or its address "
              "escapes so something else may write it.)")

    if dead:
        return 1
    print("\n  no dead flags.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
