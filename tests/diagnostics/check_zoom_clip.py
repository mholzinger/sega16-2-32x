#!/usr/bin/env python3
"""Build/run actual optional zoom clip kernel against scalar carry oracle.

No ROM assets or target builds required. CC defaults to clang.
"""
import os
from pathlib import Path
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="zoom-clip-") as tmp:
    exe=Path(tmp)/"check"
    subprocess.run([os.environ.get("CC", "clang"), "-std=c99", "-O2", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(Path(__file__).with_suffix(".c")),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
