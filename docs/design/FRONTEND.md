# Optional console frontend

Ordinary boot enters the arcade attract sequence. Hold B during boot, or hold
B and press Start at the arcade title, to enter options. The game's existing
Start/Mode alias also works for cores that report Start as Mode.

Up/down selects a row; left/right changes a setting. A or Start confirms.
B returns to the arcade. Round selection applies to the next credited game:
return to the title and use the ordinary coin/start controls. No credits are
added or spent by opening, using or leaving options.

Options include starting round1-5, difficulty, lives, controller test and restore
arcade defaults. ARCADE preserves this port's established DIP54 configuration;
it does not silently select the Genesis port's defaults. Explicit difficulty
choices are Easy/Normal/Hard/Hardest. Lives offers the arcade's2/3/4 choices.
Settings last for this running session; cold boot restores the defaults.

The controller page retains both ports, raw handshake pairs, decoded buttons,
type checks, counters and history. B freezes/resumes; C clears counters/history.
Start or A+C returns to options, allowing a return even when a core binds Start
as Mode. Mode remains visible as a testable button. This does not change the
six-button acceptance rule or claim to repair the reported physical-pad issue.

## Ownership and resume

`md_src/frontend.c` handles entry and MD interrupt ownership. `frontend.h`
defines a mailbox protocol independent of the game's object addresses:

| COMM12 | Owner/action |
| --- | --- |
| FE00 | MD requests the frontend |
| FE01 | Master grants after the slave has quiesced |
| FDxx | MD has restored video state and returns selected settings |
| 0000 | Master has applied settings and releases both CPUs |

COMM6 carries title eligibility after the boot heartbeat, or FE02 when the
slave has stopped at a completed frame boundary. The old CRAM readback verdict
stays in DIAG2; its COMM12 mirror is disabled when the frontend owns that word.
The supported adapter is the C port with live pads, MD idle consume and the
R60 slave-owned frame loop. Build checks reject other loop configurations.

The master stops game ticks; the slave completes its current packet/span/flip
before granting the menu. Cold-boot entry can stop before the first render pass
and before the converted arcade program boots. Interrupt clocks continue while
the menu is open, but game ticks do not accumulate for later replay. The boot
acknowledgement remains live even when the frontend is holding the game.

The MD owns video and controller reads during the menu. It uses window table
D000-DFFF and a font atF400-F99F, outside game art belowC000, plane tables at
C000/E000, sprite attributes atF000 and hscroll atFC00. The font uses opaque
black pen2 and white pen1. CRAM0-2, the disabled-window registers and the32X
display mode are restored before release. Both32X framebuffer banks remain
untouched, so the renderer's kept pixels and caches still describe their data.
Long menu helpers remain outside the scarce SH-2 hot-code slot.

The existing test-cartridge font, text formatting and controller page are
shared with the frontend. `TCPADTEST` remains a separate standalone diagnostic;
it suppresses the game frontend flag and still exits only by reset.

## Game adapter and checks

The options rows and `port_frontend_settings` are the Altered Beast adapter.
Difficulty and lives use native DIP fields, preserving coinage, starting-credit
cost and player meter. Updating lives also refreshes the arcade's boot-latched
starting-lives byte through the native ROM table. Starting-round selection uses
the normal new-game round-initialization window; it does not force forms, clear
enemies, change health or redirect a running stage.

Run real-pad menu regression cases with:

```
python3 tests/frontend/check_frontend.py build/port/rom/sw334.32x --out build/frontend-check
```

These exercise boot/title entry, controller return, repeated entry, credit
isolation, all five rounds, difficulty/lives and defaults. Follow with the full
ordinary-game gates in HANDOFF-CODEX. Menu-open ticks intentionally pause, so
tick/vint soak checks apply to ordinary launches without menu input.

Sound test, per-stage beast selection and Genesis-style stage restart remain
later features; they are not exposed as unfinished menu rows.
