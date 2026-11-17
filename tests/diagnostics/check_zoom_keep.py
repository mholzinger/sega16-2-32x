#!/usr/bin/env python3
"""Check immutable source bytes and actual C copy under ASan/UBSan."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

src=Path(__file__).resolve()
header=src.parents[2]/"sh_src/r60/r60_zoom_keep.h"
initializer=header.read_text().split("= {",1)[1].split("};",1)[0]
data=bytes(int(v,16) for v in re.findall(r"0x([0-9a-f]+)",initializer))
digest=hashlib.sha256(data).hexdigest()
assert len(data)==512
assert digest=="9a65c1471eccf176da73009163530c8f628cef860c2c0a77f2f207abe4aaf0f3"
with tempfile.TemporaryDirectory(prefix="zoom-keep-") as tmp:
    exe=Path(tmp)/"check"
    subprocess.run([os.environ.get("CC","clang"),"-std=c99","-O2","-g",
                    "-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",str(src.with_suffix(".c")),
                    "-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print(f"Immutable source bytes: 512; SHA256 {digest}")
