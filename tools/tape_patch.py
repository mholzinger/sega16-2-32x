#!/usr/bin/env python3
"""Replace the attract demo's first tape slot (game ROM 0x3E4B0-0x3EDAF,
768 frames x 3 raw port bytes; NOTES 43, LOOP-DECOMPILE 111) inside a
built .32x image. The game ROM sits at 0x300000 in the image (located
by content, tools/tape_patch.py checks it before writing).

    tools/tape_patch.py <in.32x> <tape.hex> <out.32x>
"""
import sys
GAME_BASE, SLOT, SLOT_LEN = 0x300000, 0x3E4B0, 768 * 3
src, tape, dst = sys.argv[1:4]
img = bytearray(open(src, 'rb').read())
orig = open('roms/altbeast/prog68k.bin', 'rb').read()[SLOT:SLOT + SLOT_LEN]
off = GAME_BASE + SLOT
assert bytes(img[off:off + SLOT_LEN]) == orig, "the image's tape slot does not hold the game's original tape"
frames = [bytes.fromhex(l.strip()) for l in open(tape) if l.strip() and not l.startswith('#')]
data = b''.join(frames)
assert len(data) == SLOT_LEN, f"tape is {len(data)} bytes, slot is {SLOT_LEN}"
img[off:off + SLOT_LEN] = data
open(dst, 'wb').write(img)
print(f"{dst}: tape written at image offset {off:#x} ({len(frames)} frames)")
