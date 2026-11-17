#!/usr/bin/env python3
"""Actual C chunk decoder vs scalar/store-group oracles, ASan/UBSan.

Also uses selected local native smoke snapshots when their ignored assets are
available. No target ROM build or emulator invocation.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

src=Path(__file__).resolve()
root=src.parents[2]
with tempfile.TemporaryDirectory(prefix="sprite-chunk-") as tmp:
    exe=Path(tmp)/"check"
    subprocess.run([os.environ.get("CC","clang"),"-std=c99","-O2","-g",
                    "-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",str(src.with_suffix(".c")),
                    "-o",str(exe)],check=True)
    rom=root/"sh_src/sprites.bin"
    snapshots=[root/p for p in (
        "build/codex-smoke-health/arcade-smoke-clear/ev_7544_sprite.bin",
        "build/codex-clip-smoke/arcade-full-40k/ev_30677_sprite.bin",
        "build/codex-clip-smoke/arcade-full-40k/ev_33614_sprite.bin")]
    records=[]
    if rom.exists():
        for path in snapshots:
            if not path.exists():continue
            block=path.read_bytes()
            for offset in range(0,1024,16):
                e=struct.unpack_from(">8H",block,offset)
                if e[2]&0x8000:break
                top,bottom=e[0]&255,min(e[0]>>8,224)
                hz,vz=e[5]&31,(e[5]>>5)&31
                if e[2]&0x4000 or top>=bottom or not hz:continue
                height=bottom-top
                pitch=(e[2]&255)-(256 if e[2]&128 else 0)
                for row in range(0,height,4):
                    records.append(struct.pack(">BIhBBBBBB",(e[4]>>8)&7,e[3],pitch,
                        bool(e[2]&256),hz,vz,height,row,min(4,height-row)))
    args=[str(exe)]
    if records:
        data=rom.read_bytes()
        vectors=Path(tmp)/"native.bin"
        vectors.write_bytes(struct.pack(">III",0x53435631,len(data)//2,len(records))+
                            data+b"".join(records))
        args.append(str(vectors))
    subprocess.run(args,check=True)
    print(f"Native smoke chunks: {len(records)} descriptors x8 output alignments")
