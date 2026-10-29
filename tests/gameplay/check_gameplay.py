#!/usr/bin/env python3
"""The gameplay gate: the same record compare as demo-b, on a tape instead of the attract.

SKELETON. The arcade half runs; the port half does not exist yet, on purpose, because the
one thing that has to be true before any comparison means anything is that BOTH MACHINES
SAW THE SAME INPUTS AT THE SAME FRAMES. Until `port_replay_ok()` can prove that, this
refuses to compare rather than printing a byte count that is really an input mismatch.

WHAT DIFFERS FROM demo-b, and why the shape is not copied wholesale:

  THE TAPE IS THE TIMELINE. demo-b anchors on the attract's own tick at 0xFFF02A, which
  restarts at every step, so its dumps are scene-named and its targets are (scene, tick).
  Here the frame number is unique by itself: the tape is replayed from a cold boot with a
  fresh NVRAM directory, which is what makes the run repeatable.

  THERE IS NO FROZEN-SCENE PROBLEM, so no `nth`. Nothing here stands still at one tick
  while its records move; the game is running.

  THE PORT CLOCK STILL APPLIES. The port services one game frame per master vint under
  PORTCLK1 but its wall clock is not the arcade's (x1.015 as measured), so a port frame
  must be found by the GAME frame, off the blink timer at 0xFFF020/21, exactly as
  check_demo.game_frame does -- never by the wall frame and never by comparing the timer.

SUPERSEDED 2026-10-22 by the parity harness: `make parity ROM=...` runs the input proof,
milestones, kind census and video compare (tools/parity_play.py, tools/parity_compare.py,
docs/handoff/PARITY-HARNESS.md). This file is kept importable for its tape/dump readers.

Run:  python3 tests/gameplay/check_gameplay.py            # arcade side only, prints the harness
      SWEEP=1 ...                                         # every dump, not just targets
"""
import os
import sys
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'demo-b'))

import check_demo as D          # the record compare, live(), game_frame(), tick_of()

TAPE = os.path.join(HERE, '..', '..', 'tools', 'inputs', 'level1_boss.csv')
ARCADE = os.path.join(HERE, 'arcade')
# EVERY 16 GAME TICKS ALONG THE TAPE is the dump cadence Mike set. At 60 Hz that is a dump
# every 16 frames; the targets are a subset of those, one per phase of the level, and
# SWEEP=1 reads the lot.
DUMP_EVERY = 16
SWEEP = os.environ.get('SWEEP') == '1'


def tape_extent(path=TAPE):
    """(events, last frame) of the input tape. The tape is the spec for the whole run, so
    its length is reported beside every result."""
    n, last = 0, 0
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        f = int(line.split(',')[0])
        n += 1
        last = max(last, f)
    return n, last


def arcade_dumps():
    """{frame: path} of the arcade oracle along the tape."""
    out = {}
    for p in sorted(glob.glob(os.path.join(ARCADE, 'aw_*.bin'))):
        try:
            out[int(os.path.basename(p)[3:-4])] = p
        except ValueError:
            pass
    return out


def port_replay_ok():
    """DOES THE PORT SEE THE SAME INPUTS AT THE SAME FRAMES?

    The proof now lives in tools/parity_compare.py section 1 (INPUT PROOF): the port's
    latched pad byte 0xFFE028, read off the port's timeline.csv at every tape edge, matched
    against the tape. This skeleton no longer owns it and returns False so that nothing here
    compares; run the harness instead (docs/handoff/PARITY-HARNESS.md)."""
    return False


def main():
    ev, last = tape_extent()
    dumps = arcade_dumps()
    print('tape: %s' % os.path.relpath(TAPE, os.path.join(HERE, '..', '..')))
    print('tape: %d events, last at frame %d (%.0f seconds at 60 Hz)' % (ev, last, last / 60.0))
    print('arcade dumps: %d in %s (cadence every %d frames)'
          % (len(dumps), os.path.relpath(ARCADE), DUMP_EVERY))
    if dumps:
        fs = sorted(dumps)
        print('arcade dumps: frames %d..%d' % (fs[0], fs[-1]))
        for f in (fs[0], fs[len(fs) // 2], fs[-1]):
            b = open(dumps[f], 'rb').read()
            print('  frame %-6d hero x %04x  live records %2d  game frame %d'
                  % (f, int.from_bytes(b[0xc:0xe], 'big'), len(D.live(b)), D.game_frame(b)))
    else:
        print('arcade dumps: none yet -- tests/gameplay/arcade_dump.sh %s <frames>'
              % os.path.relpath(TAPE, os.path.join(HERE, '..', '..')))
    if not port_replay_ok():
        print('port: NOT COMPARED here. The gameplay harness is:')
        print('  make parity ROM=build/port/rom/<name>.32x [TAPE=tools/inputs/level1_full.csv]'
              ' [ARCADE=tests/gameplay/arcade_level1_full]')
        print('  make parity-candidate NAME=<name>      # PORTGODMODE candidate, then parity')
        print('  make parity-oracle                     # regenerate the arcade corpus')
        print('  -> build/parity/<name>/REPORT.md; docs/handoff/PARITY-HARNESS.md explains it')
        return 0
    raise SystemExit('unreachable: the compare lives in tools/parity_compare.py')


if __name__ == '__main__':
    sys.exit(main())
