#!/usr/bin/env python3
"""Relocate the Sega 32X security blob from cart 0x3F0 to cart 0x40400.

The blob is position-locked by five absolute internal refs (found by scan,
verified as lea/movea operands in the disassembly). All other absolute
values (0x880000 window bases, 0x8802A2 trampoline install) stay put.
"""
import struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0x40400 - 0x3F0

# (blob offset of the 32-bit value, expected old value)
REFS = [
    (0x005E, 0x04D4),
    (0x006C, 0x04E8),
    (0x00AE, 0x04C0),
    (0x00D8, 0x06BC),
    (0x01E4, 0x063E),
]

b = bytearray((ROOT / 'md_src' / 'sega_blob.bin').read_bytes())
assert len(b) == 0x410, f"blob size {len(b):#x} != 0x410"
for off, expect in REFS:
    v = struct.unpack_from('>I', b, off)[0]
    assert v == expect, f"blob+{off:#x}: {v:#x} != expected {expect:#x}"
    struct.pack_into('>I', b, off, v + DELTA)
(ROOT / 'md_src' / 'sega_blob_patched.bin').write_bytes(b)
print(f"sega_blob_patched.bin: {len(REFS)} refs relocated by +{DELTA:#x}")
