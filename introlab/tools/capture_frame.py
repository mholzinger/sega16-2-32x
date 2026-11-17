#!/usr/bin/env python3
"""Capture one frame of an arcade set's attract mode from MAME, headless, for use as
cabinet art (a region's title screen).

    capture_frame.py <mame set> <frame> <out.png> [rompath]

e.g. capture_frame.py altbeastj 960 introlab/art/altbeast/src/title_jp.png
Frame 960 of altbeastj is the red title logo. MAME runs with no video, no sound, no
keyboard grab and a thumbnail window, and stops itself after the frame."""
import os
import shutil
import subprocess
import sys
import tempfile

import cabinet.common as common

LUA = """
local target = tonumber(os.getenv("CAP_FRAME"))
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n == target then manager.machine.video:snapshot() end
  if n > target then manager.machine:exit() end
end)
"""


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    rom_set, frame, out = sys.argv[1], int(sys.argv[2]), os.path.abspath(sys.argv[3])
    rompath = sys.argv[4] if len(sys.argv) > 4 else common.resolve("mame")
    with tempfile.TemporaryDirectory() as tmp:
        lua = os.path.join(tmp, "cap.lua")
        open(lua, "w").write(LUA)
        subprocess.run(["mame", rom_set, "-rompath", rompath, "-skip_gameinfo", "-video", "none", "-sound", "none",
                        "-nothrottle", "-window", "-resolution", "160x120", "-keyboardprovider", "none",
                        "-nomouse", "-nojoystick", "-bench", str(frame // 60 + 10),
                        "-snapshot_directory", tmp, "-cfg_directory", tmp, "-nvram_directory", tmp,
                        "-autoboot_script", lua],
                       check=True, env=dict(os.environ, CAP_FRAME=str(frame)),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        shots = [os.path.join(d, f) for d, _, fs in os.walk(tmp) for f in fs if f.endswith(".png")]
        if not shots:
            sys.exit(f"capture: MAME wrote no snapshot for {rom_set} frame {frame}")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copy(shots[0], out)
    print(f"captured {rom_set} frame {frame} -> {out}")


if __name__ == "__main__":
    main()
