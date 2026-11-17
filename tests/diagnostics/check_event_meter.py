#!/usr/bin/env python3
"""Compile the actual generic accumulator with ASan/UBSan; no ROM/build assets."""
import os
from pathlib import Path
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="event-meter-") as tmp:
    for metrics in (1, 6, 8, 32):
        exe = Path(tmp) / f"check-{metrics}"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c99", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        f"-DR60_EM_METRICS={metrics}", str(Path(__file__).with_suffix(".c")),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
