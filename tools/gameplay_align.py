#!/usr/bin/env python3
"""Screenshot a rom at the vint where the game's own scene timer
(WRAM 0xFFF02A, one tick per GAME frame) first reaches T, plus the
neighbours — so two roms running at different speeds can be compared
at the SAME game frame.

    tools/gameplay_align.py rom.32x T [--out DIR] [--tag NAME] [--lo 600 --hi 6000]

Prints the vint found; writes <tag>_g<T>_v<N+k>.png for k in -1..2.
"""
import argparse, os, struct, subprocess, sys
ARES = os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def timer_at(rom, frames, inp, tmp):
    subprocess.run([ARES, "--frames", str(frames), "--input", inp,
                    "--dump", f"wram:0xFFF02A:2:{tmp}", rom],
                   check=True, capture_output=True)
    return struct.unpack(">H", open(tmp, "rb").read())[0]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("T", type=int)
    ap.add_argument("--out", default="."); ap.add_argument("--tag", default="rom")
    ap.add_argument("--lo", type=int, default=600); ap.add_argument("--hi", type=int, default=6000)
    ap.add_argument("--input", default=os.path.join(ROOT, "discover/inputs/play_level1.csv"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tmp = os.path.join(a.out, f"{a.tag}_t.bin")
    lo, hi = a.lo, a.hi                # invariant: timer(lo) < T <= timer(hi)
    if timer_at(a.rom, hi, a.input, tmp) < a.T:
        sys.exit("T not reached by --hi")
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if timer_at(a.rom, mid, a.input, tmp) >= a.T: hi = mid
        else: lo = mid
    n = hi
    cmd = [ARES, "--frames", str(n + 3), "--input", a.input]
    for k in range(-1, 3):
        cmd += ["--screenshot", f"{n+k}:{os.path.join(a.out, f'{a.tag}_g{a.T}_k{k+1}.png')}"]
    subprocess.run(cmd + [a.rom], check=True, capture_output=True)
    print(f"{a.tag}: game frame {a.T} first seen at vint {n}")

if __name__ == "__main__":
    main()
