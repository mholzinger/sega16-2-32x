#!/usr/bin/env python3
"""STATIC-SCENE gates 1-3, headless (docs/design/STATIC-SCENE.md).
    python3 tools/mdstatic_gate.py ROM --frame N [--frame M ...]
                                   [--jitter K] [--input CSV] [--out DIR]

For each frame N (and, with --jitter K, for each start shift k in
0..K-1 applied to every input event) runs ares-headless to N, dumps
the SH-2 SDRAM, the MD VRAM/VSRAM/CRAM and the 32X DRAM/CRAM, and
prints tools/state_frame.py's report for that moment: pens exact or
fallback per set, stale cells, slots referenced vs held, plus the
display-gate census (blanks / held vints) and the MDSTATIC counters.

Gate 2 (boot-order battery) is the --jitter run: on an MDSTATIC build
the four allocator tables must be byte-identical across every k for a
known scene (they are a table), DIAG[36] identical, and the slot map
must hold only scene tiles. On the dynamic build (the control) they
are expected to differ — that difference is the bug being fixed.

Dump byte order: ares-headless writes every block in BUS order
(big-endian words) — the opposite of the .bs1 layout state_frame.py
decodes — so this adapter presents the dumps to state_frame.State in
the layout it expects (see State.from_dumps).
"""
import argparse, os, struct, subprocess, sys, tempfile, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import state_frame as sf

ARES = os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BLOCKS = [('sdram', 'sdram', 0, 0x40000), ('vram', 'VDP VRAM', 0, 0x10000),
          ('vsram', 'VSRAM', 0, 0x50), ('cram', 'VDP CRAM', 0, 0x80),
          ('dram', 'DRAM', 0, 0x40000), ('x32cram', '32X CRAM', 0, 0x200)]


def shifted_input(src, k, dst):
    with open(src) as f, open(dst, 'w') as g:
        for ln in f:
            s = ln.strip()
            if not s or s.startswith('#'):
                g.write(ln)
                continue
            parts = s.split(',')
            parts[0] = str(int(parts[0]) + k)
            g.write(','.join(parts) + '\n')


def run(rom, frame, inp, out):
    cmd = [ARES, '--frames', str(frame), '--input', inp]
    for name, blk, off, ln in BLOCKS:
        cmd += ['--dump', f'{blk}:{off:#x}:{ln:#x}:{os.path.join(out, name + ".bin")}']
    cmd.append(rom)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    if r.returncode:
        sys.exit(f'ares failed at frame {frame}:\n{r.stdout}\n{r.stderr}')
    return {name: open(os.path.join(out, name + '.bin'), 'rb').read() for name, *_ in BLOCKS}


def state_from_dumps(d):
    """Build a state_frame.State without a .bs1: the dumps are already in
    natural (bus) order, which is what State's decoders produce after
    their own un-swapping of the .bs1 layout."""
    st = sf.State.__new__(sf.State)
    st.sdram = d['sdram']
    st.vram = d['vram']
    st.cram = struct.unpack('>64H', d['cram'])
    st.vsram = struct.unpack('>40H', d['vsram'])
    st.x32cram = struct.unpack('>256H', d['x32cram'])
    # DRAM in bus order: pixel x of row y at +0x200 + y*320 + x (no lane swap).
    # State.frame() indexes (x ^ 1) for the .bs1 layout, so swap lanes here once.
    dr = d['dram']
    st.raw = None
    st.dram_bus = dr
    st.shown_half = 0                       # the FS bit is not a memory block
    st.wram = None
    return st


def frame_from_dumps(st, half):
    fb = st.dram_bus[half * 0x20000 + 0x200:][:224 * 320]
    # re-present as the .bs1 lane order State.frame expects
    fb = bytes(fb[i ^ 1] for i in range(224 * 320))
    class R(bytes):
        pass
    st.raw = b'\0' * 0x200 + fb           # State.frame slices raw[dram + half*0x20000 + 0x200:]
    st.dram = 0
    st.shown_half = 0
    return st.frame()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--frame', type=int, action='append', required=True)
    ap.add_argument('--jitter', type=int, default=1)
    ap.add_argument('--input', default=os.path.join(ROOT, 'discover/inputs/play_level1.csv'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--png', action='store_true', help='also write the reconstructed frame (half 0 and 1)')
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp(prefix='mdstatic_gate_')
    os.makedirs(out, exist_ok=True)
    tables = {}
    for k in range(a.jitter):
        inp = a.input
        if k:
            inp = os.path.join(out, f'input_k{k}.csv')
            shifted_input(a.input, k, inp)
        for fr in a.frame:
            fr_k = fr + k
            d = run(a.rom, fr_k, inp, os.path.join(out, f'k{k}_f{fr}'))
            st = state_from_dumps(d)
            m = st.sdram
            diag = struct.unpack_from('>64I', m, sf.DIAG)
            census = struct.unpack_from('>I', m, 0x28F7C)[0]
            mds = struct.unpack_from('>4I', m, 0x28F40)
            print(f'=== {os.path.basename(a.rom)} k={k} frame={fr_k} ===')
            print(f'display gate: blanks={census >> 16} held={census & 0xFFFF} vints | '
                  f'MDSTATIC installs={mds[0]} pin-evict-refused={mds[1]} flushes={mds[2]} pin-free-refused={mds[3]} | '
                  f'pscene_sw={struct.unpack_from(">I", m, 0x28F5C)[0]}')
            st.report()
            tab = m[sf.MDP_LINE_C:sf.MDP_LINE_C + 96] + m[sf.MDP_S_LINE:sf.MDP_S_LINE + 128] + \
                m[sf.MDP_S_MAP:sf.MDP_S_MAP + 1024] + m[sf.MDP_S_USED:sf.MDP_S_USED + 128]
            tables.setdefault(fr, {})[k] = (hashlib.sha1(tab).hexdigest()[:12], diag[36])
            if a.png:
                for half in (0, 1):
                    img = frame_from_dumps(st, half)
                    p = os.path.join(out, f'k{k}_f{fr}_half{half}.png')
                    img.save(p)
                    img.resize((1600, 1200)).save(p[:-4] + '_43.png')
    if a.jitter > 1:
        print('=== boot-order battery: per frame, (k: tables-sha1, DIAG[36]) ===')
        for fr, per in tables.items():
            vals = set(per.values())
            verdict = 'IDENTICAL across k' if len(vals) == 1 else f'{len(vals)} DISTINCT outcomes'
            print(f'frame {fr}: {verdict}  {per}')
    print('dumps in', out)


if __name__ == '__main__':
    main()
