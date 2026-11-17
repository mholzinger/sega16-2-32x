#!/usr/bin/env python3
"""Actual source-row component versus independent per-cell page selection."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='plane-rows-') as tmp:
    exe=str(Path(tmp)/'check')
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O1','-g',
                    '-fsanitize=address,undefined',str(Path(__file__).with_suffix('.c')),'-o',exe],check=True)
    subprocess.run([exe,*sys.argv[1:]],check=True)
