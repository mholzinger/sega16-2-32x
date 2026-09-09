#!/usr/bin/env python3
"""NIGHT RIG (2026-09-09): one candidate build in, one ledger row out.

    tools/night_run.py TAG --flags "FBXPORT=1 TXTWRAM=1 ..." [--base TAG]
    tools/night_run.py TAG --nobuild rom/night/x.32x            (measure only)

Per candidate, in order, every step recorded even when a later one fails:
  1. make clean; make ship-us <flags>         (HANDOFF-PIPELINE section 0:
     a dirty build inherits the previous flag set's asset bake)
  2. region guard (`_end` in rom/s16.lst < 0x06019000), build_id, md5,
     the .build_flags stamp
  3. rom/s16.32x -> rom/night/TAG.32x  (rom/ is gitignored; the shipping
     path is never measured in place and never left pointing at a probe
     without a final `make ship-us FBXPORT=1`)
  4. gameplay_speed.run() at f1500/2200/2900/3600/4100 -> four 700-vint
     windows + the canonical [1500,4100] total (LOOP28 88: slice before
     believing a gap; the scene-reset guard applies per window)
  5. anim_rate.py (a FLOOR for correctness, never a ranking; START-HERE)
  6. frames f2000/f3000/f4000: black fraction + distinct colours of the
     active area -> rom/night/TAG_fN.png.  Guards only.  LOOK AT THEM.
  7. SPRLATE[3] shadow-ramp draws, [12]/[13] relocations, over [1500,4100]
     (Mike's failed TXTWRAM play pass correlated with [3]: 19 ship / 247
     failed / 114 opt1)
  8. --base TAG: bytes differing from that tag's rom. Same flags should
     differ by < 16 bytes; ~1.3 MB apart = a stale bake, rebuild.
  9. append a JSON line to docs/log/night-0909/ledger.jsonl and a row to
     docs/log/night-0909/LEDGER.md

The measured tools (gameplay_speed, anim_rate, frame_timeline,
attract_parity) are imported or called, never modified here.
"""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gameplay_speed as gs

NIGHT = os.path.join(ROOT, "rom", "night")
LEDGER_DIR = os.path.join(ROOT, "docs", "log", "night-0909")
FRAMES = [1500, 2200, 2900, 3600, 4100]
SHOTS = [2000, 3000, 4000]
SPRLATE = "0x3A7D8:0x28"       # sh_src/m_main.c:610: the lean (ship) build parks
                               # SPRLATE[0..9] at 0x3A7D8; [3] = shadow-ramp draws
REGION_LIMIT = 0x06019000


def sh(cmd, log=None, check=False):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if log:
        with open(log, "a") as f:
            f.write(f"$ {' '.join(cmd)}\n{r.stdout}{r.stderr}\n")
    if check and r.returncode:
        raise RuntimeError(f"{' '.join(cmd)} -> {r.returncode}\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return r


def region_end():
    try:
        for line in open(os.path.join(ROOT, "rom", "s16.lst")):
            if line.rstrip().endswith(" _end"):
                return int(line.split()[0], 16)
    except FileNotFoundError:
        pass
    return None


def frame_stats(png):
    from PIL import Image
    im = Image.open(png).convert("RGB")
    w, h = im.size
    if w >= 1345 and h >= 243:                 # ares overscan frame
        im = im.crop((65, 19, 65 + 1280, 19 + 224)).resize((320, 224))
    px = list(im.getdata())
    black = sum(1 for p in px if max(p) < 16) / len(px)
    return round(100 * black, 1), len(set(px))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--flags", default="")
    ap.add_argument("--nobuild", default=None, help="measure this rom, skip the build")
    ap.add_argument("--base", default=None, help="tag whose rom to byte-diff against")
    ap.add_argument("--note", default="")
    a = ap.parse_args()
    os.makedirs(NIGHT, exist_ok=True)
    os.makedirs(LEDGER_DIR, exist_ok=True)
    t0 = time.time()
    row = {"tag": a.tag, "flags": a.flags, "note": a.note,
           "start": time.strftime("%Y-%m-%d %H:%M:%S")}
    log = os.path.join(NIGHT, f"{a.tag}.log")
    open(log, "w").close()
    rom = os.path.join(NIGHT, f"{a.tag}.32x")

    # 1-3. build
    if a.nobuild:
        shutil.copyfile(a.nobuild, rom)
        row["built"] = False
    else:
        sh(["make", "clean"], log)
        r = sh(["make", "ship-us"] + a.flags.split(), log)
        row["built"] = r.returncode == 0
        if r.returncode:
            row["error"] = f"build failed rc={r.returncode}; see {log}"
            finish(row, t0)
            return 1
        end = region_end()
        row["region_end"] = hex(end) if end else None
        row["region_ok"] = bool(end and end < REGION_LIMIT)
        row["build_flags"] = open(os.path.join(ROOT, ".build_flags")).read()
        shutil.copyfile(os.path.join(ROOT, "rom", "s16.32x"), rom)
    row["rom"] = os.path.relpath(rom, ROOT)
    row["md5"] = hashlib.md5(open(rom, "rb").read()).hexdigest()
    row["size"] = os.path.getsize(rom)
    bid = sh(["python3", os.path.join(HERE, "build_id.py"), "show", rom], log)
    row["build_id"] = (bid.stdout.strip().splitlines() or [""])[0]
    if a.base:
        b = os.path.join(NIGHT, f"{a.base}.32x")
        if os.path.exists(b):
            c = subprocess.run(["cmp", "-l", rom, b], capture_output=True, text=True)
            row["diff_bytes_vs_base"] = len(c.stdout.splitlines())
            row["base"] = a.base

    # 4. speed, sliced
    out = os.path.join(NIGHT, f"{a.tag}_gs")
    os.makedirs(out, exist_ok=True)
    timers, misses, diags, extras = {}, {}, {}, {}
    try:
        for n in FRAMES:
            shots = SHOTS if n == FRAMES[-1] else ()
            t, d, m = gs.run(rom, n, out, f"f{n}", shots=shots, extra=SPRLATE)
            timers[n], misses[n], diags[n] = t, m, d
            extras[n] = open(os.path.join(out, f"f{n}_extra.bin"), "rb").read()
    except SystemExit as e:
        row["error"] = f"ares failed in speed run: {e}"
        finish(row, t0)
        return 1
    win = {}
    for lo, hi in zip(FRAMES, FRAMES[1:]):
        dt = timers[hi] - timers[lo]
        win[f"{lo}-{hi}"] = None if dt < 0 else round(100.0 * dt / (hi - lo), 1)
    dt = timers[FRAMES[-1]] - timers[FRAMES[0]]
    vints = FRAMES[-1] - FRAMES[0]
    row["speed_total"] = None if dt < 0 else round(100.0 * dt / vints, 1)
    row["speed_windows"] = win
    dm = (misses[FRAMES[-1]] - misses[FRAMES[0]]) & 0xFFFF
    row["irq4_miss_pct"] = round(100.0 * dm / vints, 1)
    # Speed by the game's own miss counter (START-HERE: speed = 100 - miss
    # rate). The scene timer can STALL with zero misses (LOOP29 110:
    # rotor-off window 3600-4100 read 62.8 by the timer, 100.0 by misses),
    # so both are kept; when they disagree, the run diverged in content.
    row["miss_speed_windows"] = {
        f"{lo}-{hi}": round(100.0 - 100.0 * ((misses[hi] - misses[lo]) & 0xFFFF) / (hi - lo), 1)
        for lo, hi in zip(FRAMES, FRAMES[1:])}
    row["miss_speed_total"] = round(100.0 - row["irq4_miss_pct"], 1)
    row["timers"] = {str(n): timers[n] for n in FRAMES}
    row["misses"] = {str(n): misses[n] for n in FRAMES}
    row["diag_hash"] = f"0x{struct.unpack_from('>I', diags[FRAMES[-1]], 18 * 4)[0]:08x}"
    sl_a = struct.unpack(">10I", extras[FRAMES[0]])
    sl_b = struct.unpack(">10I", extras[FRAMES[-1]])
    row["sprlate"] = {"ramp3": (sl_b[3] - sl_a[3]) & 0xFFFFFFFF,
                      "raw_a": list(sl_a[:4]), "raw_b": list(sl_b[:4])}

    # 6. frames
    fr = {}
    for s in SHOTS:
        src = os.path.join(out, f"f{FRAMES[-1]}_f{s}.png")
        if os.path.exists(src):
            dst = os.path.join(NIGHT, f"{a.tag}_f{s}.png")
            shutil.copyfile(src, dst)
            black, cols = frame_stats(dst)
            fr[str(s)] = {"black_pct": black, "colours": cols, "png": os.path.relpath(dst, ROOT)}
    row["frames"] = fr

    # 5. presented animation (floor)
    r = sh(["python3", os.path.join(HERE, "anim_rate.py"), rom], log)
    m = re.search(r"at \[([^\]]*)\]: \[([^\]]*)\]", r.stdout)
    mm = re.search(r"mean ([0-9.]+)", r.stdout)
    row["anim"] = {"windows": [int(x) for x in m.group(2).split(",")] if m else None,
                   "mean": float(mm.group(1)) if mm else None}
    finish(row, t0)
    return 0


def finish(row, t0):
    row["wall_s"] = round(time.time() - t0)
    with open(os.path.join(LEDGER_DIR, "ledger.jsonl"), "a") as f:
        f.write(json.dumps(row) + "\n")
    md = os.path.join(LEDGER_DIR, "LEDGER.md")
    if not os.path.exists(md):
        with open(md, "w") as f:
            f.write("# Night ledger 2026-09-09 (tools/night_run.py; roms in rom/night/)\n\n"
                    "Speed = game-frames per vint on the level-1 script, 100% = 60 fps. "
                    "Windows are 700-vint slices of [1500,4100]. anim/black/colours/ramp "
                    "are guards, not rankings. A gap under 6 points is noise (LOOP28 88).\n\n"
                    "| tag | flags | md5 | total% | 1500-2200 | 2200-2900 | 2900-3600 | 3600-4100 "
                    "| miss% | anim mean | black% f2000/3000/4000 | colours | ramp[3] | diff vs base | wall s | note |\n"
                    "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
    w = row.get("speed_windows", {})
    fr = row.get("frames", {})
    def g(k):
        v = w.get(k)
        return "RESET" if v is None and k in w else ("" if v is None else v)
    cells = [row["tag"], f"`{row['flags']}`" if row.get("flags") else "", row.get("md5", "")[:8],
             row.get("speed_total", row.get("error", "")),
             g("1500-2200"), g("2200-2900"), g("2900-3600"), g("3600-4100"),
             row.get("irq4_miss_pct", ""),
             (row.get("anim") or {}).get("mean", ""),
             "/".join(str(fr[s]["black_pct"]) for s in map(str, SHOTS) if s in fr),
             "/".join(str(fr[s]["colours"]) for s in map(str, SHOTS) if s in fr),
             (row.get("sprlate") or {}).get("ramp3", ""),
             row.get("diff_bytes_vs_base", ""), row.get("wall_s", ""), row.get("note", "")]
    with open(md, "a") as f:
        f.write("| " + " | ".join(str(c) for c in cells) + " |\n")
    print(json.dumps(row, indent=1))


if __name__ == "__main__":
    sys.exit(main())
