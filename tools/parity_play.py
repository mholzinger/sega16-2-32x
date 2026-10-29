#!/usr/bin/env python3
"""parity_play.py: the PORT side of the gameplay oracle.

Plays an input tape into the port rom on headless ares and writes the same corpus
that tests/gameplay/arcade_oracle.sh (tools/gameplay_dump.lua) writes for the arcade,
so the two can be compared row for row with tests/gameplay/milestones.py:

  <out>/timeline.csv   one row per emulated frame (frames 1..N), header exactly
    frame,tick,gameframe,level,round,scroll,px,py,pstat,phand,lives,score,credits,
    f028,f14b,f154,pad,boss_stat,boss_hand,step,demo
    hex without 0x except frame (decimal); gameframe is check_demo.game_frame()
    over 0xFFF020/21.
  <out>/census.csv     frame,slot,status,handler for every active slot 1-63 (status
    bit 15), every --census-every frames.
  <out>/ev_<frame>_{wram,tilemap,text,sprite,palette,health}.bin and
  <out>/ev_<frame>.png  one ares run per event frame: the --milestones frames plus
    the frames where 0xFFF142, 0xFFF14E, 0xFFF14B, 0xFFF028 or the slot 40 status
    change in the timeline, capped at --max-events (explicit milestones first, then
    level/round, f14b, slot 40, f028 in that order of priority). The screenshot is
    frame-1 so it is the picture presented when the dump was taken.
  <out>/health.txt     the port's _port_l2_out block from the LAST event dump:
    u32[13] missed indirect dispatches (printed loudly when nonzero), u32[40..47]
    missed targets, u32[48..55] occupancy.
  <out>/raw/           the ares logs and watch CSVs the above were decoded from.

Memory: the 68K work RAM 0xFFC000-0xFFFFFF is mirrored in 32X SDRAM at offset
0x2CC00 (sdram_off = 0x2CC00 + (addr - 0xFFC000)); the video shadows are tilemap
0x19000, text 0x26000, sprite 0x28400, palette 0x27000 (STATE.md, port_l2.c).

Instrument: /Users/mikeholzinger/src/bin/ares-headless-sdram (A1 build, branch
headless-32x-sdramwatch) whose --gameframe-watch takes an sdram: selector. Its log
samples at the START of each frame, so log row N is the state after N frames and the
same bytes a "--dump ... --frames N" run writes; the timeline therefore comes from a
--frames N+1 run with row 0 dropped. If that binary is missing, the tool falls back
to the stock ares-headless, one --dump run per sample frame every --sample-every
frames (8 in parallel), and says so.

Run:
  python3 tools/parity_play.py build/port/rom/hp1.32x --tape tools/inputs/level1_full.csv \\
      --out build/parity/hp1 --frames 36000 --milestones 28316,31804
Then:
  python3 tests/gameplay/milestones.py build/parity/hp1
"""
import argparse
import csv
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

ARES_SDRAM = '/Users/mikeholzinger/src/bin/ares-headless-sdram'
ARES_SDRAM_GF = '/Users/mikeholzinger/src/bin/ares-headless-sdram-gf'
ARES_OLD = os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')

WRAM_BASE = 0xFFC000
WRAM_SDRAM = 0x2CC00
WRAM_LEN = 0x4000
SLOT = 0x80

# video shadows in SDRAM (name, offset, length)
SHADOWS = [
    ('wram', WRAM_SDRAM, WRAM_LEN),
    ('tilemap', 0x19000, 0xD000),
    ('text', 0x26000, 0x1000),
    ('sprite', 0x28400, 0x400),
    ('palette', 0x27000, 0x1000),
]
HEALTH_LEN = 0x100

# watch windows for the timeline run: (name, 68K address, length)
W_SLOT0 = ('slot0', WRAM_BASE, 0x20)
W_SLOT40 = ('slot40', WRAM_BASE + 40 * SLOT, 0x8)      # 0xFFD400
W_E000 = ('e000', 0xFFE000, 0x30)
W_F000 = ('f000', 0xFFF000, 0x160)
TIMELINE_WATCHES = [W_SLOT0, W_SLOT40, W_E000, W_F000]
W_TABLE = ('table', WRAM_BASE, 0x2000)

HEADER = ('frame,tick,gameframe,level,round,scroll,px,py,pstat,phand,lives,score,credits,'
          'f028,f14b,f154,pad,boss_stat,boss_hand,step,demo')


def sdram_off(addr):
    return WRAM_SDRAM + (addr - WRAM_BASE)


def game_frame(f000):
    """check_demo.game_frame() over the 0xFFF000 block (b[0x20] = 0xFFF020, b[0x21] = 0xFFF021)."""
    n = f000[0x20]
    return sum(60 if (i & 1) else 30 for i in range(n)) + ((60 if (n & 1) else 30) - 1 - f000[0x21])


def u16(b, o):
    return (b[o] << 8) | b[o + 1]


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def decode_row(frame, slot0, slot40, e000, f000):
    """One timeline row from the four windows (bytes)."""
    return ','.join([
        str(frame),
        '%04x' % u16(f000, 0x2A),
        str(game_frame(f000)),
        '%02x' % f000[0x142],
        '%02x' % f000[0x14E],
        '%04x' % u16(f000, 0x132),
        '%04x' % u16(slot0, 0xC),
        '%04x' % u16(slot0, 0x10),
        '%04x' % u16(slot0, 0),
        '%08x' % u32(slot0, 2),
        '%02x' % e000[0x18],
        '%08x' % u32(e000, 0),
        '%02x' % f000[0],
        '%02x' % f000[0x28],
        '%02x' % f000[0x14B],
        '%02x' % f000[0x154],
        '%02x' % e000[0x28],
        '%04x' % u16(slot40, 0),
        '%08x' % u32(slot40, 2),
        '%02x' % f000[0x31],
        '%d' % (f000[0x26] & 1),
    ])


def row_from_wram(frame, w):
    """Timeline row from a full 0x4000-byte work RAM image."""
    s0 = w[0:0x20]
    s40 = w[40 * SLOT:40 * SLOT + 8]
    e = w[0xFFE000 - WRAM_BASE:0xFFE000 - WRAM_BASE + 0x30]
    f = w[0xFFF000 - WRAM_BASE:0xFFF000 - WRAM_BASE + 0x160]
    return decode_row(frame, s0, s40, e, f)


def census_rows(frame, table):
    out = []
    for slot in range(1, 64):
        o = slot * SLOT
        st = u16(table, o)
        if st & 0x8000:
            out.append('%d,%d,%04x,%08x' % (frame, slot, st, u32(table, o + 2)))
    return out


def health_addr(lst_path):
    with open(lst_path) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) >= 3 and parts[-1] == '_port_l2_out':
                return int(parts[0], 16)
    return None


class Log:
    def __init__(self, path):
        self.fh = open(path, 'a')
        self.lock = threading.Lock()
        self.t0 = time.time()

    def __call__(self, msg):
        line = '[%7.1fs] %s' % (time.time() - self.t0, msg)
        with self.lock:
            print(line, flush=True)
            self.fh.write(line + '\n')
            self.fh.flush()


def run_ares(ares, rom, args, log, logfile):
    cmd = [ares] + args + [rom]
    log('run: ' + ' '.join(cmd))
    with open(logfile, 'w') as fh:
        fh.write(' '.join(cmd) + '\n')
        fh.flush()
        r = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        log('FAILED rc=%d: %s (see %s)' % (r.returncode, ' '.join(cmd), logfile))
    return r.returncode


NM = os.environ.get('SH_NM', os.path.expanduser('~/src/marsdev/mars/sh-elf/bin/sh-elf-nm'))


def sym_sdram(rom, name):
    """SDRAM offset of a port symbol, from the ELF the build keeps beside the rom."""
    elf = rom[:-4] + '.elf'
    if os.path.exists(elf):
        for l in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
            if l.endswith(' ' + name):
                return int(l.split()[0], 16) - 0x06000000
    return None


def gf_steps(gameframes):
    """Game frames since boot per row, from check_demo.game_frame() values: the steps of the
    blink clock, a step over 4 counting 0 (the boot-time init). The same unsigned 32-bit rule
    as port_gf_count in sh_src/port_l2.c, so both sides count identically."""
    out, cnt, prev = [], 0, None
    for g in gameframes:
        g &= 0xFFFFFFFF
        if prev is not None:
            d = ((g + 11520 - prev) & 0xFFFFFFFF) % 11520
            if d <= 4:
                cnt = (cnt + d) & 0xFFFF
        prev = g
        out.append(cnt)
    return out


def keyed_tape(tape, arcade, out, shift_from=None, shift=0):
    """The tape re-keyed from arcade wall frames to arcade game-frame counts (port_gf_count).
    An event the arcade applied at the start of frame f was read by the pass that ended frame
    f, so on the port it must land once the count reaches the value BEFORE that pass, S(f-1).
    A press must outlive the keying (docs/design/ORACLE-PLAYTHROUGH.md): per button the keys
    are made strictly increasing, so a press and its release never land on one game frame."""
    gfs = {}
    with open(os.path.join(arcade, 'timeline.csv')) as fh:
        for r in csv.DictReader(fh):
            gfs[int(r['frame'])] = int(r['gameframe'])
    frames = sorted(gfs)
    steps = dict(zip(frames, gf_steps([gfs[f] for f in frames])))
    last = {}
    rows = []
    with open(tape) as fh:
        for line in fh:
            if not line.strip() or line.startswith('#'):
                rows.append(line.rstrip('\n'))
                continue
            f = line.split(',')
            fr = int(f[0])
            k = steps.get(fr - 1, 0)
            if shift_from is not None and fr >= shift_from:
                k += shift
            key = (f[1], f[3].strip() if len(f) > 3 else '1')
            if key in last and k <= last[key]:
                k = last[key] + 1
            last[key] = k
            rows.append(','.join([str(k)] + f[1:]).rstrip('\n'))
    with open(out, 'w') as fh:
        fh.write('\n'.join(rows) + '\n')
    return out


def calibrate_keyed(a, raw, log):
    """Shift the keyed tape's PLAY events by the measured counter offset. The two machines'
    blink-step counts drift apart across the attract/coin transitions and then hold steady in
    play (measured 2026-10-25: port = arcade + 20 at eight ticks from 0x80 to 0x400), and with
    the raw count the tape's first walk input landed ~20 game frames early: the 'divergence'
    at arcade frame 1497 was that input, not the port. One short keyed run finds the port's
    count at the first in-play tick the arcade reaches before any play input; every event the
    arcade applied at or after that frame is re-keyed by the difference."""
    A = list(csv.DictReader(open(os.path.join(a.key_arcade, 'timeline.csv'))))
    af = [int(r['frame']) for r in A]
    S = dict(zip(af, gf_steps([int(r['gameframe']) for r in A])))
    events = [l.split(',') for l in open(a.tape) if l.strip() and not l.startswith('#')]
    start = min((int(e[0]) for e in events if e[1] == 'start'), default=0)
    play = [int(e[0]) for e in events if int(e[0]) > start + 60]
    if not play:
        return
    first_play = min(play)
    # the calibration tick: the arcade's scene timer at the frame just before the first play
    # input, provided it is in play (the timer counts up there, one per game frame)
    cal = None
    for r in A:
        f = int(r['frame'])
        if start + 60 < f < first_play and int(r['tick'], 16) >= 0x20:
            cal = (f, r['tick'])
    if cal is None:
        log('calibration: no in-play tick before the first play input; tape left unshifted')
        return
    fa, tick = cal
    clog = os.path.join(raw, 'calibrate_watch.csv')
    run_ares(a.ares, a.rom, ['--frames', str(fa + 200)] + input_args(a) +
             ['--gameframe-log', clog, '--gameframe-watch',
              ('sdram:0x%X:2' % (a.mirror + MIRROR_AT['f000'] + 0x2A)) if a.mirror is not None
              else 'sdram:0x%X:2' % sdram_off(0xFFF02A)],
             log, os.path.join(raw, 'calibrate_ares.log'))
    fp = None
    for r in csv.reader(open(clog)):
        if r and r[0].isdigit() and len(r) > 2 and r[2].upper() == tick.upper() and int(r[0]) > start:
            fp = int(r[0])
            cnt = int(r[1])
            break
    if fp is None:
        raise SystemExit('parity_play: calibration found no port frame at tick %s' % tick)
    # both sides are the state just after the pass that set the tick: the log row is sampled
    # at frame fp's start, the arcade row at frame fa's end
    off = cnt - S[fa]
    keyed_tape(a.tape, a.key_arcade, a.keyed, shift_from=fa, shift=off)
    log('calibration: arcade frame %d tick %s, port frame %d count %d, offset %+d applied to events from frame %d'
        % (fa, tick, fp, cnt, off, fa))


# where each timeline watch lives inside the port's tick-end mirror (sh_src/port_l2.c
# port_tl_mirror, measurement builds): same windows, same order as TIMELINE_WATCHES
MIRROR_AT = {'slot0': 0x00, 'slot40': 0x20, 'e000': 0x28, 'f000': 0x58}


def timeline_watch(a, name, addr, ln):
    """The watch selector for one timeline window: the tick-end mirror when the rom has one,
    else the live work-RAM image (sampled mid-tick about half the time)."""
    if a.mirror is not None:
        return 'sdram:0x%X:0x%X' % (a.mirror + MIRROR_AT[name], ln)
    return 'sdram:0x%X:0x%X' % (sdram_off(addr), ln)


def input_args(a):
    """The --input arguments every ares run of this replay takes: the plain tape, or the
    keyed tape with --input-gameframe. A keyed run must NOT also pass --gameframe: ares
    parses it second and it would re-point the key at that address."""
    if a.rom_tape:
        return ['--pad1', a.pad1]           # the rom applies the tape itself (PORTTAPEPAD)
    if a.keyed:
        return ['--input', a.keyed, '--input-gameframe', 'sdram:0x%X' % a.gf_off, '--pad1', a.pad1]
    return ['--input', a.tape, '--pad1', a.pad1]


def gf_arg(a, addr):
    return [] if a.keyed else ['--gameframe', 'sdram:0x%X' % sdram_off(addr)]


def parse_watch_log(path):
    """Yield (frame, [bytes or None per watch column]) from an ares --gameframe-log."""
    with open(path) as fh:
        rd = csv.reader(fh)
        header = next(rd)
        ncol = len(header) - 2
        for row in rd:
            if not row:
                continue
            frame = int(row[0])
            cols = []
            for i in range(ncol):
                h = row[2 + i] if 2 + i < len(row) else ''
                cols.append(bytes.fromhex(h) if h else None)
            yield frame, cols


def sdram_timeline(a, out, raw, log):
    """Timeline and census from two parallel watch runs on the sdram-aware binary."""
    n = a.frames
    tl_log = os.path.join(raw, 'timeline_watch.csv')
    cs_log = os.path.join(raw, 'census_watch.csv')
    tl_args = ['--frames', str(n + 1)] + input_args(a) + gf_arg(a, 0xFFF02A) + ['--gameframe-log', tl_log]
    for name, addr, ln in TIMELINE_WATCHES:
        tl_args += ['--gameframe-watch', timeline_watch(a, name, addr, ln)]
    cs_args = ['--frames', str(n + 1)] + input_args(a) + gf_arg(a, 0xFFF02A) + ['--gameframe-log', cs_log,
               '--gameframe-watch', 'sdram:0x%X:0x%X' % (sdram_off(W_TABLE[1]), W_TABLE[2]),
               '--gameframe-watch-every', str(a.census_every)]
    with ThreadPoolExecutor(2) as ex:
        f1 = ex.submit(run_ares, a.ares, a.rom, tl_args, log, os.path.join(raw, 'timeline_ares.log'))
        f2 = ex.submit(run_ares, a.ares, a.rom, cs_args, log, os.path.join(raw, 'census_ares.log'))
        rc = f1.result() | f2.result()
    if rc:
        raise SystemExit('ares timeline/census run failed')
    rows = []
    with open(os.path.join(out, 'timeline.csv'), 'w') as fh:
        fh.write(HEADER + '\n')
        for frame, cols in parse_watch_log(tl_log):
            if frame < 1 or frame > n:
                continue
            s0, s40, e, f = cols
            line = decode_row(frame, s0, s40, e, f)
            fh.write(line + '\n')
            rows.append(line)
    with open(os.path.join(out, 'census.csv'), 'w') as fh:
        fh.write('frame,slot,status,handler\n')
        for frame, cols in parse_watch_log(cs_log):
            if frame < 1 or frame > n or cols[0] is None:
                continue
            for line in census_rows(frame, cols[0]):
                fh.write(line + '\n')
    log('timeline.csv %d rows, census.csv written' % len(rows))
    return rows


def sampled_timeline(a, out, raw, log):
    """Fallback for the stock binary: one --dump run per sample frame, 8 in parallel."""
    frames = list(range(a.sample_every, a.frames + 1, a.sample_every))
    if a.frames not in frames:
        frames.append(a.frames)
    log('FALLBACK: %s missing, sampling %d frames every %d with %s (one run per frame)'
        % (ARES_SDRAM, len(frames), a.sample_every, a.ares))

    def one(fr):
        bin_ = os.path.join(raw, 'sample_%d_wram.bin' % fr)
        args = ['--frames', str(fr)] + input_args(a) + [
                '--dump', 'sdram:0x%X:0x%X:%s' % (WRAM_SDRAM, WRAM_LEN, bin_)]
        rc = run_ares(a.ares, a.rom, args, log, os.path.join(raw, 'sample_%d.log' % fr))
        return fr, bin_, rc

    rows, cens = [], []
    with ThreadPoolExecutor(8) as ex:
        for fr, bin_, rc in ex.map(one, frames):
            if rc:
                continue
            with open(bin_, 'rb') as fh:
                w = fh.read()
            rows.append(row_from_wram(fr, w))
            cens += census_rows(fr, w[:0x2000])
    with open(os.path.join(out, 'timeline.csv'), 'w') as fh:
        fh.write(HEADER + '\n')
        fh.write('\n'.join(rows) + '\n')
    with open(os.path.join(out, 'census.csv'), 'w') as fh:
        fh.write('frame,slot,status,handler\n')
        fh.write('\n'.join(cens) + '\n')
    log('SAMPLED timeline.csv %d rows (every %d frames), census.csv written' % (len(rows), a.sample_every))
    return rows


def event_frames(rows, explicit, cap, log):
    """Explicit milestones plus the frames where the tracked fields change, capped."""
    cols = HEADER.split(',')
    ix = {c: i for i, c in enumerate(cols)}
    groups = {'level': [], 'round': [], 'f14b': [], 'boss_stat': [], 'f028': []}
    prev = None
    for line in rows:
        r = line.split(',')
        if prev is not None:
            for k in groups:
                if r[ix[k]] != prev[ix[k]]:
                    groups[k].append(int(r[ix['frame']]))
        prev = r
    chosen = []
    for fr in explicit:
        if fr not in chosen:
            chosen.append(fr)
    dropped = []
    for k in ('level', 'round', 'f14b', 'boss_stat', 'f028'):
        for fr in groups[k]:
            if fr in chosen:
                continue
            if len(chosen) < cap:
                chosen.append(fr)
            else:
                dropped.append((k, fr))
    log('events: explicit %s; changes level %d round %d f14b %d boss_stat %d f028 %d; chosen %d, dropped %d'
        % (explicit, len(groups['level']), len(groups['round']), len(groups['f14b']),
           len(groups['boss_stat']), len(groups['f028']), len(chosen), len(dropped)))
    if dropped:
        log('dropped (over --max-events): ' + ' '.join('%s@%d' % d for d in dropped[:40]))
    return sorted(chosen)


def event_dump(a, out, raw, fr, haddr, log):
    args = ['--frames', str(fr)] + input_args(a)
    for name, off, ln in SHADOWS:
        args += ['--dump', 'sdram:0x%X:0x%X:%s' % (off, ln, os.path.join(out, 'ev_%d_%s.bin' % (fr, name)))]
    if haddr is not None:
        args += ['--dump', 'sdram:0x%X:0x%X:%s' % (haddr, HEALTH_LEN, os.path.join(out, 'ev_%d_health.bin' % fr))]
    if fr >= 1:
        args += ['--screenshot', '%d:%s' % (fr - 1, os.path.join(out, 'ev_%d.png' % fr))]
    return fr, run_ares(a.ares, a.rom, args, log, os.path.join(raw, 'ev_%d.log' % fr))


def write_health(out, frames, log):
    done = [fr for fr in frames if os.path.exists(os.path.join(out, 'ev_%d_health.bin' % fr))]
    if not done:
        log('health: no health dump available')
        return
    fr = max(done)
    with open(os.path.join(out, 'ev_%d_health.bin' % fr), 'rb') as fh:
        h = fh.read()
    words = struct.unpack('>%dI' % (len(h) // 4), h)
    missed = words[13]
    targets = words[40:48]
    occ = words[48:56]
    lines = ['health from ev_%d_health.bin (_port_l2_out)' % fr,
             'missed_indirect_dispatches %d' % missed,
             'missed_targets ' + ' '.join('%08x' % t for t in targets),
             'occupancy ' + ' '.join('%d' % o for o in occ)]
    if missed:
        lines.insert(0, '!!!!! MISSED INDIRECT DISPATCHES: %d (must be 0) !!!!!' % missed)
    with open(os.path.join(out, 'health.txt'), 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    for l in lines:
        log('health: ' + l)
    if missed:
        print('\n' + '!' * 70 + '\nMISSED INDIRECT DISPATCHES: %d (frame %d) -- the port took an unmapped thunk\n' % (missed, fr) + '!' * 70, file=sys.stderr)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('rom')
    p.add_argument('--tape', required=True)
    p.add_argument('--out', default=None, help='default build/parity/<romname>/')
    p.add_argument('--frames', type=int, default=36000)
    p.add_argument('--census-every', type=int, default=60)
    p.add_argument('--milestones', default='', help='comma-separated event frames to dump')
    p.add_argument('--max-events', type=int, default=24)
    p.add_argument('--jobs', type=int, default=6, help='parallel event dump runs')
    p.add_argument('--ares', default=None, help='default %s, else the stock ares-headless' % ARES_SDRAM)
    p.add_argument('--sample-every', type=int, default=100, help='fallback sampling period (stock binary)')
    p.add_argument('--lst', default=None, help='s16.lst for _port_l2_out (default beside the rom)')
    p.add_argument('--pad1', default='fighting')
    p.add_argument('--no-events', action='store_true', help='timeline and census only')
    p.add_argument('--rom-tape', action='store_true',
                   help='the rom carries the tape (make ... PORTTAPEPAD=); ares gets no --input')
    p.add_argument('--key-arcade', default=None, metavar='DIR',
                   help='key the tape to game frames (port_gf_count) using this arcade corpus timeline')
    a = p.parse_args()

    a.rom = os.path.abspath(a.rom)
    a.tape = os.path.abspath(a.tape)
    name = os.path.splitext(os.path.basename(a.rom))[0]
    out = os.path.abspath(a.out or os.path.join('build', 'parity', name))
    raw = os.path.join(out, 'raw')
    os.makedirs(raw, exist_ok=True)
    log = Log(os.path.join(out, 'run.log'))
    log('parity_play ' + ' '.join(sys.argv))

    sdram_ok = a.ares is None and os.access(ARES_SDRAM, os.X_OK)
    if a.ares is None:
        a.ares = ARES_SDRAM if sdram_ok else ARES_OLD
    elif os.path.basename(a.ares) == 'ares-headless-sdram' or 'sdram' in os.path.basename(a.ares):
        sdram_ok = True
    log('ares %s (sdram watch %s)' % (a.ares, 'yes' if sdram_ok else 'NO, sampling fallback'))
    a.keyed = None
    if a.rom_tape and sym_sdram(a.rom, '_tp_key') is None and sym_sdram(a.rom, '_l2m_io_tape') is None:
        log('WARNING: --rom-tape but no tape symbol found in %s (stripped statics?); trusting the build flags' % a.rom)
    a.mirror = sym_sdram(a.rom, '_port_tl_mirror')
    log('timeline source: %s' % ('tick-end mirror at sdram 0x%X' % a.mirror if a.mirror is not None
                                 else 'live work RAM (mid-tick samples possible)'))
    if a.key_arcade and not a.rom_tape:
        # THE STOCK FORK CANNOT KEY THIS PORT: during boot frames 5-8 every SDRAM word holds
        # warm-up garbage, ares read the counter as e.g. 64707 and fired every tape event up
        # to it at boot. The -gf build keys on min(counter, frame) (ares-debug branch
        # headless-32x-sdramwatch + that one line, 2026-10-25).
        if a.ares in (ARES_SDRAM, ARES_OLD):
            if not os.access(ARES_SDRAM_GF, os.X_OK):
                raise SystemExit('parity_play: --key-arcade needs %s (min(counter, frame) keying)' % ARES_SDRAM_GF)
            a.ares, sdram_ok = ARES_SDRAM_GF, True
            log('ares %s for the keyed replay' % a.ares)
        if not sdram_ok:
            raise SystemExit('parity_play: --key-arcade needs the sdram-aware ares (%s)' % ARES_SDRAM)
        a.gf_off = sym_sdram(a.rom, '_port_gf_count')
        if a.gf_off is None:
            raise SystemExit('parity_play: %s has no _port_gf_count (a rom older than the keyed replay, '
                             'or no .elf beside it)' % a.rom)
        a.keyed = keyed_tape(a.tape, a.key_arcade, os.path.join(raw, 'tape_gamekeyed.csv'))
        log('tape keyed to game frames: %s, counter sdram 0x%X' % (a.keyed, a.gf_off))
        calibrate_keyed(a, raw, log)

    # the rom's own map first (make port keeps <name>.lst beside <name>.32x), else the
    # shared s16.lst, which belongs to the LAST build and may not be this rom's
    named = os.path.splitext(a.rom)[0] + '.lst'
    lst = a.lst or (named if os.path.exists(named) else os.path.join(os.path.dirname(a.rom), 's16.lst'))
    haddr = None
    if a.lst is None and lst != named and os.path.exists(lst):
        # the shared map is rewritten by every build; one newer than the rom is some other
        # build's and would point the health dump at 256 bytes of something else (hp1 read
        # hp0's map on 2026-10-22; same address by luck). The port target keeps <name>.lst
        # beside every named rom; an older tree passes --lst explicitly.
        dl, dr = os.path.getmtime(lst), os.path.getmtime(a.rom)
        if dl > dr + 2 or dl < dr - 600:
            raise SystemExit('parity_play: %s (mtime %s) is not this rom\'s map (rom mtime %s): no %s; '
                             'rebuild with make port (it copies the map) or pass --lst'
                             % (lst, time.ctime(dl), time.ctime(dr), named))
    if os.path.exists(lst):
        sym = health_addr(lst)
        if sym is not None:
            haddr = sym - 0x06000000
            log('_port_l2_out %08x from %s (sdram 0x%X); lst mtime %s, rom mtime %s'
                % (sym, lst, haddr, time.ctime(os.path.getmtime(lst)), time.ctime(os.path.getmtime(a.rom))))
            shutil.copy(lst, os.path.join(raw, 'symbols.lst'))
    if haddr is None:
        log('WARNING: _port_l2_out not found (%s); no health dump' % lst)

    t0 = time.time()
    if sdram_ok:
        rows = sdram_timeline(a, out, raw, log)
    else:
        rows = sampled_timeline(a, out, raw, log)
    log('timeline+census wall %.1fs' % (time.time() - t0))

    if not a.no_events:
        explicit = [int(x, 0) for x in a.milestones.split(',') if x.strip()]
        frames = event_frames(rows, explicit, a.max_events, log)
        t1 = time.time()
        with ThreadPoolExecutor(a.jobs) as ex:
            # longest runs first so the pool tail is short
            res = list(ex.map(lambda fr: event_dump(a, out, raw, fr, haddr, log), sorted(frames, reverse=True)))
        bad = [fr for fr, rc in res if rc]
        log('event dumps: %d ok, %d failed %s, wall %.1fs' % (len(res) - len(bad), len(bad), bad, time.time() - t1))
        write_health(out, frames, log)

    log('total wall %.1fs; outputs in %s' % (time.time() - t0, out))
    ms = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'gameplay', 'milestones.py')
    if os.path.exists(ms):
        r = subprocess.run([sys.executable, ms, out], capture_output=True, text=True)
        for l in (r.stdout + r.stderr).splitlines():
            log('milestones: ' + l)


if __name__ == '__main__':
    main()
