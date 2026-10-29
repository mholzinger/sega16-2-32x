#!/usr/bin/env python3
"""Read port memory by NAME, never by a hand-computed SDRAM offset.

    python3 tools/port_peek.py rom/ring2.32x 3500 _port_state_ring:0x80 0xFFF0C0:16 SYM+8:4

Each spec is  WHAT[:LEN]  where WHAT is a .lst symbol (optionally +offset), a 68K
work-RAM address 0xFFC000-0xFFFFFF (mapped through the port's WRAM_BASE), or a raw
SDRAM address 0x06xxxxxx. Prints hex. One ares run, all specs dumped from its END
state (ares --dump reflects the end whatever frame is named -- LESSONS).

WHY. 2026-10-04: the i8751 mailbox was read at sdram offset 0x300C0 instead of
WRAM_BASE + 0x30C0 = 0x2FCC0 -- one hand-computed offset, outside work RAM -- and the
0xFF it returned produced a false "the mailbox is 0xFF from boot". The real bytes
were 0x00 and the real cause (an unconsumed sound command) was a different bug.
The record held the right answer; the arithmetic was the invention.
"""
import os, re, subprocess, sys, tempfile
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
rom = sys.argv[1]; frames = int(sys.argv[2]); specs = sys.argv[3:]
lst = os.path.splitext(rom)[0] + ".lst"
syms = {}
for l in open(lst):
    f = l.split()
    if len(f) == 3: syms[f[2]] = int(f[0], 16)
WRAM = syms.get("_WRAM_BASE") or syms.get("WRAM_BASE") or 0x0602CC00
def resolve(w):
    m = re.match(r'^([A-Za-z_]\w*)(?:\+(0x[0-9a-fA-F]+|\d+))?$', w)
    if m:
        if m.group(1) not in syms: raise SystemExit("%s: not in %s" % (m.group(1), lst))
        return syms[m.group(1)] + (int(m.group(2), 0) if m.group(2) else 0)
    v = int(w, 0)
    if 0xFFC000 <= v <= 0xFFFFFF: return WRAM + (v - 0xFFC000)
    if 0x06000000 <= v < 0x06040000: return v
    if 0x26000000 <= v < 0x26040000: return v & 0x0FFFFFFF | 0x06000000
    raise SystemExit("%s: not a symbol, a 68K work-RAM address, or an SDRAM address" % w)
with tempfile.TemporaryDirectory() as td:
    args = [ARES, "--frames", str(frames)]; outs = []
    for i, sp in enumerate(specs):
        what, _, ln = sp.partition(":"); ln = int(ln, 0) if ln else 16
        a = resolve(what); f = os.path.join(td, "d%d" % i); outs.append((sp, a, f))
        args += ["--dump", "sdram:0x%X:0x%X:%s" % (a - 0x06000000, ln, f)]
    subprocess.run(args + [rom], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    for sp, a, f in outs:
        d = open(f, "rb").read()
        print("%-28s @%08X  %s" % (sp, a, " ".join("%02x" % b for b in d)))
