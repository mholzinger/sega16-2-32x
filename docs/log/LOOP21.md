# LOOP 21 — FLICKER FUSION (the Zeus fix, cadence-independent)

Read `LOOP20.md`'s top block first; this loop builds its "next item".
Status: **BUILT, MAME-GATED, AWAITING MIKE'S ARES PASS** of
`rom/test/R_flickfuse.32x` (stamped FLICKFUSE).

## THE MECHANISM, MEASURED (arcade oracle — this reframes LOOP20's design)

`tools/zeus_list_probe.lua` (mame altbeast, oracle_shots cadence, raw
object RAM every frame over the round-1 intro) says LOOP20's "50% duty
alternation" model was wrong in two ways that matter:

1. **OFF frames REMOVE the record from the list** (end marker moves to
   slot 1). There is no hide bit to sample — presence itself is the
   signal, so any detector must track records across frames, not watch
   a flag.
2. **Duty is not fixed 50% — it is a GRADED ALPHA ENVELOPE**: fade-in
   ramps ~1/8 -> 1/4 -> 1/3, the hold runs at **2-in-3** (`.##` period
   3), fade-out ramps back down. Plus recurring ~15-frame FULL-OFF gaps
   (the lightning flashes) that are real content, not dither.
   The whole apparition (Zeus + orb) is ONE zoomed record: slot 1,
   colourset 3, addr F0C9 stable through the hold, zoom word breathing
   0x042<->0x063 (the 45<->49px pulse LOOP20's mamecap analysis saw).

Period 3 at the hold means 30Hz does NOT hard-lock there (2 and 3 are
coprime — it flutters at ~10-15Hz); only the period-2/4 fade phases
lock, and their period wobbles (4,4,4,3...), so lock is transient.
Mike's "solid or invisible on P" was the fade-phase lock.

## THE BUILD: `make ... FLICKFUSE=1` (requires FBSPR)

Master, at the k1 snapshot fill (`flick_update`): tracks ZOOMED records
across windows (4 trackers, match = colourset + position within
+-24/+-32; shadow 0x3F excluded). A tracker keeps an 8-window presence
history; while the history is TOGGLING (>=3 transitions, sticky), the
record renders EVERY window at coverage = on-ratio r/8:

  - present this window: `flick_lvl[slot] = r`.
  - absent: the held copy is INJECTED back into SPR_SNAP (terminator
    moved) — the dither, not the game's duty, does the fading.
  - r==8 steady: sticky clears, normal draw. r==0: nothing drawn (the
    lightning gaps render dark, as the arcade does); tracker survives
    16 absent windows so re-appearance resumes the stipple instantly
    instead of paying 2-3 windows of re-classification blink.

Compose: `flick_lvl[i]` nonzero routes the record down the zoomed
scalar path with `ZNIB_F` = ZNIB + `bay[y&3][x&3] < 2r` (16-byte 4x4
Bayer, .bss). Non-flicker sprites take the exact same code as before;
without the flag the shipping rom is byte-identical (`_end 0x06018138`,
statics title 2.44 dx=0 / eyehold 3.37 dx=0 exact, re-run this loop).

## GATE (MAME, valid here: FBSPR restored sprite rendering; this is
## STRUCTURE, not colour — canonical stays colour-blind in MAME)

`tools/flick_gate.lua` + consecutive-frame diff over the Zeus bbox,
frames 1080..1400, R_flickfuse vs Q_cut30_harvest (only flag differs):

                      baseline    FLICKFUSE
    mean diff px        1372         387
    frames > 2000px      104          20

Baseline: full ~4400px appear/vanish every cycle through the hold.
FLICKFUSE: steady stipple; Zeus reads as uniform translucency with the
"I COMMAND YOU..." text legible THROUGH him (f_1250 A/B). Remaining
>2000px events sit at the blackout edges — hard cuts in the arcade too.

## KNOWN LIMITS (accepted for v1, listed for the ares read)

  - **On-ratio under-samples true duty during even-period fade phases**
    (30Hz sees every 2nd frame): fade-in can read ~2x bright (r=4/8 vs
    true 1/4) until the phase wobbles. Correct fix if Mike sees it: a
    per-vint presence signature from the 68K on the off-vint (comm
    word), which also future-proofs 60Hz. Not built — measure first.
  - Tracker class is ZOOMED records only. The known flicker family
    (apparitions) is zoomed; widen the filter only on evidence.
  - Gated (pp<=1) flicker records lose the priority gate while
    stippled (none exist in practice — Zeus is pp=2).

## NEGATIVE RESULTS

  - **Slot collisions 7 and 8, both built and caught by the peek probe
    (`tools/flick_peek.lua`)**: (a) 0x3A680 "FBCLEAR spare tail" is
    ROWLIVE's [232] under DIRTYROW — canonical wipes it every cycle;
    (b) 0x3E780 (past pri_lut, "unclaimed to 0x3F000") is inside the
    master's LIVE STACK — state churned every frame. The 0x28D00 hole
    is also full (see the audit at m_main.c ~2544). Fixed-block real
    estate is EXHAUSTED; new state goes in .bss against the region
    guard (was 0x18bc8 -> 0x18c90 with the flag, ~880B spare).
  - First A/B ran with the ROWLIVE collision and showed "no effect":
    a wiped tracker never classifies. The peek probe found it in one
    run — instrument state, do not stare at pixels.

## STILL OWED (carried from LOOP20, unchanged)

  - Mike's ares pass of R_flickfuse (this loop's decisive gate), and
    the Q savestate for the 30Hz falsifiers.
  - Cutscene recheck since CUTBLANK; punch-list residue (oracle-first).
  - The 30->60 road: window ~48 lines, push ~21 (palette coverage
    design), zoomed compose path uninstrumented.

## V2 — THE ARES VERDICT'S FIXES (R2_flickfuse.32x, BUILD 2a59a8f5)

Mike's corpus read (screenshots/, 9697 frames): items 1/2 = the known
slow load-in (tile cut drain, punch list, untouched here). Items 3/5
("stalled image, flipped colour palette, banding") diagnosed from
frame_000800: **a giant stale GHOST at the hold position beside the
real shrinking Zeus** — a scale step past the ±24/±32 match tolerance
orphaned tracker A (which kept injecting its held copy for up to 16
windows) while tracker B took the real record. Item 4's stutter =
level jumps (r moves 2-3/window) + flip/blit skips 3.6% (state
R_flickfuse.bs1; flick_update runs in-window and window/ack read 75.4).

Four changes:
  1. **Two-tier match**: position first, then same-colourset ANYWHERE —
     a scale jump MOVES the tracker instead of orphaning it. Ghost
     class dead at the source.
  2. **Injection capped at age<=4** (~8 frames): longer absences are
     content (lightning blackouts, despawns), not dither.
  3. **Early-exit**: no zoomed record + no live tracker + nothing to
     clear -> flick_update returns before touching SPR_SNAP. The 68K's
     FM wait pays zero outside apparition scenes.
  4. **Coverage slew** (1 step/window, ramp from 1) kills brightness
     stutter; **temporal Bayer phase** (pattern shifts one column per
     window parity) turns the static screen-door into shimmer — the
     closest 30Hz gets to the arcade's flash.

MAME gate: same-phase (4-apart) Zeus-bbox diffs — V2 999 mean/42 big
vs V1 960/44 vs baseline 2538/183. Adjacent diffs rise by DESIGN (the
shimmer alternates the drawn set; judge steadiness on the 4-apart
number). Shipping rom untouched: `_end 0x06018138`.

Still open on this item: Mike's ares pass of R2; the handler cost has
NO Q-state baseline to compare 106.6 against (Q savestate still owed).
