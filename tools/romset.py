#!/usr/bin/env python3
"""Unpack a MAME ROM set into roms/<game>/ and interleave the 68K program.

    python3 tools/romset.py altbeast [--zipdir mame]

The release input: the user's own MAME zip, never committed. Accepts a merged set
(clones in subdirectories of the parent zip, e.g. altbeast.zip/altbeastj/) or a
split/non-merged one (mame/<clone>.zip). The clone's files overlay the parent's,
flattened, so roms/<game>/ holds every chip of that board under its MAME name.

prog68k.bin = the program pairs interleaved (high byte from the first chip of each
pair), checked against the SHA-1 of the image every tool and table in this repo was
built from. A different dump revision fails here, not three tools later.
Pairs and hashes measured 2026-10-29 against the roms/ images the port was built on.
"""
import argparse
import hashlib
import os
import sys
import zipfile

GAMES = {
    # game: (parent, [(high, low), ...] in address order, sha1 of prog68k.bin)
    'altbeast':  (None, [('epr-11907.a7', 'epr-11906.a5')],
                  'a8f28563be8296ed4e49979314cd4cf6176b2833'),
    'altbeastj': ('altbeast', [('epr-11885.a7', 'epr-11884.a5')],
                  '417ec57fd00ab1d53afcdf52186ab93d34f8c4ee'),
    'goldnaxe':  (None, [('epr-12545.ic2', 'epr-12544.ic1')],
                  '834220e2593d0149f902e7c0fb2ef2f5c8b1db89'),
    'goldnaxe2': ('goldnaxe', [('epr-12523.a7', 'epr-12522.a5'), ('epr-12521.a8', 'epr-12519.a6')],
                  '4e945f11caf530408e3cda3555ad408be8f1c315'),
}


def members(path, subdir=None):
    """{basename: bytes} of the zip's root files, or of one subdirectory."""
    out = {}
    if not os.path.exists(path):
        return out
    with zipfile.ZipFile(path) as z:
        for n in z.namelist():
            head, _, base = n.rpartition('/')
            if base and head == (subdir or ''):
                out[base] = z.read(n)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game')
    ap.add_argument('--zipdir', default='mame')
    ap.add_argument('--out', default='roms')
    a = ap.parse_args()
    if a.game not in GAMES:
        sys.exit('romset: no program layout for %r (known: %s)' % (a.game, ', '.join(GAMES)))
    parent, pairs, sha = GAMES[a.game]
    z = lambda g: os.path.join(a.zipdir, g + '.zip')
    files = {}
    if parent:
        files.update(members(z(parent)))
        files.update(members(z(parent), a.game))
    files.update(members(z(a.game)))
    need = [f for p in pairs for f in p if f not in files]
    if need:
        tried = [z(g) for g in ([parent] if parent else []) + [a.game]]
        sys.exit('romset: %s not found in %s -- place your MAME %s set there'
                 % (', '.join(need), ' / '.join(tried), a.game))
    prog = b''
    for hi, lo in pairs:
        h, l = files[hi], files[lo]
        if len(h) != len(l):
            sys.exit('romset: %s and %s differ in size' % (hi, lo))
        prog += bytes(b for p in zip(h, l) for b in p)
    got = hashlib.sha1(prog).hexdigest()
    if got != sha:
        sys.exit('romset: %s program SHA-1 %s, expected %s -- a different dump revision; '
                 'every table here was built from the expected one' % (a.game, got, sha))
    d = os.path.join(a.out, a.game)
    os.makedirs(d, exist_ok=True)
    for name, data in files.items():
        with open(os.path.join(d, name), 'wb') as f:
            f.write(data)
    with open(os.path.join(d, 'prog68k.bin'), 'wb') as f:
        f.write(prog)
    print('romset: %s -> %s (%d files, program %d KB, sha1 ok)' % (a.game, d, len(files), len(prog) // 1024))


if __name__ == '__main__':
    main()
