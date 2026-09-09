#!/usr/bin/env python3
"""PRESENTED ANIMATION RATE — how often the SCREEN changes (LOOP28 93).

    tools/anim_rate.py ROM [--windows 1800,2600,3400] [--input CSV]

`gameplay_speed.py` measures the game's LOGIC advancing: scene-timer
ticks per vint.  It does not measure what reaches the player.  The two
diverge badly — a single-buffered build can run its logic at 49.7% and
present 11.7 screen updates a second, while a double-buffered build runs
at 29.6% and presents 25.3.  Mike's play pass called that difference
before any number did.

This captures one second of consecutive frames at each window and counts
how many differ from the frame before by more than 0.05% of the screen.
Report the mean and the per-window list: a window reading 0 means the
picture stood still for a whole second, which no logic-rate number
shows.
"""
import argparse, os, subprocess, sys, tempfile

ARES = os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def window(rom, start, out, inp):
    args = [ARES, "--frames", str(start + 65), "--input", inp]
    for f in range(start, start + 60):
        args += ["--screenshot", f"{f}:{os.path.join(out, f'f{f}.png')}"]
    args.append(rom)
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"ares-headless failed ({r.returncode})")
    from PIL import Image
    prev, upd = None, 0
    for f in range(start, start + 60):
        d = list(Image.open(os.path.join(out, f"f{f}.png")).convert("RGB").getdata())
        if prev is not None and sum(1 for a, b in zip(prev, d) if a != b) > len(d) * 0.0005:
            upd += 1
        prev = d
    return upd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--windows", default="1800,2600,3400")
    ap.add_argument("--input", default=None)
    a = ap.parse_args()
    inp = a.input or os.path.join(ROOT, "discover/inputs/play_level1.csv")
    starts = [int(x) for x in a.windows.split(",") if x]
    rates = []
    for s in starts:
        with tempfile.TemporaryDirectory() as td:
            rates.append(window(a.rom, s, td, inp))
    print(f"rom {a.rom}")
    print(f"screen updates per second at {starts}: {rates}")
    print(f"mean {sum(rates)/len(rates):.1f} of a possible 60")
    if 0 in rates:
        print("A WINDOW READ 0: the picture stood still for a full second.")
    print()
    print("THIS COUNTS CHANGE, NOT CORRECTNESS. A build rendering confetti")
    print("scores HIGH here — TEXTCAPMASTER read 36.7 against the shipping")
    print("line's 11.7 and its bottom third was noise (LOOP28 97). A colour")
    print("count and a black-fraction do not catch it either; both were in")
    print("range on the broken frame. Before believing any number above,")
    print("LOOK at a frame, or run tools/attract_parity.py against the")
    print("arcade corpus, which is the only oracle that judges the pixels.")


if __name__ == "__main__":
    main()
