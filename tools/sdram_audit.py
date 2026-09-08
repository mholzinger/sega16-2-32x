#!/usr/bin/env python3
"""SDRAM fixed-map audit by DUMP-DIFF (HANDOFF-SESSION3 queue #1).

The fixed-SDRAM map comments carry their era (docs/design/BOSSFIGHT.md collisions
#13/#14). Before ANY new fixed placement, audit the LIVE build:

    tools/sdram_audit.py run  rom/s16_boss.32x out/      # 7 headless runs
    tools/sdram_audit.py spans out/ [--gap 32]           # live/free spans

`run` executes ares-headless for the battery script (play2.csv) at
frames 1/300/900/1900 and attract (no input) at 1500/3000/5400, each
dumping sdram 0x19000-0x40000 at end of run. `spans` marks a byte LIVE
if it is nonzero in any dump or differs between any two, merges live
bytes across gaps < --gap, and annotates every span and every free gap
with the fixed addresses (0x[02]60xxxxx literals) declared ANYWHERE in
sh_src/ — every #ifdef branch, because the grep that misses the
inactive-looking branch is how #13 (FBCLEAR under BLIT_SKIP) happened.

A gap is FREE only if it is quiet in every dump AND no declaration
falls inside it AND it is not inside a declared array's extent (the
tool cannot know extents — read the decl lines it prints).

Caveat: end-of-run dumps see residue, not transients. A tenant that
writes and later zeroes its own bytes is invisible; nothing in this
map does that, but a new suspect should also get a mid-run dump.
"""
import glob, os, re, subprocess, sys

ARES = "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE, LEN = 0x19000, 0x27000
RUNS = [("p1", 1, "play2.csv"), ("p300", 300, "play2.csv"),
        ("p900", 900, "play2.csv"), ("p1900", 1900, "play2.csv"),
        ("a1500", 1500, None), ("a3000", 3000, None), ("a5400", 5400, None)]


def run(rom, out):
    os.makedirs(out, exist_ok=True)
    procs = []
    for name, frames, inp in RUNS:
        cmd = [ARES, "--frames", str(frames)]
        if inp:
            cmd += ["--input", f"{REPO}/discover/inputs/{inp}"]
        cmd += ["--dump", f"sdram:{BASE:#x}:{LEN:#x}:{out}/{name}.sdram",
                "--dump", f"wram:0xFFA000:0x2000:{out}/{name}.wram", rom]
        procs.append((name, subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                             stderr=subprocess.DEVNULL)))
    for name, p in procs:
        print(name, "exit", p.wait())


def declared():
    decl = []
    for f in glob.glob(f"{REPO}/sh_src/*.[ch]"):
        for n, line in enumerate(open(f), 1):
            for m in re.finditer(r"0x[02]60[1-3][0-9A-Fa-f]{4}", line):
                decl.append((int(m.group(0), 16) & 0x3FFFF, m.group(0),
                             f"{os.path.basename(f)}:{n}"))
    return sorted(decl)


def spans(out, gap):
    files = sorted(glob.glob(f"{out}/*.sdram"))
    dumps = {os.path.basename(f)[:-6]: open(f, "rb").read() for f in files}
    names = list(dumps)
    first = dumps[names[0]]
    n = len(first)
    live = bytearray(n)
    for b in dumps.values():
        for i in range(n):
            if b[i] or b[i] != first[i]:
                live[i] = 1
    decl = declared()
    sp = []
    i = 0
    while i < n:
        if not live[i]:
            i += 1
            continue
        k = i
        while True:
            while k < n and live[k]:
                k += 1
            g = k
            while g < n and g - k < gap and not live[g]:
                g += 1
            if g < n and live[g] and g - k < gap:
                k = g
            else:
                break
        sp.append((i, k))
        i = k
    print(f"dumps {names}  region {BASE:#x}-{BASE+n:#x}  gap={gap}")
    for s, e in sp:
        who = [k for k, b in dumps.items() if any(b[x] for x in range(s, e))]
        syms = " ".join(hx for a, hx, _ in decl if s <= a - BASE < e)
        print(f"{BASE+s:#07x}-{BASE+e:#07x} {e-s:6d}B seen:{','.join(who)} {syms}")
    print(f"live {sum(e-s for s, e in sp)}B of {n}B; quiet gaps >= 64B:")
    prev = 0
    for s, e in sp + [(n, n)]:
        if s - prev >= 64:
            d = " ".join(f"{hx}@{w}" for a, hx, w in decl if prev <= a - BASE < s)
            print(f"  QUIET {BASE+prev:#07x}-{BASE+s:#07x} {s-prev:6d}B  decl: {d or '(none)'}")
        prev = e


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "run":
        run(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 3 and sys.argv[1] == "spans":
        g = int(sys.argv[sys.argv.index("--gap") + 1]) if "--gap" in sys.argv else 32
        spans(sys.argv[2], g)
    else:
        print(__doc__)
        sys.exit(2)
