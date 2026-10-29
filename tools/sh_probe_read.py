#!/usr/bin/env python3
"""Read and DECOMPOSE the SH-2 hardware probe (2026-09-27).

    python3 tools/sh_probe_read.py ares=a.bin blastem=b.bin

The probe (sh_src/sh_probe.h) publishes raw FRT ticks to SDRAM 0x0603B000.
Raw ticks are comparable across machines but they are not the interesting
quantity; the per-access costs behind them are, and those come out of three
readings taken so the loop overhead cancels:

    a resident walk   = 1024 iterations, 0 misses
    an all-miss walk  = 1024 iterations, 1024 misses
    => miss cost      = (all_miss - resident) / 1024

Same subtraction gives the cart's miss cost against SDRAM's. Ticks are
converted with the FRT prescale the probe reports rather than an assumed one:
TCR=1 is phi/32, so one tick is 32 SH-2 cycles (m_main.c:2722).
"""
import struct
import sys

FIELDS = ["magic", "ccr", "tcr", "iters", "hot2k", "cold2k", "res2k",
          "miss16k", "stride16", "stride4", "loads2k", "stores2k",
          "sdram_miss", "cart_miss", "tail", "stride4_raw"]
PRESCALE = {0: 8, 1: 32, 2: 128, 3: 0}     # TCR[1:0]; 3 = external clock


def load(path):
    b = open(path, "rb").read()
    v = [struct.unpack(">I", b[i * 4:i * 4 + 4])[0] for i in range(16)]
    d = dict(zip(FIELDS, v))
    if d["magic"] != 0x53485032:
        sys.exit(f"sh_probe_read: {path} has no SHP2 magic -- the probe did "
                 f"not run. Check TESTCART=1 reached SHCCFLAGS and that COMM4 "
                 f"came back 0x5044.")
    if d["tail"] != 0xC0FFEE00:
        sys.exit(f"sh_probe_read: {path} tail marker missing -- the probe "
                 f"started but did not finish.")
    return d


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: sh_probe_read.py name=dump.bin [name=dump.bin ...]")
    ms = []
    for a in argv[1:]:
        n, p = a.split("=", 1)
        ms.append((n, load(p)))

    w = max(10, max(len(n) for n, _ in ms))
    def row(label, fn, unit=""):
        cells = []
        for _, d in ms:
            try:
                cells.append(f"{fn(d):>{w}}")
            except ZeroDivisionError:
                cells.append(f"{'--':>{w}}")
        print(f"  {label:32}" + " ".join(cells) + (f"  {unit}" if unit else ""))

    print(f"  {'':32}" + " ".join(f"{n:>{w}}" for n, _ in ms))
    print("-- configuration")
    row("CCR", lambda d: f"{d['ccr']:#04x}")
    row("FRT prescale (phi/N)", lambda d: PRESCALE.get(d["tcr"] & 3, "?"))
    row("iterations per walk", lambda d: d["iters"])
    print("-- raw ticks, 1024 steps")
    row("2 KB resident (all hits)", lambda d: d["res2k"])
    row("16 KB walk (all misses)", lambda d: d["miss16k"])
    row("cart walk (all misses)", lambda d: d["cart_miss"])
    print("-- cache clear, proved by behaviour")
    row("hot", lambda d: d["hot2k"])
    row("after CCR purge", lambda d: d["cold2k"])
    row("purge cost (must be > 0)", lambda d: d["cold2k"] - d["hot2k"])
    print("-- derived, SH-2 cycles per access (loop overhead cancelled)")
    cyc = lambda d, t: round(t * PRESCALE.get(d["tcr"] & 3, 32) / d["iters"], 1)
    row("resident access + loop", lambda d: cyc(d, d["res2k"]))
    row("SDRAM miss, extra over a hit",
        lambda d: cyc(d, d["sdram_miss"] - d["res2k"]))
    row("CART miss, extra over a hit",
        lambda d: cyc(d, d["cart_miss"] - d["res2k"]))
    row("cart / SDRAM miss ratio",
        lambda d: round((d["cart_miss"] - d["res2k"])
                        / (d["sdram_miss"] - d["res2k"]), 2))
    print("-- write-through: a store can never be cached away")
    row("2 KB resident loads", lambda d: d["loads2k"])
    row("2 KB stores, same window", lambda d: d["stores2k"])
    row("store premium", lambda d: f"{round(100.0 * (d['stores2k'] - d['loads2k']) / d['loads2k'])}%")
    print("-- line fill: same 16 KB covered two ways")
    row("stride 16, 1024 steps", lambda d: d["stride16"])
    row("stride 4, 4096 steps", lambda d: d["stride4_raw"])
    row("extra cycles per cached step",
        lambda d: round((d["stride4_raw"] - d["stride16"])
                        * PRESCALE.get(d["tcr"] & 3, 32) / 3072.0, 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
