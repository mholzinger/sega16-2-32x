# Controller handshake diagnostic

Build with `make candidate NAME=pad333 EXTRA='TCPADTEST=1'` in the
advisor-bisect worktree. The resulting `build/port/rom/pad333.32x` opens the
controller test directly. It does not start Altered Beast. Reset to exit.

The tester reuses this project's test-cartridge display and boot, and the
word/lamp/four-word-history conventions of `~/src/32x-builder/sh_src/m_main.c`
(`pad_test_draw`). It adds samples before the six-button detection gate. No
files in 32x-builder were changed. Its sticky six-button latch was not imported:
we need to see a failed detection rather than conceal it.

Each port shows:

| Field | Meaning |
| --- | --- |
| WORD | Decoded buttons, before arcade input conversion |
| TYPE | Six-button signature accepted, or three-button/absent |
| POLL | Number of polls, wrapping at FFFF |
| MXYZSACBRLDU | Lit letters for pressed buttons; S is Start |
| PHASE LOW HIGH | Actual port bytes from each of the four low/high TH pairs |
| ID3 | Third low phase has the zero low-nibble marker |
| ID4 | Fourth low phase passes the reader's right/left-bit check |
| 6 OK / MISS | Saturating counts of accepted/rejected six-button signatures |
| Four hexadecimal words | Most recent distinct decoded words, newest first |

All numbers are hexadecimal. MISS is expected to increase for a three-button
pad or an empty port; it does not mean the ordinary buttons failed.

Port-one B toggles display freeze; polling and history continue behind it.
Port-one C clears counters and history. Both controls use ordinary three-button
inputs, and there is no six-button shortcut needed to reach the page.

On a failing setup, observe the idle screen, then press X, Y, Z and Mode
individually. A photo of the phase rows and type/counters is useful. Compare
with the same pad on a working setup if available. Six-button detection failing
here points toward the pad protocol, timing or emulator controller selection.
If the standalone page works but the game fails, investigate the game's polling
cadence and input delivery next. This page alone cannot prove the cartridge is
at fault or that production input has been repaired.

The reader keeps its existing detection rule, interrupt masking and delays
inside each edge/read pair. Diagnostic-only trace stores add a few cycles
between complete phase pairs. Each port is polled once per video frame. Both
SH-2s stop after boot acknowledgement, avoiding gameplay and renderer traffic.
The arcade input mailboxes and mapping are not used by this standalone page.

The diagnostic exposed a separate boot-linker defect: `__text_end = ALIGN(2)`
rounded the copy symbol but did not pad `.text`. Its odd size E79 put `.data`
at ROM 8C1289 while the boot copied from 8C128A. Main arrived in work RAM at
FF1263 instead of linked FF1264. `md.ld` now advances the location counter,
pads `.data` to word size and asserts the boot-copy contract. This is a boot
fix, not evidence for the reported six-button failure.

Local evidence: `build/codex-pad-sw333/`. Three 1100-frame ares tapes test
six/three, three/six and absent/absent ports. Type detection, each scripted
button and one poll per video frame pass. Screenshot contact sheet includes
extended-button lamps and freeze/resume. Physical-pad acceptance is pending.
The existing sw333 gameplay ROM remains unchanged. A normal-build comparison
is recorded alongside the diagnostic results.

For repeatable emulator checks, save the matching 68K list beside the ROM as
`pad333.md.lst`, then run:

```
python3 tools/padtest_ares.py build/port/rom/pad333.32x --out build/pad-check
```

At the pad333 milestone, the normal guard's controller and game code was unchanged. The alignment fix
also rounds its boot-copy length from1593 to1594, correctly including the last
initialized byte (`hold_last`, FF) and a padding zero. This is the single
non-stamp byte difference from sw333; it is not a six-button detection change.
The normal guard is for verification, not a new player candidate. Run full game
gates when incorporating the boot fix into the next gameplay candidate.

The optional boot/title frontend now integrates this controller page in sw334.
See `docs/design/FRONTEND.md` for its controls and current handoff for gate status.
Pad333 remains the standalone controller investigation ROM.
