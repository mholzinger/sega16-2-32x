#!/usr/bin/env python3
"""Unattended decomp loop: one queue item per run, an agent writes the fix, the gate judges it.

    build/decomp/.venv/bin/python tools/decomp_runner.py altbeast [--runs N] [--dry-run]

Setup, once:
    python3 -m venv build/decomp/.venv && build/decomp/.venv/bin/pip install claude-agent-sdk
    ~/.config/anthropic/api_key (chmod 600) or $ANTHROPIC_API_KEY: a key from the org that
    holds the API credits

One run:
  1. Baseline. The gate (`make check-demo`) is run on the base commit in its own worktree,
     once per base commit, and tools/decomp_queue.py turns it into the queue.
  2. The top 'todo' item (earliest divergence) gets a fresh worktree on branch
     decomp/<game>-<handler>-<stamp>, seeded with the gitignored inputs the build reads.
  3. An Agent SDK session (Sonnet 5.5) in that worktree writes the fix: a hand file per
     CLAUDE.md "The port's workflow". It may build (`make port`); it may NOT run the gate or
     any emulator -- the runner does that, outside the agent, under the emulator lock.
  4. The gate runs in the worktree with its own DEMO_WORK. The result is scored against the
     baseline per handler (never the "N of 20" headline, which moves with tick landing):
     ACCEPT when the total differing record bytes drop and no handler diverges that did
     not diverge before. A rejected attempt is fed back to the same session for up to
     --rounds rounds.
  5. ACCEPT: commit on the worktree branch, which becomes the base of the next run (the
     chain is Mike's to review and merge; nothing reaches the line on its own).
     REJECT: the worktree and branch are deleted, the item's attempt count goes up, and
     after --max-attempts it is marked 'failed' and skipped.
Every run appends one line to build/decomp/runs.jsonl: item, verdict, cost, tokens, turns.

Emulators: the gate starts up to 8 ares-headless workers. Before a gate the runner takes
an exclusive lock (~/.cache/sega16-2-32x/emu.lock, so two runners never gate at once) and
waits until the machine's ares-headless count leaves room for those 8 under --emu-cap.
Interactive sessions are not capped by this; they are only waited for.
"""
import argparse, asyncio, datetime, fcntl, json, os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build/decomp')
STATE = os.path.join(OUT, 'state.json')
RUNS = os.path.join(OUT, 'runs.jsonl')
LOCK = os.path.expanduser('~/.cache/sega16-2-32x/emu.lock')
GATE_WORKERS = 8                      # check_demo.py ThreadPoolExecutor(max_workers=min(8, ...))
# gitignored inputs the out-of-tree build reads (tools/shadow_tree.sh links these four from
# ROOT when they exist, so a worktree needs them linked at its own root)
LINKED = ['roms', 'mame', 'docs/proof', 'srcref']
# gitignored generated blobs and Sega-derived banks inside the copied trees
SEEDED = ['sh_src', 'md_src', 'sndtest']   # sndtest: the gitignored speech banks a candidate build reads
# the auto-commit hook appends to this tracked file on every edit; it is not part of a fix
NOT_LOG = ':(exclude).claude/prompt.log'

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import decomp_queue  # noqa: E402


KEYFILE = os.path.expanduser('~/.config/anthropic/api_key')


def api_key():
    """$ANTHROPIC_API_KEY, else the chmod-600 key file (launchd does not read ~/.zshrc)."""
    k = os.environ.get('ANTHROPIC_API_KEY')
    if not k and os.path.exists(KEYFILE):
        k = open(KEYFILE).read().strip()
    return k


def sh(cmd, cwd=ROOT, **kw):
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, **kw)


def log(msg):
    print('%s %s' % (datetime.datetime.now().strftime('%H:%M:%S'), msg), flush=True)


def load_state():
    return json.load(open(STATE)) if os.path.exists(STATE) else {}


def save_state(st):
    json.dump(st, open(STATE, 'w'), indent=1, sort_keys=True)


def month_spend():
    if not os.path.exists(RUNS):
        return 0.0
    m = datetime.date.today().strftime('%Y-%m')
    return sum(r.get('cost_usd') or 0 for r in map(json.loads, open(RUNS)) if r['when'].startswith(m))


# ---------------------------------------------------------------- worktrees

def make_worktree(path, branch, base):
    if os.path.exists(path):
        drop_worktree(path, None)
    r = sh(['git', 'worktree', 'add', '-q', '-b', branch, path, base])
    if r.returncode:
        raise SystemExit('decomp_runner: git worktree add failed: %s' % r.stderr.strip())
    # the links are files to git, and roms/ mame/ ... in .gitignore match directories only
    excl = os.path.join(ROOT, sh(['git', 'rev-parse', '--git-common-dir']).stdout.strip(), 'info', 'exclude')
    have = open(excl).read().split('\n') if os.path.exists(excl) else []
    with open(excl, 'a') as fh:
        for d in LINKED:
            if '/' + d not in have:
                fh.write('/%s\n' % d)
    for d in LINKED:
        src = os.path.join(ROOT, d)
        if os.path.exists(src):
            os.makedirs(os.path.dirname(os.path.join(path, d)), exist_ok=True)
            os.symlink(src, os.path.join(path, d))
    for d in SEEDED:
        # copy only what the worktree lacks: the tracked sources are already there at the
        # base commit and must not be overwritten by the main checkout's working copy
        sh(['rsync', '-a', '--ignore-existing', '--exclude=*.o', '--exclude=*.d',
            '--exclude=__pycache__', os.path.join(ROOT, d) + '/', os.path.join(path, d) + '/'])
    # A FRESH CHECKOUT STAMPS EVERY TRACKED FILE NOW and rsync -a kept the blobs' old mtimes,
    # so make saw sh_src/speech.bin older than tools/speech_blob.py and re-ran it against
    # sound-lab sources the out-of-tree shadow cannot see (2026-10-25, every candidate build).
    # The seeded generated files are as new as the checkout.
    seeded = sh(['git', 'ls-files', '--others', '--ignored', '--exclude-standard', '--'] + SEEDED,
                cwd=path).stdout.split('\n')
    now = time.time()
    for f in filter(None, seeded):
        try:
            os.utime(os.path.join(path, f), (now, now))
        except OSError:
            pass


def drop_worktree(path, branch):
    sh(['git', 'worktree', 'remove', '--force', path])
    if os.path.exists(path):
        shutil.rmtree(path)
    sh(['git', 'worktree', 'prune'])
    if branch:
        sh(['git', 'branch', '-D', branch])


# ---------------------------------------------------------------- the gate

def ares_count():
    # by process name: a `pgrep -f` pattern also matches any shell whose command line names it
    return len(sh(['pgrep', '-x', 'ares-headless']).stdout.split())


ARES = os.environ.get('ARES', os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless'))
TICK_SDRAM = 0x2CC00 + 0x302A        # 0xFFF02A (check_demo.py WRAM_SDRAM_OFF + TICK)
TICK_FRAMES = (3000, 3400)


def tick_advance(work):
    """The attract tick at two frames 400 apart on the rom the gate just measured. A freeze
    passes check-demo: the p_A5 re-read dropped real divergence and hung the attract, and only
    a tick-advance test caught it (advisor thread, 2026-10-25). Returns (ok, (t0, t1))."""
    roms = sorted((os.path.join(work, n) for n in os.listdir(work) if n.startswith('rom_') and n.endswith('.32x')),
                  key=os.path.getmtime)
    if not roms:
        return False, None
    procs, outs = [], []
    for f in TICK_FRAMES:
        o = os.path.join(work, 'tick_%d.bin' % f)
        outs.append(o)
        procs.append(subprocess.Popen([ARES, roms[-1], '--frames', str(f), '--dump',
                                       'sdram:0x%x:2:%s' % (TICK_SDRAM, o)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    for pr in procs:
        pr.wait()
    if not all(os.path.exists(o) for o in outs):
        return False, None
    t = tuple(int.from_bytes(open(o, 'rb').read()[:2], 'big') for o in outs)
    return t[0] != t[1], t


def gate(wt, tag, emu_cap):
    """make check-demo in worktree wt, its own DEMO_WORK, then the attract tick-advance
    check; returns (log path, work dir, (ok, ticks))."""
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    work = os.path.join(OUT, 'work', tag)
    os.makedirs(work, exist_ok=True)
    logp = os.path.join(OUT, 'logs', '%s.check-demo.log' % tag)
    os.makedirs(os.path.dirname(logp), exist_ok=True)
    with open(LOCK, 'w') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        waited = False
        while ares_count() + GATE_WORKERS > emu_cap:
            if not waited:
                log('gate %s: waiting, %d ares-headless running (cap %d)' % (tag, ares_count(), emu_cap))
                waited = True
            time.sleep(30)
        log('gate %s: running' % tag)
        t0 = time.time()
        env = dict(os.environ, DEMO_WORK=work, DEMO_PAIRS=os.path.join(work, 'pairs'))
        env.pop('PORT_EXTRA', None)
        with open(logp, 'w') as fh:
            subprocess.run(['make', 'check-demo'], cwd=wt, env=env, stdout=fh, stderr=subprocess.STDOUT)
        adv = tick_advance(work)
        log('gate %s: done in %d min, attract tick %s' % (tag, (time.time() - t0) / 60, adv[1]))
    return logp, work, adv


def score(game, logp, work):
    """{handler: bytes} over every target, or None when the gate produced no target table
    (a build failure)."""
    os.environ['DEMO_WORK'] = work
    os.environ['DEMO_PAIRS'] = os.path.join(work, 'pairs')
    try:
        pairs = decomp_queue.pairs_check_demo(logp)
    except SystemExit:
        return None
    if not pairs:
        return None
    # logic bytes only: decomp_queue drops records the arcade also held within a few frames
    # (timing), so a change that merely shifts phase can neither pass nor fail an item
    return {it['handler']: it['bytes'] for it in decomp_queue.build_items(game, pairs) if it['bytes']}


def verdict(base, new, handler):
    if new is None:
        return 'REJECT', 'the gate produced no target table (build failed?)'
    fresh = sorted(set(new) - set(base))
    b, n = sum(base.values()), sum(new.values())
    if fresh:
        return 'REJECT', 'new divergence in %s' % ' '.join(fresh)
    if n >= b:
        return 'REJECT', 'record bytes %d -> %d (item %d -> %d)' % (b, n, base.get(handler, 0), new.get(handler, 0))
    return 'ACCEPT', 'record bytes %d -> %d (item %d -> %d), handlers %d -> %d' % (
        b, n, base.get(handler, 0), new.get(handler, 0), len(base), len(new))


def demo_no_worse(base, new):
    """The attract gate as a guard for a gameplay fix: it must not add a diverging handler or
    logic bytes."""
    if new is None:
        return 'REJECT', 'check-demo produced no target table'
    fresh = sorted(set(new) - set(base))
    if fresh:
        return 'REJECT', 'attract logic regressed: new divergence in %s' % ' '.join(fresh)
    if sum(new.values()) > sum(base.values()):
        return 'REJECT', 'attract logic regressed: %d -> %d bytes' % (sum(base.values()), sum(new.values()))
    return 'ACCEPT', ''


def baseline(game, base, emu_cap, st):
    sha = sh(['git', 'rev-parse', base]).stdout.strip()
    bl = st.setdefault('baselines', {}).get(sha)
    if bl and os.path.exists(bl['log']):
        return sha, bl
    wt = os.path.join(OUT, 'wt', 'base-%s' % sha[:8])
    make_worktree(wt, 'decomp/base-%s' % sha[:8], sha)
    try:
        logp, work, adv = gate(wt, 'base-%s' % sha[:8], emu_cap)
    finally:
        drop_worktree(wt, 'decomp/base-%s' % sha[:8])
    sc = score(game, logp, work)
    if sc is None:
        raise SystemExit('decomp_runner: the baseline gate on %s produced no target table; see %s' % (sha[:8], logp))
    if not adv[0]:
        raise SystemExit('decomp_runner: the base %s fails the attract tick-advance check %s: fix the '
                         'base before the loop ranks anything on it' % (sha[:8], adv[1]))
    os.environ['DEMO_WORK'] = work
    os.environ['DEMO_PAIRS'] = os.path.join(work, 'pairs')
    decomp_queue.merge(queue_path(game), game, decomp_queue.build_items(game, decomp_queue.pairs_check_demo(logp)))
    st['baselines'][sha] = bl = {'log': logp, 'work': work, 'score': sc}
    save_state(st)
    return sha, bl


GATE = 'demo'                         # set from --gate: 'demo' (check-demo) or 'parity' (level-1 play)


def queue_path(game):
    return os.path.join(OUT, '%s.%squeue.jsonl' % (game, 'parity.' if GATE == 'parity' else ''))


def read_queue(game):
    p = queue_path(game)
    return [json.loads(l) for l in open(p) if l.strip()] if os.path.exists(p) else []


def write_queue(game, items):
    with open(queue_path(game), 'w') as fh:
        for it in items:
            fh.write(json.dumps(it, sort_keys=True) + '\n')


# ---------------------------------------------------------------- the gameplay gate

PARITY_ARCADE = os.path.join(OUT, 'parity', 'arcade_level1_full')
PARITY_TAPE = 'tools/inputs/level1_full.csv'
PARITY_JOBS = 6                       # parity_play.py --jobs default


def parity_gate(wt, tag, emu_cap):
    """make parity-candidate in worktree wt (candidate flags + PORTGODMODE, the replayed
    level-1 tape, parity_compare against the arcade corpus); returns a copy of REPORT.md
    under build/decomp/logs, or None when the run wrote none."""
    if not os.path.exists(os.path.join(PARITY_ARCADE, 'timeline.csv')):
        log('parity: building the arcade corpus %s' % PARITY_ARCADE)
        sh(['make', 'parity-oracle', 'ARCADE=' + PARITY_ARCADE, 'TAPE=' + PARITY_TAPE])
    logp = os.path.join(OUT, 'logs', '%s.parity.log' % tag)
    os.makedirs(os.path.dirname(logp), exist_ok=True)
    with open(LOCK, 'w') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        while ares_count() + PARITY_JOBS > emu_cap:
            time.sleep(30)
        log('parity %s: running' % tag)
        t0 = time.time()
        with open(logp, 'w') as fh:
            # GFKEY=1: the tape keyed to game frames (decomp/gameframe-key), or the port's
            # slower clock moves the hero off the arcade's route at the first enemy
            subprocess.run(['make', 'parity-candidate', 'NAME=pg', 'ARCADE=' + PARITY_ARCADE,
                            'TAPE=' + PARITY_TAPE, 'GFKEY=1'], cwd=wt, stdout=fh, stderr=subprocess.STDOUT)
        log('parity %s: done in %d min' % (tag, (time.time() - t0) / 60))
    rep = os.path.join(wt, 'build/parity/pg/REPORT.md')
    if not os.path.exists(rep):
        return None
    keep = os.path.join(OUT, 'logs', '%s.REPORT.md' % tag)
    shutil.copyfile(rep, keep)
    return keep


def parity_baseline(game, sha, emu_cap, st):
    pb = st.setdefault('parity_baselines', {}).get(sha)
    if pb and os.path.exists(pb['report']):
        return pb
    wt = os.path.join(OUT, 'wt', 'pbase-%s' % sha[:8])
    make_worktree(wt, 'decomp/pbase-%s' % sha[:8], sha)
    try:
        rep = parity_gate(wt, 'pbase-%s' % sha[:8], emu_cap)
    finally:
        drop_worktree(wt, 'decomp/pbase-%s' % sha[:8])
    if rep is None:
        raise SystemExit('decomp_runner: the parity baseline on %s wrote no REPORT.md' % sha[:8])
    st['parity_baselines'][sha] = pb = {'report': rep, 'fails': decomp_queue.parity_fails(rep)}
    decomp_queue.merge(queue_path(game), game, decomp_queue.parity_items(game, rep))
    save_state(st)
    return pb


def parity_verdict(base, rep, key):
    if rep is None:
        return 'REJECT', 'the parity run wrote no REPORT.md (build failed?)', None
    try:
        new = decomp_queue.parity_fails(rep)
    except SystemExit as e:
        return 'REJECT', 'parity run void: %s' % e, None
    fresh = sorted(set(new) - set(base))
    if fresh:
        return 'REJECT', 'new parity failures: %s' % ' | '.join(new[k] for k in fresh)[:400], new
    if key in new:
        return 'REJECT', 'the item still fails: %s' % new[key], new
    return 'ACCEPT', 'parity failures %d -> %d (item cleared)' % (len(base), len(new)), new


# ---------------------------------------------------------------- the agent

UNATTENDED = """\
You are running UNATTENDED inside tools/decomp_runner.py. No human will read or answer
anything until the run is over. Instructions elsewhere (user CLAUDE.md, memory) that tell
you to ask the user, prompt for input, suggest prompts instead of working, or wait for
approval do not apply here: decide and proceed. If you are truly blocked, end your final
message with a line starting BLOCKED: and the reason.

Hard limits for this session:
- Work only inside the current directory (a git worktree). Never touch another checkout.
- Do NOT run `make check-demo`, tests/demo-b/*.py, ares-headless, mame, or any emulator.
  The runner gates your change after you finish, under a machine-wide emulator lock.
  You MAY run `make port` to prove the port compiles.
- Never git push, reset, rebase, or switch branches. The runner commits.
"""


PARITY_PROMPT = """Fix one gameplay-logic failure in the {game} port.

The gameplay gate (`make parity`, docs/handoff/PARITY-HARNESS.md) replays a recorded level-1
input tape on the arcade and on the port and compares them. Its verdict for the base commit
includes this failure:

    {line}

The full report for the base commit is {report} (read it: the object census table, the
milestones, the divergence section). The queue item, as JSON:

{item}

Kinds: 'kind' = an object handler the arcade runs in that level and slot range and the port
never does (a spawn table, a kind dispatcher, or the spawning object's logic); 'thin' = the
port runs it far less often than the arcade; 'milestone' = an event the arcade reaches and the
port never does; 'hang' = the port's game stopped; 'level' = the port never reached the level.
Combat diverges within seconds of the first enemy (RNG), so the census and milestones are
the evidence, not frame-exact positions.

Method (CLAUDE.md is authoritative; FACTS.md is a grep, not a read): find which 68K routine
spawns or reaches the missing thing (roms/{game}/prog68k.bin via m68k objdump from
docs/audit/func_ranges.txt, the decompile docs/proof/{game}.c, the port's generated
build/port/sh_src/portfull/altbeast_port.c), find where the port's translation differs, and
fix it as sh_src/portfull/hand/FUN_<addr>.c (signature from protos.h); a generator pass only
when one shape breaks several functions. Check the known decompiler classes first: access
width, two-register returns (D0,D1), A-register file gaps, jumps through locals. Run
`make port` to prove it builds. Final message: two lines for the commit, the finding and the
fix, then nothing else.
"""


def item_prompt(game, it, feedback=None, report=None):
    if not feedback and it.get('gate') == 'parity':
        return PARITY_PROMPT.format(game=game, line=it['verdict_line'], report=report,
                                    item=json.dumps(it, indent=1, sort_keys=True))
    if feedback:
        return ('The gate rejected your change: %s\n\nPer-target rows from the gate for the '
                'records this item owns:\n%s\n\nRevise the fix. Same rules as before.' % feedback)
    return """Fix one object-logic divergence in the {game} port.

The gate (make check-demo) diffs every live object record against the arcade's work RAM.
tools/decomp_queue.py traced the earliest divergence to this object handler; the queue
item, as JSON:

{item}

Fields: handler = the 68K routine the ARCADE's record pointed at; function/entry = the
function that owns that address (docs/audit/func_ranges.txt); slots = object slots that
differ; offsets = record byte offsets that differ; first_target = the first (scene, tick)
where it diverges; port_handler_differs = the port's record points at a different handler
(a state transition went wrong); live_mismatch = a record live on one machine only (a
spawn or a kill went wrong). The bug can be in the handler or in anything it calls.

Method (CLAUDE.md is authoritative; FACTS.md is a grep, not a read):
1. Read the 68K for the function: m68k objdump of roms/{game}/prog68k.bin from the
   func_ranges.txt boundary, and the decompiled body in docs/proof/{game}.c. The ghidra
   skill covers headless Ghidra if you need the project.
2. Read what the port compiles for it (build/port/sh_src/portfull/altbeast_port.c is
   generated output; the input is docs/proof/{game}.c through tools/cport_prep).
3. Find the instruction whose port translation computes a different value for the
   differing offsets. Check the known decompiler classes first (FACTS.md, LESSONS.md):
   access width, two-register returns, A-register file gaps, jumps through locals.
4. Fix it as sh_src/portfull/hand/FUN_<addr>.c (signature from protos.h). A generator
   pass only when the same shape breaks several functions.
5. Run `make port` and make sure it builds.
6. Final message: two lines for the commit -- the finding and the fix -- then nothing else.
""".format(game=game, item=json.dumps(it, indent=1, sort_keys=True))


async def run_agent(wt, prompt, args, resume=None):
    from claude_agent_sdk import (query, ClaudeAgentOptions, AssistantMessage, ResultMessage,
                                  TextBlock)
    opts = ClaudeAgentOptions(
        model=args.model, effort=args.effort, cwd=wt,
        system_prompt={'type': 'preset', 'preset': 'claude_code', 'append': UNATTENDED},
        setting_sources=['user', 'project'], skills=['ghidra'],
        permission_mode='dontAsk',
        allowed_tools=['Read', 'Edit', 'Write', 'Glob', 'Grep', 'Bash'],
        disallowed_tools=['WebFetch', 'WebSearch', 'Bash(git push:*)', 'Bash(git reset:*)',
                          'Bash(git rebase:*)', 'Bash(git checkout:*)', 'Bash(git switch:*)',
                          'Bash(make check-demo:*)', 'Bash(mame:*)'],
        max_turns=args.max_turns, max_budget_usd=args.budget, resume=resume,
        env={'ANTHROPIC_API_KEY': api_key()})
    text, res = [], None
    async for m in query(prompt=prompt, options=opts):
        if isinstance(m, AssistantMessage):
            text += [b.text for b in m.content if isinstance(b, TextBlock)]
        elif isinstance(m, ResultMessage):
            res = m
    return (text[-1] if text else ''), res


def feedback_rows(logp, it):
    slots = {s.split(':')[-1] for s in it['slots']}
    rows = []
    for line in open(logp):
        if re.match(r'^[A-Z] [0-9a-f]{4}', line) and any(re.search(r'\b%s:\d' % s, line) for s in slots):
            rows.append(line.rstrip())
    return '\n'.join(rows[:12]) or '(no row for those slots)'


# ---------------------------------------------------------------- one run

def one_run(game, args, st):
    base = st.get('tip') or args.base
    sha, bl = baseline(game, base, args.emu_cap, st)
    pb = parity_baseline(game, sha, args.emu_cap, st) if GATE == 'parity' else None
    items = read_queue(game)
    it = next((i for i in items if i['status'] == 'todo'), None)
    if it is None:
        log('queue empty: nothing to do on %s' % sha[:8])
        return False
    stamp = datetime.datetime.now().strftime('%m%d%H%M')
    branch = 'decomp/%s-%s-%s' % (game, re.sub(r'[^a-z0-9]+', '-', it['handler'].lower().replace('0x', '')).strip('-'), stamp)
    wt = os.path.join(OUT, 'wt', branch.split('/', 1)[1])
    log('item %s %s (rank %s, attempt %d) on %s -> %s' % (
        it['handler'], it['function'], it['rank'], it['attempts'] + 1, sha[:8], branch))
    make_worktree(wt, branch, sha)
    rec = {'when': datetime.datetime.now().isoformat(timespec='seconds'), 'game': game,
           'handler': it['handler'], 'base': sha[:8], 'branch': branch, 'model': args.model,
           'rounds': [], 'cost_usd': 0.0, 'usage': {}}
    v, why, session = 'REJECT', 'no round ran', None
    try:
        prompt = item_prompt(game, it, report=pb['report'] if pb else None)
        for rnd in range(args.rounds):
            if args.dry_run:
                summary, res = 'dry run: no agent', None
            else:
                summary, res = asyncio.run(run_agent(wt, prompt, args, resume=session))
                if res and res.is_error and res.api_error_status in (401, 403):
                    # an expired or revoked key fails every item the same way: stop the loop
                    # instead of marking the queue failed
                    raise SystemExit('decomp_runner: API key rejected (%d); replace %s'
                                     % (res.api_error_status, KEYFILE))
                session = res.session_id if res else session
                rec['cost_usd'] += (res.total_cost_usd or 0) if res else 0
                for k, n in ((res.usage or {}) if res else {}).items():
                    if isinstance(n, (int, float)):
                        rec['usage'][k] = rec['usage'].get(k, 0) + n
            if 'BLOCKED:' in summary:
                v, why = 'BLOCKED', summary.split('BLOCKED:', 1)[1].strip()[:300]
                rec['rounds'].append({'verdict': v, 'why': why})
                break
            if not args.dry_run and not sh(['git', 'status', '--porcelain', '--', '.', NOT_LOG], cwd=wt).stdout.strip() \
                    and not sh(['git', 'log', '--oneline', '%s..HEAD' % sha], cwd=wt).stdout.strip():
                v, why = 'REJECT', 'the agent changed nothing'
                rec['rounds'].append({'verdict': v, 'why': why})
                break
            tag = '%s-r%d' % (branch.split('/', 1)[1], rnd)
            prep = None
            if GATE == 'parity':
                # the gameplay gate first (~20 min); check-demo (~50 min) only for a survivor
                prep = parity_gate(wt, tag, args.emu_cap)
                v, why, pnew = parity_verdict(pb['fails'], prep, it['handler'])
                logp = prep
                if v == 'ACCEPT':
                    logp, work, adv = gate(wt, tag, args.emu_cap)
                    v2, why2 = demo_no_worse(bl['score'], score(game, logp, work))
                    if v2 != 'ACCEPT':
                        v, why = v2, why2
                    elif not adv[0]:
                        v, why = 'REJECT', 'attract tick frozen %s' % (adv[1],)
            else:
                logp, work, adv = gate(wt, tag, args.emu_cap)
                v, why = verdict(bl['score'], score(game, logp, work), it['handler'])
                if v == 'ACCEPT' and not adv[0]:
                    v, why = 'REJECT', 'attract tick frozen %s (the gate passed; the game hangs)' % (adv[1],)
            rec['rounds'].append({'verdict': v, 'why': why, 'log': logp, 'summary': summary[:600],
                                  'turns': res.num_turns if res else 0})
            log('round %d: %s (%s)' % (rnd + 1, v, why))
            if v == 'ACCEPT' or args.dry_run or month_spend() + rec['cost_usd'] > args.monthly_cap:
                break
            if GATE == 'parity':
                prompt = item_prompt(game, it, (why, 'new report: %s' % (prep or '(none written)')))
            else:
                prompt = item_prompt(game, it, (why, feedback_rows(logp, it)))
        if v == 'ACCEPT':
            msg = summary.strip() or 'decomp: %s' % it['handler']
            sh(['git', 'add', '-A', '--', '.', NOT_LOG], cwd=wt)
            sh(['git', 'commit', '-q', '-m', '%s\n\n%s (decomp_runner, %s)\n\nCo-Authored-By: %s <noreply@anthropic.com>'
                % (msg, why, branch, args.model)], cwd=wt)
            st['tip'] = sh(['git', 'rev-parse', 'HEAD'], cwd=wt).stdout.strip()
            # the accepted round's gate IS the new tip's baseline: same tree, so reuse it
            # instead of paying another gate, and re-read the queue from it
            sc = score(game, logp, work)
            st['baselines'][st['tip']] = {'log': logp, 'work': work, 'score': sc}
            if GATE == 'parity':
                st.setdefault('parity_baselines', {})[st['tip']] = {'report': prep, 'fails': pnew}
                it['status'] = 'accepted'
                decomp_queue.merge(queue_path(game), game, decomp_queue.parity_items(game, prep))
            else:
                it['status'] = 'todo' if it['handler'] in sc else 'accepted'   # partial fix: retry on the new tip
                decomp_queue.merge(queue_path(game), game,
                                   decomp_queue.build_items(game, decomp_queue.pairs_check_demo(logp)))
            drop_worktree(wt, None)          # the branch stays for review
        else:
            drop_worktree(wt, branch)
            it['attempts'] += 1
            if it['attempts'] >= args.max_attempts or v == 'BLOCKED':
                it['status'] = 'failed' if v != 'BLOCKED' else 'blocked'
    except BaseException:
        drop_worktree(wt, branch)
        raise
    finally:
        items = [it if i['handler'] == it['handler'] else i for i in read_queue(game)]
        write_queue(game, items)
        save_state(st)
        rec.update(verdict=v, why=why)
        with open(RUNS, 'a') as fh:
            fh.write(json.dumps(rec, sort_keys=True) + '\n')
    log('%s %s: %s, $%.2f' % (v, it['handler'], why, rec['cost_usd']))
    return True


def regate(game, commits, args, st):
    """Cherry-pick earlier accepted commits onto the base one at a time and gate each, no
    agent: a verdict from an older gate is not evidence (advisor thread, 2026-10-25)."""
    base = st.get('tip') or args.base
    sha, bl = baseline(game, base, args.emu_cap, st)
    for c in commits:
        c = sh(['git', 'rev-parse', c]).stdout.strip()
        branch = 'decomp/regate-%s-%s' % (c[:8], datetime.datetime.now().strftime('%m%d%H%M'))
        wt = os.path.join(OUT, 'wt', branch.split('/', 1)[1])
        make_worktree(wt, branch, sha)
        rec = {'when': datetime.datetime.now().isoformat(timespec='seconds'), 'game': game,
               'handler': None, 'regate': c[:8], 'base': sha[:8], 'branch': branch,
               'model': None, 'rounds': [], 'cost_usd': 0.0, 'usage': {}}
        if sh(['git', 'cherry-pick', c], cwd=wt).returncode:
            sh(['git', 'cherry-pick', '--abort'], cwd=wt)
            v, why = 'REJECT', 'does not apply on %s' % sha[:8]
        else:
            logp, work, adv = gate(wt, branch.split('/', 1)[1], args.emu_cap)
            sc = score(game, logp, work)
            v, why = verdict(bl['score'], sc, None)
            if v == 'ACCEPT' and not adv[0]:
                v, why = 'REJECT', 'attract tick frozen %s' % (adv[1],)
            rec['rounds'].append({'verdict': v, 'why': why, 'log': logp})
        if v == 'ACCEPT':
            sha = sh(['git', 'rev-parse', 'HEAD'], cwd=wt).stdout.strip()
            st['tip'] = sha
            st['baselines'][sha] = bl = {'log': logp, 'work': work, 'score': sc}
            drop_worktree(wt, None)
        else:
            drop_worktree(wt, branch)
        rec.update(verdict=v, why=why)
        with open(RUNS, 'a') as fh:
            fh.write(json.dumps(rec, sort_keys=True) + '\n')
        save_state(st)
        log('REGATE %s %s: %s' % (c[:8], v, why))
    os.environ['DEMO_WORK'] = bl['work']
    os.environ['DEMO_PAIRS'] = os.path.join(bl['work'], 'pairs')
    decomp_queue.merge(queue_path(game), game,
                       decomp_queue.build_items(game, decomp_queue.pairs_check_demo(bl['log'])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game', choices=sorted(decomp_queue.GAMES))
    ap.add_argument('--runs', type=int, default=1)
    ap.add_argument('--base', default='advisor-bisect', help='first base commit (later runs chain on accepted work); '
                    'the integration line per the advisor thread, 2026-10-25')
    ap.add_argument('--model', default='claude-sonnet-5-5')
    ap.add_argument('--effort', default='high', choices=['low', 'medium', 'high', 'xhigh', 'max'])
    ap.add_argument('--rounds', type=int, default=2, help='gate verdicts fed back per item')
    ap.add_argument('--max-turns', type=int, default=80)
    ap.add_argument('--budget', type=float, default=5.0, help='USD cap per agent session')
    ap.add_argument('--monthly-cap', type=float, default=190.0, help='stop when runs.jsonl shows this spent this month')
    ap.add_argument('--max-attempts', type=int, default=3)
    ap.add_argument('--emu-cap', type=int, default=os.cpu_count() or 10)
    ap.add_argument('--dry-run', action='store_true', help='worktree + gate, no agent, no spend')
    ap.add_argument('--gate', choices=['demo', 'parity'], default='demo',
                    help="demo = check-demo (attract); parity = make parity level-1 play, check-demo as a guard")
    ap.add_argument('--regate', nargs='+', metavar='COMMIT', help='re-gate these commits on the base, in order, no agent')
    ap.add_argument('--fresh', action='store_true', help='forget the accepted chain and archive the queue (new base)')
    args = ap.parse_args()
    if args.game != 'altbeast':
        raise SystemExit('decomp_runner: %s has no port to gate yet (tests/goldnaxe/oracle.py is the '
                         'arcade side only)' % args.game)
    if not args.dry_run and not args.regate and not api_key():
        raise SystemExit('decomp_runner: no API key: set ANTHROPIC_API_KEY or write it to %s '
                         '(chmod 600), from the org with the API credits' % KEYFILE)
    os.makedirs(OUT, exist_ok=True)
    global GATE
    GATE = args.gate
    st = load_state()
    if args.fresh:
        st.pop('tip', None)
        q = queue_path(args.game)
        if os.path.exists(q):
            os.rename(q, q + '.%s' % datetime.datetime.now().strftime('%Y%m%d%H%M'))
        save_state(st)
    if args.regate:
        return regate(args.game, args.regate, args, st)
    for n in range(args.runs):
        spent = month_spend()
        if spent >= args.monthly_cap:
            log('monthly cap reached: $%.2f of $%.2f' % (spent, args.monthly_cap))
            break
        if not one_run(args.game, args, st):
            break


if __name__ == '__main__':
    main()
