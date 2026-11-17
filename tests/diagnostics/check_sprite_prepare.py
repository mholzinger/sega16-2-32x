#!/usr/bin/env python3
"""Exercise the actual prepared-packet component with scalar color and bounds checks."""
from pathlib import Path
import os
import subprocess
import tempfile
with tempfile.TemporaryDirectory(prefix='sprite-prepare-') as tmp:
    exe = str(Path(tmp) / 'check')
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c99', '-O1', '-g',
                    '-fsanitize=address,undefined',
                    str(Path(__file__).with_suffix('.c')), '-o', exe], check=True)
    subprocess.run([exe], check=True)
