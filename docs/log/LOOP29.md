# LOOP29 — the overnight ladder above opt1 (2026-09-09)

Continues LOOP28 (last entry 106). Protocol: `docs/handoff/LOOP-NIGHT-0909.md`.
Ledger: `docs/log/night-0909/LEDGER.md` (rig: `tools/night_run.py`; roms in
`rom/night/`, gitignored). Entries carry `date` output, the build line, md5,
the sliced speed table, and one-sentence conclusions. NEGATIVE RESULTS are
marked in their headings.

All speed numbers: `tools/gameplay_speed.py` semantics on the level-1
script, [1500,4100] total plus four 700-vint windows; 100% = 60 fps.

## 107. A1: THE ACCEPTED LINE REPRODUCES AT 49.7% (01:36-01:38)

    make clean; make ship-us FBXPORT=1       md5 1812b490   _end 0x60135d0
    build c2ef98f8 (HEAD after the rig commits), stamp R60

    total [1500,4100]   49.7%      IRQ4 misses 50.3% of vints
    1500-2200           49.7
    2200-2900           51.6
    2900-3600           50.0
    3600-4100           46.8
    anim mean           11.7  (windows 11.7 = T_singlebuf, LOOP28 93)
    black% f2000/3000/4000   4.4 / 4.4 / 4.4
    SPRLATE[3] ramp draws    1919 over 2600 vints   (the baseline for B)

Frame f2000 read: level-1 intro (Zeus, "I command you to rise"),
sprites and text intact. START-HERE's number and LOOP28 93's anim
figure both reproduce on today's source, so the rig and the tree agree
with the record. Rig cost: 148 s including the clean build.

Note: the rom that was in `rom/s16.32x` before this tick carried the
FBXSTAGE+FBXBOTH stamp (a double-buffer probe, 46.1%, black 17.9%);
it is in the ledger as `smoke_current` and is not a result.
