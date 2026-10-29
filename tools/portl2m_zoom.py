#!/usr/bin/env python3
"""Read a PORTZOOM build: the L2 per-frame curve at FULL resolution for 32 frames.

    make line NOSPRBAKE=1 PORTFULL=1 PORTTRACE=1 PORTZOOM=73 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2m_zoom.py rom/z73.32x 20000

READ THE NET, NOT THE SCORE. The per-frame curve re-seeds from the arcade every
frame, so its score tracks how busy the arcade was that frame -- the control. A step
in the SCORE is usually the game doing something; a step in the NET (port minus
control) is the port getting something wrong. Keying the detector on the score fired
at a frame where the control stepped with it and the net never moved.
"""
import os, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
rom = os.path.abspath(sys.argv[1])
n = int(sys.argv[2]) if len(sys.argv) > 2 else 20000
import romsyms
lst = romsyms.lst_for(rom)

def sym(name):
    for l in open(lst):
        f = l.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s not found -- build with PORTFULL=1" % name)

B = open(os.environ.get("L2_BLOB", "docs/proof/l2m/deltas.bin"), "rb").read()
nb = struct.unpack(">I", B[:4])[0]
ctl = []
for k in range(1, nb + 1):
    o = struct.unpack(">I", B[4 + 4*(k-1):8 + 4*(k-1)])[0]
    ctl.append(struct.unpack(">I", B[o:o+4])[0])

a = sym("_port_l2m_out")
with tempfile.TemporaryDirectory() as td:
    d = os.path.join(td, "d.bin")
    subprocess.run([ARES, "--frames", str(n),
                    "--dump", "sdram:0x%X:0x400:%s" % (a - 0x06000000, d), rom],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    D = open(d, "rb").read()
g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]

if g(0) != 0x464D4C32:
    raise SystemExit("no 'FML2' magic (%08X)" % g(0))
base, stepf = g(168), g(167)
if not base:
    raise SystemExit("not a PORTZOOM build (window base 0)")
print("per-frame %d of %d; zoom window f%d..f%d; WORST net at %s"
      % (g(3), g(1), base, base + 31, ("f%d" % stepf) if stepf else "none"))
print("  frame :  port  control    net")
for j in range(32):
    f = base + j
    if f - 1 >= len(ctl):
        break
    v = g(40 + j)
    print("  f%-5d %5d %7d  %+5d%s" % (f, v, ctl[f-1], v - ctl[f-1],
                                       "   <== WORST NET" if f == stepf else ""))
if stepf:
    nsp, nmi = g(164), g(165)
    net = g(169) - (1 << 32) if g(169) >= (1 << 31) else g(169)   # SIGNED
    print("  at f%d: net %+d, control %d" % (stepf, net, g(170)))
    if nsp:
        print("    UNCHANGED-BUT-WRONG (%d%s): %s" % (nsp, "+" if nsp >= 32 else "",
              " ".join("%06X" % g(100+k) for k in range(min(nsp, 32)))))
    if nmi:
        print("    MISSED (%d%s): %s" % (nmi, "+" if nmi >= 32 else "",
              " ".join("%06X" % g(132+k) for k in range(min(nmi, 32)))))
