#!/usr/bin/env python3
"""Build provenance stamp for rom/s16.32x.

stamp <rom> [mode]  — write "S16B" + git short hash + UTC time + mode
                      into the trailing pad (last 48 bytes, 0xFF pad).
show  <rom>         — print the stamp.

The same git hash (as u32) is compiled into the SH-2 image via
sh_src/buildstamp.h and written to DIAG[18] at master boot, so every
ares/MAME savestate self-identifies its build commit.
"""
import subprocess
import sys
import time

MAGIC = b"S16B"
OFF = -48


def git_hash():
    try:
        h = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"],
                           capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "-uno", "--",
                                "sh_src", "md_src", "tools", "Makefile"],
                               capture_output=True, text=True).stdout.strip()
        return h + ("+" if dirty else "")
    except Exception:
        return "unknown"


def main():
    cmd, rom = sys.argv[1], sys.argv[2]
    if cmd == "stamp":
        mode = sys.argv[3] if len(sys.argv) > 3 else "normal"
        data = bytearray(open(rom, "rb").read())
        assert all(b == 0xFF for b in data[OFF:]), "tail pad not free"
        ts = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        s = f"{git_hash()} {ts} {mode}".encode()[:43]
        data[OFF:] = (MAGIC + s).ljust(48, b"\0")
        open(rom, "wb").write(data)
    elif cmd == "show":
        tail = open(rom, "rb").read()[OFF:]
        if tail[:4] == MAGIC:
            print("BUILD:", tail[4:].rstrip(b"\0").decode())
        else:
            print("BUILD: unstamped")


if __name__ == "__main__":
    main()
