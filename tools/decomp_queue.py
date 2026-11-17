#!/usr/bin/env python3
"""Turn a logic-gate failure into a queue of 68K functions to fix, one item per object handler.

    python3 tools/decomp_queue.py altbeast --check-demo build/decomp/check-demo.log
    python3 tools/decomp_queue.py goldnaxe --oracle tests/goldnaxe/arcade <port dump dir>
        [--queue build/decomp/<game>.queue.jsonl]   (the default)

Both gates compare object records against the arcade's work RAM at fixed targets. This
tool reads the SAME image pairs the gate compared, and for every live record that differs
it takes the handler long out of the ARCADE's record (+2 in both titles): that is the 68K
routine that owned the record on the machine that is right, so it is where a fix starts.
Items are ranked by the first target on the arcade's timeline where the handler diverges
-- a later difference is usually a consequence of an earlier one, so the head of the queue
is the root-cause candidate, not the biggest number.

Inputs are image pairs, never the gate's prose, except for the one line per target that
names the two images (check_demo.py prints the arcade dump and the port frame; the port
image is tests/demo-b/work/port_<rom tag>_<frame>.bin). A new title adds a GAMES entry and,
if its gate writes another format, one pair reader.

Re-running merges: an item already in the queue keeps its status and attempt count, so a
runner can mark items without losing them when the gate is re-read. An item whose handler
no longer diverges is marked 'clear'.
"""
import argparse, csv, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per title: the object tables inside the 16 KB work-RAM image at 0xFFC000, as
# (offset, slots, record size); live = byte 0 bit 7; the handler long is at +2.
# altbeast: check_demo.py live() and records.py. goldnaxe: tests/goldnaxe/oracle.py TABLES
# (LOOP-DECOMPILE-GOLDNAXE "Relocation").
GAMES = {
    'altbeast': {'tables': [(0x0000, 64, 0x80)], 'handler': 2,
                 'ranges': 'docs/audit/func_ranges.txt', 'hand': 'sh_src/portfull/hand'},
    'goldnaxe': {'tables': [(0x0000, 64, 0x80), (0x2100, 16, 0x40)], 'handler': 2,
                 'ranges': None, 'hand': None},
}


def pairs_from_gate(d, arc):
    """The images the gate itself compared, written by check_demo.py under DEMO_PAIRS=<d>:
    port_<scene>_<target>_<nth>.bin + pair_*.json naming the arcade dump. Since 411710e2 the
    gate compares the port at the tick's END (tick_end(), built in memory), so this is the only
    faithful source; reading the frame-end dump scored ~100 bytes on rows it reads clean."""
    out = []
    for n in sorted(os.listdir(d)):
        if n.startswith('pair_') and n.endswith('.json'):
            j = json.load(open(os.path.join(d, n)))
            k = n[len('pair_'):-len('.json')]
            label = '%s %04x%s' % (j['scene'], j['target'], '#%d' % j['nth'] if j['nth'] else '')
            when = int(re.match(r'^aw_[A-Z]_(\d+)\.bin$', j['arcade']).group(1))
            out.append((label, when, os.path.join(arc, j['arcade']), os.path.join(d, 'port_%s.bin' % k)))
    return out


def pairs_check_demo(log):
    """(label, timeline key, arcade image, port image) for every target check_demo.py
    printed a row for. The row: scene, target[#nth], compared tick, port frame, dump name.
    With DEMO_PAIRS set (the runner always sets it) the gate's own images are used and a gate
    that did not write them is an error, never a silent fall back to the frame-end dump."""
    arc = os.environ.get('DEMO_TICKS', os.path.join(ROOT, 'tests/demo-b/arcade'))
    pd = os.environ.get('DEMO_PAIRS')
    if pd:
        got = pairs_from_gate(pd, arc) if os.path.isdir(pd) else []
        if not got and any(re.match(r'^[A-Z] [0-9a-f]{4}', l) for l in open(log)):
            raise SystemExit('decomp_queue: %s has target rows but %s holds no pair files: the '
                             'gate predates DEMO_PAIRS (branch decomp/gate-pairs, b8211705)' % (log, pd))
        return got
    tag, out = None, []
    work = os.environ.get('DEMO_WORK', os.path.join(ROOT, 'tests/demo-b/work'))
    arc = os.environ.get('DEMO_TICKS', os.path.join(ROOT, 'tests/demo-b/arcade'))
    for line in open(log):
        m = re.match(r'^rom: ([0-9a-f]{8}) ', line)
        if m:
            tag = m.group(1)
        m = re.match(r'^([A-Z]) ([0-9a-f]{4})(#\d+)?\s+([0-9a-f]{4})\s+(\d+)\s+(aw_[A-Z]_(\d+)\.bin)', line)
        if m:
            if tag is None:
                raise SystemExit('decomp_queue: %s has a target row before its "rom:" line' % log)
            label = '%s %s%s' % (m.group(1), m.group(2), m.group(3) or '')
            out.append((label, int(m.group(7)), os.path.join(arc, m.group(6)),
                        os.path.join(work, 'port_%s_%s.bin' % (tag, m.group(5)))))
    if not out and tag is None:
        raise SystemExit('decomp_queue: %s is not a check_demo.py log (no "rom:" line)' % log)
    return out


def pairs_oracle(ref, port):
    """tests/goldnaxe/oracle.py's directory format: index.csv + aw_<scene>_<frame>.bin,
    paired on (scene, tick)."""
    def idx(d):
        return {(r['scene'], r['tick']): r for r in csv.DictReader(open(os.path.join(d, 'index.csv')))}
    a, p = idx(ref), idx(port)
    out = []
    for k, r in a.items():
        if k in p:
            name = lambda d, x: os.path.join(d, 'aw_%s_%s.bin' % (x['scene'], x['frame']))
            out.append(('%s %s' % k, int(r['frame']), name(ref, r), name(port, p[k])))
    return out


# ---------------------------------------------------------------- the gameplay gate
# `make parity` (docs/handoff/PARITY-HARNESS.md) writes REPORT.md whose "## Verdict" list is
# parity_compare.py's fails[], one line per failure, formats fixed in that file. The LOGIC
# failures become queue keys; setup/input failures void the run (the machines are not the
# same game), attract failures are the renderer lane and ignored here.
PARITY_LOGIC = [
    (r'^kinds: level (\d+) slots (\S+), arcade-only (.+)$', 'kind'),
    (r'^kinds: level (\d+) slots (\S+), sustained on the arcade only (.+)$', 'thin'),
    (r'^kinds: level (\d+) never reached on the port$', 'level'),
    (r'^milestone: (.+?) reached on the arcade at \d+, never on the port$', 'milestone'),
    (r"^milestone: score never drawn", 'milestone'),
    (r'^port hang: tick frozen', 'hang'),
    (r'^divergence: hero x diverges at frame \d+ before the first enemy$', 'divergence'),
]


def parity_fails(report):
    """{key: verdict line} for every logic failure in a parity REPORT.md. Raises when the run
    is void (setup or input failed) or a verdict line matches no known format, so a change in
    parity_compare's wording stops the loop instead of silently scoring nothing."""
    lines, on = [], False
    # the port's measurement rom pins its lives (PORTGODMODE, parity_compare's 'port-godmode'
    # pin) while the arcade dies: every 'lives lost #N' is that asymmetry, not port logic
    godmode = 'port-godmode' in open(report).read()
    for l in open(report):
        if l.startswith('## Verdict'):
            on = True
        elif on and l.startswith('- '):
            lines.append(l[2:].strip())
    if not on:
        raise SystemExit('decomp_queue: %s has no "## Verdict" section' % report)
    out = {}
    for l in lines:
        if l.startswith(('setup:', 'input:', 'arcade hang')):
            raise SystemExit('decomp_queue: parity run void, %s' % l)
        if l.startswith('attract:') or (godmode and l.startswith('milestone: lives lost')):
            continue
        for rx, kind in PARITY_LOGIC:
            m = re.match(rx, l)
            if not m:
                continue
            if kind in ('kind', 'thin'):
                for h in re.sub(r'\([^)]*\)', ' ', m.group(3)).split():   # drop '(density a vs b)'
                    out['%s:L%s:%s:0x%06X' % (kind, m.group(1), m.group(2), int(h, 16))] = l
            elif kind == 'level':
                out['level:L%s' % m.group(1)] = l
            elif kind == 'milestone':
                out['milestone:%s' % (m.group(1) if m.groups() else 'score drawn')] = l
            else:
                out[kind] = l
            break
        else:
            raise SystemExit('decomp_queue: unknown parity verdict line: %r' % l)
    return out


def parity_items(game, report):
    """Queue items from a parity report: one per handler for census failures (the kind the
    arcade spawns and the port does not, or sustains and the port does not), one per missing
    milestone, the hang, the early divergence."""
    ranges = load_ranges(GAMES[game]['ranges'])
    items = []
    for key, line in sorted(parity_fails(report).items()):
        kind = key.split(':')[0]
        h = key.rsplit(':', 1)[1] if kind in ('kind', 'thin') else None
        entry, name = owner(ranges, int(h, 16)) if h else (None, None)
        items.append({'game': game, 'handler': key, 'gate': 'parity', 'parity_kind': kind,
                      'verdict_line': line, 'function': name,
                      'entry': '0x%06X' % entry if entry is not None else None,
                      'hand_file': hand_file(game, entry), 'first_when': 0, 'bytes': 1,
                      'phase': False})
    return items


def load_ranges(path):
    """func_ranges.txt: start end_inclusive entry name, one range per line."""
    if not path or not os.path.exists(os.path.join(ROOT, path)):
        return []
    rs = []
    for line in open(os.path.join(ROOT, path)):
        f = line.split()
        if len(f) >= 4 and not line.startswith('#'):
            rs.append((int(f[0], 16), int(f[1], 16), int(f[2], 16), f[3]))
    return rs


def owner(ranges, addr):
    return next(((e, n) for s, x, e, n in ranges if s <= addr <= x), (None, None))


def hand_file(game, entry):
    d = GAMES[game]['hand']
    if not d or entry is None:
        return None
    for pre in ('FUN', 'AT_FUN'):
        p = os.path.join(d, '%s_%08x.c' % (pre, entry))
        if os.path.exists(os.path.join(ROOT, p)):
            return p
    return None


# A record the port holds that the ARCADE also held a few frames earlier or later is a
# timing (phase) difference, not wrong logic: STATE.md "THE DEMO-A RESIDUE IS PHASE, NOT
# LOGIC" (the 0x6CB8 trail spawns ~8 frames late; the 0xF352 pair is one frame out). The
# first unattended run spent $1.88 rediscovering that, so such records are filtered here.
PHASE_FRAMES = 10


def neighbours(fa, when):
    """Arcade images of the same scene within PHASE_FRAMES of this one (same directory,
    aw_<scene>_<frame>.bin), excluding the image itself."""
    d, n = os.path.split(fa)
    m = re.match(r'^aw_([A-Z])_(\d+)\.bin$', n)
    if not m:
        return []
    out = []
    for f in range(when - PHASE_FRAMES, when + PHASE_FRAMES + 1):
        q = os.path.join(d, 'aw_%s_%d.bin' % (m.group(1), f))
        if f != when and os.path.exists(q):
            out.append(open(q, 'rb').read())
    return out


def is_phase(game, t, s, p, near):
    off, _, sz = GAMES[game]['tables'][t]
    rp = p[off + s * sz: off + (s + 1) * sz]
    return any(b[off + s * sz: off + (s + 1) * sz] == rp for b in near)


def diverging(game, a, p):
    """[(table, slot, arcade handler, port handler, [differing offsets])] for each record
    live on either side that differs."""
    g, out = GAMES[game], []
    for t, (off, n, sz) in enumerate(g['tables']):
        for s in range(n):
            ra, rp = a[off + s * sz: off + (s + 1) * sz], p[off + s * sz: off + (s + 1) * sz]
            if not (ra[0] & 0x80 or rp[0] & 0x80):
                continue
            d = [i for i in range(sz) if ra[i] != rp[i]]
            if d:
                h = g['handler']
                out.append((t, s, int.from_bytes(ra[h:h + 4], 'big'),
                            int.from_bytes(rp[h:h + 4], 'big'), d, bool(ra[0] & 0x80), bool(rp[0] & 0x80)))
    return out


def build_items(game, pairs):
    ranges = load_ranges(GAMES[game]['ranges'])
    items = {}
    for label, when, fa, fp in sorted(pairs, key=lambda x: x[1]):
        if not os.path.exists(fp):
            raise SystemExit('decomp_queue: no port image %s for %s (the gate\'s work dir was '
                             'cleared or DEMO_WORK points elsewhere)' % (fp, label))
        a, p = open(fa, 'rb').read(), open(fp, 'rb').read()
        near = neighbours(fa, when)
        for t, s, ha, hp, offs, la, lp in diverging(game, a, p):
            phase = is_phase(game, t, s, p, near)
            # a record live only on the port has no arcade handler worth trusting: key it
            # on the port's, and say so
            h = ha if la else hp
            entry, name = owner(ranges, h & 0xFFFFFF)
            key = '%06x' % (h & 0xFFFFFF)
            it = items.setdefault(key, {
                'game': game, 'handler': '0x%06X' % (h & 0xFFFFFF),
                'function': name, 'entry': '0x%06X' % entry if entry is not None else None,
                'hand_file': hand_file(game, entry), 'first_target': label,
                'first_when': when, 'targets': [], 'slots': set(), 'offsets': set(),
                'bytes': 0, 'port_handler_differs': False, 'live_mismatch': False,
                'phase_hits': 0, 'logic_hits': 0})
            it['phase_hits' if phase else 'logic_hits'] += 1
            if phase:
                continue
            if it['logic_hits'] == 1:        # the first divergence that is not timing
                it['first_target'], it['first_when'] = label, when
            if label not in it['targets']:
                it['targets'].append(label)
            it['slots'].add('%d:%d' % (t, s) if len(GAMES[game]['tables']) > 1 else str(s))
            it['offsets'].update(offs)
            it['bytes'] += len(offs)
            it['port_handler_differs'] |= (ha != hp)
            it['live_mismatch'] |= (la != lp)
    out = []
    for it in items.values():
        it['phase'] = it['logic_hits'] == 0
        it['slots'] = sorted(it['slots'], key=lambda x: [int(v) for v in x.split(':')])
        it['offsets'] = ['0x%02X' % o for o in sorted(it['offsets'])]
        out.append(it)
    # logic items first, by first divergence; all-phase items last
    out.sort(key=lambda it: (it['phase'], it['first_when'], -it['bytes']))
    return out


def merge(path, game, fresh):
    old = {}
    if os.path.exists(path):
        for line in open(path):
            if line.strip():
                it = json.loads(line)
                old[it['handler']] = it
    seen = set()
    for rank, it in enumerate(fresh, 1):
        prev = old.get(it['handler'], {})
        it['rank'] = rank
        it['status'] = prev.get('status', 'todo') if prev.get('status') not in ('clear', 'phase') else 'todo'
        it['attempts'] = prev.get('attempts', 0)
        if it['phase'] and it['status'] == 'todo':
            it['status'] = 'phase'           # timing, not logic: not for the decomp loop
        seen.add(it['handler'])
    for h, it in old.items():
        if h not in seen and it.get('game') == game:
            it['status'] = 'clear'
            it['rank'] = None
            fresh.append(it)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w') as fh:
        for it in fresh:
            fh.write(json.dumps(it, sort_keys=True) + '\n')
    return fresh


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game', choices=sorted(GAMES))
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--check-demo', metavar='LOG')
    src.add_argument('--oracle', nargs=2, metavar=('REF', 'PORT'))
    src.add_argument('--parity', metavar='REPORT', help='a make parity REPORT.md (level-1 gameplay)')
    ap.add_argument('--queue')
    o = ap.parse_args()
    # one queue per gate: merge() marks items absent from a fresh read 'clear', so the attract
    # gate and the gameplay gate must never share a file
    q = o.queue or os.path.join(ROOT, 'build/decomp/%s.%squeue.jsonl' % (o.game, 'parity.' if o.parity else ''))
    if o.parity:
        pairs = []
        items = merge(q, o.game, parity_items(o.game, o.parity))
    else:
        pairs = pairs_check_demo(o.check_demo) if o.check_demo else pairs_oracle(*o.oracle)
        if not pairs:
            raise SystemExit('decomp_queue: no target pairs found')
        items = merge(q, o.game, build_items(o.game, pairs))
    todo = [it for it in items if it['status'] not in ('clear', 'phase')]
    nph = sum(1 for it in items if it['status'] == 'phase')
    print('%d image pairs read, %d handlers diverge in logic, %d in phase only -> %s'
          % (len(pairs), len(todo), nph, os.path.relpath(q, ROOT)))
    for it in todo[:10]:
        print('  %2d  %s  %-24s first %-10s targets %2d  bytes %4d  %s%s' % (
            it['rank'], it['handler'], it['function'] or '?', it['first_target'], len(it['targets']),
            it['bytes'], it['hand_file'] or 'no hand file', '  [port handler differs]' if it['port_handler_differs'] else ''))


if __name__ == '__main__':
    main()
