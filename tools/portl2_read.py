#!/usr/bin/env python3
"""Read the L2 gate's result: one frame of decompiled 68K logic vs the arcade.

    python3 tools/portl2_read.py rom/pl1.32x 200 400 800

The rom seeds 16384 bytes of work RAM from an arcade capture, runs the frame body
(irq4_handler, FUN_00003aae, FUN_0000144a, the object dispatch) on the SH-2, and
diffs all 16384 bytes against the arcade's NEXT frame. A gameplay frame changes
only 40 of those bytes, so a port that does nothing at all scores 40 wrong, and
only a port that does the RIGHT work scores 0.

STAGE is the watchdog: 1 irq4_handler, 2 FUN_00003aae, 3 FUN_0000144a,
4 object dispatch, 5 finished. A rom stuck below 5 is hung in that call.
"""
import os, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
TICK_US = 32.0 / 23.01
VINT_US = 1000000.0 / 59.92
STAGE = {0: "not started", 1: "irq4_handler", 2: "FUN_00003aae", 3: "FUN_0000144a",
         4: "object dispatch", 5: "finished"}

def sym(lst, name):
    for line in open(lst):
        f = line.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s: %s not found -- build with PORTFULL=1" % (lst, name))

rom = os.path.abspath(sys.argv[1])
frames = [int(x) for x in sys.argv[2:]] or [400]
import romsyms
lst = romsyms.lst_for(rom)
addr = sym(lst, "_port_l2_out")
off = addr - 0x06000000

print("_port_l2_out = 0x%08X" % addr)
print("frame | magic    runs stage             | WRONG  match/16384   first wrong | unk hnd | frame cost")
for n in frames:
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "d.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", "sdram:0x%X:0x200:%s" % (off, d), rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        D = open(d, "rb").read()
    g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]
    magic, stage, wrong, first, ticks, unk, match, firstunk, runs = (
        g(0), g(1), g(2), g(3), g(4), g(5), g(6), g(7), g(8))
    slot, cur, entered, returned = g(9), g(10), g(11), g(12)
    us = ticks * TICK_US
    print("%5d | %08X %4d %-17s | %5d  %5d/16384  %s | %3d %5s | %6.0f us %4.1f%% vint"
          % (n, magic, runs, STAGE.get(stage, "?%d" % stage), wrong, match,
             ("0x%06X" % first) if first != 0xFFFFFFFF else "  none  ",
             unk, ("0x%05X" % firstunk) if firstunk else "-", us, 100.0 * us / VINT_US)
          + ("" if magic == 0x504C3220 else "   <-- NO PL2 MAGIC: driver never ran"))
    if stage == 4 and entered != returned:
        print("        HUNG in handler 0x%05X at slot %d (entered %d, returned %d)"
              % (cur, slot, entered, returned))
    elif entered:
        print("        handlers: %d entered, %d returned" % (entered, returned))
    if g(22):
        print("        *** RESULT ARRAY OVERWRITTEN during the sweep (canary %d): "
              "every number above is suspect ***" % g(22))
    else:
        print("        canaries intact -- nothing wrote through the result array")
    if g(67):
        print("        *** %d handler call(s) SKIPPED as known-hanging (0x16BF0): the "
              "frame is INCOMPLETE ***" % g(67))
    print("        port_call68 misses %d; dispatch entered %d returned %d"
          % (g(13), g(11), g(12)))
    ps = [(k, g(56 + k)) for k in range(8) if g(56 + k)]
    if ps:
        print("        misses per call SITE: %s"
              % "  ".join("site %d = %d" % (k, v) for k, v in ps))
    miss = [g(40 + k) for k in range(8) if g(48 + k)]
    if miss:
        print("        distinct missed targets: %s"
              % " ".join("0x%05X" % m for m in miss))
    if g(23) != 0x5641524D:
        print("        (no VARM marker: the variant block has not been written)")
    names = ["control (runs nothing)",
             "+ irq4_handler",
             "+ FUN_00003aae",
             "+ FUN_0000144a",
             "+ object dispatch",
             "FUN_00003aae alone (no irq4)",
             "  and FUN_0000144a",
             "  and object dispatch"]
    base = g(24)
    print("        cumulative ladder -- each line adds ONE call to the line above:")
    prev = base
    for k in range(8):
        d = g(24 + k)
        step = "" if k in (0, 5) else "  this call: %+d" % (d - prev)
        print("        %-30s %5d wrong  %+5d vs control%s" % (names[k], d, d - base, step))
        prev = d if k != 4 else base
    print("        register-seed experiment (oracle registers at each dispatch):")
    print("        full frame, seeded            %5d wrong  %+5d vs control  (unseeded %d)"
          % (g(38), g(38) - base, g(28)))
    print("        slot 0 only, seeded           %5d wrong  %+5d vs control  (unseeded %d)"
          % (g(39), g(39) - base, g(32)))
    print("        per-slot: the full frame, dispatching ONE object slot")
    for k, sl in enumerate((0, 16, 17, 40, 41, 63)):
        d = g(32 + k)
        print("        only slot %-2d                     %5d wrong  %+5d vs control"
              % (sl, d, d - base))
