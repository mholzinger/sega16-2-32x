#!/usr/bin/env python3
"""Host sanitizer check of the exact two-owner probe helper; no shared builds."""
import os
from pathlib import Path
import subprocess
import tempfile
with tempfile.TemporaryDirectory(prefix="event-probe-") as tmp:
    exe = Path(tmp) / "check"
    subprocess.run([os.environ.get("CC", "clang"), "-std=c99", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(Path(__file__).with_suffix(".c")), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
