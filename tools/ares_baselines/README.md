# ares gate baselines

One JSON per (scene x frame-count x input-script), produced by
`tools/ares_gate.py baseline` from ares-headless dumps (see
HANDOFF-ARES.md for the harness contract and gate rules). NEVER
compare across scenes: attract and gameplay have different normal
ranges (rejects ~9-10% attract vs ~6% gameplay; flip-late is
dominated by scene-load page storms in attract).

Naming: `<rom-letter>_<scene>_<frames>.json`. The stored build_hash
records which build cut the baseline; the gate's --hash flag checks
the RUN's rom, not the baseline's.

- `Y_attract_3600.json` — Y_visrflip_flick (BUILD 484e2020),
  no-input cold boot, 3600 frames: boot + title + first demo.
  First-ever headless-ares baseline (2026-08-21).
