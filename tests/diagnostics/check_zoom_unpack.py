#!/usr/bin/env python3
"""Actual C expander vs raw bake packets, with ASan/UBSan bounded buffers.

Usage: check_zoom_unpack.py RAW_ZB1_ATLAS. Input is an ignored local ROM asset.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from bake_zoom import compact_packet, unpack_compact
from s16art import unpack_frame

raw=Path(sys.argv[1]).read_bytes()
magic,n,size,_=struct.unpack_from('>4sIII',raw)
assert magic==b'ZB1\0' and len(raw)==size
vectors=bytearray(struct.pack('>I',n))
for i in range(n):
    off=struct.unpack_from('>I',raw,24+12*i)[0]
    rows,ends,end=unpack_frame(raw,off)
    frame=raw[off:end]
    packed=compact_packet(frame)
    a,b,c=unpack_compact(packed,0)
    assert a==rows and b==ends and c==len(packed)
    vectors+=struct.pack('>II',len(packed)//4,len(frame)//4)+packed+frame
with tempfile.TemporaryDirectory(prefix='zoom-unpack-') as tmp:
    exe=Path(tmp)/'check';src=Path(tmp)/'vectors.bin';src.write_bytes(vectors)
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O1','-g',
                    '-fsanitize=address,undefined',str(Path(__file__).with_suffix('.c')),
                    '-o',str(exe)],check=True)
    subprocess.run([str(exe),str(src)],check=True)
