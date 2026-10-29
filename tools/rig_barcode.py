#!/usr/bin/env python3
"""DECODE THE NAME-TABLE BARCODE (docs/design/RIG-READOUT.md).

A RIGBARCODE=1 rom writes 80 bits into the MD window plane every vint:
name-table rows 22-27, each row the SAME 80 bits as 40 four-colour
cells (2 bits each: blank / white / red / blue), so six rows vote per
bit through sprites and the text layer.

  tools/rig_barcode.py shot.png ...        decode files (320x224, or an
                                           ares 1415x243 frame, cropped)
  tools/rig_barcode.py --rom NAME          scp the rig's shots for NAME
                                           and decode them all

Layout: byte 0 0xA5 | 1-2 vint | 3 packets consumed | 4 palette-flagged
consumed | 5 SH-2 packets published | 6 visible stale cells / 4 (untagged or dirty slots, SH-2 mirror) | 7 [5:0]
CRAM 16-47 non-zero [6] md_hold (packet bit 13) [7] IO_MISC bit 5 video-on | 8
[2:0] round [3] cut [6:4] attract step [7] play | 9 XOR of 0-8.
Counters are mod 256. `--raw` prints every row's symbols.
"""
import sys, os, glob, subprocess, argparse
from PIL import Image

import os as _os
# THERE IS NO DEFAULT RIG, ON PURPOSE (2026-09-27). mister.office.local is the
# DECOMPILE thread's and mister.tv.local is the BUILDER's. A default sends every
# un-flagged invocation at one of them, which is how the wrong rig got a rom
# more than once -- and the push that misfires is invisible from here, because
# the launch API answers "game started" whichever machine it was. Naming the rig
# is one word; guessing it wrong costs somebody else's session. So: name it.
HOST = _os.environ.get("MISTER")
if not HOST:
    raise SystemExit("rig_barcode: set MISTER=root@mister.tv.local (builder) "
                     "or root@mister.office.local (decompile). No default.")
SHOTS = "/media/fat/screenshots/S32X"

def frame(path):
    im = Image.open(path).convert("RGB")
    if im.size == (1415, 243):
        # NEAREST, not BILINEAR (2026-09-27). ares gives a 4:1 pixel-doubled
        # frame, so NEAREST is EXACT and bilinear blurs every edge: the same
        # ares and FPGA frame read 9.6% different through bilinear and
        # 0.00% -- pixel-identical -- through nearest. Rig shots arrive at
        # 320x224 already, so this path only ever affects ares frames.
        im = im.crop((65, 11, 65 + 1280, 11 + 224)).resize((320, 224), Image.NEAREST)   # active area rows 11-234 (measured 2026-09-25; 19 was 8 lines low)
    elif im.size != (320, 224):
        im = im.resize((320, 224), Image.NEAREST)
    return im

def sym(px, col, row, dy=0):
    """4x4 centre of an 8x8 cell -> 0 blank, 1 white, 2 red, 3 blue, or
    None when the cell is off the frame"""
    r = g = b = 0
    for y in range(row * 8 + 2 + dy, row * 8 + 6 + dy):
        for x in range(col * 8 + 2, col * 8 + 6):
            if not (0 <= y < 224): return None
            p = px[x, y]; r += p[0]; g += p[1]; b += p[2]
    r /= 16.0; g /= 16.0; b /= 16.0
    if min(r, g, b) > 150: return 1
    if r > 150 and g < 90 and b < 90: return 2
    if b > 150 and r < 90 and g < 90: return 3
    return 0

def row_bytes(px, row, dy=0):
    syms = [sym(px, c, row, dy) for c in range(40)]
    if any(v is None for v in syms): return None, syms
    by = []
    for i in range(10):
        v = 0
        for k in range(4): v = (v << 2) | syms[i * 4 + k]
        by.append(v)
    return by, syms

def valid(c):
    return c is not None and c[0] == 0xA5 and \
        (c[0] ^ c[1] ^ c[2] ^ c[3] ^ c[4] ^ c[5] ^ c[6] ^ c[7] ^ c[8]) == c[9]

def vote(rows):
    """per-bit majority over rows that at least carry the marker"""
    good = [r for r in rows if r is not None and r[0] == 0xA5]
    if len(good) < 2: return None
    out = []
    for i in range(10):
        v = 0
        for k in range(8):
            ones = sum((r[i] >> k) & 1 for r in good)
            if ones * 2 > len(good): v |= 1 << k
        out.append(v)
    return out

def decode(path, raw=False):
    im = frame(path); px = im.load()
    out = {"file": os.path.basename(path), "ok": False}
    for r0 in (22, 21, 23, 26, 25, 27):  # ares' crop lands the rows one up; BCROWS=2 sits at 26
        for dy in (0, -3, 3):
            rows = [row_bytes(px, r0 + k, dy)[0] for k in range(6)]
            if raw:
                for k in range(6):
                    b, s = row_bytes(px, r0 + k, dy)
                    print(f"      r{r0+k}{dy:+d} " + "".join(".WRB"[v] if v is not None else "?" for v in s))
            cands = []
            v = vote(rows)
            if valid(v): cands.append(("vote", v))
            for k, r in enumerate(rows):
                if valid(r): cands.append((f"row{r0+k}", r))
            if cands:
                how, c = cands[0]
                agree = sum(1 for r in rows if r == c)
                out.update(ok=True, how=how, agree=agree, r0=r0, dy=dy,
                           vint=(c[1] << 8) | c[2], pkt=c[3], pal68=c[4],
                           sh_pub=c[5], stale4=c[6], cram=c[7] & 63, hold=(c[7] >> 6) & 1,
                           vidon=c[7] >> 7, round=c[8] & 7, cut=(c[8] >> 3) & 1,
                           step=(c[8] >> 4) & 7, play=c[8] >> 7)
                return out
    return out

# BYTES 5 AND 6 ARE MODE-DEPENDENT AND THE WIRE FORMAT DOES NOT SAY WHICH MODE.
# m_main.c defines RIGB5 sixteen different ways depending on the build's flags,
# and byte 0's 0xA5 is a fixed magic, not a mode tag. This decoder used to print
# byte 5 as "sh_pub" unconditionally, so a PORTTEST rom -- where byte 5 is the
# self-test pass count -- read as a packet count. 2026-09-27: that misread a
# PORT_TEST readout of 0 as "3 cases passed", because sh_pub happened to be 3.
#
# So the decoder no longer NAMES bytes it cannot identify. Pass --mode to label
# them; without it they print as b5/b6 and the reader is not told a meaning that
# may be false. Add a mode here when you add a RIGB5 arm in m_main.c.
MODES = {
    "default":  ("sh_pub", "stale-cells/4"),
    "porttest": ("cases-passed", "vectors-run(b6 unreliable)"),
    "rate":     ("(68K owns b5)", "fs*255/vint"),
    "bench":    ("sb_res0", "sb_res1"),
    "blit":     ("blit-lines", "blit-groups"),
    "pick":     ("pick-line", "blit-half-lines"),
    "isr":      ("(see RIGB5 arm)", "isr-lines"),
    "task":     ("(see RIGB5 arm)", "rig_task"),
}


def show(d, mode="default"):
    if not d["ok"]:
        print(f"  {d['file']}  NO BARCODE"); return
    l5, l6 = MODES.get(mode, MODES["default"])
    if mode == "default":
        l5, l6 = "b5", "b6"          # unnamed on purpose: 16 possible meanings
    print(f"  {d['file']}  vint {d['vint']:5d}  pkt {d['pkt']:3d}  pal68 {d['pal68']:3d}  "
          f"{l5} {d['sh_pub']:3d}  {l6} {d['stale4']:3d}  cram16-47 {d['cram']:2d}  "
          f"hold {d['hold']} vidon {d['vidon']}  round {d['round']} cut {d['cut']} "
          f"step {d['step']} play {d['play']}   [{d['how']} r{d['r0']}{d['dy']:+d}, {d['agree']}/6 rows agree]")

ap = argparse.ArgumentParser()
ap.add_argument("files", nargs="*")
ap.add_argument("--rom", help="pull every rig screenshot named for this rom into --tmp and decode")
ap.add_argument("--tmp", default="/tmp/rigbc")
ap.add_argument("--raw", action="store_true", help="print every row's cell symbols")
ap.add_argument("--mode", default="default", choices=sorted(MODES),
                help="what THIS rom's flags make bytes 5 and 6 mean; without it they "
                     "print unnamed, because the wire format does not carry a mode tag")
a = ap.parse_args()
files = list(a.files)
if a.rom:
    os.makedirs(a.tmp, exist_ok=True)
    subprocess.run(["scp", "-q", f"{HOST}:{SHOTS}/*-{a.rom}.png", a.tmp], check=False)
    files += sorted(glob.glob(os.path.join(a.tmp, f"*-{a.rom}.png")))
if not files: sys.exit("no files")
for f in files: show(decode(f, a.raw), a.mode)
