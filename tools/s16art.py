"""s16art.py: the System 16 sprite art rule, shared by the census and bake tools.

One decoder for everything that reads a sprite frame off the sprite ROM the way the slave's
plotter does (sh_src/r60/r60_main.c PLOT5/ROWFAST; jts16_obj_scan.v:78, jts16_obj_draw.v:115):

  row r of a record reads words from addr + pitch*(r+1), mod 64K inside the record's bank
  (words (bank&7)*0x10000 ..); forward for an unflipped record, backward with the nibble
  order reversed for a flipped one; four pens a word, high nibble first; pens 0 and 15 draw
  nothing and the row ends after the word whose LAST drawn nibble is 15. Pixel k of a row
  lands at screen x + k. 256 words is the safety cap a runaway row stops at.

A frame is keyed by (bank, addr, pitch, flip): its rows are independent, so a bake of the
tallest height ever drawn serves every shorter record of the same key (rows 0..h-1).

The baked copy format ("groups"), per frame:
  u16 h          rows baked
  u16 w          1 + the highest opaque pixel index over the rows (the box width)
  rows, each:
    u8  first4   pixel index of the first group / 4 (0 for an empty row)
    u8  ngroups  longword groups that follow (0 = nothing drawn on this row; a row past 255 is
                 not bakeable: pack_frame raises)
    u16 nwords   ROM words the plotter reads on this row (to the terminator): the copy needs it
                 for the row's end x, which the span table and the carry rule depend on
    ngroups x 4 bytes, one raw pen a byte (1..14; 0 = transparent inside the span)
The runtime adds the record's 32X bank << 4 only to nonzero pens. Zero must
remain byte zero so the overwrite image preserves any earlier sprite underneath.
"""
import struct


def load_rom(path):
    rom = open(path, 'rb').read()
    return struct.unpack('>%dH' % (len(rom) // 2), rom)


def decode_scaled(W, bank, addr, pitch, flip, h, hzoom, vzoom):
    """Decode scaled raw pens without palette or screen placement.

    Derived from jts16_obj_scan.v:78-80 and jts16_obj_draw.v:70-93.
    The vertical carry applies before drawing the current row. Horizontal
    acceptance is the closed-form difference between successive six-bit carries.

    Return rows, source-word total, source words per row. Pixel end positions
    are len(row), NOT source_words*4: the existing unscaled packet consumer
    cannot consume these rows without an explicit extent-format extension.
    """
    if not (0 <= bank < 8 and -128 <= pitch <= 127 and 0 <= h <= 255
            and 0 <= hzoom < 32 and 0 <= vzoom < 32):
        raise ValueError('invalid System16B scaled-frame descriptor')
    if len(W) < (bank + 1) * 65536:
        raise ValueError('sprite ROM does not contain the requested physical bank')
    rows, rowwords = [], []
    seed = (hzoom & 15) * 4
    shifts = (0, 4, 8, 12) if flip else (12, 8, 4, 0)
    for r in range(h):
        source_row = r + 1 + (r + 1) * vzoom // 32
        start = (addr + pitch * source_row) & 65535
        pixels = []
        for n in range(256):
            word = int(W[bank * 65536 + ((start + (-n if flip else n)) & 65535)])
            for j, shift in enumerate(shifts):
                k = n * 4 + j
                if (seed + (k + 1) * hzoom) // 64 == (seed + k * hzoom) // 64:
                    pen = (word >> shift) & 15
                    pixels.append(pen if 0 < pen < 15 else 0)
            if (word >> shifts[-1]) & 15 == 15:
                break
        else:
            raise ValueError('scaled sprite row lacks a terminator in 256 words')
        rows.append(pixels)
        rowwords.append(n + 1)
    return rows, sum(rowwords), rowwords


def decode(W, bank, addr, pitch, flip, h):
    """rows of pen lists (0 = transparent), the ROM words the plotter reads in all, and the
    words it reads per row"""
    base = (bank & 7) * 0x10000
    rows = []
    rownw = []
    words = 0
    a = addr
    for _ in range(h):
        a = (a + pitch) & 0xFFFF
        o = a
        px = []
        nw0 = words
        for _k in range(256):
            w = W[base + (o & 0xFFFF)]
            if flip:
                nib = [w & 15, (w >> 4) & 15, (w >> 8) & 15, w >> 12]
                o -= 1
            else:
                nib = [w >> 12, (w >> 8) & 15, (w >> 4) & 15, w & 15]
                o += 1
            px += [p if 1 <= p <= 14 else 0 for p in nib]
            words += 1
            if nib[3] == 15:
                break
        rows.append(px)
        rownw.append(words - nw0)
    return rows, words, rownw


def row_span(px):
    """(first, ngroups) of the opaque span padded to longword groups, (0, 0) when empty"""
    nz = [i for i, p in enumerate(px) if p]
    if not nz:
        return 0, 0
    first = nz[0] & ~3
    last = (nz[-1] + 4) & ~3
    return first, (last - first) // 4


def size_groups(rows):
    n = 0
    for px in rows:
        n += 4
        _f, g = row_span(px)
        n += g * 4
    return n


def pack_frame(rows, rownw):
    """the frame's bytes in the groups format; ValueError when a row cannot be described"""
    w = 0
    out = []
    for px, nw in zip(rows, rownw):
        first, g = row_span(px)
        if g > 255 or first > 1020 or nw > 65535:
            raise ValueError('row too wide for the groups header')
        if g:
            w = max(w, first + g * 4)
            span = px[first:first + g * 4]
            span += [0] * (g * 4 - len(span))
        else:
            span = []
        out.append(struct.pack('>BBH', first >> 2, g, nw) + bytes(span))
    return struct.pack('>HH', len(rows), w) + b''.join(out)


def unpack_frame(blob, off):
    """inverse of pack_frame: (rows as full-width pen lists, words per row, bytes consumed)"""
    h, w = struct.unpack_from('>HH', blob, off)
    off += 4
    rows = []
    rownw = []
    for _ in range(h):
        first4, g, nw = struct.unpack_from('>BBH', blob, off)
        first = first4 * 4
        off += 4
        px = [0] * (first + g * 4)
        px[first:first + g * 4] = blob[off:off + g * 4]
        off += g * 4
        rows.append(px)
        rownw.append(nw)
    return rows, rownw, off


def records(block):
    """the live records of one 1 KB sprite RAM dump: (key, h, zoomed) per drawn record"""
    out = []
    for i in range(64):
        e = struct.unpack_from('>8H', block, i * 16)
        if e[2] & 0x8000:
            break
        if e[2] & 0x4000:
            continue
        top, bot = e[0] & 0xFF, e[0] >> 8
        if top >= bot:
            continue
        pitch = struct.unpack('b', bytes([e[2] & 0xFF]))[0]
        x = (e[1] & 0x1FF) - 184
        if x >= 320 or x < -4 * abs(pitch) - 8:
            continue
        key = ((e[4] >> 8) & 7, e[3], pitch, 1 if e[2] & 0x100 else 0)
        out.append((key, min(bot, 224) - top, bool(e[5] & 0x3FF)))
    return out
