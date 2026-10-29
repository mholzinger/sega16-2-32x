# demo-b: the port's game-logic gate

`make check-demo` builds the port out of tree and compares every live object record, and
each live record's hardware sprite-list entry, against the arcade's work RAM at 20 targets
over five scenes. `PICTURE=all` adds the screen-band compare at every target;
`SWEEP=1` adds the full sweep.

## A standing difference gets a pad control before it gets a card

If the gate reports the same difference on two runs, **do not open a card on it until a
no-op layout change reproduces it or fails to.** Build a pad — dead bytes in the section the
suspect change touches, nothing hot moved, no function altered — and gate it identically.
A change that cannot alter behaviour must not alter the gate; if it does, the gate is what
needs fixing, and whatever the card was about is not yet measured.

This is not a hypothetical. A 0074 (39 bytes) and A 00c4 (59 bytes) were reported as the
port's own defect for two days, stable across runs, and were neither: a 688-byte `.ramtext`
pad shifted the port four wall frames, moved the failures to entirely different targets
(A 0140, B 0246) and took the picture from 0 of 20 to 7 of 20. Diffing the two builds at the
same tick *and* the same game frame left 35 bytes, among them `0xFFF01C` — the game's own
frame latch, the cell the arcade's IRQ4 handler self-gates on. The gate had been comparing
dumps taken on opposite sides of the game's frame boundary. With the latch in the key the
gate reads 20 of 20, and reads the same under the pad.

The alignment key is therefore three things, and all three are in the baseline sidecars:
**tick, game frame, and the frame latch.** A comparison that lands on a different value of
any of them is refused, not counted.

## Picture baseline (2026-10-11)

Re-stamped from `rom/port/clean1.32x`, sha1 79fddaa97d7dae58509335080a115f2eb483021f,
commit 86bef0f7 -- the first rom in this project built from a deleted `build/port`. Every
baseline before it was taken against a rom built from a generated unit that had not been
regenerated since d696b228 (eleven hand-file signatures disagreed with `protos.h`;
`cport_prep` only checks on a clean build).
