# Level-five scroll-lock capture

Latest sw336 retest: Mike confirms the same lock after collecting two white-wolf spirit orbs. The exact collection/escape order is not established.

Mike reached level five by playing through earlier levels. The second white wolf
escaped, all visible enemies were cleared, the player could move/jump, and the
screen would not scroll before Neff appeared. Direct round-five tests have not
reproduced that sequence. The scoped level4-to5 test also advances; the full1-to5 physical route remains to be captured.

`build/port/rom/sw336-lock2.32x` is a diagnostic.
It keeps the options menu and normal progression. Start with the same route and
settings that produced the lock; it does not force a round, form, lives, enemy
counts, or boss health. It writes a footer into the bottom28rows of the picture.
The game RAM values are only read, never changed.

If it locks, the two footer rows identify the camera limit, recorded enemy count,
and live records. A short stable capture is useful because the selected actor
changes about every two emulated seconds, preferring actors marked offscreen.
The footer replaces the lower HUD and adds drawing work. It is a correctness
probe, not a speed measurement; the separate `sw336-timing.32x` measures phases.

| Field | Meaning |
| --- | --- |
| C | Camera position |
| L | Current camera limit |
| E | Game's remaining-enemy count |
| W | White-wolf-active flag |
| K | Scroll-lock flag |
| V | Neff encounter/visit |
| P | Player's target power/form |
| D | Difficulty/lives DIP byte |
| M | Mask of live ordinary actor records16..39 |
| S | Selected actor slot;28 is Neff/boss when the ordinary actor table is empty |
| H | Selected actor's handler address |
| X,Y | Selected actor's world coordinates |
| R | Selected actor's status word;8800 includes drawn,8000 is active without drawn |
| T | Selected actor's state byte |

All values are hexadecimal. Fields are live reads and can straddle one game
update, so several frames are more useful than one transitional picture. The
footer does not try to unlock the screen or remove an enemy.

Local tests execute the real footer against guarded buffers and prove unchanged
game RAM, framebuffer bounds, display-hold ownership, field decoding, and actor
selection/cycling. A4001-frame cold test enters round5 through the actual menu,
shows a readable footer matching its RAM dump, and keeps ticks tracking vints.
Normal guard code/data matches sw336 outside its64-byte build stamp. Final diagnostic source67d710cc, stamp2026-10-28 12:20:51 R60;
SHA2566261eca917496f769024c8b656ad708e5b53f4b24e2bac578431dd45ed21877f.
Its1501-frame cold/menu5capture decodesC102C/E03/M000007/S10/H1650A and
D54, matching live RAM fields with the expected one-tick camera difference.
The empty-table Neff fallback is covered by host execution tests.
