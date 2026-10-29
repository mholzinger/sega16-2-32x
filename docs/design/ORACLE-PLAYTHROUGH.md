# The arcade playthrough oracle (2026-09-25)

Six of Mike's seven play-pass defects live in scenes no input script reached:
the beast transformation, the round-clear bonus, the crystal ball between
level 1 and 2, level 2 itself. This is the rig that reaches them on ares, with
the arcade's own picture beside ours at every moment.

## Why a recorded playthrough

Our rom runs the game at 50-70% of 60 Hz. An input script keyed to EMULATOR
frames therefore drifts out of the route within seconds. The game itself does
not care: it samples input once per GAME frame. So key the route to the game's
own frame count and both machines take the same route at their own speeds.

  - the arcade's game frame: a video frame whose end finds the frame flag
    (0xFFF01C) clear, i.e. the game consumed that frame's release.
  - ours: `md_main.c` `shim_vblank` counts the same event into the u16 at
    **0xFFA1D0**, zeroed at boot.

`ares-headless --input-gameframe 0xFFA1D0` keys input playback to that counter.

**A press must survive the keying.** The counter STALLS while the game is
mid-pass, so a real 10-frame arcade press can span zero game frames -- the
start button at a scene change did, and the press and its release then
cancelled out on our rom. `inp_dump.lua` keeps each button's keys strictly
increasing, so every press is held for at least one game frame.

## The DIPs must be the port's, and they come from cfg/oracle

`md_main.c:7472` hands the game DSW1 0xFF (1 coin / 1 credit) and DSW2 0xFD
(3 lives, normal, demo sounds on). MAME's own DIP DEFAULTS are exactly those
bytes -- but `cfg/altbeast.cfg` overrides them to 0x66 / 0x54, so the arcade
oracle has been running a different coinage, lives count and difficulty from
the port. `cfg/oracle/altbeast.cfg` is that file with the DIP overrides
removed and the key bindings kept; every oracle run passes

    -cfg_directory cfg/oracle -nowriteconfig

and reads 0xFF / 0xFD, verified. (Forcing the DIPs from lua does NOT work:
`field:set_value` snaps to the settinglist and yields 0xEE / 0x01.)

## Recording (interactive, ~3 minutes of play)

    mame altbeast -rompath ./mame -skip_gameinfo \
        -cfg_directory cfg/oracle -nowriteconfig \
        -input_directory ./inp -record ab_full \
        -autoboot_script tools/record_setup.lua

Play: coin, start, through level 1, take a transformation, the boss, the round
clear, the crystal ball, into level 2. Quit with Esc. The route lands in
`./inp/ab_full.inp`.

## The oracle dump (headless, ~1 minute)

    ID_OUT=$PWD/oracle ID_FRAMES=20000 ID_EVERY=30 \
    mame altbeast -rompath ./mame -skip_gameinfo \
        -cfg_directory cfg/oracle -nowriteconfig \
        -input_directory ./inp -playback ab_full \
        -video none -sound none -nothrottle -window -resolution 160x120 \
        -keyboardprovider none -nomouse -nojoystick -bench 400 \
        -snapshot_directory $PWD/oracle \
        -autoboot_script tools/inp_dump.lua

Writes into `oracle/`:

    state.csv      per frame: game frame, scene timer, step, round, the solo
                   byte (0xFFF148), frame flag, display gate, the layer
                   registers (page words + scroll) from text RAM
    inputs_gf.csv  the route, keyed to the game frame
    snaps.csv      every arcade snapshot with the frame and GAME frame it shows
    altbeast/*.png the arcade's picture, every 30 frames and at every scene edge

## Ours beside it

    tools/gf_align.py rom/s16.32x oracle --tag wc9          # every arcade picture
    tools/gf_align.py rom/s16.32x oracle --edges            # scene edges only
    tools/gf_align.py rom/s16.32x oracle 4200 4260 --tag x  # named game frames

Two ares runs whatever the target count: one logs our game-frame map
(`--gameframe-log`), one takes every screenshot. Writes
`oracle/ours/<tag>_gf<N>.png` beside `arc_gf<N>.png` and prints the arcade's
state per target, so a divergence in the GAME's logic shows before any pixel
is compared.

## What it is and is not

It **reaches the scenes**: verified 2026-09-25 on a scripted recording -- at
game frames 469/1469/2469 the arcade and our rom are in play at the same point
of level 1 with the same enemies and the same score.

It is **not frame-for-frame identity for ever**. Damage and enemy behaviour
diverge as the run lengthens (by game frame 2869 the recording had 1400 points
and two lives, ours 300 and none). Use it to arrive at a scene and to compare
the arcade's picture with ours there; do not read a late-run pixel diff as a
port defect without checking the state columns first.
