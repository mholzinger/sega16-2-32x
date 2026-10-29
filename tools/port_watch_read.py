#!/usr/bin/env python3
"""Read the port's software watchpoint and floor_collide entry sampler.

    python3 tools/port_watch_read.py rom/fc6.32x 1200      # needs PORTTRACE=1

The watchpoint brackets every function in the port unit with
-finstrument-functions, saves the watched byte on entry and compares on exit.
Callees exit first, so the FIRST span to report a change is the innermost writer
and the sequence after it is the outer frames plus any LATER writer. Reading only
the first was misleading once already (FULL-PORT.md 22).
"""
import os, struct, subprocess, sys, tempfile, bisect

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
rom = os.path.abspath(sys.argv[1])
n = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
lst = os.path.splitext(rom)[0] + ".lst"
syms = []
for l in open(lst):
    f = l.split()
    if len(f) == 3 and f[1] in 'tT':
        syms.append((int(f[0], 16), f[2]))
syms.sort()
ad = [a for a, _ in syms]
def nm(pc):
    i = bisect.bisect_right(ad, pc) - 1
    return syms[i][1].lstrip('_') if i >= 0 else "?"
def sym(name):
    for l in open(lst):
        f = l.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s not found -- build with PORTFULL=1 PORTTRACE=1" % name)

a = sym("_port_watch_out")
with tempfile.TemporaryDirectory() as td:
    d = os.path.join(td, "d.bin")
    subprocess.run([ARES, "--frames", str(n),
                    "--dump", "sdram:0x%X:0x400:%s" % (a - 0x06000000, d), rom],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    D = open(d, "rb").read()
g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]

if g(0) != 0x57415443:
    raise SystemExit("no 'WATC' marker (%08X): needs PORTTRACE=1 and the chain must have run" % g(0))
if g(17):
    print("*** watch-stack overflowed %d times -- attributions are SKEWED ***" % g(17))

print("WATCHED CELLS -- innermost writer, and how many spans saw a change")
for k in range(4):
    print("  %06X : %-28s 0x%02X -> 0x%02X   spans %d"
          % (g(1+k), nm(g(20+k*3)), g(21+k*3), g(22+k*3), g(128+k)))
if g(19) == 0x43454C4C:
    v = g(18)
    print("  after the frame: FFC82C=0x%02X FFC800=0x%02X FFC812=0x%02X FFC81B=0x%02X"
          % ((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
if g(166):
    print("FULL CHANGE SEQUENCE for %06X (innermost first, outer frames repeat it):" % g(4))
    for h in range(min(g(166), 10)):
        print("  %2d  %-30s 0x%02X -> 0x%02X" % (h, nm(g(136+h*3)), g(137+h*3), g(138+h*3)))

if g(16) == 0x464C4F52:
    print("floor_collide: %d entries this frame; table f142=%d ptr=0x%08X entry=0x%04X 0x%04X 0x%04X"
          % (g(64), g(5), g(6), g(7), g(8), g(9)))
    for s0 in range(2):
        q = [g(66 + s0*14 + k) for k in range(14)]
        if q[13] != 0x464E5452:
            continue
        print("  call %d  A6=0x%08X (work RAM +0x%04X = slot %d)"
              % (s0, q[0], q[0] - 0x0602CC00, (q[0] - 0x0602CC00) // 0x80))
        print("    +0x1A=0x%04X +0x5A=0x%04X +0x68=0x%04X +0x54=0x%04X +0x56=0x%04X +0x10=0x%04X +0x2C=0x%02X"
              % (q[1], q[2], q[3], q[4], q[5], q[6], q[7]))
        e0, e1, e2 = q[10], q[11], q[12]
        ok = (q[1] & 0x8000) == 0 and e0 <= q[2] and q[3] <= e0 and q[4] <= e2 and e1 <= q[5]
        print("    guard+match: %s" % ("ALL PASS -- the branch runs" if ok else "fails"))
