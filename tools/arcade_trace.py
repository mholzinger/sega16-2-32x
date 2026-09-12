#!/usr/bin/env python3
"""Analyse a tools/arcade_trace.lua trace.

Reports, per frame and exactly (no sampling):
  instructions executed, IRQ4 handler length, instructions spent in the
  game's vblank wait loop (0x3982/0x3986), and where the CPU was when
  each vblank arrived.

The arcade 68000 runs at 10 MHz = 166,667 cycles/frame, so
    cycles per instruction = 166667 / instructions-per-frame
is the average cost of the game's own instruction mix ON THE ARCADE,
including its bus wait states. Our 68000 has 7,670,453/60 = 127,841
cycles/frame to do the same work.
"""
import re, sys, collections

# MAME COLLAPSES A TIGHT LOOP into one `(loops for N instructions)` line.
# Counting only the listed lines therefore counts every loop ONCE, and on
# Altered Beast that under-reports the 68000's work by 2.5x to 2.9x
# (LOOP-DECOMPILE 88). Every figure this script printed before that was
# found -- 3691 arcade instructions a frame, 2780 game, 2882 shim, and the
# 45 cycles-per-instruction that followed from them -- was low by that
# factor. The RATIOS were unaffected; the absolutes were not.
LOOPS = re.compile(r"^\s+\(loops for (\d+) instructions\)")
IDLE = (0x3982, 0x3986)
IRQ4 = 0x2AAC
ARC_CYC, MD_CYC = 10000000 / 60.0, 7670453 / 60.0

lines = []
executed = 0                      # listed lines PLUS collapsed iterations
for line in open(sys.argv[1], errors="ignore"):
    m = re.match(r"^([0-9A-Fa-f]{6,8}): (.*)$", line.rstrip())
    if m:
        lines.append((int(m.group(1), 16), m.group(2)))
        executed += 1
        continue
    m = LOOPS.match(line)
    if m:
        executed += int(m.group(1))
        if lines:
            lines.append((lines[-1][0], "(x%s)" % m.group(1)))
irq = [i for i, (pc, _) in enumerate(lines) if pc == IRQ4]
per = [irq[i + 1] - irq[i] for i in range(len(irq) - 1)]
idle = sum(1 for pc, _ in lines if pc in IDLE)
print("instructions traced   %d over %d frames" % (len(lines), max(1, len(irq) - 1)))
print("instructions EXECUTED %d  (collapsed loops expanded; the number that "
      "matters)" % executed)
print("instructions / frame  %s" % per)
if per:
    mean = sum(per) / len(per)
    print("mean                  %.0f" % mean)
    print("arcade cycles/instr   %.1f   (10 MHz, %.0f cycles/frame)" % (ARC_CYC / mean, ARC_CYC))
    print("our budget            %.0f cycles/frame at 7.670 MHz" % MD_CYC)
    print("  the game's frame fits here only if our cycles/instr <= %.1f" % (MD_CYC / mean))
print("idle-loop instructions %d (%.2f%%)" % (idle, 100 * idle / max(1, len(lines))))
at = collections.Counter(lines[i - 2][0] for i in irq if i > 2)
print("CPU location at vblank:", {("%06X" % k): v for k, v in at.items()})
