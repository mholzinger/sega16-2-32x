# THE ATTRACT FACE SCREEN DRAWS NO FACE (2026-09-15, ares, no rig)

Found offline on the line (`make line`, bldS-equivalent, 17 stamp bytes).

## Alignment

Do not diff two roms at the same frame number -- they run at different
speeds. Align on the game's own clock. Attract, no input, 4000 frames:

    ships 3058 / 4000 = 0.764 of arcade    ->  our frame = arcade x 1.309
    wall 0.91v (mean), max 5.38v

That ratio is confirmed against the game's own attract step (0xFFF031,
the address tools/arcade_pagesel.lua reads) at nine points, every one
inside its predicted window. It also settles which clock gates the 68K:
gated per GENERATION would predict 1.098x and put us AHEAD of the
arcade; gated per SHIPPED FRAME predicts 1.309x and fits. It is the
shipped frame.

## The defect

Classify each centre pixel as a FIELD colour (the screen's blue, red,
orange/yellow, or black) or not. The face is the only thing on that
screen that is not a field colour.

    arcade, face frame          non-field centre pixels  30.9%
    ours, frames 1560..1595     non-field centre pixels   0.0%

Zero, at every sampled frame across the whole screen, so it is not a
phase artefact. The background renders correctly -- blue and red fields
and the flame scrolls are all present and the right colours. The face
alone is missing.

Frames 1605-1640 are then FULLY BLACK (100% of sampled centre pixels),
a sustained blackout of at least 35 frames before the eye screen.

Reproduce:

    A=.../ares-headless
    $A --frames 1620 --screenshot 1585:/tmp/face.png rom/s16.32x
    # and the oracle, no coin:
    mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
      -nothrottle -window -resolution 160x120 -keyboardprovider none \
      -nomouse -nojoystick -bench 40 -autoboot_script tools/attract_steps.lua

tools/attract_steps.lua logs the arcade's step/round/progress timeline,
which is what the alignment above is built on.

## Not yet known

Whether the face is sprites or tiles on this screen, and therefore
whether this is the sprite path or the tile path. That is the next
question and it is answerable from the arcade with no rig.
