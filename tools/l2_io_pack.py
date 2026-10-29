#!/usr/bin/env python3
"""Pack the per-frame System 16 I/O captures into one blob for .portdata.

    python3 tools/l2_io_pack.py docs/proof/l2m docs/proof/l2m/io.bin

WHY A SEPARATE BLOB. A frame's inputs are the work-RAM seed PLUS the controller and
dip state, and only the first was ever captured. The port's MMIO stub returned a
constant, so every input-derived cell was wrong by construction -- 9 of the 19 cells
the rotated real frame gets wrong at the seed frame (FULL-PORT.md 38), written by
FUN_00001366 straight out of the I/O window:

    DAT_ffffe028 = ~DAT_00c41003;      /* player 1 */
    DAT_ffffe0a8 = ~DAT_00c41007;      /* player 2 */

Worse, the gate PAID the hand-rolled four-call body for not reading the controller
at all. This blob is kept separate from deltas.bin so the delta format -- which the
driver's scorer walks as a sorted merge -- is not disturbed.

NOT CIRCULAR. The controller state is an INPUT to the frame, declared like the seed,
captured by tools/l2_oracle.lua from MAME's ioport layer (no bus access, no latch
consumed) and never read back out of the answer. It is CHECKED against the answer,
which is a different thing: `~io(N)[0xC41003]` must equal work RAM's 0xFFE028 at
frame N+1, and does for 239 of 257 frames (the other 18 are frames where
FUN_00001366's write is gated off); player two is 257 of 257.

BLOB FORMAT, big-endian, read in place by the SH-2:

    u32 'L2IO'
    u32 nframes                 frames 0..nframes-1, frame k = capture base+k
    u32 nports
    u32 port[nports]            the 68K addresses, in capture order
    u8  val[nframes][nports]    padded to a 4-byte boundary per frame

A port with no ioport behind it (0xC40001, 0xC43001, 0xC43007 -- display/watchdog
and the mapper, not inputs) is recorded as 0xFF, which is what the port's own stub
fill already holds; this file says so rather than letting a fabricated byte look
like a measurement.
"""
import glob, os, re, struct, sys

PORTS = [0xC40001, 0xC41001, 0xC41003, 0xC41005, 0xC41007,
         0xC42001, 0xC42003, 0xC43001, 0xC43007]
UNCAPTURED = {0xC40001, 0xC41005, 0xC43001, 0xC43007}

src = sys.argv[1] if len(sys.argv) > 1 else 'docs/proof/l2m'
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(src, 'io.bin')

files = sorted(glob.glob(os.path.join(src, 'io_*.bin')))
if not files:
    raise SystemExit('l2_io_pack: no io_*.bin in %s -- run tools/l2_oracle.lua '
                     '(it writes them beside the work-RAM dumps)' % src)
nums = [int(re.search(r'io_(\d+)\.bin$', f).group(1)) for f in files]
base = nums[0]
if nums != list(range(base, base + len(nums))):
    raise SystemExit('l2_io_pack: frames are not consecutive from %d' % base)

MAXF = int(os.getenv('L2_MAXF') or '0')
if MAXF:
    files, nums = files[:MAXF], nums[:MAXF]

rec = []
for f in files:
    d = open(f, 'rb').read()
    if len(d) != len(PORTS):
        raise SystemExit('l2_io_pack: %s is %d bytes, expected %d'
                         % (f, len(d), len(PORTS)))
    rec.append(d)

stride = (len(PORTS) + 3) & ~3
blob = struct.pack('>III', 0x4C32494F, len(rec), len(PORTS))
blob += b''.join(struct.pack('>I', p) for p in PORTS)
for d in rec:
    blob += d + b'\0' * (stride - len(d))
open(out, 'wb').write(blob)
print('l2_io_pack: %d frame(s) from %d, %d port(s), %d bytes -> %s'
      % (len(rec), base, len(PORTS), len(blob), out))
print('  captured:   ' + ' '.join('0x%06X' % p for p in PORTS if p not in UNCAPTURED))
print('  uncaptured: ' + ' '.join('0x%06X' % p for p in PORTS if p in UNCAPTURED)
      + '  (not input ports; recorded 0xFF)')
