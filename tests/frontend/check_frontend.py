#!/usr/bin/env python3
"""Drive the shipping frontend with real pad inputs, without gameplay probes.

Check boot/title entry, controller-page return, repeated entry, credit isolation,
all five starting rounds, DIP settings, the cached-lives latch and defaults.
"""
import argparse
import concurrent.futures
import csv
import json
from pathlib import Path
import subprocess

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('rom', type=Path)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--ares', type=Path, default=Path.home() / 'src/bin/ares-headless-sdram')
ap.add_argument('--workers', type=int, default=3)
ap.add_argument('--case', action='append', help='Run only a named case; repeatable')
a = ap.parse_args()
rom, out = a.rom.resolve(), a.out.resolve()
out.mkdir(parents=True, exist_ok=True)

def tap(events, frame, button):
    events.extend([(frame, button, 1), (frame + 4, button, 0)])

def boot():
    return [(0, 'b', 1), (60, 'b', 0)]

def title():
    return [(180, 'b', 1), (200, 'start', 1), (210, 'start', 0), (220, 'b', 0)]

cases = [('boot', boot(), 350, 'control', None),
         ('title', title(), 550, 'control', None)]
nav = boot()
for f in (100, 120, 140): tap(nav, f, 'down')
tap(nav, 160, 'start')
nav.extend([(190, 'x', 1), (204, 'x', 0), (210, 'mode', 1), (224, 'mode', 0)])
tap(nav, 240, 'a'); tap(nav, 240, 'c')  # Return without relying on Start/Mode.
tap(nav, 300, 'b')
# Allow the first arcade title to finish loading after the cold-boot menu.
# The RAM sequencer reaches its title state before the picture is presented.
nav.extend([(600, 'b', 1), (620, 'mode', 1), (632, 'mode', 0), (640, 'b', 0)])
tap(nav, 720, 'b')
cases.append(('navigate', nav, 1100, 'fighting', None))
nav_three = [(f, 'start' if b == 'mode' else b, v) for f, b, v in nav
             if b not in ('x', 'mode') or f >= 600]
cases.append(('navigate-three', nav_three, 1100, 'control', None))

for n in range(5):
    ev = boot()
    for i in range(n): tap(ev, 100 + i * 20, 'right')
    tap(ev, 220, 'down')
    for i in range(n): tap(ev, 250 + i * 20, 'right')
    tap(ev, 350, 'down')
    for i in range(n % 4): tap(ev, 380 + i * 20, 'right')
    tap(ev, 470, 'b')
    for f in (600, 700, 800): tap(ev, f, 'start')
    expected = (n + 1, [0x54, 0x98, 0xDC, 0x54, 0x14][n], [4, 2, 3, 4, 4][n])
    cases.append((f'round{n+1}', ev, 1500, 'control', expected))

ev = title()
for f in (260, 280): tap(ev, f, 'down')
tap(ev, 310, 'right'); tap(ev, 350, 'b')
for f in (500, 600, 700): tap(ev, f, 'start')
cases.append(('title-lives', ev, 1400, 'control', (1, 0x58, 2)))
ev = boot()
for f in (100, 120, 140, 160): tap(ev, f, 'right')
tap(ev, 200, 'down')
for f in (220, 240, 260, 280): tap(ev, f, 'right')
tap(ev, 320, 'down')
for f in (340, 360): tap(ev, f, 'right')
for f in (400, 420): tap(ev, f, 'down')
tap(ev, 450, 'a'); tap(ev, 500, 'b')
for f in (600, 700, 800): tap(ev, f, 'start')
cases.append(('defaults', ev, 1500, 'control', (1, 0x54, 4)))
if a.case:
    unknown = set(a.case) - {c[0] for c in cases}
    if unknown: ap.error('unknown case: ' + ', '.join(sorted(unknown)))
    cases = [c for c in cases if c[0] in a.case]

def run(case):
    name, events, frames, pad, expected = case
    work = out / name
    work.mkdir(exist_ok=True)
    tape = work / 'input.csv'
    tape.write_text('\n'.join(','.join(map(str, e)) for e in sorted(events)) + '\n')
    cmd = [str(a.ares), '--frames', str(frames), '--pad1', pad, '--input', str(tape),
           '--gameframe', 'sdram:0x2FC2A', '--gameframe-log', str(work/'state.csv'),
           '--gameframe-watch', 'sdram:0x2FC00:0x180',
           '--dump', f'VDP VRAM:0:0x10000:{work}/vram.bin']
    for f in (30, 180, 192, 240, 300, 440, 520, 660, 800, 900):
        if f < frames: cmd += ['--screenshot', f'{f}:{work}/{f}.png']
    with (work/'run.log').open('w') as log:
        subprocess.run(cmd + [str(rom)], stdout=log, stderr=subprocess.STDOUT, check=True)
    rows = list(csv.DictReader((work/'state.csv').open()))
    ram = [bytes.fromhex(r['sdram:0x2FC00:0x180']) for r in rows]
    result = dict(case=name, level=ram[-1][0x142]+1, dip=ram[-1][0x1B], lives=ram[-1][0x19])
    if expected:
        assert (result['level'], result['dip'], result['lives']) == expected, result
        assert any(w[0x28] & 1 for w in ram[900:]), (name, 'did not start')
    if name == 'boot':
        assert all(int(r['gameframe']) == 0 for r in rows[40:]), 'arcade boot ran under menu'
    if name == 'title':
        assert len({r['gameframe'] for r in rows[240:]}) == 1, 'title timer did not pause'
    if name in ('boot', 'title') or name.startswith('navigate'):
        # The shared work-RAM image is initialized during startup. Its brief
        # pre-initialization contents are not the arcade's credit counter.
        assert all(w[0] == 0 for w in ram[40:]), (name, 'menu leaked a coin/start')
    if name.startswith('navigate'):
        assert len({r['gameframe'] for r in rows[320:380]}) > 1, 'failed first resume'
        assert len({r['gameframe'] for r in rows[660:705]}) == 1, 'failed repeated entry'
        assert len({r['gameframe'] for r in rows[800:850]}) > 1, 'failed second resume'
    (work/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(name, 'PASS', result, flush=True)
    return result

with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
    results = list(pool.map(run, cases))
(out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
