#!/usr/bin/env python3
"""R60/E4: harvest the LIVE TILE palette demand from ares-headless.

tools/palharvest_tiles.lua is the MAME twin, but the R60 transport rom
no longer renders in stock MAME at all (measured this experiment: the
screen is uninitialised garbage from boot, PAL_SH and SPR_SNAP read
zero for the whole run — the era's display transport depends on partial
DREQ landings, which MAME lands as zero; CLAUDE.md's "SPRTRUNC is the
only current example" predates R60).  So the harvest runs on ares:
ares-headless dumps memory only at END of run, but runs are frame-exact
deterministic under --input, so one run per sample frame IS a per-cycle
probe.  Runs are parallelised; ~185 samples cost ~3 minutes.

Per sample frame it records the same regions the lua would have:
  sdram 0x27000 0x800   PAL_SH tile block, 128 sets x 8 words
                        (sh_src/m_main.c: cram_paint(.., PAL_SH + c*8, ..))
  sdram 0x3E480 0x100   tile_grp[2][128]  (0xFF = holds no CRAM group)
  sdram 0x3E380 0x80    mdp_s_used[128]   pixel-usage mask per tile colour
  sdram 0x28360 0x20    grp_key[32]       group -> owner colour set
  wram  0xF031  1       scene byte (0xFFF031)

Output: one text file, T/G lines in tools/palharvest_tiles.lua's format
(T f scene n  cc,mm,gg,w0..w7 ...  /  G f k0,...,k31) so the offline
packer reads either corpus.

  python3 tools/palharvest_tiles_ares.py rom/s16.32x /tmp/palharvest_t.txt \
      [start=60] [end=1900] [step=10]
"""
import os, struct, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ARES = "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless"
INPUT = "/Users/mikeholzinger/src/sega16-2-32x/discover/inputs/play2.csv"
JOBS = 8


def sample(rom, f, tmpdir):
    pre = os.path.join(tmpdir, "f%05d" % f)
    args = [ARES, "--frames", str(f), "--input", INPUT,
            "--dump", "sdram:0x27000:0x800:%s_pal.bin" % pre,
            "--dump", "sdram:0x3E480:0x100:%s_tg.bin" % pre,
            "--dump", "sdram:0x3E380:0x80:%s_mu.bin" % pre,
            "--dump", "sdram:0x28360:0x20:%s_gk.bin" % pre,
            "--dump", "wram:0xF031:1:%s_sc.bin" % pre,
            rom]
    subprocess.run(args, capture_output=True, timeout=600)
    pal = open(pre + "_pal.bin", "rb").read()
    tg = open(pre + "_tg.bin", "rb").read()
    mu = open(pre + "_mu.bin", "rb").read()
    gk = open(pre + "_gk.bin", "rb").read()
    sc = open(pre + "_sc.bin", "rb").read()[0]
    for suf in ("_pal", "_tg", "_mu", "_gk", "_sc"):
        os.unlink(pre + suf + ".bin")
    # union of both tile_grp parities, like the lua
    live = {}
    for c in range(128):
        for par in (0, 1):
            g = tg[par * 128 + c]
            if g != 0xFF:
                live.setdefault(c, g)
    lines = []
    if live:
        parts = ["T %d %02X %d" % (f, sc, len(live))]
        for c in sorted(live):
            words = struct.unpack(">8H", pal[c * 16:c * 16 + 16])
            parts.append("%02X,%02X,%02X," % (c, mu[c], live[c]) +
                         ",".join("%04X" % (w & 0x7FFF) for w in words))
        lines.append(" ".join(parts))
        lines.append("G %d %s" % (f, ",".join("%02X" % b for b in gk)))
    return f, lines


def main():
    rom = os.path.abspath(sys.argv[1])
    out = sys.argv[2]
    start = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    end = int(sys.argv[4]) if len(sys.argv) > 4 else 1900
    step = int(sys.argv[5]) if len(sys.argv) > 5 else 10
    frames = list(range(start, end + 1, step))
    tmpdir = tempfile.mkdtemp(prefix="palharvest_")
    res = {}
    with ThreadPoolExecutor(max_workers=JOBS) as ex:
        for f, lines in ex.map(lambda fr: sample(rom, fr, tmpdir), frames):
            res[f] = lines
            sys.stderr.write("\r%d/%d" % (len(res), len(frames)))
    sys.stderr.write("\n")
    with open(out, "w") as fh:
        for f in sorted(res):
            for ln in res[f]:
                fh.write(ln + "\n")
    os.rmdir(tmpdir)


if __name__ == "__main__":
    main()
