#!/usr/bin/env python3
"""REBUILD Phase 0 / M1 decoder — arcade 68K frame occupancy.

Reads error.log lines `M1S <beamy hex> <frame hex>` written by
tools/m1_occupancy.lua (a breakpoint on every `stop #$2300` site).

Model: the game halts at a STOP until the vint (line 224) wakes it.
Idle lines for a stop at line V: (224-V) if V<224 else (262-V)+224.
Multiple stops per frame = re-idling after a mid-frame IRQ wake; sum
spans conservatively by taking the FIRST stop only (later stops sit
inside the same pre-vint span). Frames with NO stop = 100% busy.

Occupancy = 1 - idle/262. The M1 question: what does the busy-frame
population look like, especially in gameplay (frames > 1100)?
The MD 68K runs the game at 0.77x arcade clock minus handler share:
a frame needs occupancy <= ~0.72 to fit 60Hz on the port.
"""
import sys
import collections

LINES = 262
VINT = 224

path = sys.argv[1] if len(sys.argv) > 1 else "error.log"
first_stop = {}
hits_per_frame = collections.Counter()
for line in open(path, errors="ignore"):
    if not line.startswith("M1S "):
        continue
    try:
        _, v_hex, f_hex = line.split()
        v, f = int(v_hex, 16), int(f_hex, 16)
    except ValueError:
        continue
    hits_per_frame[f] += 1
    if f not in first_stop or v < first_stop[f]:
        first_stop[f] = v

if not first_stop:
    sys.exit("no M1S hits in the log")

frames = sorted(first_stop)
f_lo, f_hi = frames[0], frames[-1]


def occupancy(f):
    # wait-entry model (hook 0x397E): work runs from the vint (line
    # 224) to the wait entry at line V; multi-frame waits re-enter
    # the hook just after each wake, reading as handler-only frames.
    if f not in first_stop:
        return 1.0
    v = first_stop[f]
    return ((v - VINT) % LINES) / LINES


def report(name, lo, hi):
    occ = [occupancy(f) for f in range(lo, hi)]
    if not occ:
        return
    occ_s = sorted(occ)
    busy100 = sum(1 for f in range(lo, hi) if f not in first_stop)
    over = sum(1 for o in occ if o > 0.72)
    n = len(occ)
    print(f"{name}: frames={n} mean occ={sum(occ)/n*100:.1f}% "
          f"p50={occ_s[n//2]*100:.1f}% p95={occ_s[int(n*.95)]*100:.1f}% "
          f"p99={occ_s[int(n*.99)]*100:.1f}% max={occ_s[-1]*100:.1f}%")
    print(f"  100%-busy frames (no STOP): {busy100} "
          f"({100.0*busy100/n:.2f}%)   frames over the 0.72 port "
          f"budget: {over} ({100.0*over/n:.2f}%)")


print(f"M1 ARCADE OCCUPANCY — stop-hits logged over frames "
      f"{f_lo}..{f_hi}; multi-stop frames: "
      f"{sum(1 for c in hits_per_frame.values() if c > 1)}")
report("ALL      ", f_lo, f_hi)
report("attract  ", f_lo, min(1100, f_hi))
report("gameplay ", 1100, f_hi)
