#!/usr/bin/env python3
"""Land a port rom on a GAME TICK and dump its framebuffer.

The exactness gate compares two renderers, and a frame number is not a
comparison point: the port's ticks per frame is not 1 and differs between
builds (check_demo.py:312), so f700 of one rom is a different moment of the
game than f700 of the other.  The tick counter 0xFFF02A -- WRAM offset 0x302A,
the 16 KB work-RAM image -- is the game's own clock and is the only sound
alignment.  Ticks also FREEZE (the high-score step holds 0x2BA for ~300
frames), so the search reports the frame span a tick covers and takes its
first frame.

  tools/r60_tick_fb.py <rom> <tick> [--out DIR] [--lo N] [--hi N]
"""
import os, subprocess, sys, struct

ARES = os.environ.get('ARES', os.path.expanduser(
    '~/src/ares-debug/build_macos/headless-ui/Release/ares-headless'))
WRAM_TICK = 0x2CC00 + 0x302A       # WRAM_BASE + 0xFFF02A inside the 16 KB image


def run(rom, frame, out, fb=False):
    args = [ARES, '--frames', str(frame),
            '--dump', 'sdram:0x%X:0x4:%s/t.bin' % (WRAM_TICK, out)]
    if fb:
        args += ['--dump', 'dram:0x200:0x11800:%s/fb.bin' % out]
    args.append(rom)
    subprocess.run(args, capture_output=True, timeout=1800)
    b = open('%s/t.bin' % out, 'rb').read()
    return struct.unpack('>H', b[:2])[0]


def find(rom, tick, out, lo=60, hi=3000):
    """Bisect on the frame whose tick is >= the target, then step back to its
    FIRST frame.  The tick counts up monotonically inside a scene, which is
    what makes a bisect legal; a frozen tick makes the predicate flat, not
    wrong."""
    seen = {}
    while lo < hi:
        mid = (lo + hi) // 2
        t = seen.setdefault(mid, run(rom, mid, out))
        print('   f%-5d tick=0x%04X' % (mid, t), flush=True)
        if t < tick:
            lo = mid + 1
        else:
            hi = mid
    t = run(rom, lo, out, fb=True)
    return lo, t


if __name__ == '__main__':
    a = sys.argv[1:]
    out = '/tmp/r60tick'
    if '--out' in a:
        i = a.index('--out'); out = a[i + 1]; del a[i:i + 2]
    lo, hi = 60, 3000
    if '--lo' in a:
        i = a.index('--lo'); lo = int(a[i + 1]); del a[i:i + 2]
    if '--hi' in a:
        i = a.index('--hi'); hi = int(a[i + 1]); del a[i:i + 2]
    rom, tick = a[0], int(a[1], 0)
    os.makedirs(out, exist_ok=True)
    f, t = find(rom, tick, out, lo, hi)
    print('%s: tick 0x%04X at frame %d (landed 0x%04X)' % (rom, tick, f, t))
    if t != tick:
        print('   WARNING: landed on 0x%04X, not 0x%04X' % (t, tick))
