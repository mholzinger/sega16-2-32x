# -*- coding: utf-8 -*-
"""ONE resolver for Ghidra symbol names, shared by cport_prep.py and
cport_layout.py.

WHY THIS FILE EXISTS. A ROM-resident table that is not PLACED at its true address
gets DEFINED as a 64-byte zeroed array in defs.c, so the port reads zeros where the
68K reads a jump table or a character table. That bug was found and fixed on
2026-09-28 (208 indirect calls a frame resolving to address 0) by matching names
against docs/audit/sym_addrs.txt -- and the match was EXACT STRING EQUALITY, which
leaks in two ways Ghidra produces on its own:

  1. `+` IS NOT A C IDENTIFIER CHARACTER. The listing calls a symbol
     `PTR_LAB_00010202+1_00001cda`; the decompiler emits
     `PTR_LAB_00010202_1_00001cda`. Same address, two spellings, no match.
  2. THE PREFIX FAMILY DISAGREES WITH ITSELF. The listing says
     `PTR_DAT_00006dc0` where the decompiler says `PTR_FUN_00006dc0` -- Ghidra
     decided the pointed-to thing was data in one view and a function in the
     other. Same address, no match.

Six tables fell through both holes, measured 2026-10-02. One of them,
`PTR_LAB_00010202_1_00001cda` at ROM 0x1CDA, is read by the MAIN LOOP'S PROLOGUE:

    _DAT_fffff142 = CONCAT11(*(undefined *)((int)&PTR_LAB_00010202_1_00001cda
                              + (int)(short)(_DAT_fffff14e >> 8 & 7)), DAT_fffff143);

(0x662 in prog68k.asm: `lea %pc@(0x1cda),%a0`). `0xFFFFF142` is the mode byte the
frame loop branches on -- `cmpi.b #6,(0xfffff142).w`. Reading zeros there starts
the port's state machine in the wrong state.

SO MATCH ON THE ADDRESS, NOT THE NAME. Every one of these names ends in its own
address; that is the stable key.
"""
import re

#: name prefixes whose symbol lives in program ROM and must never be fabricated
ROM_TABLE_RE = re.compile(r'^(PTR_|switchD?_|LAB_|s_)')

#: a wild-jump class: reading zeros here dispatches to address 0
JUMP_TABLE_RE = re.compile(r'^(PTR_|switchD?_|LAB_)')


def c_ident(name):
    """the spelling a C compiler sees: every non-identifier character becomes _"""
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def addr_of(name):
    """the ROM address encoded in the TRAILING `_<hex>` of a Ghidra symbol name.

    Ghidra appends the symbol's OWN address last; any address earlier in the name
    is the thing it points AT (`PTR_LAB_00010202+1_00001cda` is at 0x1CDA and
    points near 0x10202). Returns None when the name carries no address -- which
    happens when Ghidra truncates a long string literal's name
    (`s_TAKYOUR_NAME__`), and is the one case that cannot be resolved from the
    name alone.
    """
    # ON A `DAT_`, A TRAILING `_N` IS A BYTE OFFSET, NOT A DISAMBIGUATOR. Ghidra names
    # a sub-component of a data item `<item>_<offset>`: DAT_fffff14e_1 is the SECOND
    # BYTE of the word at 0xFFF14E, i.e. 0xFFF14F. The unit proves it --
    #     _DAT_fffff14e = CONCAT11(DAT_fffff14e + '\x01', DAT_fffff14e_1);
    # CONCAT11 puts its first argument in the HIGH byte, so the low byte it preserves
    # has to be 0xFFF14F. Placed at 0xFFF14E instead (measured 2026-10-20, layout.ld
    # `PROVIDE(_DAT_fffff14e_1 = 0x0602FD4E)`), that line read the round number and
    # wrote the OLD round number back into 0xFFF14F on every round advance.
    #
    # A LABEL'S `_N` IS STILL A DISAMBIGUATOR (`LAB_00002686_2`, `LAB_00006d5a_2`) --
    # a second symbol at ONE address -- and must keep falling through to the stem.
    m = re.match(r'^_?DAT_([0-9a-fA-F]{4,8})_([0-9]{1,3})$', name)
    if m:
        return int(m.group(1), 16) + int(m.group(2))
    stem = re.sub(r'_[0-9]{1,3}$', '', name)
    for cand in (name, stem):
        m = re.search(r'_([0-9a-fA-F]{4,8})$', cand)
        if m:
            return int(m.group(1), 16)
    return None


def is_rom_table(name):
    return bool(ROM_TABLE_RE.match(name))


def is_jump_table(name):
    return bool(JUMP_TABLE_RE.match(name))


def load_sym_addrs(path):
    """docs/audit/sym_addrs.txt -> {address: [names]}, names C-sanitised.

    Keyed by ADDRESS so a caller can resolve a decompiler spelling that the
    listing never used.
    """
    byaddr = {}
    try:
        fh = open(path)
    except IOError:
        return byaddr
    with fh:
        for l in fh:
            if l.startswith('#'):
                continue
            f = l.split()
            if len(f) != 2:
                continue
            byaddr.setdefault(int(f[0], 16), []).append(c_ident(f[1]))
    return byaddr
