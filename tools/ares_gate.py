#!/usr/bin/env python3
"""ares-headless dump decoder + CI gate (LOOP24; HANDOFF-ARES.md).

Consumes the two raw dumps produced by the ares-debug harness:

    ares-headless --frames N \
        --dump sdram:0x28000:0x1000:diag.bin \
        --dump wram:0xFFA000:0x2000:wram.bin  rom.32x

and decodes them with the SAME address map as tools/state_health.py
(the savestate reader) — the layout is authoritative in sh_src/m_main.c
and md_src/md_main.c; the harness never parses a field (the contract).

    ares_gate.py report   diag.bin wram.bin            human-readable
    ares_gate.py json     diag.bin wram.bin            metrics JSON
    ares_gate.py baseline diag.bin wram.bin -o f.json  write baseline
    ares_gate.py gate     diag.bin wram.bin f.json [--hash HEX]
                                                       exit 1 on fail

Gate rules (from HANDOFF-ARES Q1; per-scene baselines — only compare
runs with the same frame count and input script):
  identity   DIAG[18] == --hash when given (wrong-ROM check)
  skips      DIAG[7] == 0, always (a skip is a dropped frame)
  cadence    <= baseline + 0.10          (vints/cycle; THE metric)
  flip-late  rate <= baseline + 1.0pp    (the tear family, DIAG[31])
  rejects    rate <= baseline + 2.0pp AND < 15% absolute
             (the absolute cap is a way-out-of-family guard only:
              gameplay runs ~6%, attract scenes ~9-10% — the REAL
              bound is the per-scene baseline, so never tighten the
              cap back to a gameplay number)
  68K budget handler_mean and window_ack_mean <= baseline * 1.10
"""
import json
import struct
import sys

WRAM_BASE = 0xA000                       # dump starts at bus 0xFFA000
LINES = 46.0                             # FRT ticks per scanline


def load(diag_path, wram_path):
    d = open(diag_path, "rb").read()
    w = open(wram_path, "rb").read()
    if len(d) < 0x100:
        sys.exit(f"{diag_path}: too short for the DIAG block")
    if len(w) < 0x2000:
        sys.exit(f"{wram_path}: expected 0x2000 bytes from 0xFFA000")
    return d, w


def metrics(d, w):
    def D(i):
        return struct.unpack(">I", d[i * 4:i * 4 + 4])[0]

    def w16(a):
        o = a - WRAM_BASE
        return struct.unpack(">H", w[o:o + 2])[0]

    def w32(a):
        o = a - WRAM_BASE
        return struct.unpack(">I", w[o:o + 4])[0]

    vints = w16(0xB0F0)
    cycles = D(9)
    wn = w16(0xA03C)
    m = {
        "build_hash": f"{D(18):08x}",
        "vints": vints,
        "cycles": cycles,
        "cadence": round(vints / cycles, 3) if cycles else None,
        "skips": D(7),
        "flip_late": D(31),
        "flip_late_rate": round(D(31) / cycles, 4) if cycles else None,
        "rejects": w16(0xB0FC),
        "reject_rate": round(w16(0xB0FC) / vints, 4) if vints else None,
        "handler_mean": round(w32(0xB0D0) / vints, 1) if vints else None,
        "window_ack_mean": round(w32(0xA040) / vints, 1) if vints else None,
        "consume_mean": round(w32(0xA038) / wn, 1) if wn else None,
        "consume_max": w16(0xA03E),
        # VISRFLIP family (all zero on non-probe builds; see LOOP24.md)
        "visr": {
            "fires": D(49),
            "isr_flips": D(58),
            "body_fallback": D(56),
            "stale": D(59),
            "nopost": D(60),
            "k1_seen": D(61),
            "span_mean_lines": round(D(63) / D(58) / LINES, 1) if D(58) else None,
            "span_max_lines": round(D(62) / LINES, 1) if D(58) else None,
            # K2FREE family (zero on plain VISRFLIP builds)
            "flip_pos_mean_lines": round(D(45) / D(58) / LINES, 1) if D(58) and D(45) else None,
            "flip_pos_max_lines": round(D(46) / LINES, 1) if D(46) else None,
            "late_k2": D(47),
        },
        "push_aborts": w16(0xB0E0),
        "consumeB_deferrals": w16(0xB0EE),
    }
    return m


def gate(m, base, want_hash):
    fails = []

    def chk(ok, line):
        print(("PASS " if ok else "FAIL ") + line)
        if not ok:
            fails.append(line)

    if want_hash:
        chk(m["build_hash"] == want_hash.lower().removeprefix("0x"),
            f"identity: hash {m['build_hash']} vs expected {want_hash}")
    chk(m["skips"] == 0, f"skips == 0 (got {m['skips']})")
    chk(m["cadence"] is not None
        and m["cadence"] <= base["cadence"] + 0.10,
        f"cadence {m['cadence']} <= {base['cadence']} + 0.10")
    chk(m["flip_late_rate"] is not None
        and m["flip_late_rate"] <= base["flip_late_rate"] + 0.010,
        f"flip-late rate {m['flip_late_rate']} <= "
        f"{base['flip_late_rate']} + 0.010")
    chk(m["reject_rate"] is not None and m["reject_rate"] < 0.15
        and m["reject_rate"] <= base["reject_rate"] + 0.020,
        f"reject rate {m['reject_rate']} < 0.15 and <= "
        f"{base['reject_rate']} + 0.020")
    for k in ("handler_mean", "window_ack_mean"):
        chk(m[k] is not None and m[k] <= base[k] * 1.10,
            f"{k} {m[k]} <= {base[k]} * 1.10")
    return fails


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    mode, diag_path, wram_path = argv[1], argv[2], argv[3]
    m = metrics(*load(diag_path, wram_path))
    if mode == "json":
        print(json.dumps(m, indent=2))
    elif mode == "report":
        v = m["visr"]
        print(f"BUILD {m['build_hash']}  vints={m['vints']} "
              f"cycles={m['cycles']} cadence={m['cadence']}")
        print(f"skips={m['skips']} flip-late={m['flip_late']} "
              f"({(m['flip_late_rate'] or 0) * 100:.1f}%/cycle) "
              f"rejects={m['rejects']} "
              f"({(m['reject_rate'] or 0) * 100:.1f}%/vint)")
        print(f"68K handler mean={m['handler_mean']} lines "
              f"(window/ack {m['window_ack_mean']}) "
              f"consume mean={m['consume_mean']} max={m['consume_max']}")
        if v["fires"]:
            print(f"VISRFLIP fires={v['fires']} isr-flips={v['isr_flips']} "
                  f"fallback={v['body_fallback']} stale={v['stale']} "
                  f"nopost={v['nopost']} k1={v['k1_seen']} "
                  f"span mean={v['span_mean_lines']} "
                  f"max={v['span_max_lines']} lines (vblank=38)")
    elif mode == "baseline":
        if "-o" not in argv:
            sys.exit("baseline: need -o <file>")
        out = argv[argv.index("-o") + 1]
        with open(out, "w") as f:
            json.dump(m, f, indent=2)
            f.write("\n")
        print(f"baseline -> {out}")
    elif mode == "gate":
        if len(argv) < 5:
            sys.exit("gate: need a baseline JSON")
        base = json.load(open(argv[4]))
        want = argv[argv.index("--hash") + 1] if "--hash" in argv else None
        fails = gate(m, base, want)
        if fails:
            print(f"GATE FAILED ({len(fails)}):")
            for f in fails:
                print("  " + f)
            sys.exit(1)
        print("GATE PASSED")
    else:
        sys.exit(f"unknown mode {mode}\n{__doc__}")


if __name__ == "__main__":
    main(sys.argv)
