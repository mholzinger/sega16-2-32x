#!/usr/bin/env python3
"""Headless gameplay speed ladder (session 7).

Runs a rom through ares-headless with the level-1 input script, dumps
the 68K scene timer (WRAM 0xFFF02A, one tick per GAME frame) and the
SH-2 DIAG page at two frame counts, and reports game-frames per vint
over the interval (100% = 60 game-frames/s).  Also snapshots a few
frames so an A/B can be eyeballed.

    tools/gameplay_speed.py rom.32x [--a 1500] [--b 4100] [--out DIR]
                                    [--shots 2000,3000] [--input CSV]

Every ares run is deterministic for a given rom+script, so two runs at
different frame counts sample the same timeline.
"""
import argparse, os, struct, subprocess, sys

ARES = os.path.expanduser(
    "~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(rom, frames, out, tag, shots=(), inp=None, extra=None):
    wram = os.path.join(out, f"{tag}_wram.bin")
    diag = os.path.join(out, f"{tag}_diag.bin")
    cmd = [ARES, "--frames", str(frames),
           "--dump", f"wram:0xFFF000:0x200:{wram}",
           "--dump", f"wram:0xFFA000:0x2000:{os.path.join(out, tag + '_wramA.bin')}",
           "--dump", f"sdram:0x28000:0x1000:{diag}",
           "--input", inp or os.path.join(ROOT, "discover/inputs/play_level1.csv")]
    if extra:
        cmd += ["--dump", f"sdram:{extra}:{os.path.join(out, tag + '_extra.bin')}"]
    for s in shots:
        if s < frames:
            cmd += ["--screenshot", f"{s}:{os.path.join(out, f'{tag}_f{s}.png')}"]
    cmd.append(rom)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"ares-headless failed ({r.returncode})")
    with open(wram, "rb") as f:
        w = f.read()
    with open(diag, "rb") as f:
        d = f.read()
    timer = struct.unpack_from(">H", w, 0x2A)[0]
    miss = struct.unpack_from(">H", w, 0x144)[0]   # the game's own IRQ4
                                                   # frame-miss counter
    return timer, d, miss


def diag16(d, i):
    return struct.unpack_from(">I", d, i * 4)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--a", type=int, default=1500)
    ap.add_argument("--b", type=int, default=4100)
    ap.add_argument("--out", default=None)
    ap.add_argument("--shots", default="2000,3000,4000")
    ap.add_argument("--input", default=None)
    ap.add_argument("--extra", default=None,
                    help="addr:len of an SDRAM block to dump, printed as u32 deltas")
    args = ap.parse_args()
    out = args.out or os.path.join(os.environ.get("TMPDIR", "/tmp"), "gspeed")
    os.makedirs(out, exist_ok=True)
    shots = [int(x) for x in args.shots.split(",") if x]
    ta, da, ma = run(args.rom, args.a, out, "a", inp=args.input, extra=args.extra)
    tb, db, mb = run(args.rom, args.b, out, "b", shots=shots, inp=args.input, extra=args.extra)
    vints = args.b - args.a
    # SCENE-RESET GUARD (LOOP28 87).  0xFFF02A is a PER-SCENE counter: it
    # restarts when the scene does (level end, death, attract rollover).
    # The old `(tb - ta) & 0xFFFF` turned every reset into a huge positive
    # and printed it as a speed — [3000,5500] on the shipping rom read
    # 2546.7%.  A window that crosses a reset measures nothing; say so
    # instead of reporting a number.
    if tb < ta:
        print(f"rom {args.rom}")
        print(f"scene timer f{args.a}={ta} f{args.b}={tb}  "
              f"WENT BACKWARDS: the scene reset inside this window.")
        print("NO SPEED NUMBER. Pick a window inside one scene "
              "(the level-1 script holds one from ~f1200 to ~f4500).")
        raise SystemExit(2)
    dt = tb - ta
    pct = 100.0 * dt / vints
    print(f"rom {args.rom}")
    print(f"scene timer f{args.a}={ta} f{args.b}={tb}  "
          f"game-frames {dt} / vints {vints} = {pct:.1f}%")
    dm = (mb - ma) & 0xFFFF
    print(f"game IRQ4 frame-misses (WRAM 0xFFF144) {dm} = {100.0 * dm / vints:.1f}% of vints "
          f"(a vint the game's pass had not finished by)")
    # DIAG page: [14] cache misses queued (cumulative), [18] build hash
    print(f"build hash 0x{struct.unpack_from(">I", db, 18*4)[0]:08x}  "
          f"DIAG[14] misses a={diag16(da,14)} b={diag16(db,14)} "
          f"delta={(diag16(db,14)-diag16(da,14)) & 0xFFFF}")
    if args.extra:
        ea = open(os.path.join(out, "a_extra.bin"), "rb").read()
        eb = open(os.path.join(out, "b_extra.bin"), "rb").read()
        n = len(ea) // 4
        va = struct.unpack(f">{n}I", ea); vb = struct.unpack(f">{n}I", eb)
        d = [(y - x) & 0xFFFFFFFF for x, y in zip(va, vb)]
        print("extra u32 deltas:", d)
        print(f"  per vint: {[round(x / vints, 2) for x in d]}  per game-frame: {[round(x / max(dt,1), 2) for x in d]}")
    print(f"dumps in {out}")


if __name__ == "__main__":
    main()
