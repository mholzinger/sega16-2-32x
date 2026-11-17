#!/usr/bin/env python3
"""Shoot a played level on the port AND the arcade at the same game ticks, then look.

    python3 tools/play_pairs.py capture ROM.32x [--out DIR] [--step 50] [--frames 36000]
            [--tape tools/inputs/level1_full.csv] [--offset 33]
    python3 tools/play_pairs.py list  [--out DIR]            frame table with a luma score
    python3 tools/play_pairs.py sheet [--out DIR] NAME f1 f2 ...   rows of [port | arcade | diff]
    python3 tools/play_pairs.py zoom  [--out DIR] NAME f x0 y0 x1 y1 [--scale 4]
    python3 tools/play_pairs.py red   [--out DIR] [--rows 100:190]   pure-red / pure-white px per frame

Made 2026-11-05 when Mike said "capture screens where you think you know they should be;
this frees me from playing the game over and over". ROM is a PORTTAPEPAD god-mode rom (make
parity-candidate NAME=x TAPEPAD=1 EXTRA='<the candidate's flags>'): the tape is baked in, so
the port follows the arcade's route tick for tick until the boss (REPORT.md section 3b), and
ares-headless shoots it every STEP host frames. The arcade is run once in MAME on the same tape
(tools/gameplay_dump.lua GD_FRAMES + GD_ONCHANGE) and shot at frame + OFFSET, the host-frame
lead the harness measures ("start taken" arcade 902 vs port 869 = 33 on p15bpt2g).

What it found the first time: the "red wolf" of STATE 3372 is the ARCADE'S OWN flash (sprite
palette 8 = all red, one frame in four, measured 0.25 on the arcade and 0.35 of PRESENTED
frames on the port, where a two-vint pass holds the red frame), seen on the port and not the
arcade only because the pair lands one tick apart. Pairs are a view, not a gate: a scrolling
scene one frame apart scores luma 20+ with nothing wrong, so LOOK at the sheet.
"""
import argparse
import csv
import os
import subprocess
import sys
import tempfile
import shutil

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from active_area import crop_active   # noqa: E402

ARES = os.path.expanduser('~/src/bin/ares-headless-ssf')
MAME_FLAGS = ['-rompath', './mame', '-skip_gameinfo', '-video', 'none', '-sound', 'none', '-nothrottle',
              '-window', '-resolution', '160x120', '-keyboardprovider', 'none', '-nomouse', '-nojoystick']


def capture(a):
    port = os.path.join(a.out, 'port'); arc = os.path.join(a.out, 'arcade')
    os.makedirs(port, exist_ok=True); os.makedirs(arc, exist_ok=True)
    frames = list(range(a.start, a.frames, a.step))
    cmd = [ARES, a.rom, '--frames', str(a.frames), '--pad1', 'fighting']
    for f in frames:
        cmd += ['--screenshot', '%d:%s/o_%06d.png' % (f, port, f)]
    p = subprocess.Popen(cmd, stdout=open(port + '/ares.log', 'w'), stderr=subprocess.STDOUT, cwd=ROOT)
    nv = tempfile.mkdtemp(); cf = tempfile.mkdtemp()
    env = dict(os.environ, GD_IN=a.tape, GD_FRAMES=','.join(str(f + a.offset) for f in frames),
               GD_END=str(a.frames + a.offset), GD_ADDR='0xFFC000', GD_LEN='0x4000', GD_OUTDIR=arc, GD_GOD='1',
               GD_TIMELINE=arc + '/timeline.csv', GD_CENSUS=arc + '/census.csv', GD_CENSUS_EVERY='60',
               GD_ONCHANGE='1', GD_ONCHANGE_MAX=str(len(frames) + 1000))
    m = subprocess.Popen(['mame', 'altbeast'] + MAME_FLAGS + ['-bench', str((a.frames + a.offset) // 60 + 20),
                          '-nvram_directory', nv, '-cfg_directory', cf, '-snapshot_directory', arc,
                          '-autoboot_script', 'tools/gameplay_dump.lua'],
                         stdout=open(arc + '/mame.log', 'w'), stderr=subprocess.STDOUT, cwd=ROOT, env=env)
    rp = p.wait(); rm = m.wait()
    shutil.rmtree(nv, ignore_errors=True); shutil.rmtree(cf, ignore_errors=True)
    open(os.path.join(a.out, 'offset'), 'w').write('%d\n' % a.offset)
    np_ = len([x for x in os.listdir(port) if x.endswith('.png')])
    na = len([x for x in os.listdir(arc) if x.startswith('ev_') and x.endswith('.png')])
    print('capture: port %d shots (ares exit %d), arcade %d shots (mame exit %d) -> %s' % (np_, rp, na, rm, a.out))


def offset(out):
    try:
        return int(open(os.path.join(out, 'offset')).read())
    except OSError:
        return 33


def port_im(out, f):
    return crop_active(Image.open('%s/port/o_%06d.png' % (out, f)).convert('RGB'))


def arc_im(out, f):
    p = '%s/arcade/ev_%d.png' % (out, f + offset(out))
    return Image.open(p).convert('RGB') if os.path.exists(p) else None


def frames_of(out):
    return sorted(int(x[2:8]) for x in os.listdir(os.path.join(out, 'port')) if x.startswith('o_'))


def luma(im):
    return np.asarray(im.convert('L'), dtype=np.int16)


def cmd_list(a):
    print('  port arcade  luma')
    for f in frames_of(a.out):
        ar = arc_im(a.out, f)
        if ar is None:
            continue
        print('%6d %6d %6.1f' % (f, f + offset(a.out), np.abs(luma(port_im(a.out, f)) - luma(ar)).mean()))


def cmd_sheet(a):
    box = (0, 80, 320, 200)
    rows = []
    for f in a.frames:
        p = port_im(a.out, f).crop(box); ar = arc_im(a.out, f)
        ar = ar.crop(box) if ar else Image.new('RGB', (320, 120))
        d = np.abs(np.asarray(p, dtype=np.int16) - np.asarray(ar, dtype=np.int16)).sum(axis=2)
        rows.append((p, ar, Image.fromarray(np.clip(d, 0, 255).astype(np.uint8)).convert('RGB')))
    out = Image.new('RGB', (3 * 324, len(rows) * 124))
    for i, (p, ar, dm) in enumerate(rows):
        out.paste(p, (0, i * 124)); out.paste(ar, (324, i * 124)); out.paste(dm, (648, i * 124))
    path = os.path.join(a.out, a.name + '.png')
    out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(path); print(path)


def cmd_zoom(a):
    x0, y0, x1, y1 = a.box
    p = port_im(a.out, a.frame).crop((x0, y0, x1, y1)); ar = arc_im(a.out, a.frame).crop((x0, y0, x1, y1))
    w, h = p.size; sc = a.scale
    out = Image.new('RGB', (2 * (w * sc + 4), h * sc))
    out.paste(p.resize((w * sc, h * sc), Image.NEAREST), (0, 0))
    out.paste(ar.resize((w * sc, h * sc), Image.NEAREST), (w * sc + 4, 0))
    path = os.path.join(a.out, a.name + '.png'); out.save(path); print(path)


def cmd_red(a):
    y0, y1 = (int(v) for v in a.rows.split(':'))

    def count(im):
        x = np.asarray(im, dtype=np.int16)[y0:y1]
        red = ((x[..., 0] >= 240) & (x[..., 1] <= 20) & (x[..., 2] <= 20)).sum()
        white = ((x[..., 0] >= 250) & (x[..., 1] >= 250) & (x[..., 2] >= 250)).sum()
        return int(red), int(white)
    print(' frame  port_red port_white  arc_red arc_white')
    for f in frames_of(a.out):
        pr, pw = count(port_im(a.out, f)); ar = arc_im(a.out, f)
        ard, arw = count(ar) if ar else (-1, -1)
        if max(pr, pw, ard, arw) >= a.min:
            print('%6d %9d %10d %8d %9d' % (f, pr, pw, ard, arw))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'pairs'))
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('capture'); c.add_argument('rom'); c.add_argument('--step', type=int, default=50)
    c.add_argument('--start', type=int, default=1000); c.add_argument('--frames', type=int, default=36000)
    c.add_argument('--tape', default='tools/inputs/level1_full.csv'); c.add_argument('--offset', type=int, default=33)
    c.set_defaults(fn=capture)
    sub.add_parser('list').set_defaults(fn=cmd_list)
    s = sub.add_parser('sheet'); s.add_argument('name'); s.add_argument('frames', type=int, nargs='+'); s.set_defaults(fn=cmd_sheet)
    z = sub.add_parser('zoom'); z.add_argument('name'); z.add_argument('frame', type=int); z.add_argument('box', type=int, nargs=4)
    z.add_argument('--scale', type=int, default=4); z.set_defaults(fn=cmd_zoom)
    r = sub.add_parser('red'); r.add_argument('--rows', default='100:190'); r.add_argument('--min', type=int, default=200); r.set_defaults(fn=cmd_red)
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)   # MAME resolves a relative snapshot path against -snapshot_directory: nested dirs, no shots
    a.fn(a)


if __name__ == '__main__':
    main()
