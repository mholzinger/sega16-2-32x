#!/usr/bin/env python3
"""The compact sound blob may remove only streams the runtime never reads."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def table_rows(text, name):
    body = re.search(rf'{name}\[\d+\] = \{{(.*?)\n\}};', text, re.S).group(1)
    return [tuple(int(x) for x in row)
            for row in re.findall(r'\{\s*(\d+)u?,\s*(\d+)u?,\s*(\d+)\s*\}', body)]


def main():
    music = ROOT/'sndtest/md/render_music_psg.h'
    if not music.is_file():
        music = ROOT/'sndtest/md/render_music.h'
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        full, compact = td/'full.bin', td/'compact.bin'
        full_h, compact_h = td/'full.h', td/'compact.h'
        base = ['python3', str(ROOT/'tools/sound_blob.py'),
                str(music),
                str(ROOT/'sndtest/md/sndmap_data.h')]
        subprocess.run(base + [str(full), str(full_h)], check=True)
        subprocess.run(base + [str(compact), str(compact_h), '--omit-unplayed-sfx'], check=True)
        fb, cb = full.read_bytes(), compact.read_bytes()
        assert len(cb) < len(fb)
        ft, ct = table_rows(full_h.read_text(), 'snd_trks'), table_rows(compact_h.read_text(), 'snd_trks')
        map_text = (ROOT/'sndtest/md/sndmap_data.h').read_text()
        map_body = re.search(r'sndmap\[256\] = \{(.*?)\};', map_text, re.S).group(1)
        entries = [tuple(int(x, 0) for x in row) for row in
                   re.findall(r'\{\s*(\d+),\s*(\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}', map_body)]
        assert len(ft) == len(ct) and len(entries) == 256
        live = {idx for typ, idx, _ in entries if typ == 1}
        dead = {idx for typ, idx, _ in entries if typ == 2}
        assert live and dead and live.isdisjoint(dead)
        for i in live:
            fo, fn, fl = ft[i]; co, cn, cl = ct[i]
            assert (fn, fl) == (cn, cl)
            assert fb[fo:fo+fn] == cb[co:co+cn]
        sentinels = set()
        for i in dead:
            off, size, _ = ct[i]
            assert size == 1 and cb[off:off+1] == b'\xFF'
            sentinels.add(off)
        assert len(sentinels) == 1
        assert cb[:min(row[0] for row in ct)] == fb[:min(row[0] for row in ft)]
        stamp = ROOT/'sh_src/sound.bin.src'
        accepted = ROOT/'sh_src/sound.bin'
        if (stamp.is_file() and accepted.is_file() and
                stamp.read_text().strip() == str(music.relative_to(ROOT))):
            assert fb == accepted.read_bytes()
    print(f'sound blob compact: PASS ({music.name})')


if __name__ == '__main__':
    main()
