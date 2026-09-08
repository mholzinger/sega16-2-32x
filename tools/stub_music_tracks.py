#!/usr/bin/env python3
"""Stub the (now-dead) music track arrays in sndmap_data.h. KIT.

The router plays music from render_music.h (the faithful renders); the
isolation-sweep music tracks (snd_trks loop==1) are dead weight that
still linked into the 68K image and overflowed it. This replaces each
loop==1 track's array body with a 1-byte stub and sets its snd_trks
length to 1, reclaiming ~200KB. SFX/speech (loop==0) are untouched.

  stub_music_tracks.py sndtest/md/sndmap_data.h   # edits in place (+ .bak)
"""
import re
import sys


def main():
    path = sys.argv[1]
    src = open(path).read()
    open(path + ".bak", "w").write(src)

    # snd_trks[] rows: { trk_XX, LEN, loop }
    trk_re = re.compile(r"\{\s*(trk_[0-9A-Fa-f]+),\s*(\d+),\s*(\d)\s*\}")
    music = [m.group(1) for m in trk_re.finditer(src) if m.group(3) == "1"]
    if not music:
        print("no loop==1 music tracks found", file=sys.stderr)
        return

    # 1) replace each music array body with a 1-byte stub
    for name in music:
        arr = re.compile(
            r"static const uint8_t %s\[\d+\] = \{.*?\};" % re.escape(name),
            re.S)
        src, n = arr.subn(
            "static const uint8_t %s[1] = { 0xFF };" % name, src)
        if n != 1:
            print(f"WARN: {name} array not replaced ({n})", file=sys.stderr)

    # 2) set their snd_trks length to 1
    def fix_row(m):
        if m.group(1) in music:
            return "{ %s, 1, %s }" % (m.group(1), m.group(3))
        return m.group(0)
    src = trk_re.sub(fix_row, src)

    open(path, "w").write(src)
    print(f"stubbed {len(music)} music tracks: {', '.join(music)}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
