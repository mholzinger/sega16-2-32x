#!/usr/bin/env python3
"""The demo-B parity check: every live object record against the arcade, by tick.

    python3 tests/demo-b/check_demo.py build/port/rom/s16.32x

For each target (scene, tick) -- demo A and demo B, the two attract plays -- the port rom is run on ares-headless to the frame at which its
tick counter (0xFFF02A) reads that tick, and the 64 object records (0xFFC000, 0x80
bytes each) that are live on either machine are diffed against the arcade's work RAM
at the same tick (tests/demo-b/arcade/aw_<frame>.bin: MAME altbeast, fresh NVRAM
directory, tools/wram_dump.lua). The tick table is printed; exit 1 on any differing
byte in a live record, on a differing live set, or on a tick the port never shows.

The port's tick at a given FRAME varies 1-4 ticks between builds (the SH-2 frame loop
runs the game slightly off 60 Hz) and the port sometimes runs two game ticks in one
frame, so a target tick can be invisible at frame boundaries; the search then takes
the nearest tick on either side for which an arcade dump exists (the arcade set holds
the three ticks around each target) and says so in the table. A FROZEN tick (the
high-score step holds 0x2BA) has no neighbouring tick, so its targets are keyed on the
game frame and the frame latch instead, and when the port shows no complete copy of that
game frame the nearest arcade dump of the same scene it does show is compared, and the
table says so (check_one, GF_NEAR).

    ARES=<path>        the emulator binary
    DEMO_TICKS=<dir>   the arcade dump directory
    DEMO_WORK=<dir>    where the port dumps go (default tests/demo-b/work)
"""
import os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ARES = os.environ.get('ARES', '/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
HERE = os.path.dirname(os.path.abspath(__file__))
ARC = os.environ.get('DEMO_TICKS', os.path.join(HERE, 'arcade'))
WRAM_SDRAM_OFF = 0x2CC00           # 68K 0xFFC000 lives at SDRAM 0x0602CC00 (cport_layout.py)
TICK = 0x302a                      # 0xFFF02A inside the 16 KB work-RAM image
# scene -> (the arcade frame of its tick 0, the arcade frame range its aw_*.bin files
# fall in). Demo A is the first attract play (frames 464..1176), demo B the second
# (1498..); the tick counter 0xFFF02A restarts at each, so the arcade dumps are keyed
# (the A targets: 0x74/0x7C the trail, 0xC4 the balls, 0xEE the dash, 0x118 the pair's
# knockback -- each a first divergence found 2026-10-04 -- and 0x2B8 the scene end;
# (scene, tick). The port's tick-0 frame is within +-20 of the arcade's.
# scene -> (the frame of a reference tick, that tick, +1 when 0xFFF02A COUNTS UP in this
# scene and -1 when it counts DOWN, the frame range its arcade dumps fall in). The two
# attract steps before demo A count the tick down (0xB4 -> 0, then 0xE8 -> 0) and both
# therefore pass through the same tick values, which is why a scene is a frame range and
# a tick is only unique inside one.
SCENES = {
    'X': (18,   0x000, +1, (1, 30)),        # the boot card: 0xFFF02A is not a clock here
    'T': (20,   0x0B4, -1, (30, 203)),      # title/logo step
    'E': (210,  0x0E8, -1, (203, 450)),     # the second attract step
    'A': (464,  0x000, +1, (450, 1163)),    # demo A
    'H': (1163, 0x2BA,  0, (1163, 1491)),   # the high-score step: ONE frozen tick, ~300 frames
    'B': (1498, 0x000, +1, (1491, 2111)),   # demo B
    'P': (2130, 0x0B4,  0, (2111, 99999)),  # the post-demo-B attract step
}


# The handler a frozen scene installs into slot 0, which is what its state is anchored on.
# H: 0x2384 (measured -- the arcade's slot 0 holds it from frame 1168, the first frame of
# the high-score display, while the tick has been frozen at 0x2BA since 1163).
ANCHOR = {'H': 0x2384}


# PORT_STRETCH scales the whole port frame axis against the arcade's. The scene table is
# the ARCADE's timeline, and a port whose game-frame rate differs from 60/sec walks away
# from it linearly: PORTCLK1=1 drops every vint that lands while a frame is already owed
# (17% of them, pp_clk_dbg[1]), so its demo A starts at frame 667 instead of 577. The
# search is proportional and self-correcting, but only within 12 steps of its guess.
STRETCH = float(os.environ.get('PORT_STRETCH', '0')) or None


def calibrate(rom, workdir):
    """Set STRETCH from ONE probe of the port's own clock. Demo A counts its tick UP from
    frame 464, so a dump at a frame deep inside it says where the port's axis has got to:
    scale = probe frame / (464 + its tick). The walk is rate-adaptive but it only has 12
    steps, so a guess in the WRONG SCENE is unrecoverable -- measured 2026-10-06 under
    PORTCLK1: demo B's 0x0a2 guess landed in the high-score step, the walk stepped back
    over demo A's own 0x0a2 and compared it against a demo-B dump (893 bytes)."""
    global STRETCH
    if STRETCH:
        return STRETCH
    # TWO PROBES, AND THE LOCAL RATE. One probe divides by the frames since RESET, which
    # includes a boot the port does not spend at its running rate: at a true x1.041 the
    # frame-1000 ratio read x1.065, the guess for demo B's last tick landed 50 frames past
    # the scene, and the walk settled on a tick from the NEXT attract pass. Demo A counts
    # its tick up, so two frames inside it give frames-per-tick directly.
    t1 = tick_of(dump_port(rom, 700, workdir))
    t2 = tick_of(dump_port(rom, 1100, workdir))
    if 0x20 < t1 < 0x2b0 and 0x20 < t2 < 0x2b0 and t2 > t1:
        STRETCH = 400.0 / (t2 - t1)
    else:
        t = tick_of(dump_port(rom, 1000, workdir))
        STRETCH = (1000.0 / (SCENES['A'][0] + t)) if 0x20 < t < 0x2b0 else 1.0
    return STRETCH


def guess_frame(scene, tick):
    """The port frame to start the search at. direction 0 means the scene holds one FROZEN
    tick (the high-score step, the post-demo attract), so the tick says nothing about the
    frame and the scene's own first frame is the only guess available."""
    f0, t0, d, _ = SCENES[scene]
    return int(round((f0 + d * (tick - t0)) * (STRETCH or 1.0)))
# A target is (scene, tick) or (scene, tick, nth): nth picks among the arcade dumps at a
# FROZEN tick, where the records move from frame to frame while 0xFFF02A stands still, and
# the port's matching frame is found by the offset from each side's own freeze start.
TARGETS = [('T', 0x096), ('T', 0x028), ('E', 0x0CA), ('E', 0x034),
           ('A', 0x074), ('A', 0x07C), ('A', 0x0C4), ('A', 0x0EE), ('A', 0x118), ('A', 0x140), ('A', 0x1C0), ('A', 0x240), ('A', 0x2B8),
           # nth 7 = arcade frame 1170, nth 13 = 1308, both GAME-FRAME BOUNDARIES. nth 5
           # (frame 1168) sat inside the teardown's single game frame, which is why it
           # never settled: 7, 33, 40 and 57 bytes on four builds.
           ('H', 0x2BA, 7), ('H', 0x2BA, 13),
           ('B', 0x0A2), ('B', 0x0D4), ('B', 0x1FE), ('B', 0x228), ('B', 0x246)]


# THE GAME'S OWN FRAME NUMBER, off the blink timer at 0xFFF020/21. The timer steps once per
# game frame (irq4_handler decrements 0xFFF021 and reloads it at 0x1D or 0x3B by the parity
# of 0xFFF020), so the pair is a monotonic game-frame count. It is a CLOCK here, never a
# value to compare: a frozen scene has to be aligned on game frames, because the arcade
# spends four wall frames on the teardown's single game frame and a wall-frame offset puts
# the two machines at different points of the same frame. Measured 2026-10-07: the same
# high-score target read 7, 33, 40 and 57 bytes on four builds that way, and 7 bytes at
# three different game frames once aligned.
def game_frame(b):
    n = b[0x3020]
    return sum(60 if (i & 1) else 30 for i in range(n)) + ((60 if (n & 1) else 30) - 1 - b[0x3021])


# THE GAME'S OWN FRAME LATCH, 0xFFF01C. The arcade's IRQ4 handler self-gates on it: latch
# clear = publish and advance, latch set = bump 0xFFF144 and do nothing. So it says WHICH
# SIDE of the game's own frame boundary a dump was taken on, and two dumps at the same tick
# and the same game frame are still different moments if it differs.
#
# MEASURED 2026-10-08, and this is why it is part of the key: 688 bytes of DEAD SPACE added
# to .ramtext -- nothing hot moved, no function changed -- shifted the port 4 wall frames and
# changed which targets fail (A 0074 + A 00c4 became A 0140 + B 0246) and took the picture
# from 0 of 20 to 7 of 20. Diffing the two builds at the SAME tick and the SAME game frame,
# 35 bytes differ and 0xFFF01C is one of them: head 0x00 against pad 0x01. The records that
# differ (slots 62/63, the demo actors) are a frame of work apart, not wrong. Without this
# the gate measures where the master's vint happened to land inside the game's frame, which
# is a layout lottery and not a game-logic difference.
def latch_of(b):
    return b[0x301C]


def tick_of(b):
    return int.from_bytes(b[TICK:TICK + 2], 'big')


def live(b):
    return [s for s in range(64) if b[s * 0x80] & 0x80]


def arcade_frames():
    """(scene, tick) -> [(frame, name, bytes), ...] sorted by frame. A tick the game
    FREEZES (it stops advancing 0xFFF02A while it waits on frames: the transformation
    cutscene at demo A 0x24D, the scene ends) has several frames, and the records move
    between them, so which frame a dump came from is part of its identity."""
    out = {}
    for n in os.listdir(ARC):
        # THE NAME CARRIES THE SCENE: aw_<scene>_<frame>.bin. It used to be aw_<frame>.bin
        # with the scene derived from a frame range, so a tick that occurs in two scenes
        # (0xB4 in demo B and in the post-demo attract, 0x2BA in demo A's tail and the
        # high-score step) could take the wrong dump by directory order, and a dump whose
        # frame fell outside every range was silently unused. A legacy name is an error.
        m = re.match(r'^aw_([A-Z])_(\d+)\.bin$', n)
        if not m:
            if re.match(r'^aw_\d+\.bin$', n):
                raise SystemExit('check_demo: %s has no scene in its name -- rename it '
                                 'aw_<scene>_<frame>.bin (tests/demo-b/arcade_dump.sh takes '
                                 'the scene as its first argument)' % n)
            continue
        sc, fr = m.group(1), int(m.group(2))
        if sc not in SCENES:
            raise SystemExit('check_demo: %s names scene %s, which SCENES does not define' % (n, sc))
        lo, hi = SCENES[sc][3]
        if not (lo <= fr < hi):
            raise SystemExit('check_demo: %s is outside scene %s\'s frame range %d..%d'
                             % (n, sc, lo, hi - 1))
        b = open(os.path.join(ARC, n), 'rb').read()
        if len(b) == 0x4000:
            out.setdefault((sc, tick_of(b)), []).append((fr, n, b))
    for v in out.values():
        v.sort()
    return out


def arcade_by_tick():
    """(scene, tick) -> the FIRST arcade dump at that tick (the gate's single-frame view)."""
    return {k: (v[0][1], v[0][2]) for k, v in arcade_frames().items()}


def freeze_start(arc, scene, tick, workdir=None):
    """The frame a frozen tick BEGAN at, or None when the dump set does not carry the
    boundary. The frame before it must be dumped (in ANY scene -- a freeze can start right
    after a scene boundary, as the high-score step's does) and show a different tick."""
    fs = arc.get((scene, tick))
    if not fs:
        return None
    first = fs[0][0]
    prev = {fr: t for (_sc, t), v in arc.items() for fr, _n, _b in v}.get(first - 1)
    return first if (prev is not None and prev != tick) else None


def port_freeze_start(rom, frame, tick, workdir):
    """The port's own first frame at a frozen tick: walk down while the tick holds."""
    f = frame
    for _ in range(40):
        if tick_of(dump_port(rom, f - 1, workdir)) != tick:
            return f
        f -= 1
    return None


NM = os.environ.get('SH_NM', os.path.expanduser('~/src/marsdev/mars/sh-elf/bin/sh-elf-nm'))
_marker = [None]


def marker_offset(rom):
    """SDRAM offset of port_tick_done (0xFFF02A of the last COMPLETE tick), from the
    ELF beside the rom; None when there is no ELF or no symbol (older roms)."""
    if _marker[0] is None:
        elf = rom[:-4] + '.elf'
        off = 0
        if os.path.exists(elf):
            try:
                for l in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
                    if l.endswith(' _port_tick_done'):
                        off = int(l.split()[0], 16) - 0x06000000
            except OSError:
                pass
        _marker[0] = off
    return _marker[0] or None


_romtag = {}


def snapshot_rom(rom, workdir):
    """Copy the rom aside and measure THAT. The gate and `make port` both live on
    build/port/rom/s16.32x, so a build landing mid-gate silently replaces the thing being
    measured: three runs were corrupted that way on 2026-10-06/07 -- one reported 18 of 18
    FB staging ticks where every clean run says 20, one anchored the high-score target 11
    frames off and read 57 bytes instead of 7, and one reported the first 20 of 20 this
    project has seen from two different roms. The dumps are keyed by the rom's sha1 so they
    never MIX inside a target, which is exactly why the corruption reads as a plausible
    number instead of an error. A copy costs 4 MB and one second and removes the class."""
    tag = rom_tag(rom)
    snap = os.path.join(workdir, 'rom_%s.32x' % tag)
    if not os.path.exists(snap):
        shutil.copyfile(rom, snap)
    return snap, tag


def rom_tag(rom):
    """A cache key for the ROM ITSELF. The dumps were keyed by frame number alone, so a
    second run against a DIFFERENT rom read the first rom's dumps out of the work dir and
    reported them as its own -- measured 2026-10-06 A/B-ing PORTCLK1 against the baseline:
    the baseline run reused the flag build's frames and moved two targets. 8 hex of the
    rom's content."""
    if rom not in _romtag:
        import hashlib
        _romtag[rom] = hashlib.sha1(open(rom, 'rb').read()).hexdigest()[:8]
    return _romtag[rom]


def dump_port(rom, frame, workdir):
    out = os.path.join(workdir, 'port_%s_%d.bin' % (rom_tag(rom), frame))
    mk = marker_offset(rom)
    if not os.path.exists(out):
        cmd = [ARES, rom, '--frames', str(frame), '--dump', 'sdram:0x%x:0x4000:%s' % (WRAM_SDRAM_OFF, out)]
        if mk:
            cmd += ['--dump', 'sdram:0x%x:4:%s.mk' % (mk, out)]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return open(out, 'rb').read()


# THE TICK'S OWN END, NOT THE FRAME'S (2026-10-24). A frame-end dump lands wherever the
# master stands at the vblank, and the master starts a tick on the vint: from demo A on,
# every frame-end dump holds tick N+1 begun (0xFFF02A already N+1, the dispatcher reset,
# slot 0 part-run) on top of tick N's records. How far in depends only on the master's
# speed, so a faster build (PORTHOT2 T3) read slot 0 a handler further on (hero x 11bf
# against 11b8) with a byte-identical game: its SH-2 work-RAM write stream equals sw317's,
# write for write, over frames 462-2140. The arcade dump is the 68K parked in
# wait_frame_flag (0xFFF109 = 0x40, 0xFFF01C = 0). The equivalent port state is rebuilt
# here: a dump a few frames early plus every SH-2 write into work RAM after it, replayed in
# order and cut at the game's own end-of-frame write -- port_frame_wait's clr.b 0xFFF01C
# with the counter at the tick (sh_src/port_l2.c, port_frame_wait). Replay over 12 frames
# reproduces a direct dump byte for byte. port_tick_done was meant to reject mid-tick dumps
# but cannot: snapshot_rom copies the rom without its .elf, so marker_offset finds no
# symbol and tick_complete returns True for every frame (dead since a2b5aba9, 2026-10-07).
TE_SPAN = 4
TE_LATCH = 0x301C                  # 0xFFF01C, the frame latch the wait clears


def tick_end(rom, frame, tick, workdir):
    """Work RAM at the END of game tick `tick` (the clr.b 0xFFF01C that closes it), searched
    in [frame - TE_SPAN, frame + TE_SPAN). None when the window holds no such write (a
    blank hold yields without clearing the latch)."""
    import csv
    f0, f1 = max(frame - TE_SPAN, 1), frame + TE_SPAN
    log = os.path.join(workdir, 'te_%s_%d_%d.csv' % (rom_tag(rom), f0, f1))
    if not os.path.exists(log):
        lo = 0x06000000 + WRAM_SDRAM_OFF
        subprocess.run([ARES, rom, '--frames', str(f1), '--watch-sdram',
                        '0x%08x:0x%08x:%d:%d' % (lo, lo + 0x3FFF, f0, f1),
                        '--watch-sdram-out', log],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if not os.path.exists(log):
        return None
    w = bytearray(dump_port(rom, f0, workdir))
    with open(log) as fh:
        for r in csv.DictReader(fh):
            a = int(r['address'], 16) - 0x06000000 - WRAM_SDRAM_OFF
            d, m = int(r['data'], 16), int(r['mask'])
            if m & 2:
                w[a] = d >> 8
            if m & 1:
                w[a + 1] = d & 0xFF
            if a == TE_LATCH and (m & 2) and (d >> 8) == 0 and tick_of(w) == tick:
                return bytes(w)
    return None


def tick_complete(workdir, frame, b, rom=None):
    """True when the dump's tick counter is the last COMPLETE tick (or no marker)."""
    mkf = os.path.join(workdir, 'port_%s_%d.bin.mk' % (rom_tag(rom), frame)) if rom else None
    if not mkf:
        return True
    if not os.path.exists(mkf):
        return True
    return int.from_bytes(open(mkf, 'rb').read()[:4], 'big') == tick_of(b)


def find_port_frame(rom, scene, target, arcade, workdir, start_guess):
    """The port frame whose dump holds the target tick COMPLETE (port_tick_done equals the
    tick counter); failing that, the complete dump nearest the target among the ticks the
    arcade set also holds. The port runs two game ticks in one frame when it is behind, so
    a frame's dump can land mid-tick (counter = N, slots half-dispatched): those are
    skipped, and the frames on either side are tried instead."""
    # THE WALK STAYS INSIDE ITS OWN SCENE. SCENES carries each scene's arcade frame band;
    # scaled by the port's clock that is the port's band. Without this the walk can leave
    # the scene entirely and still "succeed": measured 2026-10-06 with the clock at x1.045,
    # demo B's 0x246 guess landed past the scene's end, the walk found tick 0x0B4 -- which
    # demo B also contains -- in the POST-demo attract step and compared it against an
    # arcade dump from 500 frames earlier (587 bytes, hero x 0000).
    _st = STRETCH or 1.0
    # LOOSE, because the stretch is an average and a scene's own end can run past it: at
    # x1.031 demo A's 0x2B8 sits 35 frames beyond the scaled band and a tight clamp made it
    # NOT REACHED. The band only has to keep the walk out of the NEIGHBOURING scenes; the
    # tick tolerance below is what rejects a wrong tick.
    blo = int(SCENES[scene][3][0] * _st) - 64
    bhi = int(SCENES[scene][3][1] * _st) + 64
    frame = min(max(start_guess, blo), bhi)
    seen = {}
    rate = SCENES[scene][2] or 1                   # ticks per port frame, SIGNED
    for _ in range(12):                            # walk to the target's neighbourhood
        b = dump_port(rom, frame, workdir)
        t = tick_of(b)
        seen[frame] = (t, b)
        if abs(t - target) <= 2:
            break
        if t == 0 and len(seen) > 1:
            break                                  # the demo is over / not started
        # THE PORT'S TICKS PER FRAME IS NOT 1. The scene table is the arcade's timeline,
        # and the port's game-frame rate is its own: under PORTCLK1 it drops the 12% of
        # vints that land while a frame is already owed, so a 0x100-tick walk that assumed
        # one tick per frame undershot by 16% and ran out of its 12 steps (measured
        # 2026-10-06: the four title/attract targets went NOT REACHED). Two probes in the
        # same scene give the local rate; keep the table's sign until they do.
        far = [(f, tv) for f, (tv, _) in seen.items() if f != frame and tv not in (0, t)]
        if far:
            f0, t0 = far[-1]
            if frame != f0:
                r = (t - t0) / float(frame - f0)
                if abs(r) > 0.05:
                    rate = r
        step = int(round((target - t) / rate))
        step = max(-64, min(64, step)) or (1 if (target - t) * rate > 0 else -1)
        if frame + step in seen:
            break
        frame += step
        if frame < blo or frame > bhi:             # the scene's own band, nothing outside
            frame = min(max(frame, blo), bhi)
            if frame in seen:
                break
    for f in range(max(frame - 3, blo), min(frame + 4, bhi + 1)):   # the window around it
        if f not in seen:
            b = dump_port(rom, f, workdir)
            seen[f] = (tick_of(b), b)
    best = None
    for f in sorted(seen):
        t, b = seen[f]
        # AND NEVER A DISTANT TICK. The adjacent-tick fallback exists for a tick the port
        # skipped inside a frame, which is one or two away; accepting anything with a dump
        # let a 0x0B4 from the post-demo step stand in for demo B's 0x246 (2026-10-06).
        if abs(t - target) > 8:
            continue
        if (scene, t) not in arcade or not tick_complete(workdir, f, b, rom):
            continue
        # the latch is part of the key: prefer a frame whose latch matches the arcade's
        want_l = latch_of(arcade[(scene, t)][1])
        cost = (latch_of(b) != want_l, abs(t - target))
        if best is None or cost < (latch_of(best[2]) != want_l, abs(best[1] - target)):
            best = (f, t, b)
    return best if best else (None, None, None)


SPR = 0x3800        # 0xFFF800: the hardware sprite list the 68K hands the sprite chip,
                    # one 0x10-byte entry per record, indexed by the record's byte +8.


def scene_anchor(arc, scene, tick):
    """For a scene whose tick is FROZEN, the frame its state actually starts at: the first
    dumped frame whose slot-0 handler is the one the scene installs (ANCHOR below). The
    freeze START is not that frame -- the high-score step's tick freezes at 1163 while demo
    A's objects are still being torn down and the install lands at 1168 -- and the teardown
    takes a different number of frames on the port, so offsets measured from the freeze
    moved under it (measured 2026-10-05: the same target aligned at 1163 in one build and
    1165 in the next). Returns (frame, handler long) or None."""
    h = ANCHOR.get(scene)
    if h is None:
        return None
    want = h.to_bytes(4, 'big')
    for fr, _n, b in arc.get((scene, tick), []):
        if b[2:6] == want:
            return fr, want
    return None


def port_anchor(rom, scene, tick, handler, workdir, guess):
    """The port's own frame for that anchor: the first frame at the frozen tick whose
    slot-0 handler matches."""
    for f in range(guess - 12, guess + 40):
        b = dump_port(rom, f, workdir)
        if tick_of(b) == tick and b[2:6] == handler:
            return f
    return None


# THE FRAMEBUFFER'S TILEMAP STAGING AGAINST SDRAM TRUTH. The record compare cannot see the
# picture: the port can be byte-exact in work RAM while the renderer hands the VDP a stale
# page. restore_pages replays TILEMAP_U (0x26019000, 13 pages of 4 KB) into the bank the FB
# window maps (0x04012000), so staging == truth is the renderer's own invariant, and it is
# what the 2026-10-06 narrowing of ff_bm had to preserve. Pages 0-11 must match exactly;
# page 12 is the blank page, which restore_pages covers only in part (PG_LO/PG_HI) and
# which stood at ~750 differing words before and after that change -- reported, not gated.
# THE COMPOSED PICTURE, AGAINST A BASELINE. Everything else the gate reads is state: the
# records are work RAM and the staging is the tilemap. Neither sees whether the renderer
# actually DREW the same frame, and the two changes that needed that most -- the sbuf margin
# rebase and the hash slice -- both passed the records AND the staging while moving 3,237 to
# 8,707 of the screen's 72,192 bytes. So the screen band goes in the gate too, compared with
# a baseline the repo carries, and with a CONTROL: the same rom five frames later, which is
# 0 bytes at a static tick and tells a real difference from frame phase.
#
# PORT_PIC=<rom> names the baseline. Without it the check prints the band's own fingerprint
# so a baseline can be adopted (PORT_PIC_WRITE=1 writes one), and it never fails the gate
# on the first run of a new scene.
FB_SCREEN, FB_SCREEN_END = 0x200, 0x11C00     # 224 rows x 320 bytes of displayed 32X FB
# THE BOTTOM EIGHT ROWS ARE NOT COMPARED. PORTRIG=1 paints its two rate bars into screen
# rows 216-223 (sbuf 224-231), so a PORTRIG rom's picture would differ from every baseline
# by the bars alone, and the bars move every frame by construction. The band compared is
# the leading prefix, so baselines taken over the full 224 rows stay valid unchanged.
PIC_ROWS_MASKED = 8
PIC_CMP = (FB_SCREEN_END - FB_SCREEN) - PIC_ROWS_MASKED * 320
PIC_WRITE = os.environ.get('PORT_PIC_WRITE') == '1'
# ADOPT THE TICK WITHOUT REWRITING THE PICTURE. The baselines in picbase/ were committed
# before they carried a tick; this writes each one's sidecar from the tick the walk lands
# on now and leaves the bytes untouched, so the sidecar can be added without a new
# baseline pass (which would silently adopt whatever the current rom renders).
PIC_TICK = os.environ.get('PORT_PIC_TICK') == '1'
# ONE TARGET PER SCENE BY DEFAULT. The check costs two ares runs a target (the frame and
# its +5 control), which took the gate from about 5 minutes to 15 when it ran on all 20.
# One per scene still covers every scene's compose path; PICTURE=all does the lot, and any
# commit touching sh_src/m_main.c should run it that way -- that file is the renderer.
PIC_ALL = os.environ.get('PICTURE') == 'all' or PIC_WRITE or PIC_TICK
PIC_ONE = {}                      # scene -> the first target in it, filled by main()
PIC_DIR = os.path.join(HERE, 'picbase')


def fb_screen(rom, frame, workdir):
    """The displayed screen band at this port frame, or None."""
    f = os.path.join(workdir, 'pic_%s_%d.bin' % (rom_tag(rom), frame))
    if not os.path.exists(f):
        subprocess.run([ARES, rom, '--frames', str(frame),
                        '--dump', 'dram:0x%x:0x%x:%s' % (FB_SCREEN, FB_SCREEN_END - FB_SCREEN, f)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return open(f, 'rb').read() if os.path.exists(f) else None


def pic_wanted(scene, target, nth):
    return PIC_ALL or PIC_ONE.get(scene) == (scene, target, nth)


def pic_threshold(ctln):
    """How far a picture may differ from its baseline before it is a regression.

    IT SCALES WITH THE CONTROL. The control is what one five-frame step of motion costs,
    so a difference of that order is a phase error -- the walk landing one frame either
    side -- and not a renderer change. A flat `max(control, 64)` therefore flagged 1797
    bytes against a 1793-byte control (2026-10-07, an H target): a four-byte margin on a
    number that moves by hundreds between builds of the same source. Half the control
    again is the margin, and the floor stays 64 so a static tick is still held exactly."""
    return max(64, ctln + ctln // 2)


def pic_check(rom, scene, target, nth, frame, landed, gframe, latch, workdir):
    """(differing bytes vs the baseline, control bytes, note). The control is the same rom
    five frames on: a static tick reads 0 and a moving one reads what motion costs, so a
    difference only counts when it clears pic_threshold() of it.

    `landed` is the tick the walk compared at, `gframe` the game frame inside it and `latch`
    the game's own frame latch 0xFFF01C; the baseline carries all three.

    THE LATCH IS IN THE KEY FOR THE SAME REASON IT IS IN THE RECORD COMPARE. Measured
    2026-10-08: 688 bytes of dead space in .ramtext -- nothing hot moved -- took the picture
    from 0 of 20 to 7 of 20 while the records, once keyed on the latch, read 0 of 20 both
    ways. A framebuffer sampled on one side of the game's frame boundary against a baseline
    taken on the other is two different moments, not a renderer change."""
    cur = fb_screen(rom, frame, workdir)
    if cur is None:
        return None, None, 'no dump'
    ctl = fb_screen(rom, frame + 5, workdir)
    ctln = sum(1 for a, b in zip(cur[:PIC_CMP], ctl[:PIC_CMP]) if a != b) if ctl else None
    name = 'pic_%s_%04x%s.bin' % (scene, target, ('_%d' % nth) if nth else '')
    path = os.path.join(PIC_DIR, name)
    # THE BASELINE CARRIES THE TICK IT WAS TAKEN AT, beside it, in a one-line sidecar.
    # The target in the filename is what was ASKED for; the walk can land elsewhere -- the
    # 0x246 guess landed past demo B's end and the walk compared at 0x0B4 (see the target
    # table). A baseline adopted at one landed tick and compared at another is two
    # different pictures, and the byte count that comes out of it means nothing. Raw bytes
    # stay raw, so every baseline already committed is still valid; one with no sidecar is
    # compared as before and says so, and PORT_PIC_WRITE=1 adopts the sidecar.
    tpath = path[:-4] + '.tick'
    # AND THE ROM IT WAS ADOPTED ON, as provenance. A rom whose picture legitimately
    # changes SHOULD fail against an old baseline -- that is the check working -- but when
    # it does, the first question is always "which rom is this baseline from". Measured
    # 2026-10-07: 11 of 20 targets failed with a control of 0 because the baselines were
    # adopted at 8ac52849 and HEAD had since fixed the y two objects hand
    # sprite_build_and_cull (0x1080 instead of their own), which moves sprites on screen.
    # Reading that off the file costs nothing; deriving it cost an afternoon.
    stamp = '%04x %d %s %d\n' % (landed, gframe, rom_tag(rom), latch)
    if PIC_TICK and os.path.exists(path):
        open(tpath, 'w').write(stamp)
        return None, ctln, 'tick %04x game frame %d adopted for the existing baseline' % (
            landed, gframe)
    if PIC_WRITE:
        os.makedirs(PIC_DIR, exist_ok=True)
        open(path, 'wb').write(cur)
        open(tpath, 'w').write(stamp)
        return 0, ctln, ('baseline written at tick %04x game frame %d latch %d'
                         % (landed, gframe, latch))
    if not os.path.exists(path):
        return None, ctln, 'no baseline (PORT_PIC_WRITE=1 to adopt)'
    if not os.path.exists(tpath):
        return (sum(1 for a, b in zip(cur[:PIC_CMP], open(path, 'rb').read()[:PIC_CMP])
                    if a != b), ctln, 'baseline carries no tick: comparison unanchored')
    parts = open(tpath).read().split()
    wast = int(parts[0], 16)
    wasg = int(parts[1]) if len(parts) > 1 else None
    wasl = int(parts[3]) if len(parts) > 3 else None
    if wast != landed:
        return None, ctln, ('NOT COMPARED: baseline taken at tick %04x, this run landed '
                            'at %04x' % (wast, landed))
    if wasg is not None and wasg != gframe:
        return None, ctln, ('NOT COMPARED: baseline taken at game frame %d, this run '
                            'landed at %d (same tick %04x)' % (wasg, gframe, landed))
    if wasl is not None and wasl != latch:
        return None, ctln, ('NOT COMPARED: baseline taken with the frame latch at %d, this '
                            'run landed at %d (same tick %04x, same game frame %d)'
                            % (wasl, latch, landed, gframe))
    base = open(path, 'rb').read()
    return sum(1 for a, b in zip(cur[:PIC_CMP], base[:PIC_CMP]) if a != b), ctln, ''


FB_STAGE = 0x12000      # 32X DRAM offset of the staging the FB window maps
FB_TRUTH = 0x19000      # SDRAM offset of TILEMAP_U, the port's own truth
FB_PAGES, FB_PGSZ = 13, 0x1000


def fb_staging(rom, frame, workdir):
    """(pages 0-11 differing from truth, page-12 words differing) at this port frame."""
    tag = rom_tag(rom)
    f1 = os.path.join(workdir, 'fbs_%s_%d.bin' % (tag, frame))
    f2 = os.path.join(workdir, 'fbt_%s_%d.bin' % (tag, frame))
    n = FB_PAGES * FB_PGSZ
    if not (os.path.exists(f1) and os.path.exists(f2)):
        subprocess.run([ARES, rom, '--frames', str(frame),
                        '--dump', 'dram:0x%x:0x%x:%s' % (FB_STAGE, n, f1),
                        '--dump', 'sdram:0x%x:0x%x:%s' % (FB_TRUTH, n, f2)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if not (os.path.exists(f1) and os.path.exists(f2)):
        return None, None
    a, b = open(f1, 'rb').read(), open(f2, 'rb').read()
    bad, p12 = [], 0
    for pg in range(FB_PAGES):
        o = pg * FB_PGSZ
        d = sum(1 for i in range(FB_PGSZ) if a[o + i] != b[o + i])
        if pg == 12:
            p12 = d // 2
        elif d:
            bad.append((pg, d // 2))
    return bad, p12


# How far, in game frames, a frozen-tick target may move to a neighbouring arcade dump when
# the port shows no complete copy of its own game frame (check_one). The H dumps at 1300+
# are 8 game frames apart, so 16 reaches the next one on either side and no further.
GF_NEAR = 16
_GNOTE = {}                         # (scene, target, nth) -> the table note for a moved target


def _port_at_game_frame(rom, workdir, tick, centre, want, latch):
    """The first port frame within +-16 of `centre` that holds `tick`, game frame `want`
    and frame latch `latch` -- a COMPLETE game frame at the arcade's moment -- or None."""
    for f in range(centre - 16, centre + 17):
        b = dump_port(rom, f, workdir)
        if tick_of(b) == tick and game_frame(b) == want and latch_of(b) == latch:
            return f
    return None


def check_one(rom, scene, target, arcade, workdir, guess, nth=0, arcframes=None):
    frame, t, p = find_port_frame(rom, scene, target, arcade, workdir, guess)
    if frame is None:
        return (scene, target, nth), None, None, None, None, None, None, None
    pic_t, pic_p = t, p                  # the picture baseline is keyed on the frame-end dump
    if not nth and (scene, target) in arcade and latch_of(arcade[(scene, target)][1]) == 0:
        te = tick_end(rom, frame, target, workdir)   # the arcade's phase: tick done, 68K parked
        if te is not None:
            t, p = target, te
    name, a = arcade[(scene, t)]
    if nth and arcframes:
        fs = arcframes.get((scene, t), [])
        an = scene_anchor(arcframes, scene, t)
        pf = port_anchor(rom, scene, t, an[1], workdir, frame) if an else None
        if not (nth < len(fs) and an and pf is not None):
            return (scene, target, nth), None, None, None, None, None, None, None
        arcfr, name, a = fs[nth]
        # ALIGN ON GAME FRAMES, not wall frames. an[0] is the arcade's anchor frame and pf
        # the port's; the offset that matters is the one in the game's own clock.
        # BOTH SIDES MEASURED FROM THEIR OWN ANCHOR FRAME. Measuring the arcade's offset
        # from the scene's FIRST dump while the port's came from its INSTALL frame mixed
        # two reference points and put the later target out of reach entirely.
        _ab = next((b for (fr, _n, b) in fs if fr == an[0]), None)
        if _ab is None:
            return (scene, target, nth), None, None, None, None, None, None, None
        gf0 = game_frame(dump_port(rom, pf, workdir)) - game_frame(_ab)
        frame = _port_at_game_frame(rom, workdir, t, pf + (arcfr - an[0]),
                                    game_frame(a) + gf0, latch_of(a))
        if frame is None:
            # THE NEAREST COMPLETE GAME FRAME THE PORT SHOWS. A frozen tick has no tick to
            # step to, so this is the frozen-scene form of find_port_frame's adjacent-tick
            # fallback: another arcade dump of the SAME scene, after the anchor, at the
            # nearest game frame on either side that the port shows with the arcade's latch.
            # The exact pair is a SAMPLING fact, not a game one (measured 2026-10-24, sw317,
            # ares --watch-sdram on 0xFFF01C-21 over frames 1160-1312):
            #   - the port's wait_frame_flag clear lands at VDP line 0xEA or 0x1E6, either
            #     side of the frame boundary where the dump is taken (vcounter 0xEA->0x1E5),
            #     so in steady H the latch reads 1 at about 70% of port frames: 1306 (gf
            #     1326) is one of them, the clear arriving 0x18 dots into the next frame.
            #     The arcade's main loop is done long before its frame end (latch 0 at
            #     every H dump outside its 1165-1167 overrun);
            #   - after the high-score install the master body stalls two frames and then
            #     completes three game frames inside one (lines 0x61-0x7F of frame 1170),
            #     so game frames 1188/1189 end mid-frame and no dump can hold them.
            # The latch is still part of the key: a latch-1 dump is the main loop mid-frame.
            gf_a = game_frame(a)
            for k in sorted((k for k in range(len(fs))
                             if k != nth and fs[k][0] >= an[0]
                             and abs(game_frame(fs[k][2]) - gf_a) <= GF_NEAR),
                            key=lambda k: (abs(game_frame(fs[k][2]) - gf_a), fs[k][0])):
                fk, nk, bk = fs[k]
                frame = _port_at_game_frame(rom, workdir, t, pf + (fk - an[0]),
                                            game_frame(bk) + gf0, latch_of(bk))
                if frame is not None:
                    _GNOTE[(scene, target, nth)] = (
                        '  (port shows no complete game frame %d; compared at %s, %+d game frames)'
                        % (gf_a + gf0, nk, game_frame(bk) - gf_a))
                    arcfr, name, a = fk, nk, bk
                    break
        if frame is None:
            return (scene, target, nth), None, None, None, None, None, None, None
        p = dump_port(rom, frame, workdir)
    # DEMO_PAIRS=<dir>: keep the port image this row compared. tick_end() builds it in memory
    # from a write-watch replay, so it exists nowhere else, and tools/decomp_queue.py read the
    # frame-end dump instead and scored ~100 bytes on rows this gate reads clean (2026-10-25).
    # One file per row (the rows run in parallel threads): the image plus a JSON sidecar.
    if os.environ.get('DEMO_PAIRS'):
        import json as _json
        _d = os.environ['DEMO_PAIRS']
        os.makedirs(_d, exist_ok=True)
        _k = '%s_%04x_%d' % (scene, target, nth)
        open(os.path.join(_d, 'port_%s.bin' % _k), 'wb').write(bytes(p))
        _json.dump({'scene': scene, 'target': target, 'nth': nth, 'tick': t, 'arcade': name,
                    'port_frame': frame}, open(os.path.join(_d, 'pair_%s.json' % _k), 'w'))
    la, lp = live(a), live(p)
    diffs = []
    for s in sorted(set(la) | set(lp)):
        o = s * 0x80
        n = sum(1 for i in range(0x80) if a[o + i] != p[o + i])
        if n:
            diffs.append((s, n))
        # ... and the sprite-list entry that record owns. The record is what the game
        # computes; this is what it hands the hardware, so a record can match while the
        # picture does not -- 2026-10-05: handler 0x103E0's entries differed in byte +1
        # (the sprite's second y) and the address word at 240 of 319 swept ticks while
        # every record matched. The entry index is the record's own byte +8.
        eo = SPR + a[o + 8] * 0x10      # the entry index is the record's own byte +8; when
        m = sum(1 for i in range(0x10)   # the two sides disagree on it the record diff above
                if a[eo + i] != p[eo + i])   # has already said so
        if m:
            diffs.append(('e%d' % a[o + 8], m))
    pic = (pic_check(rom, scene, target, nth, frame, pic_t, game_frame(pic_p), latch_of(pic_p), workdir)
           if pic_wanted(scene, target, nth) else None)
    fb = fb_staging(rom, frame, workdir)
    if fb[0]:
        # A SCENE'S INSTALL FRAME IS STALE BY CONSTRUCTION: the game rewrites the whole
        # tilemap and the next flip is what replays it, so truth leads staging for a frame
        # or two. MEASURED 2026-10-06 at the high-score install -- 12 pages, worst 1791
        # words -- IDENTICALLY with the old all-13-pages restore and the new dirty-set one,
        # with the frames on either side clean in both. So the invariant is CONVERGENCE,
        # not instantaneous equality: stale here and still stale two flips later is a bug,
        # stale here alone is the cut.
        # ONE FLIP INTERVAL, WITH MARGIN. The transient runs until the next flip replays
        # the pages: measured 6 frames at the high-score install (1240-1245 stale, 1246
        # clean), so a +3 probe lands inside it and proves nothing. +8 and +16, and the
        # far one has to be clean.
        # AND THE WINDOW HAS TO BE GENEROUS, because the install frame MOVES WITH THE CLOCK
        # and the transient's length moves with it: measured 2026-10-07, two builds of the
        # same source anchored H at port frames 1222 and 1211 (calibrated clock x1.020 vs
        # x1.013), and the earlier one had not converged by +16 -- reported as a regression
        # that did not exist. The invariant is that it CONVERGES, so probe out to +64 and
        # pass on the first clean one.
        later = [(d, fb_staging(rom, frame + d, workdir)[0]) for d in (8, 16, 32, 64)]
        if any(x == [] for _, x in later):
            span = 'clean by +%d' % next(d for d, x in later if x == [])
            fb = ([], fb[1], 'install frame, %s' % span)
    return (scene, target, nth), t, frame, name, (a, p, la, lp), diffs, fb, pic


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rom = sys.argv[1]
    workdir = os.environ.get('DEMO_WORK', os.path.join(HERE, 'work'))
    os.makedirs(workdir, exist_ok=True)
    for n in os.listdir(workdir):
        if n.startswith('port_') or n.startswith('probe_'):
            os.remove(os.path.join(workdir, n))
    for _st in TARGETS:                 # the first target of each scene carries the check
        PIC_ONE.setdefault(_st[0], (_st[0], _st[1], _st[2] if len(_st) > 2 else 0))
    # THE GATE REFUSES AN UNNAMED PROBE. PORT_EXTRA builds are named now (Makefile
    # PORT_SLUG) and never write s16.32x, so a probe cannot reach this path by accident --
    # but a stale s16.32x from before that rule, or a hand-run gate with PORT_EXTRA set,
    # still could. The gate measures the DEFAULT port build and nothing else.
    # DEMO_PROBE=1 GATES A NAMED PROBE (2026-10-13). The refusal below stayed exactly as it
    # was -- an ACCIDENTAL probe (a stale s16.32x, a hand-run gate with PORT_EXTRA set)
    # still cannot reach the gate -- but it had also become a cap on what could land:
    # PORTPLOTW, PORTPLOTW again and PORTHOT were each measured and each went ungated on
    # logic, because every candidate worth measuring is a PORT_EXTRA build by construction.
    # The opt-in is explicit and it is reported on line 1 beside the rom, so a probe's gate
    # result can never be mistaken for the line's.
    _probe = os.environ.get('DEMO_PROBE') == '1'
    _named = os.path.basename(rom) != 's16.32x'
    if not _probe:
        if os.environ.get('PORT_EXTRA'):
            sys.exit('check_demo: PORT_EXTRA=%s is set. The gate measures the default port '
                     'build; a probe is named (build/port/rom/<slug>.32x). Set DEMO_PROBE=1 '
                     'to gate the named probe against the same targets.'
                     % os.environ['PORT_EXTRA'])
        if _named:
            sys.exit('check_demo: refusing %s -- the gate measures build/port/rom/s16.32x, '
                     'the default build. Set DEMO_PROBE=1 to gate this named probe.' % rom)
    if _probe and (_named or os.environ.get('PORT_EXTRA')):
        _bid = ''
        try:
            _bid = subprocess.run([sys.executable, os.path.join(HERE, '..', '..', 'tools', 'build_id.py'),
                                   'show', rom], capture_output=True, text=True).stdout.strip()
        except Exception:
            pass
        print('PROBE: %s   %s   PORT_EXTRA=%s'
              % (os.path.basename(rom), _bid or '(no build_id)',
                 os.environ.get('PORT_EXTRA', '(unset)')))
    rom, _tag = snapshot_rom(rom, workdir)
    print('rom: %s (snapshot of the build measured in this run)' % _tag)
    print('port clock: x%.3f of the arcade axis' % calibrate(rom, workdir))
    print('records: the port at each tick\'s END (frame-end dump + replayed SH-2 writes, cut '
          'at clr.b 0xFFF01C), the arcade\'s phase; H targets stay on frame-end dumps')
    print('picture: %s' % ('all 20 targets' if PIC_ALL else
          'one per scene (%s); PICTURE=all for every target'
          % ' '.join('%s %04x' % (k, v[1]) for k, v in sorted(PIC_ONE.items()))))
    arcframes = arcade_frames()
    arcade = {k: (v[0][1], v[0][2]) for k, v in arcframes.items()}
    missing = [st[:2] for st in TARGETS if st[:2] not in arcade]
    if missing:
        sys.exit('no arcade dump for %s in %s' % (' '.join('%s:%04x' % st for st in missing), ARC))
    # the port's frame for a tick: the arcade's demo-B start plus the tick, refined by
    # the first probe of each target (they run in parallel)
    with ThreadPoolExecutor(max_workers=min(8, len(TARGETS))) as ex:
        results = list(ex.map(lambda st: check_one(rom, st[0], st[1], arcade, workdir,
                                                   guess_frame(st[0], st[1]),
                                                   st[2] if len(st) > 2 else 0, arcframes), TARGETS))
    bad = 0
    print('%-7s %-8s %-10s %-12s %-16s %-5s %-6s %s' % ('target', 'at tick', 'port frame', 'arcade dump', 'hero x arc/port', 'live', 'bytes', 'live records differing (slot:bytes)'))
    fbbad = 0
    fbp12 = []
    picbad = 0
    for (scene, target, nth), t, frame, name, pack, diffs, fb, pic in results:
        if frame is None:
            print('%s %04x%-2s %-8s %-10s %-12s %-16s %-5s %-6s %s' % (scene, target, ('#%d' % nth) if nth else '', '-', '-', '-', '-', '-', '-', 'NOT REACHED: no port frame at this tick or its neighbours'))
            bad += 1
            continue
        a, p, la, lp = pack
        total = sum(n for _, n in diffs)
        same = 'same' if la == lp else 'DIFF'
        note = '' if t == target else '  (port skipped %04x inside a frame; compared at %04x)' % (target, t)
        note += _GNOTE.get((scene, target, nth), '')
        print('%s %04x%-2s %04x     %-10d %-12s %s/%s        %-5s %-6d %s%s' % (
            scene, target, ('#%d' % nth) if nth else '', t, frame, name, a[0xc:0xe].hex(), p[0xc:0xe].hex(), same, total,
            ' '.join('%s:%d' % d for d in diffs) if diffs else '-', note))
        if la != lp:
            print('        live arcade %s' % la)
            print('        live port   %s' % lp)
        if diffs or la != lp:
            bad += 1
        if pic and pic[0] is not None and pic[1] is not None and pic[0] > pic_threshold(pic[1]):
            print('        PICTURE %d of %d screen bytes differ from the baseline '
                  '(control %d, threshold %d)'
                  % (pic[0], PIC_CMP, pic[1], pic_threshold(pic[1])))
            picbad += 1
        elif pic and pic[2]:
            print('        picture: %s' % pic[2])
        if fb is not None and len(fb) > 2:
            print('        fb staging transient: %s' % fb[2])
        if fb is not None and fb[0] is not None:
            if fb[0]:
                print('        FB STAGING STALE vs truth: %s   (page:words)'
                      % ' '.join('%d:%d' % x for x in fb[0]))
                fbbad += 1
            fbp12.append(fb[1])
    if fbp12:
        print('fb staging: pages 0-11 match SDRAM truth at %d of %d ticks; page 12 (blank) '
              '%d-%d words' % (len(fbp12) - fbbad, len(fbp12), min(fbp12), max(fbp12)))
    if picbad:
        print('picture: %d of %d ticks differ from the baseline beyond their control'
              % (picbad, len(TARGETS)))
    if bad or fbbad or picbad:
        print('check-demo: %d of %d ticks differ%s' % (bad, len(TARGETS),
              ', %d with stale FB staging' % fbbad if fbbad else ''))
        sys.exit(1)
    print('check-demo: every live record matches the arcade at all %d ticks, and the FB\'s '
          'tilemap staging matches truth' % len(TARGETS))


if __name__ == '__main__':
    main()
