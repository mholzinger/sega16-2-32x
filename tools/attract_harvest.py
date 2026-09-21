#!/usr/bin/env python3
"""HARVEST THE ATTRACT SCENES FOR THE BAKE (2026-09-21).

The tile bake is keyed by scene. Rounds 0-4 are harvested from gameplay
(discover/cram/wide/s<n>_*.bin, the game's palette RAM at WRAM 0xFF9000)
and their colour sets come from the ROM's level tilemaps. The attract's
pictures have no ROM tilemap, so this takes both from a headless-ares run
of the LINE at frames anchored on the game's own attract step:

    s<id>_f<frame>.bin   2 KB of WRAM 0xFF9000, as the rounds' dumps
    s<id>_sets.txt       "set cells" per line: every colour set whose
                         tiles were on screen (VRAM name tables -> slot
                         -> md_tag's set field, both planes), summed
                         over the frames

    python3 tools/attract_harvest.py rom/night/attract2.32x
    python3 tools/attract_harvest.py ROM --scene 6 --frames 1660,1760

Scene ids 5-9 and their frames are the line's attract as traced on
2026-09-21 (docs/design/RIG-READOUT.md): 5 title splash (boot step 1-2
and the mid-attract step 2), 6 eye (step 4), 7 second picture (step 5),
8 score table (step 0/7), 9 the round-0 transformation cut (cut bit).
"""
import os, sys, struct, subprocess, argparse, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
OUT = os.path.join(ROOT, "discover", "cram", "wide")
MD_TAG = 0x0603B400            # m_main.c: md_tag, NSETS*NWAYS longs
SCENES = {
    5: [60, 120, 200, 300, 400, 480, 4530, 4600, 4700, 4800, 4900],
    6: [1640, 1660, 1700, 1760, 1850, 1950],
    7: [1980, 2050, 2150, 2300, 2500, 2800],
    8: [2930, 3000, 3100, 3300, 3500],
    9: [1503, 1510, 1520, 1535, 1550, 1570, 1590, 1605],
}

def run(rom, f, d):
    os.makedirs(d, exist_ok=True)
    cmd = [ARES, "--frames", str(f + 2),
           "--dump", f"wram:0xFF9000:0x800:{d}/pal.bin",
           "--dump", f"vram:0xC000:8192:{d}/nt.bin",
           "--dump", f"vsram:0:4:{d}/vs.bin",
           "--dump", f"vram:0xFC00:4:{d}/hs.bin",
           "--dump", f"sdram:0x{MD_TAG:X}:4096:{d}/tag.bin", rom]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)

def sets_on_screen(d):
    nt = open(f"{d}/nt.bin", "rb").read()
    tags = struct.unpack(">1024I", open(f"{d}/tag.bin", "rb").read())
    vs = struct.unpack(">2H", open(f"{d}/vs.bin", "rb").read())
    hs = struct.unpack(">2H", open(f"{d}/hs.bin", "rb").read())
    cells = {}
    for plane, base, vsc, hsc in (("A", 0, vs[0], hs[0]), ("B", 4096, vs[1], hs[1])):
        for row in range(28):
            for col in range(40):
                tr = ((row * 8 + vsc) >> 3) & 31
                tc = (((col * 8 - hsc) & 0x3FF) >> 3) & 63
                w = struct.unpack(">H", nt[base + tr * 128 + tc * 2: base + tr * 128 + tc * 2 + 2])[0]
                slot = w & 0x7FF
                if slot == 0x3FF or slot >= 1024: continue
                t = tags[slot]
                if t == 0xFFFFFFFF: continue
                cs = (t >> 16) & 0x7F
                cells[cs] = cells.get(cs, 0) + 1
    return cells

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--scene", type=int)
    ap.add_argument("--frames")
    a = ap.parse_args()
    scenes = {a.scene: [int(x) for x in a.frames.split(",")]} if a.scene else SCENES
    os.makedirs(OUT, exist_ok=True)
    for sc, frames in scenes.items():
        total = {}
        for f in frames:
            d = f"/tmp/attract_harvest/s{sc}_f{f}"
            run(a.rom, f, d)
            if not os.path.exists(f"{d}/pal.bin"):
                print(f"scene {sc} f{f}: ares produced nothing"); continue
            cells = sets_on_screen(d)
            for k, v in cells.items(): total[k] = total.get(k, 0) + v
            os.replace(f"{d}/pal.bin", os.path.join(OUT, f"s{sc}_f{f}.bin"))
            print(f"scene {sc} f{f}: {len(cells)} sets on screen, {sum(cells.values())} cells")
        with open(os.path.join(OUT, f"s{sc}_sets.txt"), "w") as fh:
            fh.write(f"# harvested from {os.path.basename(a.rom)} at frames {frames}\n")
            for k in sorted(total): fh.write(f"{k} {total[k]}\n")
        print(f"scene {sc}: {len(total)} sets total: {sorted(total)}")

if __name__ == "__main__":
    main()
