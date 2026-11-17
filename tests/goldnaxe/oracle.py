#!/usr/bin/env python3
"""The Golden Axe arcade oracle: object records at fixed (scene, tick) targets.

    python3 tests/goldnaxe/oracle.py build [DIR]       one MAME run -> DIR/aw_<scene>_<frame>.bin
                                                       + DIR/index.csv (default tests/goldnaxe/arcade)
    python3 tests/goldnaxe/oracle.py check REF OTHER   diff every live record at every target;
                                                       exit 1 on any differing byte

The demo-b gate's shape (tests/demo-b/check_demo.py) with Golden Axe's addresses. There is
no Golden Axe port yet, so today OTHER is a second fresh arcade build (`make goldnaxe-oracle-check`):
it proves the targets are deterministic and the key is sound. A port side only has to write
the same directory format (16 KB WRAM images + index.csv) to be gated by `check`.

Every address below was measured on set 6 (`mame goldnaxe`), 2026-10-24, from two cold-boot
no-input runs dumped every frame (1800 and 5400 frames, byte-identical over 1..1800):
  objects   64 x 0x80 at 0xFFC000 and 16 x 0x40 at 0xFFE100, live = byte 0 bit 7
            (LOOP-DECOMPILE-GOLDNAXE "Relocation"; dispatch loops 0x3CD0 / 0x3D12)
  tick      0xFFEC2A, the scene step timer: zeroed at each cut, compared against per-scene
            limits (0x85C cmpi #690, 0x17F0 cmpi #1800, 0x1D04 cmpi #60) -- AB's 0xFFF02A
  latch     0xFFEC1C, the frame flag: IRQ4 sets it (0x2F7A), the frame wait clears it (0x3C98)
  clock     0xFFEC20/21, the blink timer: 0xFFEC21 counts down and reloads at 59 or 29 by
            the parity of 0xFFEC20 -- the same shape as AB's 0xFFF020/21
"""
import csv, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WRAM, WLEN = 0xFFC000, 0x4000
TICK, LATCH, CLK = 0x2C2A, 0x2C1C, 0x2C20          # offsets inside the 16 KB image
TABLES = [(0x0000, 64, 0x80), (0x2100, 16, 0x40)]  # (offset, slots, record size)

# scene -> the frame range it occupies on the arcade's no-input attract (measured: the tick
# jumps to 0 at 931, 1316, 1661, 2025, 2560, 2917, 3281, 3610; the cycle repeats from the
# title at 4905). Only T and the three demos carry live objects; the interstitials hold 0.
SCENES = {
    'T': (382, 931),     # title: tick counts up 0x001..0x1E6, then holds
    'A': (931, 1316),    # demo 1: holds 0 to 1008 (load), up 0x001..0x12D
    'B': (2025, 2560),   # demo 2: holds 0 to 2102, up 0x001..0x1C3
    'C': (3281, 3610),   # demo 3: holds 0 to 3358, up 0x001..0x0F1 (skips ticks near 0xB6-0xC0)
}
# Each target is dumped with its neighbours (tick-1, tick, tick+1), as demo-b does, so a port
# that lands a tick late or early still has an arcade image to meet.
TARGETS = {
    'T': [0x040, 0x100, 0x1C0],
    'A': [0x010, 0x060, 0x0B0, 0x100, 0x128],
    'B': [0x010, 0x080, 0x100, 0x180, 0x1C0],
    'C': [0x010, 0x060, 0x0A0, 0x0E0],
}

w16 = lambda b, o: int.from_bytes(b[o:o + 2], 'big')


def game_frame(b):
    n = b[CLK]
    return sum(60 if (i & 1) else 30 for i in range(n)) + ((60 if (n & 1) else 30) - 1 - b[CLK + 1])


def live(b):
    return [(t, s) for t, (off, n, sz) in enumerate(TABLES) for s in range(n) if b[off + s * sz] & 0x80]


def record(b, t, s):
    off, _, sz = TABLES[t]
    return b[off + s * sz: off + (s + 1) * sz]


def mame_dump(last, outdir):
    """Every frame 1..last into outdir/aw_<frame>.bin, fresh NVRAM, CLAUDE.md headless flags."""
    nv = tempfile.mkdtemp()
    env = dict(os.environ, WD_FRAMES=','.join(str(f) for f in range(1, last + 1)),
               WD_ADDR=hex(WRAM), WD_LEN=hex(WLEN), WD_OUTDIR=outdir)
    subprocess.run(['mame', 'goldnaxe', '-rompath', './mame', '-skip_gameinfo', '-video', 'none',
                    '-sound', 'none', '-nothrottle', '-window', '-resolution', '160x120',
                    '-keyboardprovider', 'none', '-nomouse', '-nojoystick', '-bench', str(last // 60 + 5),
                    '-nvram_directory', nv, '-autoboot_script', 'tools/wram_dump.lua'],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shutil.rmtree(nv)


def build(out):
    last = max(hi for _, hi in SCENES.values())
    tmp = tempfile.mkdtemp()
    try:
        mame_dump(last, tmp)
        img = {}
        for f in range(1, last + 1):
            p = os.path.join(tmp, 'aw_%d.bin' % f)
            if not os.path.exists(p):
                raise SystemExit('oracle: MAME stopped before frame %d' % f)
            img[f] = open(p, 'rb').read()
        os.makedirs(out, exist_ok=True)
        for n in os.listdir(out):
            if n.startswith('aw_') or n == 'index.csv':
                os.remove(os.path.join(out, n))
        rows = []
        for sc, ticks in TARGETS.items():
            lo, hi = SCENES[sc]
            for t0 in ticks:
                for t in (t0 - 1, t0, t0 + 1):
                    f = next((f for f in range(lo, hi) if w16(img[f], TICK) == t), None)
                    if f is None:
                        raise SystemExit('oracle: scene %s never shows tick 0x%03X' % (sc, t))
                    b = img[f]
                    open(os.path.join(out, 'aw_%s_%d.bin' % (sc, f)), 'wb').write(b)
                    rows.append((sc, '0x%03X' % t, f, b[LATCH], game_frame(b), len(live(b))))
        with open(os.path.join(out, 'index.csv'), 'w', newline='') as fh:
            wr = csv.writer(fh)
            wr.writerow(['scene', 'tick', 'frame', 'latch', 'game_frame', 'live'])
            wr.writerows(rows)
        print('oracle: %d images, %d targets -> %s' % (len(rows), len(rows) // 3, out))
    finally:
        shutil.rmtree(tmp)


def load(d):
    out = {}
    for r in csv.DictReader(open(os.path.join(d, 'index.csv'))):
        b = open(os.path.join(d, 'aw_%s_%s.bin' % (r['scene'], r['frame'])), 'rb').read()
        out[(r['scene'], int(r['tick'], 16))] = (int(r['frame']), b)
    return out


def check(ref_dir, other_dir):
    ref, oth = load(ref_dir), load(other_dir)
    bad = 0
    print('scene tick   ref_f  oth_f  live  bytes  verdict')
    for sc, ticks in TARGETS.items():
        for t in ticks:
            if (sc, t) not in ref:
                raise SystemExit('oracle: %s has no target %s 0x%03X' % (ref_dir, sc, t))
            fr, a = ref[(sc, t)]
            if (sc, t) not in oth:
                print('%-5s 0x%03X %6d      -     -      -  MISSING' % (sc, t, fr)); bad += 1; continue
            fo, b = oth[(sc, t)]
            if a[LATCH] != b[LATCH]:
                # the two images sit on opposite sides of the game's frame boundary (demo-b
                # README): refused, not counted as a logic difference
                print('%-5s 0x%03X %6d %6d     -      -  REFUSED latch %d/%d' % (sc, t, fr, fo, a[LATCH], b[LATCH]))
                bad += 1; continue
            la, lb = live(a), live(b)
            n = sum(x != y for t_, s in sorted(set(la) | set(lb))
                    for x, y in zip(record(a, t_, s), record(b, t_, s)))
            ok = la == lb and n == 0
            bad += not ok
            print('%-5s 0x%03X %6d %6d  %4d  %5d  %s' % (sc, t, fr, fo, len(la), n,
                  'exact' if ok else ('LIVE SET' if la != lb else 'DIFF')))
    total = sum(len(v) for v in TARGETS.values())
    print('%d of %d exact' % (total - bad, total))
    return 1 if bad else 0


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['build']:
        build(a[1] if len(a) > 1 else os.path.join(HERE, 'arcade'))
    elif a[:1] == ['check'] and len(a) == 3:
        sys.exit(check(a[1], a[2]))
    else:
        raise SystemExit(__doc__)
