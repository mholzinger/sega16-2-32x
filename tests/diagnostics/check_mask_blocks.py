#!/usr/bin/env python3
"""Build/run the actual portable C mask path with sanitizer and scalar reference.
Optional args: decoded tiles.bin followed by native snapshot prefixes.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='mask-blocks-') as tmp:
    exe = str(Path(tmp) / 'check')
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c99', '-O1', '-g',
                    '-fsanitize=address,undefined',
                    str(Path(__file__).with_suffix('.c')), '-o', exe], check=True)
    subprocess.run([exe, *sys.argv[1:]], cwd=root, check=True)
