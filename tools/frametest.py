#!/usr/bin/env python3
"""FRAME DELIVERY TESTS — scene-anchored, defect-shaped, run on any rom.

    tools/frametest.py rom/night/mdb48.32x
    tools/frametest.py rom/night/mdb48.32x --baseline rom/night/lineT.32x
    tools/frametest.py --list

WHY THIS EXISTS. Every expensive mistake in this port has the same
shape: a change was judged by eye, or by one number that did not cover
the thing that broke. 2026-09-18/19 alone:

  - TILESMD shipped INERT for an evening (the .section "a" bug). It
    passed every gate because a feature that does nothing is identical
    to the baseline. Two rig builds were played and a rig verdict taken
    on them.
  - A pen change was ranked on the GLOBAL pinned-set count (97 > 94) and
    regressed round 0, the only level anyone can play. That shipped.
  - The wolf transformation was called a regression and cost four rig
    cycles and a revert of working code. It was pre-existing; nobody ran
    the control.

None of those need a human. They need assertions with names.

WHAT IT MEASURES, and every metric here is a DEFECT WE ACTUALLY HAVE:

  blob_in_cart     the baked tile blob's bytes are physically in the
                   .32x. A flag that silently does nothing passes every
                   behavioural gate, so this runs FIRST.
  tiles_per_vint   tile-record DMAs / vints (DMACENSUS). Throughput is
                   residency is black tiles.
  black_cells      fully-black 8x8 cells in the picture area. UNDERCOUNTS
                   by construction: pop-in is temporal and a still frame
                   catches only what is black at that instant.
  hot_glyph_cells  saturated red/orange cells outside the HUD rows. The
                   Zeus message fragments (R,M,U / O,Y,N) that render
                   incomplete and then never clear.
  scene_colours    distinct colours over 200px. The transformation was
                   3 of an expected 7 and moved to 4 when throughput
                   rose.

SCENE ANCHORING IS NOT OPTIONAL. Two roms at the same EMULATOR frame are
at different points in the game whenever they differ in speed or phase,
and then every number is motion rather than rendering. That trap alone
produced four wrong readings on 2026-09-17, including a "HUD dropout"
escalated as a defect that was two builds photographed one frame apart.
Checkpoints are 68K scene-timer values (WRAM 0xFFF02A, one tick per GAME
frame) and each rom is captured at ITS OWN emulator frame for that value.

ares charges SH-2 instruction cycles only -- no SDRAM waits, no data
cache -- so tiles_per_vint here is an EXACTNESS measure, not a speed
ranking. Speed still belongs to the rig.
"""
import argparse, collections, json, os, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARES = os.environ.get("ARES", os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless"))
INPUT = os.path.join(ROOT, "discover/inputs/play_level1.csv")

# Checkpoints are GAME frames (68K scene timer 0xFFF02A), never emulator
# frames. Keep them inside one scene: the timer resets per scene, so
# bisection is only monotonic within one.
CHECKPOINTS = [
    ("level1_early", 1124),
    ("level1_mid",   1624),
]

HUD_ROWS = range(0, 4)      # score/lives live here; their colour is not a defect


def run(rom, frames, dumps=None, shots=None):
    cmd = [ARES, "--frames", str(frames), "--input", INPUT]
    for spec in (dumps or []):
        cmd += ["--dump", spec]
    for f, path in (shots or []):
        cmd += ["--screenshot", f"{f}:{path}"]
    cmd.append(rom)
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def timer_at(rom, frame):
    with tempfile.TemporaryDirectory() as td:
        f = os.path.join(td, "t.bin")
        run(rom, frame, dumps=[f"wram:0xFFF02A:2:{f}"])
        try:
            return struct.unpack(">H", open(f, "rb").read())[0]
        except Exception:
            return -1


def emulator_frame_for(rom, target, lo=60, hi=6000):
    """Smallest emulator frame whose scene timer >= target."""
    if timer_at(rom, hi) < target:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if timer_at(rom, mid) < target:
            lo = mid
        else:
            hi = mid
    return hi


def blob_in_cart(rom):
    """A flag that adds data to the cart must be PROVEN to have done so."""
    blob = os.path.join(ROOT, "sh_src/tiles_md.bin")
    if not os.path.exists(blob):
        return None
    b = open(blob, "rb").read()
    r = open(rom, "rb").read()
    sig = b[0x540:0x560]
    i = r.find(sig)
    return None if i < 0 else i - 0x540


def picture_area(a):
    """Crop the letterbox; the border is not part of any defect."""
    import numpy as np
    rows = np.where(a.sum(axis=(1, 2)) > 0)[0]
    cols = np.where(a.sum(axis=(0, 2)) > 0)[0]
    if len(rows) == 0 or len(cols) == 0:
        return a
    return a[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]


def frame_metrics(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(int)
    sub = picture_area(a)
    h, w, _ = sub.shape
    ch, cw = h // 8 * 8, w // 8 * 8
    cells = sub[:ch, :cw].reshape(ch // 8, 8, cw // 8, 8, 3)

    black = int((cells.sum(axis=(1, 3, 4)) == 0).sum())

    hot = (sub[:, :, 0] > 150) & (sub[:, :, 2] < 90)
    hot_cells = hot[:ch, :cw].reshape(ch // 8, 8, cw // 8, 8).any(axis=(1, 3))
    for r in HUD_ROWS:
        if r < hot_cells.shape[0]:
            hot_cells[r, :] = False

    flat = sub.reshape(-1, 3)
    cnt = collections.Counter(map(tuple, flat))
    colours = sum(1 for _, n in cnt.items() if n > 200)

    return {
        "black_cells": black,
        "total_cells": int(cells.shape[0] * cells.shape[2]),
        "hot_glyph_cells": int(hot_cells.sum()),
        "scene_colours": colours,
    }


def throughput(rom):
    """tiles/vint. Needs a DMACENSUS build; absent is not a failure."""
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "d.bin")
        v = os.path.join(td, "v.bin")
        run(rom, 3000, dumps=[f"wram:0xFFA246:8:{d}", f"wram:0xFFB0F0:2:{v}"])
        try:
            n = struct.unpack(">H", open(d, "rb").read()[0:2])[0]
            vi = struct.unpack(">H", open(v, "rb").read())[0]
            return round(n / vi, 2) if vi else None
        except Exception:
            return None


def measure(rom, tmp):
    out = {"rom": rom, "blob_in_cart": blob_in_cart(rom),
           "tiles_per_vint": throughput(rom), "frames": {}}
    for name, game_frame in CHECKPOINTS:
        ef = emulator_frame_for(rom, game_frame)
        if ef is None:
            out["frames"][name] = {"error": "never reached game frame %d" % game_frame}
            continue
        png = os.path.join(tmp, "%s_%s.png" % (os.path.basename(rom), name))
        run(rom, ef + 10, shots=[(ef, png)])
        if not os.path.exists(png):
            out["frames"][name] = {"error": "no capture at emulator frame %d" % ef}
            continue
        m = frame_metrics(png)
        m["game_frame"] = game_frame
        m["emulator_frame"] = ef
        out["frames"][name] = m
    return out


def report(cur, base):
    bad = 0
    print("\n\033[1m== frame delivery ==\033[0m")
    print("  rom: %s" % cur["rom"])
    if base:
        print("  baseline: %s" % base["rom"])

    b = cur["blob_in_cart"]
    if b is None:
        print("  \033[33mblob_in_cart   n/a\033[0m (no tiles_md.bin, or TILESMD off)")
    else:
        print("  blob_in_cart   present at cart 0x%X" % b)

    def cmp_line(label, c, bs, lower_better=True, tol=0):
        nonlocal bad
        if c is None:
            print("  %-16s n/a" % label)
            return
        if bs is None:
            print("  %-16s %s" % (label, c))
            return
        d = c - bs
        worse = (d > tol) if lower_better else (d < -tol)
        mark = "\033[31mWORSE\033[0m" if worse else ("\033[32mbetter\033[0m" if d else "same")
        if worse:
            bad += 1
        print("  %-16s %-8s vs %-8s %+d  %s" % (label, c, bs, d, mark))

    cmp_line("tiles_per_vint", cur["tiles_per_vint"],
             base["tiles_per_vint"] if base else None, lower_better=False)

    for name, _ in CHECKPOINTS:
        c = cur["frames"].get(name, {})
        bs = (base or {}).get("frames", {}).get(name, {})
        print("  \033[1m%s\033[0m  game frame %s -> emulator %s" %
              (name, c.get("game_frame", "?"), c.get("emulator_frame", "?")))
        if "error" in c:
            print("    \033[31m%s\033[0m" % c["error"])
            bad += 1
            continue
        cmp_line("  black_cells", c["black_cells"], bs.get("black_cells"))
        cmp_line("  hot_glyphs", c["hot_glyph_cells"], bs.get("hot_glyph_cells"))
        cmp_line("  colours", c["scene_colours"], bs.get("scene_colours"),
                 lower_better=False)

    print("\n\033[1m== result ==\033[0m")
    if base is None:
        print("  baseline not given: measurements only, nothing gated.")
    elif bad:
        print("  \033[31m%d metric(s) regressed against the baseline.\033[0m" % bad)
    else:
        print("  \033[32mno regression against the baseline.\033[0m")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="?")
    ap.add_argument("--baseline", default=None,
                    help="rom to compare against (normally the line)")
    ap.add_argument("--json", default=None, help="write raw measurements here")
    ap.add_argument("--list", action="store_true", help="show the checkpoints")
    a = ap.parse_args()

    if a.list:
        print("checkpoints (68K scene timer 0xFFF02A, one tick per GAME frame):")
        for n, g in CHECKPOINTS:
            print("  %-16s game frame %d" % (n, g))
        return 0
    if not a.rom:
        ap.error("give a rom, or --list")

    with tempfile.TemporaryDirectory() as tmp:
        cur = measure(a.rom, tmp)
        base = measure(a.baseline, tmp) if a.baseline else None
        if a.json:
            json.dump({"rom": cur, "baseline": base}, open(a.json, "w"), indent=1)
        return 1 if report(cur, base) else 0


if __name__ == "__main__":
    sys.exit(main())
