# ares against the MiSTer core — 2026-09-27

30 tests, 10 graded (all pass on both), 20 measurements. Values are raster
lines unless noted. The cart carries the ares figures as its REF column and
shows the delta on screen, so on an unprofiled machine the numbers that differ
announce themselves.

## The pattern separates cleanly

| test | ares | MiSTer | delta |
|---|---|---|---|
| 1K WRAM READ | 90 | 93 | +3 |
| 1K CART READ | 90 | 95 | +5 |
| 1K BYTE READ | 90 | 93 | +3 |
| 1K LONG READ | 107 | 111 | +4 |
| 1K FB WRITE | 64 | 68 | +4 |
| 1K FB READ | 90 | 96 | +6 |
| 256 BANKSWITCH | 42 | 43 | +1 |
| FMDL ASSEMBLE | 55 | 57 | +2 |
| FMDL BLAST | 92 | 94 | +2 |
| FMDL LIFT | 68 | 71 | +3 |
| FMDL % OF FRAME | 56% | 57% | +1 |
| 1K VRAM WRITE | 113 | 113 | **same** |
| 1K VRAM READ | 113 | 113 | **same** |
| 1K MD CRAM WRITE | 64 | 64 | **same** |
| DMA256 ACTIVE | 28 | 28 | **same** |
| DMA256 VBLANK | 2 | 2 | **same** |
| DMA256 FILL | 14 | 14 | **same** |

**Every 68K-side memory access is slower on the FPGA. Every VDP-internal
operation is identical.** Not a small distinction: it says the core's 68K bus
model departs from ares while its VDP is cycle-exact against it, and it falls
out of the screen at a glance rather than from a transcription.

## Other things worth having

**The cart penalty exists only on the core.** WRAM and cart both cost 90 lines
on ares; on the core, cart costs 95 against WRAM's 93. That is the ROM DTACK
wait, and a mask-ROM cartridge should show no such gap -- a specific prediction
for the real unit.

**A DMA fill is twice the speed of a WRAM-sourced DMA** (14 lines against 28,
same 256 words, same frame position) and a VRAM-to-VRAM copy matches the WRAM
source at 28. Identical on both machines.

**Byte access to the framebuffer is worse than word access per byte**, not just
per access: 73 lines for 1024 bytes against 64 for 1024 words, so about 2.3x
the cost per byte moved.

**A byte read and a word read cost the same** (90 each) -- correct for a 68K,
where both are one bus cycle. A long read is 107, only 1.19x a word, because
loop overhead dominates at this size.

**A bank switch roughly doubles the cost of the read it enables**: 256
switch-plus-read pairs cost 42 lines against the 22 that 256 plain cart reads
would.

## Still not settled

32X CRAM written during active scan LANDS on both machines. Graded from the PEN
rule and both disagreed, so it is report-only. Real hardware decides.
