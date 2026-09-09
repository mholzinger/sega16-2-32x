# OVERNIGHT LOOP 2026-09-09 — protocol

Started 01:30 EDT. Mike is asleep. This file is re-read at the top of
every tick. `docs/handoff/START-HERE.md` outranks it; where they
disagree, START-HERE wins.

## THE BAR

`tools/gameplay_speed.py` on the level-1 script: 100% = 60 fps.
Nothing else ranks a build. `anim_rate`, black fraction, colour counts,
ramp draws and `attract_parity` are CORRECTNESS FLOORS a candidate must
clear, never a ranking.

    accepted line   make ship-us FBXPORT=1                         49.7%
    opt1 line       + TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1  82.3%  (LOOP28 header, md5 b62237e5)
    rotor ceiling   opt1 + PALROTOROFF=1                            94.7%  (LOOP28 86; colours freeze, not shippable)
    the bar                                                        100%

opt1 is not accepted because Mike's play pass failed TXTWRAM on
2026-09-07 (sprites drawn in the shadow ramp) and the three LATE* fixes
that answer it await his pass. Its 82.3% is real and reproduces clean.

## ONE TICK = ONE EXPERIMENT

1. `date`. Read the tail of `docs/log/LOOP29.md` and
   `docs/log/night-0909/LEDGER.md`. The queue below says what is next;
   the log says what is done.
2. Run it through `tools/night_run.py TAG --flags "..." --base BASE`
   (clean build, sliced speed, guards, ledger row, rom in `rom/night/`).
   For a timeline question use `tools/frame_timeline.py` on
   `rom/night/TAG.32x`.
3. LOOK at `rom/night/TAG_f2000.png` (Read it) before writing a number
   down as a result.
4. Write the LOOP29 entry: number, `date`, build line, md5, the table,
   the conclusion in one sentence, what it rules in or out. Dry voice:
   claim, number, file:line. Use the Write/Edit tools for source and
   docs so the auto-commit hook stamps the time.
5. `ScheduleWakeup` 60 s with this same prompt. At 07:30 or later go to
   WRAP-UP instead.

Budget: a candidate gets at most 45 minutes. A lever with no measured
prize of 6+ points does not get a real implementation; build the
ceiling probe first.

## THE QUEUE

### A. Ground truth (do all four before anything in B)

- A1 `base`: `FBXPORT=1`. Expect 49.7. This is the accepted rom.
- A2 `opt1`: `FBXPORT=1 TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1`,
  `--base base`. DONE: 85.2 (LOOP29 108). md5s never reproduce across
  commits (the git hash is baked in). `diff vs base` is a stale-bake
  test only between SAME-flag siblings.
- A3 timelines: `frame_timeline.py` on both roms, windows 2000-2012 and
  3000-3012. Produce ONE table per rom: per vint, irq4 line, miss y/n,
  raise, drop, gate spin lines, idle line. Then the attribution for the
  missing vints: how many lines over 262, and of those how many are FM
  spin vs work. This table is what B chooses from. Write it in full.
- A4 `opt1_rotoroff`: opt1 + `PALROTOROFF=1`, `--base opt1`. Expect
  94.7. Confirms the palette prize (12.4) on today's source.

### B. The ladder above opt1 (the night's work)

Method for every lever, in this order:
  (i)   price it from the A3 table or a stage trace;
  (ii)  ceiling probe (the term removed, correctness be damned) — if it
        buys < 6 points, log it as NEGATIVE and move on;
  (iii) the real version behind a NEW default-off flag;
  (iv)  gates: speed total AND no window regresses > 6; `anim` mean not
        below opt1 minus 2; black% within 5 and colours within 20% of
        opt1 at all three frames; ramp[3] <= opt1's; LOOK at the frame;
        then `tools/attract_parity.py rom/night/TAG.32x` not worse than
        opt1's scorecard (run this once per surviving candidate);
  (v)   if it survives, it is a candidate for Mike's pass, not a
        result. Say so in the entry.

Seeded levers, from the record. A3 decides the order; do not run them
blind:

- B0 (cheap, 5 min): `FMLATE=1` on the opt1 line, `--base opt1`. It
  was "no change" on the DREQ line (HANDOFF-SESSION7 3c); the FB
  transport moved the push, so one clean re-measure is owed. If it is
  within 6 points of opt1, it stays dead.
- B1 the palette path (prize 12.4, LOOP28 86). PALNOCMP failed twice
  (74.1 / 57.9: the compare is compression) and PALSTREAK/PALBACKOFF
  found no structure. So the third design needs the stage cost, not
  another sweep. Stage trace recipe: add a probe flag `STAGETRACE=1`
  that writes a stage id to a free WRAM word. Use 0xFF5FF0: the census
  in LOOP28 84 proved 0xFF2200-0xFF5FFF unwritten after boot and unread
  ever, and it sits just below PAL_SHADOW (0xFF6000); the 0xFFA1xx page
  is NOT free (md_main.c:898 keeps a shadow at 0xFFA1C0). Write it at
  every boundary of the vint shim (entry, rotor, compare, selection,
  MD upload, push, tail, exit), then trace that word with
  `--trace-access 0xFF5FF0:0xFF5FF1:stage:2000:2012` and print the
  line of each write. One write per stage, no timing distortion. The
  table of stage lines on the opt1 line is the deliverable; the diet
  is whatever the biggest stage allows.
- B2 the FM=1 span, if A3 shows residual gate spin on opt1. The span
  is the master's raise-to-drop; what is inside it is the blit. Levers
  that exist: `SHIPBLITSHIFT=N` (master/slave split), `DIRTYROW`
  already on. Price first: how many spin lines remain per missing vint.
- B3 our cost inside the game's pass (thunk entries per vint in the
  A3 gate-entry line). If the pass on opt1 is longer than the 147/185
  light/heavy regimes, our access rebasing is the difference; a
  `WRITECOST`-style bound exists (Makefile 1344).

Dead ends, do not re-open (START-HERE): double buffering (FBXSTAGE,
FBXBOTH, FLIPEDGEOFF), PALSTREAK/PALBACKOFF, TEXTCAPMASTER, PALNOCMP,
CLAIMNEW, FBXTAIL, BGBLANK0.

### C. The Plane B name-table bug (from 05:30, or when B stalls; 90 min cap)

START-HERE "THE ONE OPEN BUG". Instrument the Plane B upload against
Plane A's at the title cut (demo f1000 vs title f1834): cells shipped
per window, which cells, on the attract script (no `--input`). The
paths differ from the same pass; the difference is the defect. Fix
behind a default-off flag, gate with `attract_parity.py` (the "eye"
scene column must collapse to 0-3), and LOOK at f1834.

### D. WRAP-UP (07:30, or when the queue is spent, or on 3 consecutive ares failures)

1. `make clean && make ship-us FBXPORT=1` so `rom/s16.32x` is the
   accepted rom again; verify `_end` and the `normal` stamp.
2. Write `docs/handoff/HANDOFF-NIGHT-0909.md`: the ledger table, the
   A3 attribution tables, what survived every gate (with md5 and the
   `rom/night/` path Mike should play), what needs his pass, every
   negative with its number.
3. Add one line under THE WORKING LOG in START-HERE pointing at it.
   Change nothing else in START-HERE; the accepted line is Mike's call.
4. `ScheduleWakeup` with `stop: true`.

## HARD RULES

- Ares only. No MiSTer (no scp/curl to mister.office.local), no
  hardware. MAME only with `-window -resolution 160x120
  -keyboardprovider none -nomouse -nojoystick -bench N`.
- No `git push`. Wip commits via the hook are the clock.
- Every build through `night_run.py` (clean build, md5, sliced windows).
  A speed number without md5 and windows is not written down.
- Trust a gap of 6+ points only, and only if no window contradicts it.
  A 1.3 MB byte-diff between siblings is a stale bake: rebuild.
- Every code change behind a new default-off flag with a Makefile
  header comment in the existing style. Never edit the shipping default
  path. Never modify gameplay_speed.py, anim_rate.py,
  frame_timeline.py, attract_parity.py.
- Region guard: `_end` < 0x06019000 (night_run records it).
- No claims from a total read as a rate; divide first.
- If something cannot be measured, the next step is the probe that
  bounds it, not a paragraph.
