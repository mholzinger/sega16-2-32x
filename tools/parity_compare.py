#!/usr/bin/env python3
"""Gameplay parity scorecard: the port's tape run against the arcade oracle's.

    python3 tools/parity_compare.py --arcade tests/gameplay/arcade_level1_full \\
        --port build/parity/<rom> --rom build/port/rom/<rom>.32x \\
        [--tape tools/inputs/level1_full.csv] [--accept build/accept] \\
        [--report build/parity/<rom>/REPORT.md]

Both corpora carry the same files (tests/gameplay/arcade_oracle.sh on the arcade, the
port runner tools/parity_play.py on ares-headless): timeline.csv (one row per emulated
frame, header frame,tick,gameframe,level,round,scroll,px,py,pstat,phand,lives,score,
credits,f028,f14b,f154,pad,boss_stat,boss_hand,step,demo), census.csv (frame,slot,status,
handler for every active slot), ev_<frame>_{wram,tilemap,text,sprite,palette}.bin at the
game's state changes, and screenshots named <anything>_<frame>.png. The tape is --tape,
else the arcade mame.log's "GAMEPLAY-DUMP TAPE" line, else tools/inputs/level1_full.csv;
when both are present and differ the report says so.

It measures, in this order, and writes a markdown report:
  0 SETUP         what must agree before any row is compared: the DIP latch at
                  0xFFF01A/1B read off the ev wram dumps (the port seeds its own DIPs at
                  boot, the oracle forces its own), the credits the start consumed and the
                  lives it set, how each side pins lives (GD_GOD on the arcade, PORT_GODMODE
                  on the port) and whether the pin is visible in the rows, and a HANG check:
                  the longest run of in-play rows over which neither the tick 0xFFF02A nor
                  the game frame advanced. FAIL when the DIPs or the start differ, or a run
                  reaches --hang-frames (300; the arcade's longest is under 30). Rows at and
                  after a hang are frozen and every later section says "hung" for them.
  1 INPUT PROOF   per tape event, the first frame on each machine where the latched pad
                  byte 0xFFE028 (coins: 0xFFF000; start: the demo bit 0xFFF026) reflects
                  it. An event that never lands is "hung" when its search window overlaps
                  a hang, "unlatched" when it sits in a run of two or more never-landing
                  events over which the machine's pad byte did not change at all (the game
                  is not reading the pad: level loads, intros, the round-clear wait; the
                  arcade itself has 8 of them at the level-2 intro, frames 31804-31932),
                  else "never". FAIL when an event lands on one machine and reads "never"
                  on the other, or the port's lag in ACTIVE frames (frames where its tick
                  advanced, i.e. the game ran a pass; a frame where the master ran no pass
                  is cadence, scored by the temporal and soak gates) exceeds the arcade's
                  by more than --lag-slack (3).
  2 MILESTONES    the first frame and tick of each game event on both machines, with the
                  frame and tick deltas. FAIL when the arcade reached one the port did not.
                  "score drawn" is a dump-schedule frame (the first ev text dump at or after
                  the first score change with a nonzero digit), so it carries no deltas.
  3 DIVERGENCE    two alignments, because the tape is keyed by host frame and the game by
                  its pass count. (a) BY HOST FRAME with the measured start offset (the port
                  takes the start 2 frames later on the level1_full tape): hero x within 8 px
                  while both heroes run the play handler, overall and per 3000-frame window,
                  and the first frame outside 8 px. FAIL when that frame comes before the
                  first live enemy (0xFFF154) on both machines, i.e. before combat can
                  diverge. (b) BY (segment, tick): a segment is a run of in-play rows with
                  one level and no tick jump over 16 (a level, the round-transition
                  countdown that counts 0xFFF02A DOWN at level 0, the next level), matched
                  by ordinal; the port's tick is shifted by a SKEW measured on the opening
                  descent (ticks 1..0x5f, input-independent), where ares samples the tick
                  one pass ahead of its records. First tick where lives LOST (relative to
                  the start value, so the setup difference does not mask a death), score or
                  x differ.
  4 KIND CENSUS   per level the port reached and per slot range (1-15 the hero's trail
                  objects, 16-39 enemies, 40 boss, 41-63 effects), the handlers alive on
                  each machine with their census sample counts (live = status high byte &
                  0x88 == 0x88 for 16-39, status bit 15 elsewhere). FAIL when the arcade
                  holds a handler for --kind-min-samples (2) or more samples and the port
                  never does; port-only handlers are listed, not failed.
  5 VIDEO         at each milestone where both sides have ev dumps within --ev-slack (8)
                  frames: tilemap cells per page, text RAM rows that differ, sprite count
                  (jts16_obj_scan.v:163-177), and the mean luma diff of screenshots taken
                  within --shot-slack (8) frames of the milestone on BOTH sides, the ares
                  canvas (1415x243, content at cols 65..1344 rows 11..234) cropped to its
                  content before both are resized to 320x224; a pair further apart is not
                  scored (an arcade shot 300 frames off measures the scroll, not the port).
  6 ATTRACT       the accept files build/accept/{soak,parity,temporal,artaudit}_<rom>.txt
                  when present: soak >= 0.78 flips/vint, artaudit 0 cells, temporal
                  presented >= 40/59 in both spans. The attract_parity ladder is printed
                  per scene (collapse column and minimum) and is INFORMATIONAL: no rom on
                  record collapses every row (boot card reads 55 flat on all of them);
                  --parity-collapse N turns it into a gate.
Exit 1 on any FAIL, 0 otherwise. A 15-line summary goes to stdout.

The port's tilemap shadow holds 13 of the arcade's 16 pages (SDRAM 0x19000, page i at
+i*0x1000) and 0x400 of the 0x800 sprite bytes; the compare covers what both sides hold.
Self-compare (--port pointing at the arcade corpus) must pass with zero deltas.
"""
import argparse
import csv
import glob
import os
import re
import sys
import time
from collections import Counter, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tests', 'demo-b'))
import check_demo as D          # tick_of(), live(), game_frame() on a 16 KB work-RAM image

HEX_COLS = ('tick', 'level', 'round', 'scroll', 'px', 'py', 'pstat', 'phand', 'lives',
            'score', 'credits', 'f028', 'f14b', 'f154', 'pad', 'boss_stat', 'boss_hand',
            'step', 'demo')
# The pad byte 0xFFE028 as the brief states it; section 1 re-derives it from the first
# tape events on the arcade and reports any disagreement.
PAD_BITS_STATED = {'c': 0, 'a': 1, 'b': 2, 'down': 4, 'up': 5, 'right': 6, 'left': 7}
PAD_NAMES = {'a': 'punch', 'b': 'kick', 'c': 'jump'}
BOSS_TRANSFORM = 0x1794C
PLAYER_PLAY = 0x49BA           # slot 0's handler while the hero is in play
# Level-2 enemy handlers named in the brief; the census section also reports what the
# arcade actually spawned, which is the set the FAIL rule uses.
L2_KINDS_STATED = {0xEA5E, 0xE474, 0x137CC, 0x13374, 0x12F0A, 0x12CBA, 0x124BC, 0x1211A}
SCORE_TEXT = (0xB4, 0xC4)       # text RAM bytes holding P1's score digits (ASCII words)
DIP_LATCH = 0x301A              # 0xFFF01A/1B in the 16 KB work-RAM image: DSW1, DSW2 as latched at boot
PAGE = 0x1000
TEXT_ROW = 128
SPRITE_REC = 16
# the ares-headless screenshot: a 1415x243 canvas whose picture sits at these pixels
# (measured on build/parity/hp1/ev_*.png: nonblack rows 11..234, cols 65..1344 = 1280x224)
ARES_CANVAS = (1415, 243)
ARES_CONTENT = (65, 11, 1345, 235)
SLOT_RANGES = (('1-15', 1, 15), ('16-39', 16, 39), ('40', 40, 40), ('41-63', 41, 63))
SEGMENT_JUMP = 16               # a tick move larger than this opens a new segment
SEGMENT_MIN_ROWS = 30           # shorter runs are residues (the attract count after the start)


# ---------------------------------------------------------------- loading

def load_timeline(d):
    p = os.path.join(d, 'timeline.csv')
    rows = []
    with open(p) as fh:
        for r in csv.DictReader(fh):
            o = {'frame': int(r['frame']), 'gameframe': int(r['gameframe'])}
            for k in HEX_COLS:
                o[k] = int(r[k], 16)
            rows.append(o)
    return rows


def load_census(d):
    p = os.path.join(d, 'census.csv')
    out = []
    if not os.path.exists(p):
        return out
    with open(p) as fh:
        for r in csv.DictReader(fh):
            out.append((int(r['frame']), int(r['slot']), int(r['status'], 16), int(r['handler'], 16)))
    return out


def load_tape(path):
    ev = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        f, b, v = line.split(',')[:3]
        ev.append((int(f), b.strip(), int(v)))
    ev.sort(key=lambda e: e[0])
    return ev


def mame_log(d):
    p = os.path.join(d, 'mame.log')
    return open(p, errors='replace').read() if os.path.exists(p) else None


def tape_from_log(d):
    """The tape path the arcade run was given, off mame.log's GAMEPLAY-DUMP TAPE line
    (tools/gameplay_dump.lua prints it since 2026-10-22; older logs have none)."""
    txt = mame_log(d)
    if not txt:
        return None
    m = re.search(r'GAMEPLAY-DUMP TAPE\s+(\S+)', txt)
    return m.group(1) if m else None


def ev_dumps(d):
    """{frame: {what: path}} of the ev_<frame>_<what>.bin dumps."""
    out = {}
    for p in glob.glob(os.path.join(d, 'ev_*_*.bin')):
        m = re.match(r'ev_(\d+)_(\w+)\.bin$', os.path.basename(p))
        if m:
            out.setdefault(int(m.group(1)), {})[m.group(2)] = p
    return out


def screenshots(d):
    """{frame: path} of every png whose name ends in _<frame>.png or is <frame>.png."""
    out = {}
    for p in glob.glob(os.path.join(d, '*.png')):
        m = re.search(r'(?:^|_)(\d+)\.png$', os.path.basename(p))
        if m:
            out[int(m.group(1))] = p
    return out


def nearest(keys, frame, limit):
    """The key nearest to frame within limit, or None."""
    best = None
    for k in keys:
        if abs(k - frame) <= limit and (best is None or abs(k - frame) < abs(best - frame)):
            best = k
    return best


def first_frame(tl, pred, after=None):
    for r in tl:
        if (after is None or r['frame'] > after) and pred(r):
            return r['frame']
    return None


def start_frame(tl):
    """The frame the start was taken: the demo bit clears with the attract step at 8 after a
    credit was paid. None when the run never left the attract."""
    credit = next((r['frame'] for r in tl if 0 < r['credits'] < 0x10), None)
    if credit is None:
        return None
    return next((r['frame'] for r in tl if r['frame'] > credit and r['demo'] == 0 and r['step'] == 8), None)


def in_play(r):
    return not (r['demo'] & 1) and bool(r['f028'] & 1)


# ---------------------------------------------------------------- section 0: setup

def dip_latch(M):
    """Counter of (DSW1, DSW2) pairs at 0xFFF01A/1B over the ev wram dumps from frame 100 on
    (earlier dumps read the pre-boot bytes: 00/00 on the arcade, SDRAM garbage on the port)."""
    vals = Counter()
    for f in sorted(M['ev']):
        p = M['ev'][f].get('wram')
        if not p or f < 100:
            continue
        b = open(p, 'rb').read()
        if len(b) > DIP_LATCH + 1:
            vals[(b[DIP_LATCH], b[DIP_LATCH + 1])] += 1
    return vals


def start_state(M):
    """Credits before and at the start row, and the lives it set."""
    st = start_frame(M['tl'])
    if st is None:
        return None
    before = M['byframe'].get(st - 1, M['byframe'][st])
    at = M['byframe'][st]
    return {'frame': st, 'credits_before': before['credits'], 'credits_after': at['credits'], 'lives': at['lives']}


def hang_run(M, min_frames):
    """The longest run of in-play rows over which neither the tick nor the game frame
    advanced: (first frame, length, tick) when it reaches min_frames, else None."""
    best = None
    start = n = 0
    key = None
    for r in M['tl']:
        k = (r['tick'], r['gameframe']) if in_play(r) else None
        if k is not None and k == key:
            n += 1
        else:
            if key is not None and n >= min_frames and (best is None or n > best[1]):
                best = (start, n, key[0])
            key, start, n = k, r['frame'], 1
    if key is not None and n >= min_frames and (best is None or n > best[1]):
        best = (start, n, key[0])
    return best


def stalled_frames(M, hang):
    """In-play rows after the first tick 0 where the tick did not advance from the previous
    row, excluding a hang: the frames where the game ran no pass (cadence)."""
    st = start_frame(M['tl'])
    if st is None:
        return 0
    origin = first_frame(M['tl'], lambda r: r['tick'] == 0, st)
    n = 0
    prev = None
    for r in M['tl']:
        if origin is None or r['frame'] <= origin or (hang and r['frame'] >= hang[0]):
            continue
        if in_play(r) and prev is not None and in_play(prev) and r['tick'] == prev['tick']:
            n += 1
        prev = r
    return n


def lives_pinning(M):
    """How this machine holds P1's lives and whether a death can show in its rows."""
    txt = mame_log(M['dir'])
    if txt is not None:
        if 'GAMEPLAY-DUMP GOD after-row' in txt:
            return ('after-row', 'GD_GOD writes 3 after the row and the ev dumps, so a death shows as one row below 3')
        return ('before-row', 'mame.log has no GOD line: an oracle from before 2026-10-22 ran GD_GOD before the row, '
                              'so an arcade death can never show; regenerate with make parity-oracle')
    flags = M.get('flags')
    if flags and os.path.exists(flags):
        if '-DPORT_GODMODE' in open(flags).read():
            return ('port-godmode', 'PORT_GODMODE holds lives at 3 or more in the per-tick MCU block (sh_src/port_l2.c); '
                                    'a death from above 3 shows, a death at 3 shows only if the row samples before the hold')
        return ('none', 'no PORT_GODMODE in %s: the player can game over' % os.path.basename(flags))
    return ('unknown', 'no mame.log and no .flags beside the rom')


def section_setup(A, P, tape_path, tape_log, hang_frames, out, fails):
    out.append('## 0. Setup and run health')
    out.append('')
    res = {}
    if tape_log and os.path.abspath(tape_log) != os.path.abspath(tape_path) and os.path.basename(tape_log) != os.path.basename(tape_path):
        out.append('- FAIL tape: the arcade corpus was recorded on %s, this compare scores %s' % (tape_log, tape_path))
        fails.append('setup: arcade corpus tape %s is not %s' % (os.path.basename(tape_log), os.path.basename(tape_path)))
    da, dp = dip_latch(A), dip_latch(P)
    fmt = lambda c: ', '.join('%02x/%02x x%d' % (k[0], k[1], n) for k, n in c.most_common()) or 'no wram dump'
    out.append('- DIP latch 0xFFF01A/1B (DSW1/DSW2) over the ev wram dumps from frame 100: arcade %s; port %s' % (fmt(da), fmt(dp)))
    va = da.most_common(1)[0][0] if da else None
    vp = dp.most_common(1)[0][0] if dp else None
    res['dips'] = (va, vp)
    if va and vp and va != vp:
        out.append('- FAIL the two machines boot on different DIP switches (the port seeds them at boot, '
                   'sh_src/port_l2.c PORT_CORO; the oracle forces GD_DSW1/GD_DSW2, default ff/fd); lives, '
                   'coinage and difficulty differ before the first row is compared')
        fails.append('setup: DIPs differ, arcade %02x/%02x port %02x/%02x' % (va + vp))
    elif va is None or vp is None:
        out.append('- NOTE DIPs unverified on %s (no wram dump after frame 100)' % ('the arcade' if va is None else 'the port'))
    if da and len(da) > 1 or dp and len(dp) > 1:
        out.append('- NOTE the DIP latch changed between dumps on %s' % ('the arcade' if len(da) > 1 else 'the port'))
    sa, sp = start_state(A), start_state(P)
    res['start'] = (sa, sp)
    if sa and sp:
        ca, cp = sa['credits_before'] - sa['credits_after'], sp['credits_before'] - sp['credits_after']
        out.append('- start: arcade frame %d consumed %d credit(s) (%d -> %d) and set %d lives; port frame %d consumed %d (%d -> %d) and set %d'
                   % (sa['frame'], ca, sa['credits_before'], sa['credits_after'], sa['lives'],
                      sp['frame'], cp, sp['credits_before'], sp['credits_after'], sp['lives']))
        if ca != cp or sa['lives'] != sp['lives']:
            out.append('- FAIL the start does not set up the same game: every lives and continue comparison below inherits it')
            fails.append('setup: start consumed %d credits / %d lives on the port vs %d / %d on the arcade' % (cp, sp['lives'], ca, sa['lives']))
    else:
        out.append('- start: %s never took the start' % ('the arcade' if sa is None else 'the port'))
    pa, pp = lives_pinning(A), lives_pinning(P)
    res['pin'] = (pa[0], pp[0])
    out.append('- lives pinning: arcade %s (%s); port %s (%s)' % (pa[0], pa[1], pp[0], pp[1]))
    ha, hp = hang_run(A, hang_frames), hang_run(P, hang_frames)
    A['hang'], P['hang'] = ha, hp
    for name, M, h in (('arcade', A, ha), ('port', P, hp)):
        if h:
            last = M['tl'][-1]['frame']
            tail = ' to the end of the run' if h[0] + h[1] - 1 >= last else ''
            out.append('- FAIL %s HANG: tick frozen at %04x and the game frame frozen from frame %d for %d frames%s; '
                       'the object pass is not being run (the health block counts no missed dispatch for a loop '
                       'that waits outside the handlers); every row from %d is the frozen state'
                       % (name.upper(), h[2], h[0], h[1], tail, h[0]))
            fails.append('%s hang: tick frozen at %04x from frame %d for %d frames' % (name, h[2], h[0], h[1]))
    if not ha and not hp:
        out.append('- hang: none (no in-play run of %d frames with a frozen tick on either machine)' % hang_frames)
    res['hang'] = (ha, hp)
    sta, stp = stalled_frames(A, ha), stalled_frames(P, hp)
    res['stalled'] = (sta, stp)
    out.append('- stalled frames (in play, tick did not advance, outside a hang): arcade %d, port %d; cadence, '
               'scored by the temporal and soak gates, not here' % (sta, stp))
    return res


# ---------------------------------------------------------------- section 1: inputs

def derive_pad_bits(tl, tape):
    """{button: bit} from the first press of each pad button: the bits that appear in the pad
    byte within 16 frames of the event and were clear the frame before it are its candidates;
    buttons pressed together (the tape presses right with punch) are told apart by
    elimination, a button with one candidate claiming that bit from the others."""
    byframe = {r['frame']: r for r in tl}
    pad_ev = [e for e in tape if e[1] not in ('y', 'start')]
    cand = {}
    for i, (f, b, v) in enumerate(pad_ev):
        if v != 1 or b in cand:
            continue
        # the window closes at the next pad event of any button, so a later press cannot
        # pollute the candidates (the tape presses kick 8 frames after punch)
        end = min(f + 16, next((e[0] for e in pad_ev[i + 1:] if e[0] > f), f + 16))
        before = byframe.get(f - 1, {}).get('pad', 0)
        gained = 0
        for g in range(f, end):
            r = byframe.get(g)
            if r is None:
                break
            gained |= r['pad'] & ~before & 0xFF
        bits = {i for i in range(8) if gained >> i & 1}
        if bits:
            cand[b] = bits
    out = {}
    for _ in range(8):
        for b, bits in cand.items():
            if b not in out and len(bits) == 1:
                out[b] = next(iter(bits))
        for b in cand:
            if b not in out:
                cand[b] = cand[b] - set(out.values())
    return out


def input_landings(tl, tape, bits, cap):
    """[(event index, lag or None, note)] for one machine. A pad event lands at the first
    frame after it where the bit reads the event's value, searched up to the next PRESS of
    the same button (or cap frames); a release is searched from the frame after its press
    landed. A coin press lands when 0xFFF000 rises; the start press when the demo bit
    clears; their releases are unobservable (note 'n/a'), as is any event past the end of
    the machine's timeline or a press whose bit was already held at the event frame."""
    byframe = {r['frame']: r for r in tl}
    last = tl[-1]['frame']
    nxt = {}
    following = [None] * len(tape)
    for i in range(len(tape) - 1, -1, -1):
        following[i] = nxt.get(tape[i][1])
        if tape[i][2]:
            nxt[tape[i][1]] = tape[i][0]
    out = []
    press_landed = {}
    for i, (f, b, v) in enumerate(tape):
        if f > last:
            out.append((i, None, 'n/a'))       # past the end of this machine's timeline
            continue
        end = min(following[i] if following[i] else f + cap, f + cap, last)
        if b == 'y':
            if v == 0:
                out.append((i, None, 'n/a'))
                continue
            r0 = byframe.get(f)
            if r0 is None:
                out.append((i, None, 'never'))
                continue
            c0 = r0['credits']
            lag = next((g - f for g in range(f + 1, end + 1)
                        if g in byframe and byframe[g]['credits'] != c0), None)
            out.append((i, lag, '' if lag is not None else 'never'))
            continue
        if b == 'start':
            if v == 0:
                out.append((i, None, 'n/a'))
                continue
            lag = next((g - f for g in range(f + 1, end + 1)
                        if g in byframe and byframe[g]['demo'] == 0 and byframe[g]['step'] == 8), None)
            out.append((i, lag, '' if lag is not None else 'never'))
            continue
        bit = bits.get(b)
        if bit is None:
            out.append((i, None, 'n/a'))
            continue
        want = 1 if v else 0
        # Row f is the state at the START of frame f, before the event's input applies, so
        # a landing is searched from f + 1 and lag 0 cannot occur. A press whose bit already
        # reads 1 at row f (held since an earlier press whose release never landed) is
        # unobservable: 'n/a'. A release is searched from the frame after its press landed;
        # a release whose press never landed is unobservable too.
        if v == 1:
            r0 = byframe.get(f)
            if r0 is not None and (r0['pad'] >> bit) & 1:
                press_landed[b] = None
                out.append((i, None, 'n/a'))
                continue
            start = f + 1
        else:
            pl = press_landed.get(b)
            if pl is None:
                out.append((i, None, 'n/a'))
                continue
            start = max(f + 1, pl + 1)
        lag = next((g - f for g in range(start, end + 1)
                    if g in byframe and ((byframe[g]['pad'] >> bit) & 1) == want), None)
        if v == 1:
            press_landed[b] = f + lag if lag is not None else None
        out.append((i, lag, '' if lag is not None else 'never'))
    return out


def classify_never(M, tape, land, cap):
    """Rewrite 'never' notes: 'hung' when the event's search window reaches a hang on this
    machine; 'unlatched' when the event is one of two or more consecutive never-landing pad
    events over whose span (first event frame to the last one's window end) the pad byte
    never changed, i.e. the game was not reading the pad."""
    byframe = M['byframe']
    hang = M.get('hang')
    out = list(land)
    for j, (i, lag, note) in enumerate(out):
        if note == 'never' and hang and tape[i][0] + cap >= hang[0]:
            out[j] = (i, lag, 'hung')
    # runs of consecutive never pad events (n/a events do not break a run, landed ones do)
    j = 0
    while j < len(out):
        i, lag, note = out[j]
        if note != 'never' or tape[i][1] in ('y', 'start'):
            j += 1
            continue
        k = j
        run = []
        while k < len(out):
            ik, lk, nk = out[k]
            if nk == 'n/a':
                k += 1
                continue
            if nk != 'never' or tape[ik][1] in ('y', 'start'):
                break
            run.append(k)
            k += 1
        if len(run) >= 2:
            # the span of the events themselves plus the lag floor: the window of the last
            # event reaches into the frames where the game latches again
            f0 = tape[out[run[0]][0]][0]
            f1 = min(tape[out[run[-1]][0]][0] + 2, M['tl'][-1]['frame'])
            pads = {byframe[g]['pad'] for g in range(f0, f1 + 1) if g in byframe}
            if len(pads) == 1:
                for k2 in run:
                    out[k2] = (out[k2][0], None, 'unlatched')
        j = k if k > j else j + 1
    return out


def active_lag(M, f, lag):
    """Frames in (f, f + lag] where the tick advanced from the previous row: the frames in
    which the game ran a pass. A frame where the master ran no pass cannot latch anything."""
    if lag is None:
        return None
    byframe = M['byframe']
    n = 0
    for g in range(f + 1, f + lag + 1):
        a, b = byframe.get(g - 1), byframe.get(g)
        if a is not None and b is not None and a['tick'] != b['tick']:
            n += 1
    return n


def lag_hist(land):
    h = Counter()
    for _i, lag, note in land:
        if note in ('n/a', 'never', 'hung', 'unlatched'):
            h[note] += 1
        elif lag <= 3:
            h[str(lag)] += 1
        elif lag <= 7:
            h['4-7'] += 1
        else:
            h['8+'] += 1
    return h


def section_inputs(A, P, tape, slack, out, fails):
    out.append('## 1. Input proof')
    bits_a = derive_pad_bits(A['tl'], tape)
    bits_p = derive_pad_bits(P['tl'], tape)
    bits = dict(PAD_BITS_STATED)
    bits.update(bits_a)
    out.append('')
    out.append('Pad byte 0xFFE028 bits, derived from the first press of each button on the arcade '
               '(stated mapping in brackets): ' +
               ', '.join('%s=bit%d [%s]' % (b, bits_a.get(b, -1), PAD_BITS_STATED.get(b, '?'))
                         for b in sorted(bits_a)) + '.')
    for b in bits_a:
        if PAD_BITS_STATED.get(b) != bits_a[b]:
            out.append('- NOTE arcade bit for %s disagrees with the stated mapping' % b)
    for b in bits_p:
        if bits_p[b] != bits.get(b):
            out.append('- FAIL port latches %s on bit %d, arcade on bit %d' % (b, bits_p[b], bits[b]))
            fails.append('input: pad bit for %s differs' % b)
    if tape and not bits_a:
        out.append('- NOTE no pad bit could be derived on the arcade (no press ever reached 0xFFE028)')
    cap = 240
    la = classify_never(A, tape, input_landings(A['tl'], tape, bits, cap), cap)
    lp = classify_never(P, tape, input_landings(P['tl'], tape, bits, cap), cap)
    ha, hp = lag_hist(la), lag_hist(lp)
    cols = ['1', '2', '3', '4-7', '8+', 'never', 'unlatched', 'hung', 'n/a']
    out.append('')
    out.append('Lag in host frames from the tape edge to the first row showing it (row F is the state '
               'at the start of frame F, so 1 is the floor):')
    out.append('')
    out.append('| machine | ' + ' | '.join(cols) + ' | events |')
    out.append('|---|' + '---|' * (len(cols) + 1))
    out.append('| arcade | ' + ' | '.join(str(ha.get(c, 0)) for c in cols) + ' | %d |' % len(tape))
    out.append('| port | ' + ' | '.join(str(hp.get(c, 0)) for c in cols) + ' | %d |' % len(tape))
    one_sided, slow, excused = [], [], Counter()
    for (i, a, na), (_i, p, np_) in zip(la, lp):
        if na == 'n/a' or np_ == 'n/a':
            continue
        if na in ('hung', 'unlatched') or np_ in ('hung', 'unlatched'):
            excused[np_ if np_ in ('hung', 'unlatched') else na] += 1
            continue
        if (a is None) != (p is None):
            one_sided.append((i, a, p))
        elif a is not None and p is not None:
            aa, ap = active_lag(A, tape[i][0], a), active_lag(P, tape[i][0], p)
            if ap - aa > slack:
                slow.append((i, a, p, aa, ap))
    out.append('')
    out.append('Events landing on one machine only (the other reads never, not unlatched or hung): %d. '
               'Events excused: %s. Events where the port\'s lag in ACTIVE frames (frames where its tick '
               'advanced) exceeds the arcade\'s by more than %d: %d.'
               % (len(one_sided), dict(excused) or 'none', slack, len(slow)))
    for i, a, p in one_sided[:20]:
        f, b, v = tape[i]
        out.append('- FAIL event %d (frame %d %s=%d): arcade %s, port %s'
                   % (i, f, b, v, 'lag %d' % a if a is not None else 'never',
                      'lag %d' % p if p is not None else 'never'))
    for i, a, p, aa, ap in slow[:20]:
        f, b, v = tape[i]
        out.append('- FAIL event %d (frame %d %s=%d): arcade lag %d (%d active), port lag %d (%d active)' % (i, f, b, v, a, aa, p, ap))
    if one_sided:
        fails.append('input: %d events land on one machine only' % len(one_sided))
    if slow:
        fails.append('input: %d events lag more than %d active frames on the port' % (len(slow), slack))
    # the unlatched windows, so a reader can see where each machine stopped reading the pad
    for name, M, land in (('arcade', A, la), ('port', P, lp)):
        fr = [tape[i][0] for i, _l, n in land if n == 'unlatched']
        if fr:
            out.append('- %s unlatched windows (pad byte constant): events at frames %d..%d (%d events)' % (name, min(fr), max(fr), len(fr)))
    return {'arcade': ha, 'port': hp, 'one_sided': len(one_sided), 'slow': len(slow), 'excused': excused}


# ---------------------------------------------------------------- section 2: milestones

def milestones(M, l2_kinds):
    """OrderedDict name -> frame (or None) for one machine. Lives-lost and continue entries
    are lists of frames."""
    tl, cs, byframe = M['tl'], M['cs'], M['byframe']
    ms = OrderedDict()
    # the first rise of 0xFFF000 from 0 to a credit count; the port's SDRAM mirror reads
    # garbage (0x81..) before the 68K clears work RAM, the arcade's reads 0xff
    ms['coin accepted'] = next((r['frame'] for i, r in enumerate(tl)
                                if i and 0 < r['credits'] < 0x10 and tl[i - 1]['credits'] == 0), None)
    st = start_frame(tl)
    ms['start taken'] = st
    # slot 0 still holds the attract demo's hero (handler 0x2490) for a few frames after
    # the start is taken, so the player is live when his record runs the PLAY handler
    ms['player live'] = first_frame(tl, lambda r: r['pstat'] & 0x8000 and r['phand'] == PLAYER_PLAY, st)
    if st is not None and ms['player live'] is not None:
        r0 = byframe[ms['player live']]
        x0, y0 = r0['px'], r0['py']
        ms['player first moves'] = first_frame(tl, lambda r: r['pstat'] & 0x8000 and
                                               (r['px'] != x0 or r['py'] != y0), ms['player live'])
    else:
        ms['player first moves'] = None
    ms['first score change'] = first_frame(tl, lambda r: r['score'] != 0, st)
    ms['score drawn (first ev text dump)'] = score_drawn(M, ms['first score change'])
    ms['boss active'] = first_frame(tl, lambda r: r['boss_stat'] & 0x8000, st)
    ms['boss transform'] = first_frame(tl, lambda r: r['boss_hand'] == BOSS_TRANSFORM, st)
    ms['round clear'] = first_frame(tl, lambda r: r['f14b'] == 1, st)
    rc = ms['round clear']
    if rc is not None:
        r0 = byframe[rc]
        ms['ball scene'] = first_frame(tl, lambda r: r['level'] == r0['level'] and
                                       (r['round'] != r0['round'] or r['step'] != r0['step']), rc)
    else:
        ms['ball scene'] = None
    ms['level 2'] = first_frame(tl, lambda r: r['level'] == 1, st)
    l2 = ms['level 2']
    ms['first level-2 enemy'] = next((f for f, s, stt, h in cs
                                      if l2 is not None and f >= l2 and 16 <= s <= 39
                                      and live_enemy(stt) and h in l2_kinds), None)
    lost = []
    prev = None
    for r in tl:
        if st is not None and r['frame'] > st and r['demo'] == 0 and 0 < r['lives'] < 0x10:
            if prev is not None and r['lives'] < prev:
                lost.append(r['frame'])
            prev = r['lives']
        elif r['demo']:
            prev = None
    ms['lives lost'] = lost
    ms['continue window'] = first_frame(tl, lambda r: r['f028'] & 0x10, st)
    ms['game over / attract'] = first_frame(tl, lambda r: r['demo'] & 1, st)
    return ms


def score_drawn(M, from_frame):
    """The first ev text dump at or after from_frame whose score digits at 0xB4..0xC4 hold a
    digit other than '0'."""
    if from_frame is None:
        return None
    seen = 0
    for f in sorted(M['ev']):
        p = M['ev'][f].get('text')
        if f < from_frame or not p:
            continue
        seen += 1
        b = open(p, 'rb').read()
        digits = [b[i + 1] for i in range(SCORE_TEXT[0], SCORE_TEXT[1], 2)]
        if any(0x31 <= d <= 0x39 for d in digits):
            return f
    # None means examined and never drawn; with no dump to examine the answer is unknown
    return None if seen else 'no ev text dump'


def tick_at(M, frame):
    r = M['byframe'].get(frame)
    return r['tick'] if r else None


def section_milestones(A, P, l2_kinds, pins, out, fails):
    out.append('## 2. Milestones')
    out.append('')
    ma, mp = milestones(A, l2_kinds), milestones(P, l2_kinds)
    out.append('| milestone | arcade frame | arcade tick | port frame | port tick | d frames | d ticks | verdict |')
    out.append('|---|---|---|---|---|---|---|---|')
    verdicts = {}
    for name in ma:
        a, p = ma[name], mp[name]
        if isinstance(a, list):
            n = max(len(a), len(p))
            if n == 0:
                out.append('| %s | - | - | - | - | - | - | none on either |' % name)
                verdicts[name] = 'ok'
                continue
            pinned = name == 'lives lost' and pins[0] == 'before-row'
            for i in range(n):
                fa = a[i] if i < len(a) else None
                fp = p[i] if i < len(p) else None
                row = milestone_row('%s #%d' % (name, i + 1), A, P, fa, fp, verdicts, fails)
                if pinned and fa is None and fp is not None:
                    # the arcade cannot show a death with GD_GOD before the row; not a port-only event
                    row = row.rsplit('|', 2)[0] + '| n/a (arcade lives pinned before the row) |'
                    verdicts['%s #%d' % (name, i + 1)] = 'n/a'
                out.append(row)
            continue
        if name.startswith('score drawn'):
            out.append(score_row(name, fa_=a, fp_=p, verdicts=verdicts, fails=fails))
            continue
        out.append(milestone_row(name, A, P, a, p, verdicts, fails))
    if pins[0] == 'before-row':
        out.append('')
        out.append('The arcade corpus pins lives BEFORE its rows are written (no GOD line in mame.log), so an '
                   'arcade death is invisible by construction; "lives lost" rows are n/a until the oracle is '
                   'regenerated (tools/gameplay_dump.lua now writes GD_GOD after the row).')
    return ma, mp, verdicts


def score_row(name, fa_, fp_, verdicts, fails):
    """The score-drawn row: a dump-schedule frame, so no deltas and no tick."""
    def word(x):
        if isinstance(x, str):
            return x
        return 'never' if x is None else 'drawn by ev %d' % x
    if isinstance(fa_, str) or isinstance(fp_, str):
        v = 'n/a (%s)' % (fa_ if isinstance(fa_, str) else fp_)
        verdicts[name] = 'n/a'
    elif fa_ is not None and fp_ is None:
        v = 'FAIL'
        verdicts[name] = v
        fails.append('milestone: score never drawn in the port\'s text shadow 0xB4..0xC4 (the arcade had by ev %d)' % fa_)
    elif fa_ is None and fp_ is None:
        v = 'neither'
        verdicts[name] = v
    else:
        v = 'ok' if fa_ is not None else 'port only'
        verdicts[name] = v
    return '| %s | %s | - | %s | - | - | - | %s |' % (name, word(fa_), word(fp_), v)


def milestone_row(name, A, P, fa, fp, verdicts, fails):
    if isinstance(fa, str) or isinstance(fp, str):
        # unknown on one side (no dump to read): not a FAIL, not an ok
        v = 'n/a (%s)' % (fa if isinstance(fa, str) else fp)
        verdicts[name] = 'n/a'
        fmt = lambda x: '-' if x is None or isinstance(x, str) else str(x)
        return '| %s | %s | - | %s | - | - | - | %s |' % (name, fmt(fa), fmt(fp), v)
    ta, tp = tick_at(A, fa), tick_at(P, fp)
    if fa is None and fp is None:
        v = 'neither'
    elif fa is not None and fp is None:
        v = 'FAIL'
        if P.get('hang') and fa >= P['hang'][0]:
            v = 'FAIL (port hung at %d)' % P['hang'][0]
        fails.append('milestone: %s reached on the arcade at %d, never on the port' % (name, fa))
    elif fa is None:
        v = 'port only'
    else:
        v = 'ok'
    verdicts[name] = v if not v.startswith('FAIL') else 'FAIL'
    fmt = lambda x: '-' if x is None else str(x)
    hx = lambda x: '-' if x is None else '%04x' % x
    df = fp - fa if (fa is not None and fp is not None) else None
    dt = tp - ta if (ta is not None and tp is not None) else None
    return '| %s | %s | %s | %s | %s | %s | %s | %s |' % (name, fmt(fa), hx(ta), fmt(fp), hx(tp),
                                                        fmt(df), fmt(dt), v)


# ---------------------------------------------------------------- section 3: divergence

def segments(M):
    """In-play rows after the start, cut into segments: a new one opens when the level changes
    or the tick moves by more than SEGMENT_JUMP between rows (a level's count, the
    round-transition countdown that counts DOWN at level 0, the next level's count). Runs
    under SEGMENT_MIN_ROWS are residues (the attract scene's count held for a few rows after
    the start) and are dropped. Each segment maps tick -> its first row."""
    st = start_frame(M['tl'])
    segs = []
    if st is None:
        return segs
    cur = prev = None
    for r in M['tl']:
        if r['frame'] < st or r['demo'] & 1:
            continue
        if cur is None or r['level'] != cur['level'] or abs(r['tick'] - prev['tick']) > SEGMENT_JUMP:
            cur = {'level': r['level'], 'start': r['frame'], 'end': r['frame'], 'rows': OrderedDict(), 'n': 0}
            segs.append(cur)
        cur['rows'].setdefault(r['tick'], r)
        cur['n'] += 1
        cur['end'] = r['frame']
        prev = r
    return [s for s in segs if s['n'] >= SEGMENT_MIN_ROWS]


def tick_skew(sa, sp):
    """The shift d of the port's ticks that best matches the hero's (px, py) over ticks
    1..0x5f of the first segment, the opening descent, which takes no input: (d, matches,
    tried). ares samples at the frame start, after the vint advanced 0xFFF02A and before the
    pass of that tick ran, so the port's tick reads one ahead of its records; d = 0 wins ties."""
    best = (0, -1, 0)
    for d in (0, 1, -1, 2, -2):
        m = t = 0
        for tk in range(1, 0x60):
            a, p = sa['rows'].get(tk), sp['rows'].get(tk + d)
            if a is None or p is None:
                continue
            t += 1
            m += (a['px'], a['py']) == (p['px'], p['py'])
        if m > best[1]:
            best = (d, m, t)
    return best


def hero_live(r):
    return bool(r['pstat'] & 0x8000) and r['phand'] == PLAYER_PLAY and in_play(r)


def section_divergence(A, P, out, fails):
    out.append('## 3. Divergence')
    out.append('')
    res = {'aligned': 0, 'within8': 0.0, 'first_px': None, 'frame_within8': 0.0, 'frame_first': None, 'offset': None, 'skew': None}
    sa, sp = start_frame(A['tl']), start_frame(P['tl'])
    if sa is None or sp is None:
        out.append('- nothing aligned (%s never took the start)' % ('the arcade' if sa is None else 'the port'))
        return res
    # (a) by host frame
    off = sp - sa
    res['offset'] = off
    hang_p, hang_a = P.get('hang'), A.get('hang')
    lim = min(hang_p[0] if hang_p else 1 << 30, (hang_a[0] - off) if hang_a else 1 << 30)
    enemy_a = first_frame(A['tl'], lambda r: r['f154'] > 0, sa)
    enemy_p = first_frame(P['tl'], lambda r: r['f154'] > 0, sp)
    win = OrderedDict()
    n = w = 0
    first = None
    for r in A['tl']:
        f = r['frame']
        q = P['byframe'].get(f + off)
        if q is None or f <= sa or f + off >= lim:
            continue
        if hero_live(r) and hero_live(q):
            k = (f // 3000) * 3000
            win.setdefault(k, [0, 0])
            n += 1
            win[k][1] += 1
            if abs(r['px'] - q['px']) <= 8:
                w += 1
                win[k][0] += 1
            elif first is None:
                first = (f, r['px'], q['px'])
    out.append('### (a) by host frame, port = arcade + %d (start taken %d vs %d)' % (off, sa, sp))
    out.append('')
    out.append('The tape is keyed by host frame, so input-driven motion (the walk) is compared at the same '
               'host frame; rows at or after a hang are excluded.')
    res['frame_within8'] = w / n if n else 0.0
    res['frame_first'] = first
    out.append('- frames with both heroes in the play handler and x within 8 px: %d of %d (%.1f%%)' % (w, n, 100 * res['frame_within8']))
    out.append('- per 3000-frame window: ' + ', '.join('%d: %d/%d' % (k, v[0], v[1]) for k, v in win.items()))
    if first is None:
        out.append('- first frame where |px| differs by more than 8: none')
    else:
        out.append('- first frame where |px| differs by more than 8: arcade frame %d (arcade %04x, port %04x); '
                   'first live enemy (0xFFF154) arcade %s, port %s' % (first[0], first[1], first[2], enemy_a, enemy_p))
        pre = (enemy_a is None or first[0] < enemy_a) and (enemy_p is None or first[0] + off < enemy_p)
        if pre:
            out.append('- FAIL the hero diverges before any enemy is live on either machine: no combat to blame')
            fails.append('divergence: hero x diverges at frame %d before the first enemy' % first[0])
    # (b) by (segment, tick)
    out.append('')
    out.append('### (b) by (segment, tick), port tick shifted by the measured skew')
    out.append('')
    ga, gp = segments(A), segments(P)
    if not ga or not gp:
        out.append('- no in-play segment on %s' % ('the arcade' if not ga else 'the port'))
        return res
    d, m, t = tick_skew(ga[0], gp[0])
    res['skew'] = d
    out.append('- tick skew %+d (port tick t pairs arcade tick t%+d; %d of %d opening ticks match on (px, py) at that shift)' % (d, -d, m, t))
    out.append('')
    out.append('| segment | level | arcade frames | port frames | arcade ticks | port ticks | aligned |')
    out.append('|---|---|---|---|---|---|---|')
    pairs = []
    for k in range(max(len(ga), len(gp))):
        a = ga[k] if k < len(ga) else None
        p = gp[k] if k < len(gp) else None
        if a is None or p is None:
            s = a or p
            out.append('| %d | %d | %s | %s | %s | %s | %s only |' % (
                k, s['level'], '%d..%d' % (a['start'], a['end']) if a else '-', '%d..%d' % (p['start'], p['end']) if p else '-',
                len(a['rows']) if a else '-', len(p['rows']) if p else '-', 'arcade' if a else 'port'))
            continue
        if a['level'] != p['level']:
            out.append('| %d | %d/%d | %d..%d | %d..%d | %d | %d | levels differ, alignment stops |' % (
                k, a['level'], p['level'], a['start'], a['end'], p['start'], p['end'], len(a['rows']), len(p['rows'])))
            break
        keys = [tk for tk in a['rows'] if tk + d in p['rows']]
        pairs += [(k, tk, a['rows'][tk], p['rows'][tk + d]) for tk in keys]
        out.append('| %d | %d | %d..%d | %d..%d | %d | %d | %d |' % (
            k, a['level'], a['start'], a['end'], p['start'], p['end'], len(a['rows']), len(p['rows']), len(keys)))
    res['aligned'] = len(pairs)
    if not pairs:
        out.append('- nothing aligned')
        return res
    la0 = A['byframe'][sa]['lives']
    lp0 = P['byframe'][sp]['lives']
    first_px = first_lives = first_score = first_live = None
    within = both = 0
    for k, tk, a, p in pairs:
        al, pl = hero_live(a), hero_live(p)
        if al != pl and first_live is None:
            first_live = (k, tk)
        if al and pl:
            both += 1
            if abs(a['px'] - p['px']) <= 8:
                within += 1
            elif first_px is None:
                first_px = ((k, tk), a['px'], p['px'])
        if (la0 - a['lives']) != (lp0 - p['lives']) and first_lives is None:
            first_lives = ((k, tk), la0 - a['lives'], lp0 - p['lives'])
        if a['score'] != p['score'] and first_score is None:
            first_score = ((k, tk), a['score'], p['score'])
    fmt = lambda k: 'segment %d tick %04x' % k
    out.append('')
    out.append('- first tick where |px| differs by more than 8: %s' %
               ('none' if first_px is None else '%s (arcade %04x, port %04x)' % (fmt(first_px[0]), first_px[1], first_px[2])))
    out.append('- first tick where the hero is in the play handler on one side only: %s' %
               ('none' if first_live is None else fmt(first_live)))
    out.append('- first tick where lives LOST since the start differ (start values arcade %d, port %d): %s' %
               (la0, lp0, 'none' if first_lives is None else '%s (arcade lost %d, port lost %d)' % (fmt(first_lives[0]), first_lives[1], first_lives[2])))
    out.append('- first tick where the score differs: %s' %
               ('none' if first_score is None else '%s (arcade %08x, port %08x)' % (fmt(first_score[0]), first_score[1], first_score[2])))
    res['within8'] = within / both if both else 0.0
    res['first_px'] = first_px
    out.append('- aligned ticks with both heroes in play and x within 8 px: %d of %d (%.1f%%)' % (within, both, 100 * res['within8']))
    return res


# ---------------------------------------------------------------- section 4: kinds

def live_enemy(status):
    """0x8800 and 0x8900 are live enemies: bit 15 active plus bit 11 of the status word."""
    return ((status >> 8) & 0x88) == 0x88


def kinds_per_level(M):
    """{level: {handler: first frame}} of live enemies in slots 16-39."""
    out = {}
    for f, s, stt, h in M['cs']:
        if not (16 <= s <= 39) or not live_enemy(stt):
            continue
        r = M['byframe'].get(f)
        if r is None or r['demo'] & 1:
            continue
        out.setdefault(r['level'], {}).setdefault(h, f)
    return out


def range_census(M):
    """{(level, range name): {handler: [samples, first frame]}} over in-play census rows,
    excluding rows at or after a hang (the frozen table would count as thousands of
    samples)."""
    out = {}
    hang = M.get('hang')
    for f, s, stt, h in M['cs']:
        r = M['byframe'].get(f)
        if r is None or not in_play(r) or (hang and f >= hang[0]):
            continue
        for name, lo, hi in SLOT_RANGES:
            if lo <= s <= hi:
                if name == '16-39' and not live_enemy(stt):
                    break
                e = out.setdefault((r['level'], name), {}).setdefault(h, [0, f])
                e[0] += 1
                break
    return out


def max_scroll(M):
    """{level: furthest 0xFFF132 reached in play before any hang}: the level's progress axis."""
    out = {}
    hang = M.get('hang')
    for r in M['tl']:
        if in_play(r) and not (hang and r['frame'] >= hang[0]):
            out[r['level']] = max(out.get(r['level'], 0), r['scroll'])
    return out


def census_samples(M):
    """{level: number of census sample frames taken in play before any hang}: the
    denominator of a handler's density (samples per sample frame, i.e. slots it held on
    average)."""
    out = {}
    hang = M.get('hang')
    seen = set()
    for f, _s, _stt, _h in M['cs']:
        r = M['byframe'].get(f)
        if r is None or not in_play(r) or (hang and f >= hang[0]) or f in seen:
            continue
        seen.add(f)
        out[r['level']] = out.get(r['level'], 0) + 1
    return out


def section_kinds(A, P, min_samples, out, fails):
    out.append('## 4. Object census per level and slot range')
    out.append('')
    ka, kp = kinds_per_level(A), kinds_per_level(P)
    ra, rp = range_census(A), range_census(P)
    levels_p = {r['level'] for r in P['tl'] if in_play(r)}
    levels_a = {r['level'] for r in A['tl'] if in_play(r)}
    msp = max_scroll(P)
    na, np_ = census_samples(A), census_samples(P)
    out.append('Samples are census rows (every 60 frames, in play, before any hang). A handler the arcade holds '
               'for %d or more samples and the port never is a FAIL, unless the arcade first saw it at a scroll '
               '(0xFFF132) beyond the furthest the port reached in that level: those are BEYOND PORT PROGRESS, '
               'downstream of whatever stopped the walk, and are listed, not failed. A handler SUSTAINED on the '
               'arcade (density, samples per sample frame, of 0.1 or more) that the port holds at under a tenth '
               'of that density is a FAIL too (the hero\'s five trail objects 0x6CB8 live in slots 1-5 for the '
               'whole level on the arcade). Port-only handlers are listed.' % min_samples)
    out.append('')
    out.append('| level | slots | arcade handlers (samples) | port handlers (samples) | arcade only | beyond port progress | sustained on the arcade only | port only | verdict |')
    out.append('|---|---|---|---|---|---|---|---|---|')
    hx = lambda c: ' '.join('%x(%d)' % (h, c[h][0]) for h in sorted(c)) or '-'
    for lv in sorted(levels_a | levels_p):
        reach = msp.get(lv)
        da, dp = max(na.get(lv, 0), 1), max(np_.get(lv, 0), 1)
        for name, _lo, _hi in SLOT_RANGES:
            a, p = ra.get((lv, name), {}), rp.get((lv, name), {})
            if not a and not p:
                continue
            aonly = {h: v for h, v in a.items() if h not in p}
            beyond = {h: v for h, v in aonly.items()
                      if reach is not None and A['byframe'][v[1]]['scroll'] > reach}
            aonly = {h: v for h, v in aonly.items() if h not in beyond}
            ponly = {h: v for h, v in p.items() if h not in a}
            thin = {h: v for h, v in a.items() if h in p and v[0] / da >= 0.1 and p[h][0] / dp < v[0] / da / 10}
            strong = [h for h, v in aonly.items() if v[0] >= min_samples]
            if lv not in levels_p:
                v = 'FAIL (port never reached level %d)' % (lv + 1)
            elif strong or thin:
                v = 'FAIL'
                if strong:
                    fails.append('kinds: level %d slots %s, arcade-only %s' % (lv + 1, name, ' '.join('%x' % h for h in sorted(strong))))
                if thin:
                    fails.append('kinds: level %d slots %s, sustained on the arcade only %s' % (
                        lv + 1, name, ' '.join('%x (density %.2f vs %.3f)' % (h, a[h][0] / da, p[h][0] / dp) for h in sorted(thin))))
            else:
                v = 'ok'
            thin_s = ' '.join('%x(%.2f/%.3f)' % (h, a[h][0] / da, p[h][0] / dp) for h in sorted(thin)) or '-'
            out.append('| %d | %s | %s | %s | %s | %s | %s | %s | %s |' % (lv + 1, name, hx(a), hx(p), hx(aonly), hx(beyond), thin_s, hx(ponly), v))
        if lv not in levels_p:
            fails.append('kinds: level %d never reached on the port' % (lv + 1))
        elif reach is not None:
            out.append('| %d | | port furthest scroll %04x; sample frames arcade %d port %d | | | | | | |' % (lv + 1, reach, na.get(lv, 0), np_.get(lv, 0)))
    stated = L2_KINDS_STATED
    l2a = set(ka.get(1, {}))
    if l2a:
        out.append('')
        out.append('Level-2 enemy kinds (slots 16-39) the arcade spawned not in the stated kind list: %s; stated kinds the arcade '
                   'never spawned: %s.' % (' '.join('%x' % h for h in sorted(l2a - stated)) or 'none',
                                           ' '.join('%x' % h for h in sorted(stated - l2a)) or 'none'))
    return ka, kp


# ---------------------------------------------------------------- section 5: video

def page_fill(tilemap):
    return [sum(1 for i in range(p, min(p + PAGE, len(tilemap)), 2) if tilemap[i] or tilemap[i + 1])
            for p in range(0, len(tilemap), PAGE)]


def text_rows_differ(a, b):
    n = min(len(a), len(b)) // TEXT_ROW
    return [r for r in range(n) if a[r * TEXT_ROW:(r + 1) * TEXT_ROW] != b[r * TEXT_ROW:(r + 1) * TEXT_ROW]]


def sprite_count(b):
    """Records the S16B sprite chip would draw: scanned in order until word 2 bit 15 (EOT;
    jts16_obj_scan.v:175-177 tests it in scan state 3, and state 2 reads xpos from word 1,
    so state 3 holds word 2), skipping word 2 bit 14 (hide) and records whose top byte is
    not below their bottom byte (word 0 = bottom:top, jts16_obj_scan.v:101-102)."""
    n = 0
    for i in range(0, len(b) - SPRITE_REC + 1, SPRITE_REC):
        w0 = (b[i] << 8) | b[i + 1]
        w2 = (b[i + 4] << 8) | b[i + 5]
        bottom, top = w0 >> 8, w0 & 0xFF
        if w2 & 0x8000:
            break
        if not (w2 & 0x4000) and top < bottom:
            n += 1
    return n


def picture(path):
    """The screenshot as an L image at 320x224: the ares canvas is cropped to its content
    box first (any other oversize image to its nonblack bounding box), MAME's 320x224 is
    taken as is."""
    from PIL import Image
    import numpy as np
    im = Image.open(path).convert('L')
    if im.size == ARES_CANVAS:
        im = im.crop(ARES_CONTENT)
    elif im.size[0] > 400:
        a = np.asarray(im)
        ys, xs = np.where(a.max(axis=1) > 8)[0], np.where(a.max(axis=0) > 8)[0]
        if len(ys) and len(xs):
            im = im.crop((int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1))
    return im.resize((320, 224), Image.BILINEAR)


def luma_diff(pa, pb, out_png):
    """Mean |luma| diff of two screenshots at 320x224 (ares canvas cropped), and the
    side-by-side png."""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    ia, ib = picture(pa), picture(pb)
    a, b = np.asarray(ia, dtype=np.int16), np.asarray(ib, dtype=np.int16)
    d = np.abs(a - b)
    side = Image.new('L', (320 * 3 + 8, 224), 128)
    side.paste(ia, (0, 0))
    side.paste(ib, (324, 0))
    side.paste(Image.fromarray(d.astype('uint8')), (648, 0))
    side.save(out_png)
    return float(d.mean())


def section_video(A, P, ma, mp, report_dir, ev_slack, shot_slack, out, fails):
    out.append('## 5. Video at shared milestones')
    out.append('')
    out.append('ev dumps within %d frames of each side\'s own milestone frame; screenshots within %d frames on '
               'both sides, else not scored (the arcade oracle snapshots at every ev dump since 2026-10-22, '
               'older corpora only every 300 frames).' % (ev_slack, shot_slack))
    out.append('')
    out.append('| milestone | arcade ev | port ev | pages differing (cells) | text rows differing | sprites a/p | shots a/p | luma diff | png |')
    out.append('|---|---|---|---|---|---|---|---|---|')
    results = []
    sa, sp = screenshots(A['dir']), screenshots(P['dir'])
    skipped = []
    for name in ma:
        fa, fp = ma[name], mp[name]
        if isinstance(fa, (list, str)) or isinstance(fp, (list, str)) or fa is None or fp is None:
            continue
        ea, ep = nearest(A['ev'], fa, ev_slack), nearest(P['ev'], fp, ev_slack)
        if ea is None or ep is None:
            na, np_ = nearest(A['ev'], fa, 1 << 30), nearest(P['ev'], fp, 1 << 30)
            skipped.append('%s (nearest ev arcade %s, port %s)' % (name, na, np_))
            continue
        da, dp = A['ev'][ea], P['ev'][ep]
        cells, rows, spr = '-', '-', '-'
        if 'tilemap' in da and 'tilemap' in dp:
            ta, tp = open(da['tilemap'], 'rb').read(), open(dp['tilemap'], 'rb').read()
            fa_, fp_ = page_fill(ta), page_fill(tp)
            n = min(len(fa_), len(fp_))
            diffs = ['p%d:%+d' % (i, fp_[i] - fa_[i]) for i in range(n) if fa_[i] != fp_[i]]
            cells = ' '.join(diffs) if diffs else 'none (%d pages)' % n
        if 'text' in da and 'text' in dp:
            r = text_rows_differ(open(da['text'], 'rb').read(), open(dp['text'], 'rb').read())
            rows = ' '.join(str(x) for x in r) if r else 'none'
        if 'sprite' in da and 'sprite' in dp:
            ba, bp = open(da['sprite'], 'rb').read(), open(dp['sprite'], 'rb').read()
            m = min(len(ba), len(bp))
            spr = '%d/%d' % (sprite_count(ba[:m]), sprite_count(bp[:m]))
        shot_a, shot_p = nearest(sa, fa, shot_slack), nearest(sp, fp, shot_slack)
        luma, png = '-', '-'
        shots = '-'
        if shot_a is not None and shot_p is not None:
            png = 'video_%s.png' % re.sub(r'[^a-z0-9]+', '_', name.lower())
            v = luma_diff(sa[shot_a], sp[shot_p], os.path.join(report_dir, png))
            shots = '%d/%d' % (shot_a, shot_p)
            luma = '%.1f' % v if v is not None else 'no PIL'
        else:
            na, np_ = nearest(sa, fa, 1 << 30), nearest(sp, fp, 1 << 30)
            shots = 'none within %d (nearest %s/%s)' % (shot_slack, na, np_)
        out.append('| %s | %d | %d | %s | %s | %s | %s | %s | %s |' % (name, ea, ep, cells, rows, spr, shots, luma, png))
        results.append((name, cells, rows, spr, luma))
    if not results:
        out.append('| (no milestone has ev dumps within %d frames on both sides) | | | | | | | | |' % ev_slack)
    if skipped:
        out.append('')
        out.append('Milestones reached on both sides without ev dumps within %d frames: %s.' % (ev_slack, '; '.join(skipped)))
    return results


# ---------------------------------------------------------------- section 6: attract

def section_attract(accept, romname, out, fails, collapse=0):
    out.append('## 6. Attract scorecard (build/accept)')
    out.append('')
    got = {}
    for kind in ('soak', 'parity', 'temporal', 'artaudit'):
        p = os.path.join(accept, '%s_%s.txt' % (kind, romname)) if accept else None
        if p and os.path.exists(p):
            got[kind] = open(p).read()
    if not got:
        out.append('No accept files for %s under %s; section skipped.' % (romname, accept))
        return {}
    res = {}
    if 'soak' in got:
        m = re.search(r'flips/vint\s+([0-9.]+)', got['soak'])
        v = float(m.group(1)) if m else None
        ok = v is not None and v >= 0.78
        res['soak'] = v
        out.append('- soak flips/vint %s (>= 0.78): %s' % (v, 'ok' if ok else 'FAIL'))
        if not ok:
            fails.append('attract: soak %s' % v)
    if 'artaudit' in got:
        cells = [int(x) for x in re.findall(r'(\d+) visible cells', got['artaudit'])]
        ok = bool(cells) and all(c == 0 for c in cells)
        res['artaudit'] = cells
        out.append('- artaudit cells %s (0 on every line): %s' % (cells, 'ok' if ok else 'FAIL'))
        if not ok:
            fails.append('attract: artaudit %s' % cells)
    if 'temporal' in got:
        spans = re.findall(r'(\d+)-(\d+): presented (\d+)/(\d+).*?flicker/transition.*?total (\d+)', got['temporal'])
        res['temporal'] = spans
        ok = bool(spans) and all(int(p) >= 40 for _a, _b, p, _n, _f in spans)
        out.append('- temporal ' + '; '.join('%s-%s presented %s/%s flicker %s' % s for s in spans) +
                   ' (presented >= 40/59 in both spans): %s' % ('ok' if ok else 'FAIL'))
        if not ok:
            fails.append('attract: temporal presented %s' % [s[2] for s in spans])
    if 'parity' in got:
        lines = got['parity'].splitlines()
        ladder = None
        rows = []
        for line in lines:
            m = re.match(r'^(scene)\s+arc \|\s*(.*)$', line)
            if m:
                ladder = [int(x) for x in m.group(2).split()]
                continue
            m = re.match(r'^(.+?)\s+(\d+) \|\s*(.*)$', line)
            if m and ladder:
                vals = [int(x) for x in m.group(3).split()]
                thr = collapse if collapse > 0 else 20
                col = next((ladder[i] for i, v in enumerate(vals) if v < thr), None)
                rows.append((m.group(1).strip(), col, min(vals) if vals else None))
        res['parity'] = rows
        bad = [r for r in rows if r[1] is None]
        if collapse > 0:
            out.append('- parity collapse column per scene: ' +
                       ', '.join('%s=%s' % (n, c if c is not None else 'NONE(min %s)' % mn) for n, c, mn in rows) +
                       ' (every row < %d somewhere): %s' % (collapse, 'ok' if rows and not bad else 'FAIL'))
            if not rows or bad:
                fails.append('attract: parity rows never collapsing: %s' % [r[0] for r in bad])
        else:
            out.append('- parity ladder per scene (column where the diff first reads under 20, and the row minimum; '
                       'informational, read against the previous candidate: no rom on record collapses every row): ' +
                       ', '.join('%s=%s/min %s' % (n, c if c is not None else 'none', mn) for n, c, mn in rows))
    return res


# ---------------------------------------------------------------- main

def load_machine(d, rom=None):
    tl = load_timeline(d)
    M = {'dir': d, 'tl': tl, 'byframe': {r['frame']: r for r in tl}, 'cs': load_census(d),
         'ev': ev_dumps(d), 'hang': None}
    if rom:
        M['rom'] = rom
        M['flags'] = os.path.splitext(rom)[0] + '.flags'
    return M


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--arcade', required=True)
    ap.add_argument('--port', required=True)
    ap.add_argument('--rom', required=True, help='the port rom; its basename names the accept files and <rom>.flags')
    ap.add_argument('--accept', default='build/accept')
    ap.add_argument('--report', default=None)
    ap.add_argument('--tape', default=None, help='the input tape (default: mame.log TAPE line, else tools/inputs/level1_full.csv)')
    ap.add_argument('--lag-slack', type=int, default=3, help='port active-frame lag may exceed the arcade\'s by this')
    ap.add_argument('--hang-frames', type=int, default=300, help='in-play frames with a frozen tick that count as a hang')
    ap.add_argument('--kind-min-samples', type=int, default=2, help='arcade census samples a handler needs to FAIL when absent on the port')
    ap.add_argument('--ev-slack', type=int, default=8, help='frames between a milestone and the ev dump used for it')
    ap.add_argument('--shot-slack', type=int, default=8, help='frames between a milestone and the screenshot used for it')
    ap.add_argument('--parity-collapse', type=int, default=0,
                    help='when > 0, gate the attract parity rows: every row must read below this somewhere (default off)')
    args = ap.parse_args()

    romname = re.sub(r'\.32x$', '', os.path.basename(args.rom))
    report = args.report or os.path.join(args.port, 'REPORT.md')
    report_dir = os.path.dirname(os.path.abspath(report))
    os.makedirs(report_dir, exist_ok=True)
    tape_log = tape_from_log(args.arcade)
    tape_path = args.tape or (tape_log if tape_log and os.path.exists(tape_log) else None) or os.path.join(HERE, 'inputs', 'level1_full.csv')
    tape = load_tape(tape_path)
    A, P = load_machine(args.arcade), load_machine(args.port, args.rom)
    fails = []
    out = ['# Gameplay parity: %s vs arcade' % romname, '',
           'generated %s by %s' % (time.strftime('%Y-%m-%d %H:%M:%S'), os.path.relpath(os.path.abspath(__file__))),
           'arcade: %s (%d frames, %d census rows, %d ev dumps)' % (args.arcade, len(A['tl']), len(A['cs']), len(A['ev'])),
           'port: %s (%d frames, %d census rows, %d ev dumps)' % (args.port, len(P['tl']), len(P['cs']), len(P['ev'])),
           'tape: %s (%d events)%s' % (tape_path, len(tape), '' if not tape_log else '; arcade corpus recorded on %s' % tape_log), '']

    setup = section_setup(A, P, tape_path, tape_log, args.hang_frames, out, fails)
    out.append('')
    inp = section_inputs(A, P, tape, args.lag_slack, out, fails)
    out.append('')
    l2_kinds = set(kinds_per_level(A).get(1, {})) | L2_KINDS_STATED
    ma, mp, verdicts = section_milestones(A, P, l2_kinds, setup['pin'], out, fails)
    out.append('')
    div = section_divergence(A, P, out, fails)
    out.append('')
    ka, kp = section_kinds(A, P, args.kind_min_samples, out, fails)
    out.append('')
    vid = section_video(A, P, ma, mp, report_dir, args.ev_slack, args.shot_slack, out, fails)
    out.append('')
    att = section_attract(args.accept, romname, out, fails, args.parity_collapse)
    out.append('')
    out.append('## Verdict')
    out.append('')
    out.append('FAIL: %d' % len(fails) if fails else 'PASS')
    for f in fails:
        out.append('- ' + f)
    with open(report, 'w') as fh:
        fh.write('\n'.join(out) + '\n')

    # the 15-line summary
    reached = sum(1 for v in verdicts.values() if v == 'ok')
    missing = [n for n, v in verdicts.items() if v == 'FAIL']
    ha, hp = inp['arcade'], inp['port']
    va, vp = setup['dips']
    sa, sp = setup['start']
    hang_a, hang_p = setup['hang']
    s = []
    s.append('parity_compare %s vs arcade: %s' % (romname, 'FAIL (%d)' % len(fails) if fails else 'PASS'))
    s.append('setup: DIPs arcade %s port %s; start credits/lives arcade %s port %s; lives pin arcade %s port %s' % (
        '%02x/%02x' % va if va else '?', '%02x/%02x' % vp if vp else '?',
        '%d/%d' % (sa['credits_before'] - sa['credits_after'], sa['lives']) if sa else '-',
        '%d/%d' % (sp['credits_before'] - sp['credits_after'], sp['lives']) if sp else '-',
        setup['pin'][0], setup['pin'][1]))
    s.append('hang: arcade %s; port %s; stalled frames arcade %d port %d' % (
        'tick %04x from %d for %d' % (hang_a[2], hang_a[0], hang_a[1]) if hang_a else 'none',
        'tick %04x from %d for %d' % (hang_p[2], hang_p[0], hang_p[1]) if hang_p else 'none',
        setup['stalled'][0], setup['stalled'][1]))
    s.append('inputs: %d tape events; lag hist arcade %s' % (len(tape), dict(ha)))
    s.append('inputs: lag hist port %s' % dict(hp))
    s.append('inputs: one-sided %d, excused %s, port slower by >%d active frames %d' % (inp['one_sided'], dict(inp['excused']) or 'none', args.lag_slack, inp['slow']))
    s.append('milestones: %d reached on both, %d arcade-only%s' % (reached, len(missing), ': ' + ', '.join(missing) if missing else ''))
    for n in ('start taken', 'boss transform', 'round clear', 'level 2'):
        fa, fp = ma.get(n), mp.get(n)
        s.append('milestone %s: arcade %s port %s delta %s' % (n, fa, fp, (fp - fa) if fa is not None and fp is not None else '-'))
    s.append('divergence: by frame (offset %s) x within 8 px on %.1f%%, first |px|>8 at arcade frame %s; by tick (skew %s) %d aligned, within 8 on %.1f%%'
             % (div['offset'], 100 * div['frame_within8'], div['frame_first'][0] if div['frame_first'] else 'none',
                div['skew'], div['aligned'], 100 * div['within8']))
    s.append('kinds (slots 16-39): ' + '; '.join('L%d arcade %d port %d' % (lv + 1, len(ka.get(lv, {})), len(kp.get(lv, {})))
                                                 for lv in sorted(set(ka) | set(kp))))
    s.append('video: %d shared milestones compared%s; attract: %s' % (
        len(vid), ', luma ' + ' '.join(v[4] for v in vid) if vid else '',
        ('soak %s artaudit %s temporal %s parity min %s' % (
            att.get('soak'), att.get('artaudit'), [x[2] for x in att.get('temporal', [])],
            [m for _n, _c, m in att.get('parity', [])])) if att else 'no accept files'))
    s.append('report: %s; fails: %s' % (report, '; '.join(fails) if fails else 'none'))
    print('\n'.join(s[:15]))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
