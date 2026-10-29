#!/usr/bin/env python3
"""Pack N consecutive L2 oracle captures into a seed + per-frame DELTA blob.

    python3 tools/l2_pack.py docs/proof/l2m docs/proof/l2m/deltas.bin

WHY DELTAS. .portdata is 32 KB (sh_src/mars.ld: 0x02380000 -> 0x02388000), which is
exactly the two 16 KB captures the ONE-frame gate uses and no more. A multi-frame
gate needs N+1 captures and would want 500 KB. But a gameplay frame changes only
27-47 of 16,384 bytes (measured over 30 frames), so frame k is the seed plus a few
hundred byte-writes: 30 frames cost under 5 KB.

BLOB FORMAT, big-endian (the SH-2 reads it in place, no conversion):

    u32 nframes                 number of DELTA frames (captures - 1)
    u32 idx[nframes]            byte offset of frame k's block (k = 1..nframes)
    per frame:  u32 count
                count x (u16 offset, u16 value)      SORTED BY OFFSET

Offsets are from 0xFFC000. Sorted because the driver scores a frame with one
ascending pass over work RAM and a cursor per frame's list (a 30-way merge), which
is what lets it reconstruct expected(k) with no 16 KB buffer -- there is only ~3 KB
of headroom under the region guard at 0x06019000.
"""
import glob, os, struct, sys

src = sys.argv[1] if len(sys.argv) > 1 else 'docs/proof/l2m'
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(src, 'deltas.bin')

# MAX FRAMES, because .portdata's size sets where .portcode STARTS. Adding the
# arcade's real 15-call frame body (FULL-PORT.md 32) overflowed the 4 MB cart by
# 2,900 bytes, and the cheapest space is the tail of the delta chain: the gate loses
# frames, the port gains two thirds of its frame.
MAXF = int(os.environ.get('L2_MAXF', '0'))
files = sorted(glob.glob(os.path.join(src, 'wram_*.bin')))
if MAXF:
    files = files[:MAXF + 1]
if len(files) < 2:
    raise SystemExit('l2_pack: need at least 2 captures in %s, found %d' % (src, len(files)))
imgs = []
for f in files:
    b = open(f, 'rb').read()
    if len(b) != 16384:
        raise SystemExit('l2_pack: %s is %d bytes, expected 16384' % (f, len(b)))
    imgs.append(b)

blocks = []
for k in range(1, len(imgs)):
    a, b = imgs[k - 1], imgs[k]
    rec = [(i, b[i]) for i in range(16384) if a[i] != b[i]]
    blk = struct.pack('>I', len(rec))
    for off, val in rec:                      # already ascending by construction
        blk += struct.pack('>HH', off, val)
    blocks.append(blk)

n = len(blocks)
head = struct.pack('>I', n)
base = 4 + 4 * n
idx, pos = b'', base
for blk in blocks:
    idx += struct.pack('>I', pos)
    pos += len(blk)
open(out, 'wb').write(head + idx + b''.join(blocks))

print('l2_pack: %d captures -> %d delta frames, %d bytes' % (len(imgs), n, pos))
print('  seed      %s' % files[0])
print('  changes/frame  min %d  max %d  mean %.1f'
      % (min(len(b) // 4 - 1 for b in blocks) + 1,
         max(len(b) // 4 - 1 for b in blocks) + 1,
         sum(len(b) // 4 - 1 for b in blocks) / float(n) + 1))
print('  -> %s' % out)
