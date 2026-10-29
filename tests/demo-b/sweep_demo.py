#!/usr/bin/env python3
"""The full sweep: every arcade tick of both demos against the port, whole work RAM.

    python3 tests/demo-b/sweep_demo.py build/port/rom/s16.32x      (make check-demo SWEEP=1)

check_demo.py gates a handful of ticks on the LIVE records. This compares, at every tick
the arcade set holds (tests/demo-b/arcade/aw_*.bin, both scenes), all 64 object records
live or dead and every other work-RAM byte except the cells that hold SH-2 pointers or the
68K stack, and prints one line per differing tick plus a one-line summary:

    sweep: N ticks compared, D differing, slots {..}; globals {..}

Exit 1 when any tick differs. Dumps go to tests/demo-b/work (DEMO_WORK), one ares run per
frame, and a frame is used only when its port_tick_done marker equals the tick counter.
Environment: ARES, DEMO_TICKS, DEMO_WORK, SWEEP_SCENES=T,E,A,H,B, SWEEP_JOBS (default 8).
"""
import os, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_demo as cd

# cells that legitimately differ: the port stores SH-2 pointers where the 68K stored its own
# addresses (text/HUD pointers 0xFFE008/88, the published register pointers 0xFFF0EC..F3, the
# palette queue 0xFFF402..405 and 0xFFF600..7FF), the frame latch 0xFFF01C, the title blink
# timer 0xFFF020/21 (a few vblanks of phase, STATE.md), and the 68K stack 0xFFFE80..
EXCL = set(range(0x2008, 0x200b)) | set(range(0x2088, 0x208b)) | set(range(0x30ec, 0x30f4)) \
     | set(range(0x3402, 0x3406)) | set(range(0x3600, 0x3800)) | set(range(0x3e80, 0x4000)) \
     | {0x300c, 0x301c, 0x3020, 0x3021, 0x3145, 0x302a, 0x302b}
SPR = 0x3800        # 0xFFF800: the hardware sprite list, one 0x10-byte entry per record


def hidden_sprite_tail(a, p):
    """Bytes 4..F of every sprite-list entry whose first long is zero on BOTH sides.
    The sprite chip reads an entry only while its scanline span (the first word) is
    non-zero, and hide_object_sprite zeroes just that first long -- so the tail of a
    hidden entry is whatever was written the last time the sprite WAS drawn: history the
    hardware never reads. The port's history differs across a scene boundary (demo B's
    entries 1-3 carried demo A's bank/palette/pen bytes), which is not a defect."""
    out = set()
    for e in range(64):
        o = SPR + e * 0x10
        if a[o:o + 4] == b'\0\0\0\0' and p[o:o + 4] == b'\0\0\0\0':
            out.update(range(o + 4, o + 0x10))
    return out


def sweep_tick(rom, scene, tick, arcade, workdir, arcframes=None):
    frame, t, p = cd.find_port_frame(rom, scene, tick, arcade, workdir, cd.guess_frame(scene, tick))
    if frame is None or t != tick:
        return scene, tick, None, None, None
    # A FROZEN TICK IS SEVERAL FRAMES AND THE RECORDS MOVE BETWEEN THEM. 0xFFF02A stops
    # advancing while the game waits on frames (demo A 0x24D inside the transformation,
    # the scene ends), so "the dump at tick T" is not one state: comparing the port's
    # first frame at T against the arcade's dump from three frames later judged nothing.
    # When the arcade set carries the freeze boundary, align on the offset from it.
    if arcframes and len(arcframes.get((scene, tick), [])) > 1:
        # the same anchor the gate uses: a frozen scene's state starts at its INSTALL
        # frame (cd.ANCHOR), not at the freeze, and the teardown before it takes a
        # different number of frames on the port
        an = cd.scene_anchor(arcframes, scene, tick)
        if an:
            arcfirst = an[0]
            pfirst = cd.port_anchor(rom, scene, tick, an[1], workdir, frame)
        else:
            arcfirst = cd.freeze_start(arcframes, scene, tick)
            pfirst = cd.port_freeze_start(rom, frame, tick, workdir) if arcfirst else None
        if arcfirst and pfirst:
            slots, glob, live = set(), set(), set()
            judged = 0
            for af, _n, a in arcframes[(scene, tick)]:
                pb = cd.dump_port(rom, pfirst + (af - arcfirst), workdir)
                if cd.tick_of(pb) != tick:
                    continue                      # the port left the freeze sooner
                judged += 1
                slots |= {s for s in range(64)
                          if any(a[s * 0x80 + i] != pb[s * 0x80 + i] for i in range(0x80))}
                live |= {s for s in range(64) if a[s * 0x80] & 0x80 or pb[s * 0x80] & 0x80}
                _sx = hidden_sprite_tail(a, pb)
                glob |= {i for i in range(0x2000, 0x4000)
                         if a[i] != pb[i] and i not in EXCL and i not in _sx}
            if judged:
                return scene, tick, sorted(slots), sorted(glob), sorted(live)
    name, a = arcade[(scene, tick)]
    slots = [s for s in range(64) if any(a[s * 0x80 + i] != p[s * 0x80 + i] for i in range(0x80))]
    live = [s for s in range(64) if a[s * 0x80] & 0x80 or p[s * 0x80] & 0x80]
    sx = hidden_sprite_tail(a, p)
    glob = [i for i in range(0x2000, 0x4000) if a[i] != p[i] and i not in EXCL and i not in sx]
    return scene, tick, slots, glob, live


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rom = sys.argv[1]
    workdir = os.environ.get('DEMO_WORK', os.path.join(cd.HERE, 'work'))
    os.makedirs(workdir, exist_ok=True)
    scenes = os.environ.get('SWEEP_SCENES', 'T,E,A,H,B').split(',')
    arcframes = cd.arcade_frames()
    arcade = {k: (v[0][1], v[0][2]) for k, v in arcframes.items()}
    targets = sorted((sc, t) for (sc, t) in arcade if sc in scenes and t != 0)
    jobs = int(os.environ.get('SWEEP_JOBS', '8'))
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda st: sweep_tick(rom, st[0], st[1], arcade, workdir, arcframes), targets))
    compared = differing = 0
    allslots, allglob = set(), set()
    for scene, tick, slots, glob, live in results:
        if slots is None:
            print('%s %04x  no complete port dump at this tick' % (scene, tick))
            continue
        compared += 1
        if slots or glob:
            differing += 1
            allslots |= set(slots); allglob |= set(glob)
            print('%s %04x  slots %s  globals %s' % (scene, tick,
                  ' '.join('%d%s' % (s, '' if s in live else 'd') for s in slots) or '-',
                  ' '.join('%04x' % (0xffc000 + g) for g in glob[:8]) + (' ...' if len(glob) > 8 else '') if glob else '-'))
    print('sweep: %d ticks compared, %d differing, slots {%s}; globals {%s}' % (
          compared, differing, ' '.join(str(s) for s in sorted(allslots)),
          ' '.join('%04x' % (0xffc000 + g) for g in sorted(allglob)[:16]) + (' ...' if len(allglob) > 16 else '')))
    sys.exit(1 if differing else 0)


if __name__ == '__main__':
    main()
