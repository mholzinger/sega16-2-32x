#!/usr/bin/env python3
"""LOOP 17 JOB 2 — bake the discovered sprite frames into ROM.

    tools/bake_sprites.py [discover-dir-or-csv ...]     (default: discover/)

Takes the unique decode jobs found by tools/sprite_discover.lua and
pre-decodes each one OFFLINE, exactly the way compose_sprites decodes
it live, into a copy-friendly blob plus an open-addressed hash index:

    sh_src/sprbake.bin      the blob (index table first, then frames)
    sh_src/sprbake.h        sizes/mask/format constants for m_main.c

THE ACCURACY GATE RUNS BEFORE THE ROM EXISTS. Every baked frame is
re-decoded through a Python port of the live algorithm and replayed
through a Python port of the fast path, into two row buffers, at a
sweep of x positions (left-clipped, aligned, right-clipped). They must
be byte-identical or this script exits non-zero and no ROM links.

WHAT IS BAKED, AND WHAT IS NOT
  Baked: native-zoom frames only ((d5 & 0x3FF) == 0). The scaling pipe
  keeps the live decoder -- zoom changes which nibbles land on which x,
  so a zoomed frame is not a copy of anything.
  Not baked into the pixels: the colour set. The blob stores raw pen
  values 1..14 and the runtime adds `base` (spr_pair[par][col] << 4),
  which is what lets one baked frame serve every palette pair.
  The fast path is also only valid for UNGATED, NON-SHADOW sprites
  (pp >= 2, colour != 0x3F): gated sprites test the destination pixel
  per pen and shadow sprites read it. Those stay live. This checker
  proves the ungated non-shadow native case and nothing else.

FORMAT (little-endian is NOT used -- SH-2 here is big-endian, and the
blob is read as u16/u8 straight out of ROM)

  header   u32 magic 'SPBK'
           u16 version
           u16 slots            index slots, a power of two
           u32 frames_off       byte offset of the first frame record
           u32 blob_bytes
  index    slots x 12 bytes: u16 addr, u16 d2, u16 (bank<<8|height),
                             u16 pad, u32 frame_off (0xFFFFFFFF = empty)
  frame    u16 rows
           u16 flags           bit0 flip (informational -- the pen
                               sequence is already in DRAW order, so
                               the runtime never re-flips)
           u16 bytes           size of this record
           u16 pad
           u16 row_off[rows]   byte offset from the record start
           row payloads, each 2-byte aligned:
               u16 nsegs
               nsegs x { u8 skip, u8 len, u8 pen[len] }
           skip advances x, len pens are written as base+pen. len may
           be 0 (a skip longer than 255 chains through extra segments).
           Trailing transparent pixels are dropped: nothing is drawn
           there, so replaying them would only cost cycles.
"""
import csv
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPRITES = ROOT / 'sh_src' / 'sprites.bin'
OUT_BIN = ROOT / 'sh_src' / 'sprbake.bin'
OUT_H = ROOT / 'sh_src' / 'sprbake.h'
BUDGET = 768 * 1024

MAGIC = 0x5350424B          # 'SPBK'
VERSION = 1
SLOT_BYTES = 12
EMPTY = 0xFFFFFFFF

# compose_sprites' own screen geometry (m_main.c): row[] is indexed by
# sx = x - 184, and only sx < 320 lands on screen.
X_ORIGIN = 184
SCREEN_W = 320
X_LIMIT = 504               # the 1:1 loop bound, tested per WORD


def load_rom():
    if not SPRITES.exists():
        sys.exit(f'{SPRITES} missing -- run: python3 tools/gen_sprites.py')
    data = SPRITES.read_bytes()
    # word i == MAME spritedata[i], 16-bit big-endian
    return memoryview(data).cast('H') if sys.byteorder == 'big' else data


class Rom:
    """Word-addressed view of the interleaved sprite ROM, banked in
    64K-word pages exactly like `altbeast_sprites + bank * 0x10000`."""

    def __init__(self, raw):
        self.raw = raw

    def word(self, bank, off):
        i = (bank * 0x10000 + (off & 0xFFFF)) * 2
        return (self.raw[i] << 8) | self.raw[i + 1]


def decode_row(rom, bank, o, flip):
    """One strip row, walked like the sprite chip (compose_sprites NIB
    loops). Returns the pen sequence in DRAW order: index j is the pen
    at x = xpos + j. 0 = transparent, 15 = the end marker (kept so the
    caller can see the exact extent), 1..14 = drawn."""
    pens = []
    for _ in range(128):
        w = rom.word(bank, o)
        if not flip:
            nib = [(w >> 12) & 0xF, (w >> 8) & 0xF, (w >> 4) & 0xF, w & 0xF]
            o = (o + 1) & 0xFFFF
        else:
            nib = [w & 0xF, (w >> 4) & 0xF, (w >> 8) & 0xF, (w >> 12) & 0xF]
            o = (o - 1) & 0xFFFF
        pens.extend(nib)
        # the row ends when the LAST-walked nibble of a word is 0xF;
        # mid-word F nibbles are ordinary skipped pixels
        if nib[3] == 15:
            break
    return pens


def decode_frame(rom, bank, addr, pitch, flip, height):
    """Every row of a native-zoom strip. Row r reads from addr+pitch*(r+1)
    -- compose_sprites advances addr BEFORE drawing each row, and with
    vzoom 0 the accumulator never carries.

    This line SHIPPED WRONG once: `range(1, height+1)` with pitch*(r+1)
    starts at addr+2*pitch, so every baked row was the live decoder's
    NEXT row. It survived the checker because the checker fed the same
    wrong row list to both renderers -- it proved the FORMAT round-trips
    and never proved the ADDRESSING. That is why check_frame below
    re-derives the live rows from the record fields instead."""
    return [decode_row(rom, bank, (addr + pitch * (r + 1)) & 0xFFFF, flip)
            for r in range(height)]


# ---------------------------------------------------------------- live
def render_live(pens, xpos, base, width=SCREEN_W + 64):
    """A faithful port of the live 1:1 ungated non-shadow path: pen is
    drawn iff (pen-1) < 14, x advances for every nibble including the
    skipped ones, and the loop bound x < 504 is tested per WORD (four
    nibbles), not per pixel."""
    row = bytearray(width)
    x = xpos
    for j, pen in enumerate(pens):
        if j % 4 == 0 and x >= X_LIMIT:
            break                       # word-granular bound, as in C
        if 1 <= pen <= 14:
            sx = x - X_ORIGIN
            if 0 <= sx < SCREEN_W:
                row[sx] = (base + pen) & 0xFF
        x += 1
    return row


# ---------------------------------------------------------------- bake
def encode_row(pens):
    """Pen sequence -> segment list. Drops trailing transparency."""
    last = -1
    for j, pen in enumerate(pens):
        if 1 <= pen <= 14:
            last = j
    if last < 0:
        return b'', 0
    segs = bytearray()
    n = 0
    j = 0
    while j <= last:
        skip = 0
        while j <= last and not (1 <= pens[j] <= 14) and skip < 255:
            skip += 1
            j += 1
        run = bytearray()
        while j <= last and 1 <= pens[j] <= 14 and len(run) < 255:
            run.append(pens[j])
            j += 1
        segs.append(skip)
        segs.append(len(run))
        segs += run
        n += 1
    return bytes(segs), n


def render_baked(rec, r, xpos, base, width=SCREEN_W + 64):
    """A faithful port of the fast path in compose_sprites, reading the
    EMITTED RECORD BYTES the same way the SH-2 does: row_off[r] out of
    the header table, then u16 nsegs, then the segment stream. Parsing
    the real bytes is the point -- the previous version replayed a
    python object and so could not catch a bad header or a bad offset."""
    off = struct.unpack_from('>H', rec, 8 + 2 * r)[0]
    nsegs = struct.unpack_from('>H', rec, off)[0]
    p = off + 2
    row = bytearray(width)
    x = xpos
    drawn = 0
    for _ in range(nsegs):
        skip, ln = rec[p], rec[p + 1]
        p += 2
        x += skip
        drawn += skip
        for k in range(ln):
            if (drawn & 3) == 0 and x >= X_LIMIT:
                return row              # word-granular bound preserved
            pen = rec[p + k]
            sx = x - X_ORIGIN
            if 0 <= sx < SCREEN_W:
                row[sx] = (base + pen) & 0xFF
            x += 1
            drawn += 1
        p += ln
    return row


def decode_frame_zoomed(rom, bank, addr, pitch, flip, height, vz, hz):
    """SCALEBAKE (2026-09-01): the zoomed strip, rows via the CLOSED
    FORM the runtime documents as bit-exact with its per-row yacc loop
    (source_row(r) = r + ((r*(vz<<10))>>15)), pixels via the ZNIB xacc
    chain (xacc=(xacc&0x3F)+hz per source nibble, keep when <0x40 —
    kept pixels are DENSE; pen 0/15 kept cells stay transparent). The
    raw pen stream from decode_row already carries the word-boundary
    15-terminator semantics the live loop breaks on."""
    rows = []
    for r in range(height):
        sr = r + ((r * (vz << 10)) >> 15)
        pens = decode_row(rom, bank, (addr + pitch * (sr + 1)) & 0xFFFF,
                          flip)
        out = []
        xacc = 4 * hz
        for pp in pens:
            xacc = (xacc & 0x3F) + hz
            if xacc < 0x40:
                out.append(pp)
        rows.append(out)
    return rows


def check_frame_zoomed(rom, e, rec):
    """The zoom accuracy gate: the LIVE side transcribes the runtime's
    ITERATIVE loops (per-row yacc carries + ZNIB chain); the baked side
    used the closed form. Independent derivations must agree pixel-for-
    pixel over the displayed columns."""
    vz, hz = (e['zoom'] >> 5) & 0x1F, e['zoom'] & 0x1F
    bad = 0
    live = []
    a = e['addr']
    yacc = 0
    for r in range(e['height']):
        a = (a + e['pitch']) & 0xFFFF
        pens = decode_row(rom, e['bank'], a, e['flip'] == 1)
        out = []
        xacc = 4 * hz
        for pp in pens:
            xacc = (xacc & 0x3F) + hz
            if xacc < 0x40:
                out.append(pp)
        live.append(out)
        yacc += vz << 10
        if yacc & 0x8000:
            a = (a + e['pitch']) & 0xFFFF
            yacc &= 0x7FFF
    for xpos in (100, 184, 300, 496):
        for r in range(e['height']):
            if render_live(live[r], xpos, 0x30) != \
                    render_baked(rec, r, xpos, 0x30):
                bad += 1
    return bad, len(live) * 4


def check_frame(rom, e, rec):
    """THE ACCURACY GATE. Renders the whole sprite twice and compares
    every row:
      live  -- walks rows the way compose_sprites does, advancing addr by
               pitch BEFORE each row, deriving everything from the record
               FIELDS (bank, addr, pitch, flip, height);
      baked -- parses the emitted record bytes through the fast path.
    Deriving the live side independently is what makes this a gate: an
    error in decode_frame's row addressing cannot cancel out, because the
    live side never touches decode_frame.
    Compared over the DISPLAYED columns only (sx 0..319). The live
    NIB_NC variant can also write sx 320..323, but blit_half ships 320
    columns and the priority gate reads sx < 320, so those bytes reach
    neither the screen nor any later read."""
    bad = 0
    a = e['addr']
    live = []
    for r in range(e['height']):
        a = (a + e['pitch']) & 0xFFFF
        live.append(decode_row(rom, e['bank'], a, e['flip'] == 1))
    for xpos in (100, 184, 300, 496):
        for r in range(e['height']):
            if render_live(live[r], xpos, 0x30) != \
                    render_baked(rec, r, xpos, 0x30):
                bad += 1
    return bad, len(live) * 4


def build_frame(rom, e):
    if e.get('zoom'):
        rows = decode_frame_zoomed(rom, e['bank'], e['addr'], e['pitch'],
                                   e['flip'], e['height'],
                                   (e['zoom'] >> 5) & 0x1F,
                                   e['zoom'] & 0x1F)
    else:
        rows = decode_frame(rom, e['bank'], e['addr'], e['pitch'],
                            e['flip'], e['height'])
    enc = [encode_row(p) for p in rows]
    n = len(rows)
    body = bytearray()
    offs = []
    head = 8 + 2 * n
    if head & 1:
        head += 1
    for segs, nsegs in enc:
        if len(body) & 1:
            body.append(0)
        offs.append(head + len(body))
        body += struct.pack('>H', nsegs)
        body += segs
    if len(body) & 1:
        body.append(0)
    total = head + len(body)
    rec = bytearray()
    rec += struct.pack('>HHHH', n, e['flip'] & 1, total, 0)
    for o in offs:
        rec += struct.pack('>H', o)
    while len(rec) < head:
        rec.append(0)
    rec += body
    assert len(rec) == total, (len(rec), total)
    return bytes(rec), rows, enc


# --------------------------------------------------------------- index
def hash_key(addr, d2, bank, zoom, mask):
    """One 32-bit multiply and a shift -- the SH-2 has MUL.L, so this is
    a handful of cycles, and the key packs into a word with no loss.

    The obvious cheap alternative (two 16x16 mulu.w folded into 16 bits)
    was measured on the real key set and clusters badly: max probe 12 at
    1024 slots vs 3 here. Sprite addresses are not random -- frames of
    one animation sit a few words apart and share d2 exactly -- so the
    hash has to mix the LOW bits upward, which is what the golden-ratio
    multiply does."""
    # HEIGHT-TOLERANT (2026-09-01): height is OUT of the hash and the
    # match — same (addr,d2,bank) at different heights is the same row
    # walk (decode_frame is per-row independent), so a shorter draw is
    # a row-PREFIX of the tallest bake. Only the max-height variant is
    # emitted; the index carries its row count and the runtime accepts
    # any request <= it. Kills the clipped-variant miss class (the
    # 19% boss-fight residual) with NEGATIVE art cost.
    k = (addr | (d2 << 16)) ^ (bank << 28) ^ (zoom << 19)
    k = (k * 0x9E3779B1) & 0xFFFFFFFF
    return (k >> 18) & mask


def load_keys(paths):
    if not paths:
        paths = [str(ROOT / 'discover')]
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p)
                            if f.endswith('.csv'))
        else:
            files.append(p)
    if not files:
        sys.exit('no discovery CSVs found -- run tools/sprite_discover.sh')
    out = {}
    for f in files:
        with open(f) as fh:
            for r in csv.DictReader(fh):
                if int(r['native']) != 1:
                    continue
                k = r['key']
                if k in out:
                    out[k]['count'] += int(r['count'])
                    continue
                out[k] = {'key': k,
                          'bank': int(r['bank']),
                          'addr': int(r['addr'], 16),
                          'd2': int(r['w2'], 16),
                          'pitch': int(r['pitch']),
                          'flip': int(r['flip']),
                          'height': int(r['height']),
                          'zoom': int(r.get('w5', '0'), 16) & 0x3FF,
                          'count': int(r['count'])}
    return out, files


def main():
    keys, files = load_keys(sys.argv[1:])
    rom = Rom(load_rom())
    print(f'discovery: {len(files)} file(s), {len(keys)} native frames')

    # bake most-seen first: if the budget ever does bite, the tail is
    # what falls off, and the tail is the cheapest thing to lose
    frames = sorted(keys.values(), key=lambda e: -e['count'])

    recs, checked, mismatch = [], 0, 0
    for e in frames:
        rec, _rows, _enc = build_frame(rom, e)
        e['rec'] = rec
        recs.append(e)
        bad, n = (check_frame_zoomed if e.get('zoom')
                  else check_frame)(rom, e, rec)
        checked += n
        mismatch += bad
        if bad and mismatch <= 5:
            print(f'  MISMATCH {e["key"]}: {bad}/{n} row renders')
    if mismatch:
        sys.exit(f'PIXEL-IDENTITY FAILED: {mismatch}/{checked} rows differ')
    print(f'pixel identity: {checked} row renders, 0 mismatches')

    # 4x the frame count (load factor <= 0.25): the index is 24 KB at
    # this size, and halving it to 12 KB costs 3 probes -> 8 in the
    # worst case. Probes are per SPRITE per CYCLE; the KB is free.
    slots = 1
    while slots < len(recs) * 4:
        slots <<= 1
    mask = slots - 1
    # the SH-2 lookup terminates on the empty slot; that only holds if
    # one exists on every chain, which a strictly-under-full table
    # guarantees. bake_find's trip bound is the belt, this is the braces.
    assert len(recs) < slots, 'index full: bake_find would not terminate'
    idx_off = 16
    frames_off = idx_off + slots * SLOT_BYTES

    # keep only the tallest variant per (addr,d2,bank)
    tall = {}
    for e in recs:
        a = (e['addr'], e['d2'], e['bank'], e.get('zoom', 0))
        if a not in tall or e['height'] > tall[a]['height']:
            tall[a] = e
    dropped_variants = len(recs) - len(tall)
    recs = list(tall.values())
    print(f'height-dedupe: {dropped_variants} shorter variants folded '
          f'into their tallest frames')
    body = bytearray()
    placed = []
    for e in recs:
        if len(body) & 3:
            body += b'\0' * (4 - (len(body) & 3))
        placed.append((e, frames_off + len(body)))
        body += e['rec']

    index = bytearray(b'\xFF' * (slots * SLOT_BYTES))
    probes_max, probes_tot = 0, 0
    for e, off in placed:
        h = hash_key(e['addr'], e['d2'], e['bank'], e.get('zoom', 0),
                     mask)
        p = 0
        while struct.unpack('>I', bytes(
                index[h * SLOT_BYTES + 8:h * SLOT_BYTES + 12]))[0] != EMPTY:
            h = (h + 1) & mask
            p += 1
        probes_tot += p
        probes_max = max(probes_max, p)
        s = h * SLOT_BYTES
        index[s:s + SLOT_BYTES] = struct.pack(
            '>HHHHI', e['addr'], e['d2'],
            (e['bank'] << 8) | (e['height'] & 0xFF),
            e.get('zoom', 0), off)

    blob = bytearray()
    blob += struct.pack('>IHHII', MAGIC, VERSION, slots, frames_off, 0)
    blob += index
    blob += body
    struct.pack_into('>I', blob, 12, len(blob))
    OUT_BIN.write_bytes(blob)

    OUT_H.write_text(f"""/* generated by tools/bake_sprites.py -- do not edit */
#ifndef SPRBAKE_H
#define SPRBAKE_H
#define SPRBAKE_MAGIC     0x{MAGIC:08X}u
#define SPRBAKE_VERSION   {VERSION}
#define SPRBAKE_SLOTS     {slots}
#define SPRBAKE_MASK      {mask}
#define SPRBAKE_SLOT_SZ   {SLOT_BYTES}
#define SPRBAKE_FRAMES    {len(recs)}
#define SPRBAKE_BYTES     {len(blob)}
#define SPRBAKE_MAXPROBE  {probes_max}
#endif
""")

    idx_kb = (slots * SLOT_BYTES) / 1024.0
    print(f'index:  {slots} slots ({idx_kb:.1f} KB), '
          f'max probe {probes_max}, mean {probes_tot / max(len(recs), 1):.2f}')
    print(f'frames: {len(recs)} records, {len(body) / 1024.0:.1f} KB')
    print(f'blob:   {len(blob) / 1024.0:.1f} KB of {BUDGET / 1024:.0f} KB '
          f'free -> {"FITS" if len(blob) <= BUDGET else "OVER BUDGET"}')
    print(f'wrote {OUT_BIN.relative_to(ROOT)}, {OUT_H.relative_to(ROOT)}')
    if len(blob) > BUDGET:
        sys.exit('OVER BUDGET')


if __name__ == '__main__':
    main()
