#!/usr/bin/env python3
"""Read the PORTTEST self-test's result array out of a rom, in ares.

    python3 tools/porttest_read.py rom/s16.32x 300 400

Resolves _port_test_out from rom/s16.lst rather than hardcoding an address:
the array MOVED across a relink once and an address noted in a handoff was
silently wrong for a day.

Slot map is sh_src/port_selftest.c (generated). [11]/[12]/[13] are the FRT
timing slots: ticks are the free-run timer at TCR=1 (/8), so one tick is
8 SH-2 clocks = 0.3477 us at 23.01 MHz.

ares charges INSTRUCTION CYCLES ONLY -- no data cache, no SDRAM waits
(LESSONS: "ares charges instructions only"). So the ares tick count is the
floor; the FPGA's excess over it is the memory-traffic cost, which is the
whole question in SILICON.md 6b. Compare, never read one alone.
"""
import os, re, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
# SH7604 FRT TCR bits 1..0 = CKS: 00 = phi/8, 01 = phi/32, 10 = phi/128.
# m_main writes TCR = 1, so CKS = 01 and a tick is 32 SH-2 clocks -- NOT 8.
# Cross-checked against the RIGBLIT conversion already in m_main.c (FRT/46
# ticks per scanline: 63.56us / 46 = 1.382us/tick, which is 32 clocks).
TICK_US = 32.0 / 23.01                     # 1.3907 us
VINT_US = 1000000.0 / 59.92                # NTSC vint

def sym(lst, name):
    for line in open(lst):
        f = line.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s: %s not found -- rebuild with PORTTEST=1" % (lst, name))

rom = os.path.abspath(sys.argv[1])
frames = [int(x) for x in sys.argv[2:]] or [400]
import romsyms
addr = sym(romsyms.lst_for(rom), "_port_test_out")
if not 0x06000000 <= addr < 0x06040000:
    raise SystemExit("_port_test_out at 0x%08X is not in SDRAM" % addr)
off = addr - 0x06000000

print("_port_test_out = 0x%08X (sdram +0x%X)" % (addr, off))
print("frame | magic    run pass vints mirr b5 wdog | passE ticks worst@case |   us/sweep     %vint")
for n in frames:
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "d.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", "sdram:0x%X:0x200:%s" % (off, d), rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        D = open(d, "rb").read()
    g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]
    magic = g(0)
    ncall = g(1) or 1
    lo = g(11) * TICK_US                   # every delta truncated toward zero
    hi = (g(11) + ncall) * TICK_US         # ...by at most one tick each
    print("%5d | %08X %4d %4d %5d %4d %2d %4d | %5d %5d %5d@%-3d | %6.0f-%-6.0f %4.1f-%.1f%%"
          % (n, magic, g(1), g(2), g(3), g(4), g(5), g(6),
             g(10), g(11), g(12), g(13), lo, hi,
             100.0 * lo / VINT_US, 100.0 * hi / VINT_US)
          + ("" if magic == 0x50525433 else "   <-- NO PRT3 MAGIC: sweep never ran"))
