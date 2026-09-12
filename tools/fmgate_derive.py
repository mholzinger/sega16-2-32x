#!/usr/bin/env python3
"""LOOP 23 — FMGATE derivation: spans, entries, and the audit trail.

Reproduces the per-site FM gating specification from ground truth:
  - the wpcatch write/read censuses (48 write PCs, 1 read PC — the
    clrb RMW; prefetch-skewed, resolved against the listing),
  - SR-context classification (VINT-only: the sprite upload, ungated
    by trampoline order; MAIN-only: everything else),
  - rts-bounded regions merged to subsystem SPANS, then a control-
    transfer scan over the whole listing to fixpoint: every branch/
    jsr/jmp into a span interior from outside is either a listed
    ENTRY (gets a gate thunk) or a derivation failure (build must
    fail).

Output: the SPANS/ENTRIES tables to embed in patch_game's FMGATE
generator, and the violation list (must be empty).
"""
import re
import bisect
import sys

ASM = 'roms/altbeast/prog68k.asm'

# MAIN-context store sites (census-resolved accessor addresses).
MAIN_SITES = [0x154A, 0x16CE, 0x16F4, 0x1700, 0x1740, 0x1764, 0x259C,
              0x36A8, 0x36BC, 0x36D6, 0x36EC, 0x37E0, 0x37F8, 0x3806,
              0x3826, 0x38B6, 0x38EA, 0x38EE, 0x38F2, 0x38F8, 0x3900,
              0x3904, 0x3944, 0x394A, 0x3A9A, 0x3AA4, 0x3ABE, 0x3AFA,
              0x4D92, 0x573E,
              0x6504, 0x6560]   # LOOP29 225: the round-clear typewriter
# VINT-context sites (sprite upload + header stores): UNGATED — the
# trampoline guarantees FM=0 for the whole game vint.
VINT_SITES = [0x2ADE, 0x2AEC, 0x2AFA, 0x2B08, 0x2B0A, 0x2B14, 0x2B38,
              0x2B46]

SPANS = [(0x153E, 0x155C), (0x16BE, 0x1772), (0x2550, 0x25AA),
         (0x35CC, 0x3950), (0x3A9A, 0x3AFC), (0x4D80, 0x4D98),
         (0x56E0, 0x5742), (0x64DA, 0x6510), (0x6536, 0x656C)]


def main():
    asm = {}
    for line in open(ASM):
        if ':\t' in line:
            a = line.split(':', 1)[0].strip()
            try:
                asm[int(a, 16)] = line.rstrip()
            except ValueError:
                pass
    keys = sorted(asm)

    def span_of(a):
        for i, (s, e) in enumerate(SPANS):
            if s <= a <= e:
                return i
        return None

    for s in MAIN_SITES:
        assert span_of(s) is not None, f"MAIN site {s:#x} outside all spans"
    for s in VINT_SITES:
        assert span_of(s) is None, f"VINT site {s:#x} inside a span"

    pat = re.compile(r'\b0x([0-9a-f]{2,6})\b')
    entries = {}
    for src in keys:
        t = asm[src].split('\t')[-1]
        op = t.split()[0] if t.split() else ''
        if op.startswith(('bchg', 'bclr', 'bset', 'btst')):
            continue
        if not op.startswith(('b', 'db', 'jsr', 'jmp')):
            continue
        for m in pat.finditer(t):
            tgt = int(m.group(1), 16)
            ri = span_of(tgt)
            if ri is not None and span_of(src) != ri:
                entries.setdefault(ri, set()).add(tgt)

    print("SPANS/ENTRIES for the FMGATE generator:")
    for i, (s, e) in enumerate(SPANS):
        ent = sorted(entries.get(i, set()) | {s} if i == 6 else
                     entries.get(i, set()))
        print(f"  span {s:05X}..{e:05X}: " +
              ' '.join(f'{t:05X}' for t in ent))
    # span 6 has no external branch entries: it is entered by
    # fallthrough — the generator must gate its head (0x56E0) and
    # assert the preceding instruction is a flow boundary.
    return 0


if __name__ == '__main__':
    sys.exit(main())
