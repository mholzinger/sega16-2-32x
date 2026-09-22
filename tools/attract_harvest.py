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
    5: [60, 120, 200, 300, 400, 480, 4530, 4560, 4600, 4650, 4700, 4750, 4800, 4850, 4900, 4950],
    6: [1660, 1700, 1760, 1850, 1900, 1950],   # 1640 is still the cut (eye anchor 1652)
    7: [2030, 2050, 2150, 2300, 2500, 2800],   # 1980 is still the eye (picture anchor 2004)
    8: [2930, 3000, 3100, 3300, 3500],
    9: [1545, 1550, 1556, 1562, 1570, 1580, 1590, 1600, 1605],   # after the page switch (~1540); before it the level's own table shows
}

def run(rom, f, d):
    os.makedirs(d, exist_ok=True)
    cmd = [ARES, "--frames", str(f + 2),
           "--dump", f"wram:0xFF9000:0x800:{d}/pal.bin",
           "--dump", f"vram:0xC000:4096:{d}/nt.bin",
           "--dump", f"vram:0xE000:4096:{d}/ntb.bin",
           "--dump", f"vram:0:32768:{d}/tiles.bin",
           "--dump", f"sdram:0x0603D200:4480:{d}/mirror.bin",
           "--dump", f"vsram:0:4:{d}/vs.bin",
           "--dump", f"vram:0xFC00:4:{d}/hs.bin",
           "--dump", f"sdram:0x{MD_TAG:X}:4096:{d}/tag.bin", rom]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)

MASKS = {}   # scene -> {set: layer mask, 1 = plane B (S16 BG), 2 = plane A (FG)}

def sets_on_screen(d):
    """Colour sets the walker put on the visible screen, per layer.

    Source is md_dbg_nt (m_main.c:653, SDRAM 0x0603D200): the walker's
    [2][28][40] mirror of the name-table word it wrote for every VIEW
    cell -- BG view first, FG at +1120 -- with MD_BLANK_SLOT for a
    blank cell and 0xDEAD for never-written. That is the S16's truth
    for the frame, per screen cell, with no scroll arithmetic. Reading
    the VDP tables instead (until 2026-09-22) counted stale plane-B
    content the walker never re-marked: the level ground behind the
    scores table's black bands, 21 sets nobody sees.

    A BG cell under an FG tile with no pen-0 nibble is hidden
    (jts16_prio.v: the FG covers it) and is not counted.
    """
    mir = struct.unpack(">2240H", open(f"{d}/mirror.bin", "rb").read())
    tags = struct.unpack(">1024I", open(f"{d}/tag.bin", "rb").read())
    tiles = open(f"{d}/tiles.bin", "rb").read()
    cells = {}
    masks = MASKS.setdefault(int(os.path.basename(d).split("_")[0][1:]), {})   # set -> 1 BG | 2 FG
    for q in range(1120):
        fg = mir[1120 + q]
        bg = mir[q]
        for layer, w in (("A", fg), ("B", bg)):
            if w == 0xDEAD: continue
            slot = w & 0x7FF
            if slot == 0x3FF or slot >= 1024: continue
            if layer == "B":
                fs = fg & 0x7FF
                if fg != 0xDEAD and fs < 1024:
                    px = tiles[fs * 32: fs * 32 + 32]
                    if len(px) == 32 and all(b & 0xF0 and b & 0x0F for b in px):
                        continue
            t = tags[slot]
            if t == 0xFFFFFFFF: continue
            cs = (t >> 16) & 0x7F
            cells[cs] = cells.get(cs, 0) + 1
            masks[cs] = masks.get(cs, 0) | (2 if layer == "A" else 1)
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
            fh.write("# set cells layermask (1 = plane B / S16 BG, 2 = plane A / FG, 3 = both)\n")
            for k in sorted(total): fh.write(f"{k} {total[k]} {MASKS.get(sc, {}).get(k, 3)}\n")
        print(f"scene {sc}: {len(total)} sets total: {sorted(total)}")

if __name__ == "__main__":
    main()
