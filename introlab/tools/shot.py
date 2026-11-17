#!/usr/bin/env python3
"""Run the lab rom in headless ares, save a screenshot, and read the SH-2's own frame
counters back out of SDRAM.  usage: shot.py <ares-headless> <rom prefix> <frames>"""
import struct
import subprocess
import sys

ares, prefix, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
# optional: an ares --input csv, then extra frame numbers to screenshot (<prefix>_<frame>.png)
inputs = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else None
extra = [int(a) for a in sys.argv[5:]]
addr = None
for line in open(prefix + ".lst"):
    parts = line.split()
    if len(parts) == 3 and parts[2] == "_introlab_diag":
        addr = int(parts[0], 16)
if addr is None:
    sys.exit("shot: _introlab_diag is not in " + prefix + ".lst")
dump, png = prefix + ".diag.bin", prefix + ".png"
cmd = [ares, "--frames", str(frames), "--screenshot", f"{frames - 1}:{png}"]
for f in extra:
    cmd += ["--screenshot", f"{f}:{prefix}_{f}.png"]
if inputs:
    cmd += ["--input", inputs]
subprocess.run(cmd + ["--dump", f"sdram:0x{addr:08X}:52:{dump}", prefix + ".32x"],
               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
magic, flipped, vblanks, worst, faces, *rest = struct.unpack(">13I", open(dump, "rb").read(52))
ticks, door_frames, door_vbl = rest[:4], rest[6], rest[7]
if magic != 0x494E5452:
    sys.exit(f"shot: bad magic {magic:#010x} at {addr:#010x}: the SH-2 never reached m_main")
rate = flipped / vblanks if vblanks else 0.0
print(f"emulated frames {frames}  flipped {flipped}  vblanks {vblanks}  "
      f"frames per vblank {rate:.3f}  worst frame {worst} vblank(s)  faces drawn {faces}")
if flipped and any(ticks):
    ms = [t / flipped * 128 / 23.011 / 1000 for t in ticks]      # FRT at Phi/128, Phi = 23.011 MHz
    print(f"mean per frame: {vblanks / flipped * 16.68:.1f} ms = {60 * flipped / vblanks:.1f} fps;  geometry {ms[0]:.2f} ms  "
          f"primary clear {ms[1]:.2f}  primary raster {ms[2]:.2f}  secondary (wake to done) {ms[3]:.2f}")
if door_vbl:
    print(f"doors swinging: {door_frames} frames in {door_vbl} vblanks = {60 * door_frames / door_vbl:.1f} fps")
print(f"screenshot: {png}")
