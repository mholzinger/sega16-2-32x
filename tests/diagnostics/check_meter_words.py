#!/usr/bin/env python3
"""Actual diagnostic word painter against a per-pixel oracle, ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
with tempfile.TemporaryDirectory(prefix='meter-words-') as tmp:
    exe=str(Path(tmp)/'check')
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O1','-g',
                    '-fsanitize=address,undefined',str(Path(__file__).with_suffix('.c')),'-o',exe],check=True)
    subprocess.run([exe],check=True)
