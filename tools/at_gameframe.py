#!/usr/bin/env python3
"""Find the emulator frame where a rom reaches a given GAME frame.

    tools/at_gameframe.py rom.32x <scene-timer value> [--input CSV]

WHY THIS EXISTS. Comparing two roms at the same EMULATOR frame compares
different game states whenever they differ in speed or phase, and then
every pixel number is motion, not rendering. That trap cost four separate
wrong readings on 2026-09-17 alone -- including a 'HUD dropout' that was
escalated as a defect and was two builds photographed one frame apart.

The 68K scene timer (WRAM 0xFFF02A) ticks once per GAME frame, so it is
the anchor. Bisect the emulator frame for a target timer value, then
capture both roms at THEIR OWN frame for the same value.

Note the timer is per-scene and resets (LOOP28 87), so it is monotonic
only inside one scene -- bisection is valid only within a scene.
"""
import argparse, os, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless"))

def timer_at(rom, frame, inp):
    with tempfile.TemporaryDirectory() as td:
        f = os.path.join(td, "t.bin")
        cmd = [ARES, "--frames", str(frame), "--dump", f"wram:0xFFF02A:2:{f}"]
        if inp: cmd += ["--input", inp]
        cmd.append(rom)
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return struct.unpack(">H", open(f, "rb").read())[0]

def frame_for(rom, target, inp, lo=60, hi=6000):
    """smallest emulator frame whose scene timer >= target"""
    if timer_at(rom, hi, inp) < target:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if timer_at(rom, mid, inp) < target: lo = mid
        else: hi = mid
    return hi

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("target", type=int)
    ap.add_argument("--input", default=None)
    ap.add_argument("--lo", type=int, default=60); ap.add_argument("--hi", type=int, default=6000)
    a = ap.parse_args()
    f = frame_for(a.rom, a.target, a.input, a.lo, a.hi)
    if f is None:
        sys.exit(f"{os.path.basename(a.rom)}: game frame {a.target} not reached by {a.hi}")
    print(f"{os.path.basename(a.rom)}: game frame {a.target} at emulator frame {f} "
          f"(timer reads {timer_at(a.rom, f, a.input)})")
    print(f)

if __name__ == "__main__":
    main()
