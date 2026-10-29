#!/usr/bin/env python3
"""Capture our rom at the GAME frame an arcade recording names (2026-09-25).

The arcade oracle comes from tools/inp_dump.lua: a recorded playthrough replayed
headlessly, which writes `inputs_gf.csv` (the route, keyed to the game's own frame
count), `state.csv` (per-frame game state) and `snaps.csv` (every arcade snapshot
with the frame and GAME frame it shows). Our rom replays that route with
`--input-gameframe 0xFFA1D0`, the shim's game-frame counter (md_main.c
shim_vblank), so it takes the same route at the same game frames however slowly
it runs.

    tools/gf_align.py <rom> <oracle-dir> <gf> [<gf> ...] [--out DIR] [--tag NAME]
    tools/gf_align.py <rom> <oracle-dir> --edges          every scene edge
    tools/gf_align.py <rom> <oracle-dir> --round 1        every edge of a round

For each target game frame it bisects the emulator frame at which our rom's
counter first reaches it, then writes <tag>_gf<N>.png beside the arcade's
own snapshot for that game frame (copied in as arc_gf<N>.png), and prints the
two games' state side by side so a mismatch in the GAME's logic is visible
before any pixel is compared.
"""
import argparse, csv, os, shutil, struct, subprocess, sys

ARES = os.environ.get("ARES", os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless"))
GF_ADDR = 0xFFA1D0


def run(rom, frames, inputs, dumps=(), shots=()):
    cmd = [ARES, "--frames", str(frames), "--input", inputs,
           "--input-gameframe", hex(GF_ADDR)]
    for m, a, l, p in dumps:
        cmd += ["--dump", f"{m}:{hex(a)}:{hex(l)}:{p}"]
    for f, p in shots:
        cmd += ["--screenshot", f"{f}:{p}"]
    cmd.append(rom)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"ares-headless failed ({r.returncode})")


# our own state, logged as columns of the same game-frame log: the scene timer,
# the round byte, the solo/cutscene byte, and the FG/BG page words (the arcade's
# text RAM 0x410000 is our 0xFF8000, so word 0x740 is 0xFF8E80)
WATCH = ["0xFFF02A:2", "0xFFF142:1", "0xFFF148:1", "0xFF8E80:4"]


def gf_map(rom, inputs, frames, path):
    """one run: {game frame -> (first emulator frame that reached it, our state)}"""
    cmd = [ARES, "--frames", str(frames), "--input", inputs,
           "--input-gameframe", hex(GF_ADDR), "--gameframe-log", path]
    for w in WATCH:
        cmd += ["--gameframe-watch", w]
    cmd += [rom]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"ares-headless failed ({r.returncode})")
    first = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            gf = int(row["gameframe"])
            if gf not in first:
                first[gf] = (int(row["frame"]),
                             f"timer {int(row['0xFFF02A:2'], 16)} round {int(row['0xFFF142:1'], 16)} "
                             f"solo {int(row['0xFFF148:1'], 16)} page {row['0xFF8E80:4'][:4]}/{row['0xFF8E80:4'][4:]}")
    return first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("oracle")
    ap.add_argument("gf", nargs="*", type=int)
    ap.add_argument("--out", default=None); ap.add_argument("--tag", default=None)
    ap.add_argument("--edges", action="store_true")
    ap.add_argument("--round", type=int, default=None)
    ap.add_argument("--hi", type=int, default=20000)
    a = ap.parse_args()

    out = a.out or os.path.join(a.oracle, "ours")
    os.makedirs(out, exist_ok=True)
    tag = a.tag or os.path.basename(a.rom).replace(".32x", "")
    inputs = os.path.join(a.oracle, "inputs_gf.csv")

    snaps = list(csv.DictReader(open(os.path.join(a.oracle, "snaps.csv"))))
    arc_pngs = sorted(f for f in os.listdir(os.path.join(a.oracle, "altbeast"))
                      if f.endswith(".png")) if os.path.isdir(os.path.join(a.oracle, "altbeast")) else []
    state = {int(r["gf"]): r for r in csv.DictReader(open(os.path.join(a.oracle, "state.csv")))}

    targets = list(a.gf)
    if not a.gf and not a.edges and a.round is None:
        # default: every game frame the arcade actually has a picture for
        targets += [int(s["gf"]) for s in snaps if int(s["gf"]) > 0]
    if a.edges:
        targets += [int(s["gf"]) for s in snaps if s["edge"] == "1"]
    if a.round is not None:
        targets += [int(s["gf"]) for s in snaps
                    if s["edge"] == "1" and int(s["round"]) == a.round]
    targets = sorted(set(t for t in targets if t > 0))
    if not targets:
        raise SystemExit("no target game frames (pass some, or --edges / --round N)")

    # run 1: the map from our rom's game frames to its emulator frames
    first = gf_map(a.rom, inputs, a.hi, os.path.join(out, f".{tag}_gfmap.csv"))
    hit = [(t, first[t][0], first[t][1]) for t in targets if t in first]
    missed = [t for t in targets if t not in first]

    # run 2: every screenshot at once
    shots = [(fr, os.path.join(out, f"{tag}_gf{t}.png")) for t, fr, _ in hit]
    if hit:
        run(a.rom, max(f for _, f, _ in hit) + 1, inputs, shots=shots)

    print(f"{'gf':>6} {'frame':>7}  {'ARCADE':<44} {'OURS':<44}")
    for t, fr, ourstate in hit:
        s = state.get(t)
        arc = (f"timer {s['timer']} round {s['round']} solo {s['solo']} "
               f"page {s['page0']}/{s['page1']}" if s else "(not in state.csv)")
        gap = ""
        near = [(abs(int(x["gf"]) - t), int(x["index"]), int(x["gf"]))
                for x in snaps if int(x["gf"]) > 0]
        if near and arc_pngs:
            d, idx, sgf = min(near)
            if idx < len(arc_pngs):
                shutil.copy(os.path.join(a.oracle, "altbeast", arc_pngs[idx]),
                            os.path.join(out, f"arc_gf{t}.png"))
                gap = f"  arcade png from gf {sgf}" + (f" (+-{d})" if d else " (exact)")
        flag = "" if (s and f"round {int(s['round'])} " in ourstate + " ") else "  <-- DIVERGED"
        print(f"{t:6d} {fr:7d}  {arc:<44} {ourstate:<44}{flag}{gap}")
    for t in missed:
        print(f"{t:6d} {'never':>9}  (not reached within {a.hi} emulator frames)")
    print(f"\nwrote {out}/  ({tag}_gf*.png beside arc_gf*.png)")


if __name__ == "__main__":
    main()
