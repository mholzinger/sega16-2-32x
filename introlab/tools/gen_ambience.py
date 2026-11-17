#!/usr/bin/env python3
"""The launcher's background music: a stream for the port's Z80 player that plays, quietly,
the songs of another cabinet in the room, one after the other, and comes round again.

    gen_ambience.py MUSIC_DIR OUT.bin [--quieter STEPS] [--wav OUT.wav]

MUSIC_DIR holds per-frame SN76489 register logs (frame, three tone periods, noise, four
volumes) and songs.json with each song's loop point. They are captured from the owner's
own cartridge dump and are never tracked; without them the stream is empty and the
launcher is silent.

The stream is the player's (docs/sound/SOUND.md): a byte below 0x80 waits that many
milliseconds, 0xF2 b writes b to the PSG, 0xFF ends. Output: the LZSS form the 68K feeder
unpacks (tools/lzss.py), behind a 4-byte big-endian length. The whole stream loops."""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from lzss import compress, decompress

ORDER = [('85', 1), ('82', 1)]                   # song, passes of its loop: main music, beast mode (Mike, 2026-11-14: the boss music is out, to leave room for the room's sounds)
GAP_FRAMES = 45                                  # silence between songs
FRAME_MS = 17                                    # a frame is 16.67 ms; a whole number keeps the waits one repeating byte, and packs 20% smaller, for a tempo 2% slow

def rows(path):
    return [tuple(map(int, l.split(',')))[1:] for l in open(path) if l[0] != '#']

def main():
    src, out = sys.argv[1], sys.argv[2]
    # volume steps (2 dB each) taken off each PSG channel: tone 1, 2, 3, noise. Mike on the rig,
    # 2026-11-14: 4 on all was right but for the music wanting 8% more; a whole step is 26%, so
    # the lead channel alone comes up one step, which is about 9% on the whole.
    quieter = [int(sys.argv[sys.argv.index('--quieter') + 1])] * 4 if '--quieter' in sys.argv else [3, 4, 4, 4]
    meta_path = os.path.join(src, 'songs.json')
    if not os.path.exists(meta_path):
        open(out, 'wb').write(b'\0\0\0\0'); print('ambience: no songs in %s, the launcher stays silent' % src); return
    meta = json.load(open(meta_path))
    frames = []
    for song, passes in ORDER:
        r = rows(os.path.join(src, 'song_%s.csv' % song)); m = meta[song]
        if m['kind'] == 'loop':
            i, p = m['intro_frames'], m['loop_frames']
            r = r[:i] + r[i:i + p] * passes
        frames += r + [(0, 0, 0, 0, 15, 15, 15, 15)] * GAP_FRAMES
    s = bytearray(); t = 0.0; owed = 0.0; reg = [None] * 8
    def wait(ms):
        while ms > 127: s.append(127); ms -= 127
        if ms: s.append(ms)
    for f in frames:
        w = []
        for c in range(3):
            if f[c] != reg[c]:                   # the latch byte alone when only the low four bits moved (vibrato)
                w.append(0x80 | (c << 5) | (f[c] & 15))
                if reg[c] is None or (f[c] >> 4) != (reg[c] >> 4): w.append((f[c] >> 4) & 0x3F)
                reg[c] = f[c]
        if f[3] != reg[3]: w.append(0xE0 | (f[3] & 7)); reg[3] = f[3]
        for c in range(4):
            v = min(15, f[4 + c] + quieter[c]) if f[4 + c] < 15 else 15
            if v != reg[4 + c]: w.append(0x90 | (c << 5) | v); reg[4 + c] = v
        if w:
            ms = int(owed); wait(ms); owed -= ms
            for b in w: s += bytes([0xF2, b])
        owed += FRAME_MS
    wait(int(owed))
    packed = compress(bytes(s))
    assert decompress(packed) == bytes(s)
    open(out, 'wb').write(len(packed).to_bytes(4, 'big') + packed + (b'\0' if len(packed) & 1 else b''))
    print('ambience: %d songs, %.0f s, %d B of stream, %d B packed' % (len(ORDER), len(frames) / 60.0, len(s), len(packed)))

if __name__ == '__main__':
    main()
