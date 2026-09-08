#!/usr/bin/env python3
"""PALSTATIC v2 glow bake — derive the palette animators from capture.

    python3 tools/glow_bake.py          # regen sh_src/glow_tab.h
    python3 tools/glow_bake.py --stats  # report only

Consumes discover/glow/*.glow — consecutive-frame 2048-word PAL_SH
dumps (headless-ares, one run per frame). The level-1 graveyard glow
is GAME code animating 18 tile-half words every logic tick (census
2026-09-01); the animators are scene- and level-global (Mike's boss
states bs2/bs3 and level-2 bs6 all mid-cycle on the same rings):

  RAMP  0x99-0x9F: 7-word ring of 0x4900+(k<<8), rotates -2/tick
  WAVE  0xA1-0xA5 and 0xA9-0xAD (same values): 16-phase brightness
        march over {0x100F base, 0x305F..0x30DF front}
  BLINK 0x36: irregular 0x100F/0x7FFF — NOT baked (stays 68K-shipped;
        block 1 is unmasked)

Everything here is DERIVED and VALIDATED against the dumps; the bake
fails loudly if the capture stops obeying the rules (a new level's
corpus may differ — regenerate per round).

Emits sh_src/glow_tab.h: the wave phase table, ramp parameters, and
the masked-word lists the SH-2 animator (GLOW_ANIM, m_main.c) plays
at its own 60Hz vint — the ARCADE's rate; the game's own updates are
logic-clocked and stutter at our 73% 68K, so the baked glow is
closer to arcade cadence than the shipped one was.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'discover' / 'glow'
OUT = ROOT / 'sh_src' / 'glow_tab.h'

RAMP = list(range(0x99, 0xA0))          # 7 words
WAVE_A = list(range(0xA1, 0xA6))        # 5 words
WAVE_B = list(range(0xA9, 0xAE))        # mirror group


def main():
    files = sorted(SRC.glob('*.glow'))
    assert len(files) >= 32, "need a consecutive-frame corpus"
    seq = [struct.unpack('>2048H', p.read_bytes()) for p in files]

    # RAMP: every distinct state must be a rotation of the ascending
    # ring, and every update a -2 rotation (mod 7) of the previous.
    ring = [0x4900 + (k << 8) for k in range(7)]
    updates = []
    prev = None
    for s in seq:
        g = tuple(s[i] for i in RAMP)
        if g != prev:
            updates.append(g)
            prev = g
    phases = []
    for g in updates:
        p = [r for r in range(7)
             if all(g[j] == ring[(j + r) % 7] for j in range(7))]
        # mid-write dumps (the 68K streamer caught between words) are
        # not rotations; tolerate them but they carry no phase info
        phases.append(p[0] if p else None)
    good = [(a, b) for a, b in zip(phases, phases[1:])
            if a is not None and b is not None]
    steps = [(b - a) % 7 for a, b in good]
    assert steps and all(s in (5, 3) for s in steps), \
        f"ramp no longer rotates -2/tick: steps {sorted(set(steps))}"
    torn = phases.count(None)
    print(f"  ramp: {len(updates)} updates, rotate -2/tick verified "
          f"({torn} mid-write captures tolerated)")

    # WAVE: a fixed ORDER of states (base, front marching in, full,
    # snap back) with per-state dwells. The frame capture shows the
    # order strictly but the dwells jittered by our slow 68K (logic
    # stutters stretch a state; catch-up bursts can swallow one), so
    # dwells are taken as the MODE across cycles — the game's nominal
    # pattern, which is what the arcade showed at full speed.
    for s in seq:
        assert tuple(s[i] for i in WAVE_A) == \
               tuple(s[i] for i in WAVE_B), "wave groups diverged"
    frames = [tuple(s[i] for i in WAVE_A) for s in seq]
    order = []
    for g in frames:
        if g not in order:
            order.append(g)
    base = frames[0]
    assert base == order[0] and all(v == base[0] for v in base), \
        "capture must start in the wave's base state"
    # order check: every transition advances along the cycle; a
    # 2-step advance is a logic catch-up BURST swallowing one state
    # (seen once in 96 frames) — tolerated and counted, never baked.
    idx = {g: k for k, g in enumerate(order)}
    bursts = 0
    for a, b in zip(frames, frames[1:]):
        if a != b:
            step = (idx[b] - idx[a]) % len(order)
            assert step in (1, 2), \
                f"wave broke order: {idx[a]} -> {idx[b]}"
            bursts += step == 2
    # dwell mode per state, from complete cycles only
    cyc_starts = [k for k, g in enumerate(frames)
                  if g == base and (k == 0 or frames[k - 1] != base)]
    from collections import Counter
    dwell = {g: Counter() for g in order}
    for a, b in zip(cyc_starts, cyc_starts[1:]):
        run, cnt = frames[a], 0
        for g in frames[a:b] + [None]:
            if g == run:
                cnt += 1
            else:
                dwell[run][cnt] += 1
                run, cnt = g, 1
    wave_states = order
    wave_dwell = [dwell[g].most_common(1)[0][0] for g in order]
    print(f"  wave: {len(order)} states, dwells {wave_dwell} "
          f"(= {sum(wave_dwell)}-tick cycle) over "
          f"{len(cyc_starts) - 1} cycles")

    if '--stats' in sys.argv:
        return
    with open(OUT, 'w') as f:
        f.write("/* GENERATED by tools/glow_bake.py — do not edit.\n"
                " * Level-1 glow animators derived from capture; the\n"
                " * SH-2 (GLOW_ANIM) plays them at 60Hz vint — the\n"
                " * arcade's own rate. Regenerate per round. */\n")
        f.write("#define GLOW_RAMP0 0x99   /* 7 words, rotate -2/tick */\n")
        f.write(f"#define GLOW_WAVE_N {len(wave_states)}\n")
        f.write("static const uint16_t glow_wave[GLOW_WAVE_N][5]\n"
                "    __attribute__((section(\".palscenes\"))) = {\n")
        for g in wave_states:
            f.write("    { " + ", ".join(f"0x{v:04X}" for v in g)
                    + " },\n")
        f.write("};\n")
        f.write("static const uint8_t glow_dwell[GLOW_WAVE_N]\n"
                "    __attribute__((section(\".palscenes\"))) = "
                "{ " + ", ".join(str(d) for d in wave_dwell) + " };\n")
        ring = [0x4900 + (k << 8) for k in range(7)]
        f.write("/* doubled ring: word j at phase p = glow_ring2[p+j]"
                " — no runtime %7 (SDRAM-code diet) */\n")
        f.write("static const uint16_t glow_ring2[14]\n"
                "    __attribute__((section(\".palscenes\"))) = "
                "{ " + ", ".join(f"0x{v:04X}" for v in ring + ring)
                + " };\n")
    print(f"wrote {OUT}")


if __name__ == '__main__':
    main()
