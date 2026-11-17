#!/usr/bin/env python3
"""Validate a non-LTO slave compositor object's declared closure before linking.

The only permitted fixed external is the SH unsigned-division helper. Linker
guards separately own placement/capacity; this catches closure escape that a
final size assertion cannot see. Only SH R_SH_DIR32 is supported. This checks
symbols and relocation records, not arbitrary encoded immediates or indirect
callback targets; final resolved instructions/literals still need an audit.
"""
import argparse
import json
from pathlib import Path
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(path, entry_symbol='_r60_sb_code_run'):
    data = Path(path).read_bytes()
    require(len(data) >= 52 and data[:6] == b'\x7fELF\x01\x02', 'expected big-endian ELF32 SH object')
    header = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    require(header[:2] == (1, 42), 'expected relocatable SH object, not linked/LTO output')
    require(header[10] == 40 and header[11] and header[12] < header[11], 'invalid section table')
    require(header[5]+header[10]*header[11] <= len(data), 'truncated section table')
    sections = [struct.unpack_from('>10I', data, header[5]+i*40) for i in range(header[11])]

    def body(section):
        require(section[4]+section[5] <= len(data), 'truncated section body')
        return data[section[4]:section[4]+section[5]]

    names = body(sections[header[12]])

    def string(table, offset):
        end = table.find(b'\0', offset)
        require(0 <= offset < len(table) and end >= offset, 'invalid ELF string')
        return table[offset:end].decode('ascii')

    labels = [string(names, s[0]) for s in sections]
    owned = set()
    allocated = {}
    for index, (name, section) in enumerate(zip(labels, sections)):
        require(not name.startswith('.gnu.lto'), 'LTO page object forbidden; compile with -fno-lto')
        if not section[2]&2 or not section[5]:
            continue
        require(not section[2]&1 and section[1] == 1, 'writable/BSS page section forbidden: '+name)
        require(name == '.r60wscomp.text' or name == '.text' or name.startswith('.text.')
                or name == '.rodata' or name.startswith('.rodata.'), 'orphan allocated page section: '+name)
        require(section[8] <= 64, 'unexpected closure section alignment: '+name)
        owned.add(index)
        allocated[name] = section[5]
    require(allocated.get('.r60wscomp.text', 0) > 0, 'page compositor entry section empty/missing')

    symbol_tables = {}
    undefined = set()
    entry = None
    for index, section in enumerate(sections):
        if section[1] != 2:
            continue
        require(section[6] < len(sections) and section[9] == 16 and not section[5]%16,
                'invalid symbol table')
        strings = body(sections[section[6]])
        raw = body(section)
        symbols = []
        for offset in range(0, len(raw), 16):
            name, value, size, info, other, target = struct.unpack_from('>IIIBBH', raw, offset)
            name = string(strings, name)
            symbols.append((name, value, size, info, target))
            require(target != 0xfff2, 'COMMON writable page symbol forbidden: '+name)
            require(target != 0xfff1 or (info&15 == 4 and info>>4 == 0),
                    'ABS closure symbol forbidden: '+name)
            if target == 0 and name:
                require(name == '___udivsi3' and info>>4 == 1, 'unexpected page external: '+name)
                undefined.add(name)
            if name == entry_symbol:
                require(entry is None, 'duplicate compositor entry')
                require(target in owned and labels[target] == '.r60wscomp.text'
                        and value == 0 and size > 0 and size <= sections[target][5]
                        and info&15 == 2 and info>>4 == 1,
                        'page compositor entry must be global function at first byte')
                entry = size
        symbol_tables[index] = symbols
    require(entry is not None, 'page compositor entry missing')
    relocations = 0
    for section in sections:
        if section[1] not in (4, 9) or section[7] not in owned:
            continue
        width = 12 if section[1] == 4 else 8
        require(section[6] in symbol_tables and section[9] == width and not section[5]%width,
                'invalid closure relocation table')
        raw = body(section)
        for offset in range(0, len(raw), width):
            at, info = struct.unpack_from('>II', raw, offset)
            symbols = symbol_tables[section[6]]
            require(info&255 == 1, 'unsupported SH closure relocation kind: '+str(info&255))
            source = sections[section[7]]
            require(info>>8 < len(symbols) and not at&3 and at+4 <= source[5], 'invalid closure relocation')
            name, value, _, _, target = symbols[info>>8]
            # Normal elf32-sh DIR32 is partial_inplace even in RELA files:
            # BFD adds the existing32-bit literal, symbol value and r_addend.
            # Model signed in-place addends without permitting32-bit wrap.
            literal = struct.unpack_from('>i', body(source), at)[0]
            addend = struct.unpack_from('>i', raw, offset+8)[0] if width == 12 else 0
            if target == 0 and name == '___udivsi3':
                require(value == literal == addend == 0, 'division helper addend must be zero')
            else:
                require(target in owned, 'page relocation escapes owned closure: '+name)
                resolved = value+literal+addend
                require(0 <= resolved < sections[target][5], 'owned relocation addend out of bounds: '+name)
            relocations += 1
    return {'allocated_sections': allocated, 'external_symbols': sorted(undefined),
            'entry_symbol': entry_symbol, 'entry_bytes': entry,
            'checked_relocations': relocations, 'non_LTO_readonly_closure': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('object', type=Path)
    parser.add_argument('--entry', choices=('_r60_sb_code_run', '_r60_sb_page_run',
                                             '_r60_tp_code_run',
                                             '_r60_mc_code_draw_packet_ready'),
                        default='_r60_sb_code_run', help='legacy name only for auditing frozen page prototypes')
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.object, args.entry), sort_keys=True))
    except (ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, 'workset code: '+str(error)+'\n')
