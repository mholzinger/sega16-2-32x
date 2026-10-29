# Neff smoke timing on the Miyoo

Normal play: `build/port/rom/sw336.32x`.
Timing diagnostic: `build/port/rom/sw336-timing.32x`.

The timing ROM uses the same game fixes and existing R60RIGSPLIT counters.
Its options frontend is disabled to fit instrumentation in the normal hot-code
memory budget. It starts level one normally; Start-as-coin and ordinary controls
remain. No enemy-clear, forced form, health or starting-round probe is enabled.
The normal sw336 keeps its options and level select.

Play through Neff and record the full game picture during smoke entrance,
ordinary fighting, and the defeat explosion. Keep the bottom28rows visible.
Those white bars identify work done by the game and renderer. If the emulator
can show its actual FPS, including it also distinguishes the emulator slowing
in wall time from the game missing a presentation interval. Neither has yet
been measured on the Miyoo for this build.

For a320x224 game picture, useful rows are:

| Row | Meaning |
| --- | --- |
|199|Sprite plotting, one pixel per emulated scanline of work|
|200|Foreground masking|
|209|Average presentation interval divided by two|
|213|Master game update|
|217|Master background preparation|

The bars add work. Compare smoke with ordinary fighting in this same diagnostic;
do not treat its speed as the normal ROM's exact frame rate. A physical capture
is needed before choosing a performance change. Ares checks only show that the
instrumentation is live and the diagnostic boots.

The level-five scroll-lock report remains separate. A screenshot or clip of the
locked scene in the normal ROM would identify the checkpoint and remaining
visible enemies; the automatic default/Easy combat routes have not reproduced it.
Do not force-clear enemy counts as a repair without identifying the cause.

Local instrument check: sourceb19abf2c; final timing ROM code/data exactly matches
the cold-tested diagnostic outside the64-byte build stamp (12stamp bytes differ).
The2501-frame cold test has live plot/tick/preparation counters and2486vints,
2493ticks,2030slave passes. Normal RAMTEXT cap is respected; no oversized ROM
was run. Physical measurements remain pending.
