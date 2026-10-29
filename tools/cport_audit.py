#!/usr/bin/env python3
"""AUDIT the generated port unit for 68K idioms the generator may not handle.

    python3 tools/cport_audit.py [sh_src/portfull/altbeast_port.c]

WHY THIS EXISTS. Every port defect found so far has been a GENERATOR CLASS, not a
mistranslated function -- register passing, declared widths, addressing modes, the
dbra idiom, pointers in data. Each was discovered by a FAILING CELL pointing at it,
one at a time, which is the slowest possible order. This asks the question directly:
for each way the 68K can express something that C cannot express the same way, does
the generator handle it, and how many instances are in the unit?

A count here is EVIDENCE OF EXPOSURE, not proof of a bug. The point is to rank what
to look at, and to make a zero-count explicit so nobody re-derives it.
"""
import os, re, sys

src_path = sys.argv[1] if len(sys.argv) > 1 else 'sh_src/portfull/altbeast_port.c'
src = open(src_path).read()
# STRIP COMMENTS FIRST. Without this the audit counts its own prose: the division
# check matched file paths like `tools/cport_prep` and reported 151 divisions where
# the unit has 2.
src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
src = re.sub(r'//[^\n]*', ' ', src)
hdr = ''
for h in ('w68.h', 'dat_syms.h', 'pseudoregs.h', 'protos.h'):
    p = os.path.join(os.path.dirname(src_path), h)
    if os.path.exists(p):
        hdr += open(p).read()

def n(pat, text=None, flags=0):
    return len(re.findall(pat, text if text is not None else src, flags))

CHECKS = [
    # (name, count, handled?, note)
    ("register INPUTS still unplumbed (in_*/unaff_* reads)",
     n(r'\b(?:in|unaff)_[AD][0-7][a-z]*\b'),
     "PARTLY", "parameters declared for 549 functions; what is left is read as a global and returns 0"),
    ("register OUTPUTS still unplumbed (extraout_* reads)",
     n(r'\bextraout_[AD][0-7][a-z0-9_]*\b'),
     "PARTLY", "return storage declared for 47 producers"),
    ("condition codes carried across a CALL (SR/CCR pseudo-regs)",
     n(r'\b(?:in|unaff|extraout)_(?:SR|CCR|[NZVCX])\b'),
     "?", "a 68K subroutine CAN return its answer in the flags; Ghidra models flags per function"),
    ("BCD arithmetic (abcd/sbcd -- the score counters use it on many S16 titles)",
     n(r'\b(?:abcd|sbcd|bcd_add|__bcd)\b', src + hdr, re.I),
     "?", "if the program uses it and Ghidra rendered a plain add, scores drift silently"),
    ("division: quotient+remainder packed in one 32-bit result",
     n(r'\bCONCAT\d\d?\('),
     "PARTLY", "cport_prep normalises returns; CONCAT forms were truncated once (FULL-PORT 16)"),
    ("integer division / modulo at all",
     n(r'[^/*]/[^/*=]') + n(r'%[^s%d\)]'),
     "n/a", "the 68000's divu is 16-bit; a C int divide is not the same on overflow"),
    ("unsigned compared against -1 (the dbra idiom)",
     n(r'\(\s*\*\(\s*(?:ushort|byte|uint)\s*\*\)[^;]{0,120}?\)\s*[!=]=\s*-1\b'),
     "YES", "cport_prep masks by width; 0 left means the pass is working"),
    ("raw 68K ROM-range literal used as a pointer base",
     n(r'\*\(\s*\w+\s*\*\)\s*\(\s*0x[0-9a-f]{4,5}\s*\)'),
     "YES", "P68/ROM-base rewrite; a nonzero count is a miss"),
    # A SHAPE IS NOT A DISCRIMINATOR. The first version of this counted every
    # `(intvar + -0xNNNN)` whose 16-bit two's complement lands in 0xC000..0xFFFF and
    # reported 192 MISSES. All 192 were arithmetic: `sVar7 + -0xe0` is a coordinate
    # compare, and EVERY `x - small constant` lands in that range. Absolute-short
    # addressing is a DEREFERENCE whose base is an integer -- requiring the deref
    # takes it to 17 forms, 15 of them A-register displacements `(-d16,An)` and 2
    # pointer vars, so ZERO misses.
    ("absolute-short MISS: an integer base, DEREFERENCED, landing in work RAM",
     len([1 for b, o in re.findall(
          r'\*\s*\(\s*[A-Za-z_]\w*\s*\*\s*\)\s*\(\s*([A-Za-z_]\w*)\s*\+\s*-(0x[0-9a-fA-F]+)\s*\)', src)
          if not b.startswith('p')                    # pXxxVarN is a POINTER
             and not re.match(r'^(unaff_|in_|extraout_)?A[0-7]', b)
             and re.match(r'^[a-z]{1,3}Var\d+$', b)
             and 0xC000 <= 0x10000 - int(o, 16) <= 0xFFFF]),
     "YES", "a nonzero count is a real miss; the shape alone is not evidence"),
    ("CONCAT wider than 32 bits returned from a function declared `uint`",
     len([1 for m in re.finditer(r'^uint (\w+)\(', src, re.M)
          for cm in re.finditer(r'return\s+CONCAT(\d)(\d)\(',
                                src[m.end():src.find(chr(10) + '}', m.end())])
          if int(cm.group(1)) + int(cm.group(2)) > 4]),
     "YES", "the truncation FULL-PORT 16 hit; 10 functions now return ulonglong and 120 sites unpack with >> 0x20"),
    ("misaligned 32-bit access through the packed accessors",
     n(r'\bLD32[US]\(') + n(r'\bST32\('),
     "YES", "the SH-2 raises an address error where the 68000 allowed 2-byte alignment"),
    ("68K pointer loaded from DATA and translated at use",
     n(r'\bP68\('),
     "YES", "an untranslated one made floor_collide walk forever"),
    ("16-bit work-RAM pointer widened at use",
     n(r'\bW68\(') + n(r'\bW68PTR\('),
     "YES", "the 68000 stored 16 bits because its addressing mode sign-extended them"),
    ("indirect calls routed through the lookup",
     n(r'\bport_call68_at\('),
     "YES", "calling a 68K address directly is a wild jump"),
    ("functions still returning `undefined` (no declared return storage)",
     n(r'^undefined\d? \w+\(', src, re.M),
     "?", "a caller reading such a value gets whatever the SH-2 left"),
    ("`code *` values (a function pointer in data)",
     n(r'\bcode \*'),
     "?", "each must reach port_call68_at, not be called directly"),
]

print("GENERATED UNIT AUDIT -- %s" % src_path)
print("%-62s %8s  %-7s" % ("idiom", "count", "handled"))
print("-" * 92)
for name, cnt, handled, note in CHECKS:
    print("%-62s %8d  %-7s" % (name[:62], cnt, handled))
    print("    %s" % note)
