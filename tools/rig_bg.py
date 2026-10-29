#!/usr/bin/env python3
"""IS THE BACKGROUND ON THE SCREEN? A number, from the rig, per launch.

    MISTER=root@mister.office.local tools/rig_bg.py pc9ff.32x [--reads 3] [--wait 40]

LESSONS.md 830 fixed the criterion on the attract scene: a 99% non-black frame with the
line's colour mix at 37-50 s after launch = background PRESENT; a sky-only frame or a dark
one = ABSENT. It also fixed the protocol -- the FPGA's background follows BUILD LAYOUT, so
a verdict needs THREE launches, each read on its own.

This reads the MiSTer's own screenshot (named after the loaded rom, which is the only
defence against cross-rom contamination) and reports, per read:
    nonblack   per cent of sampled pixels that are not near-black
    top        the most common colour's share -- one flat fill is the ABSENT signature
    distinct   distinct colours in the sample
A read is PRESENT when nonblack >= 90 and top < 60. Nothing here decides what it MEANS:
a pad control (LAYOUTPROBE=1) says whether a verdict belongs to the feature or the layout.
"""
import os, subprocess, sys, time, struct, zlib, collections

HOST = os.environ.get("MISTER")
if not HOST:
    raise SystemExit("rig_bg: set MISTER=root@mister.office.local (decompile) "
                     "or root@mister.tv.local (builder). No default, on purpose.")
HOSTNAME = HOST.split("@")[-1]
SHOTS = "/media/fat/screenshots/S32X"


def sh(c):
    return subprocess.run(["ssh", "-o", "ConnectTimeout=8", HOST, c],
                          capture_output=True, text=True).stdout.strip()


def launch(rom):
    subprocess.run(["curl", "-s", "-m", "10", "-X", "POST", "-H", "Content-Type: application/json",
                    # the API wants the rom's FULL PATH on the rig, the way
                    # tools/mister_push.sh sends it -- "Sega32X/<rom>" is refused
                    # with "no system found" and the old rom keeps running, which
                    # reads as a perfectly repeatable verdict for the wrong build.
                    "-d", '{"path":"/media/fat/games/S32X/%s"}' % rom,
                    "http://%s:8182/api/games/launch" % HOSTNAME],
                   capture_output=True)
    # /tmp/remote.log IS THE SIGNAL, AND IT IS WRITTEN LATE. Reading it straight after the
    # curl returns the PREVIOUS launch's line, which is how a read of the wrong build looks
    # like a clean verdict (2026-10-06). Poll for this rom's own "game started".
    stem = rom.replace(".32x", "")
    for _ in range(12):
        time.sleep(1)
        line = sh("tail -10 /tmp/remote.log | grep 'game started' | grep '%s' | tail -1" % stem)
        if line:
            return line
    return "NO LAUNCH CONFIRMED for " + rom


def unpng(path):
    d = open(path, "rb").read()
    pos, idat, w, h, ct = 8, b"", None, None, None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ, data = d[pos + 4:pos + 8], d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, _bd, ct = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT":
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride, rows, prev, i = w * ch, [], bytearray(w * ch), 0

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        if f == 1:
            for x in range(ch, stride): line[x] = (line[x] + line[x - ch]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                line[x] = (line[x] + ((line[x - ch] if x >= ch else 0) + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                line[x] = (line[x] + paeth(line[x - ch] if x >= ch else 0, prev[x],
                                           prev[x - ch] if x >= ch else 0)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def census(path):
    w, h, ch, rows = unpng(path)
    c = collections.Counter()
    for y in range(0, h, 2):
        r = rows[y]
        for x in range(0, w, 2):
            c[r[x * ch:x * ch + 3]] += 1
    tot = sum(c.values())
    dark = sum(n for col, n in c.items() if max(col) < 24)
    top = c.most_common(1)[0]
    return (100.0 * (tot - dark) / tot, 100.0 * top[1] / tot, len(c), tuple(top[0]))


def main():
    rom = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
    reads = int(sys.argv[sys.argv.index("--reads") + 1]) if "--reads" in sys.argv else 3
    # 37-50 s AFTER LAUNCH is the scene the criterion was fixed on (LESSONS 830); earlier
    # than that the attract is still in a dark phase and every build reads ABSENT.
    wait = int(sys.argv[sys.argv.index("--wait") + 1]) if "--wait" in sys.argv else 42
    stem = rom.replace(".32x", "")
    verdicts = []
    # EACH READ IS ITS OWN LAUNCH, and the API NO-OPS on an already-running path -- three
    # screenshots of one launch is not three reads (2026-10-06: all three carried the same
    # "game started" line). Launching a different rom first forces the core to reload.
    alt = sys.argv[sys.argv.index("--alt") + 1] if "--alt" in sys.argv else None
    for k in range(reads):
        if alt and k:
            launch(alt)
        print("read %d: %s" % (k + 1, launch(rom)))
        time.sleep(wait)
        before = sh("ls -t %s/*-%s.png 2>/dev/null | head -1" % (SHOTS, stem))
        sh("echo screenshot > /dev/MiSTer_cmd")
        time.sleep(4)
        shot = sh("ls -t %s/*-%s.png 2>/dev/null | head -1" % (SHOTS, stem))
        if not shot or shot == before:
            print("        no new screenshot (rate limit or the rom did not load)"); continue
        local = "/tmp/rig_bg_%s_%d.png" % (stem, k)
        subprocess.run(["scp", "-q", "%s:%s" % (HOST, shot), local], check=True)
        nb, top, dis, col = census(local)
        ok = nb >= 90.0 and top < 60.0
        verdicts.append(ok)
        print("        nonblack %5.1f%%  top colour %5.1f%% rgb%-14s distinct %4d  -> %s"
              % (nb, top, col, dis, "PRESENT" if ok else "ABSENT"))
    if verdicts:
        print("%s: background PRESENT in %d of %d reads" % (stem, sum(verdicts), len(verdicts)))


if __name__ == "__main__":
    main()
