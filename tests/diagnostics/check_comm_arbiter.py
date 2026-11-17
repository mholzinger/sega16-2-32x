#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).with_suffix('.c')
with tempfile.TemporaryDirectory(prefix='comm-arbiter-') as tmp:
    out=Path(tmp)/'check'
    subprocess.run(['clang','-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
