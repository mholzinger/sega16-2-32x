#!/usr/bin/env python3
"""Read a .bss PROBE COUNTER BLOCK out of a running rom, by SYMBOL NAME.

    tools/probe_read.py rom/s16.32x mdspr_why --n 10
    tools/probe_read.py rom/s16.32x mt_drain_ticks --frames 2000,2600
    tools/probe_read.py rom/s16.32x sl_ans --n 8 --input discover/inputs/play2.csv

WHY THIS EXISTS. Probe counters in this tree have historically lived at
hand-chosen fixed SDRAM addresses, and the SDRAM map has sixteen numbered
collisions to show for it (`tools/sdram_map.py`). Counters placed in
.bss instead cannot collide: the linker owns the address, and the symbol
is in `rom/s16.lst`. That pattern was used four times on 2026-09-10
(`mdspr_why`, `mdspr_nokey_set`, `mt_*`, `sl_ans`) and worked every time
-- but each read meant grepping the listing, subtracting 0x06000000 by
hand, and composing an ares dump. This does that.

THE PATTERN, for a new probe:

    #ifdef MY_PROBE
    /* volatile: nothing READS these, so -O2 -flto dead-store-eliminates
     * them otherwise -- 8 of 10 counters vanished that way (LOOP29 119). */
    static volatile uint32_t my_counts[8];
    #endif

then `tools/probe_read.py rom/s16.32x my_counts --n 8`.

Two rules that block the traps this tree has already hit:
  - **volatile, always.** A never-read static array is dead-store
    eliminated and reads back zero.
  - **Reconcile totals.** If the counters partition something, check
    they sum to it. A control-flow probe that changes control flow
    (counters inserted between `if (c)` and `continue;` without braces)
    reads plausibly and is measuring itself.
"""
import argparse, os, re, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARES = os.environ.get("ARES", os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless"))
SDRAM_BASE = 0x06000000


def resolve(sym, lst):
    """symbol -> SDRAM offset. The listing writes C symbols with a
    leading underscore; accept the name with or without it."""
    pat = re.compile(r'^([0-9a-fA-F]+)\s+\S+\s+_?%s$' % re.escape(sym), re.M)
    m = pat.search(open(lst, errors="replace").read())
    if not m:
        raise SystemExit(
            "symbol %r not found in %s.\n"
            "  Is the probe flag on for this build? Is the array `static`\n"
            "  (a plain local is not in .bss)?" % (sym, lst))
    addr = int(m.group(1), 16)
    if not (SDRAM_BASE <= addr < SDRAM_BASE + 0x40000):
        raise SystemExit("%s is at %08X, outside 32X SDRAM -- not a .bss "
                         "counter block." % (sym, addr))
    return addr - SDRAM_BASE, addr


def dump(rom, frame, off, nbytes, inp, out):
    subprocess.run([ARES, "--frames", str(frame), "--input", inp,
                    "--dump", "sdram:%#x:%#x:%s" % (off, nbytes, out), rom],
                   capture_output=True)
    return open(out, "rb").read() if os.path.exists(out) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("symbol")
    ap.add_argument("--n", type=int, default=8, help="u32 slots to read")
    ap.add_argument("--frames", default="2000,2600", help="A,B (delta) or a single frame")
    ap.add_argument("--input", default=None)
    ap.add_argument("--lst", default=None)
    a = ap.parse_args()

    lst = a.lst or os.path.splitext(a.rom)[0] + ".lst"
    if not os.path.exists(lst):
        lst = os.path.join(ROOT, "rom", "s16.lst")
    inp = a.input or os.path.join(ROOT, "discover/inputs/play_level1.csv")
    off, addr = resolve(a.symbol, lst)
    nbytes = a.n * 4
    frames = [int(x) for x in a.frames.split(",") if x]
    td = tempfile.mkdtemp()

    print("%s at %08X (sdram +%#x), %d slots, %s"
          % (a.symbol, addr, off, a.n, os.path.basename(a.rom)))
    if len(frames) == 1:
        d = dump(a.rom, frames[0], off, nbytes, inp, td + "/a.bin")
        if not d:
            raise SystemExit("ares produced no dump")
        v = struct.unpack(">%dI" % a.n, d[:nbytes])
        for i, x in enumerate(v):
            print("  [%2d] %12d  0x%08X" % (i, x, x))
        return 0

    lo, hi = frames[0], frames[1]
    da = dump(a.rom, lo, off, nbytes, inp, td + "/a.bin")
    db = dump(a.rom, hi, off, nbytes, inp, td + "/b.bin")
    if not da or not db:
        raise SystemExit("ares produced no dump")
    va = struct.unpack(">%dI" % a.n, da[:nbytes])
    vb = struct.unpack(">%dI" % a.n, db[:nbytes])
    print("  frames %d..%d (%d vints)\n" % (lo, hi, hi - lo))
    tot = 0
    for i, (x, y) in enumerate(zip(va, vb)):
        d = (y - x) & 0xFFFFFFFF
        tot += d
        print("  [%2d] %12d   (%d -> %d)" % (i, d, x, y))
    print("\n  sum of deltas %d" % tot)
    print("  RECONCILE: if these partition something, check they sum to it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
