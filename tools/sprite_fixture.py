#!/usr/bin/env python3
"""Embed an oracle's sprite commands in a local renderer-only diagnostic.

Generated header is ROM-derived and must never be committed. This fixture
verifies rasterization, not gameplay, cadence, or normal palette delivery.
Usage: sprite_fixture.py SPRITES DEST [--bank 0|1] [NATIVE_PLANE_PREFIX].
The optional bank selector extracts one active table from a two-bank capture;
the optional fixed scene retains selected physical pages while the game adapter
supplies tile banking.
"""
import hashlib
import itertools
import struct
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    source, dest = map(Path, sys.argv[1:3])
    extra = sys.argv[3:]
    bank = None
    if '--bank' in extra:
        at = extra.index('--bank')
        if at + 1 >= len(extra):
            raise ValueError('--bank requires 0 or 1')
        bank = int(extra[at + 1], 0)
        del extra[at:at + 2]
        if bank not in (0, 1):
            raise ValueError('--bank requires 0 or 1')
    if len(extra) > 1:
        raise ValueError('too many fixture arguments')
    data = source.read_bytes()
    if len(data) not in (1024, 2048):
        raise ValueError('expected 1/2KiB oracle sprite RAM')
    if bank is not None:
        if len(data) != 2048:
            raise ValueError('--bank requires a two-bank capture')
        data = data[bank * 1024:(bank + 1) * 1024]
    tables = [data[i:i + 1024] for i in range(0, len(data), 1024)]
    lines = ['/* Local oracle sprite commands. SHA256 ' + hashlib.sha256(data).hexdigest() + ' */']
    for table_index, table in enumerate(tables):
        end = next((slot for slot in range(64)
                    if struct.unpack_from('>H', table, slot * 16 + 4)[0] & 0x8000),
                   None)
        if end is None:
            raise ValueError('oracle uses more than64records; refuse truncated fixture')
        # The renderer stops at the first terminator.  Do not embed stale words
        # after it: dense fixtures otherwise waste diagnostic ROM space.
        used = table[:(end + 1) * 16]
        words = struct.unpack('>' + str(len(used) // 2) + 'H', used)
        name = 'port_sprite_fixture' if table_index == 0 else 'port_sprite_fixture_next'
        lines.append('static const unsigned short ' + name + '[] = {')
        for i in range(0, len(words), 8):
            lines.append('    ' + ', '.join(f'0x{x:04x}' for x in words[i:i + 8]) + ',')
        lines.append('};\n')
    if len(tables) == 2:
        lines.append('#define PORT_SPRITE_FIXTURE_PAIR 1\n')
    if extra:
        prefix = extra[0]
        tilemap = Path(prefix + '_tilemap.bin').read_bytes()
        text = Path(prefix + '_text.bin').read_bytes()
        if len(tilemap) != 65536 or len(text) != 4096:
            raise ValueError('invalid native plane snapshot sizes')
        # A fixed scene can address only the pages selected by its fixed registers.
        # Blank other pages, then RLE the full 13-page shadow to fit the diagnostic ROM.
        selected = {((int.from_bytes(text[o:o+2], 'big') >> shift) & 15)
                    for o in (0x740 * 2, 0x741 * 2) for shift in (0, 4, 8, 12)}
        visible = bytearray(0xd000)
        for page in selected:
            if page < 13:
                visible[page*4096:(page+1)*4096] = tilemap[page*4096:(page+1)*4096]
        for label, raw in (('tilemap', visible), ('text', text)):
            values = struct.unpack('>' + str(len(raw) // 2) + 'H', raw)
            encoded = []
            literals = []
            def flush():
                if literals:
                    encoded.extend((0x8000 | len(literals), *literals))
                    literals.clear()
            for value, group in itertools.groupby(values):
                count = len(list(group))
                if count >= 3:
                    flush()
                    encoded.extend((count, value))
                else:
                    literals.extend([value] * count)
            flush()
            lines.append('/* ' + label + ' decoded SHA256 ' + hashlib.sha256(raw).hexdigest() + ' */')
            lines.append('static const unsigned short port_plane_fixture_' + label + '[] = {')
            for i in range(0, len(encoded), 8):
                lines.append('    ' + ', '.join(f'0x{x:04x}' for x in encoded[i:i + 8]) + ',')
            lines.append('};')
        lines.append('#define PORT_PLANE_FIXTURE 1')
        lines.append("""
static __attribute__((noinline)) void port_plane_fixture_unpack(
    const unsigned short *src, unsigned words, volatile unsigned short *dst)
{
    unsigned i = 0, j = 0, n, value;
    while (i < words) {
        n = src[i++];
        if (n & 0x8000u) {
            n &= 0x7fffu;
            while (n--) dst[j++] = src[i++];
        } else {
            value = src[i++];
            while (n--) dst[j++] = value;
        }
    }
}
static __attribute__((noinline)) void port_plane_fixture_apply(
    volatile unsigned short *tiles, volatile unsigned short *text)
{
    port_plane_fixture_unpack(port_plane_fixture_tilemap,
        sizeof(port_plane_fixture_tilemap)/sizeof(unsigned short), tiles);
    port_plane_fixture_unpack(port_plane_fixture_text,
        sizeof(port_plane_fixture_text)/sizeof(unsigned short), text);
}
""")
    dest.write_text('\n'.join(lines))


if __name__ == '__main__':
    main()
