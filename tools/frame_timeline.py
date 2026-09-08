#!/usr/bin/env python3
"""68K FRAME TIMELINE on ares-headless (DISCOVERY-TIMING question 4).
    python3 tools/frame_timeline.py ROM [--a 2000] [--b 2012] [--input CSV] [--out DIR]
Uses the fork's `--trace-access lo:hi:name:fa:fb` (2026-09-07 late): every
68K read/write in a range during frames [fa,fb) stamped with the VDP beam.
Prints, per vint, the LINE (0 = vblank start, V=0xE0) of:
  raise   the shim's FM raise (68K write of 0xA15100 with bit 15 set)
  drop    FM seen low again by a 68K read (the SH-2 drops it at its ack)
  irq4    the game's IRQ4 entry (fetch of 0x902AAC)
  miss    the game's own frame-miss count (write of 0xFFF144)
  gate    first FM read from a gate thunk (pc 0xFFBxxx) while FM=1, and
          the thunk's pc -> which game site is spinning; spin length in lines
  idle    first frame-flag read from the game's idle loop (0x903982)
  loop    fetch of the level loop top (0x90097C)
"""
import argparse, csv, os, subprocess, sys, tempfile
ARES = os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RANGES = [('fm', 0xA15100, 0xA15101), ('comm0', 0xA15120, 0xA15121),
          ('flag', 0xFFF01C, 0xFFF01D), ('miss', 0xFFF144, 0xFFF145),
          ('irq4', 0x902AAC, 0x902AAD), ('loop', 0x90097C, 0x90097D)]
GATE_PC_LO, GATE_PC_HI = 0xFFB800, 0xFFBFF0     # tile/pal/fm thunk block
LINES = 262


def line_of(v):
    """NTSC vcounter -> lines since vblank start (V=0xE0)."""
    if 0xE0 <= v <= 0xEA:
        return v - 0xE0
    if v >= 0x1E5:
        return v - 0x1E5 + 11
    return v + 38


def thunk_map():
    """tst-read pc -> FMGATE entry index, decoded from md_src/fmgate_tab.h
    (the pc ares reports for the tst.w operand read is thunk+8)."""
    import re
    src = open(os.path.join(ROOT, 'md_src', 'fmgate_tab.h')).read()
    base = int(re.search(r'FMGATE_THUNK_ADDR 0x([0-9A-F]+)', src).group(1), 16)
    body = src.split('fmgate_thunks[] = {')[1].split('};')[0]
    words = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4})', body)]
    m = {}; i = 0; n = 0
    while i < len(words):
        if words[i:i + 4] == [0x4A79, 0x00A1, 0x5100, 0x6BF8]:
            m[0xFF0000 + base + 2 * i + 8] = n
            j = i + 4
            while words[j] != 0x4E75:
                j += 1
            n += 1; i = j + 1
        else:
            i += 1
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--a', type=int, default=2000)
    ap.add_argument('--b', type=int, default=2012)
    ap.add_argument('--input', default=os.path.join(ROOT, 'discover/inputs/play_level1.csv'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--csv', default=None, help='reuse an existing trace csv (skip the run)')
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp(prefix='timeline_')
    os.makedirs(out, exist_ok=True)
    csvp = a.csv or os.path.join(out, 'trace_access.csv')
    if not a.csv:
        cmd = [ARES, '--frames', str(a.b + 1), '--input', a.input, '--trace-access-out', csvp,
               '--trace-comm', os.path.join(out, 'trace_comm.csv')]
        for name, lo, hi in RANGES:
            cmd += ['--trace-access', f'{lo:#x}:{hi:#x}:{name}:{a.a}:{a.b}']
        cmd.append(a.rom)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        if r.returncode:
            sys.exit(f'ares failed:\n{r.stdout}\n{r.stderr}')
    ev = list(csv.DictReader(open(csvp)))
    for e in ev:
        e['f'] = int(e['frame']); e['vv'] = int(e['v'], 16); e['pcv'] = int(e['pc'], 16)
        e['d'] = int(e['data'], 16); e['L'] = line_of(e['vv'])
    frames = sorted({e['f'] for e in ev})
    tmap = thunk_map()
    # SH-2 window from the COMM trace: 68K posts COMM0=0x2020, master acks COMM0=0
    win = {}
    commp = os.path.join(os.path.dirname(csvp), 'trace_comm.csv')
    if os.path.exists(commp):
        for c in csv.DictReader(open(commp)):
            if 'v' not in c or c['comm'] != '0':
                continue
            f = int(c['frame']); L = line_of(int(c['v'], 16)); val = int(c['value'], 16)
            w = win.setdefault(f, [None, None])
            if c['source'] == 'm68k' and val == 0x2020 and w[0] is None:
                w[0] = L
            if c['source'] == 'shm' and val == 0 and w[0] is not None and w[1] is None:
                w[1] = L
    print(f"{os.path.basename(a.rom)}: frames {a.a}-{a.b}, {len(ev)} events; lines from vblank start (V=0xE0), 262/frame")
    print("frame  raise  drop  irq4  miss  gate(line..line = spin) thunk-pc    idle  idle-reads  loop  fm-reads-hi/total  post..ack")
    # sequence-aware pass: walk events in order, FM state from data bit 15 of any fm access
    for f in frames:
        es = [e for e in ev if e['f'] == f]
        raise_l = drop_l = irq4_l = miss_l = idle_l = loop_l = None
        gate_l = gate_end = None; gate_pc = None
        fm_hi = 0; fm_tot = 0; idle_reads = 0
        fm_state = None
        gates = {}                      # thunk# -> [first line, reads]
        for e in es:
            rn = e['range']; L = e['L']
            if rn == 'fm':
                fm_tot += 1
                hi = bool(e['d'] & 0x8000)
                if e['rw'] == 'W' and hi and raise_l is None:
                    raise_l = L
                if e['rw'] == 'R' and e['pcv'] in tmap:
                    g = gates.setdefault(tmap[e['pcv']], [L, 0]); g[1] += 1
                if e['rw'] == 'R':
                    if hi:
                        fm_hi += 1
                        if GATE_PC_LO <= e['pcv'] <= GATE_PC_HI and gate_l is None:
                            gate_l = L; gate_pc = e['pcv']
                    else:
                        if fm_state and drop_l is None and (raise_l is not None):
                            drop_l = L
                        if gate_l is not None and gate_end is None and GATE_PC_LO <= e['pcv'] <= GATE_PC_HI:
                            gate_end = L
                fm_state = hi
            elif rn == 'irq4' and irq4_l is None:
                irq4_l = L
            elif rn == 'miss' and e['rw'] == 'W' and miss_l is None:
                miss_l = L
            elif rn == 'flag' and e['rw'] == 'R' and 0x903980 <= (e['pcv'] & 0xFFFFFF) <= 0x90398A:
                idle_reads += 1
                if idle_l is None:
                    idle_l = L
            elif rn == 'loop' and loop_l is None:
                loop_l = L
        def s(x):
            return '   -' if x is None else f'{x:4d}'
        spin = '' if gate_l is None else (f'{gate_l:3d}..{s(gate_end)} = {(gate_end - gate_l) if gate_end is not None else -1:3d}')
        print(f"{f:5d}  {s(raise_l)}  {s(drop_l)}  {s(irq4_l)}  {s(miss_l)}  {spin:>18s} "
              f"{'' if gate_pc is None else f'{gate_pc:#08x}':>10s}  {s(idle_l)}  {idle_reads:9d}  {s(loop_l)}  {fm_hi}/{fm_tot}"
              + (f"  {s(win[f][0])}..{s(win[f][1])}" if f in win else ''))
        seq = sorted(gates.items(), key=lambda kv: kv[1][0])
        print("        gate entries: " + ' '.join(f"#{k}@{v[0]}" + (f"x{v[1]}" if v[1] > 1 else '') for k, v in seq))
    print(f"trace csv: {csvp}")


if __name__ == '__main__':
    main()
